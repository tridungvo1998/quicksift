// OWNER: Compiled QuickSiftApplication feature module.
#include "app/quicksift_application_internal.h"
#include "transactions/verified_file_operations_windows.h"
#include "ui/metadata_photo_values.h"
#include "core/priority_policy.h"
#include "core/log_rate_limiter.h"
#include "core/write_retry_policy.h"
#include "diagnostics/log_categories.h"

namespace quicksift::app {

// CODE GUIDE: See CODE_GUIDE.md -> "Writing metadata".
// OWNER: Metadata/file transactions and history; preserve preflight, flush, verification, commit, and rollback.

// Metadata/history safety: snapshots, storage preflight, transactional metadata
// updates, verified copy/move primitives, and bounded undo/redo history.

using metadata_values::ValuesFromPhoto;
using metadata_values::ApplyValuesToPhoto;

std::vector<fs::path> QuickSiftApplicationImpl::ActivePaths() const {
    std::vector<fs::path> paths;
    if (reviewState_.Mode() == ViewMode::Single && !reviewState_.SinglePath().empty()) {
        paths.emplace_back(reviewState_.SinglePath());
    } else if (reviewState_.Mode() == ViewMode::Compare && !reviewState_.ComparePaths().empty()) {
        paths.reserve(reviewState_.ComparePaths().size());
        for (const auto& path : reviewState_.ComparePaths()) paths.emplace_back(path);
    } else {
        paths.reserve(std::min(selection_.Size(), catalog_.VisibleCount()));
        for (size_t visibleIndex = 0; visibleIndex < catalog_.VisibleCount(); ++visibleIndex) {
            const PhotoItem& photo = VisiblePhoto(visibleIndex);
            if (selection_.Contains(photo.path.wstring())) paths.push_back(photo.path);
        }
    }
    return paths;
}

std::optional<MetadataSnapshot> QuickSiftApplicationImpl::SnapshotForPath(
    const fs::path& path) const {
    if (const auto index = CatalogIndexForPath(path.wstring())) {
        const PhotoItem& item = catalog_.PhotoAt(*index);
        MetadataSnapshot snapshot;
        snapshot.path = path;
        snapshot.values = ValuesFromPhoto(item);
        snapshot.revision = item.metadataRevision;
        snapshot.storage = UsesDirectMetadata(path) ? MetadataStorageKind::Embedded :
            MetadataStorageKind::Sidecar;
        snapshot.jpeg = IsJpegPath(path);
        snapshot.safeJpegWrite = safeJpegMetadataWrites_;
        return snapshot;
    }
    return std::nullopt;
}

void QuickSiftApplicationImpl::UpdateHistoryButtons() {
    const bool transactionIdle = !replayingHistory_ && !transactions_.Busy();
    const bool undoEnabled = transactionIdle && historyStore_.CanUndo();
    const bool redoEnabled = transactionIdle && historyStore_.CanRedo();
    if (static_cast<int>(undoEnabled) != undoEnabledState_) {
        undoEnabledState_ = static_cast<int>(undoEnabled);
        if (controls_.contains(ID_UNDO)) {
            EnableWindow(controls_[ID_UNDO], undoEnabled);
            InvalidateRect(controls_[ID_UNDO], nullptr, FALSE);
        }
    }
    if (static_cast<int>(redoEnabled) != redoEnabledState_) {
        redoEnabledState_ = static_cast<int>(redoEnabled);
        if (controls_.contains(ID_REDO)) {
            EnableWindow(controls_[ID_REDO], redoEnabled);
            InvalidateRect(controls_[ID_REDO], nullptr, FALSE);
        }
    }
}

void QuickSiftApplicationImpl::FinishUnrecordedAction(bool changed) {
    if (!changed) return;
    historyStore_.ClearRedo();
    UpdateHistoryButtons();
    ShowToast(L"This unusually large batch was completed without an in-memory Undo record to protect available RAM.");
}

void QuickSiftApplicationImpl::PushHistory(HistoryEntry entry) {
    if (replayingHistory_) return;
    if (!historyStore_.RecordNew(std::move(entry))) {
        ShowToast(L"This unusually large batch was completed without an in-memory Undo record to protect available RAM.");
    }
    UpdateHistoryButtons();
}

// Runs on the metadata transaction thread (callbacks.preflight): stats and free-space
// queries never touch the UI thread. Pure: no UI, no members.
std::optional<quicksift::transactions::MetadataStorageShortfall>
QuickSiftApplicationImpl::MetadataStorageShortfallFor(const std::vector<MetadataSnapshot>& targets) {
    struct Requirement {
        fs::path probe;
        std::uint64_t permanentBytes = 0;
        std::uint64_t temporaryBytes = 0;
    };
    std::unordered_map<std::wstring, Requirement> requirements;
    constexpr std::uint64_t kSidecarGrowthAllowance = 256ull * 1024ull;
    constexpr std::uint64_t kDirectGrowthAllowance = 8ull * 1024ull * 1024ull;
    constexpr std::uint64_t kReserve = 64ull * 1024ull * 1024ull;

    for (const MetadataSnapshot& target : targets) {
        const fs::path storagePath = target.storage == MetadataStorageKind::Embedded ?
            target.path : SidecarPathFor(target.path);
        const auto volume = VolumeRootForPath(storagePath);
        if (!volume) continue;
        Requirement& requirement = requirements[*volume];
        requirement.probe = storagePath;
        if (target.storage == MetadataStorageKind::Sidecar) {
            std::error_code ec;
            const std::uint64_t existing = static_cast<std::uint64_t>(
                fs::file_size(storagePath, ec));
            requirement.permanentBytes = SaturatingAdd(requirement.permanentBytes,
                kSidecarGrowthAllowance);
            requirement.temporaryBytes = std::max(requirement.temporaryBytes,
                SaturatingAdd(ec ? 0 : existing, kSidecarGrowthAllowance));
            continue;
        }

        requirement.permanentBytes = SaturatingAdd(requirement.permanentBytes,
            kDirectGrowthAllowance);
        const bool safeCopy = !target.jpeg || target.safeJpegWrite;
        if (safeCopy) {
            std::error_code ec;
            const std::uintmax_t size = fs::file_size(target.path, ec);
            if (ec) {
                quicksift::transactions::MetadataStorageShortfall shortfall;
                shortfall.sizeUnknown = true;
                return shortfall;
            }
            requirement.temporaryBytes = std::max(requirement.temporaryBytes,
                SaturatingAdd(static_cast<std::uint64_t>(size), kDirectGrowthAllowance));
        }
    }

    for (const auto& [volume, requirement] : requirements) {
        const std::uint64_t needed = SaturatingAdd(
            SaturatingAdd(requirement.permanentBytes, requirement.temporaryBytes), kReserve);
        const auto freeBytes = FreeBytesForPath(requirement.probe);
        if (!freeBytes || *freeBytes < needed) {
            quicksift::transactions::MetadataStorageShortfall shortfall;
            shortfall.volume = volume;
            shortfall.requiredBytes = needed;
            shortfall.availableBytes = freeBytes;
            return shortfall;
        }
    }
    return std::nullopt;
}

void QuickSiftApplicationImpl::ShowMetadataStorageShortfall(
    const quicksift::transactions::MetadataStorageShortfall& shortfall,
    const std::wstring& operation) {
    if (shortfall.sizeUnknown) {
        ShowGlassAlert(L"Metadata update stopped",
            L"QuickSift could not determine an image size before preparing a verified metadata update. Nothing was changed.",
            GlassAlertKind::Warning);
        return;
    }
    std::wstring message = FillToken(Tr(L"{operation} needs cumulative free space for the complete batch."),
        L"{operation}", operation);
    message += L"\n\n" + Tr(L"Volume:") + L" " + shortfall.volume;
    message += L"\n" + Tr(L"Required:") + L" " + FormatFileSize(shortfall.requiredBytes);
    if (shortfall.availableBytes)
        message += L"\n" + Tr(L"Available:") + L" " + FormatFileSize(*shortfall.availableBytes);
    message += L"\n\n" + Tr(L"Nothing was changed.");
    ShowGlassAlertText(Tr(L"Low storage — nothing changed"), message, GlassAlertKind::Warning);
}

quicksift::transactions::MetadataWriteOutcome
QuickSiftApplicationImpl::WriteMetadataPatchFile(const fs::path& path,
    const MetadataPatch& patch, MetadataStorageKind storage, bool jpeg,
    bool safeJpegWrites) {
    quicksift::transactions::MetadataWriteOutcome outcome;
    if (storage == MetadataStorageKind::Sidecar) {
        const SidecarMetadataWriteResult sidecar = WritePhotoMetadataPatch(path, patch);
        outcome.ok = sidecar.ok;
        outcome.conflict = sidecar.conflict;
        outcome.before = sidecar.before;
        outcome.after = sidecar.after;
        outcome.detail = sidecar.detail;
        outcome.win32Error = sidecar.win32Error;
        if (!sidecar.ok) {
            using quicksift::core::write_lane::WriteFailureKind;
            outcome.failureKind = sidecar.conflict ? WriteFailureKind::Conflict :
                (sidecar.lowStorage ? WriteFailureKind::LowStorage :
                (sidecar.win32Error != 0 ?
                    quicksift::core::write_lane::ClassifyWin32Error(sidecar.win32Error) :
                    WriteFailureKind::Other));
        }
        return outcome;
    }

    const bool fastJpegWrite = jpeg && !safeJpegWrites;
    EmbeddedMetadataWriteResult result = fastJpegWrite ?
        WriteEmbeddedMetadataInPlace(path, patch) : WriteEmbeddedMetadataAtomic(path, patch);
    const bool directOpenDenied = !result.ok && fastJpegWrite &&
        result.failure == EmbeddedMetadataFailure::WriteFailed &&
        (result.detail.find(L"Permission denied") != std::wstring::npos ||
         result.detail.find(L"errno = 13") != std::wstring::npos ||
         result.detail.find(L"Failed to open file (w+b)") != std::wstring::npos);
    if (directOpenDenied) result = WriteEmbeddedMetadataAtomic(path, patch);
    outcome.ok = result.ok;
    outcome.conflict = result.failure == EmbeddedMetadataFailure::SourceChanged;
    outcome.before = result.before;
    outcome.after = result.after;
    outcome.detail = result.detail;
    outcome.win32Error = result.win32Error;
    if (!result.ok) {
        using quicksift::core::write_lane::WriteFailureKind;
        switch (result.failure) {
        case EmbeddedMetadataFailure::InvalidValue: outcome.failureKind = WriteFailureKind::InvalidValue; break;
        case EmbeddedMetadataFailure::LowStorage: outcome.failureKind = WriteFailureKind::LowStorage; break;
        case EmbeddedMetadataFailure::UnsupportedFormat: outcome.failureKind = WriteFailureKind::Unsupported; break;
        case EmbeddedMetadataFailure::SourceChanged: outcome.failureKind = WriteFailureKind::Conflict; break;
        case EmbeddedMetadataFailure::VerificationFailed: outcome.failureKind = WriteFailureKind::Verification; break;
        case EmbeddedMetadataFailure::UnsafePath:
        case EmbeddedMetadataFailure::WriteFailed:
        case EmbeddedMetadataFailure::None:
        default:
            // Only pre-publication refusals carry a Win32 code (see embedded_metadata.h).
            outcome.failureKind = result.win32Error != 0 ?
                quicksift::core::write_lane::ClassifyWin32Error(result.win32Error) :
                WriteFailureKind::Other;
            break;
        }
    }
    if (!result.ok && outcome.detail.empty()) {
        outcome.detail = L"The embedded metadata container could not be updated safely.";
    }
    if (!result.ok && result.failure == EmbeddedMetadataFailure::LowStorage) {
        outcome.detail += L"\n\nRequired working space: " + FormatFileSize(result.requiredBytes) +
            L"\nAvailable space: " + FormatFileSize(result.availableBytes);
    }
    return outcome;
}

void QuickSiftApplicationImpl::ConfigureMetadataTransactions() {
    quicksift::transactions::MetadataTransactionCallbacks callbacks;
    callbacks.beginExclusive = [this](const std::vector<fs::path>& paths,
        std::wstring& detail) {
        // P1 OS hint (runs on the metadata service thread at the start of every
        // plan): NORMAL, i.e. above every background/below-normal worker (P3-P5 and
        // the ratings/batch workers), but never above the UI thread (P0), which also
        // gets Windows' foreground boost. Readers yield to writes cooperatively.
        static thread_local bool hinted = false;
        if (!hinted) {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
            hinted = true;
        }
        if (paths.empty() || worker_.BeginExclusivePathWrites(paths)) return true;
        detail = L"QuickSift could not release an active image decoder for this metadata batch in time. Metadata reads no longer block rating or pick updates; wait for the current preview decode to finish and try again.";
        QS_LOG_WARNING(L"Metadata", L"Exclusive metadata batch handoff timed out");
        return false;
    };
    callbacks.endExclusive = [this](const std::vector<fs::path>& paths) {
        if (!paths.empty()) worker_.EndExclusivePathWrites(paths);
    };
    callbacks.write = [](const fs::path& path, const MetadataPatch& patch,
        MetadataStorageKind storage, bool jpeg, bool safeJpegWrites) {
        return WriteMetadataPatchFile(path, patch, storage, jpeg, safeJpegWrites);
    };
    // Off-UI-thread I/O: post-write stamps and the cumulative free-space preflight.
    callbacks.stamps = [](const fs::path& path) { return MetadataFileStampsFor(path); };
    callbacks.preflight = [](const std::vector<MetadataSnapshot>& targets) {
        return MetadataStorageShortfallFor(targets);
    };
    callbacks.onRetry = [](const fs::path& path, unsigned attempt,
        quicksift::core::write_lane::WriteFailureKind kind, std::uint32_t win32Error,
        std::chrono::milliseconds elapsed, bool external) {
        // Rate-limited: a file locked for 30 s produces ~35 attempts.
        static quicksift::core::LogRateLimiter limiter(12, std::chrono::seconds(10));
        std::uint64_t suppressed = 0;
        const bool terminal = quicksift::core::write_lane::IsTransient(kind) == false;
        if (!terminal && !limiter.Allow(&suppressed)) return;
        QS_LOG_EVENT(terminal || external ? quicksift::diagnostics::Level::Warning :
                quicksift::diagnostics::Level::Info,
            quicksift::diagnostics::category::kWriteLane,
            terminal ? L"write_abort" : (external ? L"write_waiting_external" : L"write_retry"),
            { L"file", quicksift::diagnostics::LogFileName(path) },
            { L"path_id", quicksift::diagnostics::LogPathId(path) },
            { L"attempt", std::to_wstring(attempt) },
            { L"kind", std::wstring(quicksift::core::write_lane::FailureKindName(kind)) },
            { L"win32", std::to_wstring(win32Error) },
            { L"elapsed_ms", std::to_wstring(elapsed.count()) },
            { L"suppressed", std::to_wstring(suppressed) });
    };
    transactions_.ConfigureMetadata(std::move(callbacks));
}

void QuickSiftApplicationImpl::SubmitMetadataEdit(MetadataEditField field, int value) {
    std::vector<fs::path> targets;
    if (reviewState_.Mode() == ViewMode::Compare && !reviewState_.ActiveComparePath().empty())
        targets.emplace_back(reviewState_.ActiveComparePath());
    else targets = ActivePaths();
    if (targets.empty() && !reviewState_.ThumbnailFocusPath().empty()) targets.emplace_back(reviewState_.ThumbnailFocusPath());
    if (targets.empty()) return;
    // File transactions (move/copy/delete) and history replay are never raced.
    if (transactions_.FileBusy() || replayingHistory_) {
        ShowGlassAlert(L"Metadata update is waiting",
            L"QuickSift serializes verified file and metadata transactions so they cannot race over the same media.",
            GlassAlertKind::Warning);
        return;
    }

    const MetadataFieldId fieldId = field == MetadataEditField::Rating ?
        MetadataFieldId::Rating : (field == MetadataEditField::Pick ?
            MetadataFieldId::PickState : MetadataFieldId::ColorLabel);
    const std::wstring label = MetadataFieldLabel(fieldId);

    // P1 write lane: while another metadata write runs (or edits are already queued),
    // the edit shows immediately and is coalesced (last value wins per file), then
    // written on the urgent lane / right after the running plan. No modal refusal.
    if (transactions_.MetadataBusy() || !pendingMetadataEdits_.Empty()) {
        QueueMetadataEdit(targets, fieldId, value, label);
        return;
    }

    quicksift::transactions::MetadataTransactionPlan plan;
    plan.label = label;
    plan.recordHistory = HistoryStore::EstimateMetadataBytesForTargets(targets, label) <=
        historyStore_.MemoryLimit();
    plan.changes.reserve(targets.size());

    for (const fs::path& path : targets) {
        const auto index = CatalogIndexForPath(path.wstring());
        const auto before = SnapshotForPath(path);
        if (!index || !before) {
            ++plan.initialFailures;
            if (plan.initialDetail.empty())
                plan.initialDetail = L"QuickSift could not locate one or more metadata targets.";
            continue;
        }
        const MetadataFieldValue current = MetadataFieldFor(before->values, fieldId);
        if (current.IsKnown() && current.value == value) continue; // true no-op

        std::optional<MetadataFieldValue> expected;
        if (current.IsAuthoritative()) expected = current;
        MetadataPatch patch = MakeSingleFieldPatch(fieldId, value, expected);
        quicksift::transactions::MetadataChange change;
        change.before = *before;
        change.patch = patch;
        change.after = *before;
        change.after.values = ApplyMetadataPatch(before->values, patch);

        catalog_.EditPhoto(*index, [&](PhotoItem& item) {
            // Show the cull immediately. The file write remains transactional and
            // incomplete items are restored from the before snapshot.
            ApplyValuesToPhoto(item, change.after.values);
            ++item.metadataRevision;
            change.before.revision = item.metadataRevision;
            change.after.revision = item.metadataRevision;
        });
        plan.changes.push_back(std::move(change));
    }

    if (plan.changes.empty()) {
        if (plan.initialFailures != 0) {
            ShowGlassAlertText(FillToken(Tr(L"{label} update stopped"), L"{label}", Tr(label)),
                Tr(plan.initialDetail), GlassAlertKind::Warning);
        } else {
            ShowToastText(FillToken(Tr(L"{label} already matches the selected photos — no files were rewritten"),
                L"{label}", Tr(label)));
        }
        return;
    }
    // Free-space preflight now runs on the transaction thread (no UI-thread stats);
    // a shortfall fails every item and FinishMetadataTransaction rolls back.

    const auto restoreOptimistic = [&]() {
        // 0.9.4.0 polish: a refused submit used to leave the optimistic value on
        // screen although nothing would ever be written.
        for (const auto& change : plan.changes) {
            UpdatePhotoMetadataEverywhere(change.before.path, [&](PhotoItem& photo) {
                ApplyValuesToPhoto(photo, change.before.values);
            });
        }
        RebuildVisiblePhotos();
        InvalidateCanvas();
    };
    quicksift::core::priority::Demand().SetMetadataWritesPending(1);
    const std::uint64_t token = transactions_.SubmitMetadata(plan);
    if (token == 0) {
        quicksift::core::priority::Demand().SetMetadataWritesPending(transactions_.MetadataBusy() ? 1u : 0u);
        restoreOptimistic();
        ShowGlassAlertText(FillToken(Tr(L"{label} update could not start"), L"{label}", Tr(label)),
            Tr(L"Another serialized transaction became active before the metadata batch could be submitted."),
            GlassAlertKind::Warning);
        return;
    }
    EnsureMetadataWriteLaneTimer();
    UpdateColorLabelButton();
    UpdateStatus();
    for (int id : { ID_RATING_MENU, ID_PICK_STATE_MENU, ID_COLOR_LABEL }) {
        if (controls_.contains(id)) InvalidateRect(controls_[id], nullptr, FALSE);
    }
    InvalidateCanvas();
}

std::uint64_t QuickSiftApplicationImpl::SubmitRatingsImport(
    const std::vector<RatingsImportItem>& items, RatingsImportCounts& counts,
    std::wstring_view label) {
    // Same pipeline as SubmitMetadataEdit, but one multi-field patch per file:
    // optimistic catalog apply -> storage preflight -> serialized, verified,
    // cancellable worker transaction with partial-completion restore.
    // recordHistory=false: the import is confirmed as "cannot be undone"; the
    // completion path clears Redo (like any unrecorded batch) without the
    // "large batch" toast (FinishRatingsImportTransaction).
    quicksift::transactions::MetadataTransactionPlan plan;
    // Shared by Ratings Import and Batch Processing (label names the operation).
    const std::wstring localizedLabel = Tr(label);
    plan.label = localizedLabel;
    plan.recordHistory = false;
    plan.changes.reserve(items.size());

    const auto addField = [](MetadataPatchField& patchField, const MetadataFieldValue& current,
        const std::optional<int>& requested) {
        if (!requested) return false; // field not part of this operation ("No change")
        const int desired = *requested;
        if (current.IsKnown() && current.value == desired) return false; // true no-op
        // Keep an unrepresentable on-disk value rather than overwrite it with a
        // neutral token that cannot express it.
        if (current.knowledge == MetadataKnowledge::Unsupported && desired == 0) return false;
        patchField.active = true;
        patchField.desired = desired;
        if (current.IsAuthoritative()) {
            patchField.requireExpected = true;
            patchField.expected = current;
        }
        return true;
    };

    for (const RatingsImportItem& item : items) {
        const auto index = CatalogIndexForPath(item.path.wstring());
        const auto before = SnapshotForPath(item.path);
        if (!index || !before) {
            ++counts.failed;
            continue;
        }
        MetadataPatch patch;
        bool any = false;
        any = addField(patch.rating, before->values.rating, item.starRating) || any;
        any = addField(patch.pickState, before->values.pickState, item.pickState) || any;
        any = addField(patch.colorLabel, before->values.colorLabel, item.colorLabel) || any;
        if (!any) {
            ++counts.skipped;
            continue;
        }
        quicksift::transactions::MetadataChange change;
        change.before = *before;
        change.patch = patch;
        change.after = *before;
        change.after.values = ApplyMetadataPatch(before->values, patch);
        catalog_.EditPhoto(*index, [&](PhotoItem& photo) {
            ApplyValuesToPhoto(photo, change.after.values);
            ++photo.metadataRevision;
            change.before.revision = photo.metadataRevision;
            change.after.revision = photo.metadataRevision;
        });
        plan.changes.push_back(std::move(change));
    }
    counts.submitted = plan.changes.size();
    if (plan.changes.empty()) return 0;

    const auto restoreOptimistic = [&]() {
        for (const auto& change : plan.changes) {
            UpdatePhotoMetadataEverywhere(change.before.path, [&](PhotoItem& photo) {
                ApplyValuesToPhoto(photo, change.before.values);
            });
        }
        RebuildVisiblePhotos();
        InvalidateCanvas();
    };
    // Cumulative free-space preflight runs on the transaction thread.
    quicksift::core::priority::Demand().SetMetadataWritesPending(1);
    const std::uint64_t token = transactions_.SubmitMetadata(plan);
    if (token == 0) {
        quicksift::core::priority::Demand().SetMetadataWritesPending(transactions_.MetadataBusy() ? 1u : 0u);
        restoreOptimistic();
        counts.submitted = 0;
        ShowGlassAlert(ratingsImportIsBatch_ ? L"Batch processing could not start" : L"Ratings import could not start",
            L"Another serialized transaction became active before the metadata batch could be submitted.",
            GlassAlertKind::Warning);
        return 0;
    }
    EnsureMetadataWriteLaneTimer();
    UpdateColorLabelButton();
    UpdateStatus();
    for (int id : { ID_RATING_MENU, ID_PICK_STATE_MENU, ID_COLOR_LABEL }) {
        if (controls_.contains(id)) InvalidateRect(controls_[id], nullptr, FALSE);
    }
    InvalidateCanvas();
    return token;
}

void QuickSiftApplicationImpl::DrainMetadataTransactionResults() {
    // Results queue (urgent-lane results can precede the outer plan's). Bounded:
    // at most two can be outstanding (one normal + one urgent slot), plus margin.
    for (int drained = 0; drained < 4; ++drained) {
        auto result = transactions_.TakeMetadataResult();
        if (!result) break;
        FinishMetadataTransaction(std::move(*result));
        if (!IsWindow(hwnd_)) break; // FinishMetadataTransaction may close the window
    }
}

void QuickSiftApplicationImpl::FinishMetadataTransaction(
    quicksift::transactions::MetadataTransactionResult result) {
    if (!transactions_.AcceptMetadataResult(result.token)) {
        QS_LOG_WARNING(L"Metadata", L"Discarded a stale metadata-transaction completion");
        return;
    }

    HistoryEntry source;
    source.kind = HistoryEntry::Kind::Metadata;
    source.label = result.label;
    source.before.reserve(result.changes.size());
    source.after.reserve(result.changes.size());
    bool changed = false;
    std::vector<fs::path> externalConflictPaths;
    externalConflictPaths.reserve(result.changes.size());
    const bool applyMode = result.mode == quicksift::transactions::MetadataTransactionMode::Apply;
    const bool importResult = ratingsImportToken_ != 0 && result.token == ratingsImportToken_;
    std::size_t droppedQueuedEdits = 0;
    std::vector<quicksift::transactions::MetadataChange> inUseChanges;
    std::optional<quicksift::core::write_lane::WriteFailureKind> singleFailureKind;
    bool mixedFailureKinds = false;
    fs::path firstFailedPath;
    for (std::size_t index = 0; index < result.changes.size(); ++index) {
        const auto& change = result.changes[index];
        source.before.push_back(change.before);
        source.after.push_back(change.after);
        const bool completed = index < result.completed.size() && result.completed[index];
        if (!completed) {
            if (change.externalConflict) externalConflictPaths.push_back(change.before.path);
            if (change.failureKind != quicksift::core::write_lane::WriteFailureKind::None) {
                if (firstFailedPath.empty()) firstFailedPath = change.before.path;
                if (!singleFailureKind) singleFailureKind = change.failureKind;
                else if (*singleFailureKind != change.failureKind) mixedFailureKinds = true;
            }
            if (applyMode && !importResult && !result.cancelled &&
                quicksift::core::write_lane::IsTransient(change.failureKind)) {
                inUseChanges.push_back(change);
            }
            // An edit queued behind this write was based on the value we are now
            // rolling back; it is dropped with it (reported below).
            if (pendingMetadataEdits_.RemovePath(change.before.path)) ++droppedQueuedEdits;
            UpdatePhotoMetadataEverywhere(change.before.path, [&](PhotoItem& item) {
                ApplyValuesToPhoto(item, change.before.values);
            });
            if (const auto catalogIndex = CatalogIndexForPath(change.before.path.wstring())) {
                pendingMetadataIndices_.push_front(*catalogIndex);
                ++pendingRatings_;
            }
            continue;
        }
        changed = true;
        const PhotoMetadataValues values = change.hasResultingValues ? change.resultingValues :
            (result.mode == quicksift::transactions::MetadataTransactionMode::UndoHistory ?
                change.before.values : change.after.values);
        // A queued edit of the same file now targets the freshly written values.
        std::optional<PhotoMetadataValues> stillQueued;
        if (pendingMetadataEdits_.Rebase(change.before.path, values)) {
            if (const auto* entry = pendingMetadataEdits_.Find(change.before.path))
                stillQueued = ApplyMetadataPatch(entry->before.values, entry->patch);
        }
        UpdatePhotoMetadataEverywhere(change.before.path, [&](PhotoItem& item) {
            ApplyValuesToPhoto(item, stillQueued ? *stillQueued : values);
            // Stamps were captured on the transaction thread (no UI-thread stats).
            ApplyMetadataFileStamps(item, change.stamps);
        });
    }
    PumpMetadataQueue();

    std::optional<HistoryDirection> conflictHistoryRelinquish;
    if (result.mode == quicksift::transactions::MetadataTransactionMode::Apply) {
        HistoryEntry completed = MetadataHistorySubset(source, result.completed, true);
        if (result.recordHistory) PushHistory(std::move(completed));
        else if (ratingsImportToken_ != 0 && result.token == ratingsImportToken_) {
            // Ratings import / Batch Processing: no Undo record by design (confirmed
            // "cannot be undone").
            if (changed) historyStore_.ClearRedo();
        } else FinishUnrecordedAction(changed);
    } else {
        const bool undoReplay = result.mode ==
            quicksift::transactions::MetadataTransactionMode::UndoHistory;
        HistoryEntry completed = MetadataHistorySubset(source, result.completed, true);
        HistoryEntry remaining = MetadataHistorySubset(source, result.completed, false);
        if (!externalConflictPaths.empty() && !remaining.before.empty()) {
            conflictHistoryRelinquish = undoReplay ? HistoryDirection::Undo :
                HistoryDirection::Redo;
        }
        replayingHistory_ = false;
        historyStore_.CompleteReplay(undoReplay ? HistoryDirection::Undo :
            HistoryDirection::Redo, std::move(completed), std::move(remaining));
    }

    RebuildVisiblePhotos();
    UpdateColorLabelButton();
    UpdateHistoryButtons();
    UpdateStatus();
    for (int id : { ID_RATING_MENU, ID_PICK_STATE_MENU, ID_COLOR_LABEL }) {
        if (controls_.contains(id)) InvalidateRect(controls_[id], nullptr, FALSE);
    }
    InvalidateCanvas();

    if (transactions_.ConsumeCloseRequestIfIdle()) {
        if (shutdownBlocked_) {
            ShutdownBlockReasonDestroy(hwnd_);
            shutdownBlocked_ = false;
        }
        SaveSession();
        DestroyWindow(hwnd_);
        return;
    }

    QS_LOG_EVENT(result.failed ? quicksift::diagnostics::Level::Warning : quicksift::diagnostics::Level::Info,
        quicksift::diagnostics::category::kWriteLane, L"transaction_finished",
        { L"token", std::to_wstring(result.token) },
        { L"urgent", result.urgent ? L"1" : L"0" },
        { L"items", std::to_wstring(result.changes.size()) },
        { L"failed", std::to_wstring(result.failureCount) },
        { L"conflicts", std::to_wstring(result.conflictCount) },
        { L"in_use", std::to_wstring(result.inUseCount) },
        { L"cancelled", result.cancelled ? L"1" : L"0" },
        { L"dropped_queued", std::to_wstring(droppedQueuedEdits) });

    if (importResult && FinishRatingsImportTransaction(result, changed)) {
        FlushPendingMetadataEdits();
        return;
    }

    if (result.storageShortfall) {
        ShowMetadataStorageShortfall(*result.storageShortfall, Tr(result.label));
    } else if (!inUseChanges.empty() && inUseChanges.size() == result.failureCount &&
        !result.cancelled) {
        // Every failure was a lock held by another app past the write-lane budget:
        // values are rolled back; offer Retry (resubmit) or Skip (keep rollback).
        inUseRetryLabel_ = result.label;
        const std::wstring file = inUseChanges.size() == 1 ?
            inUseChanges.front().before.path.filename().wstring() :
            FillToken(Tr(L"{count} files"), L"{count}", std::to_wstring(inUseChanges.size()));
        inUseRetryChanges_ = std::move(inUseChanges);
        ShowGlassChoice(UserErrorTitle(quicksift::core::UserErrorCode::WriteInUseGaveUp),
            UserErrorText(quicksift::core::UserErrorCode::WriteInUseGaveUp, file),
            Tr(L"Retry"), PendingGlassAction::MetadataRetryInUse, Tr(L"Skip"),
            GlassAlertKind::Warning);
    } else if (result.failed || result.cancelled || result.failureCount > 0) {
        std::wstring message;
        if (!result.cancelled && singleFailureKind && !mixedFailureKinds &&
            *singleFailureKind != quicksift::core::write_lane::WriteFailureKind::Other &&
            *singleFailureKind != quicksift::core::write_lane::WriteFailureKind::Conflict) {
            // One clear cause: the localized catalog message (detail stays in the log).
            message = UserErrorText(quicksift::core::UserErrorForWriteFailure(*singleFailureKind),
                firstFailedPath.filename().wstring());
        } else {
            message = Tr(result.detail.empty() ?
                std::wstring(L"Some metadata items could not be written safely.") : result.detail);
        }
        if (result.failureCount > 1)
            message += L"\n\n" + Tr(L"Total failed items:") + L" " + std::to_wstring(result.failureCount);
        if (result.conflictCount > 1)
            message += L"\n\n" + Tr(L"External-change conflicts:") + L" " + std::to_wstring(result.conflictCount);
        if (droppedQueuedEdits > 0)
            message += L"\n\n" + Tr(L"Edits made while this update was running were also undone:") +
                L" " + std::to_wstring(droppedQueuedEdits);
        const bool offerReconcile = !result.cancelled && !externalConflictPaths.empty();
        if (offerReconcile) {
            OfferExternalChangeMetadataReconcile(message, std::move(externalConflictPaths),
                conflictHistoryRelinquish);
        } else {
            ShowGlassAlertText(Tr(result.cancelled ? L"Metadata update cancelled" :
                L"Metadata update incomplete"), message,
                result.failed ? GlassAlertKind::Error : GlassAlertKind::Warning);
        }
    } else if (!result.urgent || pendingMetadataEdits_.Empty()) {
        // Coalesced rapid edits: one toast when the lane settles, not per keystroke.
        ShowToastText(FillToken(Tr(L"{label} update complete"), L"{label}", Tr(result.label)));
    }
    FlushPendingMetadataEdits();
}

