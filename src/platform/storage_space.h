// OWNER: Windows volume discovery and conservative free-space preflight.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace quicksift::platform {

[[nodiscard]] std::optional<std::uint64_t> FreeBytesForPath(
    const std::filesystem::path& path);
[[nodiscard]] bool HasStorageHeadroom(const std::filesystem::path& path,
    std::uint64_t requiredBytes, std::uint64_t reserveBytes,
    std::uint64_t* availableOut = nullptr);
[[nodiscard]] std::optional<std::wstring> VolumeRootForPath(
    const std::filesystem::path& path);

} // namespace quicksift::platform
