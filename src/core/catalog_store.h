// OWNER: Authoritative catalog records, path lookup, and validated visible ordering.
#pragma once

#include "app_types.h"
#include "path_identity.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace quicksift::core {

class CatalogStore {
public:
    using Index = std::size_t;
    static constexpr Index InvalidVisiblePosition =
        std::numeric_limits<Index>::max();

    CatalogStore() = default;
    CatalogStore(const CatalogStore&) = delete;
    CatalogStore& operator=(const CatalogStore&) = delete;

    [[nodiscard]] Index PhotoCount() const noexcept { return photos_.size(); }
    [[nodiscard]] bool Empty() const noexcept { return photos_.empty(); }
    [[nodiscard]] const PhotoItem& PhotoAt(Index index) const { return photos_.at(index); }

    // Mutates non-identity fields while enforcing that path identity cannot be
    // changed through a general record callback.
    void EditPhoto(Index index, const std::function<void(PhotoItem&)>& edit);
    [[nodiscard]] std::span<const PhotoItem> Photos() const noexcept { return photos_; }

    [[nodiscard]] Index VisibleCount() const noexcept { return visibleIndices_.size(); }
    [[nodiscard]] bool VisibleEmpty() const noexcept { return visibleIndices_.empty(); }
    [[nodiscard]] Index VisibleCatalogIndex(Index visibleIndex) const {
        return visibleIndices_.at(visibleIndex);
    }
    [[nodiscard]] Index VisiblePosition(Index catalogIndex) const noexcept {
        return catalogIndex < visiblePositions_.size() ?
            visiblePositions_[catalogIndex] : InvalidVisiblePosition;
    }
    [[nodiscard]] std::span<const Index> VisibleOrder() const noexcept {
        return visibleIndices_;
    }
    [[nodiscard]] std::vector<Index> CopyVisibleOrder() const { return visibleIndices_; }

    void ClearAndRelease();
    [[nodiscard]] Index Append(PhotoItem item);
    // Appends a scanner batch transactionally. Existing records and the path
    // index remain unchanged if any path collides or an allocation fails.
    [[nodiscard]] std::vector<Index> AppendBatch(std::span<const PhotoItem> items);
    // Atomically appends a scanner batch and commits a visible order that may
    // reference the resulting tail indexes.
    [[nodiscard]] std::vector<Index> AppendBatchWithVisibleOrder(
        std::span<const PhotoItem> items, std::vector<Index> order);
    void ReplacePath(Index index, std::filesystem::path path);
    [[nodiscard]] std::optional<Index> FindPath(std::wstring_view path) const;

    // Commits visible order and its reverse map together. Invalid indexes or
    // duplicate entries are rejected before either authoritative vector changes.
    void SetVisibleOrder(std::vector<Index> order);
    [[nodiscard]] bool ValidateVisibleOrder() const noexcept;

private:
    [[nodiscard]] bool PathOwnedByOther(std::wstring_view path,
        std::optional<Index> excluded = std::nullopt) const;
    void InsertPath(Index index, std::wstring_view path);
    void RemovePath(Index index, std::wstring_view oldPath) noexcept;

    std::vector<PhotoItem> photos_;
    std::unordered_multimap<std::uint64_t, Index> pathIndex_;
    std::vector<Index> visibleIndices_;
    std::vector<Index> visiblePositions_;
};

} // namespace quicksift::core
