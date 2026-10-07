// CODE GUIDE: See CODE_GUIDE.md -> "Rules for asynchronous work".
// OWNER: Deterministic priority ranking and promotion; promotion must move jobs between lanes.

#pragma once

#include "app_types.h"

#include <cstddef>
#include <deque>
#include <utility>

namespace quicksift::core {

[[nodiscard]] constexpr int JobPriorityRank(JobPriority priority) noexcept {
    switch (priority) {
    case JobPriority::Interactive: return 0;
    case JobPriority::Visible: return 1;
    case JobPriority::Predictive: return 2;
    case JobPriority::Face: return 3;
    case JobPriority::Idle: return 4;
    case JobPriority::Analysis: return 5;
    }
    return 5;
}

[[nodiscard]] constexpr bool IsHigherPriority(JobPriority candidate,
    JobPriority existing) noexcept {
    return JobPriorityRank(candidate) < JobPriorityRank(existing);
}


inline constexpr std::size_t kForegroundBurstBeforeIdle = 8;

[[nodiscard]] constexpr bool ShouldServiceIdleAfterForegroundBurst(
    bool interactiveQueued, bool visibleQueued, bool idleReady, bool idleQueued,
    std::size_t foregroundBurst) noexcept {
    return !interactiveQueued && !visibleQueued && idleReady && idleQueued &&
        foregroundBurst >= kForegroundBurstBeforeIdle;
}

// Moves an already queued job into the lane that corresponds to its promoted
// priority. Merely changing the priority field would leave it waiting behind
// lower-priority work in its original deque.
template <typename Job>
void PromoteQueuedJob(std::deque<Job>& source, typename std::deque<Job>::iterator position,
    std::deque<Job>& destination, JobPriority priority) {
    Job promoted = std::move(*position);
    source.erase(position);
    promoted.priority = priority;
    destination.push_front(std::move(promoted));
}

} // namespace quicksift::core
