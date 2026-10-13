// OWNER: The ONE canonical vocabulary for star rating, pick state and color label:
// stored values, Ratings Export/Import file tokens, UI labels (English keys that the
// localizer translates), Filter-tab labels and Search keywords (EN + VI).
// Every surface (export format, import parser, Batch dropdowns, Rating/Pick/Color
// menus, Filter menus, Info tab, search keywords) reads this table; do not add
// private copies elsewhere. Header-only, portable, no Windows dependencies.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace quicksift::core::vocabulary {

// ---- Stored value domains ----------------------------------------------------------
inline constexpr int kMinStars = 0;
inline constexpr int kMaxStars = 5;
inline constexpr int kPickRejected = -1;
inline constexpr int kPickNone = 0;
inline constexpr int kPickPicked = 1;
inline constexpr int kColorNone = 0;
inline constexpr int kColorRed = 1;
inline constexpr int kColorYellow = 2;
inline constexpr int kColorGreen = 3;
inline constexpr int kColorBlue = 4;
inline constexpr int kColorPurple = 5;
inline constexpr int kMaxColor = 5;

[[nodiscard]] constexpr bool IsValidStars(int value) noexcept {
    return value >= kMinStars && value <= kMaxStars;
}
[[nodiscard]] constexpr bool IsValidColor(int value) noexcept {
    return value >= kColorNone && value <= kMaxColor;
}
// Pick state is stored as a sign: >0 picked, <0 rejected, 0 none.
[[nodiscard]] constexpr int PickSign(int pickState) noexcept {
    return pickState > 0 ? kPickPicked : (pickState < 0 ? kPickRejected : kPickNone);
}

// ---- Color labels ------------------------------------------------------------------
struct ColorEntry {
    int value;
    std::string_view exportToken;   // Ratings Export/Import file token (unchanged format)
    std::wstring_view label;        // UI label key (menus, Batch dropdown, Info tab)
    std::wstring_view xmpLabel;     // xmp:Label text written to files ("" for none)
    std::wstring_view filterLabel;  // Filter-tab menu label key
};
inline constexpr std::array<ColorEntry, 6> kColors{ { // loc-keys-begin
    { kColorNone, "None", L"None", L"", L"Unlabeled only" },
    { kColorRed, "Red", L"Red", L"Red", L"Red only" },
    { kColorYellow, "Yellow", L"Yellow", L"Yellow", L"Yellow only" },
    { kColorGreen, "Green", L"Green", L"Green", L"Green only" },
    { kColorBlue, "Blue", L"Blue", L"Blue", L"Blue only" },
    { kColorPurple, "Purple", L"Purple", L"Purple", L"Purple only" },
} }; // loc-keys-end
[[nodiscard]] constexpr const ColorEntry& Color(int value) noexcept {
    return kColors[static_cast<std::size_t>(std::clamp(value, kColorNone, kMaxColor))];
}
[[nodiscard]] constexpr std::string_view ColorExportToken(int value) noexcept {
    return Color(value).exportToken;
}
[[nodiscard]] constexpr std::optional<int> ParseColorExportToken(std::string_view token) noexcept {
    for (const ColorEntry& entry : kColors) {
        if (entry.exportToken == token) return entry.value;
    }
    return std::nullopt;
}

// ---- Pick state ----------------------------------------------------------------------
struct PickEntry {
    int value;
    std::string_view exportToken;   // Ratings Export/Import file token
    std::wstring_view command;      // Pick-state menu / Batch dropdown label key
    std::wstring_view state;        // state label key (Info tab, status bar)
    std::wstring_view filterLabel;  // Filter-tab menu label key
};
inline constexpr std::array<PickEntry, 3> kPicks{ { // loc-keys-begin
    { kPickPicked, "Picked", L"Pick", L"Pick", L"Picks only" },
    { kPickRejected, "Rejected", L"Reject", L"Reject", L"Rejected only" },
    { kPickNone, "None", L"Unmark", L"Unmarked", L"Unmarked only" },
} }; // loc-keys-end
[[nodiscard]] constexpr const PickEntry& Pick(int pickState) noexcept {
    const int sign = PickSign(pickState);
    return sign > 0 ? kPicks[0] : (sign < 0 ? kPicks[1] : kPicks[2]);
}
[[nodiscard]] constexpr std::string_view PickExportToken(int pickState) noexcept {
    return Pick(pickState).exportToken;
}
[[nodiscard]] constexpr std::optional<int> ParsePickExportToken(std::string_view token) noexcept {
    for (const PickEntry& entry : kPicks) {
        if (entry.exportToken == token) return entry.value;
    }
    return std::nullopt;
}

