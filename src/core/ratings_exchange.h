// OWNER: Portable QuickSift ratings exchange format (Import / Export Ratings):
// canonical tokens, UTF-8 writer, strict validating parser, and folder reconciliation.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quicksift::core::ratings_exchange {

// ---- Exact format literals (single source of truth; see USER_GUIDE / notes) -------
inline constexpr std::string_view kHeaderPrefix = "Header: QuickSift version ";
inline constexpr std::string_view kHeaderSuffix = " - Ratings/Pick state / Color tag data";
inline constexpr std::string_view kExportedTimePrefix = "Exported time: ";
inline constexpr std::string_view kTotalCountPrefix = "Total files count: ";
inline constexpr std::string_view kTotalCountSuffix = " files total";
inline constexpr std::string_view kDataCountPrefix =
    "Files that has been rated, picked or rejected, or color tagged: ";
inline constexpr std::string_view kDataCountSuffix = " files";
inline constexpr std::string_view kSeparator = "\xE2\x80\x94"; // U+2014 EM DASH, alone on its line
inline constexpr std::string_view kDataLabel = "Exported data:";
inline constexpr std::string_view kColumnHeader =
    "FILENAME.EXTENSION | Star Rating | Pick State | Color Tag";
inline constexpr std::string_view kFooter = "Footer: End of QuickSift exported data.";
inline constexpr std::string_view kLineBreak = "\r\n";

// App model values (match PhotoItem / MetadataFieldId semantics):
//   star rating 0..5, pick state -1 (Rejected) / 0 (None) / 1 (Picked),
//   color label 0 None, 1 Red, 2 Yellow, 3 Green, 4 Blue, 5 Purple.
struct RatingsEntry {
    std::wstring fileName;   // FILENAME.EXTENSION exactly as written (no folder)
    int starRating = 0;
    int pickState = 0;
    int colorLabel = 0;
    std::wstring sourceLine; // the complete data line exactly as read (parser only)
    std::size_t lineNumber = 0; // 1-based line in the source file (parser only)
};

struct ExportTime {
    int year = 1970;
    int month = 1;  // 1..12
    int day = 1;    // 1..31
    int hour = 0;   // 0..23
    int minute = 0; // 0..59
    int second = 0; // 0..59
};

struct ExportDocument {
    std::string version;         // e.g. "0.9.4.0" (digits and dots)
    ExportTime time{};
    std::size_t totalFiles = 0;  // "Total files count"
    std::vector<RatingsEntry> entries; // only files that carry data
};

// True when the entry carries any data worth exporting (rated, picked/rejected, or
// color tagged). Neutral entries (0 / None / None) are never written.
[[nodiscard]] bool HasData(const RatingsEntry& entry) noexcept;
[[nodiscard]] bool IsValidStarRating(int value) noexcept;
[[nodiscard]] bool IsValidPickState(int value) noexcept;
[[nodiscard]] bool IsValidColorLabel(int value) noexcept;

// Canonical tokens. Writers emit exactly these; the parser accepts exactly these
// (case-sensitive) after trimming surrounding spaces/tabs.
[[nodiscard]] std::string StarToken(int rating);
[[nodiscard]] std::string_view PickToken(int pickState) noexcept;
[[nodiscard]] std::string_view ColorToken(int colorLabel) noexcept;
[[nodiscard]] std::optional<int> ParseStarToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<int> ParsePickToken(std::string_view token) noexcept;
[[nodiscard]] std::optional<int> ParseColorToken(std::string_view token) noexcept;

// "HH:MM:SS DD mmm YYYY" with a 3-letter English month (Jan..Dec).
[[nodiscard]] std::string FormatExportTime(const ExportTime& time);
[[nodiscard]] std::optional<ExportTime> ParseExportTime(std::string_view text) noexcept;
[[nodiscard]] bool IsValidExportTime(const ExportTime& time) noexcept;
// QuickSift-Ratings-YYYYMMDD-HHMMSS.txt
[[nodiscard]] std::wstring DefaultExportFileName(const ExportTime& time);

