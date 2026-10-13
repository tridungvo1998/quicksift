#include "core/photo_predicate.h"

#include "core/batch_match_policy.h"
#include "core/metadata_vocabulary.h"

#include <algorithm>

namespace quicksift::core::predicate {
namespace {

wchar_t LowerChar(wchar_t ch) noexcept {
    const unsigned code = static_cast<unsigned>(ch);
    if (ch >= L'A' && ch <= L'Z') return static_cast<wchar_t>(ch - L'A' + L'a');
    if (code >= 0xC0u && code <= 0xDEu && code != 0xD7u) return static_cast<wchar_t>(code + 0x20u);
    // Latin Extended-A: pairs (even upper, odd lower), except the odd-based run 0x139..0x148
    // and 0x179..0x17E.
    if (code >= 0x100u && code <= 0x137u) return static_cast<wchar_t>(code | 1u);
    if ((code >= 0x139u && code <= 0x148u) || (code >= 0x179u && code <= 0x17Eu)) {
        return (code & 1u) ? static_cast<wchar_t>(code + 1u) : ch;
    }
    if (code >= 0x14Au && code <= 0x177u) return static_cast<wchar_t>(code | 1u);
    if (code == 0x1A0u || code == 0x1AFu) return static_cast<wchar_t>(code + 1u); // Ơ Ư
    if (code >= 0x1EA0u && code <= 0x1EF9u) return static_cast<wchar_t>(code | 1u); // Vietnamese
    return ch;
}

wchar_t StripDiacritic(wchar_t lower) noexcept {
    const unsigned code = static_cast<unsigned>(lower);
    if (code < 0xC0u) return lower;
    if (code >= 0xE0u && code <= 0xE5u) return L'a';
    if (code == 0xE7u) return L'c';
    if (code >= 0xE8u && code <= 0xEBu) return L'e';
    if (code >= 0xECu && code <= 0xEFu) return L'i';
    if (code == 0xF0u) return L'd';
    if (code == 0xF1u) return L'n';
    if ((code >= 0xF2u && code <= 0xF6u) || code == 0xF8u) return L'o';
    if (code >= 0xF9u && code <= 0xFCu) return L'u';
    if (code == 0xFDu || code == 0xFFu) return L'y';
    if (code >= 0x101u && code <= 0x105u) return L'a';
    if (code >= 0x107u && code <= 0x10Du) return L'c';
    if (code == 0x10Fu || code == 0x111u) return L'd';
    if (code >= 0x113u && code <= 0x11Bu) return L'e';
    if (code >= 0x129u && code <= 0x131u) return L'i';
    if (code >= 0x14Du && code <= 0x151u) return L'o';
    if (code >= 0x169u && code <= 0x173u) return L'u';
    if (code == 0x1A1u) return L'o';
    if (code == 0x1B0u) return L'u';
    if (code >= 0x1EA0u && code <= 0x1EB7u) return L'a';
    if (code >= 0x1EB8u && code <= 0x1EC7u) return L'e';
    if (code >= 0x1EC8u && code <= 0x1ECBu) return L'i';
    if (code >= 0x1ECCu && code <= 0x1EE3u) return L'o';
    if (code >= 0x1EE4u && code <= 0x1EF1u) return L'u';
    if (code >= 0x1EF2u && code <= 0x1EF9u) return L'y';
    return lower;
}

bool Compare(int actual, Comparison op, int expected) noexcept {
    switch (op) {
    case Comparison::Equal: return actual == expected;
    case Comparison::AtLeast: return actual >= expected;
    case Comparison::AtMost: return actual <= expected;
    case Comparison::Greater: return actual > expected;
    case Comparison::Less: return actual < expected;
    }
    return false;
}

bool HasCapture(const MetadataFactsView& facts) noexcept {
    return facts.captureSource != CaptureTimeSource::Unknown && facts.capture.Valid();
}

Clause Make(ClauseKind kind, int a, Comparison op = Comparison::Equal) {
    Clause clause;
    clause.kind = kind;
    clause.a = a;
    clause.op = op;
    return clause;
}

} // namespace

bool LocalDateTime::Valid() const noexcept {
    return year >= 1 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 &&
        day <= DaysInMonth(year, month) && hour >= 0 && hour <= 23 && minute >= 0 &&
        minute <= 59 && second >= 0 && second <= 60;
}

int DaysInMonth(int year, int month) noexcept {
    static constexpr int kDays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12) return 0;
    if (month == 2) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
        return leap ? 29 : 28;
    }
    return kDays[month - 1];
}

