// OWNER: Collision-rejecting catalog mutation and transactional scanner-batch commits.
#include "catalog_store.h"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <type_traits>
#include <utility>

namespace quicksift::core {
namespace {
std::wstring CatalogPathKey(const std::filesystem::path& path) {
    std::wstring key = path.lexically_normal().wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(character)));
    });
    return key;
}
} // namespace

CatalogStore::Index CatalogStore::Append(PhotoItem item) {
    const std::wstring path = item.path.wstring();
    if (path.empty()) throw std::invalid_argument("catalog photo path must not be empty");
    if (PathOwnedByOther(path)) {
        throw std::invalid_argument("catalog path already belongs to another photo");
    }
    const Index index = photos_.size();
    photos_.push_back(std::move(item));
    try { InsertPath(index, path); }
    catch (...) { photos_.pop_back(); throw; }
    return index;
}

std::vector<CatalogStore::Index> CatalogStore::AppendBatch(
    std::span<const PhotoItem> items) {
    std::vector<Index> indexes;
    indexes.reserve(items.size());
    if (items.empty()) return indexes;

    std::unordered_set<std::wstring> batchPaths;
    batchPaths.reserve(items.size() * 2 + 1);
    std::vector<std::wstring> pathTexts;
    pathTexts.reserve(items.size());
    for (const PhotoItem& item : items) {
        std::wstring pathText = item.path.wstring();
        const std::wstring key = CatalogPathKey(item.path);
        if (key.empty()) throw std::invalid_argument("catalog photo path must not be empty");
        if (PathOwnedByOther(pathText) || !batchPaths.insert(key).second) {
            throw std::invalid_argument("catalog batch contains a duplicate path");
        }
        pathTexts.push_back(std::move(pathText));
    }

    const Index originalSize = photos_.size();
    photos_.reserve(originalSize + items.size());
    try {
        for (std::size_t offset = 0; offset < items.size(); ++offset) {
            const Index index = photos_.size();
            photos_.push_back(items[offset]);
            InsertPath(index, pathTexts[offset]);
            indexes.push_back(index);
        }
    } catch (...) {
        for (Index index = photos_.size(); index-- > originalSize;) {
            RemovePath(index, pathTexts[index - originalSize]);
        }
        photos_.resize(originalSize);
        throw;
    }
    return indexes;
}

std::vector<CatalogStore::Index> CatalogStore::AppendBatchWithVisibleOrder(
    std::span<const PhotoItem> items, std::vector<Index> order) {
    if (items.size() > std::numeric_limits<Index>::max() - photos_.size()) {
        throw std::length_error("catalog batch exceeds addressable size");
    }
    const Index prospectiveCount = photos_.size() + items.size();
    std::vector<Index> positions(prospectiveCount, InvalidVisiblePosition);
    for (Index visible = 0; visible < order.size(); ++visible) {
        const Index catalog = order[visible];
        if (catalog >= prospectiveCount) {
            throw std::out_of_range("visible catalog index exceeds prospective photo count");
        }
        if (positions[catalog] != InvalidVisiblePosition) {
            throw std::invalid_argument("visible catalog order contains a duplicate index");
        }
        positions[catalog] = visible;
    }
    std::vector<Index> indexes = AppendBatch(items);
    visibleIndices_.swap(order);
    visiblePositions_.swap(positions);
    return indexes;
}

void CatalogStore::EditPhoto(Index index,
    const std::function<void(PhotoItem&)>& edit) {
    if (!edit) return;
    static_assert(std::is_nothrow_move_assignable_v<PhotoItem>);
    PhotoItem& item = photos_.at(index);
    PhotoItem candidate = item;
    edit(candidate);
    if (!PathsEqualInsensitive(candidate.path.wstring(), item.path.wstring())) {
        throw std::logic_error("catalog path identity must be changed through ReplacePath");
    }
    item = std::move(candidate);
}

void CatalogStore::ReplacePath(Index index, std::filesystem::path path) {
    PhotoItem& item = photos_.at(index);
    const std::wstring replacementText = path.wstring();
    if (replacementText.empty()) throw std::invalid_argument("catalog path must not be empty");
    const std::wstring oldPath = item.path.wstring();
    if (PathsEqualInsensitive(oldPath, replacementText)) {
        item.path = std::move(path);
        return;
    }
    if (PathOwnedByOther(replacementText, index)) {
        throw std::invalid_argument("replacement path already belongs to another photo");
    }

    const std::filesystem::path replacement = std::move(path);
    const std::uint64_t fingerprint = PathFingerprint(replacementText);
    pathIndex_.emplace(fingerprint, index);
    try { item.path = replacement; }
    catch (...) {
        const auto [first, last] = pathIndex_.equal_range(fingerprint);
        for (auto iterator = first; iterator != last; ++iterator) {
            if (iterator->second == index) { pathIndex_.erase(iterator); break; }
        }
        throw;
    }
    RemovePath(index, oldPath);
}

} // namespace quicksift::core
