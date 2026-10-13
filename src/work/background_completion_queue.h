// OWNER: Cross-thread completion ownership. This module contains no Win32 message payloads.
#pragma once

#include "core/app_types.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iterator>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace quicksift::work {

enum class BackgroundCompletionKind {
    WorkResult,
    ScanBatch,
    ScanComplete,
    MetadataJobAborted
};

enum class ScanCompletionStatus {
    Succeeded,
    Partial,
    Failed,
};

struct BackgroundCompletion {
    BackgroundCompletionKind kind = BackgroundCompletionKind::WorkResult;
    std::shared_ptr<quicksift::core::WorkResult> workResult;
    std::shared_ptr<quicksift::core::ScanBatch> scanBatch;
    std::uint64_t generation = 0;
    ScanCompletionStatus scanStatus = ScanCompletionStatus::Succeeded;
    std::uint32_t nativeError = 0;
    std::wstring detail;
    std::chrono::steady_clock::time_point queuedAt = std::chrono::steady_clock::now();

    static BackgroundCompletion ForWorkResult(
        std::shared_ptr<quicksift::core::WorkResult> result) {
        BackgroundCompletion completion;
        completion.kind = BackgroundCompletionKind::WorkResult;
        completion.workResult = std::move(result);
        return completion;
    }

    static BackgroundCompletion ForScanBatch(
        std::shared_ptr<quicksift::core::ScanBatch> batch) {
        BackgroundCompletion completion;
        completion.kind = BackgroundCompletionKind::ScanBatch;
        completion.generation = batch ? batch->generation : 0;
        completion.scanBatch = std::move(batch);
        return completion;
    }

    static BackgroundCompletion ForGeneration(
        BackgroundCompletionKind kindValue, std::uint64_t generationValue) {
        BackgroundCompletion completion;
        completion.kind = kindValue;
        completion.generation = generationValue;
        return completion;
    }

    static BackgroundCompletion ForScanCompletion(std::uint64_t generationValue,
        ScanCompletionStatus status, std::uint32_t error = 0,
        std::wstring detailValue = {}) {
        BackgroundCompletion completion;
        completion.kind = BackgroundCompletionKind::ScanComplete;
        completion.generation = generationValue;
        completion.scanStatus = status;
        completion.nativeError = error;
        completion.detail = std::move(detailValue);
        return completion;
    }
};

static_assert(noexcept(std::declval<std::deque<BackgroundCompletion>&>().swap(
    std::declval<std::deque<BackgroundCompletion>&>())));

class BackgroundCompletionQueue {
public:
    explicit BackgroundCompletionQueue(std::size_t maximumEntries = 8192)
        : maximumEntries_(maximumEntries == 0 ? 1 : maximumEntries) {}

    BackgroundCompletionQueue(const BackgroundCompletionQueue&) = delete;
    BackgroundCompletionQueue& operator=(const BackgroundCompletionQueue&) = delete;

    [[nodiscard]] bool Push(BackgroundCompletion completion, bool* transitionedFromEmpty = nullptr) {
        std::lock_guard lock(mutex_);
        if (closed_ || queue_.size() >= maximumEntries_) return false;
        const bool wasEmpty = queue_.empty();
        queue_.push_back(std::move(completion));
        if (transitionedFromEmpty) *transitionedFromEmpty = wasEmpty;
        return true;
    }

    // Terminal state transitions must not be suppressed by the producer's own
    // cancellation flag. They may exceed the normal work-item bound briefly so
    // the UI can always leave its pending state after a startup/worker failure.
    [[nodiscard]] bool PushTerminal(BackgroundCompletion completion, bool* transitionedFromEmpty = nullptr) {
        std::lock_guard lock(mutex_);
        if (closed_) return false;
        const bool wasEmpty = queue_.empty();
        queue_.push_back(std::move(completion));
        if (transitionedFromEmpty) *transitionedFromEmpty = wasEmpty;
        return true;
    }

    // Critical producers use bounded blocking rather than dropping a completion.
    // The periodic timeout observes shutdown even when no consumer can notify.
    [[nodiscard]] bool PushWait(BackgroundCompletion completion,
        const std::atomic<bool>& stopRequested, bool* transitionedFromEmpty = nullptr) {
        std::unique_lock lock(mutex_);
        while (!closed_ && queue_.size() >= maximumEntries_ &&
            !stopRequested.load(std::memory_order_acquire)) {
            spaceAvailable_.wait_for(lock, std::chrono::milliseconds(25));
        }
        if (closed_ || stopRequested.load(std::memory_order_acquire)) return false;
        const bool wasEmpty = queue_.empty();
        queue_.push_back(std::move(completion));
        if (transitionedFromEmpty) *transitionedFromEmpty = wasEmpty;
        return true;
    }

    // Transfers ownership without allocating after removing items from the
    // authoritative queue. This prevents low-memory drain failures from silently
    // destroying completions that producers already committed.
    // The caller constructs the empty destination before this function acquires
    // ownership. If that construction fails, the authoritative queue remains
    // untouched. The subsequent deque swap is statically required to be noexcept.
    void DrainInto(std::deque<BackgroundCompletion>& destination) noexcept {
        try {
            std::lock_guard lock(mutex_);
            destination.swap(queue_);
        } catch (...) {
            return;
        }
        spaceAvailable_.notify_all();
    }