// Strict UTF-8 <-> wide conversion (no overlongs, no surrogates, max U+10FFFF).
// wchar_t may be UTF-16 (Windows) or UTF-32 (other hosts); both are handled.
[[nodiscard]] std::string WideToUtf8(std::wstring_view text);
[[nodiscard]] std::optional<std::wstring> Utf8ToWide(std::string_view text);

// Serializes the document exactly as documented, CRLF line endings, no BOM.
// Entries without data are skipped; the data count line always equals the number
// of data lines written. Returns UTF-8 bytes.
[[nodiscard]] std::string WriteDocumentUtf8(const ExportDocument& document);

enum class ParseError {
    None,
    Empty,
    InvalidUtf8,
    BadHeader,
    BadExportTime,
    BadTotalCount,
    BadDataCount,
    MissingFirstSeparator,
    MissingDataLabel,
    BadDataLine,
    DuplicateEntry,
    MissingSecondSeparator,
    MissingFooter,
    TrailingContent,
    DataCountMismatch,
};

struct ParseResult {
    bool ok = false;
    ParseError error = ParseError::None;
    std::size_t lineNumber = 0;   // 1-based line where validation failed (0 = n/a)
    std::wstring offendingLine;   // the failing line as read (may be empty)
    ExportDocument document;
};

// Validates and parses an exported file. Any deviation from the documented layout
// (header prefix/suffix, time, counts, both em-dash separators, "Exported data:",
// 4-field data lines with canonical tokens, unique file names, footer, nothing but
// blank lines after the footer, data count == data lines) fails the whole parse:
// a partial or edited file is treated as corrupt and nothing is imported.
// Blank lines are ignored; a UTF-8 BOM and LF or CRLF line endings are accepted.
// An optional literal column-header row (kColumnHeader) right after
// "Exported data:" is accepted and skipped.
[[nodiscard]] ParseResult ParseDocumentUtf8(std::string_view bytes);
[[nodiscard]] const char* ParseErrorName(ParseError error) noexcept;

// Case-folding key used to match FILENAME.EXTENSION against folder files. The
// portable default folds ASCII only; the Windows app supplies an invariant-locale
// upper-case fold that mirrors NTFS case-insensitive name matching.
using NameFold = std::function<std::wstring(std::wstring_view)>;
[[nodiscard]] std::wstring AsciiFold(std::wstring_view name);

struct FolderFile {
    std::wstring fileName;
    std::size_t catalogIndex = 0;
};

struct ImportMatch {
    std::size_t entryIndex = 0;   // into ExportDocument::entries
    std::size_t catalogIndex = 0; // FolderFile::catalogIndex
};

struct ImportReconciliation {
    std::vector<ImportMatch> matches;
    std::vector<std::size_t> missingEntries; // indices into ExportDocument::entries
    // Lines whose name folds onto a file another line already matched (the app's
    // locale-aware fold can merge names the portable ASCII parser kept apart).
    // The app treats any duplicate as a corrupt file.
    std::vector<std::size_t> duplicateEntries;
    std::size_t expectedFiles = 0;           // document.totalFiles
    std::size_t presentFiles = 0;            // folder file count
    [[nodiscard]] bool CountMismatch() const noexcept { return expectedFiles != presentFiles; }
    [[nodiscard]] bool HasMissing() const noexcept { return !missingEntries.empty(); }
};

[[nodiscard]] ImportReconciliation ReconcileWithFolder(const ExportDocument& document,
    const std::vector<FolderFile>& folderFiles, const NameFold& fold = AsciiFold);

// The missing entries' data lines exactly as they appeared in the file, CRLF-joined.
[[nodiscard]] std::wstring MissingLinesText(const ExportDocument& document,
    const ImportReconciliation& reconciliation);

} // namespace quicksift::core::ratings_exchange
