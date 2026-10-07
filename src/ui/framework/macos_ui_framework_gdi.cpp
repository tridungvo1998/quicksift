// OWNER: Central macOS-inspired GDI/GDI+ component renderer.
#include "ui/framework/macos_ui_framework.h"
#include "ui/ui_layout_metrics.h"


#include <algorithm>
#include <cmath>

namespace quicksift::ui::framework {
namespace {
HFONT SafeFont(HFONT requested) noexcept {
    return requested ? requested : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

bool SelectFontObject(HDC dc, HFONT font, HFONT& previous) noexcept {
    previous = static_cast<HFONT>(SelectObject(dc, SafeFont(font)));
    return previous && previous != HGDI_ERROR;
}
}

void MacOsUiFramework::PaintThemeGlyph(HDC dc, const RECT& rect, COLORREF color,
    UINT dpi, bool lightTheme) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    const float lineWidth = static_cast<float>(std::max(1, ScaleDip(1, dpi)));
    Gdiplus::Pen pen(ToGpColor(color), lineWidth);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const float cx = static_cast<float>((rect.left + rect.right) / 2);
    const float cy = static_cast<float>((rect.top + rect.bottom) / 2);
    const float radius = static_cast<float>(ScaleDip(5, dpi));
    if (lightTheme) {
        const Gdiplus::RectF circle(cx - radius * 0.55f, cy - radius * 0.55f,
            radius * 1.1f, radius * 1.1f);
        graphics.DrawEllipse(&pen, circle);
        for (int index = 0; index < 8; ++index) {
            const float angle = static_cast<float>(index) * 3.14159265f / 4.0f;
            const float inner = radius * 0.88f;
            const float outer = radius * 1.32f;
            graphics.DrawLine(&pen, cx + std::cos(angle) * inner, cy + std::sin(angle) * inner,
                cx + std::cos(angle) * outer, cy + std::sin(angle) * outer);
        }
        return;
    }
    Gdiplus::GraphicsPath crescent;
    crescent.AddEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
    Gdiplus::GraphicsPath cutout;
    cutout.AddEllipse(cx - radius * 0.25f, cy - radius * 1.10f,
        radius * 2.0f, radius * 2.0f);
    Gdiplus::Region region(&crescent);
    region.Exclude(&cutout);
    Gdiplus::SolidBrush brush(ToGpColor(color));
    graphics.FillRegion(&brush, &region);
}

void MacOsUiFramework::PaintChevron(HDC dc, const RECT& rect, COLORREF color, UINT dpi) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Pen pen(ToGpColor(color), static_cast<float>(std::max(1, ScaleDip(1, dpi))));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const float cx = static_cast<float>((rect.left + rect.right) / 2);
    const float cy = static_cast<float>((rect.top + rect.bottom) / 2);
    graphics.DrawLine(&pen, cx - ScaleDip(3, dpi), cy - ScaleDip(1, dpi),
        cx, cy + ScaleDip(2, dpi));
    graphics.DrawLine(&pen, cx, cy + ScaleDip(2, dpi),
        cx + ScaleDip(3, dpi), cy - ScaleDip(1, dpi));
}