    void QuickSiftApplicationImpl::RefreshMetadataFileState(PhotoItem& item, const fs::path& path) {
        std::error_code ec;
        const std::uintmax_t size = fs::file_size(path, ec);
        if (!ec) item.fileSize = size;
        item.modifiedStamp = quicksift::PersistentCache::SourceStampFor(path);
        if (item.modifiedStamp == 0) {
            ec.clear();
            const auto modified = fs::last_write_time(path, ec);
            if (!ec) item.modifiedStamp = quicksift::PersistentCache::FileTimeStamp(modified);
        }
        item.sidecarStamp = MetadataStampFor(path);
    }


    std::wstring QuickSiftApplicationImpl::NormalizedPathKey(const fs::path& path) {
        return quicksift::transactions::WindowsVerifiedFileOperations::NormalizedPathKey(path);
    }


    bool QuickSiftApplicationImpl::PathFingerprint(
        const fs::path& path, quicksift::transactions::FileIdentity& identity) {
        return quicksift::transactions::WindowsVerifiedFileOperations::FingerprintPath(
            path, identity);
    }


    bool QuickSiftApplicationImpl::PathEntryExists(const fs::path& path, bool* isSymlink) {
        return quicksift::transactions::WindowsVerifiedFileOperations::PathEntryExists(
            path, isSymlink);
    }


