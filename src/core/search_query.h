// OWNER: Thumbnail search-box query language: tokenizer, fuzzy keyword resolution
// (via the canonical metadata_vocabulary table) and compilation into the shared
// predicate::Clause list that the Filter tab also uses. Portable and deterministic;
// checked by scripts/checks/search_query_checks.cpp.
//
// Grammar (tokens are separated by whitespace or commas; every clause ANDs):
//   "quoted phrase"           free text (name OR comment); never a keyword
//   name:x  comment:x         free text restricted to the file name / comment
//   time:8AM time:20 time:20:15 time:8:30pm
//   date:12 date:mon date:thu date:2026-10-09 date:9/10 date:9/10/2026 date:thứ hai
//   rating:5 stars:>=4 rating:4+ rating:unrated
//   color:red label:grn colour:"no color"      pick:picked pick:rej
// Bare tokens (no prefix) are tried, in order, as:
//   1. two-token phrases: "5 stars" / "5 sta" / "5 sao" / "5 ★", "8 am" / "8:30 pm",
//      two-word keywords ("no color", "xanh lá", "no pick");
//   2. a time (8AM, 8pm, 8:30PM, 20:15, 20h, 20h15),
//      a star form (5*, 5★, 5star, 5sta, ★★★★★, >=4, 4+, unrated),
//      an ISO date (2026-10-09),
//      a bare color or pick keyword (red, yelow, grn, purp, picked, pik, rej, unflagged);
//   3. otherwise free text matched against the file name OR the user comment.
// An explicit prefix whose value cannot be parsed (time:abc, rating:9) is reported as
// Ignored and adds no clause. A token made only of punctuation is Ignored.
#pragma once

#include "core/metadata_vocabulary.h"
#include "core/photo_predicate.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quicksift::core::search {

enum class TokenRole { Text, Rating, Pick, Color, Time, Date, Iso, Aperture, Focal, Shutter, Camera, Lens, Ignored };

struct TokenReport {
    std::wstring source; // token(s) as typed (two-token phrases joined by one space)
    TokenRole role = TokenRole::Text;
};

struct SearchQuery {
    std::vector<predicate::Clause> clauses;
    std::vector<TokenReport> tokens;
    [[nodiscard]] bool Empty() const noexcept { return clauses.empty(); }
    [[nodiscard]] std::size_t IgnoredCount() const noexcept;
};

[[nodiscard]] SearchQuery ParseSearchQuery(std::wstring_view text);

// ---- Fuzzy keyword rules -------------------------------------------------------------
// Tiers, best first: Exact; Prefix (token is a prefix of the keyword and has at least
// minPrefix characters); Fuzzy (both >= 4 characters and optimal-string-alignment
// distance <= 1, or an abbreviation: >= 3 characters, same first letter, an in-order
// subsequence of the keyword and at least half its length - "grn" -> green).
enum class KeywordTier { None = 0, Fuzzy = 1, Prefix = 2, Exact = 3 };
inline constexpr std::size_t kBareKeywordMinPrefix = 3;
inline constexpr std::size_t kPrefixedKeywordMinPrefix = 2;

[[nodiscard]] std::size_t OsaDistance(std::wstring_view left, std::wstring_view right);
// token and keyword must already be predicate::LowerFold()ed.
[[nodiscard]] KeywordTier MatchKeyword(std::wstring_view token, std::wstring_view keyword,
    std::size_t minPrefix);

struct KeywordHit {
    vocabulary::KeywordFacet facet = vocabulary::KeywordFacet::Color;
    int value = 0;
    KeywordTier tier = KeywordTier::None;
};
// Best-tier resolution over the vocabulary restricted to facets. When the best tier
// names more than one distinct (facet, value) the token is ambiguous: nullopt.
// bareOnly skips keywords flagged bare=false.
[[nodiscard]] std::optional<KeywordHit> ResolveKeyword(std::wstring_view loweredToken,
    std::span<const vocabulary::KeywordFacet> facets, bool bareOnly, std::size_t minPrefix);

// "37 of 1,024" style grouping helper for the result count (separator supplied by UI).
[[nodiscard]] std::wstring GroupThousands(std::size_t value, wchar_t separator);

} // namespace quicksift::core::search
