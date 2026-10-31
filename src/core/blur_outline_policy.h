// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#pragma once
// OWNER: The "blurry photo" (face focus warning) outline on thumbnails (round 23):
// a red DASHED stroke in the Thumbnail grid and the Single View filmstrip.
// Portable (checked by filmstrip_checks): theme-aware red with a guaranteed
// contrast against each theme's card surface, and the dash pattern.
#include <cmath>
#include <cstdint>

namespace quicksift::core::blur_outline {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
};

// Light theme: a deeper red (>= 4.5:1 on the light card surface). Dark theme: a
// lighter red (>= 3:1 on the dark card and strip surfaces). High-contrast mode
// uses the system danger colour instead (palette.dangerBorder).
inline constexpr Rgb kLightRed{ 200, 30, 45 };
inline constexpr Rgb kDarkRed{ 255, 99, 99 };
[[nodiscard]] constexpr Rgb Color(bool darkTheme) noexcept { return darkTheme ? kDarkRed : kLightRed; }

// Stroke width (DIP; the previous solid warning stroke) and the dash pattern in
// multiples of the stroke width (Direct2D D2D1_DASH_STYLE_CUSTOM units), so the
// dash scales with DPI through the DIP stroke width.
inline constexpr float kStrokeDip = 3.0f;
inline constexpr float kFilmstripStrokeDip = 2.0f;
inline constexpr float kDashOn = 2.0f;   // 6 DIP dash at 3 DIP
inline constexpr float kDashOff = 1.5f;  // 4.5 DIP gap at 3 DIP

[[nodiscard]] constexpr float DashLengthDip(float strokeDip) noexcept { return kDashOn * strokeDip; }
[[nodiscard]] constexpr float GapLengthDip(float strokeDip) noexcept { return kDashOff * strokeDip; }

// WCAG 2 relative luminance / contrast ratio (sRGB).
[[nodiscard]] inline double Luminance(Rgb color) noexcept {
    const auto channel = [](std::uint8_t value) {
        const double c = static_cast<double>(value) / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.r) + 0.7152 * channel(color.g) + 0.0722 * channel(color.b);
}
[[nodiscard]] inline double Contrast(Rgb a, Rgb b) noexcept {
    const double la = Luminance(a), lb = Luminance(b);
    return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

} // namespace quicksift::core::blur_outline
