// OWNER: Windows volume discovery and conservative free-space preflight.
#include "storage_space.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <limits>

namespace quicksift::platform {
namespace fs = std::filesystem;

std::optional<std::uint64_t> FreeBytesForPath(const fs::path& path) {
    fs::path probe = path;
    std::error_code error;
    if (!fs::is_directory(probe, error)) probe = probe.parent_path();
    error.clear();
    while (!probe.empty() && !fs::exists(probe, error)) {
        probe = probe.parent_path();
        error.clear();
    }
    if (probe.empty()) return std::nullopt;
    ULARGE_INTEGER available{};
    if (!GetDiskFreeSpaceExW(probe.c_str(), &available, nullptr, nullptr)) return std::nullopt;
    return available.QuadPart;
}

bool HasStorageHeadroom(const fs::path& path, std::uint64_t requiredBytes,
    std::uint64_t reserveBytes, std::uint64_t* availableOut) {
    const auto available = FreeBytesForPath(path);
    if (!available) return true; // The actual write remains authoritative.
    if (availableOut) *availableOut = *available;
    if (requiredBytes > std::numeric_limits<std::uint64_t>::max() - reserveBytes) return false;
    return *available >= requiredBytes + reserveBytes;
}

std::optional<std::wstring> VolumeRootForPath(const fs::path& path) {
    fs::path probe = path;
    std::error_code error;
    if (!fs::is_directory(probe, error)) probe = probe.parent_path();
    error.clear();
    while (!probe.empty() && !fs::exists(probe, error)) {
        probe = probe.parent_path();
        error.clear();
    }
    if (probe.empty()) return std::nullopt;
    std::array<wchar_t, 32768> root{};
    if (!GetVolumePathNameW(probe.c_str(), root.data(),
        static_cast<DWORD>(root.size()))) return std::nullopt;
    std::wstring value = root.data();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(character)));
    });
    return value;
}

} // namespace quicksift::platform
