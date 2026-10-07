// OWNER: Fixed maximum-performance policy; no runtime CPU/RAM throttling.
// CODE GUIDE: See CODE_GUIDE.md -> "Rules for asynchronous work".

#include "core/performance_policy.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>

namespace quicksift::core {
namespace {
constexpr std::uint64_t kGibibyte = 1024ull * 1024ull * 1024ull;
}
FixedPerformancePolicy::FixedPerformancePolicy(PerformanceBaseline baseline) noexcept
    : baseline_(baseline) {
    baseline_.totalPhysicalBytes = std::max<std::uint64_t>(baseline_.totalPhysicalBytes, 1ull * kGibibyte);
    baseline_.workerPermits = std::max(1u, baseline_.workerPermits);
    baseline_.codecThreadsPerDecode = std::max(1u, baseline_.codecThreadsPerDecode);
    baseline_.decodeByteLimit = std::max<std::size_t>(1, baseline_.decodeByteLimit);
    baseline_.mappedInputLimit = std::max<std::size_t>(1, baseline_.mappedInputLimit);
    baseline_.reusableBufferLimit = std::max<std::size_t>(1, baseline_.reusableBufferLimit);
    current_ = BuildPolicy({});
    lastPolicyUpdate_ = Clock::now();
}

PerformancePolicy FixedPerformancePolicy::Update(const PerformanceInputs& inputs,
    Clock::time_point now, bool forceUpdate) noexcept {
    constexpr auto kMinUpdateInterval = std::chrono::milliseconds(500);
    if (!forceUpdate && now - lastPolicyUpdate_ < kMinUpdateInterval) return current_;
    lastPolicyUpdate_ = now;

    current_ = BuildPolicy(inputs);

    // Stable governor: half of physical RAM is the nominal decoded-image budget.
    // Only collapse it when the machine is genuinely close to running out of RAM.
    std::uint64_t targetMemory = baseline_.totalPhysicalBytes / 2;
    MEMORYSTATUSEX memory{ sizeof(memory) };
    if (GlobalMemoryStatusEx(&memory) && memory.ullAvailPhys < 1024ull * 1024ull * 1024ull) {
        targetMemory = std::min(targetMemory,
            static_cast<std::uint64_t>(static_cast<long double>(memory.ullAvailPhys) * 0.70L));
    }
    const std::uint64_t minMemory = 256ull * 1024ull * 1024ull;
    const std::uint64_t maxMemory = baseline_.totalPhysicalBytes * 9 / 10;
    targetMemory = std::clamp(targetMemory, minMemory, maxMemory);

    current_.decodeByteLimit = static_cast<std::size_t>(targetMemory);
    current_.mappedInputLimit = static_cast<std::size_t>(targetMemory / 4);
    current_.reusableBufferLimit = static_cast<std::size_t>(targetMemory / 8);

    unsigned maxWorkers = std::max(1u, baseline_.workerPermits);
    if (targetMemory < 1024ull * 1024ull * 1024ull) maxWorkers = std::max(1u, maxWorkers / 2);
    current_.activeDecodePermits = maxWorkers;

    const AppState& state = inputs.appState;
    const bool interactiveMode = state.mode == ViewMode::Single || state.mode == ViewMode::Compare;
    if (interactiveMode) {
        current_.allowPredictive = false;
        current_.prefetchDepth = 1;
        current_.extraThumbnailRows = 0;
        current_.allowAnalysis = state.faceLockEnabled;
    } else {
        current_.allowPredictive = true;
        current_.prefetchDepth = 8;
        current_.extraThumbnailRows = 2;
        current_.allowAnalysis = !state.faceLockEnabled ||
            (!state.viewportScrolling && !state.mouseOverScrollbar);
    }
    return current_;
}

PerformancePolicy FixedPerformancePolicy::BuildPolicy(const PerformanceInputs& inputs) const noexcept {
    (void)inputs;
    PerformancePolicy policy;

    const unsigned workers = std::max(1u, baseline_.workerPermits);
    policy.activeDecodePermits = workers;
    policy.codecThreadsPerDecode = std::max(1u, baseline_.codecThreadsPerDecode);
    policy.decodeByteLimit = std::max<std::size_t>(1, baseline_.decodeByteLimit);
    policy.allowOversizedDecode = true;
    policy.readAheadFiles = std::max(1u, baseline_.readAheadFiles);
    policy.mappedInputLimit = std::max<std::size_t>(1, baseline_.mappedInputLimit);
    policy.reusableBufferLimit = std::max<std::size_t>(1, baseline_.reusableBufferLimit);
    policy.retainedDecoderSessions = std::max(1u, baseline_.retainedDecoderSessions);
    policy.derivativeLevels = baseline_.derivativeLevels;
    policy.cacheWriteQueueLimit = std::max<std::size_t>(1, baseline_.cacheWriteQueueLimit);
    policy.allowPredictive = true;
    policy.allowAnalysis = true;
    policy.allowDerivatives = true;
    policy.allowSpeculativeCacheWrites = true;

    policy.extraThumbnailRows = 4;
    policy.prefetchDepth = 16;
    policy.currentDecodeTarget = 8192;
    policy.prefetchDecodeTarget = 4096;
    policy.thumbnailPredictionHorizonSeconds = 1.20f;

    policy.uiCompletionItemBudget = 256;
    policy.uiCompletionTimeBudget = std::chrono::milliseconds(12);
    return policy;
}



} // namespace quicksift::core
