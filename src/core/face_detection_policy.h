// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Portable face-detector capability and bounded retry policy.

#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

namespace quicksift::core {

enum class FaceDetectorCapability {
    Unknown,
    Available,
    Unavailable,
    TemporarilyFailed
};

enum class FaceAnalysisOutcome {
    None,
    Completed,
    TransientFailure,
    Unavailable
};

class FaceAnalysisRetryPolicy {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr unsigned MaximumTransientFailures = 5;

    void SetCapability(FaceDetectorCapability capability, Clock::time_point now) noexcept;
    [[nodiscard]] FaceDetectorCapability Capability() const noexcept;
    [[nodiscard]] bool CapabilityRefreshDue(Clock::time_point now) const noexcept;
    [[nodiscard]] bool CanSchedule(const std::wstring& path, Clock::time_point now) const;
    void AllowExplicitRetry(const std::wstring& path, Clock::time_point now);
    void RecordOutcome(const std::wstring& path, FaceAnalysisOutcome outcome,
        Clock::time_point now);
    void ClearFileRetries();
    [[nodiscard]] unsigned FailureCount(const std::wstring& path) const;
    [[nodiscard]] bool Exhausted(const std::wstring& path) const;

private:
    struct RetryState {
        unsigned failures = 0;
        bool exhausted = false;
        Clock::time_point nextAttempt{};
    };

    static std::chrono::seconds RetryDelay(unsigned failureCount) noexcept;

    FaceDetectorCapability capability_ = FaceDetectorCapability::Unknown;
    Clock::time_point capabilityRetryAfter_{};
    std::unordered_map<std::wstring, RetryState> retries_;
};

} // namespace quicksift::core
