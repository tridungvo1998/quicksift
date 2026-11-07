// OWNER: Thumbnail grid / Single / Compare viewing: filmstrip, scroll pump,
// pointer and face-outline interaction, badge text cache, decorative background.
//
// Plain state of QuickSiftApplicationImpl (no behaviour). The class inherits
// this struct privately, so every member keeps its name and is used exactly as
// before; see app/quicksift_application_internal.h.
#pragma once

#include "app/state/app_state_common.h"
#include "core/filmstrip_policy.h"
#include "review/face_interaction_policy.h"

namespace quicksift::app::state {

struct ViewerState {
    struct FaceOutlineHit {
        std::wstring path;
        std::size_t faceIndex = 0;
    };

    struct FilmstripLayout {
        // Named geometry: core/filmstrip_policy.h (round 22).
        float width = quicksift::core::filmstrip::kWidthDip;
        float thumbSize = quicksift::core::filmstrip::kThumbDip;
        float padding = quicksift::core::filmstrip::kPaddingDip;
        float top = quicksift::core::filmstrip::kTopDip;
        int maxThumbs = 0;
        size_t firstIndex = 0;
        size_t lastIndex = 0; // exclusive
    };

    // User preference (saved as "Filmstrip"); shown only in Single View.
    bool filmstripVisible_ = false;
    int filmstripWheelAccumulator_ = 0;
    // Round 23: last filmstrip decode window published to the worker.
    bool filmstripDecodePublished_ = false;
    quicksift::core::filmstrip::Range filmstripDecodeRange_{};
    std::uint64_t filmstripDecodeGeneration_ = 0;
    size_t filmstripDecodeVisibleCount_ = 0;
    float filmstripOpacity_ = 0.0f;
    mutable FilmstripLayout filmstripLayoutCache_{};
    mutable bool filmstripLayoutCacheValid_ = false;
    mutable float filmstripLayoutCacheHeight_ = -1.0f;
    mutable size_t filmstripLayoutCacheVisibleCount_ = 0;
    mutable size_t filmstripLayoutCacheCurrentIndex_ = std::numeric_limits<size_t>::max();
    mutable std::uint64_t filmstripLayoutCacheGeneration_ = std::numeric_limits<std::uint64_t>::max();

    // Round 23: heterogeneous (wstring_view) lookup, so per-frame badge painting
    // never builds a std::wstring key.
    struct BadgeTextHash {
        using is_transparent = void;
        std::size_t operator()(std::wstring_view text) const noexcept { return std::hash<std::wstring_view>{}(text); }
    };
    // Measured badge widths (DIP) per text for the current smallText_ format.
    std::unordered_map<std::wstring, float, BadgeTextHash, std::equal_to<>> badgeTextWidthCache_;
    // Round 23: localized badge captions, rebuilt only when the language changes
    // (were a Tr() / FormatBadgeText() allocation per card per frame).
    int badgeCaptionLanguage_ = -1;
    std::wstring badgePickCaption_;
    std::wstring badgeRejectCaption_;
    std::unordered_map<std::wstring, std::wstring, BadgeTextHash, std::equal_to<>> badgeFormatCaptionCache_;

    // Paths painted as placeholders since their last real image (crossfade trigger).
    std::unordered_set<std::wstring> thumbnailPlaceholderShown_;

    bool thumbnailScrollbarHot_ = false;
    bool thumbnailScrollDragging_ = false;
    // True while scrollbar drag OR wheel/keyboard fling/coast keeps decode admission throttled.
    bool thumbnailScrollMotionActive_ = false;
    bool thumbnailViewportNeedsUpdate_ = false;
    bool thumbnailDragRenderTimerActive_ = false;
    bool completionPumpTimerActive_ = false;
    // Paint-time Shell thumbnail creation budget (0.9.4.0): at most one synchronous
    // Shell fallback per frame and only for definitively failed decodes.
    // Shared frame pump for scrollbar drag AND wheel/keyboard flings: one layout/paint
    // (and at most one viewport publish) per frame while motion is active.
    bool thumbnailScrollFrameTimerActive_ = false;
    bool thumbnailScrollFramePending_ = false;
    bool thumbnailScrollSettlePending_ = false;
    int thumbnailScrollLastPublishedFirstRow_ = std::numeric_limits<int>::min();
    int thumbnailScrollLastPublishedLastRow_ = std::numeric_limits<int>::min();
    int thumbnailDragLoadMode_ = 0;
    unsigned thumbnailScrollConcurrentCap_ = 0;
    float thumbnailDragVelocityPixelsPerSecond_ = 0.0f;
    std::chrono::steady_clock::time_point lastThrottledSchedule_{};
    bool throttledSchedulePending_ = false;
    float thumbnailScrollDragOffset_ = 0.0f;

    bool dragging_ = false;

    D2D1_POINT_2F mouseDown_{};
    D2D1_POINT_2F lastMouse_{};
    // A press on a face outline becomes a click on release unless it left the
    // drag rectangle (max(SM_CXDRAG/SM_CYDRAG, kFaceClickMaxTravelDip); then a pan).
    std::optional<FaceOutlineHit> pendingFaceClick_;
    ULONGLONG lastFaceClickTick_ = 0;
    // Click-vs-drag tracking for canvas presses in Single/Compare (system drag
    // rectangle, DPI-scaled, >= 4 DIP). Face-outline presses defer panning until
    // the press becomes a drag so a click never nudges the view.
    quicksift::review::face_interaction::PressGesture canvasPress_;
    bool facePanDeferred_ = false;
    quicksift::review::face_interaction::FaceZoomToggle faceZoomToggle_;
    struct FaceTogglePressCapture {
        bool valid = false;
        std::wstring key;
        std::optional<quicksift::review::face_interaction::PaneViewSnapshot> framedNow;
        std::wstring prePath;
        quicksift::review::face_interaction::PaneViewSnapshot pre{};
    };
    FaceTogglePressCapture faceTogglePress_;
    // Face outline reveal (1 s) + fade (250 ms); right-click pins (session only).
    quicksift::review::face_interaction::FaceOutlineFade faceOutlineFade_;

    std::vector<ColorBlob> backgroundBlobs_;
    std::vector<std::uint32_t> backgroundPixels_;
    int backgroundCacheWidth_ = 0;
    int backgroundCacheHeight_ = 0;
    int backgroundSurfaceWidth_ = 0;
    int backgroundSurfaceHeight_ = 0;
    AppTheme backgroundCacheTheme_ = AppTheme::Dark;
};

} // namespace quicksift::app::state
