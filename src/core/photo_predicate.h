// OWNER: The ONE predicate engine shared by the Filter tab and the Thumbnail search
// box. Both compile their criteria into the same Clause list and evaluate it against
// a MetadataFactsView of the authoritative per-image metadata (CatalogStore's
// PhotoItem on the UI thread, ImageMetadataRecord in an immutable worker snapshot).
// Portable: no Windows, UI, filesystem or clock dependencies.
#pragma once

#include "metadata_state.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quicksift::core::predicate {

// Wall-clock capture time in the photo's own local time (EXIF DateTimeOriginal is
// zone-less; the file-modified fallback is converted to the machine's local time).
struct LocalDateTime {
    int year = 0;
    int month = 0;  // 1..12
    int day = 0;    // 1..31
    int hour = 0;   // 0..23
    int minute = 0; // 0..59
    int second = 0; // 0..59
    [[nodiscard]] bool Valid() const noexcept;
    friend bool operator==(const LocalDateTime&, const LocalDateTime&) = default;
};
// 0 = Sunday ... 6 = Saturday (proleptic Gregorian). -1 when the date is invalid.
[[nodiscard]] int Weekday(int year, int month, int day) noexcept;
[[nodiscard]] int DaysInMonth(int year, int month) noexcept;

enum class CaptureTimeSource : std::uint8_t {
    Unknown = 0,      // not hydrated yet
    Exif = 1,         // EXIF DateTimeOriginal (or DateTimeDigitized / DateTime)
    FileModified = 2, // no readable EXIF date: file last-write time, local
};

// Read-only view of the facts a predicate may test. Non-owning; build it from a
// PhotoItem or an ImageMetadataRecord.
struct MetadataFactsView {
    std::wstring_view name;      // file name with extension
    std::wstring_view comment;   // user comment ("" when none)
    bool commentKnown = false;
    int rating = 0;
    int colorLabel = 0;
    int pickState = 0;
    MetadataKnowledge ratingKnowledge = MetadataKnowledge::Unknown;
    MetadataKnowledge colorLabelKnowledge = MetadataKnowledge::Unknown;
    MetadataKnowledge pickStateKnowledge = MetadataKnowledge::Unknown;
    LocalDateTime capture{};
    CaptureTimeSource captureSource = CaptureTimeSource::Unknown;
    // Camera settings (indexed for fast search; 0 / empty / -1 = unknown until hydrated)
    int iso = -1;
    double aperture = 0.0;       // f-number
    double focalLength = 0.0;    // mm
    double shutterSpeed = 0.0;   // seconds
    std::wstring_view camera;    // Make + Model
    std::wstring_view lens;
    bool cameraSettingsKnown = false;
};

// ---- Text folding ------------------------------------------------------------------
// Lower-cases ASCII, Latin-1, Latin Extended-A and Vietnamese letters (no other change).
[[nodiscard]] std::wstring LowerFold(std::wstring_view text);
// LowerFold plus removal of Latin/Vietnamese diacritics ("Ảnh Đẹp" -> "anh dep").
// Used for file-name and comment matching so accents are optional.
[[nodiscard]] std::wstring SearchFold(std::wstring_view text);

// ---- Clauses ---------------------------------------------------------------------------
enum class ClauseKind : std::uint8_t {
    Rating,     // compare stars:    op, a
    Pick,       // pick sign ==      a (-1, 0, 1)
    Color,      // color label ==    a (0..5)
    Time,       // capture hour ==   a; minute == b unless b < 0 (whole hour)
    DayOfMonth, // capture day ==    a
    Weekday,    // capture weekday == a (0 = Sunday)
    Date,       // capture y/m/d ==  a/b/c; a == 0 means any year (DD/MM)
    Text,       // free text: name OR comment (or one of them, see target)
    Iso,        // ISO sensitivity: op, a
    Aperture,   // f-number: op, a (stored as int*100, e.g. 280 for f/2.8)
    FocalLength,// mm: op, a
    Shutter,    // exposure time in seconds * 1000: op, a
    Camera,     // camera make/model text match
    Lens,       // lens text match
};
enum class Comparison : std::uint8_t { Equal, AtLeast, AtMost, Greater, Less };
enum class TextTarget : std::uint8_t { NameOrComment, Name, Comment };

struct Clause {
    ClauseKind kind = ClauseKind::Text;
    Comparison op = Comparison::Equal;
    TextTarget target = TextTarget::NameOrComment;
    int a = 0;
    int b = -1;
    int c = 0;
    std::wstring text; // SearchFold()ed free text (Text clauses only)
    friend bool operator==(const Clause&, const Clause&) = default;
};

// Every clause must hold (AND). Rating / pick / color clauses require Known
// knowledge (exactly the historical Filter-tab rule); time/date clauses require a
// capture time; comment matching treats an unknown comment as empty.
[[nodiscard]] bool ClauseMatches(const Clause& clause, const MetadataFactsView& facts);
[[nodiscard]] bool MatchesAll(std::span<const Clause> clauses, const MetadataFactsView& facts);
// True when any clause needs capture time / user comment facts (hydration hints).
[[nodiscard]] bool NeedsCaptureTime(std::span<const Clause> clauses) noexcept;
[[nodiscard]] bool NeedsComments(std::span<const Clause> clauses) noexcept;
[[nodiscard]] bool NeedsCameraSettings(std::span<const Clause> clauses) noexcept;

// Free-text rules (shared with Batch Processing's partial matching):
//  name:    folded substring, OR batch_match::PartialMatches on the name's runs
//           (numbers by whole run, letter runs >= 4 chars tolerate one edit);
//  comment: folded substring, OR (text >= 4 chars) one edit anywhere in the comment.
[[nodiscard]] bool NameMatchesText(std::wstring_view name, std::wstring_view foldedText);
[[nodiscard]] bool CommentMatchesText(std::wstring_view comment, std::wstring_view foldedText);

// ---- Filter tab criteria -------------------------------------------------------------
// The Filter tab's rating / pick / color menus expressed in the same clause
// structure. Enum values mirror app_types.h RatingFilter / PickFilter /
// ColorLabelFilter (kept as ints here so this unit stays dependency-free).
struct FilterCriteria {
    int ratingFilter = 0; // 0 All, 1 Unrated, 2..6 Exact1..5, 7 AtLeast4
    int pickFilter = 0;   // 0 All, 1 Picks, 2 Rejects, 3 Unmarked
    int colorFilter = 0;  // 0 All, 1 None, 2 Red .. 6 Purple
};
struct FilterClauses {
    Clause items[3];
    std::size_t count = 0;
    [[nodiscard]] std::span<const Clause> View() const noexcept { return { items, count }; }
};
[[nodiscard]] FilterClauses CompileFilterCriteria(const FilterCriteria& criteria);
[[nodiscard]] std::vector<Clause> CompileFilterCriteriaVector(const FilterCriteria& criteria);

} // namespace quicksift::core::predicate
