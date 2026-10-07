// CODE GUIDE: See CODE_GUIDE.md -> "Reading metadata".
// OWNER: Narrow safe conversion of metadata text; malformed values are rejected, not guessed.

#pragma once

#include <optional>
#include <string_view>

namespace quicksift::metadata {

// Parses an entire trimmed decimal value. Partial values such as "3 stars" are
// rejected so malformed XMP cannot silently become valid metadata.
std::optional<int> ParseBoundedInteger(std::wstring_view text, int minimum, int maximum) noexcept;
std::optional<int> ParseBoundedInteger(std::string_view text, int minimum, int maximum) noexcept;

// Returns true only for values QuickSift can persist without changing their
// meaning. Write paths reject invalid values instead of silently clamping them.
bool AreMetadataValuesValid(int rating, int colorLabel, int pickState) noexcept;

// Maps the standard Adobe color-label names after trimming surrounding XML
// whitespace. The optional form preserves the difference between an explicit
// empty/known label and a present custom value QuickSift must not erase.
std::optional<int> ParseKnownColorLabel(std::wstring_view text) noexcept;
std::optional<int> ParseKnownColorLabel(std::string_view text) noexcept;
int ParseColorLabel(std::wstring_view text) noexcept;
int ParseColorLabel(std::string_view text) noexcept;

} // namespace quicksift::metadata
