// OWNER: Portable face-detector capability and bounded retry policy.

#include "core/face_detection_policy.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace quicksift::core {

void FaceAnalysisRetryPolicy::SetCapability(
    FaceDetectorCapability capability, Clock::time_point now) noexcept {
    capability_ = capability;
    capabilityRetryAfter_ = capability == FaceDetectorCapability::TemporarilyFailed ?
        now + std::chrono::seconds(60) : Clock::time_point{};
}

FaceDetectorCapability FaceAnalysisRetryPolicy::Capability() const noexcept {
    return capability_;
}

bool FaceAnalysisRetryPolicy::CapabilityRefreshDue(Clock::time_point now) const noexcept {
    return capability_ == FaceDetectorCapability::Unknown ||
        (capability_ == FaceDetectorCapability::TemporarilyFailed &&
            now >= capabilityRetryAfter_);
}

bool FaceAnalysisRetryPolicy::CanSchedule(
    const std::wstring& path, Clock::time_point now) const {
    if (capability_ != FaceDetectorCapability::Available || path.empty()) return false;
    const auto iterator = retries_.find(path);
    if (iterator == retries_.end()) return true;
    return !iterator->second.exhausted && now >= iterator->second.nextAttempt;
}

void FaceAnalysisRetryPolicy::AllowExplicitRetry(
    const std::wstring& path, Clock::time_point now) {
    if (path.empty()) return;
    RetryState& state = retries_[path];
    state.failures = 0;
    state.exhausted = false;
    state.nextAttempt = now;
}

void FaceAnalysisRetryPolicy::RecordOutcome(
    const std::wstring& path, FaceAnalysisOutcome outcome, Clock::time_point now) {
    if (outcome == FaceAnalysisOutcome::Unavailable) {
        SetCapability(FaceDetectorCapability::Unavailable, now);
        return;
    }
    if (path.empty() || outcome == FaceAnalysisOutcome::None) return;
    if (outcome == FaceAnalysisOutcome::Completed) {
        retries_.erase(path);
        return;
    }

    RetryState& state = retries_[path];
    if (state.failures < MaximumTransientFailures) ++state.failures;
    state.exhausted = state.failures >= MaximumTransientFailures;
    state.nextAttempt = state.exhausted ? Clock::time_point::max() :
        now + RetryDelay(state.failures);
}

void FaceAnalysisRetryPolicy::ClearFileRetries() {
    retries_.clear();
}

unsigned FaceAnalysisRetryPolicy::FailureCount(const std::wstring& path) const {
    const auto iterator = retries_.find(path);
    return iterator == retries_.end() ? 0u : iterator->second.failures;
}

bool FaceAnalysisRetryPolicy::Exhausted(const std::wstring& path) const {
    const auto iterator = retries_.find(path);
    return iterator != retries_.end() && iterator->second.exhausted;
}

std::chrono::seconds FaceAnalysisRetryPolicy::RetryDelay(unsigned failureCount) noexcept {
    constexpr std::array<std::chrono::seconds, 4> delays{
        std::chrono::seconds(30),
        std::chrono::seconds(120),
        std::chrono::seconds(600),
        std::chrono::seconds(1800)
    };
    if (failureCount == 0) return delays.front();
    const std::size_t index = std::min<std::size_t>(failureCount - 1, delays.size() - 1);
    return delays[index];
}

} // namespace quicksift::core
