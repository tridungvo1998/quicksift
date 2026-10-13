// OWNER: User-facing error table (see user_error_catalog.h).
#include "core/user_error_catalog.h"

#include <array>

namespace quicksift::core {
namespace {
// Keep `message` keys free of other placeholders than {file}; the UI substitutes it.
constexpr std::array kErrors{
    UserErrorEntry{ UserErrorCode::None, L"none", L"", L"" },
    UserErrorEntry{ UserErrorCode::WriteInUseWaiting, L"write.in_use.waiting",
        L"Waiting for a file", L"Waiting for {file} — in use by another app" },
    UserErrorEntry{ UserErrorCode::WriteInUseGaveUp, L"write.in_use.gave_up",
        L"File is in use by another app",
        L"{file} stayed locked by another app for 30 seconds. The new value was not written and the previous value is shown again. Close the other app (or pause sync) and choose Retry, or choose Skip." },
    UserErrorEntry{ UserErrorCode::WriteSkipped, L"write.skipped",
        L"Metadata update skipped",
        L"{file} was skipped because another app is using it. The previous value is shown again." },
    UserErrorEntry{ UserErrorCode::WriteConflict, L"write.conflict",
        L"File changed outside QuickSift",
        L"{file} changed outside QuickSift while the update was waiting. Nothing was overwritten." },
    UserErrorEntry{ UserErrorCode::WriteLowStorage, L"write.low_storage",
        L"Low storage — nothing changed",
        L"There is not enough free space to update {file} safely. The previous value is shown again." },
    UserErrorEntry{ UserErrorCode::WriteReadOnly, L"write.read_only",
        L"File is read-only",
        L"{file} is read-only (or on read-only media). The previous value is shown again." },
    UserErrorEntry{ UserErrorCode::WriteAccessDenied, L"write.access_denied",
        L"Access denied",
        L"Windows denied write access to {file}. The previous value is shown again." },
    UserErrorEntry{ UserErrorCode::WriteDeviceBusy, L"write.device_busy",
        L"Drive is not responding",
        L"The drive holding {file} did not respond. The previous value is shown again." },
    UserErrorEntry{ UserErrorCode::WriteVerification, L"write.verification",
        L"Metadata update could not be verified",
        L"QuickSift could not verify the update of {file}; the original file was kept unchanged." },
    UserErrorEntry{ UserErrorCode::WriteUnsupported, L"write.unsupported",
        L"Metadata cannot be stored in this file",
        L"{file} cannot carry this metadata safely. The previous value is shown again." },
    UserErrorEntry{ UserErrorCode::WriteInvalidValue, L"write.invalid_value",
        L"Metadata update stopped",
        L"The requested value is outside the supported range. Nothing was changed." },
    UserErrorEntry{ UserErrorCode::WriteFailed, L"write.failed",
        L"Metadata update incomplete",
        L"{file} could not be updated safely. The previous value is shown again." },
    UserErrorEntry{ UserErrorCode::WriteCancelled, L"write.cancelled",
        L"Metadata update cancelled",
        L"The metadata update was cancelled at a safe file boundary." },
    UserErrorEntry{ UserErrorCode::WriteGateTimeout, L"write.gate_timeout",
        L"Metadata update is waiting",
        L"An image decoder did not release {file} in time. Try again in a moment." },
    UserErrorEntry{ UserErrorCode::MetadataEditQueued, L"write.queued",
        L"Metadata update queued", L"Saving after the current update…" },
    UserErrorEntry{ UserErrorCode::CacheDatabaseBusy, L"cache.db.busy",
        L"Cache is busy", L"The thumbnail cache is busy; QuickSift will retry in the background." },
    UserErrorEntry{ UserErrorCode::CacheDatabaseMigration, L"cache.db.migration_failed",
        L"Cache was rebuilt", L"The thumbnail cache could not be upgraded and was rebuilt. Thumbnails will be regenerated." },
    UserErrorEntry{ UserErrorCode::SearchReadFailed, L"search.read_failed",
        L"Search is incomplete", L"Some photos could not be read for search and are matched by name only." },
    UserErrorEntry{ UserErrorCode::RatingsImportFailed, L"ratings.import_failed",
        L"Ratings import could not start", L"The ratings file could not be read." },
    UserErrorEntry{ UserErrorCode::RatingsExportFailed, L"ratings.export_failed",
        L"Ratings export failed", L"The ratings file could not be written." },
    UserErrorEntry{ UserErrorCode::BatchFailed, L"batch.failed",
        L"Batch processing could not start", L"The batch could not be prepared." },
    UserErrorEntry{ UserErrorCode::WorkerException, L"worker.exception",
        L"Background task stopped", L"A background task stopped unexpectedly. Details were written to the diagnostic log." },
};
} // namespace

const UserErrorEntry& UserError(UserErrorCode code) noexcept {
    for (const UserErrorEntry& entry : kErrors)
        if (entry.code == code) return entry;
    return kErrors[0];
}

UserErrorCode UserErrorForWriteFailure(write_lane::WriteFailureKind kind) noexcept {
    using write_lane::WriteFailureKind;
    switch (kind) {
    case WriteFailureKind::None: return UserErrorCode::None;
    case WriteFailureKind::InUse: return UserErrorCode::WriteInUseGaveUp;
    case WriteFailureKind::DeviceBusy: return UserErrorCode::WriteDeviceBusy;
    case WriteFailureKind::AccessDenied: return UserErrorCode::WriteAccessDenied;
    case WriteFailureKind::Conflict: return UserErrorCode::WriteConflict;
    case WriteFailureKind::LowStorage: return UserErrorCode::WriteLowStorage;
    case WriteFailureKind::ReadOnly: return UserErrorCode::WriteReadOnly;
    case WriteFailureKind::Verification: return UserErrorCode::WriteVerification;
    case WriteFailureKind::Unsupported: return UserErrorCode::WriteUnsupported;
    case WriteFailureKind::InvalidValue: return UserErrorCode::WriteInvalidValue;
    case WriteFailureKind::Other: return UserErrorCode::WriteFailed;
    }
    return UserErrorCode::WriteFailed;
}

const UserErrorEntry* UserErrorTable(std::size_t& count) noexcept {
    count = kErrors.size();
    return kErrors.data();
}

} // namespace quicksift::core
