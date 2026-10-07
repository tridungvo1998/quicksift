// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Deterministic undo/redo partition policy; keep it independent of UI state.

#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace quicksift::core {

// Returns the items whose completion flag matches takeCompleted. A short flag
// vector is treated conservatively: unrepresented items are incomplete.
template <typename T>
[[nodiscard]] std::vector<T> SelectHistoryItems(const std::vector<T>& items,
    const std::vector<bool>& completed, bool takeCompleted) {
    std::vector<T> result;
    result.reserve(items.size());
    for (std::size_t index = 0; index < items.size(); ++index) {
        const bool wasCompleted = index < completed.size() && completed[index];
        if (wasCompleted == takeCompleted) result.push_back(items[index]);
    }
    return result;
}

// Selects synchronized pairs conservatively. Missing completion flags are
// incomplete, and an unmatched tail is ignored because it cannot form a valid
// metadata before/after record.
template <typename T, typename U>
[[nodiscard]] std::pair<std::vector<T>, std::vector<U>> SelectParallelHistoryItems(
    const std::vector<T>& before, const std::vector<U>& after,
    const std::vector<bool>& completed, bool takeCompleted) {
    const std::size_t count = std::min(before.size(), after.size());
    std::pair<std::vector<T>, std::vector<U>> result;
    result.first.reserve(count);
    result.second.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const bool wasCompleted = index < completed.size() && completed[index];
        if (wasCompleted == takeCompleted) {
            result.first.push_back(before[index]);
            result.second.push_back(after[index]);
        }
    }
    return result;
}

} // namespace quicksift::core