    bool QuickSiftApplicationImpl::HistoryDestinationUnchanged(const FileHistoryItem& item) const {
        quicksift::transactions::FileIdentity identity;
        return PathFingerprint(item.destination, identity) &&
            identity.size == item.destinationSize &&
            identity.stamp == item.destinationStamp &&
            identity.changeStamp == item.destinationChangeStamp &&
            identity.contentFingerprint == item.destinationContentFingerprint;
    }


    bool QuickSiftApplicationImpl::HistoryRedoSourceUnchanged(const FileHistoryItem& item) const {
        if (!item.redoSourceFingerprintValid) return true;
        quicksift::transactions::FileIdentity identity;
        return PathFingerprint(item.source, identity) &&
            identity.size == item.redoSourceSize &&
            identity.stamp == item.redoSourceStamp &&
            identity.changeStamp == item.redoSourceChangeStamp &&
            identity.contentFingerprint == item.redoSourceContentFingerprint;
    }


    bool QuickSiftApplicationImpl::HistoryEntryHasExternalIdentityDivergence(
        const HistoryEntry& entry, bool undoReplay) const {
        if (entry.kind != HistoryEntry::Kind::Files) return false;
        for (const FileHistoryItem& item : entry.files) {
            if (undoReplay) {
                if (PathEntryExists(item.destination) && !HistoryDestinationUnchanged(item))
                    return true;
            } else if (item.redoSourceFingerprintValid && PathEntryExists(item.source) &&
                !HistoryRedoSourceUnchanged(item)) {
                return true;
            }
        }
        return false;
    }


