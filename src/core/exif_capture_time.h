// OWNER: Portable EXIF capture-time reader for search hydration. Finds
// DateTimeOriginal (0x9003), else DateTimeDigitized (0x9004), else IFD0 DateTime
// (0x0132) in the first bytes of an image file: JPEG APP1 "Exif", TIFF-based files
// (TIFF/DNG/NEF/ARW/CR2/ORF/RW2/PEF...), and any embedded "Exif\0\0"+TIFF or raw
// TIFF header found by a bounded scan (HEIC/AVIF/WebP/PNG eXIf/CR3 CMT boxes when the
// block lies inside the window). Bounds-checked, no allocation beyond the result,
// no Windows or filesystem dependencies; checked by search_query_checks.cpp.
#pragma once

#include "core/photo_predicate.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace quicksift::core::exif {

// How much of the file the hydration worker reads (front of file only).
inline constexpr std::size_t kCaptureTimeReadBytes = 512u * 1024u;

enum class CaptureTag : std::uint8_t { None, DateTime, Digitized, Original };

struct CaptureTimeResult {
    predicate::LocalDateTime time{};
    CaptureTag tag = CaptureTag::None;
};

// Camera settings extracted in the same bounded read (schema v3 index).
// 0 / empty / -1 means the tag was not present or not parseable.
struct CameraSettingsResult {
    int iso = -1;
    double aperture = 0.0;       // f-number
    double focalLength = 0.0;    // mm
    double shutterSpeed = 0.0;   // seconds
    std::wstring camera;         // Make + Model
    std::wstring lens;
    bool known = false;
};

// "YYYY:MM:DD HH:MM:SS" (also '-' date separators and 'T'); trailing NULs/spaces ok.
[[nodiscard]] std::optional<predicate::LocalDateTime> ParseExifDateTimeText(std::string_view text) noexcept;
// Parses one TIFF structure starting at tiff[0] ("II*\0" / "MM\0*" or any II/MM magic).
[[nodiscard]] std::optional<CaptureTimeResult> ReadTiffCaptureTime(std::span<const std::uint8_t> tiff) noexcept;
// Whole-buffer entry point (file prefix). Best tag wins across all candidates.
[[nodiscard]] std::optional<CaptureTimeResult> FindCaptureTime(std::span<const std::uint8_t> bytes) noexcept;

// Extracts camera settings from the same bounded prefix. Safe to call even when
// FindCaptureTime returns nullopt. Does not allocate beyond the result strings.
[[nodiscard]] CameraSettingsResult FindCameraSettings(std::span<const std::uint8_t> bytes) noexcept;

} // namespace quicksift::core::exif
