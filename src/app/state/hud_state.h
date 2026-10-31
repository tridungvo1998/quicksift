// OWNER: Performance HUD visibility, background-face pacing and CPU telemetry samples.
//
// Plain state of QuickSiftApplicationImpl (no behaviour). The class inherits
// this struct privately, so every member keeps its name and is used exactly as
// before; see app/quicksift_application_internal.h.
#pragma once

#include "app/state/app_state_common.h"

namespace quicksift::app::state {

struct HudState {
    bool performanceHudVisible_ = false;

    std::chrono::steady_clock::time_point lastInteraction_ = std::chrono::steady_clock::now() - std::chrono::seconds(5);
    size_t backgroundFaceCursor_ = 0;
    size_t backgroundFaceAttemptsSinceWork_ = 0;
    std::chrono::steady_clock::time_point backgroundFacePauseUntil_{};
    std::chrono::steady_clock::time_point lastPerformanceTelemetryLog_{};
    std::chrono::steady_clock::time_point lastPerformanceCpuSample_{};
    bool performanceCpuSampleInitialized_ = false;
    bool performanceCpuLoadValid_ = false;
    unsigned performanceCpuLoadPercent_ = 0;
    std::uint64_t previousPerformanceCpuIdle_ = 0;
    std::uint64_t previousPerformanceCpuKernel_ = 0;
    std::uint64_t previousPerformanceCpuUser_ = 0;
    bool cpuSampleValid_ = false;
    std::uint64_t previousCpuIdle_ = 0;
    std::uint64_t previousCpuKernel_ = 0;
    std::uint64_t previousCpuUser_ = 0;
};

} // namespace quicksift::app::state