    void QuickSiftApplicationImpl::RefreshAfterHistory() {
        if (!currentFolder_.empty()) LoadFolder(currentFolder_);
        else {
            RebuildVisiblePhotos();
            UpdateStatus();
            InvalidateCanvas();
        }
        UpdateColorLabelButton();
        UpdateHistoryButtons();
    }


    void QuickSiftApplicationImpl::OfferExternalChangeHistoryReconcile(
        HistoryDirection direction, const std::wstring& detail) {
        const bool undo = direction == HistoryDirection::Undo;
        const std::wstring title = undo ?
            L"Undo blocked by an external file change" :
            L"Redo blocked by an external file change";
        const std::wstring message = detail +
            L"\n\nQuickSift will not overwrite or move bytes it did not record. "
            L"Relinquish this history entry to clear the stuck " +
            std::wstring(undo ? L"Undo" : L"Redo") +
            L" and refresh catalog state from disk, or keep the history and leave files untouched.";
        ShowGlassConfirmation(title, message, L"Relinquish history",
            undo ? PendingGlassAction::RelinquishUndoHistory :
                PendingGlassAction::RelinquishRedoHistory,
            GlassAlertKind::Warning, L"Keep history");
    }


    void QuickSiftApplicationImpl::RelinquishGuardedHistory(HistoryDirection direction) {
        if (transactions_.Busy()) {
            ShowGlassAlert(L"History reconcile is waiting",
                L"A serialized file or metadata transaction is still running. QuickSift will not drop history while that work is active.",
                GlassAlertKind::Warning);
            return;
        }

        auto taken = direction == HistoryDirection::Undo ?
            historyStore_.TakeUndo() : historyStore_.TakeRedo();
        if (!taken) return;

        std::size_t refreshed = 0;
        const auto refreshPath = [&](const fs::path& path) {
            if (path.empty()) return;
            const std::wstring key = path.wstring();
            wicImageSources_.erase(key);
            displayedViewKeys_.erase(key);
            if (const auto index = CatalogIndexForPath(key)) {
                catalog_.EditPhoto(*index, [&](PhotoItem& item) {
                    RefreshMetadataFileState(item, path);
                    ++item.metadataRevision;
                });
                PersistPhotoRecord(catalog_.PhotoAt(*index));
                pendingMetadataIndices_.push_front(*index);
                ++pendingRatings_;
                ++refreshed;
            }
        };

        if (taken->kind == HistoryEntry::Kind::Files) {
            for (const FileHistoryItem& item : taken->files) {
                if (direction == HistoryDirection::Undo) {
                    refreshPath(item.destination);
                    if (item.copied) refreshPath(item.source);
                } else {
                    refreshPath(item.source);
                }
            }
        } else {
            const auto& snapshots = direction == HistoryDirection::Undo ?
                taken->before : taken->after;
            for (const MetadataSnapshot& snapshot : snapshots) {
                refreshPath(snapshot.path);
            }
        }

        PumpMetadataQueue();
        RebuildVisiblePhotos();
        UpdateColorLabelButton();
        UpdateHistoryButtons();
        UpdateStatus();
        InvalidateCanvas();

        QS_LOG_INFO(L"History",
            std::wstring(direction == HistoryDirection::Undo ?
                L"Relinquished Undo" : L"Relinquished Redo") +
            L" history entry \"" + taken->label +
            L"\" after external identity divergence; refreshed " +
            std::to_wstring(refreshed) +
            L" catalog path(s); on-disk files left untouched");
        ShowToast(L"History entry relinquished; on-disk files left untouched");
    }


