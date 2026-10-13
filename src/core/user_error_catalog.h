// OWNER: The single table that maps internal error codes to (a) a stable diagnostic
// event name and (b) the English localization keys of the user-facing title and
// message. UI code shows Tr(entry.title) / Tr(entry.message); every key here has a
// Vietnamese pair in src/localization.cpp (enforced by scripts/check-localization.py).
#pragma once

#include "core/write_retry_policy.h"

#include <cstdint>
#include <string_view>

namespace quicksift::core {

enum class UserErrorCode : std::uint8_t {
    None = 0,
    WriteInUseWaiting,     // non-blocking status while another app holds a file
    WriteInUseGaveUp,      // still locked after ~30 s (Retry / Skip offered)
    WriteSkipped,          // user chose Skip while waiting
    WriteConflict,         // file changed outside QuickSift (reconcile offered)
    WriteLowStorage,
    WriteReadOnly,
    WriteAccessDenied,
    WriteDeviceBusy,
    WriteVerification,
    WriteUnsupported,
    WriteInvalidValue,
    WriteFailed,
    WriteCancelled,
    WriteGateTimeout,      // own decoders did not release the file in time
    MetadataEditQueued,    // edit coalesced behind a running write
    CacheDatabaseBusy,
    CacheDatabaseMigration,
    SearchReadFailed,
    RatingsImportFailed,
    RatingsExportFailed,
    BatchFailed,
    WorkerException,
};

struct UserErrorEntry {
    UserErrorCode code;
    std::wstring_view event;   // diagnostic event name (stable, English, never localized)
    std::wstring_view title;   // English localization key
    std::wstring_view message; // English localization key ({file} placeholder allowed)
};

[[nodiscard]] const UserErrorEntry& UserError(UserErrorCode code) noexcept;
[[nodiscard]] UserErrorCode UserErrorForWriteFailure(write_lane::WriteFailureKind kind) noexcept;
// Every entry, for checks and the localization script.
[[nodiscard]] const UserErrorEntry* UserErrorTable(std::size_t& count) noexcept;

} // namespace quicksift::core
