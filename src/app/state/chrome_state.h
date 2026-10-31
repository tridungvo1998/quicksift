// OWNER: Main-window chrome: status-bar text cache, title-bar mouse tracking,
// fullscreen bookkeeping and the animation tick.
//
// Plain state of QuickSiftApplicationImpl (no behaviour). The class inherits
// this struct privately, so every member keeps its name and is used exactly as
// before; see app/quicksift_application_internal.h.
#pragma once

#include "app/state/app_state_common.h"

namespace quicksift::app::state {

struct ChromeState {
    std::wstring statusLeftText_;
    std::wstring statusRightText_;
    std::wstring statusProgressText_;
    int statusRatingState_ = -2;
    int statusPickState_ = -2;
    int undoEnabledState_ = -1;
    int redoEnabledState_ = -1;
    std::wstring faceWarningTooltipText_;
    std::wstring faceLockAvailabilityTooltipText_;
    RECT faceWarningTooltipRect_{};

    TrafficLight hoveredTrafficLight_ = TrafficLight::None;
    TrafficLight pressedTrafficLight_ = TrafficLight::None;
    bool trackingTitleMouse_ = false;
    bool trackingCanvasMouse_ = false;
    bool windowActive_ = true;
    bool fullscreen_ = false;
    bool shutdownBlocked_ = false;
    bool closingAfterTransactionCancellation_ = false;
    bool fullscreenTitleVisible_ = true;
    LONG_PTR preFullscreenStyle_ = 0;
    LONG_PTR preFullscreenExStyle_ = 0;
    WINDOWPLACEMENT preFullscreenPlacement_{ sizeof(WINDOWPLACEMENT) };
    std::chrono::steady_clock::time_point fullscreenEdgeHoverSince_{};

    std::chrono::steady_clock::time_point lastAnimationTick_{};
    // Current ID_TIMER_ANIMATION period (tokens::kIdlePollMs or kAnimationFrameMs).
    UINT animationTimerIntervalMs_ = 33;

    std::chrono::steady_clock::time_point fullscreenLeaveSince_{};
};

} // namespace quicksift::app::state
