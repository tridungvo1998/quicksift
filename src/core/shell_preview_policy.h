// CODE GUIDE: Disposable Shell preview identity, retry and bounds.
#pragma once
#include <cstdint>
namespace quicksift::core {
inline constexpr int kShellPreviewMaximumEdge = 1024;
inline constexpr unsigned kShellPreviewQueueLimit = 16;
inline constexpr unsigned kShellPreviewTimeoutMs = 5000;
struct ShellPreviewIdentity {
    std::uint64_t generation = 0, fileSize = 0;
    std::int64_t modifiedStamp = 0;
    int target = 0;
    bool operator==(const ShellPreviewIdentity&) const = default;
};
inline bool ShellPreviewReusable(ShellPreviewIdentity stored, ShellPreviewIdentity wanted,
    bool failed, std::int64_t ageMs) noexcept {
    return stored == wanted && (!failed || ageMs < 30000);
}
}
