// OWNER: Portable locator for the standard XMP packet in a JPEG header prefix.
// Used by the embedded-metadata fast path: when the packet is found (or proven
// absent) from the first bytes of the file, reading rating/label/pick needs no
// Exiv2 open, no full metadata parse and no process-wide Exiv2 serialization.
// Anything unusual is reported as Inconclusive so the caller falls back to Exiv2.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <optional>
#include <string_view>

#include "core/metadata_state.h"

namespace quicksift::metadata {

enum class JpegXmpStatus {
    Found,        // exactly one standard XMP APP1 packet, fully inside the prefix
    Absent,       // marker walk reached start-of-scan/end-of-image: no XMP at all
    Inconclusive, // not a JPEG, truncated, malformed, extended or multiple packets
};

struct JpegXmpLocation {
    JpegXmpStatus status = JpegXmpStatus::Inconclusive;
    std::string_view packet; // valid only for Found; points into the input bytes
};

[[nodiscard]] JpegXmpLocation LocateJpegXmp(std::span<const std::uint8_t> prefix) noexcept;

// Rating (xmp:Rating 0..5), color label (xmp:Label) and pick (quicksift:Pick -1..1)
// from a JPEG prefix, with the same results the Exiv2 path produces (absent
// property -> Known 0). Returns nullopt whenever the answer is not certain: no
// conclusive packet, invalid UTF-8, a namespace bound under an unexpected prefix,
// or any field that is not a plain valid value. The caller then uses Exiv2.
[[nodiscard]] std::optional<core::PhotoMetadataValues> FastJpegXmpFields(
    std::span<const std::uint8_t> prefix);

} // namespace quicksift::metadata
