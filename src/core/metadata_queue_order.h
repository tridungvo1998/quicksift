// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#pragma once
// Visible-first ordering of the pending metadata (rating/pick/label) queue.
// Portable and header-only so the ordering contract is checked off Windows.
#include <algorithm>
#include <cstddef>
#include <deque>
#include <limits>

namespace quicksift::core::metadata_order {

// Visible-position window [first, last] (inclusive); empty when last < first.
struct VisibleWindow {
    std::size_t first = 1;
    std::size_t last = 0;
    [[nodiscard]] bool Empty() const noexcept { return last < first; }
    [[nodiscard]] bool Contains(std::size_t position) const noexcept {
        return !Empty() && position >= first && position <= last;
    }
    friend bool operator==(const VisibleWindow&, const VisibleWindow&) = default;
};

// Window of `before`/`after` positions around [firstShown, lastShown], clamped to
// [0, visibleCount). Empty when nothing is visible.
[[nodiscard]] inline VisibleWindow WindowAround(std::size_t firstShown, std::size_t lastShown,
    std::size_t visibleCount, std::size_t before, std::size_t after) noexcept {
    VisibleWindow window;
    if (visibleCount == 0 || lastShown < firstShown || firstShown >= visibleCount) return window;
    window.first = firstShown > before ? firstShown - before : 0;
    const std::size_t clampedLast = std::min(lastShown, visibleCount - 1);
    window.last = clampedLast > std::numeric_limits<std::size_t>::max() - after ? visibleCount - 1
        : std::min(clampedLast + after, visibleCount - 1);
    return window;
}

// Stable partition: queue entries whose visible position lies in `window` move to
// the front, keeping their relative order; everything else keeps its order after
// them. Entries are only reordered, never added, removed or duplicated, so the
// pending counters paired one-to-one with queue entries stay exact.
// positionOf(catalogIndex) returns the visible position or any value outside the
// window (e.g. SIZE_MAX) for filtered-out photos. Returns the number in front.
template <typename PositionOf>
std::size_t PromoteWindow(std::deque<std::size_t>& queue, const VisibleWindow& window, PositionOf positionOf) {
    if (window.Empty() || queue.empty()) return 0;
    const auto middle = std::stable_partition(queue.begin(), queue.end(),
        [&](std::size_t index) { return window.Contains(positionOf(index)); });
    return static_cast<std::size_t>(middle - queue.begin());
}

} // namespace quicksift::core::metadata_order
