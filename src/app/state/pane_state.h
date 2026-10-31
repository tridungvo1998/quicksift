// OWNER: Left pane: open/tab blends, backdrop and frost bookkeeping, per-tab scroll,
// [Pane] timing, library/tree scrollbar drag state.
//
// Plain state of QuickSiftApplicationImpl (no behaviour). The class inherits
// this struct privately, so every member keeps its name and is used exactly as
// before; see app/quicksift_application_internal.h.
#pragma once

#include "app/state/app_state_common.h"
#include "core/pane_timing.h"

namespace quicksift::app::state {

struct PaneState {
    std::array<float, 6> flyoutTabActiveBlend_{};
    float leftPaneOpenBlend_ = 0.0f;
    bool leftPaneContentVisible_ = false;
    // Skip SetWindowRgn/redraw when animated backdrop geometry is unchanged.
    int leftPaneBackdropLastW_ = -1;
    int leftPaneBackdropLastH_ = -1;
    int leftPaneBackdropLastRadiusDip_ = -1;
    // Left-pane frost (ui_frost): dirty on open/resize/theme; built from a canvas
    // read-back; the DIB itself lives in the framework (SetPaneFrost).
    bool leftPaneFrostDirty_ = true;
    bool leftPaneFrostPending_ = false;
    bool leftPaneFrostValid_ = false;
    std::uint64_t leftPaneFrostSignature_ = 0;
    COLORREF leftPaneFrostTint_ = CLR_INVALID;
    POINT leftPaneFrostOrigin_{};   // open-pane top-left (main client) the frost maps to
    std::chrono::steady_clock::time_point leftPaneFrostCapturedAt_{};

    bool paneItemBuffering_ = false; // OnDrawItem re-entry guard (buffered pane surfaces)
    bool libraryScrollDragging_ = false;
    int libraryScrollDragOffset_ = 0;

    FlyoutPanel activeFlyout_ = FlyoutPanel::None;
    // Pinned command pane: stays open and the canvas gives it room (persisted).
    bool leftPanePinned_ = false;
    std::wstring leftPanePinTooltipText_;
    HWND leftPanePinTooltip_ = nullptr;
    int leftPanePinLastW_ = -1; // skip SetWindowRgn when the size is unchanged
    int leftPanePinLastH_ = -1;
    std::array<int, 7> paneScrollOffset_{}; // indexed by FlyoutPanel
    // [Pane] timing (core/pane_timing.h): LayoutControls fills paneLayoutTiming_
    // whenever pane content is laid out; open/reveal/first-paint are logged once
    // at INFO (first of each kind, or slow) and otherwise at TRACE.
    quicksift::core::pane_timing::Recorder paneLayoutTiming_;
    bool paneEverOpened_ = false;
    std::array<bool, 7> paneSectionRevealed_{}; // indexed by FlyoutPanel
    std::chrono::steady_clock::time_point paneOpenRequestedAt_{};
    bool paneFirstPaintPending_ = false;
    bool paneEverPainted_ = false;
    bool paneFirstFrostLogged_ = false;

    int paneContentHeight_ = 0;
    RECT paneViewport_{};
    std::vector<std::pair<HWND, RECT>> paneNaturalRects_;
    std::vector<HWND> paneClippedChildren_;
    // Remembered section restored when the left hit-strip reopens the pane.
    FlyoutPanel lastFlyoutSection_ = FlyoutPanel::Folders;
    bool menuOpen_ = false;
    std::chrono::steady_clock::time_point lastFlyoutHover_ = std::chrono::steady_clock::now();
    bool treeScrollbarHot_ = false;
    bool trackingTreeMouse_ = false;
    bool treeScrollDragging_ = false;
    int treeScrollDragOffset_ = 0;
};

} // namespace quicksift::app::state
