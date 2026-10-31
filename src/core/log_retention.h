// OWNER: Retention policy for QuickSift's own diagnostic files (session logs in
// <data root>\Logs, crash reports and minidumps in <data root>\CrashReports).
//
// Header-only and portable (no windows.h) so lifecycle_checks can cover it. The
// Windows side (diagnostics::CleanupOldDiagnosticFiles) only lists the two
// folders non-recursively and asks Plan() which entries to delete. Nothing else
// is ever touched: the name must match QuickSift's own pattern exactly, the
// entry must be a plain file (no directory, no reparse point), it must be older
// than kMaxAge by last-write time, and it must not be the current session log.
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace quicksift::core::log_retention {

inline constexpr std::chrono::hours kMaxAge{ 24 * 7 };

enum class Kind { None, SessionLog, CrashReport, CrashDump };

namespace detail {
[[nodiscard]] constexpr bool Digits(std::wstring_view text) noexcept {
    if (text.empty()) return false;
    for (const wchar_t c : text) if (c < L'0' || c > L'9') return false;
    return true;
}
// "YYYYMMDD-HHMMSS-mmm" as written by TimestampForFile().
[[nodiscard]] constexpr bool Stamp(std::wstring_view text) noexcept {
    return text.size() == 19 && Digits(text.substr(0, 8)) && text[8] == L'-' &&
        Digits(text.substr(9, 6)) && text[15] == L'-' && Digits(text.substr(16, 3));
}
[[nodiscard]] constexpr wchar_t Lower(wchar_t c) noexcept {
    return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c;
}
[[nodiscard]] constexpr bool EndsWithNoCase(std::wstring_view text, std::wstring_view suffix) noexcept {
    if (text.size() < suffix.size()) return false;
    const std::wstring_view tail = text.substr(text.size() - suffix.size());
    for (std::size_t i = 0; i < suffix.size(); ++i) if (Lower(tail[i]) != Lower(suffix[i])) return false;
    return true;
}
} // namespace detail

// "QuickSift-<stamp>-<pid>.log" (Logs folder).
// "QuickSift-crash-<stamp>-p<pid>.log" / ".dmp" (CrashReports folder).
[[nodiscard]] constexpr Kind Classify(std::wstring_view name) noexcept {
    constexpr std::wstring_view kCrash = L"QuickSift-crash-";
    constexpr std::wstring_view kSession = L"QuickSift-";
    if (name.starts_with(kCrash)) {
        const bool log = detail::EndsWithNoCase(name, L".log");
        const bool dump = detail::EndsWithNoCase(name, L".dmp");
        if (!log && !dump) return Kind::None;
        const std::wstring_view middle = name.substr(kCrash.size(), name.size() - kCrash.size() - 4);
        if (middle.size() < 22 || !detail::Stamp(middle.substr(0, 19)) || middle.substr(19, 2) != L"-p" ||
            !detail::Digits(middle.substr(21))) return Kind::None;
        return log ? Kind::CrashReport : Kind::CrashDump;
    }
    if (name.starts_with(kSession) && detail::EndsWithNoCase(name, L".log")) {
        const std::wstring_view middle = name.substr(kSession.size(), name.size() - kSession.size() - 4);
        if (middle.size() < 21 || !detail::Stamp(middle.substr(0, 19)) || middle[19] != L'-' ||
            !detail::Digits(middle.substr(20))) return Kind::None;
        return Kind::SessionLog;
    }
    return Kind::None;
}

enum class Folder { Logs, CrashReports };

struct Entry {
    std::wstring name;                               // file name only, no path
    std::chrono::system_clock::time_point lastWrite{};
    bool regularFile = false;                        // false for directories and devices
    bool reparsePoint = false;                       // symlinks, junctions, cloud placeholders
};

enum class Skip { Delete, NotOurs, WrongFolder, NotPlainFile, ReparsePoint, CurrentSession, TooNew };

[[nodiscard]] inline Skip Decide(Folder folder, const Entry& entry, std::chrono::system_clock::time_point now,
    std::wstring_view currentSessionName) noexcept {
    const Kind kind = Classify(entry.name);
    if (kind == Kind::None) return Skip::NotOurs;
    if ((folder == Folder::Logs) != (kind == Kind::SessionLog)) return Skip::WrongFolder;
    if (entry.reparsePoint) return Skip::ReparsePoint;
    if (!entry.regularFile) return Skip::NotPlainFile;
    if (!currentSessionName.empty() && entry.name.size() == currentSessionName.size() &&
        detail::EndsWithNoCase(entry.name, currentSessionName)) return Skip::CurrentSession;
    // A last-write time in the future (clock change) is "new": never deleted.
    if (entry.lastWrite > now || now - entry.lastWrite <= kMaxAge) return Skip::TooNew;
    return Skip::Delete;
}

[[nodiscard]] inline std::vector<std::wstring> Plan(Folder folder, const std::vector<Entry>& entries,
    std::chrono::system_clock::time_point now, std::wstring_view currentSessionName) {
    std::vector<std::wstring> names;
    for (const Entry& entry : entries)
        if (Decide(folder, entry, now, currentSessionName) == Skip::Delete) names.push_back(entry.name);
    return names;
}

[[nodiscard]] constexpr std::wstring_view KindName(Kind kind) noexcept {
    switch (kind) {
    case Kind::SessionLog: return L"session_log";
    case Kind::CrashReport: return L"crash_report";
    case Kind::CrashDump: return L"minidump";
    case Kind::None: break;
    }
    return L"none";
}

} // namespace quicksift::core::log_retention
