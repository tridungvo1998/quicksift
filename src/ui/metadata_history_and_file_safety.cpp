// OWNER: Compiled QuickSiftApplication feature module.
#include "app/quicksift_application_internal.h"
#include "transactions/verified_file_operations_windows.h"

namespace quicksift::app {

// CODE GUIDE: See CODE_GUIDE.md -> "Writing metadata".
// OWNER: Metadata/file transactions and history; preserve preflight, flush, verification, commit, and rollback.

// Metadata/history safety: snapshots, storage preflight, transactional metadata
// updates, verified copy/move primitives, and bounded undo/redo history.

namespace {

PhotoMetadataValues ValuesFromPhoto(const PhotoItem& item) {
    return {
        { item.ratingKnowledge, item.rating, item.rawRating },
        { item.colorLabelKnowledge, item.colorLabel, item.rawColorLabel },
        { item.pickStateKnowledge, item.pickState, item.rawPickState }
    };
}

void ApplyValuesToPhoto(PhotoItem& item, const PhotoMetadataValues& values) {
    item.rating = values.rating.value;
    item.colorLabel = values.colorLabel.value;
    item.pickState = values.pickState.value;
    item.ratingKnowledge = values.rating.knowledge;
    item.colorLabelKnowledge = values.colorLabel.knowledge;
    item.pickStateKnowledge = values.pickState.knowledge;
    item.rawRating = values.rating.rawValue;
    item.rawColorLabel = values.colorLabel.rawValue;
    item.rawPickState = values.pickState.rawValue;
}

} // namespace

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

bool QuickSiftApplicationImpl::PreflightMetadataStorage(
    const std::vector<MetadataSnapshot>& targets, const std::wstring& operation) {
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
                ShowGlassAlert(L"Metadata update stopped",
                    L"QuickSift could not determine an image size before preparing a verified metadata update. Nothing was changed.",
                    GlassAlertKind::Warning);
                return false;
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
            std::wstringstream message;
            message << operation << L" needs cumulative free space for the complete batch.\n\nVolume: "
                << volume << L"\nRequired: " << FormatFileSize(needed);
            if (freeBytes) message << L"\nAvailable: " << FormatFileSize(*freeBytes);
            message << L"\n\nNothing was changed.";
            ShowGlassAlert(L"Low storage — nothing changed", message.str(), GlassAlertKind::Warning);
            return false;
        }
    }
    return true;
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
    transactions_.ConfigureMetadata(std::move(callbacks));
}

void QuickSiftApplicationImpl::SubmitMetadataEdit(MetadataEditField field, int value) {
    std::vector<fs::path> targets;
    if (reviewState_.Mode() == ViewMode::Compare && !reviewState_.ActiveComparePath().empty())
        targets.emplace_back(reviewState_.ActiveComparePath());
    else targets = ActivePaths();
    if (targets.empty() && !reviewState_.ThumbnailFocusPath().empty()) targets.emplace_back(reviewState_.ThumbnailFocusPath());
    if (targets.empty()) return;
    if (transactions_.Busy()) {
        ShowGlassAlert(L"Metadata update is waiting",
            L"QuickSift serializes verified file and metadata transactions so they cannot race over the same media.",
            GlassAlertKind::Warning);
        return;
    }

    const std::wstring label = field == MetadataEditField::Rating ? L"Rating" :
        (field == MetadataEditField::Pick ? L"Pick state" : L"Color label");
    const MetadataFieldId fieldId = field == MetadataEditField::Rating ?
        MetadataFieldId::Rating : (field == MetadataEditField::Pick ?
            MetadataFieldId::PickState : MetadataFieldId::ColorLabel);

    quicksift::transactions::MetadataTransactionPlan plan;
    plan.label = label;
    plan.recordHistory = HistoryStore::EstimateMetadataBytesForTargets(targets, label) <=
        historyStore_.MemoryLimit();
    plan.changes.reserve(targets.size());
    std::vector<MetadataSnapshot> storageTargets;
    storageTargets.reserve(targets.size());

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
        storageTargets.push_back(change.after);
        plan.changes.push_back(std::move(change));
    }

    if (plan.changes.empty()) {
        if (plan.initialFailures != 0) {
            ShowGlassAlert(label + L" update stopped", plan.initialDetail,
                GlassAlertKind::Warning);
        } else {
            ShowToast(label + L" already matches the selected photos — no files were rewritten");
        }
        return;
    }
    if (!PreflightMetadataStorage(storageTargets, label + L" update")) return;

    const std::uint64_t token = transactions_.SubmitMetadata(plan);
    if (token == 0) {
        ShowGlassAlert(label + L" update could not start",
            L"Another serialized transaction became active before the metadata batch could be submitted.",
            GlassAlertKind::Warning);
        return;
    }
    UpdateColorLabelButton();
    UpdateStatus();
    for (int id : { ID_RATING_MENU, ID_PICK_STATE_MENU, ID_COLOR_LABEL }) {
        if (controls_.contains(id)) InvalidateRect(controls_[id], nullptr, FALSE);
    }
    InvalidateCanvas();
}

void QuickSiftApplicationImpl::DrainMetadataTransactionResults() {
    if (auto result = transactions_.TakeMetadataResult())
        FinishMetadataTransaction(std::move(*result));
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
    for (std::size_t index = 0; index < result.changes.size(); ++index) {
        const auto& change = result.changes[index];
        source.before.push_back(change.before);
        source.after.push_back(change.after);
        const bool completed = index < result.completed.size() && result.completed[index];
        if (!completed) {
            if (change.externalConflict) externalConflictPaths.push_back(change.before.path);
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
        UpdatePhotoMetadataEverywhere(change.before.path, [&](PhotoItem& item) {
            ApplyValuesToPhoto(item, values);
            RefreshMetadataFileState(item, change.before.path);
        });
    }
    PumpMetadataQueue();

    std::optional<HistoryDirection> conflictHistoryRelinquish;
    if (result.mode == quicksift::transactions::MetadataTransactionMode::Apply) {
        HistoryEntry completed = MetadataHistorySubset(source, result.completed, true);
        if (result.recordHistory) PushHistory(std::move(completed));
        else FinishUnrecordedAction(changed);
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

    if (result.failed || result.cancelled || result.failureCount > 0) {
        std::wstring message = result.detail.empty() ?
            L"Some metadata items could not be written safely." : result.detail;
        if (result.failureCount > 1)
            message += L"\n\nTotal failed items: " + std::to_wstring(result.failureCount);
        if (result.conflictCount > 1)
            message += L"\n\nExternal-change conflicts: " + std::to_wstring(result.conflictCount);
        const bool offerReconcile = !result.cancelled && !externalConflictPaths.empty();
        if (offerReconcile) {
            OfferExternalChangeMetadataReconcile(message, std::move(externalConflictPaths),
                conflictHistoryRelinquish);
        } else {
            ShowGlassAlert(result.cancelled ? L"Metadata update cancelled" :
                L"Metadata update incomplete", message,
                result.failed ? GlassAlertKind::Error : GlassAlertKind::Warning);
        }
    } else {
        ShowToast(result.label + L" update complete");
    }
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
            if (!PreflightMetadataStorage(preview->before, L"Undo metadata")) return;

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
            if (!PreflightMetadataStorage(preview->after, L"Redo metadata")) return;

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
