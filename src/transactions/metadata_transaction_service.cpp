// OWNER: Metadata transaction thread, one decoder handoff per batch, and result ownership.
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

void MetadataTransactionService::Configure(MetadataTransactionCallbacks callbacks) {
    std::shared_ptr<const MetadataTransactionCallbacks> ownedCallbacks;
    if (callbacks.beginExclusive && callbacks.endExclusive && callbacks.write) {
        ownedCallbacks = std::make_shared<const MetadataTransactionCallbacks>(
            std::move(callbacks));
    }
    std::lock_guard lock(mutex_);
    // Configuration is immutable for an accepted transaction. Refuse a late
    // replacement rather than letting a queued plan observe a different writer.
    if (stopping_ || busy_.load(std::memory_order_relaxed) || plan_ || result_) return;
    callbacks_ = std::move(ownedCallbacks);
}

bool MetadataTransactionService::Submit(MetadataTransactionPlan&& plan) {
    std::lock_guard lock(mutex_);
    if (stopping_ || !callbacks_ || busy_.load(std::memory_order_relaxed) ||
        plan_ || result_) return false;
    plan_.emplace(std::move(plan));
    cancellationRequested_.store(false, std::memory_order_release);
    busy_.store(true, std::memory_order_release);
    condition_.notify_one();
    return true;
}

void MetadataTransactionService::SetCompletionSink(std::function<void()> sink) {
    std::shared_ptr<const std::function<void()>> ownedSink;
    if (sink) ownedSink = std::make_shared<const std::function<void()>>(std::move(sink));
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    completionSink_ = std::move(ownedSink);
}

std::optional<MetadataTransactionResult> MetadataTransactionService::TakeResult() {
    std::lock_guard lock(mutex_);
    if (!result_) return std::nullopt;
    std::optional<MetadataTransactionResult> output(std::move(result_));
    result_.reset();
    busy_.store(false, std::memory_order_release);
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
            result_.reset();
        }
        condition_.notify_all();
        if (thread_.joinable()) thread_.join();
        busy_.store(false, std::memory_order_release);
    } catch (...) {
        // Destruction is a no-throw process boundary.
    }
}

void MetadataTransactionService::Run() noexcept {
    for (;;) {
        MetadataTransactionPlan plan;
        std::shared_ptr<const MetadataTransactionCallbacks> callbacks;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || plan_.has_value(); });
            if (stopping_) return;
            plan = std::move(*plan_);
            plan_.reset();
            callbacks = callbacks_;
        }

        MetadataTransactionResult result;
        try {
            Execute(std::move(plan), *callbacks, result);
        } catch (...) {
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
        std::shared_ptr<const std::function<void()>> sink;
        {
            std::lock_guard lock(mutex_);
            if (stopping_) return;
            result_.emplace(std::move(result));
            sink = completionSink_;
        }
        if (sink) {
            try { (*sink)(); } catch (...) { /* Timer draining retains ownership. */ }
        }
    }
}

} // namespace quicksift::transactions
