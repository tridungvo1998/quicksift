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
        return ThemePalette{
            RGB(246, 249, 253), RGB(226, 235, 244),
            RGB(250, 252, 255), RGB(242, 247, 251), RGB(252, 254, 255),
            RGB(246, 249, 252), RGB(235, 242, 249), RGB(241, 245, 249),
            RGB(196, 208, 220), RGB(28, 42, 56), RGB(101, 117, 135),
            RGB(24, 123, 232), RGB(16, 105, 211),
            RGB(253, 237, 239), RGB(250, 219, 223), RGB(206, 78, 91),
            RGB(254, 255, 255), RGB(228, 240, 253), RGB(25, 39, 52), false
        };
    }
    return ThemePalette{
        RGB(29, 36, 47), RGB(18, 23, 31),
        RGB(36, 45, 57), RGB(29, 37, 48), RGB(42, 52, 66),
        RGB(46, 58, 72), RGB(58, 72, 90), RGB(36, 45, 57),
        RGB(77, 92, 112), RGB(240, 245, 250), RGB(162, 176, 192),
        RGB(58, 153, 255), RGB(123, 198, 255),
        RGB(95, 54, 71), RGB(131, 66, 84), RGB(212, 112, 136),
        RGB(36, 46, 58), RGB(47, 98, 154), RGB(245, 249, 253), true
    };
}


namespace {

tokens::VisualEffects gVisualEffects{};
COLORREF gSystemAccent = CLR_INVALID;
bool gVisualEffectsLoaded = false;

bool ReadUserDword(const wchar_t* key, const wchar_t* value, DWORD& out) noexcept {
    DWORD data = 0;
    DWORD size = sizeof(data);
    if (RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_DWORD, nullptr, &data, &size)
        != ERROR_SUCCESS) return false;
    out = data;
    return true;
}

tokens::VisualEffects QueryVisualEffects() noexcept {
    tokens::VisualEffects effects{};
    HIGHCONTRASTW highContrast{ sizeof(HIGHCONTRASTW) };
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, 0))
        effects.highContrast = (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    BOOL animations = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0))
        effects.animations = animations != FALSE;
    DWORD transparency = 1;
    if (ReadUserDword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"EnableTransparency", transparency))
        effects.transparency = transparency != 0;
    return effects;
}

