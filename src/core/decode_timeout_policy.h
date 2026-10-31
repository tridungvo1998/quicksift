// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#pragma once
// Adaptive decode timeout (portable, checked by decode_policy_checks).
#include <algorithm>

namespace quicksift::core::decode_timeout {

// attempt 0/1/2/3 -> base 1 s, 2 s, 4 s, 8 s; RAW or >10 MB files x1.5.
// Foreground view decodes (Single AND Compare current images, including the
// active RAW's full demosaic) get a real budget: at least 12 s for RAW/large
// files, 8 s otherwise. Before 0.9.5.0 only Single-view Full-class requests got
// it, so Compare decodes, Preview-class view decodes and the active RAW's full
// demosaic were cancelled after 1.5 s, retried at 3 s / 6 s and looked stuck.
[[nodiscard]] constexpr int AdaptiveTimeoutMs(int attempt, bool largeOrRaw, bool foregroundView) noexcept {
    const int boundedAttempt = std::clamp(attempt, 0, 3);
    int base = 1000 << boundedAttempt;
    if (largeOrRaw) base = base * 3 / 2;
    if (foregroundView) base = std::max(base * 4, largeOrRaw ? 12000 : 8000);
    return std::clamp(base, 1000, 60000);
}

} // namespace quicksift::core::decode_timeout
