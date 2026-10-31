// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Previous-session log browser: the diagnostic log window's
// "Previous sessions…" button, its worker (listing and one bounded read off the
// UI thread), the dropdown of logs (newest first) and the read-only viewer window.
// Policy (which files, order, labels, how much is shown) is core/session_log_browser.h;
// file access is diagnostics::ListPreviousSessionLogs / ReadSessionLogTail. This
// module never deletes or writes anything: old logs are removed only by the
// 7-day cleanup (core/log_retention.h).
#include "app/quicksift_application_internal.h"
#include "core/session_log_browser.h"
#include "diagnostics/log_and_continue.h"

namespace quicksift::app {

namespace {
namespace sl = quicksift::core::session_logs;

bool IsDiagnosticLogWindow(HWND window) noexcept {
    return window && IsWindow(window) &&
        reinterpret_cast<ULONG_PTR>(GetPropW(window, kDocumentKindProperty)) ==
            static_cast<ULONG_PTR>(DocumentKind::DiagnosticLog);
}

void ReplaceToken(std::wstring& text, std::wstring_view token, std::wstring_view value) {
    const std::size_t at = text.find(token);
    if (at != std::wstring::npos) text.replace(at, token.size(), value);
}
} // namespace

void QuickSiftApplicationImpl::BrowseSessionLogs(HWND documentWindow, HWND anchor) {
    if (!IsDiagnosticLogWindow(documentWindow)) return;
    // A second click while a listing or read is running is ignored; the dropdown
    // or window it asked for is about to appear.
    if (!StartSessionLogWorker(true, documentWindow, anchor, {}) &&
        !sessionLogWorkerRunning_.load(std::memory_order_acquire) && !workersStoppedForTeardown_) {
        MessageBoxW(documentWindow, Tr(L"QuickSift could not list the previous session logs.").c_str(),
            L"QuickSift", MB_OK | MB_ICONWARNING);
    }
}

bool QuickSiftApplicationImpl::StartSessionLogWorker(bool listing, HWND requester, HWND anchor, std::wstring name) {
    if (workersStoppedForTeardown_ || sessionLogWorkerRunning_.load(std::memory_order_acquire)) return false;
    StopSessionLogWorker(); // joins a finished previous worker (never a running one: checked above)
    sessionLogWorkerRunning_.store(true, std::memory_order_release);
    {
        std::lock_guard lock(sessionLogWorkerMutex_);
        sessionLogWorkerResult_.reset();
    }
    const HWND window = hwnd_;
    try {
        sessionLogWorker_ = std::thread([this, window, listing, requester, anchor, name = std::move(name),
            token = workerLifetime_.Issue()]() noexcept {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            SessionLogWorkerResult result;
            try {
                result.listing = listing;
                result.requester = requester;
                result.anchor = anchor;
                result.name = name;
                if (listing) result.logs = quicksift::diagnostics::ListPreviousSessionLogs();
                else result.text = quicksift::diagnostics::ReadSessionLogTail(name);
            } catch (...) {
                QS_LOG_AND_CONTINUE(L"Diagnostics", L"SessionLogWorker");
                result.text.ok = false;
            }
            try {
                std::lock_guard lock(sessionLogWorkerMutex_);
                sessionLogWorkerResult_ = std::move(result);
            } catch (...) {
                QS_LOG_AND_CONTINUE(L"Diagnostics", L"SessionLogWorker [2]");
            }
            sessionLogWorkerRunning_.store(false, std::memory_order_release);
            static_cast<void>(PostFromWorker(token, window, WM_APP_SESSION_LOG_WORKER_DONE));
        });
    } catch (...) {
        QS_LOG_AND_CONTINUE(L"Diagnostics", L"StartSessionLogWorker");
        sessionLogWorkerRunning_.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void QuickSiftApplicationImpl::StopSessionLogWorker() noexcept {
    try {
        if (sessionLogWorker_.joinable()) sessionLogWorker_.join();
    } catch (...) {
        QS_LOG_AND_CONTINUE(L"Diagnostics", L"StopSessionLogWorker");
        // Shutdown boundary: never throw.
    }
    sessionLogWorkerRunning_.store(false, std::memory_order_release);
}

void QuickSiftApplicationImpl::OnSessionLogWorkerDone() {
    std::optional<SessionLogWorkerResult> taken;
    {
        std::lock_guard lock(sessionLogWorkerMutex_);
        taken = std::move(sessionLogWorkerResult_);
        sessionLogWorkerResult_.reset();
    }
    if (sessionLogWorker_.joinable() && !sessionLogWorkerRunning_.load(std::memory_order_acquire)) {
        try { sessionLogWorker_.join(); } catch (...) { QS_LOG_AND_CONTINUE(L"Diagnostics", L"OnSessionLogWorkerDone"); }
    }
    // The diagnostic log window may have been closed while the worker ran.
    if (!taken || !IsDiagnosticLogWindow(taken->requester)) return;
    if (taken->listing) ShowSessionLogMenu(*taken);
    else ShowSessionLogDocument(*taken);
}

void QuickSiftApplicationImpl::ShowSessionLogMenu(SessionLogWorkerResult& result) {
    sessionLogMenu_ = std::move(result.logs);
    QS_LOG_EVENT(quicksift::diagnostics::Level::Info, L"Diagnostics", L"session_logs_listed",
        {L"count", std::to_wstring(sessionLogMenu_.size())});
    std::vector<MenuItem> items;
    if (sessionLogMenu_.empty()) {
        MenuItem none{};
        none.text = Tr(L"No previous session logs");
        none.header = true;
        items.push_back(std::move(none));
    } else {
        for (std::size_t index = 0; index < sessionLogMenu_.size(); ++index) {
            const int command = ID_DOCUMENT_SESSION_LOG_FIRST + static_cast<int>(index);
            if (command > ID_DOCUMENT_SESSION_LOG_LAST) break;
            MenuItem item{};
            item.text = sl::Label(sessionLogMenu_[index].name, sessionLogMenu_[index].bytes);
            item.command = command;
            items.push_back(std::move(item));
        }
    }
    const HWND anchor = result.anchor && IsWindow(result.anchor) ? result.anchor : result.requester;
    const int command = TrackDropdownMenuVerbatim(result.requester, anchor, std::move(items));
    if (command < ID_DOCUMENT_SESSION_LOG_FIRST || command > ID_DOCUMENT_SESSION_LOG_LAST) return;
    const auto index = static_cast<std::size_t>(command - ID_DOCUMENT_SESSION_LOG_FIRST);
    if (index >= sessionLogMenu_.size() || !IsDiagnosticLogWindow(result.requester)) return;
    if (!StartSessionLogWorker(false, result.requester, nullptr, sessionLogMenu_[index].name) &&
        !workersStoppedForTeardown_) {
        MessageBoxW(result.requester, Tr(L"QuickSift could not read that session log.").c_str(),
            L"QuickSift", MB_OK | MB_ICONWARNING);
    }
}

void QuickSiftApplicationImpl::ShowSessionLogDocument(const SessionLogWorkerResult& result) {
    const quicksift::diagnostics::SessionLogText& log = result.text;
    if (!log.ok) {
        QS_LOG_EVENT(quicksift::diagnostics::Level::Warning, L"Diagnostics", L"session_log_open_failed",
            {L"file", result.name}, {L"win32", std::to_wstring(log.error)});
        MessageBoxW(result.requester, Tr(L"QuickSift could not read that session log.").c_str(),
            L"QuickSift", MB_OK | MB_ICONWARNING);
        return;
    }
    QS_LOG_EVENT(quicksift::diagnostics::Level::Info, L"Diagnostics", L"session_log_opened",
        {L"file", result.name}, {L"bytes", std::to_wstring(log.totalBytes)},
        {L"shown_bytes", std::to_wstring(log.shownBytes)}, {L"truncated", log.truncated ? L"1" : L"0"});
    std::wstring body = Tr(L"Log file: {file}");
    ReplaceToken(body, L"{file}", (quicksift::diagnostics::SessionLogDirectory() / result.name).wstring());
    if (log.truncated) {
        std::wstring note = Tr(L"Showing the newest {shown} of {total}. Open the file itself to see older entries.");
        ReplaceToken(note, L"{shown}", sl::SizeText(log.shownBytes));
        ReplaceToken(note, L"{total}", sl::SizeText(log.totalBytes));
        body += L"\r\n" + note;
    }
    body += L"\r\n\r\n";
    body += log.text;
    const std::wstring suffix = sl::Label(result.name, log.totalBytes);
    constexpr std::wstring_view kTitleKey = L"Previous session log"; // loc-key
    const HWND window = ShowDocumentWindow(Tr(kTitleKey) + L" \u2014 " + suffix, body,
        DocumentKind::SessionLog, kTitleKey);
    if (!window) return;
    if (auto* state = reinterpret_cast<DocumentWindowState*>(GetPropW(window, kDocumentStateProperty)))
        state->titleSuffix = suffix;
}

} // namespace quicksift::app
