// CODE GUIDE: See CODE_GUIDE.md -> "The authoritative owners".
// OWNER: Single JPEG/RAW extension registry used by scanning, decoding, and sidecars.

#include "image_formats.h"

#include <array>

namespace quicksift::core {

bool IsJpegExtension(std::wstring_view extension) noexcept {
    return extension == L".jpg" || extension == L".jpeg" || extension == L".jpe";
}

bool IsCameraRawExtension(std::wstring_view extension) noexcept {
    static constexpr std::array<std::wstring_view, 29> rawExtensions{
        L".3fr", L".ari", L".arw", L".bay", L".cr2", L".cr3", L".crw", L".dcr",
        L".dng", L".erf", L".fff", L".iiq", L".k25", L".kdc", L".mef", L".mos",
        L".mrw", L".nef", L".nrw", L".orf", L".pef", L".raf", L".raw", L".rw2",
        L".rwl", L".sr2", L".srf", L".srw", L".x3f"
    };
    for (const std::wstring_view candidate : rawExtensions) {
        if (extension == candidate) return true;
    }
    return false;
}

} // namespace quicksift::core
