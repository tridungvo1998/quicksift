// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: The keyboard shortcut table. OnKeyDown dispatches through Find() and the
// '?' cheat sheet is generated from the same rows, so the two cannot drift
// (portable; checked by scripts/checks/keyboard_shortcut_checks.cpp).
//
// Key codes are Windows virtual-key values written as numbers so this header has
// no windows.h dependency. kCharQuestion is not a virtual key: '?' arrives as a
// WM_CHAR so it works on every keyboard layout.
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace quicksift::core::shortcuts {

enum class Action {
    Rate,               // value = 1..5 (from the key)
    ClearRating,
    Pick,
    Reject,
    Unmark,
    ColorLabel,         // value = 1..4 from 6..9, 5 from Ctrl+5
    ClearColorLabel,
    FitZoom,
    ActualPixels,
    Rotate,
    ToggleFilmstrip,
    RetryFailedLoads,
    Undo,
    Redo,
    OpenSelection,
    ToggleSingle,
    BackToThumbnails,
    Previous,
    Next,
    RowUp,
    RowDown,
    First,
    Last,
    SelectAll,
    MoveToDeleted,
    // Listed in the sheet; dispatched by their own owners (not OnKeyDown).
    Fullscreen,         // HandleGlobalKey
    Search,             // thumbnail_search_controller HandleSearchKey
    ShortcutSheet,      // HandleGlobalKey (WM_CHAR '?')
    AutoAdvanceToggle,  // Caps Lock state, read live
};

enum class Section { Culling, Navigation, View, Edit, Help };
inline constexpr std::array<Section, 5> kSectionOrder{ {
    Section::Culling, Section::Navigation, Section::View, Section::Edit, Section::Help } };

[[nodiscard]] constexpr std::wstring_view SectionTitleKey(Section section) noexcept {
    switch (section) { // loc-keys-begin
    case Section::Culling: return L"Rate and cull";
    case Section::Navigation: return L"Navigate";
    case Section::View: return L"View";
    case Section::Edit: return L"Edit and files";
    case Section::Help: return L"Help";
    } // loc-keys-end
    return L"";
}

inline constexpr unsigned kVkBack = 0x08;
inline constexpr unsigned kVkReturn = 0x0D;
inline constexpr unsigned kVkCapital = 0x14;
inline constexpr unsigned kVkEscape = 0x1B;
inline constexpr unsigned kVkSpace = 0x20;
inline constexpr unsigned kVkEnd = 0x23;
inline constexpr unsigned kVkHome = 0x24;
inline constexpr unsigned kVkLeft = 0x25;
inline constexpr unsigned kVkUp = 0x26;
inline constexpr unsigned kVkRight = 0x27;
inline constexpr unsigned kVkDown = 0x28;
inline constexpr unsigned kVkDelete = 0x2E;
inline constexpr unsigned kVkF5 = 0x74;
inline constexpr unsigned kVkF7 = 0x76;
inline constexpr unsigned kVkF11 = 0x7A;
inline constexpr unsigned kCharQuestion = 0x1003F; // WM_CHAR '?', outside the VK range

struct Shortcut {
    Action action;
    unsigned firstKey;
    unsigned lastKey;        // == firstKey for a single key
    bool ctrl;               // Ctrl must be held (rows without Ctrl ignore it)
    bool shift;              // Shift must be held
    int firstValue;          // value passed for firstKey (Rate / ColorLabel)
    Section section;
    std::wstring_view keys;  // shown in the sheet (localized: arrow names, Ctrl...)
    std::wstring_view description; // localization key
    bool dispatchedByOnKeyDown;
};

// Order is priority: Ctrl rows come before the plain rows they share a key with.
// loc-keys-begin
inline constexpr std::array<Shortcut, 30> kShortcuts{ {
    { Action::Undo, 'Z', 'Z', true, false, 0, Section::Edit, L"Ctrl+Z", L"Undo", true },
    { Action::Redo, 'Y', 'Y', true, false, 0, Section::Edit, L"Ctrl+Y", L"Redo", true },
    { Action::ColorLabel, '5', '5', true, false, 5, Section::Culling, L"Ctrl+5", L"Purple label", true },
    { Action::ClearColorLabel, '0', '0', true, false, 0, Section::Culling, L"Ctrl+0", L"Clear colour label", true },
    { Action::RetryFailedLoads, 'R', 'R', true, true, 0, Section::Edit, L"Ctrl+Shift+R", L"Retry images that failed to load", true },
    { Action::SelectAll, 'A', 'A', true, false, 0, Section::Navigation, L"Ctrl+A", L"Select all (Thumbnails)", true },
    { Action::Search, 'F', 'F', true, false, 0, Section::Navigation, L"Ctrl+F", L"Search thumbnails", false },
    { Action::Rate, '1', '5', false, false, 1, Section::Culling, L"1–5", L"Set star rating", true },
    { Action::ClearRating, '0', '0', false, false, 0, Section::Culling, L"0", L"Clear star rating", true },
    { Action::Pick, 'P', 'P', false, false, 0, Section::Culling, L"P", L"Pick", true },
    { Action::Reject, 'X', 'X', false, false, 0, Section::Culling, L"X", L"Reject", true },
    { Action::Unmark, 'U', 'U', false, false, 0, Section::Culling, L"U", L"Remove pick or reject", true },
    { Action::ColorLabel, '6', '9', false, false, 1, Section::Culling, L"6–9", L"Red, yellow, green, blue label", true },
    { Action::AutoAdvanceToggle, kVkCapital, kVkCapital, false, false, 0, Section::Culling, L"Caps Lock", L"Auto-advance while on", false },
    { Action::Previous, kVkLeft, kVkLeft, false, false, 0, Section::Navigation, L"←", L"Previous photo", true },
    { Action::Next, kVkRight, kVkRight, false, false, 0, Section::Navigation, L"→", L"Next photo", true },
    { Action::RowUp, kVkUp, kVkUp, false, false, 0, Section::Navigation, L"↑", L"Row up (Thumbnails) / previous", true },
    { Action::RowDown, kVkDown, kVkDown, false, false, 0, Section::Navigation, L"↓", L"Row down (Thumbnails) / next", true },
    { Action::First, kVkHome, kVkHome, false, false, 0, Section::Navigation, L"Home", L"First photo", true },
    { Action::Last, kVkEnd, kVkEnd, false, false, 0, Section::Navigation, L"End", L"Last photo", true },
    { Action::OpenSelection, kVkReturn, kVkReturn, false, false, 0, Section::Navigation, L"Enter", L"Open selection in Single or Compare", true },
    { Action::ToggleSingle, kVkSpace, kVkSpace, false, false, 0, Section::Navigation, L"Space", L"Switch between Thumbnails and Single", true },
    { Action::BackToThumbnails, kVkEscape, kVkEscape, false, false, 0, Section::Navigation, L"Esc", L"Back to Thumbnails", true },
    { Action::FitZoom, 'F', 'F', false, false, 0, Section::View, L"F", L"Fit to window", true },
    { Action::ActualPixels, 'Z', 'Z', false, false, 0, Section::View, L"Z", L"Zoom to 100% pixels", true },
    { Action::Rotate, 'R', 'R', false, false, 0, Section::View, L"R", L"Rotate view", true },
    { Action::ToggleFilmstrip, kVkF7, kVkF7, false, false, 0, Section::View, L"F7", L"Show or hide the filmstrip", true },
    { Action::Fullscreen, kVkF11, kVkF11, false, false, 0, Section::View, L"F11", L"Fullscreen", false },
    { Action::MoveToDeleted, kVkDelete, kVkDelete, false, false, 0, Section::Edit, L"Delete", L"Move selection to the Deleted folder", true },
    { Action::ShortcutSheet, kCharQuestion, kCharQuestion, false, false, 0, Section::Help, L"?", L"Show or hide this shortcut sheet", false },
} };
// loc-keys-end

struct Match {
    Action action;
    int value;
};

// The first row whose key range and required modifiers match. Rows that do not
// require Ctrl also match with Ctrl held (existing behaviour: Ctrl+1 rates 1).
// F5 is a second key for RetryFailedLoads.
[[nodiscard]] constexpr std::optional<Match> Find(unsigned key, bool ctrl, bool shift) noexcept {
    if (key == kVkF5) return Match{ Action::RetryFailedLoads, 0 };
    for (const Shortcut& row : kShortcuts) {
        if (!row.dispatchedByOnKeyDown) continue;
        if (key < row.firstKey || key > row.lastKey) continue;
        if (row.ctrl && !ctrl) continue;
        if (row.shift && !shift) continue;
        return Match{ row.action, row.firstValue + static_cast<int>(key - row.firstKey) };
    }
    return std::nullopt;
}

// True for rating/pick/reject edits that auto-advance may follow.
[[nodiscard]] constexpr bool IsCullingEdit(Action action) noexcept {
    return action == Action::Rate || action == Action::ClearRating || action == Action::Pick ||
        action == Action::Reject || action == Action::Unmark;
}

[[nodiscard]] constexpr std::size_t RowsInSection(Section section) noexcept {
    std::size_t count = 0;
    for (const Shortcut& row : kShortcuts) if (row.section == section) ++count;
    return count;
}

} // namespace quicksift::core::shortcuts
