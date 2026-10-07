// OWNER: Deterministic thumbnail-grid geometry shared by rendering, input, and prefetch.
#include "review/thumbnail_layout.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace quicksift::review::thumbnail {

ThumbnailLayoutMetrics CalculateThumbnailLayout(float canvasWidth, float canvasHeight,
    int thumbnailSize, std::size_t itemCount, float leftInset) noexcept {
    ThumbnailLayoutMetrics result;
    result.canvasWidth = std::max(0.0f, canvasWidth);
    result.canvasHeight = std::max(0.0f, canvasHeight);
    result.leftInset = std::max(result.padding, leftInset);
    result.cellWidth = static_cast<float>(std::max(1, thumbnailSize) + 24);
    // Filename text requires a real 18-DIP line plus the badge lane.
    result.cellHeight = static_cast<float>(std::max(1, thumbnailSize) + 56);
    result.cardWidth = std::max(1.0f, result.cellWidth - 8.0f);
    result.cardHeight = std::max(1.0f, result.cellHeight - 8.0f);
    const float usableWidth = std::max(result.cellWidth,
        result.canvasWidth - result.leftInset - result.padding);
    const float rawColumns = usableWidth / result.cellWidth;
    result.columns = std::max(1, static_cast<int>(std::min(rawColumns,
        static_cast<float>(std::numeric_limits<int>::max()))));
    const std::size_t columnCount = static_cast<std::size_t>(result.columns);
    const std::size_t rowCount = itemCount == 0 ? 0 : 1 + (itemCount - 1) / columnCount;
    result.rows = static_cast<int>(std::min(rowCount,
        static_cast<std::size_t>(std::numeric_limits<int>::max())));
    result.contentHeight = result.padding * 2.0f + static_cast<float>(result.rows) * result.cellHeight;
    result.maximumScroll = std::max(0.0f, result.contentHeight - result.canvasHeight);
    return result;
}

ThumbnailRect ThumbnailCardRect(const ThumbnailLayoutMetrics& metrics, std::size_t index,
    float scrollOffset) noexcept {
    const std::size_t columns = static_cast<std::size_t>(std::max(1, metrics.columns));
    const std::size_t row = index / columns;
    const std::size_t column = index % columns;
    const float x = metrics.leftInset + static_cast<float>(column) * metrics.cellWidth;
    const float y = metrics.padding + static_cast<float>(row) * metrics.cellHeight - scrollOffset;
    return { x, y, x + metrics.cardWidth, y + metrics.cardHeight };
}

std::optional<std::size_t> HitTestThumbnail(const ThumbnailLayoutMetrics& metrics,
    std::size_t itemCount, float scrollOffset, float x, float y) noexcept {
    if (itemCount == 0 || x < metrics.leftInset || y < 0.0f) return std::nullopt;
    const float columnValue = (x - metrics.leftInset) / metrics.cellWidth;
    const float rowValue = (y + scrollOffset - metrics.padding) / metrics.cellHeight;
    if (!std::isfinite(columnValue) || !std::isfinite(rowValue) ||
        columnValue < 0.0f || rowValue < 0.0f) return std::nullopt;
    const int column = static_cast<int>(std::floor(columnValue));
    const int row = static_cast<int>(std::floor(rowValue));
    if (column < 0 || column >= metrics.columns || row < 0) return std::nullopt;
    const std::size_t index = static_cast<std::size_t>(row) *
        static_cast<std::size_t>(metrics.columns) + static_cast<std::size_t>(column);
    if (index >= itemCount) return std::nullopt;
    const ThumbnailRect rect = ThumbnailCardRect(metrics, index, scrollOffset);
    if (x < rect.left || x > rect.right || y < rect.top || y > rect.bottom) return std::nullopt;
    return index;
}

float ScrollToRevealThumbnail(const ThumbnailLayoutMetrics& metrics, std::size_t index,
    float currentScroll) noexcept {
    const std::size_t row = index / static_cast<std::size_t>(std::max(1, metrics.columns));
    const float top = metrics.padding + static_cast<float>(row) * metrics.cellHeight;
    const float bottom = top + metrics.cellHeight;
    float scroll = currentScroll;
    if (top < scroll) scroll = top;
    else if (bottom > scroll + metrics.canvasHeight) scroll = bottom - metrics.canvasHeight;
    return std::clamp(scroll, 0.0f, metrics.maximumScroll);
}

float ScrollToCenterThumbnail(const ThumbnailLayoutMetrics& metrics, std::size_t index) noexcept {
    const std::size_t row = index / static_cast<std::size_t>(std::max(1, metrics.columns));
    const float center = metrics.padding + (static_cast<float>(row) + 0.5f) * metrics.cellHeight;
    return std::clamp(center - metrics.canvasHeight * 0.5f, 0.0f, metrics.maximumScroll);
}

} // namespace quicksift::review::thumbnail
