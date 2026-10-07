// CODE GUIDE: See CODE_GUIDE.md -> "The authoritative owners".
// OWNER: Public image-format registry; callers must not duplicate extension lists.

#pragma once

#include <string_view>

namespace quicksift::core {

// Extension inputs must already be lower case and include the leading dot.
[[nodiscard]] bool IsJpegExtension(std::wstring_view lowerCaseExtension) noexcept;
[[nodiscard]] bool IsCameraRawExtension(std::wstring_view lowerCaseExtension) noexcept;

} // namespace quicksift::core
