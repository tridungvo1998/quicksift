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

} // namespace quicksift::diagnostics

#define QS_LOG_TRACE(category, message) ::quicksift::diagnostics::Write(::quicksift::diagnostics::Level::Trace, category, message)
#define QS_LOG_INFO(category, message) ::quicksift::diagnostics::Write(::quicksift::diagnostics::Level::Info, category, message)
#define QS_LOG_WARNING(category, message) ::quicksift::diagnostics::Write(::quicksift::diagnostics::Level::Warning, category, message)
#define QS_LOG_ERROR(category, message) ::quicksift::diagnostics::Write(::quicksift::diagnostics::Level::Error, category, message)
#define QS_LOG_CRITICAL(category, message) ::quicksift::diagnostics::Write(::quicksift::diagnostics::Level::Critical, category, message)
#define QS_LOG_EVENT(level, category, event, ...) ::quicksift::diagnostics::WriteEvent(level, category, event, { __VA_ARGS__ })
