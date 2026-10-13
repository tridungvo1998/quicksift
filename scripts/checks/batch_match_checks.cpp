// OWNER: Deterministic, portable checks for the Batch Processing match policy.
//
// Not a CTest suite (CTest stays retired): one self-contained executable with no
// timing, filesystem, UI, or platform dependencies. Build and run from the source
// root on any host with a C++20 compiler, e.g.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Isrc scripts/checks/batch_match_checks.cpp
//       src/core/batch_match_policy.cpp src/core/image_formats.cpp
//       src/core/catalog_policy.cpp src/core/photo_predicate.cpp -o /tmp/batch_match_checks
//   /tmp/batch_match_checks
//
//   cl /std:c++20 /EHsc /W4 /utf-8 /Isrc scripts\checks\batch_match_checks.cpp
//       src\core\batch_match_policy.cpp src\core\image_formats.cpp src\core\catalog_policy.cpp
//       src\core\photo_predicate.cpp
//
// Covers: separators and quoting, de-duplication (typed and folder-loaded),
// extension parsing/validation and the empty = all-supported rule, entry extension
// stripping, complete vs partial matching, numeral priority (leading zeros, whole
// runs, order, no 123 vs 1234 false matches), letter fuzziness, one count per file,
// per-extension counts and ordering, the confirmation sentence, unmatched entries,
// cancellation, and the extension registry vs IsFormatAllowed.

#include "core/batch_match_policy.h"
#include "core/catalog_policy.h"
#include "core/image_formats.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace quicksift::core::batch_match;

int gFailures = 0;
int gChecks = 0;

void Check(bool condition, const char* what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL: %s\n", what);
    }
}

std::vector<std::wstring> Texts(const std::vector<NameEntry>& entries) {
    std::vector<std::wstring> texts;
    for (const auto& entry : entries) texts.push_back(entry.text);
    return texts;
}

std::vector<CandidateFile> Files(const std::vector<std::wstring>& names) {
    std::vector<CandidateFile> files;
    for (std::size_t index = 0; index < names.size(); ++index) {
        CandidateFile file;
        file.id = index;
        file.stem = StemOf(names[index]);
        const std::size_t dot = names[index].rfind(L'.');
        file.extension = dot == std::wstring::npos ? std::wstring() :
            NormalizeExtensionToken(names[index].substr(dot));
        files.push_back(file);
    }
    return files;
}

std::vector<std::size_t> Match(const std::wstring& list, const std::vector<std::wstring>& names,
    MatchMethod method, const std::wstring& extensions = L"") {
    const auto entries = ParseNameList(list);
    const auto result = MatchFiles(entries, Files(names),
        TargetExtensions(ParseExtensionList(extensions)), method);
    return result.matchedIds;
}

bool PartialOne(const std::wstring& entry, const std::wstring& stem) {
    return PartialMatches(Tokenize(AsciiFold(EntryTextFromToken(entry))), Tokenize(AsciiFold(stem)));
}

void CheckSeparators() {
    Check(Texts(ParseNameList(L"a b,c;d\ne\r\nf\tg")) ==
        std::vector<std::wstring>{ L"a", L"b", L"c", L"d", L"e", L"f", L"g" },
        "space, comma, semicolon, LF, CRLF and tab all separate entries");
    Check(Texts(ParseNameList(L"  ,, ;\r\n\t ")).empty(), "separator-only input has no entries");
    Check(Texts(ParseNameList(L"\"My Photo 01\", IMG_2")) ==
        std::vector<std::wstring>{ L"My Photo 01", L"IMG_2" },
        "a quoted entry keeps its spaces and commas");
    Check(Texts(ParseNameList(L"\"IMG_3 IMG_4")) == std::vector<std::wstring>{ L"IMG_3", L"IMG_4" },
        "an unterminated quote is dropped and separators apply");
    Check(Texts(ParseNameList(L"My Photo")) == std::vector<std::wstring>{ L"My", L"Photo" },
        "unquoted names with spaces split (documented limitation)");
}

