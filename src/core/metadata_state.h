// OWNER: Authoritative metadata knowledge, field patches, and conflict comparison.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace quicksift::core {

enum class MetadataKnowledge : std::uint8_t {
    Unknown = 0,
    Known = 1,
    Unreadable = 2,
    Unsupported = 3,
};

enum class MetadataFieldId : std::uint8_t {
    Rating,
    ColorLabel,
    PickState,
};

enum class MetadataStorageKind : std::uint8_t {
    Sidecar,
    Embedded,
};

struct MetadataFieldValue {
    MetadataKnowledge knowledge = MetadataKnowledge::Unknown;
    int value = 0;
    std::wstring rawValue;

    [[nodiscard]] bool IsKnown() const noexcept {
        return knowledge == MetadataKnowledge::Known;
    }
    [[nodiscard]] bool IsAuthoritative() const noexcept {
        return knowledge == MetadataKnowledge::Known ||
            knowledge == MetadataKnowledge::Unsupported;
    }
};

struct PhotoMetadataValues {
    MetadataFieldValue rating;
    MetadataFieldValue colorLabel;
    MetadataFieldValue pickState;
};

struct MetadataPatchField {
    bool active = false;
    bool requireExpected = false;
    MetadataFieldValue expected;
    int desired = 0;
};

struct MetadataPatch {
    MetadataPatchField rating;
    MetadataPatchField colorLabel;
    MetadataPatchField pickState;

    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] std::size_t ActiveCount() const noexcept;
};

[[nodiscard]] MetadataFieldValue KnownMetadataValue(int value);
[[nodiscard]] MetadataFieldValue UnknownMetadataValue() noexcept;
[[nodiscard]] MetadataFieldValue UnreadableMetadataValue() noexcept;
[[nodiscard]] MetadataFieldValue UnsupportedMetadataValue(std::wstring rawValue);
[[nodiscard]] bool MetadataFieldEquivalent(const MetadataFieldValue& left,
    const MetadataFieldValue& right) noexcept;
[[nodiscard]] MetadataFieldValue MetadataFieldFor(const PhotoMetadataValues& values,
    MetadataFieldId field);
void SetMetadataField(PhotoMetadataValues& values, MetadataFieldId field,
    MetadataFieldValue value);
[[nodiscard]] bool MetadataExpectedMatches(const PhotoMetadataValues& current,
    const MetadataPatch& patch, MetadataFieldId* conflictingField = nullptr) noexcept;
[[nodiscard]] PhotoMetadataValues ApplyMetadataPatch(const PhotoMetadataValues& current,
    const MetadataPatch& patch);
[[nodiscard]] MetadataPatch MakeSingleFieldPatch(MetadataFieldId field,
    int desired, const std::optional<MetadataFieldValue>& expected = std::nullopt);
[[nodiscard]] MetadataPatch MakeReplayPatch(const PhotoMetadataValues& expected,
    const PhotoMetadataValues& desired);
[[nodiscard]] bool MetadataValuesEquivalent(const PhotoMetadataValues& left,
    const PhotoMetadataValues& right) noexcept;
[[nodiscard]] const wchar_t* MetadataFieldDisplayName(MetadataFieldId field) noexcept;

} // namespace quicksift::core
