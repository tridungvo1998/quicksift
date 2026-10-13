// OWNER: Deterministic, portable checks for the Import / Export Ratings file format.
//
// Not a CTest suite (CTest stays retired): one self-contained executable with no
// timing, filesystem, UI, or platform dependencies. Build and run from the source
// root on any host with a C++20 compiler, e.g.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Isrc scripts/checks/ratings_exchange_checks.cpp
//       src/core/ratings_exchange.cpp -o /tmp/ratings_exchange_checks
//   /tmp/ratings_exchange_checks
//
//   cl /std:c++20 /EHsc /W4 /utf-8 /Isrc scripts\checks\ratings_exchange_checks.cpp
//       src\core\ratings_exchange.cpp
//
// Covers: exact writer layout, round trip (incl. non-ASCII names), canonical tokens,
// corrupted header/footer/separators/labels, bad data lines, duplicate names,
// trailing content, declared-count mismatch, total-count mismatch and missing-file
// reconciliation, verbatim missing-line text, BOM/LF/blank-line tolerance.

#include "core/ratings_exchange.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace quicksift::core::ratings_exchange;

int gFailures = 0;
int gChecks = 0;

void Check(bool condition, const char* what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL: %s\n", what);
    }
}

ExportDocument SampleDocument() {
    ExportDocument document;
    document.version = "0.9.4.0";
    document.time = { 2026, 10, 9, 7, 5, 3 };
    document.totalFiles = 6;
    document.entries.push_back({ L"IMG_0001.JPG", 5, 1, 1, {}, 0 });
    document.entries.push_back({ L"IMG_0002.CR3", 0, 0, 0, {}, 0 });   // neutral: skipped
    document.entries.push_back({ L"IMG_0003.jpg", 0, -1, 0, {}, 0 });
    document.entries.push_back({ L"\u1EA2nh c\u01B0\u1EDBi.HEIC", 3, 0, 5, {}, 0 }); // Vietnamese
    document.entries.push_back({ L"photo with spaces.png", 0, 0, 2, {}, 0 });
    return document;
}

std::string Replace(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    if (at != std::string::npos) text.replace(at, from.size(), to);
    return text;
}

void CheckWriterLayout() {
    const std::string text = WriteDocumentUtf8(SampleDocument());
    const std::string expected =
        "Header: QuickSift version 0.9.4.0 - Ratings/Pick state / Color tag data\r\n"
        "Exported time: 07:05:03 09 Oct 2026\r\n"
        "Total files count: 6 files total\r\n"
        "Files that has been rated, picked or rejected, or color tagged: 4 files\r\n"
        "\xE2\x80\x94\r\n"
        "Exported data:\r\n"
        "IMG_0001.JPG | 5 | Picked | Red\r\n"
        "IMG_0003.jpg | 0 | Rejected | None\r\n"
        "\xE1\xBA\xA2nh c\xC6\xB0\xE1\xBB\x9Bi.HEIC | 3 | None | Purple\r\n"
        "photo with spaces.png | 0 | None | Yellow\r\n"
        "\xE2\x80\x94\r\n"
        "Footer: End of QuickSift exported data.\r\n";
    Check(text == expected, "writer emits the exact documented layout (CRLF, em-dash, tokens)");
    Check(text.rfind("\xEF\xBB\xBF", 0) != 0, "writer emits no BOM");
    Check(DefaultExportFileName({ 2026, 10, 9, 7, 5, 3 }) == L"QuickSift-Ratings-20261009-070503.txt",
        "default export file name QuickSift-Ratings-YYYYMMDD-HHMMSS.txt");
    Check(FormatExportTime({ 2026, 1, 2, 23, 59, 58 }) == "23:59:58 02 Jan 2026", "time format HH:MM:SS DD mmm YYYY");
}

void CheckRoundTrip() {
    const ExportDocument original = SampleDocument();
    const ParseResult parsed = ParseDocumentUtf8(WriteDocumentUtf8(original));
    Check(parsed.ok, "round trip parses");
    Check(parsed.document.version == "0.9.4.0", "round trip keeps version");
    Check(parsed.document.totalFiles == 6, "round trip keeps total count");
    Check(parsed.document.time.hour == 7 && parsed.document.time.month == 10 &&
        parsed.document.time.year == 2026, "round trip keeps time");
    Check(parsed.document.entries.size() == 4, "round trip keeps only data rows");
    if (parsed.document.entries.size() == 4) {
        const RatingsEntry& first = parsed.document.entries[0];
        Check(first.fileName == L"IMG_0001.JPG" && first.starRating == 5 && first.pickState == 1 &&
            first.colorLabel == 1, "round trip entry 1 values");
        Check(parsed.document.entries[1].pickState == -1, "round trip Rejected");
        Check(parsed.document.entries[2].fileName == L"\u1EA2nh c\u01B0\u1EDBi.HEIC" &&
            parsed.document.entries[2].colorLabel == 5, "round trip non-ASCII name and Purple");
        Check(parsed.document.entries[3].fileName == L"photo with spaces.png", "inner spaces preserved");
        Check(first.sourceLine == L"IMG_0001.JPG | 5 | Picked | Red", "source line kept verbatim");
        Check(first.lineNumber == 7, "source line number recorded");
    }
    // Re-writing the parsed document reproduces the same bytes.
    ExportDocument again = parsed.document;
    Check(WriteDocumentUtf8(again) == WriteDocumentUtf8(original), "write(parse(write(x))) == write(x)");
}

