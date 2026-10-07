// OWNER: Catalog path lookup and atomic visible-order commits.
#include "catalog_store.h"

#include <stdexcept>
#include <utility>

namespace quicksift::core {

void CatalogStore::ClearAndRelease() {
    std::vector<PhotoItem>().swap(photos_);
    std::unordered_multimap<std::uint64_t, Index>().swap(pathIndex_);
    std::vector<Index>().swap(visibleIndices_);
    std::vector<Index>().swap(visiblePositions_);
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
