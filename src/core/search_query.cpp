#include "core/search_query.h"

#include <algorithm>
#include <array>

namespace quicksift::core::search {
namespace {

using predicate::Clause;
using predicate::ClauseKind;
using predicate::Comparison;
using predicate::TextTarget;
using vocabulary::KeywordFacet;

constexpr std::size_t kNoQuote = static_cast<std::size_t>(-1);
constexpr wchar_t kBlackStar = L'\u2605';

struct RawToken {
    std::wstring text;               // quotes removed
    std::size_t quoteStart = kNoQuote; // index in text where the first quoted run starts
};

bool IsSeparator(wchar_t ch) noexcept {
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' || ch == L',' ||
        ch == L'\u00A0' || ch == L'\u3000' || ch == L'\u2009' || ch == L'\u202F';
}

bool IsQuote(wchar_t ch) noexcept {
    return ch == L'"' || ch == L'\u201C' || ch == L'\u201D';
}

bool IsDigit(wchar_t ch) noexcept { return ch >= L'0' && ch <= L'9'; }

std::vector<RawToken> TokenizeQuery(std::wstring_view text) {
    std::vector<RawToken> tokens;
    RawToken current;
    bool inToken = false;
    bool inQuote = false;
    const auto flush = [&]() {
        if (inToken && !current.text.empty()) tokens.push_back(std::move(current));
        current = RawToken{};
        inToken = false;
    };
    for (const wchar_t ch : text) {
        if (inQuote) {
            if (IsQuote(ch)) inQuote = false;
            else current.text.push_back(ch);
            continue;
        }
        if (IsQuote(ch)) {
            inQuote = true;
            inToken = true;
            if (current.quoteStart == kNoQuote) current.quoteStart = current.text.size();
            continue;
        }
        if (IsSeparator(ch)) {
            flush();
            continue;
        }
        current.text.push_back(ch);
        inToken = true;
    }
    flush();
    return tokens;
}

// ---- small cursor helpers ------------------------------------------------------------
bool ReadNumber(std::wstring_view text, std::size_t& pos, std::size_t minDigits,
    std::size_t maxDigits, int& value) noexcept {
    std::size_t count = 0;
    int result = 0;
    while (pos + count < text.size() && IsDigit(text[pos + count]) && count < maxDigits) {
        result = result * 10 + (text[pos + count] - L'0');
        ++count;
    }
    if (count < minDigits) return false;
    if (pos + count < text.size() && IsDigit(text[pos + count])) return false; // too long
    pos += count;
    value = result;
    return true;
}

bool AllOf(std::wstring_view text, wchar_t ch) noexcept {
    return !text.empty() && std::all_of(text.begin(), text.end(), [ch](wchar_t c) { return c == ch; });
}

bool HasSearchableCharacter(std::wstring_view folded) noexcept {
    return std::any_of(folded.begin(), folded.end(), [](wchar_t ch) {
        if (ch >= 0x80) return ch != kBlackStar;
        return (ch >= L'a' && ch <= L'z') || IsDigit(ch);
    });
}

// ---- keywords --------------------------------------------------------------------------
std::optional<KeywordHit> Resolve(std::wstring_view token, std::span<const KeywordFacet> facets,
    bool bareOnly, std::size_t minPrefix, bool multiWordOnly) {
    KeywordTier best = KeywordTier::None;
    std::vector<std::pair<KeywordFacet, int>> hits;
    for (const vocabulary::Keyword& keyword : vocabulary::kKeywords) {
        if (bareOnly && !keyword.bare) continue;
        if (std::find(facets.begin(), facets.end(), keyword.facet) == facets.end()) continue;
        const bool multiWord = keyword.text.find(L' ') != std::wstring_view::npos;
        if (multiWordOnly && !multiWord) continue;
        const KeywordTier tier = MatchKeyword(token, keyword.text, minPrefix);
        if (tier == KeywordTier::None || tier < best) continue;
        if (tier > best) {
            best = tier;
            hits.clear();
        }
        const std::pair<KeywordFacet, int> hit{ keyword.facet, keyword.value };
        if (std::find(hits.begin(), hits.end(), hit) == hits.end()) hits.push_back(hit);
    }
    if (hits.size() != 1) return std::nullopt;
    return KeywordHit{ hits.front().first, hits.front().second, best };
}

bool IsStarUnit(std::wstring_view lowered) {
    if (AllOf(lowered, L'*') || AllOf(lowered, kBlackStar)) return true;
    static constexpr KeywordFacet kFacets[] = { KeywordFacet::StarUnit };
    return Resolve(lowered, kFacets, true, kBareKeywordMinPrefix, false).has_value();
}

// ---- stars -------------------------------------------------------------------------------
struct StarForm {
    Comparison op = Comparison::Equal;
    int value = 0;
    bool marked = false;  // comparator, '+', or unit present
    bool hasUnit = false; // unit attached ("5*", "5stars")
};

std::optional<StarForm> ParseStarForm(std::wstring_view t, bool allowPlainNumber) {
    StarForm form;
    if (AllOf(t, kBlackStar)) {
        if (t.size() > 5) return std::nullopt;
        form.value = static_cast<int>(t.size());
        form.marked = form.hasUnit = true;
        return form;
    }
    std::size_t pos = 0;
    const auto starts = [&](std::wstring_view prefix) { return t.substr(pos).starts_with(prefix); };
    if (starts(L">=") || starts(L"\u2265")) { form.op = Comparison::AtLeast; pos += starts(L">=") ? 2 : 1; }
    else if (starts(L"<=") || starts(L"\u2264")) { form.op = Comparison::AtMost; pos += starts(L"<=") ? 2 : 1; }
    else if (starts(L">")) { form.op = Comparison::Greater; ++pos; }
    else if (starts(L"<")) { form.op = Comparison::Less; ++pos; }
    else if (starts(L"=")) { ++pos; }
    form.marked = pos > 0;
    int value = 0;
    if (!ReadNumber(t, pos, 1, 1, value) || value > vocabulary::kMaxStars) return std::nullopt;
    form.value = value;
    if (pos < t.size() && t[pos] == L'+') {
        if (form.op != Comparison::Equal) return std::nullopt;
        form.op = Comparison::AtLeast;
        form.marked = true;
        ++pos;
    }
    const std::wstring_view rest = t.substr(pos);
    if (rest.empty()) {
        if (!form.marked && !allowPlainNumber) return std::nullopt;
        return form;
    }
    if (!IsStarUnit(rest)) return std::nullopt;
    form.marked = form.hasUnit = true;
    return form;
}

Clause RatingClause(Comparison op, int value) {
    Clause clause;
    clause.kind = ClauseKind::Rating;
    clause.op = op;
    clause.a = value;
    return clause;
}

// ---- time ----------------------------------------------------------------------------------
std::optional<bool> ParseMeridiem(std::wstring_view t, bool allowSingleLetter) noexcept {
    if (t == L"am" || t == L"a.m." || t == L"a.m") return false;
    if (t == L"pm" || t == L"p.m." || t == L"p.m") return true;
    if (allowSingleLetter && t == L"a") return false;
    if (allowSingleLetter && t == L"p") return true;
    return std::nullopt;
}

struct TimeForm {
    int hour = 0;
    int minute = -1;
    bool explicitForm = false;  // meridiem, minutes or 'h' present
    bool canTakeMeridiem = false;
};

int ApplyMeridiem(int hour, bool pm) noexcept {
    if (hour == 12) return pm ? 12 : 0;
    return pm ? hour + 12 : hour;
}

std::optional<TimeForm> ParseTimeForm(std::wstring_view t, bool prefixed) {
    std::size_t pos = 0;
    int hour = 0;
    if (!ReadNumber(t, pos, 1, 2, hour)) return std::nullopt;
    TimeForm form;
    bool hForm = false;
    if (pos < t.size() && t[pos] == L':') {
        ++pos;
        int minute = 0;
        if (!ReadNumber(t, pos, 2, 2, minute) || minute > 59) return std::nullopt;
        form.minute = minute;
        form.explicitForm = true;
    } else if (pos < t.size() && t[pos] == L'h') {
        ++pos;
        hForm = true;
        form.explicitForm = true;
        if (pos < t.size()) {
            int minute = 0;
            if (!ReadNumber(t, pos, 2, 2, minute) || minute > 59) return std::nullopt;
            form.minute = minute;
        }
        if (pos != t.size()) return std::nullopt;
    }
    const std::wstring_view rest = t.substr(pos);
    if (!rest.empty()) {
        const std::optional<bool> pm = ParseMeridiem(rest, prefixed);
        if (!pm || hour < 1 || hour > 12) return std::nullopt;
        form.hour = ApplyMeridiem(hour, *pm);
        form.explicitForm = true;
        return form;
    }
    if (hour > 23) return std::nullopt;
    form.hour = hour;
    form.canTakeMeridiem = !hForm && hour >= 1 && hour <= 12;
    if (!form.explicitForm && !prefixed) {
        // A bare number is only a time when an am/pm token follows (checked by caller).
        return form;
    }
    return form;
}

Clause TimeClause(int hour, int minute) {
    Clause clause;
    clause.kind = ClauseKind::Time;
    clause.a = hour;
    clause.b = minute;
    return clause;
}

// ---- date -------------------------------------------------------------------------------------
bool ValidDayMonth(int year, int month, int day) noexcept {
    if (month < 1 || month > 12 || day < 1) return false;
    return day <= predicate::DaysInMonth(year == 0 ? 2000 : year, month);
}

std::optional<Clause> ParseNumericDate(std::wstring_view t, bool prefixed) {
    const auto isSep = [](wchar_t ch) noexcept { return ch == L'-' || ch == L'/' || ch == L'.'; };
    std::size_t pos = 0;
    int first = 0;
    if (!ReadNumber(t, pos, 1, 4, first)) return std::nullopt;
    const std::size_t firstDigits = pos;
    Clause clause;
    clause.kind = ClauseKind::Date;
    if (pos == t.size()) {
        if (!prefixed || firstDigits > 2 || first < 1 || first > 31) return std::nullopt;
        clause.kind = ClauseKind::DayOfMonth;
        clause.a = first;
        return clause;
    }
    if (!isSep(t[pos])) return std::nullopt;
    const wchar_t sep = t[pos++];
    int second = 0;
    if (!ReadNumber(t, pos, 1, 2, second)) return std::nullopt;
    if (firstDigits == 4) {
        // YYYY-MM-DD
        if (pos >= t.size() || t[pos] != sep) return std::nullopt;
        ++pos;
        int third = 0;
        if (!ReadNumber(t, pos, 1, 2, third) || pos != t.size()) return std::nullopt;
        if (first < 1 || !ValidDayMonth(first, second, third)) return std::nullopt;
        clause.a = first;
        clause.b = second;
        clause.c = third;
        return clause;
    }
    if (!prefixed || firstDigits > 2) return std::nullopt;
    // DD/MM[/YYYY]
    int year = 0;
    if (pos < t.size()) {
        if (t[pos] != sep) return std::nullopt;
        ++pos;
        if (!ReadNumber(t, pos, 4, 4, year) || pos != t.size() || year < 1) return std::nullopt;
    }
    if (!ValidDayMonth(year, second, first)) return std::nullopt;
    clause.a = year;
    clause.b = second;
    clause.c = first;
    return clause;
}

Clause TextClause(std::wstring folded, TextTarget target) {
    Clause clause;
    clause.kind = ClauseKind::Text;
    clause.target = target;
    clause.text = std::move(folded);
    return clause;
}

enum class Prefix { None, Name, Comment, Time, Date, Rating, Color, Pick,
    Iso, Aperture, Focal, Shutter, Camera, Lens };

Prefix PrefixFromHead(std::wstring_view head) noexcept {
    struct Entry { std::wstring_view text; Prefix prefix; };
    static constexpr Entry kPrefixes[] = {
        { L"name", Prefix::Name }, { L"file", Prefix::Name }, { L"filename", Prefix::Name },
        { L"comment", Prefix::Comment }, { L"comments", Prefix::Comment },
        { L"note", Prefix::Comment }, { L"ghichu", Prefix::Comment },
        { L"time", Prefix::Time }, { L"gio", Prefix::Time }, { L"gi\u1edd", Prefix::Time },
        { L"date", Prefix::Date }, { L"day", Prefix::Date }, { L"ngay", Prefix::Date },
        { L"ng\u00e0y", Prefix::Date },
        { L"rating", Prefix::Rating }, { L"rate", Prefix::Rating }, { L"stars", Prefix::Rating },
        { L"star", Prefix::Rating }, { L"sao", Prefix::Rating },
        { L"color", Prefix::Color }, { L"colour", Prefix::Color }, { L"label", Prefix::Color },
        { L"mau", Prefix::Color }, { L"m\u00e0u", Prefix::Color },
        { L"pick", Prefix::Pick }, { L"flag", Prefix::Pick },
        { L"iso", Prefix::Iso },
        { L"f", Prefix::Aperture }, { L"aperture", Prefix::Aperture }, { L"fstop", Prefix::Aperture },
        { L"focal", Prefix::Focal }, { L"mm", Prefix::Focal },
        { L"shutter", Prefix::Shutter }, { L"ss", Prefix::Shutter }, { L"exposure", Prefix::Shutter },
        { L"camera", Prefix::Camera }, { L"cam", Prefix::Camera },
        { L"lens", Prefix::Lens },
    };
    for (const Entry& entry : kPrefixes) {
        if (entry.text == head) return entry.prefix;
    }
    return Prefix::None;
}

class Parser {
public:
    explicit Parser(std::vector<RawToken> tokens) : tokens_(std::move(tokens)) {
        lowered_.reserve(tokens_.size());
        for (const RawToken& token : tokens_) lowered_.push_back(predicate::LowerFold(token.text));
    }

