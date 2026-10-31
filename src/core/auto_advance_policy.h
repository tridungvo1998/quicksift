// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Auto-advance after a rating / pick / reject shortcut (portable; checked
// by scripts/checks/keyboard_shortcut_checks.cpp).
//
// Rules:
//  * active when the Settings switch is on OR Caps Lock is on (Lightroom style);
//  * only keyboard culling edits advance (1-5, 0, P, X, U), only in Single View
//    and Thumbnails, and only when the edit targeted one photo;
//  * the target is the next VISIBLE photo: if the edit made the photo leave the
//    filtered list, the photo that took its place is next; at the last visible
//    photo QuickSift stays put and says so.
#pragma once

#include <cstddef>
#include <optional>

namespace quicksift::core::auto_advance {

[[nodiscard]] constexpr bool Active(bool setting, bool capsLockOn) noexcept {
    return setting || capsLockOn;
}

// Caps Lock indicator: shown only when Caps Lock (not the setting) turns it on.
[[nodiscard]] constexpr bool ShowCapsLockIndicator(bool setting, bool capsLockOn) noexcept {
    return capsLockOn && !setting;
}

enum class Outcome { MoveTo, StayAtEnd, Nothing };

struct Target {
    Outcome outcome = Outcome::Nothing;
    std::size_t index = 0;
};

// `indexAfterEdit` is the edited photo's visible index after the edit (nullopt when
// the filters now hide it); `indexBeforeEdit` its index before; `visibleCount`
// the visible count after the edit.
[[nodiscard]] constexpr Target NextTarget(std::optional<std::size_t> indexAfterEdit,
    std::size_t indexBeforeEdit, std::size_t visibleCount) noexcept {
    if (visibleCount == 0) return {};
    if (indexAfterEdit) {
        if (*indexAfterEdit + 1 < visibleCount) return { Outcome::MoveTo, *indexAfterEdit + 1 };
        return { Outcome::StayAtEnd, *indexAfterEdit };
    }
    // The photo left the filtered list: the one now at its old position is next.
    if (indexBeforeEdit < visibleCount) return { Outcome::MoveTo, indexBeforeEdit };
    return { Outcome::StayAtEnd, visibleCount - 1 };
}

} // namespace quicksift::core::auto_advance
