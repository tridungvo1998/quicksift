// OWNER: Deterministic thumbnail-grid geometry shared by rendering, input, and prefetch.
#pragma once

#include <cstddef>
#include <optional>

namespace quicksift::review::thumbnail {

struct ThumbnailLayoutMetrics {
    float canvasWidth = 0.0f;
    float canvasHeight = 0.0f;
    float padding = 12.0f;
    float leftInset = 12.0f;
    float cellWidth = 0.0f;
    float cellHeight = 0.0f;
    float cardWidth = 0.0f;
    float cardHeight = 0.0f;
    int columns = 1;
    int rows = 0;
    float contentHeight = 0.0f;
    float maximumScroll = 0.0f;
};

struct ThumbnailRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

[[nodiscard]] ThumbnailLayoutMetrics CalculateThumbnailLayout(float canvasWidth,
    float canvasHeight, int thumbnailSize, std::size_t itemCount, float leftInset) noexcept;
[[nodiscard]] ThumbnailRect ThumbnailCardRect(const ThumbnailLayoutMetrics& metrics,
    std::size_t index, float scrollOffset) noexcept;
[[nodiscard]] std::optional<std::size_t> HitTestThumbnail(const ThumbnailLayoutMetrics& metrics,
    std::size_t itemCount, float scrollOffset, float x, float y) noexcept;
[[nodiscard]] float ScrollToRevealThumbnail(const ThumbnailLayoutMetrics& metrics,
    std::size_t index, float currentScroll) noexcept;
[[nodiscard]] float ScrollToCenterThumbnail(const ThumbnailLayoutMetrics& metrics,
    std::size_t index) noexcept;

} // namespace quicksift::review::thumbnail