    SearchQuery Run() {
        while (index_ < tokens_.size()) Step();
        return std::move(query_);
    }

private:
    bool HasNext(std::size_t offset = 1) const noexcept { return index_ + offset < tokens_.size(); }
    bool Quoted(std::size_t at) const noexcept { return tokens_[at].quoteStart != kNoQuote; }

    void Report(std::size_t count, TokenRole role) {
        std::wstring source;
        for (std::size_t at = index_; at < index_ + count && at < tokens_.size(); ++at) {
            if (!source.empty()) source += L' ';
            source += tokens_[at].text;
        }
        query_.tokens.push_back({ std::move(source), role });
        index_ += count;
    }

    void Add(Clause clause, std::size_t count, TokenRole role) {
        query_.clauses.push_back(std::move(clause));
        Report(count, role);
    }

    void AddText(std::wstring_view raw, TextTarget target, std::size_t count) {
        std::wstring folded = predicate::SearchFold(raw);
        if (!HasSearchableCharacter(folded)) {
            Report(count, TokenRole::Ignored);
            return;
        }
        Add(TextClause(std::move(folded), target), count, TokenRole::Text);
    }

    static TokenRole RoleFor(KeywordFacet facet) noexcept {
        switch (facet) {
        case KeywordFacet::Pick: return TokenRole::Pick;
        case KeywordFacet::Color: return TokenRole::Color;
        case KeywordFacet::Weekday: return TokenRole::Date;
        case KeywordFacet::Stars:
        case KeywordFacet::StarUnit: return TokenRole::Rating;
        }
        return TokenRole::Text;
    }

