// CODE GUIDE: See CODE_GUIDE.md -> "Rules for asynchronous work".
// OWNER: Fixed maximum-performance policy for visual decoding. Runtime pressure is telemetry only.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include "core/app_types.h"

namespace quicksift::core {

struct PerformanceBaseline {
    std::uint64_t totalPhysicalBytes = 8ull * 1024ull * 1024ull * 1024ull;
    bool lowMemoryTier = true;
    unsigned workerPermits = 2;
    unsigned codecThreadsPerDecode = 1;
    std::size_t decodeByteLimit = 512ull * 1024ull * 1024ull;
    unsigned readAheadFiles = 1;
    std::size_t mappedInputLimit = 96ull * 1024ull * 1024ull;
    std::size_t reusableBufferLimit = 32ull * 1024ull * 1024ull;
    unsigned retainedDecoderSessions = 1;
    unsigned derivativeLevels = 1;
    std::size_t cacheWriteQueueLimit = 32ull * 1024ull * 1024ull;
};

struct PerformanceInputs {
    std::uint64_t availablePhysicalBytes = 4ull * 1024ull * 1024ull * 1024ull;
    unsigned memoryLoadPercent = 50;
    bool lowMemorySignal = false;
    bool gpuPressure = false;
    bool interacting = false;
    bool remoteStorage = false;
    bool removableStorage = false;
    bool seekPenaltyStorage = false;
    bool cpuSampleValid = false;
    unsigned cpuLoadPercent = 0;
    AppState appState{};
    std::size_t completionBacklog = 0;
    double completionBacklogAgeMilliseconds = 0.0;
    double queueWaitMilliseconds = 0.0;
    double decodeMilliseconds = 0.0;
};

struct PerformancePolicy {
    unsigned activeDecodePermits = 1;
    unsigned codecThreadsPerDecode = 1;
    std::size_t decodeByteLimit = 1;
    bool allowOversizedDecode = true;
    unsigned readAheadFiles = 0;
    std::size_t mappedInputLimit = 1;
    std::size_t reusableBufferLimit = 1;
    unsigned retainedDecoderSessions = 0;
    unsigned derivativeLevels = 0;
    std::size_t cacheWriteQueueLimit = 0;
    bool allowPredictive = true;
    bool allowAnalysis = true;
    bool allowDerivatives = true;
    bool allowSpeculativeCacheWrites = true;

    int extraThumbnailRows = 1;
    int prefetchDepth = 2;
    int currentDecodeTarget = 2048;
    int prefetchDecodeTarget = 1280;
    float thumbnailPredictionHorizonSeconds = 0.35f;

    std::size_t uiCompletionItemBudget = 24;
    std::chrono::milliseconds uiCompletionTimeBudget{ 4 };
};

class FixedPerformancePolicy final {
public:
    using Clock = std::chrono::steady_clock;

    explicit FixedPerformancePolicy(PerformanceBaseline baseline = {}) noexcept;

    [[nodiscard]] PerformancePolicy Update(const PerformanceInputs& inputs,
        Clock::time_point now = Clock::now(), bool forceUpdate = false) noexcept;
    [[nodiscard]] const PerformancePolicy& Current() const noexcept { return current_; }

private:
    [[nodiscard]] PerformancePolicy BuildPolicy(const PerformanceInputs& inputs) const noexcept;

    PerformanceBaseline baseline_;
    PerformancePolicy current_{};
    Clock::time_point healthySince_{};
    Clock::time_point lastPolicyUpdate_{};
};


} // namespace quicksift::core
