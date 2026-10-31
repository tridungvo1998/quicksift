// OWNER: Deterministic, portable checks for the Thumbnail search query language,
// the shared predicate engine (search + Filter tab), the canonical metadata
// vocabulary, and the EXIF capture-time reader.
//
// Not a CTest suite (CTest stays retired): one self-contained executable with no
// timing, filesystem, UI, or platform dependencies. Build and run from the source
// root on any host with a C++20 compiler, e.g.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Isrc scripts/checks/search_query_checks.cpp
//       src/core/search_query.cpp src/core/photo_predicate.cpp
//       src/core/exif_capture_time.cpp src/core/batch_match_policy.cpp
//       src/core/image_formats.cpp -o /tmp/search_query_checks
//   /tmp/search_query_checks
//
//   cl /std:c++20 /EHsc /W4 /utf-8 /Isrc scripts\checks\search_query_checks.cpp
//       src\core\search_query.cpp src\core\photo_predicate.cpp
//       src\core\exif_capture_time.cpp src\core\batch_match_policy.cpp src\core\image_formats.cpp

#include "core/exif_capture_time.h"
#include "core/metadata_state.h"
#include "core/metadata_vocabulary.h"
#include "core/photo_predicate.h"
#include "core/search_query.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace quicksift::core;
using predicate::CaptureTimeSource;
using predicate::ClauseKind;
using predicate::Comparison;
using predicate::MetadataFactsView;
using search::ParseSearchQuery;
using search::TokenRole;

int gFailures = 0;
int gChecks = 0;

void Check(bool condition, const char* what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL: %s\n", what);
    }
}

// A known photo: IMG_0815.JPG, 2026-10-09 (Friday) 08:15:42, 5 stars, red, picked.
MetadataFactsView Photo() {
    MetadataFactsView facts;
    facts.name = L"IMG_0815.JPG";
    facts.comment = L"Sunset at the harbour with Lan";
    facts.commentKnown = true;
    facts.rating = 5;
    facts.colorLabel = vocabulary::kColorRed;
    facts.pickState = vocabulary::kPickPicked;
    facts.ratingKnowledge = MetadataKnowledge::Known;
    facts.colorLabelKnowledge = MetadataKnowledge::Known;
    facts.pickStateKnowledge = MetadataKnowledge::Known;
    facts.capture = { 2026, 10, 9, 8, 15, 42 };
    facts.captureSource = CaptureTimeSource::Exif;
    return facts;
}

bool Matches(const wchar_t* query, const MetadataFactsView& facts) {
    const search::SearchQuery parsed = ParseSearchQuery(query);
    return predicate::MatchesAll(parsed.clauses, facts);
}

bool OneClause(const wchar_t* query, ClauseKind kind, int a, int b = -1) {
    const search::SearchQuery parsed = ParseSearchQuery(query);
    if (parsed.clauses.size() != 1) return false;
    const predicate::Clause& clause = parsed.clauses.front();
    if (clause.kind != kind || clause.a != a) return false;
    if (kind == ClauseKind::Time && clause.b != b) return false;
    return true;
}

bool Rating(const wchar_t* query, Comparison op, int value) {
    const search::SearchQuery parsed = ParseSearchQuery(query);
    return parsed.clauses.size() == 1 && parsed.clauses[0].kind == ClauseKind::Rating &&
        parsed.clauses[0].op == op && parsed.clauses[0].a == value;
}

bool IsText(const wchar_t* query, const wchar_t* folded) {
    const search::SearchQuery parsed = ParseSearchQuery(query);
    return parsed.clauses.size() == 1 && parsed.clauses[0].kind == ClauseKind::Text &&
        parsed.clauses[0].text == folded;
}