    static Clause ClauseFor(const KeywordHit& hit) {
        Clause clause;
        switch (hit.facet) {
        case KeywordFacet::Pick: clause.kind = ClauseKind::Pick; break;
        case KeywordFacet::Color: clause.kind = ClauseKind::Color; break;
        case KeywordFacet::Weekday: clause.kind = ClauseKind::Weekday; break;
        case KeywordFacet::Stars:
        case KeywordFacet::StarUnit: clause.kind = ClauseKind::Rating; break;
        }
        clause.a = hit.value;
        return clause;
    }

    // Two-word keyword at value + next token (prefixed) or token + next (bare).
    std::optional<KeywordHit> TwoWord(std::wstring_view first, std::size_t nextIndex,
        std::span<const KeywordFacet> facets, bool bareOnly, std::size_t minPrefix) const {
        if (nextIndex >= tokens_.size() || Quoted(nextIndex)) return std::nullopt;
        const std::wstring& second = lowered_[nextIndex];
        std::wstring joined(first);
        joined += L' ';
        joined += second;
        const auto hit = Resolve(joined, facets, bareOnly, minPrefix, true);
        if (!hit) return std::nullopt;
        if (hit->tier == KeywordTier::Prefix && second.size() < 2) return std::nullopt;
        return hit;
    }

