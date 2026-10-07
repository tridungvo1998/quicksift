// CODE GUIDE: See CODE_GUIDE.md -> "Reading metadata".
// OWNER: Bounded JPEG/EXIF orientation parsing; validate every offset against its APP1 segment.

#include "jpeg_orientation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>

namespace quicksift::metadata {
namespace {

bool AddWithin(std::size_t left, std::size_t right, std::size_t limit,
               std::size_t& result) noexcept {
    if (left > limit || right > limit - left) return false;
    result = left + right;
    return true;
}

} // namespace

int ReadJpegOrientation(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() < 4 || bytes[0] != 0xFF || bytes[1] != 0xD8) return 1;

    std::size_t offset = 2;
    while (offset < bytes.size()) {
        // JPEG permits any number of 0xFF fill bytes before a marker.
        if (bytes[offset] != 0xFF) {
            ++offset;
            continue;
        }
        while (offset < bytes.size() && bytes[offset] == 0xFF) ++offset;
        if (offset >= bytes.size()) break;
        const std::uint8_t marker = bytes[offset++];
        if (marker == 0x00) continue;
        if (marker == 0xD9 || marker == 0xDA) break; // EOI or start of entropy data.
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) continue;
        if (bytes.size() - offset < 2) break;

        const std::size_t segmentLength =
            (static_cast<std::size_t>(bytes[offset]) << 8) | bytes[offset + 1];
        if (segmentLength < 2 || segmentLength > bytes.size() - offset) break;
        const std::size_t segmentEnd = offset + segmentLength;
        const std::size_t payload = offset + 2;

        if (marker == 0xE1 && segmentEnd - payload >= 14 &&
            std::memcmp(bytes.data() + payload, "Exif\0\0", 6) == 0) {
            const std::size_t tiff = payload + 6;
            if (segmentEnd - tiff < 8) return 1;
            const bool little = bytes[tiff] == 'I' && bytes[tiff + 1] == 'I';
            const bool big = bytes[tiff] == 'M' && bytes[tiff + 1] == 'M';
            if (!little && !big) return 1;

            auto read16 = [&](std::size_t position, std::uint16_t& value) noexcept {
                if (position > segmentEnd || segmentEnd - position < 2) return false;
                if (little) {
                    value = static_cast<std::uint16_t>(bytes[position]) |
                        static_cast<std::uint16_t>(bytes[position + 1]) << 8;
                } else {
                    value = static_cast<std::uint16_t>(bytes[position]) << 8 |
                        static_cast<std::uint16_t>(bytes[position + 1]);
                }
                return true;
            };
            auto read32 = [&](std::size_t position, std::uint32_t& value) noexcept {
                if (position > segmentEnd || segmentEnd - position < 4) return false;
                if (little) {
                    value = static_cast<std::uint32_t>(bytes[position]) |
                        static_cast<std::uint32_t>(bytes[position + 1]) << 8 |
                        static_cast<std::uint32_t>(bytes[position + 2]) << 16 |
                        static_cast<std::uint32_t>(bytes[position + 3]) << 24;
                } else {
                    value = static_cast<std::uint32_t>(bytes[position]) << 24 |
                        static_cast<std::uint32_t>(bytes[position + 1]) << 16 |
                        static_cast<std::uint32_t>(bytes[position + 2]) << 8 |
                        static_cast<std::uint32_t>(bytes[position + 3]);
                }
                return true;
            };

            std::uint16_t tiffMagic = 0;
            std::uint32_t ifdOffset = 0;
            if (!read16(tiff + 2, tiffMagic) || tiffMagic != 42 ||
                !read32(tiff + 4, ifdOffset)) return 1;
            // TIFF offsets are relative to the TIFF header and the first IFD
            // must not point back into the eight-byte byte-order/magic header.
            if (ifdOffset < 8) return 1;
            std::size_t ifd = 0;
            if (!AddWithin(tiff, static_cast<std::size_t>(ifdOffset), segmentEnd, ifd)) return 1;

            std::uint16_t entryCount = 0;
            if (!read16(ifd, entryCount)) return 1;
            std::size_t entry = 0;
            if (!AddWithin(ifd, 2, segmentEnd, entry)) return 1;
            for (std::uint16_t index = 0; index < entryCount; ++index) {
                if (segmentEnd - entry < 12) return 1;
                std::uint16_t tag = 0;
                std::uint16_t type = 0;
                std::uint32_t count = 0;
                if (!read16(entry, tag) || !read16(entry + 2, type) ||
                    !read32(entry + 4, count)) return 1;
                if (tag == 0x0112) {
                    // EXIF Orientation is one SHORT stored inline. Refuse unusual
                    // encodings rather than treating an offset as the value.
                    if (type != 3 || count != 1) return 1;
                    std::uint16_t orientation = 0;
                    if (!read16(entry + 8, orientation)) return 1;
                    return orientation >= 1 && orientation <= 8 ? orientation : 1;
                }
                if (!AddWithin(entry, 12, segmentEnd, entry)) return 1;
            }
        }
        offset = segmentEnd;
    }
    return 1;
}

} // namespace quicksift::metadata
