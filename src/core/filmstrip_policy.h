// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#pragma once
// OWNER: Single View filmstrip geometry and scheduling rules (portable, checked by
// filmstrip_checks). Round 22: the strip's layout, hit-testing, reserved canvas
// width, toggle placement, wheel stepping, decode target and decode admission
// live here so the Windows UI and the checks share one definition.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace quicksift::core::filmstrip {

// Named geometry (DIPs). The toggle is a child HWND sized in DIPs as well.
inline constexpr float kWidthDip = 130.0f;
inline constexpr float kThumbDip = 100.0f;
inline constexpr float kPaddingDip = 10.0f;
inline constexpr float kTopDip = 16.0f;
inline constexpr int kToggleDip = 36;
inline constexpr int kToggleMarginDip = 6;
inline constexpr std::size_t kNoCurrent = std::numeric_limits<std::size_t>::max();

struct Window {
    int maxThumbs = 0;
    std::size_t firstIndex = 0;
    std::size_t lastIndex = 0; // exclusive
    [[nodiscard]] constexpr bool Empty() const noexcept { return maxThumbs <= 0 || lastIndex <= firstIndex; }
};

// Cells that fit in `heightDip`, centred on `current` (clamped at both ends of
// the visible list). Empty when nothing is visible, the active image is not in
// the visible (filtered) list, or the canvas is too short for one cell.
[[nodiscard]] constexpr Window ComputeWindow(float heightDip, std::size_t visibleCount,
    std::size_t current) noexcept {
    Window window;
    const float available = heightDip - kTopDip;
    if (visibleCount == 0 || current >= visibleCount || available < kThumbDip + kPaddingDip) return window;
    window.maxThumbs = std::max(1, static_cast<int>(available / (kThumbDip + kPaddingDip)));
    const std::size_t count = std::min(visibleCount, static_cast<std::size_t>(window.maxThumbs));
    const std::size_t leading = (count - 1) / 2;
    window.firstIndex = current > leading ? current - leading : 0;
    if (window.firstIndex + count > visibleCount) window.firstIndex = visibleCount - count;
    window.lastIndex = window.firstIndex + count;
    return window;
}

// The whole drawn band (full canvas height, right edge) is the interaction
// surface: clicks or wheel there never reach the image underneath.
[[nodiscard]] constexpr bool BandContains(float x, float y, float canvasWidth, float canvasHeight) noexcept {
    return x >= canvasWidth - kWidthDip && x <= canvasWidth && y >= 0.0f && y <= canvasHeight;
}

// Cell under the point, or kNoCurrent (padding, margins, outside the band).
[[nodiscard]] constexpr std::size_t HitCell(const Window& window, float x, float y,
    float canvasWidth) noexcept {
    if (window.Empty()) return kNoCurrent;
    const float left = canvasWidth - kWidthDip + (kWidthDip - kThumbDip) * 0.5f;
    if (x < left || x > left + kThumbDip) return kNoCurrent;
    float top = kTopDip + kPaddingDip;
    for (std::size_t i = window.firstIndex; i < window.lastIndex; ++i) {
        if (y >= top && y <= top + kThumbDip) return i;
        top += kThumbDip + kPaddingDip;
    }
    return kNoCurrent;
}

// Width the Single View image area gives up on the right while the strip is open,
// so Fit / 100% / pan clamping never hide part of the photo under the strip.
[[nodiscard]] constexpr float ReservedRightDip(bool singleView, bool open) noexcept {
    return singleView && open ? kWidthDip : 0.0f;
}

// Toggle x (pixels): at the right edge when closed, just left of the strip when
// open, so it never covers a filmstrip cell.
[[nodiscard]] constexpr int ToggleLeftPx(int windowWidthPx, int toggleSizePx, int marginPx,
    int stripWidthPx, bool open) noexcept {
    const int right = windowWidthPx - (open ? stripWidthPx : 0);
    return std::max(0, right - toggleSizePx - marginPx);
}

// Wheel over the strip steps through the images: one step per WHEEL_DELTA of
// accumulated rotation (smooth/precision wheels send smaller deltas). Wheel up
// (positive) moves to the previous image. Returns the signed step count.
[[nodiscard]] constexpr int TakeWheelSteps(int& accumulator, int delta, int wheelDelta = 120) noexcept {
    if (wheelDelta <= 0) return 0;
    if ((accumulator > 0 && delta < 0) || (accumulator < 0 && delta > 0)) accumulator = 0; // direction change
    accumulator += delta;
    const int notches = accumulator / wheelDelta;
    accumulator -= notches * wheelDelta;
    return -notches;
}

