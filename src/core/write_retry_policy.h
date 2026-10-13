// OWNER: Metadata write-lane failure classification and the lock-resolution retry
// state machine. Portable and deterministic (scripts/checks/write_lane_checks.cpp).
//
// Flow for one file write (runs on the metadata transaction thread, never the UI):
//   1. Own handles: the transaction already holds the exclusive path gate, so this
//      process's decoders / metadata readers / search reads on the path are cancelled
//      and drained before the first attempt (Worker::BeginExclusivePathWrites).
//   2. Quick phase: a transient "in use" failure (sharing / lock violation, user-mapped
//      section, access denied by an open handle, device busy) is retried with backoff
//      50, 100, 200, 400, 800, 1000... ms for kQuickBudget (~3 s). The UI keeps
//      showing the optimistic value.
//   3. External phase: still in use after the quick budget means another process
//      holds it. Keep retrying every kExternalInterval until kExternalBudget (30 s),
//      and report "waiting" once so the UI can show a non-blocking status.
//   4. Every retry re-runs the full verified write (private temp copy + handle-bound
//      publication + compare-and-swap of expected values), so retries cannot corrupt.
//   5. Abort immediately (no retry) when continuing would risk real corruption or can
//      never succeed: the file changed under us (CAS / identity -> reconcile flow),
//      verification failed (partial write), disk full, read-only media / attribute,
//      unsupported container, invalid value. The UI then rolls back the optimistic
//      value with a localized message.
#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

namespace quicksift::core::write_lane {

enum class WriteFailureKind : std::uint8_t {
    None = 0,
    InUse,          // transient: another handle holds the file (retry)
    DeviceBusy,     // transient: device / network hiccup (retry, quick phase only)
    AccessDenied,   // maybe a handle without FILE_SHARE_DELETE, maybe an ACL (quick phase only)
    Conflict,       // file or expected value changed outside QuickSift (abort -> reconcile)
    LowStorage,     // disk full (abort)
    ReadOnly,       // read-only attribute / media / share (abort)
    Verification,   // write could not be verified / partial (abort, original kept)
    Unsupported,    // container cannot carry the metadata (abort)
    InvalidValue,   // patch outside supported range (abort)
    Other,          // anything else (abort)
};

[[nodiscard]] std::wstring_view FailureKindName(WriteFailureKind kind) noexcept;
[[nodiscard]] bool IsTransient(WriteFailureKind kind) noexcept;

// Win32 error code -> failure kind (codes are plain numbers so this stays portable).
[[nodiscard]] WriteFailureKind ClassifyWin32Error(std::uint32_t error) noexcept;

inline constexpr std::chrono::milliseconds kQuickBudget{ 3000 };
inline constexpr std::chrono::milliseconds kExternalBudget{ 30000 };
inline constexpr std::chrono::milliseconds kHardTimeout{ 15000 }; // force cancel + lock release
inline constexpr std::chrono::milliseconds kExternalInterval{ 1000 };
inline constexpr std::chrono::milliseconds kMaxQuickDelay{ 1000 };

enum class RetryAction : std::uint8_t {
    Done,          // write succeeded
    RetryAfter,    // wait `delay`, then retry
    Abort,         // stop this file; roll back its optimistic value
};

enum class RetryPhase : std::uint8_t { Quick, External };

struct RetryDecision {
    RetryAction action = RetryAction::Done;
    std::chrono::milliseconds delay{ 0 };
    RetryPhase phase = RetryPhase::Quick;
    bool enteredExternalPhase = false; // first decision of the external phase (notify once)
    WriteFailureKind reason = WriteFailureKind::None;
};

// Per-file retry state machine. Feed it every attempt's outcome with the elapsed time
// since the first attempt; it never sleeps itself.
class WriteRetryState {
public:
    [[nodiscard]] RetryDecision OnAttempt(WriteFailureKind outcome,
        std::chrono::milliseconds elapsed) noexcept;
    // The user chose "Skip" (or the transaction was cancelled): stop at once.
    [[nodiscard]] RetryDecision OnCancel() noexcept;
    [[nodiscard]] unsigned Attempts() const noexcept { return attempts_; }
    [[nodiscard]] RetryPhase Phase() const noexcept { return phase_; }

private:
    unsigned attempts_ = 0;
    unsigned quickRetries_ = 0;
    RetryPhase phase_ = RetryPhase::Quick;
};

// Backoff for the n-th quick retry (0-based): 50, 100, 200, 400, 800, 1000, 1000...
[[nodiscard]] std::chrono::milliseconds QuickBackoff(unsigned retryIndex) noexcept;

} // namespace quicksift::core::write_lane
