// OWNER: Previous-session log browser worker and its last listing
// (session_log_browser_controller.cpp).
//
// Plain state of QuickSiftApplicationImpl (no behaviour). The class inherits
// this struct privately, so every member keeps its name and is used exactly as
// before; see app/quicksift_application_internal.h.
#pragma once

#include "app/state/app_state_common.h"
#include "core/session_log_browser.h"
#include "diagnostics/diagnostic_log.h"

namespace quicksift::app::state {

struct SessionLogBrowserState {
    // One request at a time: list the Logs folder, or read one log (bounded).
    struct SessionLogWorkerResult {
        bool listing = false;
        HWND requester = nullptr;   // diagnostic log window that asked (re-checked on the UI thread)
        HWND anchor = nullptr;      // its "Previous sessions…" button (listing only)
        std::vector<quicksift::core::session_logs::Listed> logs;   // listing
        std::wstring name;                                         // read
        quicksift::diagnostics::SessionLogText text;               // read
    };
    std::thread sessionLogWorker_;
    std::atomic<bool> sessionLogWorkerRunning_{ false };
    mutable std::mutex sessionLogWorkerMutex_;   // held only to move the result in or out
    std::optional<SessionLogWorkerResult> sessionLogWorkerResult_;
    // UI thread: the listing behind the open dropdown (index = command - FIRST).
    std::vector<quicksift::core::session_logs::Listed> sessionLogMenu_;
};

} // namespace quicksift::app::state