void CheckTime() {
    Check(OneClause(L"8AM", ClauseKind::Time, 8), "8AM -> 08:xx");
    Check(OneClause(L"8am", ClauseKind::Time, 8), "8am lower case");
    Check(OneClause(L"8 am", ClauseKind::Time, 8), "8 am split across tokens");
    Check(OneClause(L"8 a.m.", ClauseKind::Time, 8), "8 a.m.");
    Check(OneClause(L"12AM", ClauseKind::Time, 0), "12AM is midnight hour");
    Check(OneClause(L"12pm", ClauseKind::Time, 12), "12PM is noon hour");
    Check(OneClause(L"8:30PM", ClauseKind::Time, 20, 30), "8:30PM -> 20:30 minute");
    Check(OneClause(L"8:30 pm", ClauseKind::Time, 20, 30), "8:30 pm split");
    Check(OneClause(L"20:15", ClauseKind::Time, 20, 15), "20:15 24-hour minute");
    Check(OneClause(L"20h", ClauseKind::Time, 20), "20h whole hour");
    Check(OneClause(L"20h15", ClauseKind::Time, 20, 15), "20h15");
    Check(OneClause(L"time:8AM", ClauseKind::Time, 8), "time:8AM");
    Check(OneClause(L"time:8PM", ClauseKind::Time, 20), "time:8PM");
    Check(OneClause(L"TIME:8pm", ClauseKind::Time, 20), "prefix is case-insensitive");
    Check(OneClause(L"time:20", ClauseKind::Time, 20), "time:20 whole hour");
    Check(OneClause(L"time:8", ClauseKind::Time, 8), "time:8 whole hour (24h)");
    Check(OneClause(L"time:20:15", ClauseKind::Time, 20, 15), "time:20:15");
    Check(OneClause(L"time:8 pm", ClauseKind::Time, 20), "time:8 pm split");
    Check(OneClause(L"time: 8pm", ClauseKind::Time, 20), "time: value in next token");
    Check(OneClause(L"time:8p", ClauseKind::Time, 20), "time:8p single-letter meridiem after prefix");

    const MetadataFactsView photo = Photo();
    Check(Matches(L"8AM", photo), "8AM matches 08:15");
    Check(!Matches(L"8PM", photo), "8PM does not match 08:15");
    Check(Matches(L"8:15am", photo), "minute form matches the minute");
    Check(!Matches(L"8:16am", photo), "other minute fails");
    Check(!Matches(L"time:20", photo), "time:20 fails at 08:15");
    MetadataFactsView evening = photo;
    evening.capture.hour = 20;
    Check(Matches(L"time:8PM", evening) && Matches(L"time:20", evening) && Matches(L"20h", evening),
        "evening photo matches 8PM forms");
    Check(Matches(L"20:15", evening), "20:15 matches 20:15:42");
    MetadataFactsView unknown = photo;
    unknown.captureSource = CaptureTimeSource::Unknown;
    Check(!Matches(L"8AM", unknown), "unknown capture time never matches time clauses");
    MetadataFactsView fallback = photo;
    fallback.captureSource = CaptureTimeSource::FileModified;
    Check(Matches(L"8AM", fallback), "file-modified fallback time is searchable");

    Check(ParseSearchQuery(L"13pm").clauses.size() == 1 &&
        ParseSearchQuery(L"13pm").clauses[0].kind == ClauseKind::Text, "13pm is not a time");
    Check(ParseSearchQuery(L"25:00").clauses[0].kind == ClauseKind::Text, "25:00 is not a time");
    Check(ParseSearchQuery(L"time:abc").clauses.empty() &&
        ParseSearchQuery(L"time:abc").IgnoredCount() == 1, "unparseable time: is ignored");
    Check(ParseSearchQuery(L"time:25").clauses.empty(), "time:25 is ignored");
    Check(IsText(L"8", L"8"), "a bare number is free text");
}

void CheckDate() {
    Check(OneClause(L"date:12", ClauseKind::DayOfMonth, 12), "date:12 day of month");
    Check(OneClause(L"date:9", ClauseKind::DayOfMonth, 9), "date:9");
    Check(ParseSearchQuery(L"date:32").clauses.empty(), "date:32 ignored");
    Check(OneClause(L"date:mon", ClauseKind::Weekday, 1), "date:mon");
    Check(OneClause(L"date:thu", ClauseKind::Weekday, 4), "date:thu");
    Check(OneClause(L"date:thurs", ClauseKind::Weekday, 4), "date:thurs");
    Check(OneClause(L"date:Friday", ClauseKind::Weekday, 5), "date:Friday");
    Check(OneClause(L"date:frday", ClauseKind::Weekday, 5), "date:frday (one edit)");
    Check(OneClause(L"date:tu", ClauseKind::Weekday, 2), "date:tu prefix of tuesday only");
    Check(ParseSearchQuery(L"date:s").clauses.empty(), "date:s is too short");
    Check(OneClause(L"date:t2", ClauseKind::Weekday, 1), "date:t2 (Vietnamese Monday)");
    Check(OneClause(L"date:th\u1ee9 hai", ClauseKind::Weekday, 1), "date:th\u1ee9 hai two words");
    Check(OneClause(L"date:cn", ClauseKind::Weekday, 0), "date:cn Sunday");
    Check(OneClause(L"date:2026-10-09", ClauseKind::Date, 2026), "date:YYYY-MM-DD");
    Check(OneClause(L"2026-10-09", ClauseKind::Date, 2026), "bare ISO date");
    Check(OneClause(L"date:9/10", ClauseKind::Date, 0), "date:DD/MM any year");
    Check(OneClause(L"date:09/10/2026", ClauseKind::Date, 2026), "date:DD/MM/YYYY");
    Check(ParseSearchQuery(L"date:31/02").clauses.empty(), "date:31/02 invalid");
    Check(ParseSearchQuery(L"9/10").clauses[0].kind == ClauseKind::Text, "bare DD/MM stays text");

    const MetadataFactsView photo = Photo();
    Check(Matches(L"date:9", photo) && !Matches(L"date:12", photo), "day-of-month evaluation");
    Check(Matches(L"date:fri", photo) && !Matches(L"date:mon", photo), "weekday evaluation (2026-10-09 is Friday)");
    Check(Matches(L"date:9/10", photo) && Matches(L"date:2026-10-09", photo) &&
        !Matches(L"date:2025-10-09", photo), "full date evaluation");
    Check(predicate::Weekday(2026, 10, 9) == 5 && predicate::Weekday(2000, 1, 1) == 6 &&
        predicate::Weekday(2024, 2, 29) == 4, "weekday calendar");
}