void CheckTokens() {
    for (int rating = 0; rating <= 5; ++rating)
        Check(ParseStarToken(StarToken(rating)) == rating, "star token round trip");
    for (int pick = -1; pick <= 1; ++pick)
        Check(ParsePickToken(PickToken(pick)) == pick, "pick token round trip");
    for (int color = 0; color <= 5; ++color)
        Check(ParseColorToken(ColorToken(color)) == color, "color token round trip");
    Check(!ParseStarToken("6") && !ParseStarToken("-1") && !ParseStarToken("05") && !ParseStarToken(""),
        "star token rejects out-of-range/padded values");
    Check(!ParsePickToken("picked") && !ParsePickToken("Pick") && !ParsePickToken(""),
        "pick token is case-sensitive canonical");
    Check(!ParseColorToken("Orange") && !ParseColorToken("red"), "color token canonical only");
}

void ExpectFailure(const std::string& text, ParseError error, const char* what) {
    const ParseResult result = ParseDocumentUtf8(text);
    Check(!result.ok && result.error == error, what);
    if (result.ok || result.error != error) {
        std::printf("      got ok=%d error=%s line=%zu\n", result.ok ? 1 : 0,
            ParseErrorName(result.error), result.lineNumber);
    }
}

void CheckCorruption() {
    const std::string good = WriteDocumentUtf8(SampleDocument());
    ExpectFailure("", ParseError::Empty, "empty file rejected");
    ExpectFailure("\r\n\r\n", ParseError::Empty, "blank-only file rejected");
    ExpectFailure(Replace(good, "Header: QuickSift version", "Header: QuickSlft version"),
        ParseError::BadHeader, "corrupted header prefix rejected");
    ExpectFailure(Replace(good, " - Ratings/Pick state / Color tag data", " - Ratings"),
        ParseError::BadHeader, "corrupted header suffix rejected");
    ExpectFailure(Replace(good, "0.9.4.0", "0.9.x.0"), ParseError::BadHeader, "bad version rejected");
    ExpectFailure(Replace(good, "09 Oct 2026", "31 Feb 2026"), ParseError::BadExportTime,
        "impossible date rejected");
    ExpectFailure(Replace(good, "09 Oct 2026", "09 oct 2026"), ParseError::BadExportTime,
        "lower-case month rejected");
    ExpectFailure(Replace(good, "6 files total", "six files total"), ParseError::BadTotalCount,
        "non-numeric total rejected");
    ExpectFailure(Replace(good, "color tagged: 4 files", "color tagged: 4"), ParseError::BadDataCount,
        "data count without suffix rejected");
    ExpectFailure(Replace(good, "\xE2\x80\x94\r\nExported data:", "-\r\nExported data:"),
        ParseError::MissingFirstSeparator, "first separator must be an em-dash");
    ExpectFailure(Replace(good, "Exported data:", "Exported data"), ParseError::MissingDataLabel,
        "\"Exported data:\" label required");
    ExpectFailure(Replace(good, "\xE2\x80\x94\r\nFooter", "Footer"), ParseError::BadDataLine,
        "missing second separator makes the footer a bad data line");
    ExpectFailure(Replace(good, "Footer: End of QuickSift exported data.\r\n", ""),
        ParseError::MissingFooter, "missing footer (truncated file) rejected");
    ExpectFailure(Replace(good, "Footer: End of QuickSift exported data.", "Footer: End of data."),
        ParseError::MissingFooter, "corrupted footer rejected");
    ExpectFailure(good + "extra\r\n", ParseError::TrailingContent, "content after footer rejected");
    ExpectFailure(Replace(good, "| 5 | Picked | Red", "| 5 | Picked"), ParseError::BadDataLine,
        "three-field data line rejected");
    ExpectFailure(Replace(good, "| 5 | Picked | Red", "| 5 | Picked | Red | x"), ParseError::BadDataLine,
        "five-field data line rejected");
    ExpectFailure(Replace(good, "| 5 | Picked | Red", "| 7 | Picked | Red"), ParseError::BadDataLine,
        "invalid star token rejected");
    ExpectFailure(Replace(good, "| 5 | Picked | Red", "| 5 | Maybe | Red"), ParseError::BadDataLine,
        "invalid pick token rejected");
    ExpectFailure(Replace(good, "| 5 | Picked | Red", "| 5 | Picked | Orange"), ParseError::BadDataLine,
        "invalid color token rejected");
    ExpectFailure(Replace(good, "IMG_0001.JPG |", " |"), ParseError::BadDataLine, "empty name rejected");
    ExpectFailure(Replace(good, "IMG_0001.JPG |", "sub\\IMG_0001.JPG |"), ParseError::BadDataLine,
        "path separators in name rejected");
    ExpectFailure(Replace(good, "IMG_0003.jpg |", "img_0001.jpg |"), ParseError::DuplicateEntry,
        "case-insensitive duplicate name rejected");
    ExpectFailure(Replace(good, "color tagged: 4 files", "color tagged: 5 files"),
        ParseError::DataCountMismatch, "declared data count must equal data lines");
    std::string badUtf8 = good;
    badUtf8.insert(badUtf8.find("IMG_0001"), "\xC3\x28");
    ExpectFailure(badUtf8, ParseError::InvalidUtf8, "invalid UTF-8 rejected");
    const ParseResult failure = ParseDocumentUtf8(Replace(good, "| 5 | Picked | Red", "| 5 | Maybe | Red"));
    Check(failure.lineNumber == 7 && failure.offendingLine == L"IMG_0001.JPG | 5 | Maybe | Red",
        "failure reports line number and offending line");
    Check(failure.document.entries.empty(), "failed parse returns no partial data");
}