void MacOsUiFramework::PaintButton(HDC dc, const RECT& sourceRect,
    const ButtonPresentation& presentation, const ThemePalette& palette,
    UINT dpi, HFONT normalFont, HFONT semiboldFont) const {
    if (!dc) return;
    RECT rect = sourceRect;
    const bool title = presentation.role == ButtonRole::TitleBar;
    const bool header = presentation.role == ButtonRole::Header;
    const bool danger = presentation.role == ButtonRole::Danger;
    const ButtonVisualState& state = presentation.state;

    float hoverBlend = presentation.hoverBlend;
    if (hoverBlend < 0.0f) hoverBlend = state.hot ? 1.0f : 0.0f;
    hoverBlend = std::clamp(hoverBlend, 0.0f, 1.0f);
    float pressBlend = presentation.pressBlend;
    if (pressBlend < 0.0f) pressBlend = state.pressed ? 1.0f : 0.0f;
    pressBlend = std::clamp(pressBlend, 0.0f, 1.0f);
    const float hoverEase = Smoothstep01(hoverBlend);
    const float pressEase = Smoothstep01(pressBlend);

    // Light press scale: inset the chrome a couple DIPs so pressed/keyboard-armed
    // buttons respond without per-frame bitmap effects.
    if (pressEase > 0.0f && !header) {
        const int inset = std::max(1, static_cast<int>(std::lround(
            static_cast<float>(ScaleDip(2, dpi)) * pressEase)));
        InflateRect(&rect, -inset, -inset);
    }

    using quicksift::ui::layout::kButtonRadiusDip;
    using quicksift::ui::layout::kButtonTextPaddingDip;
    using quicksift::ui::layout::kButtonPopupChevronDip;
    using quicksift::ui::layout::kTitleButtonTextPaddingDip;

    const COLORREF surface = title
        ? BlendColor(palette.toolbar, palette.windowTop, palette.dark ? 0.15f : 0.25f)
        : palette.panelStrong;

    COLORREF fill = title ? BlendColor(palette.button, surface, 0.22f) : palette.button;
    COLORREF border = BlendColor(palette.border, fill, 0.18f);
    COLORREF text = palette.text;
    if (hoverEase > 0.0f) {
        fill = BlendColor(fill, palette.buttonHot, hoverEase);
        border = BlendColor(border, BlendColor(palette.accent, palette.border, 0.42f), hoverEase);
    }
    if (pressEase > 0.0f) {
        const COLORREF pressedFill = BlendColor(palette.accent,
            palette.dark ? RGB(0, 0, 0) : RGB(255, 255, 255), palette.dark ? 0.08f : 0.12f);
        fill = BlendColor(fill, pressedFill, pressEase);
        border = BlendColor(border, palette.accentBorder, pressEase);
        text = BlendColor(text, RGB(255, 255, 255), pressEase);
    }
    if (state.selected) {
        fill = palette.accent;
        border = palette.accentBorder;
        text = RGB(255, 255, 255);
    }
    if (danger) {
        const COLORREF dangerIdle = BlendColor(palette.danger, surface, 0.22f);
        const COLORREF dangerHot = palette.dangerHot;
        fill = BlendColor(dangerIdle, dangerHot, std::max(hoverEase, pressEase));
        border = BlendColor(BlendColor(palette.dangerBorder, palette.border, 0.58f),
            palette.dangerBorder, std::max(hoverEase, pressEase));
        text = BlendColor(BlendColor(palette.dangerBorder, palette.text, 0.22f),
            palette.dangerBorder, std::max(hoverEase, pressEase));
    }
    if (state.disabled) {
        fill = palette.buttonDisabled;
        border = BlendColor(palette.border, surface, 0.40f);
        text = BlendColor(palette.muted, surface, 0.28f);
    }

    // Pill radius for title/header so underlay, fill, and window region share one shape.
    // Standard buttons use the shared layout metric (not a mismatched shadow radius).
    const int buttonHeight = std::max(1, static_cast<int>(sourceRect.bottom - sourceRect.top));
    const int radius = (header || title)
        ? std::max(1, buttonHeight / 2)
        : ScaleDip(kButtonRadiusDip, dpi);
    {
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        // Erase with the SAME rounded shape as the chrome. A rectangular FillSolidRect
        // previously peeked behind pill corners as a dark mismatched underlay.
        FillRoundedRectGp(graphics, sourceRect, static_cast<float>(radius), ToGpColor(surface));
        if (!header && !title && pressEase < 0.85f) {
            RECT shadow = rect;
            // Keep shadow bounds identical to the fill (same width/height/radius); only
            // offset vertically so the underlay cannot read as a smaller-radius block.
            OffsetRect(&shadow, 0, ScaleDip(pressEase > 0.5f ? 0 : 1, dpi));
            FillRoundedRectGp(graphics, shadow, static_cast<float>(radius),
                ToGpColor(BlendColor(surface, RGB(0, 0, 0), palette.dark ? 0.20f : 0.055f),
                    static_cast<BYTE>(120 - static_cast<int>(70.0f * pressEase))));
        }
        if (!(header && presentation.glyph != ButtonGlyph::None)) {
            // Soft top highlight for a less flat chrome without bitmap effects.
            const COLORREF topFill = BlendColor(fill,
                palette.dark ? RGB(255, 255, 255) : RGB(255, 255, 255),
                palette.dark ? 0.06f : 0.10f);
            FillRoundedRectGp(graphics, rect, static_cast<float>(radius), ToGpColor(fill));
            RECT highlight = rect;
            highlight.bottom = highlight.top + std::max(2, static_cast<int>((highlight.bottom - highlight.top) / 2));
            // Approximate highlight with a shorter rounded fill; clamp radius to half-height.
            const int highlightRadius = std::min(radius,
                std::max(1, static_cast<int>((highlight.bottom - highlight.top) / 2)));
            FillRoundedRectGp(graphics, highlight, static_cast<float>(highlightRadius),
                ToGpColor(topFill, static_cast<BYTE>(palette.dark ? 40 : 55)));
            // Outer border + subtle inner edge for depth.
            DrawRoundedRectGp(graphics, rect, static_cast<float>(radius), ToGpColor(border),
                static_cast<float>(std::max(1, ScaleDip(1, dpi))));
            RECT inner = rect;
            InflateRect(&inner, -ScaleDip(1, dpi), -ScaleDip(1, dpi));
            if (inner.right > inner.left && inner.bottom > inner.top) {
                const COLORREF innerEdge = BlendColor(fill,
                    palette.dark ? RGB(255, 255, 255) : RGB(255, 255, 255),
                    palette.dark ? 0.10f : 0.28f);
                DrawRoundedRectGp(graphics, inner, static_cast<float>(std::max(1, radius - 1)),
                    ToGpColor(innerEdge, static_cast<BYTE>(palette.dark ? 70 : 90)), 1.0f);
            }
        }
    }

    HFONT oldFont = nullptr;
    if (!SelectFontObject(dc, semiboldFont ? semiboldFont : normalFont, oldFont)) return;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text);
    RECT textRect = rect;
    if (presentation.glyph == ButtonGlyph::Help || presentation.glyph == ButtonGlyph::LeftArrow ||
        presentation.glyph == ButtonGlyph::RightArrow) {
        const wchar_t* glyph = presentation.glyph == ButtonGlyph::Help ? L"?" :
            (presentation.glyph == ButtonGlyph::LeftArrow ? L"◀" : L"▶");
        DrawTextW(dc, glyph, -1, &textRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else if (presentation.glyph == ButtonGlyph::Theme) {
        RECT glyphRect{ rect.left + ScaleDip(11, dpi), rect.top,
            rect.left + ScaleDip(29, dpi), rect.bottom };
        PaintThemeGlyph(dc, glyphRect, text, dpi, !palette.dark);
    } else if (presentation.glyph == ButtonGlyph::RotateLeft ||
        presentation.glyph == ButtonGlyph::RotateRight) {
        const wchar_t* glyph = presentation.glyph == ButtonGlyph::RotateLeft ? L"↶" : L"↷";
        DrawTextW(dc, glyph, -1, &textRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else {
        const int padding = title ? ScaleDip(kTitleButtonTextPaddingDip, dpi)
            : ScaleDip(kButtonTextPaddingDip, dpi);
        textRect.left += padding;
        textRect.right -= padding;
        if (state.popup) textRect.right -= ScaleDip(kButtonPopupChevronDip, dpi);
        DrawTextW(dc, presentation.text.c_str(), -1, &textRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (state.popup && !state.disabled) {
            RECT chevron{ rect.right - ScaleDip(kButtonPopupChevronDip + 3, dpi), rect.top,
                rect.right - ScaleDip(8, dpi), rect.bottom };
            PaintChevron(dc, chevron, text, dpi);
        }
    }

    if (state.focused && !state.disabled && !header) {
        // Dual focus ring: soft outer glow + crisp inner accent.
        RECT outer = rect;
        InflateRect(&outer, ScaleDip(1, dpi), ScaleDip(1, dpi));
        RECT inner = rect;
        InflateRect(&inner, -ScaleDip(2, dpi), -ScaleDip(2, dpi));
        {
            Gdiplus::Graphics graphics(dc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            const COLORREF ring = state.selected ? RGB(255, 255, 255) : palette.accent;
            DrawRoundedRectGp(graphics, outer, static_cast<float>(radius + ScaleDip(1, dpi)),
                ToGpColor(ring, 90), static_cast<float>(std::max(2, ScaleDip(2, dpi))));
            DrawRoundedRectGp(graphics, inner, static_cast<float>(std::max(1, radius - ScaleDip(1, dpi))),
                ToGpColor(ring, 220), static_cast<float>(std::max(1, ScaleDip(1, dpi))));
        }
    }
    SelectObject(dc, oldFont);
}

void MacOsUiFramework::PaintPanel(HDC dc, const RECT& rect,
    const PanelPresentation& presentation, const ThemePalette& palette, UINT dpi) const {
    if (!dc) return;
    const int radius = ScaleDip(presentation.radiusDip, dpi);
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    if (presentation.raised) {
        RECT shadow = rect;
        OffsetRect(&shadow, ScaleDip(3, dpi), ScaleDip(4, dpi));
        FillRoundedRectGp(graphics, shadow, static_cast<float>(radius),
            ToGpColor(RGB(0, 0, 0), palette.dark ? 72 : 30));
    }
    // Always paint an OPAQUE base. Semi-transparent GDI+ fills on a non-layered HWND
    // AlphaBlend with whatever sits under the window (thumbnail canvas siblings), so
    // translucent=true must never lower the base alpha — frost is a highlight wash on top.
    FillRoundedRectGp(graphics, rect, static_cast<float>(radius), ToGpColor(palette.panelStrong));
    if (presentation.translucent) {
        RECT highlight = rect;
        const int height = std::max(1, static_cast<int>(rect.bottom - rect.top));
        highlight.bottom = highlight.top + std::max(2, height / 2);
        const int highlightRadius = std::min(radius,
            std::max(1, static_cast<int>((highlight.bottom - highlight.top) / 2)));
        const COLORREF topWash = BlendColor(palette.panelStrong, RGB(255, 255, 255),
            palette.dark ? 0.08f : 0.14f);
        FillRoundedRectGp(graphics, highlight, static_cast<float>(highlightRadius),
            ToGpColor(topWash, static_cast<BYTE>(palette.dark ? 48 : 64)));
    }
    DrawRoundedRectGp(graphics, rect, static_cast<float>(radius),
        ToGpColor(BlendColor(palette.border, palette.panelStrong, 0.18f)), 1.0f);
}

void MacOsUiFramework::PaintControlBorder(HDC dc, const RECT& rect, int radiusDip,
    const ThemePalette& palette, UINT dpi) const {
    if (!dc) return;
    const int radius = ScaleDip(radiusDip, dpi);
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    DrawRoundedRectGp(graphics, rect, static_cast<float>(radius),
        ToGpColor(BlendColor(palette.border, palette.panelStrong, 0.18f)), 1.0f);
}

void MacOsUiFramework::PaintLabel(HDC dc, const RECT& rect,
    const LabelPresentation& presentation, const ThemePalette& palette,
    UINT dpi, HFONT normalFont, HFONT semiboldFont) const {
    if (!dc) return;
    FillRoundedRect(dc, rect, ScaleDip(13, dpi), palette.panelStrong, palette.border);
    RECT textRect = rect;
    textRect.left += ScaleDip(presentation.horizontalPaddingDip, dpi);
    textRect.right -= ScaleDip(8, dpi);
    HFONT oldFont = nullptr;
    if (!SelectFontObject(dc, presentation.semibold ? semiboldFont : normalFont, oldFont)) return;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, presentation.muted ? palette.muted : palette.text);
    DrawTextW(dc, presentation.text.c_str(), -1, &textRect, presentation.format | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

void MacOsUiFramework::PaintSectionTitle(HDC dc, const RECT& rect, std::wstring_view text,
    const ThemePalette& palette, UINT dpi, HFONT semiboldFont) const {
    if (!dc || !semiboldFont) return;
    using quicksift::ui::layout::kButtonRadiusDip;
    const int radius = ScaleDip(kButtonRadiusDip, dpi);
    // Opaque rounded chrome so section headers match photo-details / inputs / buttons
    // instead of a sharp rectangular strip behind the label text.
    FillRoundedRect(dc, rect, radius, palette.panelStrong,
        BlendColor(palette.border, palette.panelStrong, 0.18f));
    RECT textRect = rect;
    textRect.left += ScaleDip(14, dpi);
    textRect.right -= ScaleDip(10, dpi);
    HFONT oldFont = nullptr;
    if (!SelectFontObject(dc, semiboldFont, oldFont)) return;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, palette.muted);
    const std::wstring upper(text.begin(), text.end());
    DrawTextW(dc, upper.c_str(), -1, &textRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

void MacOsUiFramework::PaintInfoPanel(HDC dc, const RECT& rect,
    const InfoPanelPresentation& presentation, const ThemePalette& palette,
    UINT dpi, HFONT normalFont, HFONT semiboldFont) const {
    if (!dc) return;
    using quicksift::ui::layout::kLeftPaneContentRadiusDip;
    PaintPanel(dc, rect, PanelPresentation{ false, false, kLeftPaneContentRadiusDip },
        palette, dpi);
    RECT titleRect = rect;
    titleRect.left += ScaleDip(14, dpi);
    titleRect.right -= ScaleDip(10, dpi);
    titleRect.top += ScaleDip(10, dpi);
    titleRect.bottom = titleRect.top + ScaleDip(22, dpi);
    HFONT oldFont = nullptr;
    if (!SelectFontObject(dc, semiboldFont, oldFont)) return;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, palette.muted);
    DrawTextW(dc, presentation.title.c_str(), -1, &titleRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    RECT textRect = rect;
    textRect.left += ScaleDip(14, dpi);
    textRect.right -= ScaleDip(10, dpi);
    textRect.top += ScaleDip(38, dpi);
    textRect.bottom -= ScaleDip(11, dpi);
    SelectObject(dc, SafeFont(normalFont));
    SetTextColor(dc, palette.text);
    DrawTextW(dc, presentation.body.c_str(), -1, &textRect,
        DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

void MacOsUiFramework::PaintStatus(HDC dc, const RECT& sourceRect,
    const StatusPresentation& presentation, const ThemePalette& palette,
    UINT dpi, HFONT normalFont, HFONT semiboldFont) const {
    if (!dc) return;

    const auto paintContents = [&](HDC target, const RECT& rect) {
        PaintPanel(target, rect, PanelPresentation{ false, false, 18 }, palette, dpi);

        const int iconSize = ScaleDip(18, dpi);
        RECT iconRect{ rect.left + ScaleDip(13, dpi),
            rect.top + (rect.bottom - rect.top - iconSize) / 2,
            rect.left + ScaleDip(13, dpi) + iconSize,
            rect.top + (rect.bottom - rect.top + iconSize) / 2 };
        HBRUSH iconBrush = CreateSolidBrush(BlendColor(palette.panelStrong, palette.accent, 0.10f));
        HPEN iconPen = CreatePen(PS_SOLID, std::max(1, ScaleDip(1, dpi)), palette.accent);
        if (iconBrush && iconPen) {
            HGDIOBJ oldBrush = SelectObject(target, iconBrush);
            HGDIOBJ oldPen = SelectObject(target, iconPen);
            Ellipse(target, iconRect.left, iconRect.top, iconRect.right, iconRect.bottom);
            SelectObject(target, oldBrush);
            SelectObject(target, oldPen);
        }
        if (iconBrush) DeleteObject(iconBrush);
        if (iconPen) DeleteObject(iconPen);

        HPEN checkPen = CreatePen(PS_SOLID, std::max(1, ScaleDip(2, dpi)), palette.accent);
        if (checkPen) {
            HGDIOBJ oldPen = SelectObject(target, checkPen);
            MoveToEx(target, iconRect.left + ScaleDip(4, dpi), iconRect.top + ScaleDip(9, dpi), nullptr);
            LineTo(target, iconRect.left + ScaleDip(8, dpi), iconRect.top + ScaleDip(13, dpi));
            LineTo(target, iconRect.left + ScaleDip(14, dpi), iconRect.top + ScaleDip(5, dpi));
            SelectObject(target, oldPen);
            DeleteObject(checkPen);
        }

        RECT textRect = rect;
        textRect.left += ScaleDip(43, dpi);
        textRect.right -= ScaleDip(13, dpi);
        HFONT oldFont = nullptr;
        if (!SelectFontObject(target, normalFont, oldFont)) return;
        SetBkMode(target, TRANSPARENT);
        RECT primaryRect = textRect;
        if (!presentation.secondaryText.empty()) {
            const LONG middle = primaryRect.top + (primaryRect.bottom - primaryRect.top) / 2;
            primaryRect.bottom = middle + ScaleDip(1, dpi);
        }
        RECT leftRect = primaryRect;
        RECT rightRect = primaryRect;
        if (!presentation.rightText.empty()) {
            const int available = std::max(0, static_cast<int>(textRect.right - textRect.left));
            const int rightWidth = std::min(std::max(ScaleDip(320, dpi), available * 3 / 5),
                std::max(0, available - ScaleDip(180, dpi)));
            rightRect.left = std::max(textRect.left, textRect.right - rightWidth);
            leftRect.right = std::max(leftRect.left, rightRect.left - ScaleDip(14, dpi));
        }
        SetTextColor(target, palette.muted);
        DrawTextW(target, presentation.leftText.c_str(), -1, &leftRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (!presentation.rightText.empty() && rightRect.right > rightRect.left) {
            SelectObject(target, SafeFont(semiboldFont));
            SetTextColor(target, palette.text);
            DrawTextW(target, presentation.rightText.c_str(), -1, &rightRect,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        if (!presentation.secondaryText.empty()) {
            SelectObject(target, SafeFont(normalFont));
            SetTextColor(target, palette.text);
            RECT secondaryRect = textRect;
            secondaryRect.top = primaryRect.bottom - ScaleDip(1, dpi);
            DrawTextW(target, presentation.secondaryText.c_str(), -1, &secondaryRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        SelectObject(target, oldFont);
    };

    const int width = std::max(1, static_cast<int>(sourceRect.right - sourceRect.left));
    const int height = std::max(1, static_cast<int>(sourceRect.bottom - sourceRect.top));
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = buffer ? CreateCompatibleBitmap(dc, width, height) : nullptr;
    HGDIOBJ oldBitmap = buffer && bitmap ? SelectObject(buffer, bitmap) : nullptr;
    if (buffer && bitmap && oldBitmap && oldBitmap != HGDI_ERROR) {
        const RECT local{ 0, 0, width, height };
        paintContents(buffer, local);
        BitBlt(dc, sourceRect.left, sourceRect.top, width, height, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(buffer);
        return;
    }
    if (oldBitmap && oldBitmap != HGDI_ERROR && buffer) SelectObject(buffer, oldBitmap);
    if (bitmap) DeleteObject(bitmap);
    if (buffer) DeleteDC(buffer);
    paintContents(dc, sourceRect);
}

int MacOsUiFramework::MeasureVerticalTabTextExtent(std::wstring_view text, UINT dpi) const {
    if (text.empty()) return 0;

    // Keep this measurement exactly in lock-step with PaintVerticalTab():
    // same family, size, weight, and GDI+ text renderer. The returned width
    // becomes the vertical height of the rotated tab.
    HDC dc = GetDC(nullptr);
    if (!dc) return 0;

    Gdiplus::Graphics graphics(dc);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    Gdiplus::Font font(L"Segoe UI", static_cast<Gdiplus::REAL>(ScaleDip(9, dpi)),
        Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip);

    const std::wstring ownedText(text);
    Gdiplus::RectF bounds{};
    graphics.MeasureString(ownedText.c_str(), -1, &font, Gdiplus::PointF(0.0f, 0.0f),
        &format, &bounds);

    ReleaseDC(nullptr, dc);
    return static_cast<int>(std::ceil(bounds.Width));
}

void MacOsUiFramework::PaintVerticalTab(HDC dc, const RECT& rect, std::wstring_view text,
    bool active, const ThemePalette& palette, UINT dpi, float activeBlend) const {
    (void)active; // activeBlend is the authoritative visual state for animated tabs.
    if (!dc) return;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    activeBlend = std::clamp(activeBlend, 0.0f, 1.0f);
    const COLORREF baseSurface = palette.panelStrong;
    const COLORREF baseBorder = BlendColor(palette.border, palette.panelStrong, 0.18f);
    const COLORREF surface = BlendColor(baseSurface, palette.accent, activeBlend);
    const COLORREF border = BlendColor(baseBorder, palette.accentBorder, activeBlend);
    FillRoundedRectGp(graphics, rect, static_cast<float>(ScaleDip(10, dpi)), ToGpColor(surface));
    DrawRoundedRectGp(graphics, rect, static_cast<float>(ScaleDip(10, dpi)),
        ToGpColor(border), static_cast<float>(std::max(2, ScaleDip(1, dpi))));

    const Gdiplus::GraphicsState state = graphics.Save();
    const float cx = static_cast<float>((rect.left + rect.right) / 2);
    const float cy = static_cast<float>((rect.top + rect.bottom) / 2);
    graphics.TranslateTransform(cx, cy);
    graphics.RotateTransform(-90.0f);
    Gdiplus::Font font(L"Segoe UI", static_cast<Gdiplus::REAL>(ScaleDip(9, dpi)),
        Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(ToGpColor(BlendColor(palette.muted, RGB(255, 255, 255), activeBlend)));
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    const Gdiplus::RectF textRect(-static_cast<float>(rect.bottom - rect.top) / 2.0f,
        -static_cast<float>(rect.right - rect.left) / 2.0f,
        static_cast<float>(rect.bottom - rect.top), static_cast<float>(rect.right - rect.left));
    const std::wstring ownedText(text);
    graphics.DrawString(ownedText.c_str(), -1, &font, textRect, &format, &brush);
    graphics.Restore(state);
}

void MacOsUiFramework::PaintSidebarTab(HDC dc, const RECT& rect, std::wstring_view text,
    bool active, const ThemePalette& palette, UINT dpi, float activeBlend) const {
    (void)active; // activeBlend is the authoritative visual state for animated tabs.
    if (!dc) return;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    activeBlend = std::clamp(activeBlend, 0.0f, 1.0f);
    const COLORREF baseSurface = palette.panelStrong;
    const COLORREF baseBorder = BlendColor(palette.border, palette.panelStrong, 0.18f);
    const COLORREF surface = BlendColor(baseSurface, palette.accent, activeBlend);
    const COLORREF border = BlendColor(baseBorder, palette.accentBorder, activeBlend);
    using quicksift::ui::layout::kSidebarTabPadDip;
    using quicksift::ui::layout::kSidebarTabRadiusDip;
    const float tabRadius = static_cast<float>(ScaleDip(kSidebarTabRadiusDip, dpi));
    FillRoundedRectGp(graphics, rect, tabRadius, ToGpColor(surface));
    DrawRoundedRectGp(graphics, rect, tabRadius,
        ToGpColor(border), static_cast<float>(std::max(2, ScaleDip(1, dpi))));

    Gdiplus::Font font(L"Segoe UI", static_cast<Gdiplus::REAL>(ScaleDip(10, dpi)),
        Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(ToGpColor(BlendColor(palette.muted, RGB(255, 255, 255), activeBlend)));
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    const float pad = static_cast<float>(ScaleDip(kSidebarTabPadDip, dpi));
    const Gdiplus::RectF textRect(
        static_cast<float>(rect.left) + pad,
        static_cast<float>(rect.top),
        std::max(1.0f, static_cast<float>(rect.right - rect.left) - pad * 2.0f),
        static_cast<float>(rect.bottom - rect.top));
    const std::wstring ownedText(text);
    graphics.DrawString(ownedText.c_str(), -1, &font, textRect, &format, &brush);
}

void MacOsUiFramework::PaintMenuItem(HDC dc, const RECT& rect, const MenuItem& item,
    bool selected, const ThemePalette& palette, UINT dpi, HFONT font) const {
    if (!dc) return;
    if (item.separator) {
        FillSolidRect(dc, rect, palette.menu);
        const int y = (rect.top + rect.bottom) / 2;
        DrawSolidLine(dc, rect.left + ScaleDip(12, dpi), y,
            rect.right - ScaleDip(12, dpi), y, 1, palette.border);
        return;
    }
    if (item.header) {
        FillSolidRect(dc, rect, palette.menu);
        HFONT oldFont = nullptr;
        if (!SelectFontObject(dc, font, oldFont)) return;
        LOGFONTW lf{};
        GetObjectW(font, sizeof(lf), &lf);
        lf.lfWeight = FW_SEMIBOLD;
        HFONT headerFont = CreateFontIndirectW(&lf);
        HFONT activeFont = headerFont ? headerFont : font;
        SelectObject(dc, activeFont);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, palette.muted);
        RECT textRect = rect;
        textRect.left += ScaleDip(12, dpi);
        textRect.right -= ScaleDip(12, dpi);
        DrawTextW(dc, item.text.c_str(), -1, &textRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, oldFont);
        if (headerFont) DeleteObject(headerFont);
        return;
    }
    FillSolidRect(dc, rect, selected ? palette.menuSelected : palette.menu);
    HFONT oldFont = nullptr;
    if (!SelectFontObject(dc, font, oldFont)) return;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, item.destructive ? palette.dangerBorder : palette.text);
    RECT textRect = rect;
    textRect.left += ScaleDip(36, dpi);
    textRect.right -= ScaleDip(10, dpi);
    DrawTextW(dc, item.text.c_str(), -1, &textRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (item.checked) {
        RECT mark{ rect.left + ScaleDip(12, dpi), rect.top + ScaleDip(9, dpi),
            rect.left + ScaleDip(24, dpi), rect.bottom - ScaleDip(9, dpi) };
        HPEN pen = CreatePen(PS_SOLID, std::max(1, ScaleDip(2, dpi)), palette.accent);
        if (pen) {
            HGDIOBJ oldPen = SelectObject(dc, pen);
            MoveToEx(dc, mark.left, (mark.top + mark.bottom) / 2, nullptr);
            LineTo(dc, mark.left + ScaleDip(4, dpi), mark.bottom);
            LineTo(dc, mark.right, mark.top);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
        }
    }
    SelectObject(dc, oldFont);
}

void MacOsUiFramework::PaintCenteredMutedText(HDC dc, const RECT& rect, std::wstring_view text,
    const ThemePalette& palette, UINT dpi, HFONT font) const {
    (void)dpi;
    if (!dc || IsRectEmpty(&rect)) return;
    FillSolidRect(dc, rect, palette.panel);
    HFONT oldFont = nullptr;
    if (!SelectFontObject(dc, font, oldFont)) return;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, palette.muted);
    RECT textRect = rect;
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &textRect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

void MacOsUiFramework::PaintWindowBackground(HDC dc, const RECT& rect,
    const ThemePalette& palette) const {
    if (!dc) return;
    FillVerticalGradient(dc, rect, palette.windowTop, palette.windowBottom);
}

void MacOsUiFramework::PaintAccentStrip(HDC dc, const RECT& rect,
    const ThemePalette& palette) const {
    if (!dc || IsRectEmpty(&rect)) return;
    FillSolidRect(dc, rect, palette.accent);
}

void MacOsUiFramework::PaintSolidSurface(HDC dc, const RECT& rect,
    COLORREF color) const {
    if (!dc || IsRectEmpty(&rect)) return;
    FillSolidRect(dc, rect, color);
}

void MacOsUiFramework::PaintRoundedShadow(HDC dc, const RECT& rect, int radius,
    COLORREF color) const {
    if (!dc || IsRectEmpty(&rect)) return;
    FillRoundedRect(dc, rect, std::max(0, radius), color, color);
}

void MacOsUiFramework::PaintTitleBar(HDC dc, const RECT& client, int height,
    const TitleBarPresentation& presentation, const ThemePalette& palette,
    UINT dpi) const {
    if (!dc) return;
    const int width = std::max(1, static_cast<int>(client.right - client.left));
    height = std::max(1, height);
    HDC bufferDc = CreateCompatibleDC(dc);
    HBITMAP bufferBitmap = bufferDc ? CreateCompatibleBitmap(dc, width, height) : nullptr;
    if (!bufferDc || !bufferBitmap) {
        if (bufferBitmap) DeleteObject(bufferBitmap);
        if (bufferDc) DeleteDC(bufferDc);
        return;
    }
    HGDIOBJ oldBitmap = SelectObject(bufferDc, bufferBitmap);
    if (!oldBitmap || oldBitmap == HGDI_ERROR) {
        DeleteObject(bufferBitmap);
        DeleteDC(bufferDc);
        return;
    }

    RECT titleRect{ 0, 0, width, height };
    const COLORREF titleFill = BlendColor(palette.toolbar, palette.windowTop,
        palette.dark ? 0.12f : 0.22f);
    const COLORREF titleBottom = BlendColor(titleFill, palette.windowBottom,
        palette.dark ? 0.06f : 0.10f);
    FillVerticalGradient(bufferDc, titleRect, titleFill, titleBottom);

    {
        Gdiplus::Graphics graphics(bufferDc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
        for (const TrafficLightPresentation& light : presentation.trafficLights) {
            if (presentation.active && light.hot) {
                for (int ring = 4; ring >= 1; --ring) {
                    RECT halo = light.rect;
                    InflateRect(&halo, ScaleDip(ring * 2, dpi), ScaleDip(ring * 2, dpi));
                    const BYTE alpha = static_cast<BYTE>(18 + (5 - ring) * 14);
                    Gdiplus::SolidBrush glow(ToGpColor(light.color, alpha));
                    graphics.FillEllipse(&glow, ToGpRect(halo));
                }
            }

            COLORREF fill = light.color;
            if (!presentation.active) fill = BlendColor(palette.muted, titleFill, 0.62f);
            else if (light.pressed) fill = BlendColor(light.color, RGB(0, 0, 0), 0.20f);
            else if (light.hot) fill = BlendColor(light.color, RGB(255, 255, 255), 0.12f);
            Gdiplus::SolidBrush brush(ToGpColor(fill));
            graphics.FillEllipse(&brush, ToGpRect(light.rect));
            const float outlineWidth = static_cast<float>(std::max(1, ScaleDip(1, dpi)));
            Gdiplus::Pen border(ToGpColor(BlendColor(fill, RGB(0, 0, 0),
                presentation.active ? 0.18f : 0.08f)), outlineWidth);
            Gdiplus::RectF outline = ToGpRect(light.rect);
            const float inset = outlineWidth * 0.5f;
            outline.X += inset;
            outline.Y += inset;
            outline.Width -= outlineWidth;
            outline.Height -= outlineWidth;
            graphics.DrawEllipse(&border, outline);
        }
    }

    HFONT oldFont = nullptr;
    if (SelectFontObject(bufferDc, presentation.titleFont, oldFont)) {
        SetBkMode(bufferDc, TRANSPARENT);
        SetTextColor(bufferDc, presentation.active ? palette.title : palette.muted);
        RECT textRect = presentation.titleTextRect;
        DrawTextW(bufferDc, presentation.title.c_str(), -1, &textRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(bufferDc, oldFont);
    }
    DrawSolidLine(bufferDc, 0, height - 1, width, height - 1, 1,
        BlendColor(palette.border, titleFill, 0.30f));

    BitBlt(dc, client.left, client.top, width, height, bufferDc, 0, 0, SRCCOPY);
    SelectObject(bufferDc, oldBitmap);
    DeleteObject(bufferBitmap);
    DeleteDC(bufferDc);
}

void MacOsUiFramework::PaintOverlayScrollbar(HDC dc, const RECT& thumb,
    const RECT& track, bool hot, bool dragging, const ThemePalette& palette,
    UINT dpi) const {
    if (!dc || IsRectEmpty(&thumb)) return;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    if ((hot || dragging) && !IsRectEmpty(&track)) {
        FillRoundedRectGp(graphics, track, static_cast<float>(ScaleDip(3, dpi)),
            ToGpColor(BlendColor(palette.border, palette.panel, 0.55f), 90));
    }
    FillRoundedRectGp(graphics, thumb, static_cast<float>(ScaleDip(3, dpi)),
        ToGpColor(dragging ? palette.accent : palette.muted,
            hot || dragging ? 190 : 120));
}

void MacOsUiFramework::ApplyRoundedRegion(HWND window, int width, int height, int radius,
    bool redraw) noexcept {
    if (!window || width <= 0 || height <= 0) return;
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1,
        std::max(1, radius * 2), std::max(1, radius * 2));
    if (!region) return;
    // TRUE forces an immediate system redraw of the HWND (and can flash siblings).
    // Animation frames pass redraw=false and invalidate once themselves.
    if (!SetWindowRgn(window, region, redraw ? TRUE : FALSE)) DeleteObject(region);
}

} // namespace quicksift::ui::framework
