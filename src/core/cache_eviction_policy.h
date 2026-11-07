// CODE GUIDE: Eviction ranking and incoming-byte reservation; policy unchanged.
#pragma once
#include <cstddef>
#include <cstdint>
namespace quicksift::core {
struct EvictionRank { bool recent, face, frequent; std::uint64_t lastUse; };
inline bool EvictionRankLess(EvictionRank a, EvictionRank b) noexcept {
    if (a.recent != b.recent) return !a.recent;
    if (a.face != b.face) return !a.face;
    if (a.frequent != b.frequent) return !a.frequent;
    return a.lastUse < b.lastUse;
}
inline bool EvictionEligible(bool protectedEntry, bool force) noexcept { return force || !protectedEntry; }
inline std::size_t EvictionReservationLimit(std::size_t limit, std::size_t incoming) noexcept {
    return incoming >= limit ? 0 : limit - incoming;
}
}
