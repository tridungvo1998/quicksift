// OWNER: Theme/drawing implementation; tolerate optional GDI allocation failure safely.
// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".

#include "ui_design_system.h"

#include "../core/app_types.h"

#include <algorithm>
#include <cmath>

namespace quicksift::ui {
namespace {

void AddRoundedRectanglePath(Gdiplus::GraphicsPath& path, float left, float top,
    float right, float bottom, float requestedRadius) {
    const float width = std::max(0.0f, right - left);
    const float height = std::max(0.0f, bottom - top);
    const float radius = std::clamp(requestedRadius, 0.0f, std::min(width, height) * 0.5f);
    const float diameter = radius * 2.0f;
    if (diameter <= 0.0f) {
        path.AddRectangle(Gdiplus::RectF(left, top, width, height));
        return;
    }
    path.AddArc(left, top, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(right - diameter, top, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(right - diameter, bottom - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(left, bottom - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

} // namespace

ThemePalette GetThemePalette(core::AppTheme theme) noexcept {
    if (theme == core::AppTheme::Light) {
        return {
            RGB(246, 249, 253), RGB(226, 235, 244),
            RGB(250, 252, 255), RGB(242, 247, 251), RGB(252, 254, 255),
            RGB(246, 249, 252), RGB(235, 242, 249), RGB(241, 245, 249),
            RGB(196, 208, 220), RGB(28, 42, 56), RGB(101, 117, 135),
            RGB(24, 123, 232), RGB(16, 105, 211),
            RGB(253, 237, 239), RGB(250, 219, 223), RGB(206, 78, 91),
            RGB(254, 255, 255), RGB(228, 240, 253), RGB(25, 39, 52), false
        };
    }
    return {
        RGB(29, 36, 47), RGB(18, 23, 31),
        RGB(36, 45, 57), RGB(29, 37, 48), RGB(42, 52, 66),
        RGB(46, 58, 72), RGB(58, 72, 90), RGB(36, 45, 57),
        RGB(77, 92, 112), RGB(240, 245, 250), RGB(162, 176, 192),
        RGB(58, 153, 255), RGB(123, 198, 255),
        RGB(95, 54, 71), RGB(131, 66, 84), RGB(212, 112, 136),
        RGB(36, 46, 58), RGB(47, 98, 154), RGB(245, 249, 253), true
    };
}

const std::array<COLORREF, 5>& BackgroundAccentColors() noexcept {
    static constexpr std::array<COLORREF, 5> colors{
        RGB(120, 186, 255), RGB(174, 154, 255), RGB(118, 230, 214),
        RGB(255, 172, 221), RGB(255, 214, 160)
    };
    return colors;
}

UINT GetWindowDpi(HWND window) noexcept {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (getDpiForWindow && window) {
        const UINT dpi = getDpiForWindow(window);
        if (dpi) return dpi;
    }

    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static HMODULE shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static const auto getDpiForMonitor = shcore ? reinterpret_cast<GetDpiForMonitorFn>(
        GetProcAddress(shcore, "GetDpiForMonitor")) : nullptr;
    if (getDpiForMonitor && window) {
        UINT dpiX = USER_DEFAULT_SCREEN_DPI;
        UINT dpiY = USER_DEFAULT_SCREEN_DPI;
        if (SUCCEEDED(getDpiForMonitor(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST),
            0, &dpiX, &dpiY)) && dpiX) {
            return dpiX;
        }
    }

    HDC dc = GetDC(window);
    const UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : USER_DEFAULT_SCREEN_DPI;
    if (dc) ReleaseDC(window, dc);
    return dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
}

COLORREF BlendColor(COLORREF first, COLORREF second, float amount) noexcept {
    amount = std::clamp(amount, 0.0f, 1.0f);
    const auto blend = [amount](BYTE firstChannel, BYTE secondChannel) -> BYTE {
        return static_cast<BYTE>(std::lround(firstChannel +
            (secondChannel - firstChannel) * amount));
    };
    return RGB(blend(GetRValue(first), GetRValue(second)),
        blend(GetGValue(first), GetGValue(second)),
        blend(GetBValue(first), GetBValue(second)));
}

double RelativeLuminance(COLORREF color) noexcept {
    const auto channel = [](BYTE value) {
        const double normalized = static_cast<double>(value) / 255.0;
        return normalized <= 0.04045 ? normalized / 12.92 :
            std::pow((normalized + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(GetRValue(color)) +
        0.7152 * channel(GetGValue(color)) +
        0.0722 * channel(GetBValue(color));
}

double ContrastRatio(COLORREF first, COLORREF second) noexcept {
    const double firstLuminance = RelativeLuminance(first);
    const double secondLuminance = RelativeLuminance(second);
    const double lighter = std::max(firstLuminance, secondLuminance);
    const double darker = std::min(firstLuminance, secondLuminance);
    return (lighter + 0.05) / (darker + 0.05);
}

COLORREF BestContrastText(COLORREF background, COLORREF darkText, COLORREF lightText) noexcept {
    return ContrastRatio(background, darkText) >= ContrastRatio(background, lightText) ?
        darkText : lightText;
}

D2D1_COLOR_F ToDirect2DColor(COLORREF color, float alpha) noexcept {
    return D2D1::ColorF(GetRValue(color) / 255.0f, GetGValue(color) / 255.0f,
        GetBValue(color) / 255.0f, alpha);
}

Gdiplus::Color ToGpColor(COLORREF color, BYTE alpha) noexcept {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

Gdiplus::RectF ToGpRect(const RECT& rect) noexcept {
    return Gdiplus::RectF(static_cast<Gdiplus::REAL>(rect.left),
        static_cast<Gdiplus::REAL>(rect.top),
        static_cast<Gdiplus::REAL>(rect.right - rect.left),
        static_cast<Gdiplus::REAL>(rect.bottom - rect.top));
}

void FillRoundedRectGp(Gdiplus::Graphics& graphics, const RECT& rect, float radius,
    const Gdiplus::Color& color) {
    Gdiplus::GraphicsPath path;
    AddRoundedRectanglePath(path, static_cast<float>(rect.left), static_cast<float>(rect.top),
        static_cast<float>(rect.right), static_cast<float>(rect.bottom), radius);
    Gdiplus::SolidBrush brush(color);
    graphics.FillPath(&brush, &path);
}

void DrawRoundedRectGp(Gdiplus::Graphics& graphics, const RECT& rect, float radius,
    const Gdiplus::Color& color, float width) {
    const float safeWidth = std::max(1.0f, width);
    const float inset = safeWidth * 0.5f;
    Gdiplus::GraphicsPath path;
    AddRoundedRectanglePath(path,
        static_cast<float>(rect.left) + inset,
        static_cast<float>(rect.top) + inset,
        static_cast<float>(rect.right) - inset,
        static_cast<float>(rect.bottom) - inset,
        std::max(0.0f, radius - inset));
    Gdiplus::Pen pen(color, safeWidth);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawPath(&pen, &path);
}

void FillVerticalGradient(HDC dc, const RECT& rect, COLORREF top, COLORREF bottom) {
    TRIVERTEX vertices[2]{};
    vertices[0].x = rect.left;
    vertices[0].y = rect.top;
    vertices[0].Red = static_cast<COLOR16>(GetRValue(top) << 8);
    vertices[0].Green = static_cast<COLOR16>(GetGValue(top) << 8);
    vertices[0].Blue = static_cast<COLOR16>(GetBValue(top) << 8);
    vertices[0].Alpha = 0xFF00;
    vertices[1].x = rect.right;
    vertices[1].y = rect.bottom;
    vertices[1].Red = static_cast<COLOR16>(GetRValue(bottom) << 8);
    vertices[1].Green = static_cast<COLOR16>(GetGValue(bottom) << 8);
    vertices[1].Blue = static_cast<COLOR16>(GetBValue(bottom) << 8);
    vertices[1].Alpha = 0xFF00;
    GRADIENT_RECT gradient{ 0, 1 };
    GradientFill(dc, vertices, 2, &gradient, 1, GRADIENT_FILL_RECT_V);
}

void FillSolidRect(HDC dc, const RECT& rect, COLORREF color) noexcept {
    if (!dc || rect.right <= rect.left || rect.bottom <= rect.top) return;
    HBRUSH brush = CreateSolidBrush(color);
    if (!brush) return;
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void DrawSolidLine(HDC dc, int x1, int y1, int x2, int y2, int width, COLORREF color) noexcept {
    if (!dc) return;
    HPEN pen = CreatePen(PS_SOLID, std::max(1, width), color);
    if (!pen) return;
    HGDIOBJ previous = SelectObject(dc, pen);
    if (previous && previous != HGDI_ERROR) {
        MoveToEx(dc, x1, y1, nullptr);
        LineTo(dc, x2, y2);
        SelectObject(dc, previous);
    }
    DeleteObject(pen);
}

void FillRoundedRect(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF border) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    if (!brush || !pen) {
        if (pen) DeleteObject(pen);
        if (brush) DeleteObject(brush);
        return;
    }
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

} // namespace quicksift::ui
