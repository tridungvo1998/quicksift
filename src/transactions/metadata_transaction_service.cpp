// OWNER: Metadata transaction thread, one decoder handoff per batch, the interactive
// (urgent) edit lane, and result ownership.
//
// Locking: mutex_ guards plan_/urgentPlan_/results_/running_/waiting* and the callback
// pointers. It is never held while calling a callback (write, gate, stamps, preflight,
// completion sink), so a callback may call back into Busy()/Progress() freely.
#include "metadata_transaction_service.h"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <type_traits>
#include <utility>

namespace quicksift::transactions {
namespace fs = std::filesystem;

static_assert(std::is_nothrow_move_constructible_v<MetadataTransactionPlan>);
static_assert(std::is_nothrow_move_assignable_v<MetadataTransactionPlan>);
static_assert(std::is_nothrow_move_constructible_v<MetadataTransactionResult>);
static_assert(std::is_nothrow_move_assignable_v<MetadataTransactionResult>);

MetadataTransactionService::MetadataTransactionService() {
    // Start only after every field read by Run() has completed construction.
    thread_ = std::thread([this] { Run(); });
}

MetadataTransactionService::~MetadataTransactionService() {
    Stop();
}

void MetadataTransactionService::RecomputeBusyLocked() noexcept {
    busy_.store(running_ || plan_.has_value() || urgentPlan_.has_value() || !results_.empty(),
        std::memory_order_release);
}

void MetadataTransactionService::Configure(MetadataTransactionCallbacks callbacks) {
    std::shared_ptr<const MetadataTransactionCallbacks> ownedCallbacks;
    if (callbacks.beginExclusive && callbacks.endExclusive && callbacks.write) {
        if (callbacks.retryTimeDivisor == 0) callbacks.retryTimeDivisor = 1;
        ownedCallbacks = std::make_shared<const MetadataTransactionCallbacks>(
            std::move(callbacks));
    }
    std::lock_guard lock(mutex_);
    // Configuration is immutable for an accepted transaction. Refuse a late
    // replacement rather than letting a queued plan observe a different writer.
    if (stopping_ || busy_.load(std::memory_order_relaxed) || plan_ || urgentPlan_ ||
        !results_.empty()) return;
    callbacks_ = std::move(ownedCallbacks);
}

bool MetadataTransactionService::Submit(MetadataTransactionPlan&& plan) {
    std::lock_guard lock(mutex_);
    if (stopping_ || !callbacks_ || busy_.load(std::memory_order_relaxed) ||
        plan_ || urgentPlan_ || !results_.empty()) return false;
    progressCompleted_.store(0, std::memory_order_relaxed);
    progressTotal_.store(plan.changes.size(), std::memory_order_release);
    plan_.emplace(std::move(plan));
    cancellationRequested_.store(false, std::memory_order_release);
    skipWaitRequested_.store(false, std::memory_order_release);
    busy_.store(true, std::memory_order_release);
    condition_.notify_all();
    return true;
}

bool MetadataTransactionService::SubmitUrgent(MetadataTransactionPlan&& plan) {
    std::lock_guard lock(mutex_);
    // A cancelled/closing run must not pick up new work; the caller keeps the edit
    // pending and submits it normally once the cancelled plan has drained.
    if (stopping_ || !callbacks_ || urgentPlan_ ||
        cancellationRequested_.load(std::memory_order_acquire)) return false;
    urgentPlan_.emplace(std::move(plan));
    busy_.store(true, std::memory_order_release);
    condition_.notify_all(); // wakes Run() or a lock-resolution wait in Execute()
    return true;
}

bool MetadataTransactionService::UrgentSlotFree() const {
    std::lock_guard lock(mutex_);
    return !stopping_ && !urgentPlan_ && !cancellationRequested_.load(std::memory_order_acquire);
}

void MetadataTransactionService::CancelCurrent() noexcept {
    cancellationRequested_.store(true, std::memory_order_release);
    // Wake a lock-resolution wait. notify without the mutex is safe: the waiter's
    // predicate re-reads the atomic, and its wait_for timeout bounds a missed wake.
    condition_.notify_all();
}

void MetadataTransactionService::SkipWaiting() noexcept {
    skipWaitRequested_.store(true, std::memory_order_release);
    condition_.notify_all();
}

void MetadataTransactionService::SetCompletionSink(std::function<void()> sink) {
    std::shared_ptr<const std::function<void()>> ownedSink;
    if (sink) ownedSink = std::make_shared<const std::function<void()>>(std::move(sink));
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    completionSink_ = std::move(ownedSink);
}

std::optional<MetadataTransactionProgress> MetadataTransactionService::Progress() const noexcept {
    if (!busy_.load(std::memory_order_acquire)) return std::nullopt;
    MetadataTransactionProgress progress;
    progress.totalItems = progressTotal_.load(std::memory_order_acquire);
    progress.completedItems = std::min(progressCompleted_.load(std::memory_order_acquire),
        progress.totalItems);
    try {
        std::lock_guard lock(mutex_);
        if (!waitingPath_.empty()) {
            progress.waitingPath = waitingPath_;
            progress.waitingSerial = waitingSerial_;
            progress.waitingExternal = waitingExternal_.load(std::memory_order_acquire);
            progress.waitedFor = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - waitingSince_);
        }
    } catch (...) {
        progress.waitingPath.clear(); // counts remain truthful
    }
    return progress;
}

