// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Live "QuickSift is waiting for…" dialog for Close, ratings import/export
// and Batch (core/blocking_wait.h), Force quit and the diagnostic-log trail.
#include "app/quicksift_application_internal.h"
#include "diagnostics/log_categories.h"

namespace quicksift::app {

namespace bw = quicksift::core::blocking_wait;

std::vector<bw::Blocker> QuickSiftApplicationImpl::CurrentBlockers(bool includeUnsavedEdits) {
    std::vector<bw::Blocker> blockers;
    const auto now = std::chrono::steady_clock::now();
    const auto since = [&](bw::Reason reason) {
        auto [it, inserted] = blockerFirstSeen_.try_emplace(static_cast<int>(reason), now);
        (void)inserted;
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
    };
    std::unordered_map<int, std::chrono::steady_clock::time_point> seen;
    const auto keep = [&](bw::Reason reason) {
        const auto found = blockerFirstSeen_.find(static_cast<int>(reason));
        if (found != blockerFirstSeen_.end()) seen.emplace(found->first, found->second);
    };
    if (const auto progress = transactions_.MetadataProgress()) {
        bw::Blocker blocker;
        blocker.reason = !progress->waitingPath.empty() ?
            (progress->waitingExternal ? bw::Reason::FileLockedByOtherApp : bw::Reason::WriteRetry) :
            bw::Reason::MetadataWrite;
        blocker.file = progress->waitingPath.empty() ? std::wstring{} :
            fs::path(progress->waitingPath).filename().wstring();
        blocker.completed = progress->completedItems;
        blocker.total = progress->totalItems;
        blocker.elapsed = !progress->waitingPath.empty() ? progress->waitedFor : since(blocker.reason);
        keep(blocker.reason);
        blockers.push_back(std::move(blocker));
    }
    if (const auto progress = transactions_.FileProgress()) {
        bw::Blocker blocker;
        blocker.reason = bw::Reason::FileOperation;
        blocker.completed = progress->completedFiles;
        blocker.total = progress->totalFiles;
        blocker.elapsed = std::chrono::milliseconds(static_cast<long long>(progress->elapsedSeconds * 1000.0));
        blockers.push_back(std::move(blocker));
    }
    // Close never waits for read-only workers (export, batch folder scan/preflight):
    // teardown cancels them. includeUnsavedEdits doubles as "this is Close".
    if (!includeUnsavedEdits && (RatingsWorkerBusy() || ratingsImportToken_ != 0)) {
        bw::Blocker blocker;
        blocker.reason = bw::Reason::RatingsWorker;
        blocker.completed = ratingsWorkerDone_.load(std::memory_order_acquire);
        blocker.total = ratingsWorkerTotal_.load(std::memory_order_acquire);
        blocker.elapsed = since(blocker.reason);
        keep(blocker.reason);
        blockers.push_back(std::move(blocker));
    }
    if (!includeUnsavedEdits && BatchWorkerBusy()) {
        bw::Blocker blocker;
        blocker.reason = bw::Reason::BatchWorker;
        blocker.elapsed = since(blocker.reason);
        keep(blocker.reason);
        blockers.push_back(std::move(blocker));
    }
    if (includeUnsavedEdits && !pendingMetadataEdits_.Empty()) {
        bw::Blocker blocker;
        blocker.reason = bw::Reason::UnsavedEdits;
        blocker.total = pendingMetadataEdits_.Size();
        blocker.elapsed = since(blocker.reason);
        keep(blocker.reason);
        blockers.push_back(std::move(blocker));
    }
    // Reasons that cleared restart their clock the next time they appear.
    blockerFirstSeen_.swap(seen);
    return blockers;
}

bool QuickSiftApplicationImpl::ShowBlockingWaitIfNeeded(bw::Command command) {
    const auto blockers = CurrentBlockers(command == bw::Command::Close);
    if (blockers.empty()) return false;
    blockingWaitCommand_ = command;
    blockingWaitStarted_ = std::chrono::steady_clock::now();
    blockingWaitLastLog_ = {};
    quicksift::ui::framework::AlertRequest request{};
    request.title = Tr(bw::CommandKey(command));
    request.message = bw::Describe(command, blockers, [this](std::wstring_view key) { return Tr(key); });
    request.primaryText = Tr(bw::OffersForceQuit(command) ? L"Force quit" : L"Cancel operation");
    request.secondaryText = Tr(bw::OffersForceQuit(command) ? L"Wait" : L"Stop waiting");
    request.kind = GlassAlertKind::Warning;
    request.mode = GlassAlertMode::Confirmation;
    request.destructive = bw::OffersForceQuit(command);
    request.actionToken = static_cast<std::uint64_t>(PendingGlassAction::BlockingWaitPrimary);
    uiFramework_.Alerts().Show(std::move(request));
    uiFramework_.SyncOverlayTransitions(std::chrono::steady_clock::now());
    SetFocus(canvas_ ? canvas_ : hwnd_);
    LogBlockingWait(blockers, true);
    StartUiTimer(ID_TIMER_BLOCKING_WAIT, 500, L"blocking wait");
    InvalidateCanvas();
    return true;
}

void QuickSiftApplicationImpl::LogBlockingWait(const std::vector<bw::Blocker>& blockers, bool first) {
    if (!blockingWaitCommand_) return;
    const auto now = std::chrono::steady_clock::now();
    const auto sinceLog = blockingWaitLastLog_ == std::chrono::steady_clock::time_point{} ?
        std::chrono::milliseconds::max() :
        std::chrono::duration_cast<std::chrono::milliseconds>(now - blockingWaitLastLog_);
    if (!bw::ShouldLog(sinceLog, first)) return;
    blockingWaitLastLog_ = now;
    for (const bw::Blocker& blocker : blockers) {
        QS_LOG_EVENT(quicksift::diagnostics::Level::Warning, L"BlockingWait", L"waiting",
            { L"command", std::wstring(bw::CommandLogName(*blockingWaitCommand_)) },
            { L"reason", std::wstring(bw::ReasonLogName(blocker.reason)) },
            { L"file", blocker.file },
            { L"completed", std::to_wstring(blocker.completed) },
            { L"total", std::to_wstring(blocker.total) },
            { L"elapsed_ms", std::to_wstring(blocker.elapsed.count()) });
    }
}

void QuickSiftApplicationImpl::UpdateBlockingWaitDialog() {
    auto& alerts = uiFramework_.Alerts();
    const bool ours = alerts.Visible() &&
        static_cast<PendingGlassAction>(alerts.Request().actionToken) == PendingGlassAction::BlockingWaitPrimary;
    if (!blockingWaitCommand_ || !ours) {
        KillTimer(hwnd_, ID_TIMER_BLOCKING_WAIT);
        blockingWaitCommand_.reset();
        return;
    }
    const bw::Command command = *blockingWaitCommand_;
    const auto blockers = CurrentBlockers(command == bw::Command::Close);
    if (blockers.empty()) {
        // Unblocked: dismiss and carry on with what the user asked for.
        KillTimer(hwnd_, ID_TIMER_BLOCKING_WAIT);
        blockingWaitCommand_.reset();
        alerts.Clear();
        InvalidateCanvas();
        QS_LOG_EVENT(quicksift::diagnostics::Level::Info, L"BlockingWait", L"cleared",
            { L"command", std::wstring(bw::CommandLogName(command)) });
        if (command == bw::Command::Close) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        else ShowToast(L"Ready. You can start it again now.");
        return;
    }
    alerts.UpdateMessage(bw::Describe(command, blockers, [this](std::wstring_view key) { return Tr(key); }));
    LogBlockingWait(blockers, false);
    InvalidateCanvas();
}

void QuickSiftApplicationImpl::OnBlockingWaitPrimary() {
    if (!blockingWaitCommand_) return;
    const bw::Command command = *blockingWaitCommand_;
    blockingWaitCommand_.reset();
    KillTimer(hwnd_, ID_TIMER_BLOCKING_WAIT);
    const auto blockers = CurrentBlockers(true);
    for (const bw::Blocker& blocker : blockers) {
        QS_LOG_EVENT(quicksift::diagnostics::Level::Warning, L"BlockingWait",
            command == bw::Command::Close ? L"force_quit_abandoned" : L"operation_cancelled",
            { L"command", std::wstring(bw::CommandLogName(command)) },
            { L"reason", std::wstring(bw::ReasonLogName(blocker.reason)) },
            { L"file", blocker.file },
            { L"left", std::to_wstring(blocker.total > blocker.completed ? blocker.total - blocker.completed : 0) });
    }
    // Every blocking operation honours cancellation at its next safe boundary
    // (never mid-file / mid atomic media group).
    transactions_.CancelAll();
    ratingsWorkerCancel_.store(true, std::memory_order_release);
    batchWorkerCancel_.store(true, std::memory_order_release);
    if (command != bw::Command::Close) {
        ShowToast(L"Cancelling at the next safe point…");
        return;
    }
    // Force quit: flush what can be flushed, drop the rest (logged above), exit.
    if (!pendingMetadataEdits_.Empty()) {
        QS_LOG_WARNING(L"BlockingWait", L"Force quit dropped " +
            std::to_wstring(pendingMetadataEdits_.Size()) + L" unsaved metadata edit(s)");
        pendingMetadataEdits_.Clear();
    }
    SaveSession();
    worker_.RequestCacheFlush();
    closingAfterTransactionCancellation_ = true;
    transactions_.RequestCloseAtSafeBoundary();
    QS_LOG_WARNING(L"Lifecycle", L"Force quit requested; closing after workers stop at a safe boundary");
    DestroyWindow(hwnd_);
}

} // namespace quicksift::app
