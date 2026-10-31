// CODE GUIDE: See CODE_GUIDE.md -> "Logging and crashes".
// OWNER: Logging/crash-report API; record context without exposing image pixels or credentials.

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <initializer_list>
#include <cstdint>
#include <functional>
#include <vector>

#include "core/session_log_browser.h"

namespace quicksift::diagnostics {

enum class Level { Trace, Info, Warning, Error, Critical };

struct Field {
    std::wstring_view key;
    std::wstring value;
};

[[nodiscard]] std::uint64_t SessionId() noexcept;
void SetLogLevel(Level level) noexcept;
[[nodiscard]] Level GetLogLevel() noexcept;
void SetVerboseLogging(bool enabled) noexcept;
[[nodiscard]] bool VerboseLogging() noexcept;
void WriteEvent(Level level, std::wstring_view category, std::wstring_view event,
    std::initializer_list<Field> fields = {}) noexcept;
void WriteMetric(std::wstring_view category, std::wstring_view metric, double value,
    std::wstring_view unit = {}) noexcept;
void WriteSystemSnapshot(std::wstring_view reason = {}) noexcept;

[[nodiscard]] bool Initialize() noexcept;
void Shutdown() noexcept;
void InstallCrashHandlers() noexcept;
void Write(Level level, std::wstring_view category, std::wstring_view message) noexcept;
void WriteLastError(Level level, std::wstring_view category, std::wstring_view operation,
    DWORD error = GetLastError()) noexcept;

[[nodiscard]] std::wstring SnapshotText();
[[nodiscard]] std::filesystem::path DataRootPath() noexcept;
[[nodiscard]] std::filesystem::path SessionLogPath();
[[nodiscard]] std::filesystem::path CrashReportDirectory();
[[nodiscard]] bool ExportSnapshot(const std::filesystem::path& destination) noexcept;
[[nodiscard]] bool ExportSnapshotInteractive(HWND owner, std::filesystem::path* destination = nullptr) noexcept;

// Deletes QuickSift's own session logs (Logs) and crash reports/minidumps
// (CrashReports) older than core::log_retention::kMaxAge (7 days, last-write
// time). Never the current session log, never anything that does not match
// QuickSift's naming pattern, never a reparse point, never recursive. Every
// deletion is logged. Call on a background thread; `cancelled` is polled
// between files.
struct CleanupSummary {
    std::size_t scanned = 0;
    std::size_t deleted = 0;
    std::size_t failed = 0;
    bool cancelled = false;
};
CleanupSummary CleanupOldDiagnosticFiles(const std::function<bool()>& cancelled = {}) noexcept;

// Previous-session log browser (read-only; deletion stays with the 7-day cleanup).
// The folder is the one this session's log lives in: <data root>\Logs.
[[nodiscard]] std::filesystem::path SessionLogDirectory();
// Non-recursive listing of QuickSift's own session logs in SessionLogDirectory(),
// newest first, without the current session's log. The folder is skipped when it
// is a reparse point; entries that are links are never listed. Call off the UI thread.
[[nodiscard]] std::vector<core::session_logs::Listed> ListPreviousSessionLogs() noexcept;
struct SessionLogText {
    bool ok = false;
    std::wstring text;                  // UTF-16, as written (newest part when truncated)
    std::uint64_t totalBytes = 0;       // file size
    std::uint64_t shownBytes = 0;       // bytes behind `text`
    bool truncated = false;             // only the newest core::session_logs::kMaxDisplayBytes
    DWORD error = ERROR_SUCCESS;
};
// Reads one log named by ListPreviousSessionLogs (a bare QuickSift session-log
// name; anything else is refused) through a handle that never follows a link.
// Reads at most core::session_logs::kMaxDisplayBytes. Call off the UI thread.
[[nodiscard]] SessionLogText ReadSessionLogTail(std::wstring_view name) noexcept;

} // namespace quicksift::diagnostics

#define QS_LOG_TRACE(category, message) ::quicksift::diagnostics::Write(::quicksift::diagnostics::Level::Trace, category, message)
#define QS_LOG_INFO(category, message) ::quicksift::diagnostics::Write(::quicksift::diagnostics::Level::Info, category, message)
#define QS_LOG_WARNING(category, message) ::quicksift::diagnostics::Write(::quicksift::diagnostics::Level::Warning, category, message)
#define QS_LOG_ERROR(category, message) ::quicksift::diagnostics::Write(::quicksift::diagnostics::Level::Error, category, message)
#define QS_LOG_CRITICAL(category, message) ::quicksift::diagnostics::Write(::quicksift::diagnostics::Level::Critical, category, message)
#define QS_LOG_EVENT(level, category, event, ...) ::quicksift::diagnostics::WriteEvent(level, category, event, { __VA_ARGS__ })