void CheckStars() {
    Check(Rating(L"5 stars", Comparison::Equal, 5), "5 stars");
    Check(Rating(L"5 star", Comparison::Equal, 5), "5 star");
    Check(Rating(L"5 sta", Comparison::Equal, 5), "5 sta");
    Check(Rating(L"5 STA", Comparison::Equal, 5), "5 STA");
    Check(Rating(L"5 starz", Comparison::Equal, 5), "5 starz (one edit)");
    Check(Rating(L"5star", Comparison::Equal, 5), "5star");
    Check(Rating(L"5stars", Comparison::Equal, 5), "5stars");
    Check(Rating(L"5sta", Comparison::Equal, 5), "5sta");
    Check(Rating(L"5*", Comparison::Equal, 5), "5*");
    Check(Rating(L"5\u2605", Comparison::Equal, 5), "5\u2605");
    Check(Rating(L"5 \u2605", Comparison::Equal, 5), "5 \u2605 split");
    Check(Rating(L"\u2605\u2605\u2605\u2605\u2605", Comparison::Equal, 5), "five star glyphs");
    Check(Rating(L"\u2605\u2605\u2605", Comparison::Equal, 3), "three star glyphs");
    Check(Rating(L"5 sao", Comparison::Equal, 5), "5 sao (Vietnamese)");
    Check(Rating(L"rating:5", Comparison::Equal, 5), "rating:5");
    Check(Rating(L"stars:3", Comparison::Equal, 3), "stars:3");
    Check(Rating(L"rating:>=4", Comparison::AtLeast, 4), "rating:>=4");
    Check(Rating(L">=4", Comparison::AtLeast, 4), "bare >=4");
    Check(Rating(L">=4 stars", Comparison::AtLeast, 4), ">=4 stars");
    Check(Rating(L"4+ stars", Comparison::AtLeast, 4), "4+ stars");
    Check(Rating(L"<3*", Comparison::Less, 3), "<3*");
    Check(Rating(L"0 stars", Comparison::Equal, 0), "0 stars");
    Check(Rating(L"unrated", Comparison::Equal, 0), "unrated");
    Check(Rating(L"unr", Comparison::Equal, 0), "unr prefix");
    Check(Rating(L"no stars", Comparison::Equal, 0), "no stars");
    Check(Rating(L"rating:unrated", Comparison::Equal, 0), "rating:unrated");
    Check(Rating(L"rating:none", Comparison::Equal, 0), "rating:none");
    Check(ParseSearchQuery(L"6 stars").clauses.size() == 2, "6 stars is not a rating (two text clauses)");
    Check(ParseSearchQuery(L"rating:9").clauses.empty(), "rating:9 ignored");
    Check(ParseSearchQuery(L"5 st").clauses.size() == 2, "5 st is too short for the star unit");

    const MetadataFactsView photo = Photo();
    Check(Matches(L"5 sta", photo) && Matches(L">=4", photo) && !Matches(L"4 stars", photo),
        "rating evaluation");
    MetadataFactsView unknown = photo;
    unknown.ratingKnowledge = MetadataKnowledge::Unknown;
    Check(!Matches(L"5 stars", unknown) && !Matches(L"unrated", unknown),
        "unknown rating never matches (Filter-tab rule)");
}

