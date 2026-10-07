// CODE GUIDE: See CODE_GUIDE.md -> "Reading metadata".
// OWNER: Metadata parsing implementation; require full-value, bounded conversions.

#include "metadata_value_parser.h"

#include <cstdint>
#include <cwctype>
#include <limits>
#include <type_traits>

namespace quicksift::metadata {
namespace {

template <typename Character>
bool IsSpace(Character character) noexcept {
    if constexpr (std::is_same_v<Character, wchar_t>) {
        return std::iswspace(static_cast<wint_t>(character)) != 0;
    } else {
        const unsigned char byte = static_cast<unsigned char>(character);
        return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n' || byte == '\f' || byte == '\v';
    }
}

template <typename Character>
std::basic_string_view<Character> Trim(std::basic_string_view<Character> text) noexcept {
    while (!text.empty() && IsSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && IsSpace(text.back())) text.remove_suffix(1);
    return text;
}

template <typename Character>
Character LowerAscii(Character character) noexcept {
    const Character upperA = static_cast<Character>('A');
    const Character upperZ = static_cast<Character>('Z');
    if (character >= upperA && character <= upperZ) {
        return static_cast<Character>(character + (static_cast<Character>('a') - upperA));
    }
    return character;
}

template <typename Character>
bool EqualsAsciiCaseInsensitive(std::basic_string_view<Character> left,
                                std::basic_string_view<Character> right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (LowerAscii(left[index]) != LowerAscii(right[index])) return false;
    }
    return true;
}

template <typename Character>
std::optional<int> ParseBoundedIntegerImpl(std::basic_string_view<Character> text,
                                           int minimum,
                                           int maximum) noexcept {
    text = Trim(text);
    if (text.empty() || minimum > maximum) return std::nullopt;

    bool negative = false;
    std::size_t index = 0;
    const Character plus = static_cast<Character>('+');
    const Character minus = static_cast<Character>('-');
    if (text.front() == plus || text.front() == minus) {
        negative = text.front() == minus;
        index = 1;
    }
    if (index == text.size()) return std::nullopt;

    std::int64_t magnitude = 0;
    const Character zero = static_cast<Character>('0');
    const Character nine = static_cast<Character>('9');
    for (; index < text.size(); ++index) {
        const Character character = text[index];
        if (character < zero || character > nine) return std::nullopt;
        const int digit = static_cast<int>(character - zero);
        if (magnitude > (std::numeric_limits<std::int64_t>::max() - digit) / 10) return std::nullopt;
        magnitude = magnitude * 10 + digit;
    }

    // The positive magnitude was already proven <= INT64_MAX, so negating it
    // is defined. QuickSift's metadata domains are much narrower than int64.
    const std::int64_t value = negative ? -magnitude : magnitude;
    if (value < minimum || value > maximum) return std::nullopt;
    return static_cast<int>(value);
}

std::optional<int> ParseKnownColorLabelImpl(std::string_view text) noexcept {
    text = Trim(text);
    if (EqualsAsciiCaseInsensitive(text, std::string_view("red"))) return 1;
    if (EqualsAsciiCaseInsensitive(text, std::string_view("yellow"))) return 2;
    if (EqualsAsciiCaseInsensitive(text, std::string_view("green"))) return 3;
    if (EqualsAsciiCaseInsensitive(text, std::string_view("blue"))) return 4;
    if (EqualsAsciiCaseInsensitive(text, std::string_view("purple"))) return 5;
    if (text.empty()) return 0;
    return std::nullopt;
}

std::optional<int> ParseKnownColorLabelImpl(std::wstring_view text) noexcept {
    text = Trim(text);
    if (EqualsAsciiCaseInsensitive(text, std::wstring_view(L"red"))) return 1;
    if (EqualsAsciiCaseInsensitive(text, std::wstring_view(L"yellow"))) return 2;
    if (EqualsAsciiCaseInsensitive(text, std::wstring_view(L"green"))) return 3;
    if (EqualsAsciiCaseInsensitive(text, std::wstring_view(L"blue"))) return 4;
    if (EqualsAsciiCaseInsensitive(text, std::wstring_view(L"purple"))) return 5;
    if (text.empty()) return 0;
    return std::nullopt;
}

} // namespace

std::optional<int> ParseBoundedInteger(std::wstring_view text, int minimum, int maximum) noexcept {
    return ParseBoundedIntegerImpl(text, minimum, maximum);
}

std::optional<int> ParseBoundedInteger(std::string_view text, int minimum, int maximum) noexcept {
    return ParseBoundedIntegerImpl(text, minimum, maximum);
}

bool AreMetadataValuesValid(int rating, int colorLabel, int pickState) noexcept {
    return rating >= 0 && rating <= 5 && colorLabel >= 0 && colorLabel <= 5 &&
        pickState >= -1 && pickState <= 1;
}

std::optional<int> ParseKnownColorLabel(std::wstring_view text) noexcept {
    return ParseKnownColorLabelImpl(text);
}

std::optional<int> ParseKnownColorLabel(std::string_view text) noexcept {
    return ParseKnownColorLabelImpl(text);
}

int ParseColorLabel(std::wstring_view text) noexcept {
    return ParseKnownColorLabelImpl(text).value_or(0);
}

int ParseColorLabel(std::string_view text) noexcept {
    return ParseKnownColorLabelImpl(text).value_or(0);
}

} // namespace quicksift::metadata
