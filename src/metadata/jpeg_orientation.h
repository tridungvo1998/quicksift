// CODE GUIDE: See CODE_GUIDE.md -> "Reading metadata".
// OWNER: Bounded JPEG/EXIF orientation parsing; never read beyond the active APP1 segment.

#pragma once

#include <cstdint>
#include <span>

namespace quicksift::metadata {

// Returns the EXIF orientation in [1, 8]. Malformed, missing, or unsupported
// metadata deliberately falls back to the neutral orientation (1).
[[nodiscard]] int ReadJpegOrientation(std::span<const std::uint8_t> bytes) noexcept;

} // namespace quicksift::metadata