void CheckTolerance() {
    std::string text = WriteDocumentUtf8(SampleDocument());
    // LF only, BOM, blank lines, trailing spaces, optional column-header row.
    std::string lf;
    for (const char c : text) if (c != '\r') lf.push_back(c);
    lf = Replace(lf, "Exported data:\n", "Exported data:\n\nFILENAME.EXTENSION | Star Rating | Pick State | Color Tag\n");
    lf = Replace(lf, "| Red\n", "|   Red   \n");
    lf = "\xEF\xBB\xBF" + lf + "\n\n";
    const ParseResult parsed = ParseDocumentUtf8(lf);
    Check(parsed.ok, "BOM + LF + blank lines + padding + column header accepted");
    Check(parsed.ok && parsed.document.entries.size() == 4 && parsed.document.entries[0].colorLabel == 1,
        "tolerant parse keeps values");
}

void CheckReconciliation() {
    const ParseResult parsed = ParseDocumentUtf8(WriteDocumentUtf8(SampleDocument()));
    std::vector<FolderFile> folder{
        { L"img_0001.jpg", 10 },          // case-insensitive match
        { L"IMG_0002.CR3", 11 },
        { L"\u1EA2nh c\u01B0\u1EDBi.HEIC", 12 },
        { L"unrelated.tif", 13 },
    };
    const ImportReconciliation reconciliation = ReconcileWithFolder(parsed.document, folder);
    Check(reconciliation.expectedFiles == 6 && reconciliation.presentFiles == 4 &&
        reconciliation.CountMismatch(), "total-count mismatch detected (expected 6, 4 present)");
    Check(reconciliation.matches.size() == 2, "two entries matched");
    Check(reconciliation.matches.size() == 2 && reconciliation.matches[0].catalogIndex == 10,
        "case-insensitive FILENAME.EXTENSION match");
    Check(reconciliation.missingEntries.size() == 2 && reconciliation.HasMissing(), "two entries missing");
    Check(MissingLinesText(parsed.document, reconciliation) ==
        L"IMG_0003.jpg | 0 | Rejected | None\r\nphoto with spaces.png | 0 | None | Yellow",
        "missing list lines are exactly as in the file");

    std::vector<FolderFile> exact{
        { L"IMG_0001.JPG", 0 }, { L"IMG_0002.CR3", 1 }, { L"IMG_0003.jpg", 2 },
        { L"\u1EA2nh c\u01B0\u1EDBi.HEIC", 3 }, { L"photo with spaces.png", 4 }, { L"x.jpg", 5 } };
    const ImportReconciliation clean = ReconcileWithFolder(parsed.document, exact);
    Check(!clean.CountMismatch() && !clean.HasMissing() && clean.matches.size() == 4,
        "matching folder: no mismatch, nothing missing");

    // A locale-aware fold may merge names the ASCII parser kept apart.
    ExportDocument merged;
    merged.version = "0.9.4.0";
    merged.totalFiles = 1;
    merged.entries.push_back({ L"\u00E9t\u00E9.jpg", 1, 0, 0, {}, 0 });
    merged.entries.push_back({ L"\u00C9T\u00C9.jpg", 2, 0, 0, {}, 0 });
    const auto foldAll = [](std::wstring_view name) {
        std::wstring folded = AsciiFold(name);
        for (wchar_t& c : folded) if (c == L'\u00E9') c = L'\u00C9';
        return folded;
    };
    const ImportReconciliation dup = ReconcileWithFolder(merged, { { L"\u00C9t\u00E9.JPG", 7 } }, foldAll);
    Check(dup.matches.size() == 1 && dup.duplicateEntries.size() == 1,
        "names that fold onto one file are reported as duplicates");
}

} // namespace

int main() {
    CheckWriterLayout();
    CheckRoundTrip();
    CheckTokens();
    CheckCorruption();
    CheckTolerance();
    CheckReconciliation();
    std::printf("ratings_exchange_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