std::optional<MetadataTransactionResult> MetadataTransactionService::TakeResult() {
    std::lock_guard lock(mutex_);
    if (results_.empty()) return std::nullopt;
    std::optional<MetadataTransactionResult> output(std::move(results_.front()));
    results_.pop_front();
    RecomputeBusyLocked();
    return output;
}

void MetadataTransactionService::Stop() noexcept {
    try {
        {
            std::lock_guard lock(mutex_);
            if (stopping_) return;
            stopping_ = true;
            completionSink_.reset();
            cancellationRequested_.store(true, std::memory_order_release);
            plan_.reset();
            urgentPlan_.reset();
            results_.clear();
        }
        // Wakes Run() and any interruptible lock-resolution wait (bounded join).
        condition_.notify_all();
        if (thread_.joinable()) thread_.join();
        busy_.store(false, std::memory_order_release);
    } catch (...) {
        // Destruction is a no-throw process boundary.
    }
}

void MetadataTransactionService::PublishResult(MetadataTransactionResult&& result,
    bool endsRun) {
    std::shared_ptr<const std::function<void()>> sink;
    {
        std::lock_guard lock(mutex_);
        if (endsRun) running_ = false;
        if (stopping_) return;
        results_.push_back(std::move(result));
        RecomputeBusyLocked();
        sink = completionSink_;
    }
    if (sink) {
        try { (*sink)(); } catch (...) { /* Timer draining retains ownership. */ }
    }
}

namespace {
void FinalizeAfterException(MetadataTransactionResult& result) noexcept {
    // Execute moves authoritative state into result before performing any
    // write. Never overwrite its partial completion bitmap here.
    result.failed = true;
    const std::size_t incomplete = static_cast<std::size_t>(std::count(
        result.completed.begin(), result.completed.end(), false));
    result.failureCount = incomplete >
        std::numeric_limits<std::size_t>::max() - result.initialFailureCount ?
        std::numeric_limits<std::size_t>::max() :
        result.initialFailureCount + incomplete;
}
} // namespace

void MetadataTransactionService::Run() noexcept {
    for (;;) {
        MetadataTransactionPlan plan;
        bool urgent = false;
        std::shared_ptr<const MetadataTransactionCallbacks> callbacks;
        try {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] {
                return stopping_ || plan_.has_value() || urgentPlan_.has_value();
            });
            if (stopping_) return;
            // Interactive edits first: they are small and the user is looking at them.
            if (urgentPlan_) {
                plan = std::move(*urgentPlan_);
                urgentPlan_.reset();
                urgent = true;
                // A cancel aimed at the previous (finished) plan must not discard an
                // edit the user made afterwards. Close/shutdown still waits for it:
                // it is a handful of files at most.
                cancellationRequested_.store(false, std::memory_order_release);
                skipWaitRequested_.store(false, std::memory_order_release);
            } else {
                plan = std::move(*plan_);
                plan_.reset();
            }
            running_ = true;
            callbacks = callbacks_;
        } catch (...) {
            return; // lock failure: nothing sane left to do on this thread
        }

        MetadataTransactionResult result;
        result.urgent = urgent;
        if (callbacks) {
            try {
                Execute(std::move(plan), *callbacks, result, !urgent);
            } catch (...) {
                FinalizeAfterException(result);
            }
        } else {
            FinalizeAfterException(result);
        }
        try {
            PublishResult(std::move(result), true);
        } catch (...) {
            try {
                std::lock_guard lock(mutex_);
                running_ = false;
                RecomputeBusyLocked();
            } catch (...) {}
        }
    }
}

} // namespace quicksift::transactions
