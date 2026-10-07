// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Public catalog filter/sort policy; keep decisions deterministic and testable.

#pragma once

#include "app_types.h"
#include "image_formats.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <string_view>

namespace quicksift::core {

struct FormatSelection {
    std::array<bool, 7> enabled{ true, true, true, true, true, true, true };

    [[nodiscard]] bool IsEnabled(FormatGroup group) const noexcept {
        return enabled[static_cast<std::size_t>(group)];
    }

    void SetEnabled(FormatGroup group, bool value) noexcept {
        enabled[static_cast<std::size_t>(group)] = value;
    }
};

[[nodiscard]] bool IsFormatAllowed(std::wstring_view lowerCaseExtension,
    const FormatSelection& selection) noexcept;
[[nodiscard]] bool IsDateAllowed(std::filesystem::file_time_type modified,
    DateFilter filter,
    std::filesystem::file_time_type now = std::filesystem::file_time_type::clock::now()) noexcept;
[[nodiscard]] bool IsCullAllowed(const PhotoItem& item, RatingFilter rating,
    PickFilter pick, ColorLabelFilter label) noexcept;
[[nodiscard]] bool PhotoSortLess(const PhotoItem& left, const PhotoItem& right,
    SortMode mode) noexcept;

} // namespace quicksift::core