    void Step() {
        const RawToken& token = tokens_[index_];
        const std::wstring& lower = lowered_[index_];
        // Explicit prefix (the ':' must precede any quoted run).
        const std::size_t colon = lower.find(L':');
        if (colon != std::wstring::npos && colon > 0 &&
            (token.quoteStart == kNoQuote || colon < token.quoteStart)) {
            const Prefix prefix = PrefixFromHead(std::wstring_view(lower).substr(0, colon));
            if (prefix != Prefix::None) {
                StepPrefixed(prefix, colon);
                return;
            }
        }
        if (token.quoteStart != kNoQuote) {
            AddText(token.text, TextTarget::NameOrComment, 1);
            return;
        }
        if (StepBareTwoToken()) return;
        if (StepBareSingle()) return;
        AddText(token.text, TextTarget::NameOrComment, 1);
    }

    void StepPrefixed(Prefix prefix, std::size_t colon) {
        std::wstring raw = tokens_[index_].text.substr(colon + 1);
        std::wstring value = lowered_[index_].substr(colon + 1);
        std::size_t used = 1;          // tokens consumed so far
        bool valueQuoted = Quoted(index_);
        if (value.empty() && HasNext() ) {
            raw = tokens_[index_ + 1].text;
            value = lowered_[index_ + 1];
            valueQuoted = Quoted(index_ + 1);
            used = 2;
        }
        const std::size_t next = index_ + used; // lookahead token for two-token values
        if (value.empty()) {
            Report(used, TokenRole::Ignored);
            return;
        }
        switch (prefix) {
        case Prefix::Name:
            AddText(raw, TextTarget::Name, used);
            return;
        case Prefix::Comment:
            AddText(raw, TextTarget::Comment, used);
            return;
        case Prefix::Time: {
            const auto form = ParseTimeForm(value, true);
            if (!form) break;
            if (form->canTakeMeridiem && next < tokens_.size()) {
                if (const auto pm = ParseMeridiem(lowered_[next], true)) {
                    Add(TimeClause(ApplyMeridiem(form->hour, *pm), form->minute), used + 1, TokenRole::Time);
                    return;
                }
            }
            Add(TimeClause(form->hour, form->minute), used, TokenRole::Time);
            return;
        }
        case Prefix::Date: {
            if (auto clause = ParseNumericDate(value, true)) {
                Add(std::move(*clause), used, TokenRole::Date);
                return;
            }
            static constexpr KeywordFacet kFacets[] = { KeywordFacet::Weekday };
            if (!valueQuoted) {
                if (const auto hit = TwoWord(value, next, kFacets, false, kPrefixedKeywordMinPrefix)) {
                    Add(ClauseFor(*hit), used + 1, TokenRole::Date);
                    return;
                }
            }
            if (const auto hit = Resolve(value, kFacets, false, kPrefixedKeywordMinPrefix, false)) {
                Add(ClauseFor(*hit), used, TokenRole::Date);
                return;
            }
            break;
        }
        case Prefix::Rating: {
            if (const auto form = ParseStarForm(value, true)) {
                std::size_t count = used;
                if (!form->hasUnit && next < tokens_.size() && !Quoted(next) && IsStarUnit(lowered_[next]))
                    ++count;
                Add(RatingClause(form->op, form->value), count, TokenRole::Rating);
                return;
            }
            static constexpr KeywordFacet kFacets[] = { KeywordFacet::Stars };
            if (!valueQuoted) {
                if (const auto hit = TwoWord(value, next, kFacets, false, kPrefixedKeywordMinPrefix)) {
                    Add(ClauseFor(*hit), used + 1, TokenRole::Rating);
                    return;
                }
            }
            if (const auto hit = Resolve(value, kFacets, false, kPrefixedKeywordMinPrefix, false)) {
                Add(ClauseFor(*hit), used, TokenRole::Rating);
                return;
            }
            break;
        }
        case Prefix::Color:
        case Prefix::Pick: {
            const KeywordFacet facet = prefix == Prefix::Color ? KeywordFacet::Color : KeywordFacet::Pick;
            const KeywordFacet facets[] = { facet };
            if (!valueQuoted) {
                if (const auto hit = TwoWord(value, next, facets, false, kPrefixedKeywordMinPrefix)) {
                    Add(ClauseFor(*hit), used + 1, RoleFor(facet));
                    return;
                }
            }
            if (const auto hit = Resolve(value, facets, false, kPrefixedKeywordMinPrefix, false)) {
                Add(ClauseFor(*hit), used, RoleFor(facet));
                return;
            }
            break;
        }
        case Prefix::Iso:
        case Prefix::Aperture:
        case Prefix::Focal:
        case Prefix::Shutter: {
            // Accept "400", ">=400", "2.8", "1/125", "50".
            Comparison op = Comparison::Equal;
            std::wstring_view num = value;
            if (num.size() >= 2 && num[0] == L'>' && num[1] == L'=') { op = Comparison::AtLeast; num.remove_prefix(2); }
            else if (num.size() >= 2 && num[0] == L'<' && num[1] == L'=') { op = Comparison::AtMost; num.remove_prefix(2); }
            else if (!num.empty() && num[0] == L'>') { op = Comparison::Greater; num.remove_prefix(1); }
            else if (!num.empty() && num[0] == L'<') { op = Comparison::Less; num.remove_prefix(1); }
            double parsed = 0.0;
            bool ok = false;
            if (prefix == Prefix::Shutter && num.find(L'/') != std::wstring::npos) {
                // "1/125" -> 0.008
                const auto slash = num.find(L'/');
                try {
                    const double n = std::stod(std::wstring(num.substr(0, slash)));
                    const double d = std::stod(std::wstring(num.substr(slash + 1)));
                    if (d != 0.0) { parsed = n / d; ok = true; }
                } catch (...) {}
            } else {
                try { parsed = std::stod(std::wstring(num)); ok = true; } catch (...) {}
            }
            if (!ok || parsed < 0.0) break;
            Clause clause;
            TokenRole role = TokenRole::Iso;
            if (prefix == Prefix::Iso) {
                clause.kind = ClauseKind::Iso;
                clause.a = static_cast<int>(parsed + 0.5);
                role = TokenRole::Iso;
            } else if (prefix == Prefix::Aperture) {
                clause.kind = ClauseKind::Aperture;
                clause.a = static_cast<int>(parsed * 100.0 + 0.5);
                role = TokenRole::Aperture;
            } else if (prefix == Prefix::Focal) {
                clause.kind = ClauseKind::FocalLength;
                clause.a = static_cast<int>(parsed + 0.5);
                role = TokenRole::Focal;
            } else {
                clause.kind = ClauseKind::Shutter;
                clause.a = static_cast<int>(parsed * 1000.0 + 0.5);
                role = TokenRole::Shutter;
            }
            clause.op = op;
            Add(std::move(clause), used, role);
            return;
        }
        case Prefix::Camera:
        case Prefix::Lens: {
            Clause clause;
            clause.kind = prefix == Prefix::Camera ? ClauseKind::Camera : ClauseKind::Lens;
            clause.text = predicate::SearchFold(raw);
            Add(std::move(clause), used, prefix == Prefix::Camera ? TokenRole::Camera : TokenRole::Lens);
            return;
        }
        case Prefix::None:
            break;
        }
        Report(used, TokenRole::Ignored);
    }