// ---- Stars -----------------------------------------------------------------------------
inline constexpr wchar_t kStarGlyph = L'\u2605'; // ★
[[nodiscard]] inline std::string StarExportToken(int rating) {
    return std::to_string(std::clamp(rating, kMinStars, kMaxStars));
}
[[nodiscard]] constexpr std::optional<int> ParseStarExportToken(std::string_view token) noexcept {
    if (token.size() != 1 || token[0] < '0' || token[0] > '5') return std::nullopt;
    return token[0] - '0';
}
// Rating menu / Batch dropdown label keys: "0 ★ (Unrated)", "1 ★" ... "5 ★".
inline constexpr std::array<std::wstring_view, 6> kStarLabels{ // loc-keys-begin
    L"0 \u2605 (Unrated)", L"1 \u2605", L"2 \u2605", L"3 \u2605", L"4 \u2605", L"5 \u2605" }; // loc-keys-end
[[nodiscard]] constexpr std::wstring_view StarLabel(int rating) noexcept {
    return kStarLabels[static_cast<std::size_t>(std::clamp(rating, kMinStars, kMaxStars))];
}
// Filter-tab rating labels, indexed by RatingFilter (All, Unrated, Exact1..5, AtLeast4).
inline constexpr std::array<std::wstring_view, 8> kRatingFilterLabels{ // loc-keys-begin
    L"All ratings", L"No stars", L"Exactly 1 star", L"Exactly 2 stars", L"Exactly 3 stars",
    L"Exactly 4 stars", L"Exactly 5 stars", L"4 stars and up" };
inline constexpr std::wstring_view kPickFilterAllLabel = L"All pick states";
inline constexpr std::wstring_view kColorFilterAllLabel = L"All labels"; // loc-keys-end

