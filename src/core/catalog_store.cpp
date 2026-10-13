// OWNER: Catalog path lookup and atomic visible-order commits.
#include "catalog_store.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace quicksift::core {

void CatalogStore::ClearAndRelease() {
    std::vector<PhotoItem>().swap(photos_);
    BumpMetadataEpoch();
    snapshot_.reset();
    std::vector<std::shared_ptr<const CatalogSnapshot::Chunk>>().swap(snapshotChunks_);
    std::vector<std::uint8_t>().swap(snapshotChunkDirty_);
    std::unordered_multimap<std::uint64_t, Index>().swap(pathIndex_);
    std::vector<Index>().swap(visibleIndices_);
    std::vector<Index>().swap(visiblePositions_);
}

void CatalogStore::MarkFactsChanged(Index index) noexcept {
    BumpMetadataEpoch();
    const std::size_t chunk = index / CatalogSnapshot::kChunkSize;
    // Chunks past the end are new and always built; nothing to mark.
    if (chunk < snapshotChunkDirty_.size()) snapshotChunkDirty_[chunk] = 1;
}

CatalogSnapshotPtr CatalogStore::Snapshot() const {
    if (snapshot_ && snapshot_->epoch == metadataEpoch_) return snapshot_;
    constexpr std::size_t kChunk = CatalogSnapshot::kChunkSize;
    const std::size_t chunkCount = (photos_.size() + kChunk - 1) / kChunk;
    // Build into locals first: a throwing allocation leaves the cached state intact.
    std::vector<std::shared_ptr<const CatalogSnapshot::Chunk>> chunks(chunkCount);
    for (std::size_t chunk = 0; chunk < chunkCount; ++chunk) {
        const std::size_t first = chunk * kChunk;
        const std::size_t count = std::min(kChunk, photos_.size() - first);
        const bool reusable = chunk < snapshotChunks_.size() && snapshotChunks_[chunk] &&
            snapshotChunks_[chunk]->size() == count &&
            (chunk >= snapshotChunkDirty_.size() || snapshotChunkDirty_[chunk] == 0);
        if (reusable) {
            chunks[chunk] = snapshotChunks_[chunk];
            continue;
        }
        auto built = std::make_shared<CatalogSnapshot::Chunk>();
        built->reserve(count);
        for (std::size_t offset = 0; offset < count; ++offset)
            built->push_back(RecordOf(photos_[first + offset], first + offset));
        chunks[chunk] = std::move(built);
    }
    auto snapshot = std::make_shared<CatalogSnapshot>();
    snapshot->epoch = metadataEpoch_;
    snapshot->size = photos_.size();
    snapshot->chunks = chunks;
    std::vector<std::uint8_t> clean(chunkCount, 0);
    snapshotChunks_ = std::move(chunks);
    snapshotChunkDirty_ = std::move(clean);
    snapshot_ = std::move(snapshot);
    return snapshot_;
}

bool CatalogStore::PathOwnedByOther(std::wstring_view path,
    std::optional<Index> excluded) const {
    if (path.empty()) return false;
    const auto [first, last] = pathIndex_.equal_range(PathFingerprint(path));
    for (auto iterator = first; iterator != last; ++iterator) {
        const Index existing = iterator->second;
        if (excluded && existing == *excluded) continue;
        if (existing < photos_.size() &&
            PathsEqualInsensitive(photos_[existing].path.wstring(), path)) return true;
    }
    return false;
}

std::optional<CatalogStore::Index> CatalogStore::FindPath(std::wstring_view path) const {
    if (path.empty()) return std::nullopt;
    const auto [first, last] = pathIndex_.equal_range(PathFingerprint(path));
    for (auto iterator = first; iterator != last; ++iterator) {
        const Index index = iterator->second;
        if (index < photos_.size() &&
            PathsEqualInsensitive(photos_[index].path.wstring(), path)) {
            return index;
        }
    }
    return std::nullopt;
}

void CatalogStore::SetVisibleOrder(std::vector<Index> order) {
    std::vector<Index> positions(photos_.size(), InvalidVisiblePosition);
    for (Index visible = 0; visible < order.size(); ++visible) {
        const Index catalog = order[visible];
        if (catalog >= photos_.size()) {
            throw std::out_of_range("visible catalog index exceeds photo count");
        }
        if (positions[catalog] != InvalidVisiblePosition) {
            throw std::invalid_argument("visible catalog order contains a duplicate index");
        }
        positions[catalog] = visible;
    }
    visibleIndices_.swap(order);
    visiblePositions_.swap(positions);
}

bool CatalogStore::ValidateVisibleOrder() const noexcept {
    if (visiblePositions_.size() != photos_.size()) return false;
    for (Index visible = 0; visible < visibleIndices_.size(); ++visible) {
        const Index catalog = visibleIndices_[visible];
        if (catalog >= photos_.size() || visiblePositions_[catalog] != visible) return false;
    }
    for (Index catalog = 0; catalog < visiblePositions_.size(); ++catalog) {
        const Index visible = visiblePositions_[catalog];
        if (visible == InvalidVisiblePosition) continue;
        if (visible >= visibleIndices_.size() || visibleIndices_[visible] != catalog) return false;
    }
    return true;
}

void CatalogStore::InsertPath(Index index, std::wstring_view path) {
    if (PathOwnedByOther(path, index)) {
        throw std::invalid_argument("catalog path collision");
    }
    pathIndex_.emplace(PathFingerprint(path), index);
}

void CatalogStore::RemovePath(Index index, std::wstring_view oldPath) noexcept {
    const auto [first, last] = pathIndex_.equal_range(PathFingerprint(oldPath));
    for (auto iterator = first; iterator != last; ++iterator) {
        if (iterator->second == index) {
            pathIndex_.erase(iterator);
            return;
        }
    }
}

} // namespace quicksift::core
