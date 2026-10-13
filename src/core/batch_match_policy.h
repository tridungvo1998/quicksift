// OWNER: Portable Batch Processing policy: name-list parsing, de-duplication,
// extension parsing/validation, complete and partial (numeral-first fuzzy) file
// matching, per-extension counts, and the confirmation sentence.
// No Windows, UI, filesystem, or timing dependencies; checked by
// scripts/checks/batch_match_checks.cpp.
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quicksift::core::batch_match {

// Case-folding key for names. The portable default folds ASCII letters only; the
// Windows app supplies its invariant-locale file-name fold (the same one Ratings
// Import uses), so "É" and "é" also compare equal there.
using NameFold = std::function<std::wstring(std::wstring_view)>;
[[nodiscard]] std::wstring AsciiFold(std::wstring_view text);

// ---- Separators ------------------------------------------------------------------
// Name list: space, tab, comma, semicolon, CR, LF. Extension list: the same set.
[[nodiscard]] bool IsListSeparator(wchar_t ch) noexcept;

// Splits a list into raw tokens. A token that starts with '"' extends to the next
// '"' and may contain separators (e.g. "My Photo 01"); the quotes are removed. An
// unterminated opening quote is dropped and the token ends at the next separator.
// Empty tokens are discarded.
[[nodiscard]] std::vector<std::wstring> SplitList(std::wstring_view text);

// ---- Extensions --------------------------------------------------------------------
// Normalizes one extension token: optional leading "*" and "." are removed, ASCII
// letters are lower-cased, and a leading dot is added (".jpg"). Returns empty for a
// token that is not a plausible extension (empty, non-ASCII, contains '.', '/', '\').
[[nodiscard]] std::wstring NormalizeExtensionToken(std::wstring_view token);
// True when normalized (lower-case, leading dot) is a QuickSift image extension.
[[nodiscard]] bool IsSupportedImageExtension(std::wstring_view normalized) noexcept;

struct ExtensionList {
    std::vector<std::wstring> extensions; // normalized, de-duplicated, first-seen order
    std::vector<std::wstring> unknown;    // tokens as typed (trimmed), de-duplicated
    bool allSupported = false;            // no tokens at all -> every supported type
    [[nodiscard]] bool Valid() const noexcept { return unknown.empty(); }
};
// Empty or separator-only input means "all supported image extensions".
[[nodiscard]] ExtensionList ParseExtensionList(std::wstring_view text);
// The effective target set: list.extensions, or every supported extension.
[[nodiscard]] std::vector<std::wstring> TargetExtensions(const ExtensionList& list);

// ---- Names ---------------------------------------------------------------------------
struct NameEntry {
    std::wstring text; // entry as used for matching (directory and image extension removed)
    std::wstring key;  // fold(text): de-duplication and complete-match key
};

// One raw token -> entry text: surrounding whitespace trimmed, any directory prefix
// (up to the last '\\' or '/') removed, then ONE trailing extension removed if it is a
// supported image extension ("IMG_1.JPG" -> "IMG_1"; "IMG_1.xmp" stays as typed).
[[nodiscard]] std::wstring EntryTextFromToken(std::wstring_view token);

// Parses the whole list. Repeated entries (same fold key) count once; the first
// occurrence is kept, in input order.
[[nodiscard]] std::vector<NameEntry> ParseNameList(std::wstring_view text,
    const NameFold& fold = AsciiFold);

// File name minus its last extension ("a.b.jpg" -> "a.b"; ".hidden" -> ".hidden").
[[nodiscard]] std::wstring StemOf(std::wstring_view fileName);

// "Load names from folder": appends the stems of fileNames to existingText, one per
// line, skipping any stem whose entry key is already present in existingText or was
// appended earlier. Stems that contain a separator are written in double quotes so
// they stay one entry. Existing text is kept verbatim (trailing blank space trimmed).
struct MergeResult {
    std::wstring text;
    std::size_t added = 0;
    std::size_t duplicates = 0;
};
[[nodiscard]] MergeResult MergeNamesIntoText(std::wstring_view existingText,
    const std::vector<std::wstring>& fileNames, const NameFold& fold = AsciiFold);

// ---- Matching -------------------------------------------------------------------------
enum class MatchMethod { Partial, Complete };

struct CandidateFile {
    std::size_t id = 0;       // caller's identifier (catalog index)
    std::wstring stem;        // file name without its extension
    std::wstring extension;   // lower case with leading dot (".jpg")
};

// Run tokenization used by partial matching (applied to folded text).
// Digit run: maximal run of ASCII '0'..'9'. Letter run: maximal run of characters
// that are neither ASCII digits nor delimiters. Delimiters: every ASCII character
// that is not a letter or digit (space, punctuation such as _ - . ( ) [ ], controls).
// Non-ASCII characters count as letters.
struct Runs {
    std::vector<std::wstring> digits;  // leading zeros removed ("0123" -> "123", "000" -> "0")
    std::vector<std::wstring> letters; // folded text
    [[nodiscard]] bool Empty() const noexcept { return digits.empty() && letters.empty(); }
};
[[nodiscard]] Runs Tokenize(std::wstring_view foldedText);

// Optimal-string-alignment (Damerau-Levenshtein with adjacent transpositions)
// distance between pattern and the best-matching substring of text.
[[nodiscard]] std::size_t ApproximateSubstringDistance(std::wstring_view pattern,
    std::wstring_view text);

// Letter runs this long or longer tolerate one edit (insertion, deletion,
// substitution, or adjacent transposition); shorter runs must occur exactly.
inline constexpr std::size_t kFuzzyLetterMinLength = 4;
inline constexpr std::size_t kFuzzyLetterMaxEdits = 1;

// Partial rule for one entry and one stem (both already folded):
//  1. every digit run of the entry equals (leading-zero-insensitive) a WHOLE digit
//     run of the stem, in the same order, each stem run used at most once;
//  2. every letter run of the entry occurs in some letter run of the stem: exactly
//     (case-insensitive substring) when shorter than kFuzzyLetterMinLength, else
//     within kFuzzyLetterMaxEdits edits;
//  3. an entry without runs (only punctuation) matches nothing.
[[nodiscard]] bool PartialMatches(const Runs& entry, const Runs& stem);

struct ExtensionCount {
    std::wstring extension; // upper case without dot ("JPG")
    std::size_t count = 0;
};

struct MatchResult {
    std::vector<std::size_t> matchedIds;          // CandidateFile::id, input order, each once
    std::vector<ExtensionCount> extensionCounts;  // nonzero only; count desc, then name asc
    std::vector<std::size_t> unmatchedEntries;    // indices into entries, input order
    bool cancelled = false;
    [[nodiscard]] std::size_t Total() const noexcept { return matchedIds.size(); }
};

// Matches entries against the files whose extension is in targetExtensions
// (normalized). Complete: fold(stem) == entry key. Partial: PartialMatches.
// Every file is counted once however many entries match it. cancel (optional) is
// polled; a cancelled result is marked and must be discarded.
[[nodiscard]] MatchResult MatchFiles(const std::vector<NameEntry>& entries,
    const std::vector<CandidateFile>& files, const std::vector<std::wstring>& targetExtensions,
    MatchMethod method, const NameFold& fold = AsciiFold,
    const std::atomic<bool>* cancel = nullptr);

// ---- Confirmation text ------------------------------------------------------------------
inline constexpr std::wstring_view kConfirmationTemplate =
    L"Batch operations: {count} files ({breakdown}) will be processed. Confirm? This cannot be undone.";
inline constexpr std::wstring_view kConfirmationTemplateSingular =
    L"Batch operations: {count} file ({breakdown}) will be processed. Confirm? This cannot be undone.";
// "30 JPG, 7 ARW"
[[nodiscard]] std::wstring FormatExtensionBreakdown(const std::vector<ExtensionCount>& counts);
// Substitutes {count} and {breakdown}; singularTemplate is used when total == 1.
[[nodiscard]] std::wstring FormatConfirmation(std::wstring_view pluralTemplate,
    std::wstring_view singularTemplate, std::size_t total,
    const std::vector<ExtensionCount>& counts);

// Unmatched entries, one per line (CRLF-joined), as entered after normalization.
[[nodiscard]] std::wstring UnmatchedEntriesText(const std::vector<NameEntry>& entries,
    const std::vector<std::size_t>& unmatched);

} // namespace quicksift::core::batch_match