    void QuickSiftApplicationImpl::OfferExternalChangeMetadataReconcile(
        const std::wstring& detail, std::vector<fs::path> conflictPaths,
        std::optional<HistoryDirection> relinquishHistory) {
        pendingExternalMetadataReconcilePaths_ = std::move(conflictPaths);
        pendingExternalMetadataReconcileHistory_ = std::move(relinquishHistory);
        const bool alsoRelinquish = pendingExternalMetadataReconcileHistory_.has_value();
        std::wstring message = detail +
            L"\n\nQuickSift did not write rating, pick, or color-label changes onto bytes or values it did not expect. "
            L"Refresh from disk to accept the current file as the new baseline for future metadata edits";
        if (alsoRelinquish) {
            message += L", clear the stuck history entry that can no longer replay safely,";
        }
        message += L" or keep the catalog as-is and leave files untouched.";
        ShowGlassConfirmation(L"Metadata update blocked by an external change",
            message, L"Refresh from disk",
            PendingGlassAction::RefreshMetadataFromDisk,
            GlassAlertKind::Warning, L"Keep catalog");
    }


    void QuickSiftApplicationImpl::RefreshCatalogFromDiskAfterExternalMetadataConflict() {
        std::vector<fs::path> paths = std::move(pendingExternalMetadataReconcilePaths_);
        const auto relinquishHistory = pendingExternalMetadataReconcileHistory_;
        pendingExternalMetadataReconcilePaths_.clear();
        pendingExternalMetadataReconcileHistory_.reset();

        if (relinquishHistory) {
            RelinquishGuardedHistory(*relinquishHistory);
        }

        std::size_t refreshed = 0;
        for (const fs::path& path : paths) {
            if (path.empty()) continue;
            const std::wstring key = path.wstring();
            wicImageSources_.erase(key);
            displayedViewKeys_.erase(key);
            if (const auto index = CatalogIndexForPath(key)) {
                catalog_.EditPhoto(*index, [&](PhotoItem& item) {
                    RefreshMetadataFileState(item, path);
                    item.ratingKnowledge = MetadataKnowledge::Unknown;
                    item.colorLabelKnowledge = MetadataKnowledge::Unknown;
                    item.pickStateKnowledge = MetadataKnowledge::Unknown;
                    item.rawRating.clear();
                    item.rawColorLabel.clear();
                    item.rawPickState.clear();
                    ++item.metadataRevision;
                });
                PersistPhotoRecord(catalog_.PhotoAt(*index));
                pendingMetadataIndices_.push_front(*index);
                ++pendingRatings_;
                ++refreshed;
            }
        }

        PumpMetadataQueue();
        RebuildVisiblePhotos();
        UpdateColorLabelButton();
        UpdateHistoryButtons();
        UpdateStatus();
        InvalidateCanvas();

        QS_LOG_INFO(L"Metadata",
            L"Refreshed catalog from disk after external metadata conflict for " +
            std::to_wstring(refreshed) +
            L" path(s); abandoned stuck write; on-disk files left untouched");
        ShowToast(refreshed == 0 ?
            L"No catalog paths needed refresh; on-disk files left untouched" :
            L"Catalog refreshed from disk; on-disk files left untouched");
    }


