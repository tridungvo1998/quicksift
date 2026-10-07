// OWNER: Metadata knowledge and patch implementation.
#include "metadata_state.h"

#include <utility>

namespace quicksift::core {
namespace {

const MetadataPatchField& PatchFieldFor(const MetadataPatch& patch,
    MetadataFieldId field) noexcept {
    switch (field) {
    case MetadataFieldId::Rating: return patch.rating;
    case MetadataFieldId::ColorLabel: return patch.colorLabel;
    case MetadataFieldId::PickState: return patch.pickState;
    }
    return patch.rating;
}

MetadataPatchField& PatchFieldFor(MetadataPatch& patch,
    MetadataFieldId field) noexcept {
    switch (field) {
    case MetadataFieldId::Rating: return patch.rating;
    case MetadataFieldId::ColorLabel: return patch.colorLabel;
    case MetadataFieldId::PickState: return patch.pickState;
    }
    return patch.rating;
}

} // namespace

bool MetadataPatch::Empty() const noexcept {
    return !rating.active && !colorLabel.active && !pickState.active;
}

std::size_t MetadataPatch::ActiveCount() const noexcept {
    return static_cast<std::size_t>(rating.active) +
        static_cast<std::size_t>(colorLabel.active) +
        static_cast<std::size_t>(pickState.active);
}

MetadataFieldValue KnownMetadataValue(int value) {
    MetadataFieldValue result;
    result.knowledge = MetadataKnowledge::Known;
    result.value = value;
    return result;
}

MetadataFieldValue UnknownMetadataValue() noexcept {
    return {};
}

MetadataFieldValue UnreadableMetadataValue() noexcept {
    MetadataFieldValue result;
    result.knowledge = MetadataKnowledge::Unreadable;
    return result;
}

MetadataFieldValue UnsupportedMetadataValue(std::wstring rawValue) {
    MetadataFieldValue result;
    result.knowledge = MetadataKnowledge::Unsupported;
    result.rawValue = std::move(rawValue);
    return result;
}

bool MetadataFieldEquivalent(const MetadataFieldValue& left,
    const MetadataFieldValue& right) noexcept {
    if (left.knowledge != right.knowledge) return false;
    if (left.knowledge == MetadataKnowledge::Known) return left.value == right.value;
    if (left.knowledge == MetadataKnowledge::Unsupported)
        return left.rawValue == right.rawValue;
    return true;
}

MetadataFieldValue MetadataFieldFor(const PhotoMetadataValues& values,
    MetadataFieldId field) {
    switch (field) {
    case MetadataFieldId::Rating: return values.rating;
    case MetadataFieldId::ColorLabel: return values.colorLabel;
    case MetadataFieldId::PickState: return values.pickState;
    }
    return values.rating;
}

void SetMetadataField(PhotoMetadataValues& values, MetadataFieldId field,
    MetadataFieldValue value) {
    switch (field) {
    case MetadataFieldId::Rating: values.rating = std::move(value); break;
    case MetadataFieldId::ColorLabel: values.colorLabel = std::move(value); break;
    case MetadataFieldId::PickState: values.pickState = std::move(value); break;
    }
}

bool MetadataExpectedMatches(const PhotoMetadataValues& current,
    const MetadataPatch& patch, MetadataFieldId* conflictingField) noexcept {
    for (const MetadataFieldId field : { MetadataFieldId::Rating,
            MetadataFieldId::ColorLabel, MetadataFieldId::PickState }) {
        const MetadataPatchField& candidate = PatchFieldFor(patch, field);
        if (!candidate.active || !candidate.requireExpected) continue;
        const MetadataFieldValue actual = MetadataFieldFor(current, field);
        if (!MetadataFieldEquivalent(actual, candidate.expected)) {
            if (conflictingField) *conflictingField = field;
            return false;
        }
    }
    return true;
}

PhotoMetadataValues ApplyMetadataPatch(const PhotoMetadataValues& current,
    const MetadataPatch& patch) {
    PhotoMetadataValues result = current;
    for (const MetadataFieldId field : { MetadataFieldId::Rating,
            MetadataFieldId::ColorLabel, MetadataFieldId::PickState }) {
        const MetadataPatchField& candidate = PatchFieldFor(patch, field);
        if (candidate.active) SetMetadataField(result, field,
            KnownMetadataValue(candidate.desired));
    }
    return result;
}

MetadataPatch MakeSingleFieldPatch(MetadataFieldId field, int desired,
    const std::optional<MetadataFieldValue>& expected) {
    MetadataPatch patch;
    MetadataPatchField& output = PatchFieldFor(patch, field);
    output.active = true;
    output.desired = desired;
    if (expected) {
        output.requireExpected = true;
        output.expected = *expected;
    }
    return patch;
}

MetadataPatch MakeReplayPatch(const PhotoMetadataValues& expected,
    const PhotoMetadataValues& desired) {
    MetadataPatch patch;
    for (const MetadataFieldId field : { MetadataFieldId::Rating,
            MetadataFieldId::ColorLabel, MetadataFieldId::PickState }) {
        const MetadataFieldValue expectedValue = MetadataFieldFor(expected, field);
        const MetadataFieldValue desiredValue = MetadataFieldFor(desired, field);
        if (MetadataFieldEquivalent(expectedValue, desiredValue)) continue;
        // History records only QuickSift-supported changes. If a corrupt history
        // entry contains a non-known destination, leave that field inactive so a
        // replay can never synthesize or erase an unsupported raw value.
        if (!desiredValue.IsKnown()) continue;
        MetadataPatchField& output = PatchFieldFor(patch, field);
        output.active = true;
        output.requireExpected = true;
        output.expected = expectedValue;
        output.desired = desiredValue.value;
    }
    return patch;
}

bool MetadataValuesEquivalent(const PhotoMetadataValues& left,
    const PhotoMetadataValues& right) noexcept {
    return MetadataFieldEquivalent(left.rating, right.rating) &&
        MetadataFieldEquivalent(left.colorLabel, right.colorLabel) &&
        MetadataFieldEquivalent(left.pickState, right.pickState);
}

const wchar_t* MetadataFieldDisplayName(MetadataFieldId field) noexcept {
    switch (field) {
    case MetadataFieldId::Rating: return L"rating";
    case MetadataFieldId::ColorLabel: return L"color label";
    case MetadataFieldId::PickState: return L"pick state";
    }
    return L"metadata field";
}

} // namespace quicksift::core