void CheckDedupe() {
    Check(Texts(ParseNameList(L"IMG_1 img_1 IMG_1.JPG IMG_1.arw IMG_2")) ==
        std::vector<std::wstring>{ L"IMG_1", L"IMG_2" },
        "typed repeats count once, case-insensitively and across image extensions");
    Check(Texts(ParseNameList(L"C:\\Shoot\\IMG_9.JPG /tmp/IMG_9.cr3")) ==
        std::vector<std::wstring>{ L"IMG_9" }, "directory prefixes are removed before de-duplication");
    const auto merged = MergeNamesIntoText(L"IMG_1\nimg_2  \n",
        { L"IMG_1.JPG", L"IMG_2.ARW", L"IMG_3.JPG", L"IMG_3.ARW", L"IMG_4.xmp", L"My Shot.jpg" });
    Check(merged.text == L"IMG_1\nimg_2\nIMG_3\nIMG_4\n\"My Shot\"",
        "folder load appends new stems once, quotes names with separators, trims trailing space");
    Check(merged.added == 3 && merged.duplicates == 3, "folder load reports added and duplicate counts");
    Check(Texts(ParseNameList(merged.text)) ==
        std::vector<std::wstring>{ L"IMG_1", L"img_2", L"IMG_3", L"IMG_4", L"My Shot" },
        "merged text parses back to the de-duplicated entries");
    const auto again = MergeNamesIntoText(merged.text, { L"IMG_3.JPG", L"img_4.png" });
    Check(again.added == 0 && again.duplicates == 2 && again.text == merged.text,
        "loading the same folder twice adds nothing and keeps the text verbatim");
    Check(MergeNamesIntoText(L"", { L"A.jpg" }).text == L"A", "folder load into an empty field");
}

void CheckExtensions() {
    const auto list = ParseExtensionList(L"jpg, .ARW;*.Cr3  JPG .jpg");
    Check(list.Valid() && !list.allSupported, "known extensions with/without dot, any case, are valid");
    Check(list.extensions == std::vector<std::wstring>{ L".jpg", L".arw", L".cr3" },
        "extensions are normalized and de-duplicated in first-seen order");
    const auto bad = ParseExtensionList(L"jpg xyz .doc XYZ");
    Check(!bad.Valid() && bad.unknown == std::vector<std::wstring>{ L"xyz", L".doc" },
        "unknown extensions are reported once each, as typed");
    const auto empty = ParseExtensionList(L"  ,; ");
    Check(empty.allSupported && empty.Valid(), "empty extension field means all supported types");
    const auto all = TargetExtensions(empty);
    Check(all.size() == quicksift::core::AllSupportedImageExtensions().size(),
        "all-supported target set equals the registry");
    Check(NormalizeExtensionToken(L"..jpg").empty() && NormalizeExtensionToken(L"j/pg").empty() &&
        NormalizeExtensionToken(L"*").empty(), "malformed extension tokens are rejected");
    Check(EntryTextFromToken(L"IMG_1.JPEG") == L"IMG_1" && EntryTextFromToken(L"IMG_1.xmp") == L"IMG_1.xmp" &&
        EntryTextFromToken(L".jpg") == L".jpg" && EntryTextFromToken(L"a.b.nef") == L"a.b",
        "only one trailing supported image extension is stripped");
    Check(StemOf(L"a.b.jpg") == L"a.b" && StemOf(L".hidden") == L".hidden" && StemOf(L"noext") == L"noext",
        "StemOf removes only the last extension");
    quicksift::core::FormatSelection everything{};
    bool registryAccepted = true;
    for (const auto extension : quicksift::core::AllSupportedImageExtensions())
        registryAccepted = registryAccepted && quicksift::core::IsFormatAllowed(extension, everything);
    Check(registryAccepted, "every registry extension is admitted by IsFormatAllowed");
    bool noneMissing = true;
    for (const wchar_t* probe : { L".jpg", L".jpeg", L".jpe", L".png", L".avif", L".heic", L".heif",
        L".tif", L".tiff", L".bmp", L".dib", L".gif", L".webp", L".arw", L".cr3", L".nef", L".dng", L".x3f" })
        noneMissing = noneMissing && IsSupportedImageExtension(probe);
    Check(noneMissing, "every catalog format group is in the registry");
    Check(!IsSupportedImageExtension(L".xmp") && !IsSupportedImageExtension(L".txt"),
        "sidecars and other files are not image extensions");
}