void CheckColorAndPick() {
    Check(OneClause(L"red", ClauseKind::Color, vocabulary::kColorRed), "red");
    Check(OneClause(L"RED", ClauseKind::Color, vocabulary::kColorRed), "RED");
    Check(OneClause(L"yelow", ClauseKind::Color, vocabulary::kColorYellow), "yelow");
    Check(OneClause(L"grn", ClauseKind::Color, vocabulary::kColorGreen), "grn");
    Check(OneClause(L"purp", ClauseKind::Color, vocabulary::kColorPurple), "purp");
    Check(OneClause(L"blu", ClauseKind::Color, vocabulary::kColorBlue), "blu");
    Check(OneClause(L"color:blue", ClauseKind::Color, vocabulary::kColorBlue), "color:blue");
    Check(OneClause(L"label:gr", ClauseKind::Color, vocabulary::kColorGreen), "label:gr (2-char prefix after prefix)");
    Check(OneClause(L"no color", ClauseKind::Color, vocabulary::kColorNone), "no color");
    Check(OneClause(L"color:none", ClauseKind::Color, vocabulary::kColorNone), "color:none");
    Check(OneClause(L"\u0111\u1ecf", ClauseKind::Color, vocabulary::kColorRed), "\u0111\u1ecf (VI red)");
    Check(OneClause(L"xanh l\u00e1", ClauseKind::Color, vocabulary::kColorGreen), "xanh l\u00e1 (VI green)");
    Check(OneClause(L"color:tim", ClauseKind::Color, vocabulary::kColorPurple), "color:tim");
    Check(IsText(L"tim", L"tim"), "bare unaccented 'tim' stays text");
    Check(IsText(L"re", L"re"), "two letters are not a bare keyword");

    Check(OneClause(L"picked", ClauseKind::Pick, 1), "picked");
    Check(OneClause(L"pick", ClauseKind::Pick, 1), "pick");
    Check(OneClause(L"pik", ClauseKind::Pick, 1), "pik");
    Check(OneClause(L"rejected", ClauseKind::Pick, -1), "rejected");
    Check(OneClause(L"reject", ClauseKind::Pick, -1), "reject");
    Check(OneClause(L"rej", ClauseKind::Pick, -1), "rej");
    Check(OneClause(L"unflagged", ClauseKind::Pick, 0), "unflagged");
    Check(OneClause(L"unpicked", ClauseKind::Pick, 0), "unpicked");
    Check(OneClause(L"pick:rej", ClauseKind::Pick, -1), "pick:rej");
    Check(OneClause(L"pick:none", ClauseKind::Pick, 0), "pick:none");
    Check(OneClause(L"lo\u1ea1i", ClauseKind::Pick, -1), "lo\u1ea1i (VI rejected)");
    Check(IsText(L"un", L"un"), "ambiguous 'un' is text");

    const MetadataFactsView photo = Photo();
    Check(Matches(L"red", photo) && !Matches(L"blue", photo), "color evaluation");
    Check(Matches(L"picked", photo) && !Matches(L"rej", photo) && !Matches(L"unflagged", photo),
        "pick evaluation");
}

void CheckText() {
    const MetadataFactsView photo = Photo();
    Check(Matches(L"img", photo), "name substring");
    Check(Matches(L"0815", photo) && Matches(L"815", photo), "number with/without leading zero");
    Check(Matches(L"081", photo), "number substring");
    Check(!Matches(L"0816", photo), "other number fails");
    Check(Matches(L"jpg", photo), "extension text");
    Check(Matches(L"name:img_0815", photo), "name: prefix");
    Check(Matches(L"harbour", photo) && Matches(L"harbor", photo), "comment substring and one edit");
    Check(Matches(L"comment:sunset", photo) && !Matches(L"name:sunset", photo), "comment: vs name:");
    Check(Matches(L"\"with Lan\"", photo), "quoted phrase");
    Check(!Matches(L"\"with Lam Ha\"", photo), "quoted phrase mismatch");
    Check(IsText(L"\"red\"", L"red"), "quoted keyword is text");
    Check(IsText(L"name:red", L"red"), "name:red is text");
    MetadataFactsView viet = photo;
    viet.comment = L"\u1ea2nh \u0111\u1eb9p \u1edf H\u1ed9i An";
    Check(Matches(L"hoi an", viet) && Matches(L"\u0111\u1eb9p", viet), "comment folding removes diacritics");
    MetadataFactsView noComment = photo;
    noComment.commentKnown = false;
    Check(!Matches(L"sunset", noComment), "unknown comment is empty");
    Check(ParseSearchQuery(L"--- ,,").clauses.empty() && ParseSearchQuery(L"---").IgnoredCount() == 1,
        "punctuation-only token ignored");
    Check(ParseSearchQuery(L"   ").clauses.empty() && ParseSearchQuery(L"").tokens.empty(), "empty query");
    Check(ParseSearchQuery(L"foo:bar").clauses.size() == 1 &&
        ParseSearchQuery(L"foo:bar").clauses[0].kind == ClauseKind::Text, "unknown prefix stays text");
}

