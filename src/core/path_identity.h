// OWNER: Platform-neutral case-insensitive path identity used by the catalog.
#pragma once

#include <cstdint>
#include <string_view>

namespace quicksift::core {

[[nodiscard]] std::uint64_t PathFingerprint(std::wstring_view path) noexcept;
[[nodiscard]] bool PathsEqualInsensitive(
    std::wstring_view left, std::wstring_view right) noexcept;

} // namespace quicksift::core