// ---- Search keywords ---------------------------------------------------------------------
// Matched case-insensitively with the fuzzy keyword rules in search_query.h.
// bare=false aliases are honoured only after an explicit prefix (color:, pick:,
// rating:) because the unaccented Vietnamese spellings are common words/fragments.
enum class KeywordFacet : unsigned char { Stars, StarUnit, Pick, Color, Weekday };
struct Keyword {
    KeywordFacet facet;
    int value;
    std::wstring_view text; // lower case; may contain one space (two-word alias)
    bool bare;
};
inline constexpr Keyword kKeywords[] = {
    // Star units ("5 stars", "5 sta", "5 sao", "5★", "5*").
    { KeywordFacet::StarUnit, 0, L"stars", true },
    { KeywordFacet::StarUnit, 0, L"star", true },
    { KeywordFacet::StarUnit, 0, L"sao", true },
    // Unrated == 0 stars.
    { KeywordFacet::Stars, 0, L"unrated", true },
    { KeywordFacet::Stars, 0, L"no stars", true },
    { KeywordFacet::Stars, 0, L"no star", true },
    { KeywordFacet::Stars, 0, L"ch\u01b0a \u0111\u00e1nh gi\u00e1", true }, // chưa đánh giá
    { KeywordFacet::Stars, 0, L"none", false },
    // Pick state.
    { KeywordFacet::Pick, kPickPicked, L"picked", true },
    { KeywordFacet::Pick, kPickPicked, L"pick", true },
    { KeywordFacet::Pick, kPickPicked, L"picks", true },
    { KeywordFacet::Pick, kPickPicked, L"flagged", true },
    { KeywordFacet::Pick, kPickPicked, L"\u0111\u00e3 ch\u1ecdn", true }, // đã chọn
    { KeywordFacet::Pick, kPickPicked, L"ch\u1ecdn", true },               // chọn
    { KeywordFacet::Pick, kPickPicked, L"chon", false },
    { KeywordFacet::Pick, kPickRejected, L"rejected", true },
    { KeywordFacet::Pick, kPickRejected, L"reject", true },
    { KeywordFacet::Pick, kPickRejected, L"rejects", true },
    { KeywordFacet::Pick, kPickRejected, L"lo\u1ea1i", true },              // loại
    { KeywordFacet::Pick, kPickRejected, L"b\u1ecb lo\u1ea1i", true },     // bị loại
    { KeywordFacet::Pick, kPickRejected, L"loai", false },
    { KeywordFacet::Pick, kPickNone, L"unflagged", true },
    { KeywordFacet::Pick, kPickNone, L"unpicked", true },
    { KeywordFacet::Pick, kPickNone, L"unmarked", true },
    { KeywordFacet::Pick, kPickNone, L"unmark", true },
    { KeywordFacet::Pick, kPickNone, L"no pick", true },
    { KeywordFacet::Pick, kPickNone, L"ch\u01b0a ch\u1ecdn", true },       // chưa chọn
    { KeywordFacet::Pick, kPickNone, L"none", false },
    // Color labels.
    { KeywordFacet::Color, kColorRed, L"red", true },
    { KeywordFacet::Color, kColorYellow, L"yellow", true },
    { KeywordFacet::Color, kColorGreen, L"green", true },
    { KeywordFacet::Color, kColorBlue, L"blue", true },
    { KeywordFacet::Color, kColorPurple, L"purple", true },
    { KeywordFacet::Color, kColorNone, L"no color", true },
    { KeywordFacet::Color, kColorNone, L"no colour", true },
    { KeywordFacet::Color, kColorNone, L"nocolor", true },
    { KeywordFacet::Color, kColorNone, L"uncolored", true },
    { KeywordFacet::Color, kColorNone, L"unlabeled", true },
    { KeywordFacet::Color, kColorNone, L"none", false },
    { KeywordFacet::Color, kColorRed, L"\u0111\u1ecf", true },              // đỏ
    { KeywordFacet::Color, kColorYellow, L"v\u00e0ng", true },              // vàng
    { KeywordFacet::Color, kColorGreen, L"xanh l\u00e1", true },            // xanh lá
    { KeywordFacet::Color, kColorBlue, L"xanh d\u01b0\u01a1ng", true },     // xanh dương
    { KeywordFacet::Color, kColorBlue, L"xanh lam", true },
    { KeywordFacet::Color, kColorPurple, L"t\u00edm", true },               // tím
    { KeywordFacet::Color, kColorNone, L"kh\u00f4ng m\u00e0u", true },      // không màu
    { KeywordFacet::Color, kColorRed, L"do", false },
    { KeywordFacet::Color, kColorYellow, L"vang", false },
    { KeywordFacet::Color, kColorGreen, L"xanh la", false },
    { KeywordFacet::Color, kColorBlue, L"xanh duong", false },
    { KeywordFacet::Color, kColorPurple, L"tim", false },
    { KeywordFacet::Color, kColorNone, L"khong mau", false },
    // Weekdays (only after date:), 0 = Sunday.
    { KeywordFacet::Weekday, 0, L"sunday", false },
    { KeywordFacet::Weekday, 1, L"monday", false },
    { KeywordFacet::Weekday, 2, L"tuesday", false },
    { KeywordFacet::Weekday, 3, L"wednesday", false },
    { KeywordFacet::Weekday, 4, L"thursday", false },
    { KeywordFacet::Weekday, 5, L"friday", false },
    { KeywordFacet::Weekday, 6, L"saturday", false },
    { KeywordFacet::Weekday, 4, L"thurs", false },
    { KeywordFacet::Weekday, 2, L"tues", false },
    { KeywordFacet::Weekday, 0, L"ch\u1ee7 nh\u1eadt", false },  // chủ nhật
    { KeywordFacet::Weekday, 0, L"cn", false },
    { KeywordFacet::Weekday, 1, L"th\u1ee9 hai", false },        // thứ hai
    { KeywordFacet::Weekday, 2, L"th\u1ee9 ba", false },
    { KeywordFacet::Weekday, 3, L"th\u1ee9 t\u01b0", false },    // thứ tư
    { KeywordFacet::Weekday, 4, L"th\u1ee9 n\u0103m", false },   // thứ năm
    { KeywordFacet::Weekday, 5, L"th\u1ee9 s\u00e1u", false },   // thứ sáu
    { KeywordFacet::Weekday, 6, L"th\u1ee9 b\u1ea3y", false },   // thứ bảy
    { KeywordFacet::Weekday, 1, L"t2", false },
    { KeywordFacet::Weekday, 2, L"t3", false },
    { KeywordFacet::Weekday, 3, L"t4", false },
    { KeywordFacet::Weekday, 4, L"t5", false },
    { KeywordFacet::Weekday, 5, L"t6", false },
    { KeywordFacet::Weekday, 6, L"t7", false },
};

} // namespace quicksift::core::vocabulary
