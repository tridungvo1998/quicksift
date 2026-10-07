// OWNER: Platform-neutral case-insensitive path hashing and collision checks.
#include "path_identity.h"

#include <algorithm>
#include <cwctype>

namespace quicksift::core {

std::uint64_t PathFingerprint(std::wstring_view path) noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    for (wchar_t character : path) {
        const auto folded = static_cast<std::uint32_t>(
            std::towlower(static_cast<wint_t>(character)));
        hash ^= static_cast<std::uint16_t>(folded);
        hash *= 1099511628211ull;
        hash ^= static_cast<std::uint16_t>(folded >> 16);
        hash *= 1099511628211ull;
    }
    return hash;
}

bool PathsEqualInsensitive(
    std::wstring_view left, std::wstring_view right) noexcept {
    if (left.size() != right.size()) return false;
    return std::equal(left.begin(), left.end(), right.begin(), [](wchar_t a, wchar_t b) {
        return std::towlower(static_cast<wint_t>(a)) ==
            std::towlower(static_cast<wint_t>(b));
    });
}

} // namespace quicksift::core
