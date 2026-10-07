// OWNER: History memory accounting, estimation, and bounded retention policy.
#include "history_store.h"

#include <limits>

namespace quicksift::core {

std::size_t HistoryStore::SaturatingAdd(
    std::size_t left, std::size_t right) noexcept {
    return right > std::numeric_limits<std::size_t>::max() - left ?
        std::numeric_limits<std::size_t>::max() : left + right;
}

std::size_t HistoryStore::SaturatingMultiply(
    std::size_t left, std::size_t right) noexcept {
    if (left == 0 || right == 0) return 0;
    return left > std::numeric_limits<std::size_t>::max() / right ?
        std::numeric_limits<std::size_t>::max() : left * right;
}

std::size_t HistoryStore::EstimateEntryBytes(const HistoryEntry& entry) {
    const auto wideBytes = [](std::size_t characters) {
        return SaturatingMultiply(characters, sizeof(wchar_t));
    };
    const auto stringBytes = [&](const std::wstring& value) {
        return SaturatingAdd(sizeof(std::wstring), wideBytes(value.capacity()));
    };
    const auto pathBytes = [&](const std::filesystem::path& value) {
        return SaturatingAdd(sizeof(std::filesystem::path),
            wideBytes(value.native().size()));
    };

    std::size_t total = SaturatingAdd(sizeof(HistoryEntry), stringBytes(entry.label));
    total = SaturatingAdd(total,
        SaturatingMultiply(entry.before.capacity(), sizeof(MetadataSnapshot)));
    total = SaturatingAdd(total,
        SaturatingMultiply(entry.after.capacity(), sizeof(MetadataSnapshot)));
    total = SaturatingAdd(total,
        SaturatingMultiply(entry.files.capacity(), sizeof(FileHistoryItem)));
    const auto metadataValueBytes = [&](const PhotoMetadataValues& values) {
        std::size_t bytes = stringBytes(values.rating.rawValue);
        bytes = SaturatingAdd(bytes, stringBytes(values.colorLabel.rawValue));
        return SaturatingAdd(bytes, stringBytes(values.pickState.rawValue));
    };
    for (const MetadataSnapshot& snapshot : entry.before) {
        total = SaturatingAdd(total, pathBytes(snapshot.path));
        total = SaturatingAdd(total, metadataValueBytes(snapshot.values));
    }
    for (const MetadataSnapshot& snapshot : entry.after) {
        total = SaturatingAdd(total, pathBytes(snapshot.path));
        total = SaturatingAdd(total, metadataValueBytes(snapshot.values));
    }
    for (const FileHistoryItem& file : entry.files) {
        total = SaturatingAdd(total, pathBytes(file.source));
        total = SaturatingAdd(total, pathBytes(file.destination));
    }
    return total;
}

std::size_t HistoryStore::EstimateMetadataBytesForTargets(
    std::span<const std::filesystem::path> targets, std::wstring_view label) {
    std::size_t total = SaturatingAdd(sizeof(HistoryEntry), sizeof(std::wstring));
    total = SaturatingAdd(total, SaturatingMultiply(label.size(), sizeof(wchar_t)));
    total = SaturatingAdd(total, SaturatingMultiply(
        SaturatingMultiply(targets.size(), sizeof(MetadataSnapshot)), 2));
    constexpr std::size_t kRawMetadataCharactersPerField = 256;
    constexpr std::size_t kRawFieldsAcrossBeforeAndAfter = 6;
    total = SaturatingAdd(total, SaturatingMultiply(targets.size(),
        SaturatingMultiply(kRawFieldsAcrossBeforeAndAfter,
            SaturatingMultiply(kRawMetadataCharactersPerField, sizeof(wchar_t)))));
    for (const auto& path : targets) {
        const std::size_t characters = SaturatingAdd(path.native().size(), 1);
        const std::size_t bytes = SaturatingAdd(sizeof(std::filesystem::path),
            SaturatingMultiply(characters, sizeof(wchar_t)));
        total = SaturatingAdd(total, bytes);
        total = SaturatingAdd(total, bytes);
    }
    return total;
}

std::size_t HistoryStore::EstimateFileBytesForPathLengths(
    std::span<const std::pair<std::size_t, std::size_t>> pathLengths,
    std::wstring_view label) {
    std::size_t total = SaturatingAdd(sizeof(HistoryEntry), sizeof(std::wstring));
    total = SaturatingAdd(total, SaturatingMultiply(
        SaturatingAdd(label.size(), 1), sizeof(wchar_t)));
    total = SaturatingAdd(total,
        SaturatingMultiply(pathLengths.size(), sizeof(FileHistoryItem)));
    for (const auto& [sourceCharacters, destinationCharacters] : pathLengths) {
        const std::size_t characters = SaturatingAdd(
            SaturatingAdd(sourceCharacters, destinationCharacters), 2);
        total = SaturatingAdd(total,
            SaturatingMultiply(characters, sizeof(wchar_t)));
    }
    return total;
}

void HistoryStore::Trim() noexcept {
    std::size_t bytes = 0;
    for (const auto& entry : undo_) bytes = SaturatingAdd(bytes, entry.estimatedBytes);
    for (const auto& entry : redo_) bytes = SaturatingAdd(bytes, entry.estimatedBytes);

    while (bytes > memoryLimit_ && (!undo_.empty() || !redo_.empty())) {
        // Prefer dropping the oldest Undo entry. If only Redo remains, drop its
        // oldest entry too. The configured cap is absolute; retaining one giant
        // exception makes the advertised memory bound meaningless.
        if (!undo_.empty()) {
            const std::size_t removed = undo_.front().estimatedBytes;
            undo_.erase(undo_.begin());
            bytes = removed > bytes ? 0 : bytes - removed;
        } else {
            const std::size_t removed = redo_.front().estimatedBytes;
            redo_.erase(redo_.begin());
            bytes = removed > bytes ? 0 : bytes - removed;
        }
    }
}

} // namespace quicksift::core
