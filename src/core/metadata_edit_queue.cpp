// OWNER: Coalescing interactive metadata edit queue (see metadata_edit_queue.h).
#include "core/metadata_edit_queue.h"

#include <algorithm>
#include <cwctype>

namespace quicksift::core {
namespace {
bool SamePath(const std::filesystem::path& leftPath, const std::filesystem::path& rightPath) {
    const std::wstring left = leftPath.wstring();
    const std::wstring right = rightPath.wstring();
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
        [](wchar_t a, wchar_t b) {
            return std::towlower(static_cast<wint_t>(a)) == std::towlower(static_cast<wint_t>(b));
        });
}

MetadataPatchField& FieldOf(MetadataPatch& patch, MetadataFieldId field) noexcept {
    switch (field) {
    case MetadataFieldId::Rating: return patch.rating;
    case MetadataFieldId::ColorLabel: return patch.colorLabel;
    case MetadataFieldId::PickState: return patch.pickState;
    }
    return patch.rating;
}
} // namespace

void SetCoalescedField(MetadataPatch& patch, const PhotoMetadataValues& before,
    MetadataFieldId field, int value) {
    MetadataPatchField& output = FieldOf(patch, field);
    const MetadataFieldValue current = MetadataFieldFor(before, field);
    if (current.IsKnown() && current.value == value) {
        output = MetadataPatchField{}; // reverted to the on-disk value: nothing to write
        return;
    }
    output.active = true;
    output.desired = value;
    output.requireExpected = current.IsAuthoritative();
    output.expected = current.IsAuthoritative() ? current : MetadataFieldValue{};
}

std::vector<PendingMetadataEdit>::iterator PendingMetadataEdits::FindEntry(
    const std::filesystem::path& path) {
    return std::find_if(entries_.begin(), entries_.end(),
        [&](const PendingMetadataEdit& entry) { return SamePath(entry.before.path, path); });
}

const PendingMetadataEdit* PendingMetadataEdits::Find(const std::filesystem::path& path) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
        [&](const PendingMetadataEdit& entry) { return SamePath(entry.before.path, path); });
    return found == entries_.end() ? nullptr : &*found;
}

PhotoMetadataValues PendingMetadataEdits::Record(const MetadataSnapshot& before,
    MetadataFieldId field, int value) {
    auto found = FindEntry(before.path);
    if (found == entries_.end()) {
        PendingMetadataEdit entry;
        entry.before = before;
        entry.sequence = nextSequence_++;
        entries_.push_back(std::move(entry));
        found = std::prev(entries_.end());
    }
    if (std::find(found->fields.begin(), found->fields.end(), field) == found->fields.end())
        found->fields.push_back(field);
    SetCoalescedField(found->patch, found->before.values, field, value);
    const PhotoMetadataValues shown = ApplyMetadataPatch(found->before.values, found->patch);
    if (found->patch.Empty()) entries_.erase(found);
    return shown;
}

std::vector<PendingMetadataEdit> PendingMetadataEdits::TakeAll() {
    std::vector<PendingMetadataEdit> output;
    output.swap(entries_);
    return output;
}

std::optional<PendingMetadataEdit> PendingMetadataEdits::RemovePath(
    const std::filesystem::path& path) {
    const auto found = FindEntry(path);
    if (found == entries_.end()) return std::nullopt;
    PendingMetadataEdit entry = std::move(*found);
    entries_.erase(found);
    return entry;
}

void PendingMetadataEdits::Requeue(std::vector<PendingMetadataEdit> entries) {
    std::vector<PendingMetadataEdit> merged;
    merged.reserve(entries.size() + entries_.size());
    for (PendingMetadataEdit& older : entries) {
        const auto newer = FindEntry(older.before.path);
        if (newer != entries_.end()) {
            // Newer values win; the base (expected on-disk values) is the older one.
            MetadataPatch combined = older.patch;
            for (const MetadataFieldId field : { MetadataFieldId::Rating,
                    MetadataFieldId::ColorLabel, MetadataFieldId::PickState }) {
                const MetadataPatchField latest = FieldOf(newer->patch, field);
                const bool touched = std::find(newer->fields.begin(), newer->fields.end(), field) !=
                    newer->fields.end();
                if (!touched) continue;
                // Desired value of the newer entry (inactive = equal to its base).
                const int desired = latest.active ? latest.desired :
                    MetadataFieldFor(newer->before.values, field).value;
                SetCoalescedField(combined, older.before.values, field, desired);
                if (std::find(older.fields.begin(), older.fields.end(), field) == older.fields.end())
                    older.fields.push_back(field);
            }
            older.patch = combined;
            entries_.erase(newer);
        }
        if (!older.patch.Empty()) merged.push_back(std::move(older));
    }
    for (PendingMetadataEdit& remaining : entries_) merged.push_back(std::move(remaining));
    entries_ = std::move(merged);
}

bool PendingMetadataEdits::Rebase(const std::filesystem::path& path,
    const PhotoMetadataValues& values) {
    const auto found = FindEntry(path);
    if (found == entries_.end()) return false;
    MetadataPatch previous = found->patch;
    found->before.values = values;
    found->patch = MetadataPatch{};
    for (const MetadataFieldId field : { MetadataFieldId::Rating,
            MetadataFieldId::ColorLabel, MetadataFieldId::PickState }) {
        const MetadataPatchField old = FieldOf(previous, field);
        if (old.active) SetCoalescedField(found->patch, values, field, old.desired);
    }
    if (found->patch.Empty()) {
        entries_.erase(found);
        return false;
    }
    return true;
}

} // namespace quicksift::core