    bool StepBareTwoToken() {
        if (!HasNext() || Quoted(index_ + 1)) return false;
        const std::wstring& first = lowered_[index_];
        const std::wstring& second = lowered_[index_ + 1];
        // "5 stars", "5 sta", ">=4 stars", "4+ sao", "5 ★"
        if (const auto form = ParseStarForm(first, true); form && !form->hasUnit && IsStarUnit(second)) {
            Add(RatingClause(form->op, form->value), 2, TokenRole::Rating);
            return true;
        }
        // "8 am", "8:30 pm"
        if (const auto form = ParseTimeForm(first, false); form && form->canTakeMeridiem) {
            if (const auto pm = ParseMeridiem(second, false)) {
                Add(TimeClause(ApplyMeridiem(form->hour, *pm), form->minute), 2, TokenRole::Time);
                return true;
            }
        }
        static constexpr KeywordFacet kFacets[] = { KeywordFacet::Stars, KeywordFacet::Pick, KeywordFacet::Color };
        if (const auto hit = TwoWord(first, index_ + 1, kFacets, true, kBareKeywordMinPrefix)) {
            Add(ClauseFor(*hit), 2, RoleFor(hit->facet));
            return true;
        }
        return false;
    }

    bool StepBareSingle() {
        const std::wstring& lower = lowered_[index_];
        // Bare camera forms: f/4, f/2.8, 50mm, 1/125 (no other context).
        if (lower.size() >= 3 && (lower[0] == L'f' && lower[1] == L'/')) {
            try {
                const double f = std::stod(lower.substr(2));
                if (f > 0.0 && f < 100.0) {
                    Clause clause;
                    clause.kind = ClauseKind::Aperture;
                    clause.a = static_cast<int>(f * 100.0 + 0.5);
                    Add(std::move(clause), 1, TokenRole::Aperture);
                    return true;
                }
            } catch (...) {}
        }
        if (lower.size() >= 3 && lower.ends_with(L"mm")) {
            try {
                const double mm = std::stod(lower.substr(0, lower.size() - 2));
                if (mm > 0.0 && mm < 2000.0) {
                    Clause clause;
                    clause.kind = ClauseKind::FocalLength;
                    clause.a = static_cast<int>(mm + 0.5);
                    Add(std::move(clause), 1, TokenRole::Focal);
                    return true;
                }
            } catch (...) {}
        }
        if (lower.size() >= 3 && lower.find(L'/') != std::wstring::npos && lower[0] != L'f') {
            const auto slash = lower.find(L'/');
            if (slash > 0 && slash + 1 < lower.size()) {
                try {
                    const double n = std::stod(lower.substr(0, slash));
                    const double d = std::stod(lower.substr(slash + 1));
                    if (d > 0.0 && n > 0.0 && n / d < 100.0) {
                        Clause clause;
                        clause.kind = ClauseKind::Shutter;
                        clause.a = static_cast<int>((n / d) * 1000.0 + 0.5);
                        Add(std::move(clause), 1, TokenRole::Shutter);
                        return true;
                    }
                } catch (...) {}
            }
        }
        if (const auto form = ParseTimeForm(lower, false); form && form->explicitForm) {
            Add(TimeClause(form->hour, form->minute), 1, TokenRole::Time);
            return true;
        }
        if (const auto form = ParseStarForm(lower, false); form && form->marked) {
            Add(RatingClause(form->op, form->value), 1, TokenRole::Rating);
            return true;
        }
        if (auto clause = ParseNumericDate(lower, false)) {
            Add(std::move(*clause), 1, TokenRole::Date);
            return true;
        }
        static constexpr KeywordFacet kFacets[] = { KeywordFacet::Stars, KeywordFacet::Pick, KeywordFacet::Color };
        if (const auto hit = Resolve(lower, kFacets, true, kBareKeywordMinPrefix, false)) {
            Add(ClauseFor(*hit), 1, RoleFor(hit->facet));
            return true;
        }
        return false;
    }

