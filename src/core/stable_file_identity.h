// CODE GUIDE: See CODE_GUIDE.md -> "Copying or moving files".
// OWNER: Stable file identity used by guarded Undo; path renames must not invalidate it.

#pragma once

#include <cstdint>

namespace quicksift::core {

// Windows file ChangeTime is deliberately absent: NTFS may update it for a
// rename even when the same file and bytes were moved. Guarded Undo needs an
// identity that survives QuickSift's own rename while still detecting normal
// replacement or content edits.
struct StableFileIdentityFields {
    std::uint64_t volumeSerial = 0;
    std::uint64_t fileIndex = 0;
    std::uint64_t lastWriteTime = 0;
};

[[nodiscard]] constexpr std::int64_t StableFileIdentityStamp(
    const StableFileIdentityFields& fields) noexcept {
    std::uint64_t fingerprint = 1469598103934665603ull;
    auto mix64 = [&](std::uint64_t value) constexpr {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            fingerprint ^= (value >> shift) & 0xffu;
            fingerprint *= 1099511628211ull;
        }
    };
    mix64(fields.volumeSerial);
    mix64(fields.fileIndex);
    mix64(fields.lastWriteTime);
    return static_cast<std::int64_t>(fingerprint);
}

} // namespace quicksift::core