    bool QuickSiftApplicationImpl::PreflightUndoFiles(const HistoryEntry& entry,
        std::wstring& detail, bool* identityDiverged) const {
        if (identityDiverged) *identityDiverged = false;
        struct Requirement { fs::path probe; std::uint64_t bytes = 0; };
        std::unordered_map<std::wstring, Requirement> requirements;
        for (const FileHistoryItem& item : entry.files) {
            if (!PathEntryExists(item.destination)) {
                detail = L"A file expected at the operation destination is missing:\n\n" +
                    item.destination.filename().wstring();
                return false;
            }
            if (!HistoryDestinationUnchanged(item)) {
                detail = L"A destination file changed after QuickSift created it:\n\n" +
                    item.destination.filename().wstring();
                if (identityDiverged) *identityDiverged = true;
                return false;
            }
            if (!item.copied && PathEntryExists(item.source)) {
                detail = L"The original location is no longer empty:\n\n" +
                    item.source.filename().wstring();
                return false;
            }
            if (!item.copied) {
                const auto fromVolume = VolumeRootForPath(item.destination);
                const auto toVolume = VolumeRootForPath(item.source);
                if (!fromVolume || !toVolume || *fromVolume != *toVolume) {
                    const std::wstring key = toVolume.value_or(item.source.root_name().wstring());
                    Requirement& req = requirements[key];
                    req.probe = item.source;
                    req.bytes = SaturatingAdd(req.bytes, static_cast<std::uint64_t>(item.destinationSize));
                }
            }
        }
        constexpr std::uint64_t kReserve = 64ull * 1024ull * 1024ull;
        for (const auto& [key, requirement] : requirements) {
            (void)key;
            std::uint64_t available = 0;
            if (!HasStorageHeadroom(requirement.probe, requirement.bytes, kReserve, &available)) {
                detail = L"There is not enough free space to restore files across volumes.\n\nRequired working space: " +
                    FormatFileSize(SaturatingAdd(requirement.bytes, kReserve)) +
                    L"\nAvailable space: " + FormatFileSize(available);
                return false;
            }
        }
        return true;
    }