void CheckCombinations() {
    const MetadataFactsView photo = Photo();
    Check(Matches(L"8AM 5 sta red picked", photo), "AND of time, stars, color, pick");
    Check(Matches(L"8am,5*,red", photo), "comma separators");
    Check(!Matches(L"8AM 4 stars", photo), "one failing clause fails all");
    Check(Matches(L"img date:fri time:8 harbour", photo), "name + date + time + comment");
    Check(!Matches(L"img blue", photo), "name ok but color fails");
    const search::SearchQuery parsed = ParseSearchQuery(L"8 am 5 sta yelow time:zz");
    Check(parsed.clauses.size() == 3 && parsed.IgnoredCount() == 1, "mixed query with an ignored token");
    Check(parsed.tokens.size() == 4 && parsed.tokens[0].source == L"8 am" &&
        parsed.tokens[0].role == TokenRole::Time && parsed.tokens[1].source == L"5 sta",
        "token reports keep two-token phrases together");
    Check(predicate::NeedsCaptureTime(parsed.clauses) && !predicate::NeedsComments(parsed.clauses),
        "hydration hints");
}

void CheckFilterCriteria() {
    // The Filter tab compiles into the same clauses; same results as the legacy switch.
    MetadataFactsView photo = Photo();
    const auto allowed = [&](int rating, int pick, int color) {
        const predicate::FilterClauses clauses = predicate::CompileFilterCriteria({ rating, pick, color });
        return predicate::MatchesAll(clauses.View(), photo);
    };
    Check(allowed(0, 0, 0), "All/All/All allows everything");
    Check(allowed(6, 0, 0) && !allowed(5, 0, 0) && allowed(7, 0, 0), "Exact5 / Exact4 / AtLeast4");
    Check(allowed(0, 1, 0) && !allowed(0, 2, 0) && !allowed(0, 3, 0), "pick filters");
    Check(allowed(0, 0, 2) && !allowed(0, 0, 1), "color filters (Red, None)");
    photo.ratingKnowledge = MetadataKnowledge::Unreadable;
    Check(allowed(0, 1, 2) && !allowed(1, 0, 0) && !allowed(7, 0, 0), "rating filter needs Known");
    photo.ratingKnowledge = MetadataKnowledge::Known;
    photo.rating = 0;
    Check(allowed(1, 0, 0) && !allowed(7, 0, 0), "Unrated filter");
    // Search "5 stars" and the Exact5 filter compile to the same clause.
    const auto searchClauses = ParseSearchQuery(L"5 stars").clauses;
    const auto filterClauses = predicate::CompileFilterCriteriaVector({ 6, 0, 0 });
    Check(searchClauses == filterClauses, "search and filter share one clause structure");
}

void CheckVocabulary() {
    Check(vocabulary::ColorExportToken(3) == "Green" && vocabulary::ParseColorExportToken("Purple") == 5 &&
        !vocabulary::ParseColorExportToken("purple"), "color export tokens are exact");
    Check(vocabulary::PickExportToken(-7) == "Rejected" && vocabulary::ParsePickExportToken("None") == 0,
        "pick export tokens");
    Check(vocabulary::StarExportToken(9) == "5" && vocabulary::ParseStarExportToken("0") == 0 &&
        !vocabulary::ParseStarExportToken("6"), "star export tokens");
    Check(vocabulary::StarLabel(0) == L"0 \u2605 (Unrated)" && vocabulary::Color(1).label == L"Red" &&
        vocabulary::Pick(1).command == L"Pick", "UI labels");
    Check(search::MatchKeyword(L"sta", L"stars", 3) == search::KeywordTier::Prefix &&
        search::MatchKeyword(L"yelow", L"yellow", 3) == search::KeywordTier::Fuzzy &&
        search::MatchKeyword(L"grn", L"green", 3) == search::KeywordTier::Fuzzy &&
        search::MatchKeyword(L"st", L"stars", 3) == search::KeywordTier::None,
        "keyword tiers");
    Check(search::OsaDistance(L"thrusday", L"thursday") == 1, "OSA transposition");
    Check(search::GroupThousands(1024, L',') == L"1,024" && search::GroupThousands(37, L',') == L"37" &&
        search::GroupThousands(1234567, L'.') == L"1.234.567" && search::GroupThousands(100000, L',') == L"100,000",
        "thousands grouping");
}