    std::vector<RawToken> tokens_;
    std::vector<std::wstring> lowered_;
    std::size_t index_ = 0;
    SearchQuery query_;
};

} // namespace

std::size_t SearchQuery::IgnoredCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(tokens.begin(), tokens.end(),
        [](const TokenReport& token) { return token.role == TokenRole::Ignored; }));
}

std::size_t OsaDistance(std::wstring_view left, std::wstring_view right) {
    const std::size_t rows = left.size() + 1;
    const std::size_t columns = right.size() + 1;
    std::vector<std::size_t> table(rows * columns, 0);
    const auto at = [&](std::size_t row, std::size_t column) -> std::size_t& {
        return table[row * columns + column];
    };
    for (std::size_t row = 0; row < rows; ++row) at(row, 0) = row;
    for (std::size_t column = 0; column < columns; ++column) at(0, column) = column;
    for (std::size_t row = 1; row < rows; ++row) {
        for (std::size_t column = 1; column < columns; ++column) {
            const std::size_t cost = left[row - 1] == right[column - 1] ? 0 : 1;
            std::size_t best = std::min({ at(row - 1, column) + 1, at(row, column - 1) + 1,
                at(row - 1, column - 1) + cost });
            if (row > 1 && column > 1 && left[row - 1] == right[column - 2] &&
                left[row - 2] == right[column - 1]) {
                best = std::min(best, at(row - 2, column - 2) + 1);
            }
            at(row, column) = best;
        }
    }
    return at(rows - 1, columns - 1);
}