    // Removes one completion in latency order. Terminal state and interactive
    // pixels outrank speculative results, while FIFO order is retained within a
    // priority class. This lets the UI enforce a frame-time budget without
    // allowing a large predictive burst to sit ahead of the current image.
    [[nodiscard]] bool TryPopNext(BackgroundCompletion& destination) noexcept {
        try {
            std::lock_guard lock(mutex_);
            if (queue_.empty()) return false;
            const auto now = std::chrono::steady_clock::now();
            // 0.9.4.0: single pass. The former implementation re-scanned the queue
            // prefix (std::any_of) for every ScanComplete candidate, making one pop
            // O(n^2) while the UI thread held the queue lock. A scan terminal must
            // still never overtake a ScanBatch of the same generation, so remember the
            // generations whose batches precede the cursor (normally zero or one).
            std::uint64_t batchGenerations[4]{};
            std::size_t batchGenerationCount = 0;
            bool batchGenerationOverflow = false;
            const auto precedingBatch = [&](std::uint64_t generation) noexcept {
                if (batchGenerationOverflow) return true;
                for (std::size_t index = 0; index < batchGenerationCount; ++index) {
                    if (batchGenerations[index] == generation) return true;
                }
                return false;
            };
            const auto rankAt = [&](const BackgroundCompletion& completion) {
                if (completion.kind == BackgroundCompletionKind::ScanComplete &&
                    precedingBatch(completion.generation)) return 3;
                int rank = CompletionRank(completion);
                const auto age = now - completion.queuedAt;
                // Old low-priority completions still own memory reservations and
                // producer capacity. Age them into bounded service so a continuous
                // stream of current-image results cannot deadlock speculative lanes.
                if (age >= std::chrono::seconds(5)) rank = std::min(rank, 2);
                else if (age >= std::chrono::seconds(2)) rank = std::min(rank, 3);
                return rank;
            };
            auto best = queue_.end();
            int bestRank = 0;
            for (auto it = queue_.begin(); it != queue_.end(); ++it) {
                const int rank = rankAt(*it);
                if (best == queue_.end() || rank < bestRank) {
                    best = it;
                    bestRank = rank;
                    if (bestRank == 0) break;
                }
                if (it->kind == BackgroundCompletionKind::ScanBatch &&
                    !precedingBatch(it->generation)) {
                    if (batchGenerationCount < std::size(batchGenerations))
                        batchGenerations[batchGenerationCount++] = it->generation;
                    else
                        batchGenerationOverflow = true; // conservative: never reorder
                }
            }
            destination = std::move(*best);
            queue_.erase(best);
        } catch (...) {
            return false;
        }
        spaceAvailable_.notify_all();
        return true;
    }

    // Coalesced UI wake ownership (0.9.4.0). Producers call TryClaimWake() after a
    // successful push and post the payload-free wake message only when it returns
    // true, so at most one WM_APP_BACKGROUND_COMPLETION is outstanding no matter how
    // many workers complete at once. The UI releases the claim when it begins a
    // drain slice; a failed PostMessage must also release it so the next producer
    // retries. The regular UI timer pump remains a safety net for lost wakes.
    [[nodiscard]] bool TryClaimWake() noexcept {
        return !wakePending_.exchange(true, std::memory_order_acq_rel);
    }

    void ReleaseWake() noexcept {
        wakePending_.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool WakePending() const noexcept {
        return wakePending_.load(std::memory_order_acquire);
    }

    void CloseAndClear() noexcept {
        try {
            std::lock_guard lock(mutex_);
            closed_ = true;
            queue_.clear();
            spaceAvailable_.notify_all();
        } catch (...) {
            // Shutdown must remain noexcept even if the runtime is already failing.
        }
    }

    [[nodiscard]] double OldestAgeMilliseconds() const noexcept {
        try {
            std::lock_guard lock(mutex_);
            if (queue_.empty()) return 0.0;
            const auto oldest = std::min_element(queue_.begin(), queue_.end(),
                [](const BackgroundCompletion& left, const BackgroundCompletion& right) {
                    return left.queuedAt < right.queuedAt;
                });
            return std::max(0.0, std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - oldest->queuedAt).count());
        } catch (...) {
            return 0.0;
        }
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        try {
            std::lock_guard lock(mutex_);
            return queue_.size();
        } catch (...) {
            return 0;
        }
    }

private:

    [[nodiscard]] static int CompletionRank(const BackgroundCompletion& completion) noexcept {
        if (completion.kind == BackgroundCompletionKind::ScanComplete ||
            completion.kind == BackgroundCompletionKind::MetadataJobAborted) return 0;
        if (completion.kind == BackgroundCompletionKind::ScanBatch) return 3;
        if (!completion.workResult) return 4;
        if (completion.workResult->viewLane) {
            // 0.9.4.0 view lane: the image on screen is uploaded before any queued
            // thumbnail/filmstrip results; neighbour prefetch (requestEpoch 0) ranks
            // with predictive work.
            return completion.workResult->requestEpoch != 0 ? 0 : 3;
        }
        switch (completion.workResult->priority) {
        case quicksift::core::JobPriority::Interactive: return 1;
        case quicksift::core::JobPriority::Visible: return 2;
        case quicksift::core::JobPriority::Predictive: return 3;
        case quicksift::core::JobPriority::Face: return 4;
        case quicksift::core::JobPriority::Idle: return 5;
        case quicksift::core::JobPriority::Analysis: return 6;
        }
        return 6;
    }

    const std::size_t maximumEntries_;
    mutable std::mutex mutex_;
    std::condition_variable spaceAvailable_;
    std::deque<BackgroundCompletion> queue_;
    std::atomic<bool> wakePending_{ false };
    bool closed_ = false;
};

} // namespace quicksift::work
