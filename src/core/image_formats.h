// CODE GUIDE: See CODE_GUIDE.md -> "The authoritative owners".
// OWNER: Public image-format registry; callers must not duplicate extension lists.

#pragma once

#include <span>
#include <string_view>

namespace quicksift::core {

// Extension inputs must already be lower case and include the leading dot.
[[nodiscard]] bool IsJpegExtension(std::wstring_view lowerCaseExtension) noexcept;
[[nodiscard]] bool IsCameraRawExtension(std::wstring_view lowerCaseExtension) noexcept;
// Every image extension QuickSift catalogs (lower case, leading dot): the union of
// all FormatGroup members accepted by IsFormatAllowed with every group enabled.
// Used by user-facing extension validation (Batch Processing).
[[nodiscard]] std::span<const std::wstring_view> AllSupportedImageExtensions() noexcept;

} // namespace quicksift::core