    bool QuickSiftApplicationImpl::PreflightRedoFiles(const HistoryEntry& entry,
        std::wstring& detail, bool* identityDiverged) const {
        if (identityDiverged) *identityDiverged = false;
        struct Requirement { fs::path probe; std::uint64_t bytes = 0; };
        std::unordered_map<std::wstring, Requirement> requirements;
        for (const FileHistoryItem& item : entry.files) {
            quicksift::transactions::FileIdentity sourceIdentity;
            if (!PathFingerprint(item.source, sourceIdentity)) {
                detail = L"A source file is missing, inaccessible, symbolic, or not a normal file:\n\n" +
                    item.source.filename().wstring();
                return false;
            }
            if (item.redoSourceFingerprintValid &&
                (sourceIdentity.size != item.redoSourceSize ||
                 sourceIdentity.stamp != item.redoSourceStamp ||
                 sourceIdentity.changeStamp != item.redoSourceChangeStamp ||
                 sourceIdentity.contentFingerprint != item.redoSourceContentFingerprint)) {
                detail = L"A source file changed after Undo, so Redo will not copy or move different bytes:\n\n" +
                    item.source.filename().wstring();
                if (identityDiverged) *identityDiverged = true;
                return false;
            }
            if (NormalizedPathKey(item.source) == NormalizedPathKey(item.destination)) {
                detail = L"The source and destination resolve to the same path:\n\n" +
                    item.source.filename().wstring();
                return false;
            }
            if (PathEntryExists(item.destination)) {
                detail = L"A destination filename is already occupied:\n\n" +
                    item.destination.filename().wstring();
                return false;
            }
            const auto sourceVolume = VolumeRootForPath(item.source);
            const auto destinationVolume = VolumeRootForPath(item.destination);
            if (item.copied || !sourceVolume || !destinationVolume || *sourceVolume != *destinationVolume) {
                const std::wstring key = destinationVolume.value_or(item.destination.root_name().wstring());
                Requirement& req = requirements[key];
                req.probe = item.destination;
                req.bytes = SaturatingAdd(req.bytes, static_cast<std::uint64_t>(sourceIdentity.size));
            }
        }
        constexpr std::uint64_t kReserve = 64ull * 1024ull * 1024ull;
        for (const auto& [key, requirement] : requirements) {
            (void)key;
            std::uint64_t available = 0;
            if (!HasStorageHeadroom(requirement.probe, requirement.bytes, kReserve, &available)) {
                detail = L"There is not enough free space to repeat this file operation.\n\nRequired working space: " +
                    FormatFileSize(SaturatingAdd(requirement.bytes, kReserve)) +
                    L"\nAvailable space: " + FormatFileSize(available);
                return false;
            }
        }
        return true;
    }


    HistoryEntry QuickSiftApplicationImpl::FileHistorySubset(const HistoryEntry& source,
        const std::vector<bool>& completed, bool takeCompleted) {
        HistoryEntry result;
        result.kind = source.kind;
        result.label = source.label;
        result.files = SelectHistoryItems(source.files, completed, takeCompleted);
        return result;
    }


    HistoryEntry QuickSiftApplicationImpl::MetadataHistorySubset(const HistoryEntry& source,
        const std::vector<bool>& completed, bool takeCompleted) {
        HistoryEntry result;
        result.kind = source.kind;
        result.label = source.label;
        auto selected = SelectParallelHistoryItems(
            source.before, source.after, completed, takeCompleted);
        result.before = std::move(selected.first);
        result.after = std::move(selected.second);
        return result;
    }


    void QuickSiftApplicationImpl::Undo() {
        const HistoryEntry* preview = historyStore_.PeekUndo();
        if (!preview) return;
        if (transactions_.Busy()) {
            ShowGlassAlert(L"Undo is waiting",
                L"A serialized file or metadata transaction is still running. QuickSift will not let Undo race it.",
                GlassAlertKind::Warning);
            return;
        }

        if (preview->kind == HistoryEntry::Kind::Metadata) {
            if (preview->before.size() != preview->after.size()) {
                ShowGlassAlert(L"Undo history is incomplete",
                    L"The metadata history record is internally inconsistent, so QuickSift refused to write any file.",
                    GlassAlertKind::Error);
                return;
            }
            // Free-space preflight runs on the transaction thread (no UI stats).

            auto taken = historyStore_.TakeUndo();
            if (!taken) return;
            HistoryEntry entry = std::move(*taken);
            quicksift::transactions::MetadataTransactionPlan transaction;
            transaction.label = entry.label;
            transaction.mode = quicksift::transactions::MetadataTransactionMode::UndoHistory;
            transaction.changes.reserve(entry.before.size());
            for (std::size_t index = 0; index < entry.before.size(); ++index) {
                quicksift::transactions::MetadataChange change;
                change.before = entry.before[index];
                change.after = entry.after[index];
                if (const auto catalogIndex = CatalogIndexForPath(change.before.path.wstring())) {
                    catalog_.EditPhoto(*catalogIndex, [](PhotoItem& item) { ++item.metadataRevision; });
                }
                if (change.before.storage == MetadataStorageKind::Embedded) {
                    const std::wstring key = change.before.path.wstring();
                    wicImageSources_.erase(key);
                    displayedViewKeys_.erase(key);
                }
                transaction.changes.push_back(std::move(change));
            }
            const std::uint64_t token = transactions_.SubmitMetadata(transaction);
            if (token == 0) {
                historyStore_.Restore(quicksift::core::HistoryDirection::Undo,
                    std::move(entry));
                ShowGlassAlert(L"Undo could not start",
                    L"Another serialized transaction became active before metadata Undo could be submitted.",
                    GlassAlertKind::Warning);
                return;
            }
                replayingHistory_ = true;
            UpdateHistoryButtons();
            UpdateStatus();
            ShowToast(L"Undoing metadata safely…");
            return;
        }

        std::wstring detail;
        bool identityDiverged = false;
        if (!PreflightUndoFiles(*preview, detail, &identityDiverged)) {
            if (identityDiverged) {
                OfferExternalChangeHistoryReconcile(HistoryDirection::Undo, detail);
            } else {
                ShowGlassAlert(L"Undo stopped before changing files",
                    detail + L"\n\nQuickSift left the entire operation untouched. It never overwrites or automatically renames files.",
                    GlassAlertKind::Warning);
            }
            return;
        }
        auto taken = historyStore_.TakeUndo();
        if (!taken) return;
        HistoryEntry entry = std::move(*taken);
        quicksift::transactions::FileTransactionPlan transaction;
        transaction.label = entry.label;
        transaction.mode = quicksift::transactions::FileTransactionMode::UndoHistory;
        transaction.historyReplayItems = std::move(entry.files);
        const std::uint64_t token = transactions_.SubmitFile(transaction);
        if (token == 0) {
            entry.files = std::move(transaction.historyReplayItems);
            historyStore_.Restore(quicksift::core::HistoryDirection::Undo,
                std::move(entry));
            ShowGlassAlert(L"Undo could not start",
                L"Another verified file transaction became active before Undo could be submitted.",
                GlassAlertKind::Warning);
            return;
        }
        replayingHistory_ = true;
        UpdateHistoryButtons();
        UpdateStatus();
        ShowToast(L"Undoing and verifying file history…");
    }


