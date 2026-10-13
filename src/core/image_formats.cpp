// CODE GUIDE: See CODE_GUIDE.md -> "The authoritative owners".
// OWNER: Single JPEG/RAW extension registry used by scanning, decoding, and sidecars.

#include "image_formats.h"

#include <array>

namespace quicksift::core {

bool IsJpegExtension(std::wstring_view extension) noexcept {
    return extension == L".jpg" || extension == L".jpeg" || extension == L".jpe";
}

namespace {
constexpr std::array<std::wstring_view, 29> kRawExtensions{
    L".3fr", L".ari", L".arw", L".bay", L".cr2", L".cr3", L".crw", L".dcr",
    L".dng", L".erf", L".fff", L".iiq", L".k25", L".kdc", L".mef", L".mos",
    L".mrw", L".nef", L".nrw", L".orf", L".pef", L".raf", L".raw", L".rw2",
    L".rwl", L".sr2", L".srf", L".srw", L".x3f"
};

// JPEG, PNG, AVIF/HEIF, TIFF, basic (BMP/DIB/GIF), WebP, then camera RAW: must
// stay in sync with IsFormatAllowed (catalog_policy.cpp); the portable batch
// checks assert both directions.
constexpr std::array<std::wstring_view, 42> kAllSupportedExtensions{
    L".jpg", L".jpeg", L".jpe", L".png", L".avif", L".heic", L".heif",
    L".tif", L".tiff", L".bmp", L".dib", L".gif", L".webp",
    L".3fr", L".ari", L".arw", L".bay", L".cr2", L".cr3", L".crw", L".dcr",
    L".dng", L".erf", L".fff", L".iiq", L".k25", L".kdc", L".mef", L".mos",
    L".mrw", L".nef", L".nrw", L".orf", L".pef", L".raf", L".raw", L".rw2",
    L".rwl", L".sr2", L".srf", L".srw", L".x3f"
};
} // namespace

bool IsCameraRawExtension(std::wstring_view extension) noexcept {
    for (const std::wstring_view candidate : kRawExtensions) {
        if (extension == candidate) return true;
    }
    return false;
}

std::span<const std::wstring_view> AllSupportedImageExtensions() noexcept {
    return { kAllSupportedExtensions.data(), kAllSupportedExtensions.size() };
}

} // namespace quicksift::core
