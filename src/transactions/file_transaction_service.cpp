// OWNER: Serialized transaction thread, cancellation, and owned result delivery.
#include "file_transaction_service.h"

#include <stdexcept>
#include <limits>
#include <type_traits>
#include <utility>

namespace quicksift::transactions {

static_assert(std::is_nothrow_move_constructible_v<FileTransactionPlan>);
static_assert(std::is_nothrow_move_assignable_v<FileTransactionPlan>);
static_assert(std::is_nothrow_move_constructible_v<FileTransactionResult>);
static_assert(std::is_nothrow_move_assignable_v<FileTransactionResult>);

FileTransactionService::FileTransactionService(
    std::unique_ptr<VerifiedFileOperations> operations)
    : operations_(std::move(operations)) {
    if (!operations_) throw std::invalid_argument("file transaction operations must not be null");
    operations_->SetCancellationSource(&cancellationRequested_);
    // Start only after every field read by Run() has completed construction.
    thread_ = std::thread([this] { Run(); });
}

FileTransactionService::~FileTransactionService() {
    Stop();
}

bool FileTransactionService::Submit(FileTransactionPlan&& plan) {
    std::lock_guard lock(mutex_);
    if (stopping_ || busy_.load(std::memory_order_relaxed) || plan_ || result_) return false;
    if (plan.mode == FileTransactionMode::Transfer) {
        FileTransactionProgress progress;
        progress.token = plan.token;
        progress.copy = plan.copy;
        progress.deleteToFolder = plan.deleteToFolder;
        for (const TransferGroup& group : plan.groups) {
            if (group.owners.size() > std::numeric_limits<std::size_t>::max() - progress.totalFiles)
                return false;
            progress.totalFiles += group.owners.size();
        }
        progress_ = progress;
        progressStartedAt_ = std::chrono::steady_clock::now();
    } else {
        progress_.reset();
        progressStartedAt_ = {};
    }
    plan_.emplace(std::move(plan));
    cancellationRequested_.store(false, std::memory_order_release);
    busy_.store(true, std::memory_order_release);
    condition_.notify_one();
    return true;
}


std::optional<FileTransactionProgress> FileTransactionService::Progress() const {
    std::lock_guard lock(mutex_);
    if (!progress_) return std::nullopt;
    FileTransactionProgress snapshot = *progress_;
    if (progressStartedAt_ != std::chrono::steady_clock::time_point{}) {
        snapshot.elapsedSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - progressStartedAt_).count();
    }
    return snapshot;
}

void FileTransactionService::SetCompletionSink(std::function<void()> sink) {
    std::shared_ptr<const std::function<void()>> ownedSink;
    if (sink) ownedSink = std::make_shared<const std::function<void()>>(std::move(sink));
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    completionSink_ = std::move(ownedSink);
}

std::optional<FileTransactionResult> FileTransactionService::TakeResult() {
    std::lock_guard lock(mutex_);
    if (!result_) return std::nullopt;
    std::optional<FileTransactionResult> output(std::move(result_));
    result_.reset();
    progress_.reset();
    progressStartedAt_ = {};
    busy_.store(false, std::memory_order_release);
    return output;
}

void FileTransactionService::Stop() noexcept {
    try {
        {
            std::lock_guard lock(mutex_);
            if (stopping_) return;
            stopping_ = true;
            completionSink_.reset();
            cancellationRequested_.store(true, std::memory_order_release);
            plan_.reset();
            result_.reset();
            progress_.reset();
            progressStartedAt_ = {};
        }
        condition_.notify_all();
        if (thread_.joinable()) thread_.join();
        busy_.store(false, std::memory_order_release);
    } catch (...) {
        // Destruction must not throw. The process-level crash handler remains the
        // last-resort diagnostic boundary for a failing C++ runtime.
    }
}

void FileTransactionService::Run() noexcept {
    for (;;) {
        FileTransactionPlan plan;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || plan_.has_value(); });
            if (stopping_) return;
            plan = std::move(*plan_);
            plan_.reset();
        }

        FileTransactionResult result;
        try {
            result = ExecuteFileTransaction(plan, *operations_, cancellationRequested_,
                [this, token = plan.token](std::size_t completedFiles, std::size_t totalFiles) {
                    std::lock_guard lock(mutex_);
                    if (stopping_ || !progress_ || progress_->token != token) return;
                    progress_->completedFiles = completedFiles;
                    progress_->totalFiles = totalFiles;
                });
        } catch (...) {
            // Move the still-owned plan into a non-allocating emergency envelope.
            // Missing replay flags are interpreted conservatively by history
            // partitioning, so no completed operation is invented here.
            result.token = plan.token;
            result.label = std::move(plan.label);
            result.mode = plan.mode;
            result.copy = plan.copy;
            result.deleteToFolder = plan.deleteToFolder;
            result.recordHistory = plan.recordHistory;
            if (plan.mode != FileTransactionMode::Transfer) {
                result.historyItems = std::move(plan.historyReplayItems);
            }
            result.failed = true;
        }

        std::shared_ptr<const std::function<void()>> sink;
        {
            std::lock_guard lock(mutex_);
            if (stopping_) return;
            result_.emplace(std::move(result));
            sink = completionSink_;
        }
        if (sink) {
            try { (*sink)(); } catch (...) { /* Results remain owned for timer draining. */ }
        }
    }
}

} // namespace quicksift::transactions
