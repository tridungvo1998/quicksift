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

// Offset that brings the item [itemTop, itemBottom) (content coordinates) fully into
// view with `margin` pixels of breathing room; unchanged when it is already visible.
[[nodiscard]] constexpr int ScrollIntoView(int offset, int itemTop, int itemBottom,
    int contentHeight, int viewportHeight, int margin) noexcept {
    int next = offset;
    if (itemTop - margin < offset) next = itemTop - margin;
    else if (itemBottom + margin > offset + viewportHeight) next = itemBottom + margin - viewportHeight;
    return ClampScroll(next, contentHeight, viewportHeight);
}

// Wheel delta (WHEEL_DELTA = 120 per notch) to a pixel offset change.
[[nodiscard]] constexpr int WheelStep(int wheelDelta, int linePixels, int lines) noexcept {
    return -(wheelDelta * linePixels * (lines > 0 ? lines : 3)) / 120;
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
