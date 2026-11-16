// Pure geometry for the scrollable left pane and caption-fit detection (no Win32),
// so scrolling, clamping, focus-into-view and truncation are checkable on any OS.
#pragma once

namespace quicksift::core::pane {

// Largest valid offset for `contentHeight` pixels shown in a `viewportHeight` viewport.
[[nodiscard]] constexpr int MaxScroll(int contentHeight, int viewportHeight) noexcept {
    return contentHeight > viewportHeight && viewportHeight > 0 ? contentHeight - viewportHeight : 0;
}

[[nodiscard]] constexpr int ClampScroll(int offset, int contentHeight, int viewportHeight) noexcept {
    const int maxScroll = MaxScroll(contentHeight, viewportHeight);
    return offset < 0 ? 0 : (offset > maxScroll ? maxScroll : offset);
}

// Offset after a relative scroll; saturating so huge wheel/page deltas cannot
// overflow before the clamp (the only offset writer besides layout re-clamping).
[[nodiscard]] constexpr int ScrollBy(int offset, long long delta, int contentHeight, int viewportHeight) noexcept {
    const long long maxScroll = MaxScroll(contentHeight, viewportHeight);
    long long next = static_cast<long long>(offset) + delta;
    if (next < 0) next = 0;
    if (next > maxScroll) next = maxScroll;
    return static_cast<int>(next);
}

// Screen top of a pane child for `offset`. Children are ALWAYS placed from their
// natural (unscrolled) rect, never from their current (possibly scrolled) one.
[[nodiscard]] constexpr int ScrolledTop(int naturalTop, int offset) noexcept {
    return naturalTop - offset;
}

// A shown child must move whenever its live position differs from the target.
// (0.9.6.0 fix: the old "offset != 0" shortcut skipped this at the top, so returning
// to offset 0 left fully visible children at their scrolled position while the
// re-shown top children landed at their natural one - the Settings "clump".)
[[nodiscard]] constexpr bool NeedsReposition(int currentLeft, int currentTop,
    int targetLeft, int targetTop, bool currentlyVisible) noexcept {
    return !currentlyVisible || currentLeft != targetLeft || currentTop != targetTop;
}

// Offset that brings the item [itemTop, itemBottom) (content coordinates) fully into
// view with `margin` pixels of breathing room; unchanged when it is already visible.
[[nodiscard]] constexpr int ScrollIntoView(int offset, int itemTop, int itemBottom,
    int contentHeight, int viewportHeight, int margin) noexcept {
    int next = offset;
    if (itemTop - margin < offset) next = itemTop - margin;
    else if (itemBottom + margin > offset + viewportHeight) next = itemBottom + margin - viewportHeight;
    return ClampScroll(next, contentHeight, viewportHeight);
}

inline constexpr int kWheelDeltaPerNotch = 120; // WHEEL_DELTA
inline constexpr int kDefaultWheelLines = 3;
inline constexpr int kWheelLinePixelsDip = 16;
inline constexpr long long kMaximumWheelPixels = 1LL << 30; // far beyond any pane; keeps int math safe

[[nodiscard]] constexpr int SaturateWheelPixels(long long pixels) noexcept {
    return static_cast<int>(pixels > kMaximumWheelPixels ? kMaximumWheelPixels :
        (pixels < -kMaximumWheelPixels ? -kMaximumWheelPixels : pixels));
}

// Wheel delta (WHEEL_DELTA = 120 per notch) to a pixel offset change.
[[nodiscard]] constexpr int WheelStep(int wheelDelta, int linePixels, int lines) noexcept {
    const long long units = -static_cast<long long>(wheelDelta) * linePixels * (lines > 0 ? lines : kDefaultWheelLines);
    return SaturateWheelPixels(units / kWheelDeltaPerNotch);
}

// High-resolution (precision touchpad) wheel deltas smaller than one pixel step
// accumulate instead of truncating to zero; a direction change drops the
// opposite remainder. Remainder is kept in 1/120-pixel units.
struct WheelAccumulator { long long remainder = 0; };
[[nodiscard]] constexpr int TakeWheelPixels(WheelAccumulator& accumulator, int wheelDelta,
    int linePixels, int lines) noexcept {
    if (wheelDelta == 0 || linePixels <= 0) return 0;
    const long long units = -static_cast<long long>(wheelDelta) * linePixels * (lines > 0 ? lines : kDefaultWheelLines);
    if ((units < 0) != (accumulator.remainder < 0) && accumulator.remainder != 0) accumulator.remainder = 0;
    const long long total = accumulator.remainder + units;
    accumulator.remainder = total % kWheelDeltaPerNotch;
    return SaturateWheelPixels(total / kWheelDeltaPerNotch);
}

struct Thumb { int top = 0; int bottom = 0; };
// Overlay scrollbar thumb inside a track [trackTop, trackBottom).
[[nodiscard]] constexpr Thumb ThumbFor(int offset, int contentHeight, int viewportHeight,
    int trackTop, int trackBottom, int minThumb) noexcept {
    const int track = trackBottom - trackTop;
    const int maxScroll = MaxScroll(contentHeight, viewportHeight);
    if (track <= 0 || maxScroll <= 0) return {};
    int length = static_cast<int>(static_cast<long long>(track) * viewportHeight / contentHeight);
    if (length < minThumb) length = minThumb < track ? minThumb : track;
    const int travel = track - length;
    const int top = trackTop + static_cast<int>(static_cast<long long>(travel) * ClampScroll(offset, contentHeight, viewportHeight) / maxScroll);
    return { top, top + length };
}

// Pane visibility of a control [top, bottom) against the viewport [viewTop, viewBottom).
enum class Visibility { Hidden, Partial, Full };
[[nodiscard]] constexpr Visibility Classify(int top, int bottom, int viewTop, int viewBottom) noexcept {
    if (bottom <= viewTop || top >= viewBottom) return Visibility::Hidden;
    if (top >= viewTop && bottom <= viewBottom) return Visibility::Full;
    return Visibility::Partial;
}

} // namespace quicksift::core::pane

namespace quicksift::core::caption {

// Width left for a button caption after padding on both sides and the popup chevron.
[[nodiscard]] constexpr int AvailableWidth(int rectWidth, int padding, int chevron) noexcept {
    const int width = rectWidth - 2 * padding - chevron;
    return width > 0 ? width : 0;
}

// True when the measured caption does not fit and DT_END_ELLIPSIS will cut it.
[[nodiscard]] constexpr bool Truncated(int textWidth, int rectWidth, int padding, int chevron) noexcept {
    return textWidth > 0 && textWidth > AvailableWidth(rectWidth, padding, chevron);
}

} // namespace quicksift::core::caption