void PutU16(std::vector<std::uint8_t>& out, std::size_t at, unsigned value, bool little) {
    out[at] = static_cast<std::uint8_t>(little ? value & 0xFF : (value >> 8) & 0xFF);
    out[at + 1] = static_cast<std::uint8_t>(little ? (value >> 8) & 0xFF : value & 0xFF);
}
void PutU32(std::vector<std::uint8_t>& out, std::size_t at, unsigned value, bool little) {
    for (int index = 0; index < 4; ++index) {
        const int shift = little ? index * 8 : (3 - index) * 8;
        out[at + static_cast<std::size_t>(index)] = static_cast<std::uint8_t>((value >> shift) & 0xFF);
    }
}

// TIFF: IFD0 { DateTime, ExifIFD -> { DateTimeOriginal } }.
std::vector<std::uint8_t> MakeTiff(bool little, const char* original, const char* modified) {
    std::vector<std::uint8_t> tiff(200, 0);
    tiff[0] = tiff[1] = little ? 'I' : 'M';
    PutU16(tiff, 2, 42, little);
    PutU32(tiff, 4, 8, little);
    PutU16(tiff, 8, 2, little);                 // IFD0: 2 entries
    PutU16(tiff, 10, 0x0132, little);           // DateTime
    PutU16(tiff, 12, 2, little);
    PutU32(tiff, 14, 20, little);
    PutU32(tiff, 18, 100, little);
    PutU16(tiff, 22, 0x8769, little);           // ExifIFD
    PutU16(tiff, 24, 4, little);
    PutU32(tiff, 26, 1, little);
    PutU32(tiff, 30, 50, little);
    PutU16(tiff, 50, 1, little);                // Exif IFD: 1 entry
    PutU16(tiff, 52, 0x9003, little);
    PutU16(tiff, 54, 2, little);
    PutU32(tiff, 56, 20, little);
    PutU32(tiff, 60, 130, little);
    for (int index = 0; index < 19; ++index) {
        tiff[100 + static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(modified[index]);
        if (original) tiff[130 + static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(original[index]);
    }
    return tiff;
}

void CheckExif() {
    const auto text = exif::ParseExifDateTimeText("2026:10:09 08:15:42");
    Check(text && text->year == 2026 && text->hour == 8 && text->minute == 15, "EXIF text");
    Check(!exif::ParseExifDateTimeText("0000:00:00 00:00:00"), "zero EXIF date rejected");
    Check(!exif::ParseExifDateTimeText("2026:13:09 08:15:42"), "invalid month rejected");

    const auto little = MakeTiff(true, "2026:10:09 08:15:42", "2026:10:10 09:00:00");
    const auto tiffResult = exif::FindCaptureTime(little);
    Check(tiffResult && tiffResult->tag == exif::CaptureTag::Original && tiffResult->time.hour == 8,
        "TIFF (II) DateTimeOriginal preferred over DateTime");
    const auto big = MakeTiff(false, "2025:01:02 20:15:00", "2026:10:10 09:00:00");
    const auto bigResult = exif::FindCaptureTime(big);
    Check(bigResult && bigResult->time.year == 2025 && bigResult->time.minute == 15, "TIFF (MM)");
    const auto noOriginal = MakeTiff(true, nullptr, "2026:10:10 09:00:00");
    const auto fallback = exif::FindCaptureTime(noOriginal);
    Check(fallback && fallback->tag == exif::CaptureTag::DateTime && fallback->time.day == 10,
        "IFD0 DateTime fallback");

    // JPEG: SOI, APP0, APP1 Exif, SOS.
    std::vector<std::uint8_t> jpeg = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x04, 0x00, 0x00, 0xFF, 0xE1 };
    const std::size_t length = 2 + 6 + little.size();
    jpeg.push_back(static_cast<std::uint8_t>(length >> 8));
    jpeg.push_back(static_cast<std::uint8_t>(length & 0xFF));
    for (const char ch : { 'E', 'x', 'i', 'f' }) jpeg.push_back(static_cast<std::uint8_t>(ch));
    jpeg.push_back(0);
    jpeg.push_back(0);
    jpeg.insert(jpeg.end(), little.begin(), little.end());
    jpeg.insert(jpeg.end(), { 0xFF, 0xDA, 0x00, 0x02 });
    const auto jpegResult = exif::FindCaptureTime(jpeg);
    Check(jpegResult && jpegResult->time.hour == 8 && jpegResult->time.second == 42, "JPEG APP1 Exif");

    // Embedded block (e.g. HEIF Exif item) found by the bounded scan.
    std::vector<std::uint8_t> container(64, 0x11);
    container.insert(container.end(), big.begin(), big.end());
    const auto embedded = exif::FindCaptureTime(container);
    Check(embedded && embedded->time.year == 2025, "embedded TIFF block");

    // Truncated / hostile data never crashes and yields nothing.
    std::vector<std::uint8_t> truncated(little.begin(), little.begin() + 40);
    Check(!exif::FindCaptureTime(truncated), "truncated TIFF yields nothing");
    std::vector<std::uint8_t> hostile = little;
    PutU32(hostile, 4, 0xFFFFFFF0u, true);
    Check(!exif::FindCaptureTime(hostile), "out-of-range IFD offset yields nothing");
    Check(!exif::FindCaptureTime(std::vector<std::uint8_t>{}), "empty buffer");
}

} // namespace

