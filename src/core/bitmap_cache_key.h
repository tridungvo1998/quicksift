// CODE GUIDE: Non-owning bitmap probes preserve the existing case-sensitive cache identity.
#pragma once
#include "core/app_types.h"
#include <functional>
#include <string>
#include <string_view>
namespace quicksift::core {
struct CacheKeyView {
    std::wstring_view path;
    int size = 0;
    CacheClass cacheClass = CacheClass::Preview;
};
struct CacheKey {
    std::wstring path;
    int size = 0;
    CacheClass cacheClass = CacheClass::Preview;
    operator CacheKeyView() const noexcept { return {path, size, cacheClass}; }
    bool operator==(const CacheKey&) const noexcept = default;
};
struct CacheKeyHash {
    using is_transparent = void;
    size_t operator()(CacheKeyView key) const noexcept {
        const auto sizeHash = static_cast<std::uint64_t>(key.size) * 0x9E3779B185EBCA87ull;
        const auto classShift = static_cast<std::uint64_t>(key.cacheClass) << 58;
        return std::hash<std::wstring_view>{}(key.path) ^ static_cast<size_t>(sizeHash ^ classShift);
    }
};
struct CacheKeyEqual {
    using is_transparent = void;
    bool operator()(CacheKeyView a, CacheKeyView b) const noexcept {
        return a.size == b.size && a.cacheClass == b.cacheClass && a.path == b.path;
    }
};
struct PathTextHash {
    using is_transparent = void;
    size_t operator()(std::wstring_view path) const noexcept { return std::hash<std::wstring_view>{}(path); }
};
struct PathTextEqual {
    using is_transparent = void;
    bool operator()(std::wstring_view a, std::wstring_view b) const noexcept { return a == b; }
};
} // namespace quicksift::core
