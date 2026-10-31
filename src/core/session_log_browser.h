// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Previous-session log browser policy: which files in the Logs folder are
// listed, their order and menu label, and how much of one file is shown.
//
// Header-only and portable (no windows.h) so lifecycle_checks can cover it. The
// Windows side (diagnostics::ListPreviousSessionLogs / ReadSessionLogTail) lists
// the Logs folder non-recursively, reads one file through a handle that never
// follows a link, and asks this header what to keep. The browser is read-only:
// deleting old logs stays with the 7-day cleanup (core/log_retention.h).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "core/log_retention.h"

namespace quicksift::core::session_logs {

// The menu lists at most this many logs (newest first). The 7-day cleanup keeps
// the folder small, so this only matters after many restarts in one week.
inline constexpr std::size_t kMaxListed = 60;
// At most this much of one log is shown (the newest part). Session logs are
// capped at 16 MiB on disk; a rich-edit control with 16 MiB of text is slow.
inline constexpr std::uint64_t kMaxDisplayBytes = 2ull * 1024ull * 1024ull;

struct Candidate {
    std::wstring name;            // file name only
    std::uint64_t bytes = 0;
    bool regularFile = false;     // false for directories and devices
    bool reparsePoint = false;    // symlinks, junctions, cloud placeholders: never opened
};

struct Listed {
    std::wstring name;
    std::uint64_t bytes = 0;
};

namespace detail {
inline constexpr std::size_t kPrefixLength = 10; // "QuickSift-"
[[nodiscard]] inline std::wstring_view Stamp(std::wstring_view name) noexcept {
    return name.size() >= kPrefixLength + 19 ? name.substr(kPrefixLength, 19) : std::wstring_view{};
}
} // namespace detail

// True for a name the browser may open: QuickSift's own session-log pattern and a
// bare file name (no folder part), so a request can never point outside Logs.
[[nodiscard]] constexpr bool IsBrowsableName(std::wstring_view name) noexcept {
    for (const wchar_t c : name) if (c == L'\\' || c == L'/' || c == L':') return false;
    return log_retention::Classify(name) == log_retention::Kind::SessionLog;
}

// Own session logs only, plain files only, never a reparse point, never the
// current session's log (the diagnostic window already shows it). Newest first by
// the start time in the name, then by name; at most kMaxListed entries.
[[nodiscard]] inline std::vector<Listed> Select(const std::vector<Candidate>& candidates,
    std::wstring_view currentSessionName) {
    std::vector<Listed> listed;
    for (const Candidate& candidate : candidates) {
        if (!IsBrowsableName(candidate.name) || candidate.reparsePoint || !candidate.regularFile) continue;
        if (!currentSessionName.empty() && candidate.name.size() == currentSessionName.size() &&
            log_retention::detail::EndsWithNoCase(candidate.name, currentSessionName)) continue;
        listed.push_back({ candidate.name, candidate.bytes });
    }
    std::sort(listed.begin(), listed.end(), [](const Listed& left, const Listed& right) {
        const std::wstring_view a = detail::Stamp(left.name);
        const std::wstring_view b = detail::Stamp(right.name);
        if (a != b) return a > b;
        return left.name > right.name;
    });
    if (listed.size() > kMaxListed) listed.resize(kMaxListed);
    return listed;
}

// "1.2 MB", "340 KB", "512 B". Units are the same in English and Vietnamese.
[[nodiscard]] inline std::wstring SizeText(std::uint64_t bytes) {
    wchar_t buffer[32]{};
    if (bytes < 1024ull) std::swprintf(buffer, std::size(buffer), L"%llu B", static_cast<unsigned long long>(bytes));
    else if (bytes < 1024ull * 1024ull)
        std::swprintf(buffer, std::size(buffer), L"%llu KB", static_cast<unsigned long long>((bytes + 512ull) / 1024ull));
    else std::swprintf(buffer, std::size(buffer), L"%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buffer;
}

// Menu label: session start time from the name (local time, as written) and size,
// for example "2026-10-18 14:32:05 · 1.2 MB".
[[nodiscard]] inline std::wstring Label(std::wstring_view name, std::uint64_t bytes) {
    const std::wstring_view stamp = detail::Stamp(name); // YYYYMMDD-HHMMSS-mmm
    std::wstring label;
    if (stamp.size() == 19) {
        label.append(stamp.substr(0, 4)).append(L"-").append(stamp.substr(4, 2)).append(L"-")
            .append(stamp.substr(6, 2)).append(L" ").append(stamp.substr(9, 2)).append(L":")
            .append(stamp.substr(11, 2)).append(L":").append(stamp.substr(13, 2));
    } else {
        label.assign(name);
    }
    label.append(L" \u00B7 ").append(SizeText(bytes));
    return label;
}

// Which bytes of a `size`-byte file are read: the newest kMaxDisplayBytes.
struct TailPlan {
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    bool truncated = false;
};

[[nodiscard]] constexpr TailPlan PlanTail(std::uint64_t size, std::uint64_t cap = kMaxDisplayBytes) noexcept {
    if (size <= cap) return { 0, size, false };
    return { size - cap, cap, true };
}

// Where the shown text starts inside the bytes read. A whole file skips its UTF-8
// BOM; a truncated tail starts after the first line break, so it never begins in
// the middle of a line or of a UTF-8 sequence.
[[nodiscard]] constexpr std::size_t FirstShownByte(std::string_view bytes, bool truncated) noexcept {
    if (!truncated) {
        return bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
            static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF ? 3 : 0;
    }
    const std::size_t newline = bytes.find('\n');
    return newline == std::string_view::npos ? 0 : newline + 1;
}

} // namespace quicksift::core::session_logs