void CheckComplete() {
    const std::vector<std::wstring> names{ L"IMG_0123.JPG", L"IMG_0123.ARW", L"IMG_01234.JPG", L"img_0123 copy.jpg" };
    Check(Match(L"img_0123", names, MatchMethod::Complete) == std::vector<std::size_t>{ 0, 1 },
        "complete: whole stem, case-insensitive, every extension of that stem");
    Check(Match(L"img_0123", names, MatchMethod::Complete, L"arw") == std::vector<std::size_t>{ 1 },
        "complete: only files whose extension is in the target set");
    Check(Match(L"123", names, MatchMethod::Complete).empty(), "complete: no partial hits");
    Check(Match(L"\"img_0123 copy\"", names, MatchMethod::Complete) == std::vector<std::size_t>{ 3 },
        "complete: a quoted name with a space matches exactly");
}

void CheckNumerals() {
    Check(PartialOne(L"123", L"IMG_0123"), "123 matches IMG_0123 (leading zeros ignored)");
    Check(!PartialOne(L"123", L"IMG_1234"), "123 does not match IMG_1234");
    Check(!PartialOne(L"123", L"IMG_51230"), "123 does not match IMG_51230");
    Check(!PartialOne(L"123", L"DSC0124"), "different number never matches");
    Check(PartialOne(L"0123", L"IMG_123"), "leading zeros in the entry are ignored too");
    Check(PartialOne(L"1", L"Shoot 1"), "1 matches a standalone 1");
    Check(!PartialOne(L"1", L"IMG_0010") && !PartialOne(L"1", L"IMG_2021"), "1 does not match 10 or 2021");
    Check(PartialOne(L"0", L"frame_000"), "0 matches an all-zero run");
    Check(PartialOne(L"2024_15", L"Trip_2024_07_15"), "several digit runs match in order");
    Check(!PartialOne(L"15_2024", L"Trip_2024_07_15"), "digit runs must keep their order");
    Check(!PartialOne(L"12_12", L"IMG_12"), "a stem digit run is used at most once");
    Check(PartialOne(L"IMG_0123", L"IMG_0123-Edit"), "entry runs may be a subset of the stem runs");
    Check(!PartialOne(L"IMG_0123", L"DSC_0123"), "letters still must match when the entry has them");
}

void CheckLetters() {
    Check(PartialOne(L"wedding", L"Wedding_001"), "letters are case-insensitive");
    Check(PartialOne(L"wedidng", L"Wedding_001"), "adjacent transposition is one edit");
    Check(PartialOne(L"weding", L"Wedding_001"), "a deletion is one edit");
    Check(PartialOne(L"weddding", L"Wedding_001"), "an insertion is one edit");
    Check(PartialOne(L"wexding", L"Wedding_001"), "a substitution is one edit");
    Check(!PartialOne(L"wxdxing", L"Wedding_001"), "two edits do not match");
    Check(PartialOne(L"edd", L"Wedding_001"), "short letter runs match as exact substrings");
    Check(!PartialOne(L"edx", L"Wedding_001"), "short letter runs get no typo tolerance");
    Check(PartialOne(L"IMGG_0123", L"IMG_0123"), "fuzzy letters plus exact digits");
    Check(!PartialOne(L"IMGG_0124", L"IMG_0123"), "fuzzy letters never rescue a wrong number");
    Check(!PartialOne(L"___", L"IMG_1"), "punctuation-only entries match nothing");
    Check(ApproximateSubstringDistance(L"abcd", L"xxabdcxx") == 1 &&
        ApproximateSubstringDistance(L"abcd", L"") == 4 &&
        ApproximateSubstringDistance(L"abcd", L"xxabcdxx") == 0, "approximate substring distance");
}

