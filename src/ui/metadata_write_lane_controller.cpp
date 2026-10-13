// OWNER: Metadata write lane on the UI side (P1): coalescing of interactive edits
// typed while a write runs, urgent-lane submission, the lock-resolution status
// ("Waiting for <file> — in use by another app"), Retry / Skip after the external
// budget, and the localized user-error table. See ARCHITECTURE.md -> "Write lane".
//
// Threading: UI thread only. Never blocks: every call here is a queue handoff or a
// lock-free progress read; all file I/O happens on the metadata transaction thread.
#include "app/quicksift_application_internal.h"
#include "core/priority_policy.h"
#include "diagnostics/log_categories.h"
#include "ui/metadata_photo_values.h"

namespace quicksift::app {

using metadata_values::ApplyValuesToPhoto;

std::wstring QuickSiftApplicationImpl::MetadataFieldLabel(MetadataFieldId field) {
    switch (field) {
    case MetadataFieldId::Rating: return L"Rating";
    case MetadataFieldId::PickState: return L"Pick state";
    case MetadataFieldId::ColorLabel: return L"Color label";
    }
    return L"Metadata";
}

std::wstring QuickSiftApplicationImpl::UserErrorTitle(quicksift::core::UserErrorCode code) const {
    return Tr(std::wstring(quicksift::core::UserError(code).title));
}

std::wstring QuickSiftApplicationImpl::UserErrorText(quicksift::core::UserErrorCode code,
    const std::wstring& file) const {
    return FillToken(Tr(std::wstring(quicksift::core::UserError(code).message)), L"{file}",
        file.empty() ? Tr(L"the file") : file);
}

void QuickSiftApplicationImpl::ShowToastText(const std::wstring& localizedText) {
    uiFramework_.Toasts().Show(localizedText);
    uiFramework_.SyncOverlayTransitions(std::chrono::steady_clock::now());
    if (uiFramework_.Toasts().Visible()) InvalidateCanvas();
}

quicksift::transactions::MetadataChange::FileStamps
QuickSiftApplicationImpl::MetadataFileStampsFor(const fs::path& path) {
    // Transaction thread. Mirrors RefreshMetadataFileState without touching PhotoItem.
    quicksift::transactions::MetadataChange::FileStamps stamps;
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (!ec) stamps.fileSize = static_cast<std::uint64_t>(size);
    stamps.modifiedStamp = quicksift::PersistentCache::SourceStampFor(path);
    if (stamps.modifiedStamp == 0) {
        ec.clear();
        const auto modified = fs::last_write_time(path, ec);
        if (!ec) stamps.modifiedStamp = quicksift::PersistentCache::FileTimeStamp(modified);
    }
    stamps.sidecarStamp = MetadataStampFor(path);
    stamps.valid = true;
    return stamps;
}

void QuickSiftApplicationImpl::ApplyMetadataFileStamps(PhotoItem& item,
    const quicksift::transactions::MetadataChange::FileStamps& stamps) noexcept {
    if (!stamps.valid) return;
    if (stamps.fileSize != 0) item.fileSize = static_cast<std::uintmax_t>(stamps.fileSize);
    item.modifiedStamp = stamps.modifiedStamp;
    item.sidecarStamp = stamps.sidecarStamp;
}

void QuickSiftApplicationImpl::QueueMetadataEdit(const std::vector<fs::path>& targets,
    MetadataFieldId field, int value, const std::wstring& label) {
    std::size_t queued = 0;
    std::size_t missing = 0;
    for (const fs::path& path : targets) {
        const auto index = CatalogIndexForPath(path.wstring());
        const auto shown = SnapshotForPath(path); // includes in-flight optimistic values
        if (!index || !shown) {
            ++missing;
            continue;
        }
        const MetadataFieldValue current = MetadataFieldFor(shown->values, field);
        if (current.IsKnown() && current.value == value) continue; // nothing new to show
        const PhotoMetadataValues display = pendingMetadataEdits_.Record(*shown, field, value);
        catalog_.EditPhoto(*index, [&](PhotoItem& item) {
            ApplyValuesToPhoto(item, display);
            ++item.metadataRevision;
        });
        ++queued;
    }
    if (queued == 0) {
        if (missing != 0) {
            ShowGlassAlertText(FillToken(Tr(L"{label} update stopped"), L"{label}", Tr(label)),
                Tr(L"QuickSift could not locate one or more metadata targets."), GlassAlertKind::Warning);
        } else {
            ShowToastText(FillToken(Tr(L"{label} already matches the selected photos — no files were rewritten"),
                L"{label}", Tr(label)));
        }
        return;
    }
    quicksift::core::priority::Demand().SetInteractiveWritePending(true);
    QS_LOG_EVENT(quicksift::diagnostics::Level::Trace, quicksift::diagnostics::category::kWriteLane,
        L"edit_queued", { L"files", std::to_wstring(queued) },
        { L"queue", std::to_wstring(pendingMetadataEdits_.Size()) });
    FlushPendingMetadataEdits();
    EnsureMetadataWriteLaneTimer();
    UpdateColorLabelButton();
    UpdateStatus();
    for (int id : { ID_RATING_MENU, ID_PICK_STATE_MENU, ID_COLOR_LABEL }) {
        if (controls_.contains(id)) InvalidateRect(controls_[id], nullptr, FALSE);
    }
    InvalidateCanvas();
}

void QuickSiftApplicationImpl::FlushPendingMetadataEdits() {
    auto& demand = quicksift::core::priority::Demand();
    if (pendingMetadataEdits_.Empty()) {
        demand.SetInteractiveWritePending(false);
        return;
    }
    // Never race file transactions or history replay; the lane timer retries.
    if (transactions_.FileBusy() || replayingHistory_) return;
    const bool urgent = transactions_.MetadataBusy();
    if (urgent && !transactions_.MetadataUrgentSlotFree()) return; // last value keeps coalescing

    std::vector<quicksift::core::PendingMetadataEdit> entries = pendingMetadataEdits_.TakeAll();
    quicksift::transactions::MetadataTransactionPlan plan;
    // Label: the single field edited, or the generic "Metadata".
    std::optional<MetadataFieldId> onlyField;
    bool mixed = false;
    std::vector<fs::path> paths;
    paths.reserve(entries.size());
    for (const auto& entry : entries) {
        paths.push_back(entry.before.path);
        for (const MetadataFieldId field : entry.fields) {
            if (!onlyField) onlyField = field;
            else if (*onlyField != field) mixed = true;
        }
    }
    plan.label = (!onlyField || mixed) ? std::wstring(L"Metadata") : MetadataFieldLabel(*onlyField);
    plan.recordHistory = HistoryStore::EstimateMetadataBytesForTargets(paths, plan.label) <=
        historyStore_.MemoryLimit();
    plan.changes.reserve(entries.size());
    for (const auto& entry : entries) {
        quicksift::transactions::MetadataChange change;
        change.before = entry.before;
        change.patch = entry.patch;
        change.after = entry.before;
        change.after.values = ApplyMetadataPatch(entry.before.values, entry.patch);
        if (const auto index = CatalogIndexForPath(entry.before.path.wstring())) {
            const std::uint64_t revision = catalog_.PhotoAt(*index).metadataRevision;
            change.before.revision = revision;
            change.after.revision = revision;
        }
        plan.changes.push_back(std::move(change));
    }
    const std::uint64_t token = urgent ? transactions_.SubmitUrgentMetadata(plan) :
        transactions_.SubmitMetadata(plan);
    if (token == 0) {
        // Refused (slot raced, close pending): keep the edits and their order.
        pendingMetadataEdits_.Requeue(std::move(entries));
        EnsureMetadataWriteLaneTimer();
        return;
    }
    demand.SetMetadataWritesPending(1);
    demand.SetInteractiveWritePending(!pendingMetadataEdits_.Empty());
    demand.Touch();
    QS_LOG_EVENT(quicksift::diagnostics::Level::Trace, quicksift::diagnostics::category::kWriteLane,
        L"edits_flushed", { L"files", std::to_wstring(plan.changes.size()) },
        { L"lane", urgent ? L"urgent" : L"normal" }, { L"token", std::to_wstring(token) });
    EnsureMetadataWriteLaneTimer();
}

void QuickSiftApplicationImpl::EnsureMetadataWriteLaneTimer() {
    if (metadataWriteLaneTimerActive_) return;
    metadataWriteLaneTimerActive_ = StartUiTimer(ID_TIMER_METADATA_WRITE_LANE, 200,
        L"metadata write lane");
}

void QuickSiftApplicationImpl::PollMetadataWriteLane() {
    // Backstop for a lost completion post: results are owned by the service until
    // taken, so draining here is idempotent with the WM_APP completion path.
    DrainMetadataTransactionResults();
    FlushPendingMetadataEdits();

    const auto progress = transactions_.MetadataProgress();
    if (progress && progress->waitingExternal && !progress->waitingPath.empty()) {
        const ULONGLONG now = GetTickCount64();
        // Non-blocking status: once per waiting episode, refreshed every ~6 s.
        if (progress->waitingSerial != metadataWaitingToastSerial_ ||
            now - metadataWaitingToastTick_ >= 6000) {
            const bool firstForEpisode = progress->waitingSerial != metadataWaitingToastSerial_;
            metadataWaitingToastSerial_ = progress->waitingSerial;
            metadataWaitingToastTick_ = now;
            const fs::path waitingPath(progress->waitingPath);
            ShowToastText(UserErrorText(quicksift::core::UserErrorCode::WriteInUseWaiting,
                waitingPath.filename().wstring()));
            if (firstForEpisode) {
                QS_LOG_EVENT(quicksift::diagnostics::Level::Warning,
                    quicksift::diagnostics::category::kWriteLane, L"waiting_for_other_app",
                    { L"file", quicksift::diagnostics::LogFileName(waitingPath) },
                    { L"path_id", quicksift::diagnostics::LogPathId(waitingPath) },
                    { L"waited_ms", std::to_wstring(progress->waitedFor.count()) });
            }
        }
    }

    auto& demand = quicksift::core::priority::Demand();
    const bool metadataBusy = transactions_.MetadataBusy();
    demand.SetMetadataWritesPending(metadataBusy ? 1u : 0u);
    demand.SetInteractiveWritePending(!pendingMetadataEdits_.Empty());
    if (!metadataBusy && pendingMetadataEdits_.Empty()) {
        KillTimer(hwnd_, ID_TIMER_METADATA_WRITE_LANE);
        metadataWriteLaneTimerActive_ = false;
    }
}

void QuickSiftApplicationImpl::RetryInUseMetadataChanges() {
    std::vector<quicksift::transactions::MetadataChange> changes = std::move(inUseRetryChanges_);
    inUseRetryChanges_.clear();
    if (changes.empty()) return;
    // Re-enter through the coalescing queue: the optimistic value shows again, the
    // CAS expectation is the current (rolled-back) catalog value, and it is written
    // on the urgent lane if something else is running.
    std::size_t requeued = 0;
    for (const auto& change : changes) {
        const auto index = CatalogIndexForPath(change.before.path.wstring());
        const auto shown = SnapshotForPath(change.before.path);
        if (!index || !shown) continue;
        PhotoMetadataValues display = shown->values;
        bool any = false;
        const auto requeueField = [&](MetadataFieldId field, const MetadataPatchField& patchField) {
            if (!patchField.active) return;
            display = pendingMetadataEdits_.Record(*shown, field, patchField.desired);
            any = true;
        };
        requeueField(MetadataFieldId::Rating, change.patch.rating);
        requeueField(MetadataFieldId::ColorLabel, change.patch.colorLabel);
        requeueField(MetadataFieldId::PickState, change.patch.pickState);
        if (!any) continue;
        catalog_.EditPhoto(*index, [&](PhotoItem& item) {
            ApplyValuesToPhoto(item, display);
            ++item.metadataRevision;
        });
        ++requeued;
    }
    QS_LOG_EVENT(quicksift::diagnostics::Level::Info, quicksift::diagnostics::category::kWriteLane,
        L"in_use_retry", { L"files", std::to_wstring(requeued) });
    if (requeued == 0) return;
    quicksift::core::priority::Demand().SetInteractiveWritePending(true);
    FlushPendingMetadataEdits();
    EnsureMetadataWriteLaneTimer();
    RebuildVisiblePhotos();
    UpdateColorLabelButton();
    UpdateStatus();
    InvalidateCanvas();
}

void QuickSiftApplicationImpl::SkipInUseMetadataChanges() {
    if (inUseRetryChanges_.empty()) return;
    QS_LOG_EVENT(quicksift::diagnostics::Level::Info, quicksift::diagnostics::category::kWriteLane,
        L"in_use_skipped", { L"files", std::to_wstring(inUseRetryChanges_.size()) });
    inUseRetryChanges_.clear();
}

} // namespace quicksift::app