void CheckRatingRanges() {
    using C = Comparison;
    struct Case { const wchar_t* query; C op; int value; };
    const Case cases[] = {
        { L"0 stars and up", C::AtLeast, 0 }, { L"3 stars or more", C::AtLeast, 3 },
        { L"at least 3 stars", C::AtLeast, 3 }, { L"3 stars and above", C::AtLeast, 3 },
        { L"3 stars or higher", C::AtLeast, 3 }, { L"no less than 3 stars", C::AtLeast, 3 },
        { L"5 stars and lower", C::AtMost, 5 }, { L"5 stars or less", C::AtMost, 5 },
        { L"5 stars and below", C::AtMost, 5 }, { L"at most 4 stars", C::AtMost, 4 },
        { L"no more than 4 stars", C::AtMost, 4 }, { L"up to 4 stars", C::AtMost, 4 },
        { L"less than 5 stars", C::Less, 5 }, { L"under 3 stars", C::Less, 3 },
        { L"below 3 stars", C::Less, 3 }, { L"fewer than 3 stars", C::Less, 3 },
        { L"more than 2 stars", C::Greater, 2 }, { L"over 2 stars", C::Greater, 2 },
        { L"above 2 stars", C::Greater, 2 }, { L"greater than 2 stars", C::Greater, 2 },
        { L"at least 1 star", C::AtLeast, 1 }, { L"1 star or more", C::AtLeast, 1 },
        { L"3stars or more", C::AtLeast, 3 }, { L"3* and up", C::AtLeast, 3 },
        { L">=3*", C::AtLeast, 3 }, { L"<5 stars", C::Less, 5 }, { L">2 stars", C::Greater, 2 },
        { L"\u22653 stars", C::AtLeast, 3 }, { L"\u2264 4 stars", C::AtMost, 4 },
        { L">= 3 stars", C::AtLeast, 3 }, { L"\u2265 3 sao", C::AtLeast, 3 },
        { L"3 sao tr\u1edf l\u00ean", C::AtLeast, 3 }, { L"3 sao tro len", C::AtLeast, 3 },
        { L"t\u1eeb 3 sao", C::AtLeast, 3 }, { L"tu 3 sao", C::AtLeast, 3 },
        { L"t\u1eeb 3 sao tr\u1edf l\u00ean", C::AtLeast, 3 },
        { L"\u00edt nh\u1ea5t 3 sao", C::AtLeast, 3 }, { L"it nhat 3 sao", C::AtLeast, 3 },
        { L"t\u1ed1i thi\u1ec3u 3 sao", C::AtLeast, 3 },
        { L"5 sao tr\u1edf xu\u1ed1ng", C::AtMost, 5 }, { L"5 sao tro xuong", C::AtMost, 5 },
        { L"t\u1ed1i \u0111a 4 sao", C::AtMost, 4 }, { L"toi da 4 sao", C::AtMost, 4 },
        { L"kh\u00f4ng qu\u00e1 4 sao", C::AtMost, 4 }, { L"khong qua 4 sao", C::AtMost, 4 },
        { L"d\u01b0\u1edbi 5 sao", C::Less, 5 }, { L"duoi 5 sao", C::Less, 5 },
        { L"\u00edt h\u01a1n 5 sao", C::Less, 5 }, { L"it hon 5 sao", C::Less, 5 },
        { L"tr\u00ean 2 sao", C::Greater, 2 }, { L"tren 2 sao", C::Greater, 2 },
        { L"nhi\u1ec1u h\u01a1n 2 sao", C::Greater, 2 }, { L"h\u01a1n 2 sao", C::Greater, 2 },
        { L"AT LEAST 3 STARS", C::AtLeast, 3 },
    };
    for (const Case& c : cases) {
        const bool ok = Rating(c.query, c.op, c.value);
        if (!ok) std::printf("  range case: %ls\n", c.query);
        Check(ok, "rating range phrase");
    }
    // Negation still applies to a whole range; "không" alone is still negation.
    {
        const auto parsed = ParseSearchQuery(L"not at least 3 stars");
        Check(parsed.clauses.size() == 1 && parsed.clauses[0].op == C::AtLeast && parsed.clauses[0].negated,
            "not + range negates the range");
        const auto vi = ParseSearchQuery(L"kh\u00f4ng 5 sao");
        Check(vi.clauses.size() == 1 && vi.clauses[0].op == C::Equal && vi.clauses[0].negated,
            "khong + N sao is still a negation");
    }
    // The unit is required: words and bare numbers stay text, existing forms unchanged.
    Check(!ParseSearchQuery(L"under 3").clauses.empty() &&
        ParseSearchQuery(L"under 3").clauses[0].kind == ClauseKind::Text, "under 3 (no unit) is text");
    Check(IsText(L"over", L"over"), "over alone is text");
    Check(IsText(L"9/10", L"9/10"), "9/10 unchanged");
    Check(Rating(L"0 stars", C::Equal, 0) && Rating(L"0 star", C::Equal, 0) && Rating(L"0 sao", C::Equal, 0),
        "0 stars / 0 star / 0 sao");
    Check(Rating(L"3 stars or less", C::AtMost, 3), "suffix after plain N stars");

    MetadataFactsView photo = Photo(); // 5 stars
    Check(Matches(L"at least 3 stars", photo) && Matches(L"3 sao tr\u1edf l\u00ean", photo), "5 >= 3");
    Check(!Matches(L"less than 5 stars", photo) && Matches(L"5 stars or less", photo), "< vs <= at 5");
    Check(!Matches(L"more than 5 stars", photo) && Matches(L"more than 4 stars", photo), "> boundary");
}

