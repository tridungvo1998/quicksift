// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#pragma once
// Terminal-state rule for metadata (rating/pick/label) reads (portable; checked
// by metadata_progress_checks). Every dispatched read ends exactly once as:
//   Apply           - values (Known/Unsupported/Unreadable) go to the catalog;
//   Retry           - cancelled/failed/still-unknown read is queued again (bounded);
//   MarkUnreadable  - retry budget spent: unknown fields become Unreadable, so
//                     "Rating pending" ends and filters stop waiting for it.
// Before 0.9.5.0 a read cancelled before it started (e.g. Predictive work while
// the UI suspended prefetch) posted an empty Unknown result that overwrote the
// photo and was never queued again: "Rating pending" forever.
#include <cstdint>

namespace quicksift::core::metadata_progress {

inline constexpr int kMaxReadAttempts = 3;

enum class Outcome : std::uint8_t { Apply, Retry, MarkUnreadable };

[[nodiscard]] constexpr Outcome Classify(bool cancelled, bool workFailed, bool anyFieldUnknown,
    int attemptsSoFar) noexcept {
    if (!cancelled && !workFailed && !anyFieldUnknown) return Outcome::Apply;
    return attemptsSoFar + 1 < kMaxReadAttempts ? Outcome::Retry : Outcome::MarkUnreadable;
}

} // namespace quicksift::core::metadata_progress
