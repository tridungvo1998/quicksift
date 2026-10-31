// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#pragma once
// "What is QuickSift waiting for?" model behind the blocking-wait dialog shown
// when Close, ratings import/export or Batch cannot start (portable; checked by
// ui_state_checks). Before 0.9.5.0 those commands refused with a generic
// "wait for the current operation" notice, or (Close) showed a static message
// and then waited silently; nothing said which file, how long, or why.
#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quicksift::core::blocking_wait {

enum class Command { Close, RatingsImport, RatingsExport, Batch };

enum class Reason {
    FileLockedByOtherApp, // metadata write waiting for another process's handle
    WriteRetry,           // metadata write retrying a transient failure
    MetadataWrite,        // metadata write batch running
    FileOperation,        // copy / move / delete transaction running
    RatingsWorker,        // ratings import/export worker running
    BatchWorker,          // batch processing worker running
    UnsavedEdits,         // coalesced rating/pick/label edits not yet written
};

struct Blocker {
    Reason reason = Reason::MetadataWrite;
    std::wstring file;                 // current file (name only), may be empty
    std::size_t completed = 0;
    std::size_t total = 0;             // 0: unknown
    std::chrono::milliseconds elapsed{ 0 };
};

using Translator = std::function<std::wstring(std::wstring_view)>;

// English keys (all present in the localization table).
[[nodiscard]] std::wstring_view CommandKey(Command command) noexcept;
[[nodiscard]] std::wstring_view ReasonKey(Reason reason) noexcept;
[[nodiscard]] std::wstring_view ReasonLogName(Reason reason) noexcept;
// Stable machine names for the diagnostic log.
[[nodiscard]] std::wstring_view CommandLogName(Command command) noexcept;
// "1 m 05 s" style; seconds below one minute ("12 s").
[[nodiscard]] std::wstring FormatElapsed(std::chrono::milliseconds elapsed);
// Multi-line dialog body: one block per blocker (operation, file, left, elapsed, reason).
[[nodiscard]] std::wstring Describe(Command command, const std::vector<Blocker>& blockers,
    const Translator& tr);
// Close may force quit; the others offer "Cancel operation" / "Stop waiting".
[[nodiscard]] constexpr bool OffersForceQuit(Command command) noexcept { return command == Command::Close; }
// Diagnostic-log cadence: first sighting, then every interval while still blocked.
[[nodiscard]] constexpr bool ShouldLog(std::chrono::milliseconds sinceLastLog, bool first) noexcept {
    return first || sinceLastLog >= std::chrono::milliseconds(5000);
}

} // namespace quicksift::core::blocking_wait
