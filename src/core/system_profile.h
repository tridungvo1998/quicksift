// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Runtime sizing policy interface; hardware facts enter here, not scattered UI constants.

#pragma once

#include "../runtime_support.h"

#include <cstddef>
#include <cstdint>

namespace quicksift::core {

struct SystemProfile {
    std::uint64_t totalPhysicalBytes = 8ull * 1024ull * 1024ull * 1024ull;
    std::uint64_t availablePhysicalBytes = 4ull * 1024ull * 1024ull * 1024ull;
    unsigned logicalProcessors = 4;
    unsigned foregroundWorkers = 2;
    unsigned idleWorkers = 1;
    unsigned codecThreadsPerDecode = 1;
    std::size_t decodeBytesInFlightLimit = 512ull * 1024ull * 1024ull;
    std::size_t cacheWriteQueueLimit = 32ull * 1024ull * 1024ull;
    std::uint64_t maximumDecodedPixelBytes = 320ull * 1024ull * 1024ull;
    int maximumDecodeBucket = 8192;
    int idleDelayMilliseconds = 650;
    float thumbnailQualityScale = 1.10f;
    quicksift::CpuTopology cpuTopology;
    unsigned retainedDecoderSessions = 1;
    unsigned derivativeLevels = 1;
    unsigned readAheadFiles = 1;
    std::size_t mappedInputLimit = 96ull * 1024ull * 1024ull;
    std::size_t reusableBufferLimit = 32ull * 1024ull * 1024ull;
    std::size_t gpuBudgetTarget = 192ull * 1024ull * 1024ull;
    bool enableTurboJpeg = true;
    bool enablePersistentPyramid = false;
    bool enableModernImageSources = true;
    bool lowMemory = true;
};

[[nodiscard]] SystemProfile DetectSystemProfile() noexcept;

} // namespace quicksift::core
