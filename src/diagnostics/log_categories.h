// OWNER: Diagnostic category names and privacy-preserving path fields. Header-only and
// portable (no windows.h) so portable checks can include it.
//
// Path privacy policy (CODE_GUIDE.md 5.6): structured events carry `file=` (the file
// name only) and `path_id=` (a stable 64-bit FNV-1a hash of the case-folded full path,
// for correlating events about one file) — never the full path, unless an operator
// enabled verbose logging and the full path is required to reproduce an issue.
#pragma once

#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace quicksift::diagnostics::category {

// Existing categories (kept for continuity of log tooling).
inline constexpr std::wstring_view kMetadata = L"Metadata";
inline constexpr std::wstring_view kWorker = L"Worker";
inline constexpr std::wstring_view kUi = L"UI";
inline constexpr std::wstring_view kLifecycle = L"Lifecycle";
inline constexpr std::wstring_view kCatalog = L"Catalog";
// 0.9.4.0 polishing pass.
inline constexpr std::wstring_view kSearch = L"Search";
inline constexpr std::wstring_view kMetadataStore = L"MetadataStore";
inline constexpr std::wstring_view kBatch = L"Batch";
inline constexpr std::wstring_view kRatingsIo = L"RatingsIO";
inline constexpr std::wstring_view kFaceLock = L"FaceLock";
inline constexpr std::wstring_view kViewLane = L"ViewLane";
inline constexpr std::wstring_view kThumbReliability = L"ThumbReliability";
inline constexpr std::wstring_view kScheduler = L"Scheduler";
inline constexpr std::wstring_view kWriteLane = L"WriteLane";
inline constexpr std::wstring_view kDbMigration = L"DbMigration";

} // namespace quicksift::diagnostics::category

namespace quicksift::diagnostics {

[[nodiscard]] inline std::wstring LogFileName(const std::filesystem::path& path) {
    return path.filename().wstring();
}

[[nodiscard]] inline std::wstring LogPathId(const std::filesystem::path& path) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const wchar_t ch : path.native()) {
        const auto folded = static_cast<std::uint64_t>(std::towlower(static_cast<wint_t>(ch)));
        hash ^= folded;
        hash *= 1099511628211ull;
    }
    constexpr wchar_t kHex[] = L"0123456789abcdef";
    std::wstring text(16, L'0');
    for (int index = 15; index >= 0; --index) {
        text[static_cast<std::size_t>(index)] = kHex[hash & 0xF];
        hash >>= 4;
    }
    return text;
}

} // namespace quicksift::diagnostics
