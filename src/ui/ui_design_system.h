// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Shared theme and drawing API; reusable visual decisions belong here.

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <gdiplus.h>

#include <array>

#include "ui/ui_design_tokens.h"

namespace quicksift::core { enum class AppTheme; }

namespace quicksift::ui {

// Semantic colors for every native window, menu, control, canvas overlay, and
// diagnostic/help document. UI code should ask for a semantic role instead of
// inventing a local RGB value for ordinary surfaces and text.
struct ThemePalette {
    COLORREF windowTop{};
    COLORREF windowBottom{};
    COLORREF toolbar{};
    COLORREF panel{};
    COLORREF panelStrong{};
    COLORREF button{};
    COLORREF buttonHot{};
    COLORREF buttonDisabled{};
    COLORREF border{};
    COLORREF text{};
    COLORREF muted{};
    COLORREF accent{};
    COLORREF accentBorder{};
    COLORREF danger{};
    COLORREF dangerHot{};
    COLORREF dangerBorder{};
    COLORREF menu{};
    COLORREF menuSelected{};
    COLORREF title{};
    bool dark = true;
    // Text drawn on accent fills (selected buttons, active tabs, primary alert
    // button) and on the selected menu row. White / palette text for the built-in
    // themes; HIGHLIGHTTEXT in high contrast; contrast-picked for system accents.
    COLORREF accentText = RGB(255, 255, 255);
    COLORREF menuSelectedText = CLR_INVALID; // CLR_INVALID: use `text`
    bool highContrast = false;
};

// Built-in palette (no accessibility or system-accent adjustments).
ThemePalette GetThemePalette(core::AppTheme theme) noexcept;
// The palette every surface should use: high contrast -> system colours; otherwise
// the built-in palette with the Windows accent colour (contrast-guarded) applied.
ThemePalette ResolveThemePalette(core::AppTheme theme) noexcept;
ThemePalette HighContrastPalette() noexcept;
// Contrast-guarded accent derivation for a palette (pure; used by the resolver and
// the portable checks through the same arithmetic).
void ApplyAccentToPalette(ThemePalette& palette, COLORREF accent) noexcept;
COLORREF MenuSelectedTextColor(const ThemePalette& palette) noexcept;

// Accessibility / effect policy, cached on the UI thread. RefreshVisualEffects()
// re-reads high contrast, client-area animations, transparency and the system
// accent; it returns true when anything changed (callers then re-theme).
const tokens::VisualEffects& CurrentVisualEffects() noexcept;
bool RefreshVisualEffects() noexcept;

// Font families from the design tokens, resolved once: Segoe UI Variable where it is
// installed (Windows 11), plain Segoe UI otherwise. GDI would otherwise silently map a
// missing family to an unrelated face.
enum class FontRole { Text, Display };
const wchar_t* UiFontFamily(FontRole role) noexcept;
// Device-pixel stroke width for a DIP stroke token (never thinner than one pixel).
float StrokePx(float dip, UINT dpi) noexcept;
const std::array<COLORREF, 5>& BackgroundAccentColors() noexcept;
UINT GetWindowDpi(HWND window) noexcept;

COLORREF BlendColor(COLORREF first, COLORREF second, float amount) noexcept;
double RelativeLuminance(COLORREF color) noexcept;
double ContrastRatio(COLORREF first, COLORREF second) noexcept;
COLORREF BestContrastText(COLORREF background,
    COLORREF darkText = RGB(25, 39, 52),
    COLORREF lightText = RGB(255, 255, 255)) noexcept;

D2D1_COLOR_F ToDirect2DColor(COLORREF color, float alpha = 1.0f) noexcept;
Gdiplus::Color ToGpColor(COLORREF color, BYTE alpha = 255) noexcept;
Gdiplus::RectF ToGpRect(const RECT& rect) noexcept;
void FillRoundedRectGp(Gdiplus::Graphics& graphics, const RECT& rect, float radius,
    const Gdiplus::Color& color);
void DrawRoundedRectGp(Gdiplus::Graphics& graphics, const RECT& rect, float radius,
    const Gdiplus::Color& color, float width = 1.0f);
void FillVerticalGradient(HDC dc, const RECT& rect, COLORREF top, COLORREF bottom);
void FillSolidRect(HDC dc, const RECT& rect, COLORREF color) noexcept;
void DrawSolidLine(HDC dc, int x1, int y1, int x2, int y2, int width, COLORREF color) noexcept;
void FillRoundedRect(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF border);

} // namespace quicksift::ui
