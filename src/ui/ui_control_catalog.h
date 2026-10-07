// CODE GUIDE: See CODE_GUIDE.md -> "The authoritative owners".
// OWNER: Single persistent-button catalog for creation, membership, captions, and title widths.

#pragma once

#include "ui_command_ids.h"

#include <array>
#include <cstddef>
#include <string_view>

// Authoritative catalog for persistent main-window controls.
//
// A button's creation caption, UI area, and title-bar minimum width must be
// declared here rather than repeated in control creation, layout, and visibility
// switches. Menu-only command IDs remain in ui_command_ids.h.
namespace quicksift::ui::control_catalog {

struct ButtonDefinition {
    int id = 0;
    std::wstring_view captionKey;
    int minimumWidthDip = 0; // Used only by title-bar layout.
};

inline constexpr std::array<ButtonDefinition, 16> kTitleButtons{{
    { command_id::ID_PICK_FOLDER, L"Open", 62 },
    { command_id::ID_MODE_THUMBS, L"Thumbnails", 92 },
    { command_id::ID_MODE_SINGLE, L"Single", 62 },
    { command_id::ID_MODE_COMPARE, L"Compare", 76 },
    { command_id::ID_PREVIOUS, L"", 42 },
    { command_id::ID_NEXT, L"", 42 },
    { command_id::ID_SORT, L"Sort: A–Z", 100 },
    { command_id::ID_THUMB_SIZE, L"Thumbnail size: M", 132 },
    { command_id::ID_ZOOM_MODE, L"Zoom: Fit", 92 },
    { command_id::ID_ROTATE_LEFT, L"", 44 },
    { command_id::ID_ROTATE_RIGHT, L"", 44 },
    { command_id::ID_UNDO, L"Undo", 60 },
    { command_id::ID_REDO, L"Redo", 60 },
    { command_id::ID_SYNC_VIEW, L"Sync: On", 88 },
    { command_id::ID_FACE_LOCK, L"Face Lock: Off", 122 },
    { command_id::ID_FULLSCREEN, L"Fullscreen", 92 },
}};

inline constexpr std::array<ButtonDefinition, 3> kFileButtons{{
    { command_id::ID_COPY, L"Copy", 0 },
    { command_id::ID_MOVE, L"Move", 0 },
    { command_id::ID_DELETE, L"Delete", 0 },
}};

inline constexpr std::array<ButtonDefinition, 3> kCullButtons{{
    { command_id::ID_RATING_MENU, L"Rating: Unrated", 0 },
    { command_id::ID_PICK_STATE_MENU, L"Pick state: Mixed", 0 },
    { command_id::ID_COLOR_LABEL, L"Label: None", 0 },
}};

inline constexpr std::array<ButtonDefinition, 17> kSettingsButtons{{
    { command_id::ID_SETTING_BADGE_STARS, L"Show star badges", 0 },
    { command_id::ID_SETTING_BADGE_COLOR, L"Show color-label badges", 0 },
    { command_id::ID_SETTING_BADGE_PICK, L"Show pick/reject badges", 0 },
    { command_id::ID_SETTING_BADGE_PAIR, L"Show RAW/JPG badges", 0 },
    { command_id::ID_SETTING_COPY_MOVE_RAW_WITH_JPG, L"Copy/Move RAW with JPG", 0 },
    { command_id::ID_SETTING_RAW_JPEG_PREVIEWS_ONLY, L"Load only JPG previews for RAW", 0 },
    { command_id::ID_SETTING_SAFE_JPEG, L"Safe JPG writes", 0 },
    { command_id::ID_SETTING_METADATA_RAW, L"XMP vs embedded metadata for RAW", 0 },
    { command_id::ID_SETTING_METADATA_LOSSY, L"XMP vs embedded metadata for JPG/PNG/lossy", 0 },
    { command_id::ID_SETTING_METADATA_TIFF, L"XMP vs embedded metadata for TIFF", 0 },
    { command_id::ID_SETTING_PERFORMANCE_HUD, L"Show performance HUD", 0 },
    { command_id::ID_SETTING_DIAGNOSTIC_LOG, L"Show diagnostic log", 0 },
    { command_id::ID_SETTING_CLEAR_MEMORY_CACHE, L"Clear memory cache", 0 },
    { command_id::ID_SETTING_CLEAR_DISK_CACHE, L"Clear disk cache", 0 },
    { command_id::ID_SETTING_HELP, L"Help and documentation", 0 },
    { command_id::ID_SETTING_ABOUT, L"About this app", 0 },
    { command_id::ID_SETTING_LANGUAGE_SELECTOR, L"Language", 0 },
}};

inline constexpr std::array<ButtonDefinition, 5> kFilterButtons{{
    { command_id::ID_STAR_FILTER, L"Stars: All", 0 },
    { command_id::ID_PICK_FILTER, L"Pick state: All", 0 },
    { command_id::ID_LABEL_FILTER, L"Labels: All", 0 },
    { command_id::ID_FORMATS, L"Format: All", 0 },
    { command_id::ID_DATE_FILTER, L"Date: Any", 0 },
}};

inline constexpr std::array<int, 6> kFlyoutTabIds{{
    command_id::ID_FLYOUT_TAB_FOLDERS,
    command_id::ID_FLYOUT_TAB_FILE,
    command_id::ID_FLYOUT_TAB_CULL,
    command_id::ID_FLYOUT_TAB_FILTER,
    command_id::ID_FLYOUT_TAB_INFO,
    command_id::ID_FLYOUT_TAB_SETTINGS,
}};

template <std::size_t Count>
constexpr bool ContainsControlId(const std::array<ButtonDefinition, Count>& definitions,
    int id) noexcept {
    for (const ButtonDefinition& definition : definitions) {
        if (definition.id == id) return true;
    }
    return false;
}

} // namespace quicksift::ui::control_catalog