KeywordTier MatchKeyword(std::wstring_view token, std::wstring_view keyword, std::size_t minPrefix) {
    if (token.empty() || keyword.empty()) return KeywordTier::None;
    if (token == keyword) return KeywordTier::Exact;
    if (token.size() >= minPrefix && token.size() < keyword.size() && keyword.starts_with(token))
        return KeywordTier::Prefix;
    if (token.size() >= 4 && keyword.size() >= 4 && OsaDistance(token, keyword) <= 1)
        return KeywordTier::Fuzzy;
    if (token.size() >= 3 && token.size() < keyword.size() && token.front() == keyword.front() &&
        token.size() * 2 >= keyword.size()) {
        std::size_t matched = 0;
        for (const wchar_t ch : keyword) {
            if (matched < token.size() && token[matched] == ch) ++matched;
        }
        if (matched == token.size()) return KeywordTier::Fuzzy;
    }
    return KeywordTier::None;
}

std::optional<KeywordHit> ResolveKeyword(std::wstring_view loweredToken,
    std::span<const KeywordFacet> facets, bool bareOnly, std::size_t minPrefix) {
    return Resolve(loweredToken, facets, bareOnly, minPrefix, false);
}

SearchQuery ParseSearchQuery(std::wstring_view text) {
    return Parser(TokenizeQuery(text)).Run();
}

std::wstring GroupThousands(std::size_t value, wchar_t separator) {
    std::wstring digits = std::to_wstring(value);
    std::wstring result;
    const std::size_t lead = digits.size() % 3;
    for (std::size_t at = 0; at < digits.size(); ++at) {
        if (at != 0 && (at - lead) % 3 == 0 && at >= lead) result += separator;
        result += digits[at];
    }
    return result;
}

} // namespace quicksift::core::search
