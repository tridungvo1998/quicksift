// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#pragma once
// Finds the largest displayable JPEG embedded in a RAW container (portable;
// checked by embedded_jpeg_scan_checks).
#include <cstddef>
#include <cstdint>
#include <span>

namespace quicksift::core {

struct EmbeddedJpegLocation {
    std::size_t offset = 0;
    std::size_t length = 0; // SOI..EOI inclusive
    int width = 0;
    int height = 0;
    [[nodiscard]] int LongestEdge() const noexcept { return width > height ? width : height; }
    [[nodiscard]] bool Found() const noexcept { return length > 0; }
};

// Walks every SOI..EOI stream: marker segments are skipped by length (so an
// EXIF thumbnail nested in APP1 is not mistaken for a separate image), and
// after SOS the entropy-coded data is scanned for EOI while skipping FF00
// stuffing and RSTn markers. Only baseline/extended/progressive frames
// (SOF0/1/2) count; lossless SOF3 is RAW sensor data, not a preview.
[[nodiscard]] EmbeddedJpegLocation FindLargestEmbeddedJpeg(std::span<const std::uint8_t> bytes) noexcept;

} // namespace quicksift::core