// Regression (user report, 55 photos all shown as 0 stars: "0 stars" found 39,
// "0" found 55). 16 photos had no rating in any source Exiv2/sidecar/shell could
// read, so their rating stayed Unknown and never matched. After every source is
// consulted for a readable file, an unset rating is 0 (ResolveUnsetMetadataAsNone).
void CheckUnsetRatingRegression() {
    std::size_t before = 0;
    std::size_t after = 0;
    std::size_t rangeAfter = 0;
    for (int i = 0; i < 55; ++i) {
        PhotoMetadataValues values;
        if (i < 39) values.rating = KnownMetadataValue(0);
        // else: no source had a value -> Unknown (the old final state)
        MetadataFactsView facts;
        facts.name = L"IMG_" + std::to_wstring(1000 + i) + L".JPG";
        facts.rating = values.rating.value;
        facts.ratingKnowledge = values.rating.knowledge;
        if (Matches(L"0 stars", facts)) ++before;
        ResolveUnsetMetadataAsNone(values);
        facts.rating = values.rating.value;
        facts.ratingKnowledge = values.rating.knowledge;
        if (Matches(L"0 stars", facts)) ++after;
        if (Matches(L"0 stars and up", facts) && Matches(L"unrated", facts)) ++rangeAfter;
    }
    Check(before == 39, "regression baseline: unresolved Unknown ratings miss \"0 stars\" (39 of 55)");
    Check(after == 55, "regression: \"0 stars\" matches all 55 unrated photos");
    Check(rangeAfter == 55, "regression: \"0 stars and up\" and \"unrated\" match all 55");
    {
        PhotoMetadataValues values;
        values.rating = UnreadableMetadataValue();
        values.colorLabel = UnsupportedMetadataValue(L"custom");
        ResolveUnsetMetadataAsNone(values);
        Check(values.rating.knowledge == MetadataKnowledge::Unreadable, "unreadable file never becomes 0 stars");
        Check(values.colorLabel.knowledge == MetadataKnowledge::Unsupported, "custom value kept");
        Check(values.pickState.knowledge == MetadataKnowledge::Known && values.pickState.value == 0,
            "unset pick resolves to unmarked");
    }
    {
        MetadataFactsView pending;
        pending.ratingKnowledge = MetadataKnowledge::Unknown; // not read yet
        Check(!Matches(L"0 stars", pending), "a photo not read yet does not match until read");
    }
}

int main() {
    CheckTime();
    CheckDate();
    CheckStars();
    CheckColorAndPick();
    CheckText();
    CheckCombinations();
    CheckFilterCriteria();
    CheckVocabulary();
    CheckExif();
    CheckRatingRanges();
    CheckUnsetRatingRegression();
    std::printf("search_query_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
