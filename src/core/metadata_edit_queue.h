// OWNER: Coalescing queue for interactive metadata edits (rating / pick / color label)
// typed while the metadata write lane is busy. Portable; UI-thread owned.
//
// Rules (checked by scripts/checks/write_lane_checks.cpp):
//   * one entry per file (case-insensitive path); entries keep first-edit order;
//   * `before` is captured at the first edit and is the CAS expectation for the write;
//   * last value wins per field; a field set back to its `before` value becomes
//     inactive, and an entry with no active field is dropped (nothing to write);
//   * Rebase() moves an entry onto the values a just-finished write produced (the
//     entry was queued while that same file was being written);
//   * RemovePath() drops an entry whose earlier write failed (its base is unknown).
// A flushed batch is one transaction and therefore one Undo step.
#pragma once

#include "core/history_store.h"
#include "core/metadata_state.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace quicksift::core {

struct PendingMetadataEdit {
    MetadataSnapshot before;
    MetadataPatch patch;
    std::uint64_t sequence = 0;
    // Fields touched since the entry was created, in first-touch order (for labels).
    std::vector<MetadataFieldId> fields;
};

class PendingMetadataEdits {
public:
    // Records `value` for `field`. `before` is used only when the path has no entry.
    // Returns the values the UI should display for the photo after this edit.
    PhotoMetadataValues Record(const MetadataSnapshot& before, MetadataFieldId field, int value);
    [[nodiscard]] bool Empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }
    [[nodiscard]] const PendingMetadataEdit* Find(const std::filesystem::path& path) const;
    // All entries in first-edit order; the queue becomes empty.
    [[nodiscard]] std::vector<PendingMetadataEdit> TakeAll();
    std::optional<PendingMetadataEdit> RemovePath(const std::filesystem::path& path);
    // Puts entries taken by TakeAll() back (their submit was refused). They keep
    // their place ahead of newer entries; a newer edit of the same file keeps its
    // values but adopts the older (on-disk) base so its CAS expectation stays true.
    void Requeue(std::vector<PendingMetadataEdit> entries);
    // The entry's file now holds `values` (a write finished). Fields whose desired
    // value equals the new base become inactive. Returns false when the entry vanished.
    bool Rebase(const std::filesystem::path& path, const PhotoMetadataValues& values);
    void Clear() noexcept { entries_.clear(); }

private:
    std::vector<PendingMetadataEdit>::iterator FindEntry(const std::filesystem::path& path);
    std::vector<PendingMetadataEdit> entries_; // kept in sequence order
    std::uint64_t nextSequence_ = 1;
};

// Shared rule for one field: expected value + inactive-when-reverted.
void SetCoalescedField(MetadataPatch& patch, const PhotoMetadataValues& before,
    MetadataFieldId field, int value);

} // namespace quicksift::core