void CheckCountsAndConfirmation() {
    std::vector<std::wstring> names;
    for (int index = 1; index <= 30; ++index) names.push_back(L"A_" + std::to_wstring(index) + L".JPG");
    for (int index = 1; index <= 7; ++index) names.push_back(L"A_" + std::to_wstring(index) + L".ARW");
    names.push_back(L"A_1.png");
    names.push_back(L"B_99.jpg");
    std::wstring list;
    for (int index = 1; index <= 30; ++index) list += L"A_" + std::to_wstring(index) + L" ";
    list += L"A_1 a_1.jpg 1 ZZZ_404";
    const auto entries = ParseNameList(list);
    const auto result = MatchFiles(entries, Files(names),
        TargetExtensions(ParseExtensionList(L"jpg arw")), MatchMethod::Partial);
    Check(result.Total() == 37, "each file counts once even when several entries match it");
    Check(result.extensionCounts.size() == 2 && result.extensionCounts[0].extension == L"JPG" &&
        result.extensionCounts[0].count == 30 && result.extensionCounts[1].extension == L"ARW" &&
        result.extensionCounts[1].count == 7, "per-extension counts, largest first, png excluded by target set");
    Check(FormatConfirmation(kConfirmationTemplate, kConfirmationTemplateSingular, result.Total(),
        result.extensionCounts) ==
        L"Batch operations: 37 files (30 JPG, 7 ARW) will be processed. Confirm? This cannot be undone.",
        "confirmation sentence matches the specified wording");
    Check(FormatConfirmation(kConfirmationTemplate, kConfirmationTemplateSingular, 1,
        { { L"NEF", 1 } }) ==
        L"Batch operations: 1 file (1 NEF) will be processed. Confirm? This cannot be undone.",
        "singular confirmation");
    Check(FormatExtensionBreakdown({ { L"JPG", 3 }, { L"ARW", 0 }, { L"DNG", 1 } }) == L"3 JPG, 1 DNG",
        "zero counts are omitted from the breakdown");
    Check(result.unmatchedEntries.size() == 1 && entries[result.unmatchedEntries[0]].text == L"ZZZ_404",
        "unmatched entries are reported");
    Check(UnmatchedEntriesText(entries, result.unmatchedEntries) == L"ZZZ_404", "unmatched list text");
    const auto ties = MatchFiles(ParseNameList(L"x"), Files({ L"x.nef", L"x.dng" }),
        TargetExtensions(ParseExtensionList(L"")), MatchMethod::Complete);
    Check(ties.extensionCounts.size() == 2 && ties.extensionCounts[0].extension == L"DNG",
        "equal counts are ordered by extension name");
    std::atomic<bool> cancel{ true };
    std::vector<std::wstring> many;
    for (int index = 0; index < 2000; ++index) many.push_back(L"F_" + std::to_wstring(index) + L".jpg");
    const auto stopped = MatchFiles(ParseNameList(L"F"), Files(many), TargetExtensions(ParseExtensionList(L"")),
        MatchMethod::Partial, AsciiFold, &cancel);
    Check(stopped.cancelled, "cancellation is honoured");
    Check(Match(L"F_7", many, MatchMethod::Partial) == std::vector<std::size_t>{ 7 },
        "partial 7 matches only F_7 among 2000 files");
}

} // namespace

int main() {
    CheckSeparators();
    CheckDedupe();
    CheckExtensions();
    CheckComplete();
    CheckNumerals();
    CheckLetters();
    CheckCountsAndConfirmation();
    std::printf("batch_match_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
