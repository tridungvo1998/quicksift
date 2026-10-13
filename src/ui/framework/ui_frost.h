// OWNER: Platform-neutral frosted-backdrop image builder (left pane "frost").
//
// The left pane sits above the thumbnail canvas. Its material is a cached,
// blurred snapshot of the canvas pixels under the open pane, overlaid with the
// theme's panel colour at a high opacity so text stays clearly readable. The
// snapshot is rebuilt only when the pane opens or the canvas under it settles
// after a change (never per frame); see QuickSiftApplicationImpl::RefreshLeftPaneFrost.
//
// Pipeline (all CPU, on a small image): box-average downscale -> separable box
// blur (N passes ~ gaussian) -> tint mix -> bilinear upscale to the pane size.
// The result is a 32-bit BGRX top-down DIB that every pane surface (backdrop,
// tabs, buttons, labels) blits from, so all of them agree pixel-for-pixel.
#pragma once

#include <cstdint>
#include <vector>

namespace quicksift::ui::frost {

inline constexpr int kDownscale = 4;      // source pixels per frost pixel (each axis)
inline constexpr int kBlurRadius = 3;     // box radius in frost pixels (~12 px on screen)
inline constexpr int kBlurPasses = 2;     // two box passes approximate a gaussian
inline constexpr float kTintAlphaDark = 0.86f;
inline constexpr float kTintAlphaLight = 0.82f;
// Minimum spacing between canvas read-backs while the pane is open and the canvas
// keeps repainting (decodes landing, scroll settling).
inline constexpr int kRefreshIntervalMs = 450;

[[nodiscard]] constexpr float TintAlpha(bool dark) noexcept {
    return dark ? kTintAlphaDark : kTintAlphaLight;
}

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

// Win32 COLORREF (0x00BBGGRR) without pulling in windows.h.
[[nodiscard]] constexpr Rgb FromColorRef(std::uint32_t colorRef) noexcept {
    return Rgb{ static_cast<std::uint8_t>(colorRef & 0xFFu),
        static_cast<std::uint8_t>((colorRef >> 8) & 0xFFu),
        static_cast<std::uint8_t>((colorRef >> 16) & 0xFFu) };
}

struct Image {
    int width = 0;
    int height = 0;
    // Top-down rows, one 32-bit BGRX pixel each (little-endian bytes B, G, R, 0):
    // the in-memory value is 0x00RRGGBB, i.e. a BI_RGB 32 bpp DIB.
    std::vector<std::uint32_t> pixels;
    // Hash of the small (pre-upscale) tinted image, quantized so that sub-visible
    // noise does not count as a change. Used to skip repaints when nothing moved.
    std::uint64_t signature = 0;
    [[nodiscard]] bool Valid() const noexcept {
        return width > 0 && height > 0 &&
            pixels.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
};

[[nodiscard]] constexpr std::uint32_t Pack(Rgb c) noexcept {
    return (static_cast<std::uint32_t>(c.r) << 16) | (static_cast<std::uint32_t>(c.g) << 8) |
        static_cast<std::uint32_t>(c.b);
}
[[nodiscard]] constexpr Rgb Unpack(std::uint32_t p) noexcept {
    return Rgb{ static_cast<std::uint8_t>((p >> 16) & 0xFFu),
        static_cast<std::uint8_t>((p >> 8) & 0xFFu), static_cast<std::uint8_t>(p & 0xFFu) };
}

// Builds the frost image from 32-bit BGRA/BGRX source rows (`strideBytes` may
// include padding). Returns an invalid Image for empty/invalid input. `tintAlpha`
// is clamped to [0, 1]; 1 yields the solid tint.
[[nodiscard]] Image Build(const std::uint8_t* bgra, int width, int height, int strideBytes,
    Rgb tint, float tintAlpha, int downscale = kDownscale, int blurRadius = kBlurRadius,
    int blurPasses = kBlurPasses);

// Solid tint image (fallback when no snapshot is available / effects are off).
[[nodiscard]] Image Solid(int width, int height, Rgb color);

} // namespace quicksift::ui::frost
