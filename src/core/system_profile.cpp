// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Hardware-aware worker/cache/read-ahead sizing; keep thresholds centralized.

#include "system_profile.h"

#include <algorithm>

namespace quicksift::core {
namespace {
constexpr std::uint64_t kMebibyte = 1024ull * 1024ull;
constexpr std::uint64_t kGibibyte = 1024ull * 1024ull * 1024ull;
}

SystemProfile DetectSystemProfile() noexcept {
    SystemProfile profile;
    MEMORYSTATUSEX memory{ sizeof(memory) };
    if (GlobalMemoryStatusEx(&memory)) {
        profile.totalPhysicalBytes = std::max<std::uint64_t>(memory.ullTotalPhys, 1ull * kGibibyte);
        profile.availablePhysicalBytes = std::max<std::uint64_t>(memory.ullAvailPhys, 256ull * kMebibyte);
    }

    profile.cpuTopology = quicksift::DetectCpuTopology();
    profile.logicalProcessors = std::max(1u, profile.cpuTopology.logicalProcessors);
    profile.lowMemory = profile.totalPhysicalBytes <= 10ull * kGibibyte;

    // Image work is intentionally not throttled by the retired runtime pressure governor.
    // Keep one idle-capable lane so metadata/analysis can run when visual queues are
    // empty, and put every remaining logical CPU lane into foreground visual work.
    const unsigned workerCount = profile.logicalProcessors;
    if (workerCount <= 1) {
        profile.foregroundWorkers = 0;
        profile.idleWorkers = 1;
    } else {
        profile.foregroundWorkers = workerCount - 1;
        profile.idleWorkers = 1;
    }

    // Parallelize across independent decoder sessions. TurboJPEG JPEG work is
    // independently decoded by worker threads; the JPEG implementation itself
    // remains single-session and is capped at six concurrent sessions.
    profile.codecThreadsPerDecode = 1u;

    if (profile.lowMemory) {
        profile.decodeBytesInFlightLimit = static_cast<std::size_t>(std::clamp<std::uint64_t>(
            profile.availablePhysicalBytes / 8, 320ull * kMebibyte, 512ull * kMebibyte));
        profile.cacheWriteQueueLimit = 24ull * kMebibyte;
        profile.maximumDecodedPixelBytes = 320ull * kMebibyte;
        profile.maximumDecodeBucket = 8192;
        profile.idleDelayMilliseconds = 700;
        profile.thumbnailQualityScale = 1.10f;
        profile.retainedDecoderSessions = 1;
        profile.derivativeLevels = 1;
        profile.readAheadFiles = 1;
        profile.mappedInputLimit = 96ull * kMebibyte;
        profile.reusableBufferLimit = 24ull * kMebibyte;
        profile.gpuBudgetTarget = 160ull * kMebibyte;
        profile.enablePersistentPyramid = true;
    } else if (profile.totalPhysicalBytes <= 18ull * kGibibyte) {
        profile.decodeBytesInFlightLimit = static_cast<std::size_t>(std::clamp<std::uint64_t>(
            profile.availablePhysicalBytes / 6, 512ull * kMebibyte, 1024ull * kMebibyte));
        profile.cacheWriteQueueLimit = 48ull * kMebibyte;
        profile.maximumDecodedPixelBytes = 640ull * kMebibyte;
        profile.maximumDecodeBucket = 12288;
        profile.idleDelayMilliseconds = 500;
        profile.thumbnailQualityScale = 1.30f;
        profile.retainedDecoderSessions = 2;
        profile.derivativeLevels = 2;
        profile.readAheadFiles = 2;
        profile.mappedInputLimit = 256ull * kMebibyte;
        profile.reusableBufferLimit = 64ull * kMebibyte;
        profile.gpuBudgetTarget = 320ull * kMebibyte;
        profile.enablePersistentPyramid = true;
    } else {
        profile.decodeBytesInFlightLimit = static_cast<std::size_t>(std::clamp<std::uint64_t>(
            profile.availablePhysicalBytes / 5, 768ull * kMebibyte, 2048ull * kMebibyte));
        profile.cacheWriteQueueLimit = 64ull * kMebibyte;
        profile.maximumDecodedPixelBytes = 1024ull * kMebibyte;
        profile.maximumDecodeBucket = 12288;
        profile.idleDelayMilliseconds = 400;
        profile.thumbnailQualityScale = 1.50f;
        profile.retainedDecoderSessions = profile.logicalProcessors >= 16 ? 6u : 4u;
        profile.derivativeLevels = 3;
        profile.readAheadFiles = profile.logicalProcessors >= 12 ? 4u : 3u;
        profile.mappedInputLimit = 512ull * kMebibyte;
        profile.reusableBufferLimit = 128ull * kMebibyte;
        profile.gpuBudgetTarget = 640ull * kMebibyte;
        profile.enablePersistentPyramid = true;
    }
    // Maximum-performance mode does NOT mean unbounded allocations. These are fixed
    // hardware-tier safety limits chosen once at startup; they never react to
    // transient CPU/RAM pressure and therefore never throttle an otherwise healthy
    // image workload.
    return profile;
}

} // namespace quicksift::core