COLORREF QuerySystemAccent() noexcept {
    // DWM stores the user's accent as 0xAABBGGRR; the low 24 bits are a COLORREF.
    DWORD accent = 0;
    if (!ReadUserDword(L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", accent))
        return CLR_INVALID;
    return static_cast<COLORREF>(accent & 0x00FFFFFFu);
}

void EnsureVisualEffectsLoaded() noexcept {
    if (gVisualEffectsLoaded) return;
    gVisualEffectsLoaded = true;
    gVisualEffects = QueryVisualEffects();
    gSystemAccent = QuerySystemAccent();
}

int CALLBACK FontFamilyExists(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
    *reinterpret_cast<bool*>(found) = true;
    return 0;
}

bool FontFamilyInstalled(const wchar_t* family) noexcept {
    HDC dc = GetDC(nullptr);
    if (!dc) return false;
    LOGFONTW query{};
    query.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(query.lfFaceName, family, _TRUNCATE);
    bool found = false;
    EnumFontFamiliesExW(dc, &query, FontFamilyExists, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

} // namespace

const tokens::VisualEffects& CurrentVisualEffects() noexcept {
    EnsureVisualEffectsLoaded();
    return gVisualEffects;
}

bool RefreshVisualEffects() noexcept {
    EnsureVisualEffectsLoaded();
    const tokens::VisualEffects effects = QueryVisualEffects();
    const COLORREF accent = QuerySystemAccent();
    const bool changed = effects != gVisualEffects || accent != gSystemAccent;
    gVisualEffects = effects;
    gSystemAccent = accent;
    return changed;
}

ThemePalette HighContrastPalette() noexcept {
    ThemePalette palette{};
    const COLORREF window = GetSysColor(COLOR_WINDOW);
    const COLORREF windowText = GetSysColor(COLOR_WINDOWTEXT);
    const COLORREF face = GetSysColor(COLOR_BTNFACE);
    const COLORREF highlight = GetSysColor(COLOR_HIGHLIGHT);
    const COLORREF highlightText = GetSysColor(COLOR_HIGHLIGHTTEXT);
    palette.windowTop = window;
    palette.windowBottom = window;
    palette.toolbar = face;
    palette.panel = window;
    palette.panelStrong = window;
    palette.button = face;
    palette.buttonHot = face;
    palette.buttonDisabled = face;
    palette.border = windowText;
    palette.text = windowText;
    palette.muted = GetSysColor(COLOR_GRAYTEXT);
    palette.accent = highlight;
    palette.accentBorder = highlight;
    palette.danger = window;
    palette.dangerHot = highlight;
    palette.dangerBorder = GetSysColor(COLOR_HOTLIGHT);
    palette.menu = window;
    palette.menuSelected = highlight;
    palette.title = windowText;
    palette.dark = RelativeLuminance(window) < 0.5;
    palette.accentText = highlightText;
    palette.menuSelectedText = highlightText;
    palette.highContrast = true;
    return palette;
}

void ApplyAccentToPalette(ThemePalette& palette, COLORREF accent) noexcept {
    if (accent == CLR_INVALID) return;
    COLORREF adjusted = accent;
    if (!palette.dark) {
        // Light theme: white captions on accent fills need >= 4.5:1.
        for (int step = 0; step < 20 && ContrastRatio(adjusted, RGB(255, 255, 255)) < 4.5; ++step)
            adjusted = BlendColor(adjusted, RGB(0, 0, 0), 0.08f);
        palette.accentBorder = BlendColor(adjusted, RGB(0, 0, 0), 0.12f);
        palette.menuSelected = BlendColor(palette.menu, adjusted, 0.12f);
    } else {
        // Dark theme: rings/borders on the panel need >= 3:1 against panelStrong.
        for (int step = 0; step < 20 && ContrastRatio(adjusted, palette.panelStrong) < 3.0; ++step)
            adjusted = BlendColor(adjusted, RGB(255, 255, 255), 0.08f);
        palette.accentBorder = BlendColor(adjusted, RGB(255, 255, 255), 0.35f);
        palette.menuSelected = BlendColor(palette.menu, adjusted, 0.55f);
    }
    palette.accent = adjusted;
    palette.accentText = BestContrastText(adjusted, RGB(25, 39, 52), RGB(255, 255, 255));
}

COLORREF MenuSelectedTextColor(const ThemePalette& palette) noexcept {
    return palette.menuSelectedText == CLR_INVALID ? palette.text : palette.menuSelectedText;
}

ThemePalette ResolveThemePalette(core::AppTheme theme) noexcept {
    EnsureVisualEffectsLoaded();
    if (gVisualEffects.highContrast) return HighContrastPalette();
    ThemePalette palette = GetThemePalette(theme);
    ApplyAccentToPalette(palette, gSystemAccent);
    return palette;
}

const wchar_t* UiFontFamily(FontRole role) noexcept {
    static const bool textInstalled = FontFamilyInstalled(tokens::kFamilyText);
    static const bool displayInstalled = FontFamilyInstalled(tokens::kFamilyDisplay);
    if (role == FontRole::Display) return displayInstalled ? tokens::kFamilyDisplay : tokens::kFamilyFallback;
    return textInstalled ? tokens::kFamilyText : tokens::kFamilyFallback;
}

float StrokePx(float dip, UINT dpi) noexcept {
    const float scale = static_cast<float>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI) / USER_DEFAULT_SCREEN_DPI;
    return std::max(1.0f, std::round(dip * scale * 2.0f) / 2.0f);
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
    static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (getDpiForWindow && window) {
        const UINT dpi = getDpiForWindow(window);
        if (dpi) return dpi;
    }

    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static HMODULE shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static const auto getDpiForMonitor = shcore ? reinterpret_cast<GetDpiForMonitorFn>(reinterpret_cast<void*>(
        GetProcAddress(shcore, "GetDpiForMonitor"))) : nullptr;
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
    // Anti-aliased replacement for the former GDI RoundRect (aliased edges). The
    // geometry is unchanged: RoundRect's `radius` argument was the corner
    // ellipse WIDTH, so the corner radius is radius / 2, and the 1 px pen sat
    // inside the rectangle.
    if (!dc || rect.right <= rect.left || rect.bottom <= rect.top) return;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    const float cornerRadius = static_cast<float>(std::max(0, radius)) * 0.5f;
    FillRoundedRectGp(graphics, rect, cornerRadius, ToGpColor(fill));
    if (border != fill) DrawRoundedRectGp(graphics, rect, cornerRadius, ToGpColor(border), 1.0f);
}

} // namespace quicksift::ui