    void QuickSiftApplicationImpl::Redo() {
        const HistoryEntry* preview = historyStore_.PeekRedo();
        if (!preview) return;
        if (transactions_.Busy()) {
            ShowGlassAlert(L"Redo is waiting",
                L"A serialized file or metadata transaction is still running. QuickSift will not let Redo race it.",
                GlassAlertKind::Warning);
            return;
        }

        if (preview->kind == HistoryEntry::Kind::Metadata) {
            if (preview->before.size() != preview->after.size()) {
                ShowGlassAlert(L"Redo history is incomplete",
                    L"The metadata history record is internally inconsistent, so QuickSift refused to write any file.",
                    GlassAlertKind::Error);
                return;
            }
            // Free-space preflight runs on the transaction thread (no UI stats).

            auto taken = historyStore_.TakeRedo();
            if (!taken) return;
            HistoryEntry entry = std::move(*taken);
            quicksift::transactions::MetadataTransactionPlan transaction;
            transaction.label = entry.label;
            transaction.mode = quicksift::transactions::MetadataTransactionMode::RedoHistory;
            transaction.changes.reserve(entry.after.size());
            for (std::size_t index = 0; index < entry.after.size(); ++index) {
                quicksift::transactions::MetadataChange change;
                change.before = entry.before[index];
                change.after = entry.after[index];
                if (const auto catalogIndex = CatalogIndexForPath(change.after.path.wstring())) {
                    catalog_.EditPhoto(*catalogIndex, [](PhotoItem& item) { ++item.metadataRevision; });
                }
                if (change.after.storage == MetadataStorageKind::Embedded) {
                    const std::wstring key = change.after.path.wstring();
                    wicImageSources_.erase(key);
                    displayedViewKeys_.erase(key);
                }
                transaction.changes.push_back(std::move(change));
            }
            const std::uint64_t token = transactions_.SubmitMetadata(transaction);
            if (token == 0) {
                historyStore_.Restore(quicksift::core::HistoryDirection::Redo,
                    std::move(entry));
                ShowGlassAlert(L"Redo could not start",
                    L"Another serialized transaction became active before metadata Redo could be submitted.",
                    GlassAlertKind::Warning);
                return;
            }
                replayingHistory_ = true;
            UpdateHistoryButtons();
            UpdateStatus();
            ShowToast(L"Redoing metadata safely…");
            return;
        }

        std::wstring detail;
        bool identityDiverged = false;
        if (!PreflightRedoFiles(*preview, detail, &identityDiverged)) {
            if (identityDiverged) {
                OfferExternalChangeHistoryReconcile(HistoryDirection::Redo, detail);
            } else {
                ShowGlassAlert(L"Redo stopped before changing files",
                    detail + L"\n\nQuickSift left the entire operation untouched. It never overwrites or automatically renames files.",
                    GlassAlertKind::Warning);
            }
            return;
        }
        auto taken = historyStore_.TakeRedo();
        if (!taken) return;
        HistoryEntry entry = std::move(*taken);
        quicksift::transactions::FileTransactionPlan transaction;
        transaction.label = entry.label;
        transaction.mode = quicksift::transactions::FileTransactionMode::RedoHistory;
        transaction.historyReplayItems = std::move(entry.files);
        const std::uint64_t token = transactions_.SubmitFile(transaction);
        if (token == 0) {
            entry.files = std::move(transaction.historyReplayItems);
            historyStore_.Restore(quicksift::core::HistoryDirection::Redo,
                std::move(entry));
            ShowGlassAlert(L"Redo could not start",
                L"Another verified file transaction became active before Redo could be submitted.",
                GlassAlertKind::Warning);
            return;
        }
        replayingHistory_ = true;
        UpdateHistoryButtons();
        UpdateStatus();
        ShowToast(L"Redoing and verifying file history…");
    }


    void QuickSiftApplicationImpl::ApplyRating(int rating) {
        SubmitMetadataEdit(MetadataEditField::Rating, rating);
    }


    void QuickSiftApplicationImpl::PersistPhotoRecord(const PhotoItem& photo, const std::wstring& exifText ) {
        quicksift::CachedImageRecord record;
        record.fileSize = photo.fileSize;
        record.modifiedStamp = photo.modifiedStamp;
        record.sidecarStamp = photo.sidecarStamp;
        record.rating = photo.rating;
        record.colorLabel = photo.colorLabel;
        record.pickState = photo.pickState;
        record.ratingKnowledge = photo.ratingKnowledge;
        record.colorLabelKnowledge = photo.colorLabelKnowledge;
        record.pickStateKnowledge = photo.pickStateKnowledge;
        record.rawRating = photo.rawRating;
        record.rawColorLabel = photo.rawColorLabel;
        record.rawPickState = photo.rawPickState;
        record.sourceWidth = photo.sourceWidth;
        record.sourceHeight = photo.sourceHeight;
        record.faces = photo.faces;
        record.facesScanned = photo.facesScanned;
        record.faceBlurScanned = photo.faceBlurScanned;
        record.faceBlurry = photo.faceBlurry;
        record.faceSharpness = photo.faceSharpness;
        // In-memory qualities only ever come from the current analysis version.
        if (photo.faceBlurScanned && photo.faceQualities.size() == photo.faces.size()) {
            record.faceQualities = photo.faceQualities;
            record.faceQualityVersion = quicksift::core::kFaceQualityAnalysisVersion;
        }
        // Selection knowledge is written only once hydrated or chosen this session;
        // otherwise the cache keeps the stored selection (same fingerprint).
        record.faceSelectionKnown = photo.faceSelectionKnown;
        record.hasFaceSelection = photo.faceSelectionKnown && photo.faceSelection.has_value();
        if (record.hasFaceSelection) record.faceSelection = *photo.faceSelection;
        record.exifText = exifText;
        // SQLite is intentionally kept off the UI thread. The worker deduplicates
        // repeated changes for the same path and preserves cached EXIF/faces when
        // this partial record does not carry a newly completed scan.
        worker_.QueueRecordCacheWrite(photo.path, std::move(record));
    }


    void QuickSiftApplicationImpl::UpdatePhotoMetadataEverywhere(const fs::path& path, const std::function<void(PhotoItem&)>& update) {
        const std::wstring key = path.wstring();
        if (const auto index = CatalogIndexForPath(key)) {
            catalog_.EditPhoto(*index, update);
            PersistPhotoRecord(catalog_.PhotoAt(*index));
        }
    }


    void QuickSiftApplicationImpl::ApplyPickState(int state) {
        SubmitMetadataEdit(MetadataEditField::Pick, state);
    }


    void QuickSiftApplicationImpl::ApplyColorLabel(int label) {
        SubmitMetadataEdit(MetadataEditField::ColorLabel, label);
    }

} // namespace quicksift::app
