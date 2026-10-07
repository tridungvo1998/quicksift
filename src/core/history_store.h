// OWNER: Bounded Undo/Redo storage, replay transitions, and history record types.
#pragma once

#include "metadata_state.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quicksift::core {

struct MetadataSnapshot {
    std::filesystem::path path;
    PhotoMetadataValues values;
    std::uint64_t revision = 0;
    MetadataStorageKind storage = MetadataStorageKind::Sidecar;
    bool jpeg = false;
    bool safeJpegWrite = true;
};

struct FileHistoryItem {
    std::filesystem::path source;
    std::filesystem::path destination;
    bool copied = false;

    // Files that form one user-visible media unit share a non-zero group ID.
    // A zero ID is retained for compatibility and means “this item is its own
    // group”. Group ordering is preserved so replay can compensate safely.
    std::uint64_t groupId = 0;
    std::uint32_t groupOrder = 0;
    bool companion = false;

    std::uintmax_t destinationSize = 0;
    std::int64_t destinationStamp = 0;
    std::int64_t destinationChangeStamp = 0;
    std::uint64_t destinationContentFingerprint = 0;
    std::uintmax_t redoSourceSize = 0;
    std::int64_t redoSourceStamp = 0;
    std::int64_t redoSourceChangeStamp = 0;
    std::uint64_t redoSourceContentFingerprint = 0;
    bool redoSourceFingerprintValid = false;
};

struct HistoryEntry {
    enum class Kind { Metadata, Files } kind = Kind::Metadata;
    std::wstring label;
    std::vector<MetadataSnapshot> before;
    std::vector<MetadataSnapshot> after;
    std::vector<FileHistoryItem> files;
    std::size_t estimatedBytes = 0;
};

enum class HistoryDirection { Undo, Redo };

class HistoryStore {
public:
    explicit HistoryStore(std::size_t memoryLimit = 96ull * 1024ull * 1024ull)
        : memoryLimit_(memoryLimit) {}

    HistoryStore(const HistoryStore&) = delete;
    HistoryStore& operator=(const HistoryStore&) = delete;

    [[nodiscard]] bool CanUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool CanRedo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] std::size_t UndoCount() const noexcept { return undo_.size(); }
    [[nodiscard]] std::size_t RedoCount() const noexcept { return redo_.size(); }
    [[nodiscard]] const HistoryEntry* PeekUndo() const noexcept;
    [[nodiscard]] const HistoryEntry* PeekRedo() const noexcept;
    [[nodiscard]] std::optional<HistoryEntry> TakeUndo();
    [[nodiscard]] std::optional<HistoryEntry> TakeRedo();

    // Restores an entry removed for a submission that never started.
    void Restore(HistoryDirection direction, HistoryEntry entry);

    // Commits a partial/full replay without exposing either backing stack.
    // Completed work crosses to the opposite stack; untouched work stays on
    // the source stack so the user can retry it.
    void CompleteReplay(HistoryDirection direction,
        HistoryEntry completed, HistoryEntry remaining);

    void SetMemoryLimit(std::size_t bytes) noexcept {
        memoryLimit_ = bytes;
        Trim();
    }
    [[nodiscard]] std::size_t MemoryLimit() const noexcept { return memoryLimit_; }

    // Returns false when the entry is empty or exceeds the configured memory
    // limit. Oversized new work invalidates Redo but is deliberately not kept.
    [[nodiscard]] bool RecordNew(HistoryEntry entry);
    void ClearRedo() noexcept { redo_.clear(); }
    void Clear() noexcept { undo_.clear(); redo_.clear(); }

    [[nodiscard]] static std::size_t EstimateEntryBytes(const HistoryEntry& entry);
    [[nodiscard]] static std::size_t EstimateMetadataBytesForTargets(
        std::span<const std::filesystem::path> targets, std::wstring_view label);
    [[nodiscard]] static std::size_t EstimateFileBytesForPathLengths(
        std::span<const std::pair<std::size_t, std::size_t>> pathLengths,
        std::wstring_view label);
    void Trim() noexcept;

private:
    [[nodiscard]] static bool EmptyEntry(const HistoryEntry& entry) noexcept;
    static void PrepareEntry(HistoryEntry& entry);
    [[nodiscard]] static std::size_t SaturatingAdd(
        std::size_t left, std::size_t right) noexcept;
    [[nodiscard]] static std::size_t SaturatingMultiply(
        std::size_t left, std::size_t right) noexcept;

    std::vector<HistoryEntry> undo_;
    std::vector<HistoryEntry> redo_;
    std::size_t memoryLimit_;
};

} // namespace quicksift::core