// Filmstrip cells use the grid's thumbnail bucket. A different size was cancelled
// by the worker (thumbnail-target mismatch) or dropped by the UI as obsolete, and
// the one-thumbnail-per-photo cache rule made the two sizes evict each other.
[[nodiscard]] constexpr int ThumbnailTarget(int gridThumbnailTarget) noexcept {
    return std::max(1, gridThumbnailTarget);
}

// While the view lane decodes the image on screen, filmstrip (Interactive,
// Thumbnail-class) decodes run one at a time, like visible thumbnails (P2), so
// they stay below the main image; otherwise they are unrestricted.
[[nodiscard]] constexpr bool ThumbnailDecodeAdmitted(bool viewForegroundActive,
    unsigned activeThumbnailJobs, unsigned capDuringViewLoad = 1) noexcept {
    return !viewForegroundActive || activeThumbnailJobs < capDuringViewLoad;
}


// ---- Round 23: fade, decode window, badges ---------------------------------------

// Presence target of the strip's opacity fade (central Animator, Fade preset with the
// standard motion token). Shown only in Single View while open. Outside Single View
// the strip is not drawn, so its opacity snaps to 0 and the next entry fades in.
[[nodiscard]] constexpr bool FadeTarget(bool singleView, bool open) noexcept { return singleView && open; }
[[nodiscard]] constexpr bool SnapHiddenOnModeExit(bool singleView) noexcept { return !singleView; }

// Hit-testing and the reserved image area follow the LOGICAL state, never the
// animated opacity: a fading-out strip no longer takes clicks and the image fills
// the space at once (one relayout per toggle, no per-frame jumps); a fading-in strip
// takes clicks from the first frame.
[[nodiscard]] constexpr bool Interactive(bool singleView, bool open, bool windowEmpty) noexcept {
    return singleView && open && !windowEmpty;
}

// Decode window: the visible cells plus a small margin on each side. Filmstrip
// thumbnail decodes outside it are dropped (queued) or cancelled (in flight).
inline constexpr std::size_t kDecodeMarginCells = 2;

struct Range {
    std::size_t first = 0;
    std::size_t last = 0; // exclusive
    [[nodiscard]] constexpr bool Contains(std::size_t index) const noexcept { return index >= first && index < last; }
    [[nodiscard]] constexpr bool Empty() const noexcept { return last <= first; }
    friend constexpr bool operator==(const Range&, const Range&) = default;
};

[[nodiscard]] constexpr Range DecodeWindow(const Window& window, std::size_t visibleCount,
    std::size_t margin = kDecodeMarginCells) noexcept {
    if (window.Empty() || visibleCount == 0) return {};
    Range range;
    range.first = window.firstIndex > margin ? window.firstIndex - margin : 0;
    range.last = std::min(visibleCount, window.lastIndex + margin);
    if (range.last < range.first) range.last = range.first;
    return range;
}

// A filmstrip-owned thumbnail decode is dropped when the strip is inactive (closed,
// or not in Single View) or its photo left the decode window, unless the grid's
// current plan also wants that thumbnail (shared cache key). Main-image (view lane)
// and grid jobs are never filmstrip-owned, so this rule cannot touch them.
[[nodiscard]] constexpr bool DecodeDropped(bool filmstripOwned, bool stripActive, bool inDecodeWindow,
    bool gridDesired) noexcept {
    return filmstripOwned && !gridDesired && (!stripActive || !inDecodeWindow);
}

// Badges on a filmstrip cell: the grid's capsule badges, overlaid at the bottom of
// the thumbnail, scaled down for small cells (DIPs; Direct2D applies the DPI).
inline constexpr float kBadgeHeightDip = 17.0f;   // the grid's badge row height
inline constexpr float kBadgeMinHeightDip = 13.0f;
inline constexpr float kBadgeInsetDip = 4.0f;
inline constexpr float kBadgeHeightRatio = 0.17f;

[[nodiscard]] constexpr float BadgeHeightDip(float thumbDip) noexcept {
    return std::clamp(thumbDip * kBadgeHeightRatio, kBadgeMinHeightDip, kBadgeHeightDip);
}

} // namespace quicksift::core::filmstrip
