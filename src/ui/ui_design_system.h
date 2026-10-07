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
};

ThemePalette GetThemePalette(core::AppTheme theme) noexcept;
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