int Weekday(int year, int month, int day) noexcept {
    if (year < 1 || month < 1 || month > 12 || day < 1 || day > DaysInMonth(year, month)) return -1;
    static constexpr int kOffsets[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    const int y = month < 3 ? year - 1 : year;
    return (y + y / 4 - y / 100 + y / 400 + kOffsets[month - 1] + day) % 7;
}

std::wstring LowerFold(std::wstring_view text) {
    std::wstring result(text);
    for (wchar_t& ch : result) ch = LowerChar(ch);
    return result;
}

std::wstring SearchFold(std::wstring_view text) {
    std::wstring result(text);
    for (wchar_t& ch : result) ch = StripDiacritic(LowerChar(ch));
    return result;
}

bool NameMatchesText(std::wstring_view name, std::wstring_view foldedText) {
    if (foldedText.empty()) return true;
    const std::wstring foldedName = SearchFold(name);
    if (foldedName.find(foldedText) != std::wstring::npos) return true;
    const batch_match::Runs entry = batch_match::Tokenize(foldedText);
    if (entry.Empty()) return false;
    return batch_match::PartialMatches(entry, batch_match::Tokenize(foldedName));
}

bool CommentMatchesText(std::wstring_view comment, std::wstring_view foldedText) {
    if (foldedText.empty()) return true;
    if (comment.empty()) return false;
    const std::wstring foldedComment = SearchFold(comment);
    if (foldedComment.find(foldedText) != std::wstring::npos) return true;
    if (foldedText.size() < batch_match::kFuzzyLetterMinLength) return false;
    return batch_match::ApproximateSubstringDistance(foldedText, foldedComment) <=
        batch_match::kFuzzyLetterMaxEdits;
}

bool ClauseMatches(const Clause& clause, const MetadataFactsView& facts) {
    switch (clause.kind) {
    case ClauseKind::Rating:
        return facts.ratingKnowledge == MetadataKnowledge::Known &&
            Compare(facts.rating, clause.op, clause.a);
    case ClauseKind::Pick:
        return facts.pickStateKnowledge == MetadataKnowledge::Known &&
            vocabulary::PickSign(facts.pickState) == clause.a;
    case ClauseKind::Color:
        return facts.colorLabelKnowledge == MetadataKnowledge::Known &&
            facts.colorLabel == clause.a;
    case ClauseKind::Time:
        return HasCapture(facts) && facts.capture.hour == clause.a &&
            (clause.b < 0 || facts.capture.minute == clause.b);
    case ClauseKind::DayOfMonth:
        return HasCapture(facts) && facts.capture.day == clause.a;
    case ClauseKind::Weekday:
        return HasCapture(facts) &&
            Weekday(facts.capture.year, facts.capture.month, facts.capture.day) == clause.a;
    case ClauseKind::Date:
        return HasCapture(facts) && (clause.a == 0 || facts.capture.year == clause.a) &&
            facts.capture.month == clause.b && facts.capture.day == clause.c;
    case ClauseKind::Text: {
        const std::wstring_view comment = facts.commentKnown ? facts.comment : std::wstring_view{};
        switch (clause.target) {
        case TextTarget::Name: return NameMatchesText(facts.name, clause.text);
        case TextTarget::Comment: return CommentMatchesText(comment, clause.text);
        case TextTarget::NameOrComment:
            return NameMatchesText(facts.name, clause.text) ||
                CommentMatchesText(comment, clause.text);
        }
        return false;
    }
    case ClauseKind::Iso:
        return facts.cameraSettingsKnown && facts.iso >= 0 &&
            Compare(facts.iso, clause.op, clause.a);
    case ClauseKind::Aperture:
        return facts.cameraSettingsKnown && facts.aperture > 0.0 &&
            Compare(static_cast<int>(facts.aperture * 100.0 + 0.5), clause.op, clause.a);
    case ClauseKind::FocalLength:
        return facts.cameraSettingsKnown && facts.focalLength > 0.0 &&
            Compare(static_cast<int>(facts.focalLength + 0.5), clause.op, clause.a);
    case ClauseKind::Shutter:
        return facts.cameraSettingsKnown && facts.shutterSpeed > 0.0 &&
            Compare(static_cast<int>(facts.shutterSpeed * 1000.0 + 0.5), clause.op, clause.a);
    case ClauseKind::Camera:
        return facts.cameraSettingsKnown && !facts.camera.empty() &&
            NameMatchesText(facts.camera, clause.text);
    case ClauseKind::Lens:
        return facts.cameraSettingsKnown && !facts.lens.empty() &&
            NameMatchesText(facts.lens, clause.text);
    }
    return false;
}

bool MatchesAll(std::span<const Clause> clauses, const MetadataFactsView& facts) {
    for (const Clause& clause : clauses) {
        if (!ClauseMatches(clause, facts)) return false;
    }
    return true;
}

bool NeedsCaptureTime(std::span<const Clause> clauses) noexcept {
    return std::any_of(clauses.begin(), clauses.end(), [](const Clause& clause) {
        return clause.kind == ClauseKind::Time || clause.kind == ClauseKind::DayOfMonth ||
            clause.kind == ClauseKind::Weekday || clause.kind == ClauseKind::Date;
    });
}

bool NeedsComments(std::span<const Clause> clauses) noexcept {
    return std::any_of(clauses.begin(), clauses.end(), [](const Clause& clause) {
        return clause.kind == ClauseKind::Text && clause.target != TextTarget::Name;
    });
}

bool NeedsCameraSettings(std::span<const Clause> clauses) noexcept {
    return std::any_of(clauses.begin(), clauses.end(), [](const Clause& clause) {
        return clause.kind == ClauseKind::Iso || clause.kind == ClauseKind::Aperture ||
            clause.kind == ClauseKind::FocalLength || clause.kind == ClauseKind::Shutter ||
            clause.kind == ClauseKind::Camera || clause.kind == ClauseKind::Lens;
    });
}

FilterClauses CompileFilterCriteria(const FilterCriteria& criteria) {
    FilterClauses result;
    const auto push = [&result](Clause clause) { result.items[result.count++] = std::move(clause); };
    switch (criteria.pickFilter) {
    case 1: push(Make(ClauseKind::Pick, vocabulary::kPickPicked)); break;
    case 2: push(Make(ClauseKind::Pick, vocabulary::kPickRejected)); break;
    case 3: push(Make(ClauseKind::Pick, vocabulary::kPickNone)); break;
    default: break;
    }
    if (criteria.colorFilter >= 1 && criteria.colorFilter <= 6) {
        push(Make(ClauseKind::Color, criteria.colorFilter - 1));
    }
    if (criteria.ratingFilter == 1) push(Make(ClauseKind::Rating, 0));
    else if (criteria.ratingFilter >= 2 && criteria.ratingFilter <= 6)
        push(Make(ClauseKind::Rating, criteria.ratingFilter - 1));
    else if (criteria.ratingFilter == 7) push(Make(ClauseKind::Rating, 4, Comparison::AtLeast));
    return result;
}

std::vector<Clause> CompileFilterCriteriaVector(const FilterCriteria& criteria) {
    const FilterClauses compiled = CompileFilterCriteria(criteria);
    return { compiled.items, compiled.items + compiled.count };
}

} // namespace quicksift::core::predicate
