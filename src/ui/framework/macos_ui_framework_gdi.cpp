// OWNER: Central macOS-inspired GDI/GDI+ component renderer.
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include "ui/framework/macos_ui_framework.h"
#include "core/pane_scroll_policy.h"
#include "ui/ui_layout_metrics.h"


#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cwchar>

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
    const float lineWidth = StrokePx(tokens::kIconStroke, dpi);
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
    Gdiplus::Pen pen(ToGpColor(color), StrokePx(tokens::kIconStroke, dpi));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    const float cx = static_cast<float>((rect.left + rect.right) / 2);
    const float cy = static_cast<float>((rect.top + rect.bottom) / 2);
    graphics.DrawLine(&pen, cx - ScaleDip(3, dpi), cy - ScaleDip(1, dpi),
        cx, cy + ScaleDip(2, dpi));
    graphics.DrawLine(&pen, cx, cy + ScaleDip(2, dpi),
        cx + ScaleDip(3, dpi), cy - ScaleDip(1, dpi));
}

bool MacOsUiFramework::PaintButton(HDC dc, const RECT& sourceRect,
    const ButtonPresentation& presentation, const ThemePalette& palette,
    UINT dpi, HFONT normalFont, HFONT semiboldFont) const {
    if (!dc) return false;
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

    // Subtle macOS press: ~1 DIP inset (≈0.97 scale on a 32 DIP button) plus a
    // darken (lighten in dark mode) of the existing fill, no per-frame bitmap effects.
    if (pressEase > 0.0f && !header) {
        const int inset = static_cast<int>(std::lround(
            static_cast<float>(ScaleDip(1, dpi)) * pressEase));
        if (inset > 0) InflateRect(&rect, -inset, -inset);
    }
    const tokens::GlassMaterial material = tokens::Glass(palette.dark, CurrentVisualEffects());

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
        const COLORREF pressedFill = BlendColor(fill,
            palette.dark ? RGB(255, 255, 255) : RGB(0, 0, 0), palette.dark ? 0.10f : 0.08f);
        fill = BlendColor(fill, pressedFill, pressEase);
        border = BlendColor(border, BlendColor(palette.accent, palette.border, 0.42f), pressEase);
    }
    if (state.selected) {
        fill = palette.accent;
        border = palette.accentBorder;
        text = palette.accentText;
        if (pressEase > 0.0f) {
            fill = BlendColor(fill, palette.dark ? RGB(255, 255, 255) : RGB(0, 0, 0),
                0.10f * pressEase);
        }
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
        if (presentation.parentSurface) {
            // Parent-coloured corners: the whole item rect starts as the real parent
            // surface, so the AA pill edge blends into it (no halo, no fringe
            // accumulation across hover repaints). Paired with the slack region.
            Gdiplus::SolidBrush parentBrush(ToGpColor(static_cast<COLORREF>(*presentation.parentSurface)));
            graphics.FillRectangle(&parentBrush, ToGpRect(sourceRect));
        } else {
            // Erase with the SAME rounded shape as the chrome. A rectangular FillSolidRect
            // previously peeked behind pill corners as a dark mismatched underlay.
            FillRoundedRectGp(graphics, sourceRect, static_cast<float>(radius), ToGpColor(surface));
        }
        if (!header && !title && pressEase < 0.85f && !palette.highContrast) {
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
            const bool glass = material.sheenAlpha > 0.0f;
            RECT highlight = rect;
            highlight.bottom = highlight.top + std::max(2, static_cast<int>((highlight.bottom - highlight.top) / 2));
            // Approximate highlight with a shorter rounded fill; clamp radius to half-height.
            const int highlightRadius = std::min(radius,
                std::max(1, static_cast<int>((highlight.bottom - highlight.top) / 2)));
            if (glass) {
                FillRoundedRectGp(graphics, highlight, static_cast<float>(highlightRadius),
                    ToGpColor(topFill, static_cast<BYTE>(palette.dark ? 40 : 55)));
            }
            // Outer border + subtle inner edge for depth.
            DrawRoundedRectGp(graphics, rect, static_cast<float>(radius), ToGpColor(border),
                static_cast<float>(std::max(1, ScaleDip(1, dpi))));
            RECT inner = rect;
            InflateRect(&inner, -ScaleDip(1, dpi), -ScaleDip(1, dpi));
            if (glass && inner.right > inner.left && inner.bottom > inner.top) {
                const COLORREF innerEdge = BlendColor(fill,
                    palette.dark ? RGB(255, 255, 255) : RGB(255, 255, 255),
                    palette.dark ? 0.10f : 0.28f);
                DrawRoundedRectGp(graphics, inner, static_cast<float>(std::max(1, radius - 1)),
                    ToGpColor(innerEdge, static_cast<BYTE>(palette.dark ? 70 : 90)), 1.0f);
            }
        }
    }

    HFONT oldFont = nullptr;
    if (!SelectFontObject(dc, semiboldFont ? semiboldFont : normalFont, oldFont)) return false;
    bool truncated = false;
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
        SIZE extent{};
        if (!presentation.text.empty() && GetTextExtentPoint32W(dc, presentation.text.c_str(),
                static_cast<int>(presentation.text.size()), &extent)) {
            truncated = quicksift::core::caption::Truncated(static_cast<int>(extent.cx),
                static_cast<int>(rect.right - rect.left), padding,
                state.popup ? ScaleDip(kButtonPopupChevronDip, dpi) : 0);
        }
        DrawTextW(dc, presentation.text.c_str(), -1, &textRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (state.popup && !state.disabled) {
            RECT chevron{ rect.right - ScaleDip(kButtonPopupChevronDip + 3, dpi), rect.top,
                rect.right - ScaleDip(8, dpi), rect.bottom };
            PaintChevron(dc, chevron, text, dpi);
        }
    }

    if (state.focused && !state.disabled && !header) {
        // macOS-style ring hugging the shape. A child HWND cannot draw outside its
        // client (the old outer glow was inflated past it and clipped flat on every
        // side), so the ring occupies the outermost kFocusRingWidth of the control,
        // concentric with the chrome and replacing its border. On accent-filled
        // (selected) buttons the ring sits just inside in the accent text colour.
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        const float ringWidth = StrokePx(tokens::kFocusRingWidth, dpi);
        if (state.selected) {
            RECT inner = rect;
            const int inset = ScaleDip(2, dpi);
            InflateRect(&inner, -inset, -inset);
            DrawRoundedRectGp(graphics, inner, static_cast<float>(std::max(1, radius - inset)),
                ToGpColor(palette.accentText, 220), StrokePx(tokens::kHairline, dpi));
        } else {
            DrawRoundedRectGp(graphics, rect, static_cast<float>(radius),
                ToGpColor(palette.accent, palette.highContrast ? 255 : 210), ringWidth);
        }
    }
    SelectObject(dc, oldFont);
    return truncated;
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
    FillRoundedRect(dc, rect, ScaleDip(quicksift::ui::layout::kLabelRadiusDip, dpi) * 2,
        palette.panelStrong, palette.border);
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
    {
        // Section headers sit on the panelStrong flyout surface: paint the whole
        // item rect with it (parent-coloured corners) and stroke an AA border at the
        // same radius as the (slack) window region.
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        Gdiplus::SolidBrush surface(ToGpColor(palette.panelStrong));
        graphics.FillRectangle(&surface, ToGpRect(rect));
        DrawRoundedRectGp(graphics, rect, static_cast<float>(radius),
            ToGpColor(BlendColor(palette.border, palette.panelStrong, 0.18f)), 1.0f);
    }
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
        // Parent-coloured corners + the same radius as the HWND region's visual
        // shape (layout uses ApplyAntialiasedControlRegion with kStatusRadiusDip).
        // The buffer used to start black, so the region/paint radius mismatch
        // (17 vs 18) left dark specks in the corners on the light theme.
        PaintSolidSurface(target, rect, presentation.parentSurface == CLR_INVALID
            ? palette.windowBottom : presentation.parentSurface);
        PaintPanel(target, rect, PanelPresentation{ false, false, quicksift::ui::layout::kStatusRadiusDip },
            palette, dpi);

        const int iconSize = ScaleDip(18, dpi);
        RECT iconRect{ rect.left + ScaleDip(13, dpi),
            rect.top + (rect.bottom - rect.top - iconSize) / 2,
            rect.left + ScaleDip(13, dpi) + iconSize,
            rect.top + (rect.bottom - rect.top + iconSize) / 2 };
        {
            // Anti-aliased status badge (was GDI Ellipse + MoveToEx/LineTo).
            Gdiplus::Graphics graphics(target);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            const float penWidth = StrokePx(tokens::kHairline, dpi);
            Gdiplus::RectF circle = ToGpRect(iconRect);
            circle.X += penWidth * 0.5f;
            circle.Y += penWidth * 0.5f;
            circle.Width -= penWidth;
            circle.Height -= penWidth;
            Gdiplus::SolidBrush iconFill(ToGpColor(BlendColor(palette.panelStrong, palette.accent, 0.10f)));
            graphics.FillEllipse(&iconFill, circle);
            Gdiplus::Pen ring(ToGpColor(palette.accent), penWidth);
            graphics.DrawEllipse(&ring, circle);
            Gdiplus::Pen check(ToGpColor(palette.accent), StrokePx(tokens::kIconStroke, dpi));
            check.SetStartCap(Gdiplus::LineCapRound);
            check.SetEndCap(Gdiplus::LineCapRound);
            check.SetLineJoin(Gdiplus::LineJoinRound);
            const Gdiplus::PointF points[3]{
                Gdiplus::PointF(static_cast<float>(iconRect.left + ScaleDip(4, dpi)),
                    static_cast<float>(iconRect.top + ScaleDip(9, dpi))),
                Gdiplus::PointF(static_cast<float>(iconRect.left + ScaleDip(8, dpi)),
                    static_cast<float>(iconRect.top + ScaleDip(13, dpi))),
                Gdiplus::PointF(static_cast<float>(iconRect.left + ScaleDip(14, dpi)),
                    static_cast<float>(iconRect.top + ScaleDip(5, dpi))) };
            graphics.DrawLines(&check, points, 3);
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

HFONT MacOsUiFramework::SidebarTabFont(UINT dpi) const noexcept {
    if (dpi == 0) dpi = USER_DEFAULT_SCREEN_DPI;
    if (sidebarTabFont_ && sidebarTabFontDpi_ == dpi) return sidebarTabFont_;
    // Same metrics as the former GDI+ label: em size ScaleDip(10) px, bold.
    HFONT created = CreateFontW(-ScaleDip(10, dpi), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, tokens::kFamilyFallback);
    if (!created) return sidebarTabFont_; // keep the previous one (any DPI) rather than none
    if (sidebarTabFont_) DeleteObject(sidebarTabFont_);
    sidebarTabFont_ = created;
    sidebarTabFontDpi_ = dpi;
    return sidebarTabFont_;
}

void MacOsUiFramework::PaintSidebarTab(HDC dc, const RECT& rect, std::wstring_view text,
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
    using quicksift::ui::layout::kSidebarTabPadDip;
    using quicksift::ui::layout::kSidebarTabRadiusDip;
    const float tabRadius = static_cast<float>(ScaleDip(kSidebarTabRadiusDip, dpi));
    FillRoundedRectGp(graphics, rect, tabRadius, ToGpColor(surface));
    DrawRoundedRectGp(graphics, rect, tabRadius,
        ToGpColor(border), static_cast<float>(std::max(2, ScaleDip(1, dpi))));

    graphics.Flush(Gdiplus::FlushIntentionSync); // shapes land before the GDI text

    // GDI label (no GDI+ font: see SidebarTabFont). Left-aligned, vertically
    // centred, single line, end ellipsis — the former StringFormat settings.
    HFONT font = SidebarTabFont(dpi);
    HFONT oldFont = nullptr;
    if (!SelectFontObject(dc, font, oldFont)) return;
    const int pad = ScaleDip(kSidebarTabPadDip, dpi);
    RECT textRect{ rect.left + pad, rect.top, std::max(rect.left + pad + 1, rect.right - pad), rect.bottom };
    const int oldMode = SetBkMode(dc, TRANSPARENT);
    const COLORREF oldColor = SetTextColor(dc, BlendColor(palette.muted, palette.accentText, activeBlend));
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &textRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SetTextColor(dc, oldColor);
    SetBkMode(dc, oldMode);
    if (oldFont) SelectObject(dc, oldFont);
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
        // One semibold derivative per source font (was CreateFontIndirectW +
        // DeleteObject on every header paint, i.e. every hover repaint).
        // Keyed by the LOGFONT (not the HFONT value, which GDI may reuse after
        // a DPI change recreates the UI fonts). UI thread only.
        static LOGFONTW cachedKey{};
        static HFONT cachedHeader = nullptr;
        HFONT activeFont = font;
        LOGFONTW lf{};
        if (font && GetObjectW(font, sizeof(lf), &lf) == sizeof(lf)) {
            lf.lfWeight = FW_SEMIBOLD;
            if (!cachedHeader || std::memcmp(&lf, &cachedKey, sizeof(lf)) != 0) {
                HFONT created = CreateFontIndirectW(&lf);
                if (created) {
                    if (cachedHeader) DeleteObject(cachedHeader);
                    cachedHeader = created;
                    cachedKey = lf;
                }
            }
            if (cachedHeader && std::memcmp(&lf, &cachedKey, sizeof(lf)) == 0) activeFont = cachedHeader;
        }
        SelectObject(dc, activeFont);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, palette.muted);
        RECT textRect = rect;
        textRect.left += ScaleDip(12, dpi);
        textRect.right -= ScaleDip(12, dpi);
        DrawTextW(dc, item.text.c_str(), -1, &textRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, oldFont);
        return;
    }
    FillSolidRect(dc, rect, palette.menu);
    if (selected) {
        // macOS menu highlight: a rounded selection inset from the menu edges.
        RECT highlight = rect;
        InflateRect(&highlight, -ScaleDip(quicksift::ui::layout::kMenuSelectionInsetDip, dpi) / 2, 0);
        highlight.top += std::max(1, ScaleDip(1, dpi));
        highlight.bottom -= std::max(1, ScaleDip(1, dpi));
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        FillRoundedRectGp(graphics, highlight,
            static_cast<float>(ScaleDip(quicksift::ui::layout::kMenuSelectionRadiusDip, dpi)),
            ToGpColor(palette.menuSelected));
    }
    HFONT oldFont = nullptr;
    if (!SelectFontObject(dc, font, oldFont)) return;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, item.destructive ? palette.dangerBorder :
        (selected ? MenuSelectedTextColor(palette) : palette.text));
    RECT textRect = rect;
    textRect.left += ScaleDip(36, dpi);
    textRect.right -= ScaleDip(10, dpi);
    DrawTextW(dc, item.text.c_str(), -1, &textRect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (item.checked) {
        RECT mark{ rect.left + ScaleDip(12, dpi), rect.top + ScaleDip(9, dpi),
            rect.left + ScaleDip(24, dpi), rect.bottom - ScaleDip(9, dpi) };
        // Anti-aliased check mark (was GDI MoveToEx/LineTo).
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        Gdiplus::Pen pen(ToGpColor(selected && palette.highContrast ? MenuSelectedTextColor(palette)
            : palette.accent), StrokePx(tokens::kIconStroke, dpi));
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        const Gdiplus::PointF points[3]{
            Gdiplus::PointF(static_cast<float>(mark.left), static_cast<float>((mark.top + mark.bottom) / 2)),
            Gdiplus::PointF(static_cast<float>(mark.left + ScaleDip(4, dpi)), static_cast<float>(mark.bottom)),
            Gdiplus::PointF(static_cast<float>(mark.right), static_cast<float>(mark.top)) };
        graphics.DrawLines(&pen, points, 3);
    }
    SelectObject(dc, oldFont);
}

void MacOsUiFramework::PaintCenteredMutedText(HDC dc, const RECT& rect, std::wstring_view text,
    const ThemePalette& palette, UINT dpi, HFONT font) const {
    (void)dpi;
    if (!dc || IsRectEmpty(&rect)) return;
    // Text only: the caller owns the surface (the library list's empty state used
    // to lose its rounded border because this filled the whole rect).
    HFONT oldFont = nullptr;
    if (!SelectFontObject(dc, font, oldFont)) return;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, palette.muted);
    RECT textRect = rect;
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &textRect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
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

MacOsUiFramework::~MacOsUiFramework() {
    ClearPaneFrost();
    if (sidebarTabFont_) DeleteObject(sidebarTabFont_);
    sidebarTabFont_ = nullptr;
}

void MacOsUiFramework::ClearPaneFrost() noexcept {
    if (paneFrostDc_ && paneFrostPrevious_) SelectObject(paneFrostDc_, paneFrostPrevious_);
    if (paneFrostBitmap_) DeleteObject(paneFrostBitmap_);
    if (paneFrostDc_) DeleteDC(paneFrostDc_);
    paneFrostDc_ = nullptr;
    paneFrostBitmap_ = nullptr;
    paneFrostPrevious_ = nullptr;
    paneFrostWidth_ = 0;
    paneFrostHeight_ = 0;
}

void MacOsUiFramework::SetPaneFrost(const frost::Image& image) {
    if (!image.Valid()) {
        ClearPaneFrost();
        return;
    }
    if (!paneFrostBitmap_ || paneFrostWidth_ != image.width || paneFrostHeight_ != image.height) {
        ClearPaneFrost();
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = image.width;
        info.bmiHeader.biHeight = -image.height; // top-down, same row order as frost::Image
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
        HGDIOBJ previous = bitmap ? SelectObject(dc, bitmap) : nullptr;
        if (!dc || !bitmap || !bits || !previous || previous == HGDI_ERROR) {
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
            return;
        }
        paneFrostDc_ = dc;
        paneFrostBitmap_ = bitmap;
        paneFrostPrevious_ = previous;
        paneFrostWidth_ = image.width;
        paneFrostHeight_ = image.height;
    }
    DIBSECTION section{};
    if (GetObjectW(paneFrostBitmap_, sizeof(section), &section) != sizeof(section) || !section.dsBm.bmBits) {
        ClearPaneFrost();
        return;
    }
    GdiFlush();
    auto* rows = static_cast<std::uint8_t*>(section.dsBm.bmBits);
    const std::size_t rowBytes = static_cast<std::size_t>(image.width) * 4u;
    const std::size_t stride = static_cast<std::size_t>(section.dsBm.bmWidthBytes);
    for (int y = 0; y < image.height; ++y) {
        std::memcpy(rows + static_cast<std::size_t>(y) * stride,
            image.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width), rowBytes);
    }
}

void MacOsUiFramework::PaintPaneFrost(HDC dc, const RECT& dest, POINT source,
    COLORREF fallback) const {
    if (!dc || IsRectEmpty(&dest)) return;
    const int width = static_cast<int>(dest.right - dest.left);
    const int height = static_cast<int>(dest.bottom - dest.top);
    // Intersection of the requested slice with the frost image.
    const int sx0 = std::max(0, static_cast<int>(source.x));
    const int sy0 = std::max(0, static_cast<int>(source.y));
    const int sx1 = std::min(paneFrostWidth_, static_cast<int>(source.x) + width);
    const int sy1 = std::min(paneFrostHeight_, static_cast<int>(source.y) + height);
    const bool any = paneFrostDc_ && sx1 > sx0 && sy1 > sy0;
    const bool covers = any && sx0 == source.x && sy0 == source.y &&
        sx1 - sx0 == width && sy1 - sy0 == height;
    if (!covers) FillSolidRect(dc, dest, fallback);
    if (any) {
        BitBlt(dc, dest.left + (sx0 - source.x), dest.top + (sy0 - source.y),
            sx1 - sx0, sy1 - sy0, paneFrostDc_, sx0, sy0, SRCCOPY);
    }
}

void MacOsUiFramework::PaintFrostedPanelChrome(HDC dc, const RECT& rect, int radiusDip, bool wash,
    const ThemePalette& palette, UINT dpi) const {
    if (!dc || IsRectEmpty(&rect)) return;
    const int radius = ScaleDip(radiusDip, dpi);
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    if (wash && !palette.highContrast) {
        RECT highlight = rect;
        const int height = std::max(1, static_cast<int>(rect.bottom - rect.top));
        highlight.bottom = highlight.top + std::max(2, std::min(height / 2, ScaleDip(120, dpi)));
        const int highlightRadius = std::min(radius,
            std::max(1, static_cast<int>((highlight.bottom - highlight.top) / 2)));
        const COLORREF topWash = BlendColor(palette.panelStrong, RGB(255, 255, 255),
            palette.dark ? 0.08f : 0.14f);
        FillRoundedRectGp(graphics, highlight, static_cast<float>(highlightRadius),
            ToGpColor(topWash, static_cast<BYTE>(palette.dark ? 28 : 40)));
    }
    DrawRoundedRectGp(graphics, rect, static_cast<float>(radius),
        ToGpColor(BlendColor(palette.border, palette.panelStrong, 0.18f)), 1.0f);
}

void MacOsUiFramework::PaintBuffered(HDC target, const RECT& rect,
    const std::function<void(HDC, const RECT&)>& paint) const {
    if (!target || !paint) return;
    const int width = std::max(0, static_cast<int>(rect.right - rect.left));
    const int height = std::max(0, static_cast<int>(rect.bottom - rect.top));
    if (width == 0 || height == 0) return;
    HDC buffer = CreateCompatibleDC(target);
    HBITMAP bitmap = buffer ? CreateCompatibleBitmap(target, width, height) : nullptr;
    HGDIOBJ previous = bitmap ? SelectObject(buffer, bitmap) : nullptr;
    if (!buffer || !bitmap || !previous || previous == HGDI_ERROR) {
        if (bitmap) DeleteObject(bitmap);
        if (buffer) DeleteDC(buffer);
        paint(target, rect); // unbuffered fallback (low memory)
        return;
    }
    const RECT local{ 0, 0, width, height };
    paint(buffer, local);
    BitBlt(target, rect.left, rect.top, width, height, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, previous);
    DeleteObject(bitmap);
    DeleteDC(buffer);
}

void MacOsUiFramework::PaintItemBuffered(const DRAWITEMSTRUCT& draw,
    const std::function<void(HDC, const RECT&)>& background,
    const std::function<void(const DRAWITEMSTRUCT&)>& paint) const {
    if (!draw.hDC || !paint) return;
    PaintBuffered(draw.hDC, draw.rcItem, [&](HDC dc, const RECT& local) {
        if (background) background(dc, local);
        DRAWITEMSTRUCT copy = draw;
        copy.hDC = dc;
        copy.rcItem = local;
        paint(copy);
    });
}

void MacOsUiFramework::PaintRoundedShadow(HDC dc, const RECT& rect, int radius,
    COLORREF color) const {
    if (!dc || IsRectEmpty(&rect)) return;
    FillRoundedRect(dc, rect, std::max(0, radius), color, color);
}

namespace {
COLORREF TitleBarTopColor(const ThemePalette& palette) noexcept {
    return BlendColor(palette.toolbar, palette.windowTop, palette.dark ? 0.12f : 0.22f);
}
COLORREF TitleBarBottomColor(const ThemePalette& palette) noexcept {
    return BlendColor(TitleBarTopColor(palette), palette.windowBottom, palette.dark ? 0.06f : 0.10f);
}
} // namespace

COLORREF MacOsUiFramework::TitleBarSurfaceColor(const ThemePalette& palette) noexcept {
    return BlendColor(TitleBarTopColor(palette), TitleBarBottomColor(palette), 0.5f);
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
    const COLORREF titleFill = TitleBarTopColor(palette);
    const COLORREF titleBottom = TitleBarBottomColor(palette);
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
        FillRoundedRectGp(graphics, track, static_cast<float>(
            std::min(track.right - track.left, track.bottom - track.top)) * 0.5f,
            ToGpColor(BlendColor(palette.border, palette.panel, 0.55f), 90));
    }
    (void)dpi;
    FillRoundedRectGp(graphics, thumb, static_cast<float>(
        std::min(thumb.right - thumb.left, thumb.bottom - thumb.top)) * 0.5f,
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


namespace quicksift::ui::framework {

namespace { constexpr wchar_t kCheckboxCheckedProperty[] = L"QuickSift.CheckboxChecked"; }

void MacOsUiFramework::SetCheckboxChecked(HWND checkbox, bool checked) noexcept {
    if (!checkbox) return;
    if (checked) SetPropW(checkbox, kCheckboxCheckedProperty, reinterpret_cast<HANDLE>(1));
    else RemovePropW(checkbox, kCheckboxCheckedProperty);
    InvalidateRect(checkbox, nullptr, FALSE);
}

bool MacOsUiFramework::IsCheckboxChecked(HWND checkbox) noexcept {
    return checkbox && GetPropW(checkbox, kCheckboxCheckedProperty) != nullptr;
}

void MacOsUiFramework::PaintCheckbox(HDC dc, const RECT& rect, std::wstring_view text, bool checked,
    bool hot, bool focused, bool disabled, const ThemePalette& palette, UINT dpi,
    HFONT font, COLORREF parentSurface) const {
    if (!dc) return;
    const int scaleDpi = static_cast<int>(std::max<UINT>(USER_DEFAULT_SCREEN_DPI, dpi));
    const auto px = [scaleDpi](int dip) { return std::max(1, MulDiv(dip, scaleDpi, USER_DEFAULT_SCREEN_DPI)); };
    HBRUSH surface = CreateSolidBrush(parentSurface);
    if (surface) { FillRect(dc, &rect, surface); DeleteObject(surface); }
    const int box = px(16);
    const int top = rect.top + (rect.bottom - rect.top - box) / 2;
    RECT boxRect{ rect.left + px(2), top, rect.left + px(2) + box, top + box };
    const COLORREF fill = checked ? palette.accent : (hot ? palette.buttonHot : palette.button);
    const COLORREF border = checked ? palette.accentBorder : (hot || focused ? palette.accent : palette.border);
    HBRUSH fillBrush = CreateSolidBrush(disabled ? palette.buttonDisabled : fill);
    HPEN borderPen = CreatePen(PS_SOLID, px(1), border);
    HGDIOBJ oldBrush = fillBrush ? SelectObject(dc, fillBrush) : nullptr;
    HGDIOBJ oldPen = borderPen ? SelectObject(dc, borderPen) : nullptr;
    RoundRect(dc, boxRect.left, boxRect.top, boxRect.right, boxRect.bottom, px(5), px(5));
    if (oldBrush) SelectObject(dc, oldBrush);
    if (oldPen) SelectObject(dc, oldPen);
    if (fillBrush) DeleteObject(fillBrush);
    if (borderPen) DeleteObject(borderPen);
    if (checked) {
        HPEN tick = CreatePen(PS_SOLID, px(2), palette.accentText);
        HGDIOBJ previous = tick ? SelectObject(dc, tick) : nullptr;
        const POINT points[3] = {
            { boxRect.left + box * 4 / 16, boxRect.top + box * 8 / 16 },
            { boxRect.left + box * 7 / 16, boxRect.top + box * 11 / 16 },
            { boxRect.left + box * 12 / 16, boxRect.top + box * 5 / 16 } };
        Polyline(dc, points, 3);
        if (previous) SelectObject(dc, previous);
        if (tick) DeleteObject(tick);
    }
    RECT textRect{ boxRect.right + px(8), rect.top, rect.right, rect.bottom };
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? palette.muted : palette.text);
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &textRect,
        DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (focused) {
        RECT measured = textRect;
        DrawTextW(dc, text.data(), static_cast<int>(text.size()), &measured,
            DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
        const int height = measured.bottom - measured.top;
        RECT ring{ boxRect.left - px(2), (rect.top + rect.bottom - height) / 2 - px(2),
            std::min(rect.right, measured.right + px(3)), (rect.top + rect.bottom + height) / 2 + px(2) };
        ring.top = std::min(ring.top, boxRect.top - px(2));
        ring.bottom = std::max(ring.bottom, boxRect.bottom + px(2));
        HPEN focusPen = CreatePen(PS_SOLID, px(1), palette.accent);
        HGDIOBJ previousPen = focusPen ? SelectObject(dc, focusPen) : nullptr;
        HGDIOBJ previousBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, ring.left, ring.top, ring.right, ring.bottom, px(6), px(6));
        SelectObject(dc, previousBrush);
        if (previousPen) SelectObject(dc, previousPen);
        if (focusPen) DeleteObject(focusPen);
    }
    if (oldFont) SelectObject(dc, oldFont);
}

} // namespace quicksift::ui::framework

namespace quicksift::ui::framework {

namespace {
// Overflow (ellipsis) tooltip: a framework-owned, self-painted popup. The comctl32
// tracking tooltip used before was activated (TTM_TRACKACTIVATE, visible=1) but its
// content depended on comctl32's own paint/theme/fade path; this window is painted
// by us with the palette and is mouse-transparent (it can never cause the hovered
// button's WM_MOUSELEAVE).
HWND gOverflowTooltip = nullptr;
HWND gOverflowTipOwner = nullptr;
COLORREF gOverflowTipBackground = RGB(44, 48, 56);
COLORREF gOverflowTipText = RGB(235, 238, 242);
COLORREF gOverflowTipBorder = RGB(90, 96, 108);
HFONT gOverflowTipFont = nullptr;
UINT gOverflowTipDpi = USER_DEFAULT_SCREEN_DPI;
constexpr wchar_t kOverflowTipClass[] = L"QuickSift.OverflowTip";
} // namespace

void MacOsUiFramework::StyleTooltip(HWND tooltip, const ThemePalette& palette, UINT dpi, HFONT font) noexcept {
    if (!tooltip) return;
    if (tooltip == gOverflowTooltip) {
        gOverflowTipBackground = palette.dark ? palette.menu : palette.panelStrong;
        gOverflowTipText = palette.text;
        gOverflowTipBorder = palette.border;
        gOverflowTipFont = font;
        gOverflowTipDpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, dpi);
        if (IsWindowVisible(tooltip)) InvalidateRect(tooltip, nullptr, TRUE);
        return;
    }
    // Drop the visual style so the palette colours apply (themed tooltips ignore
    // TTM_SETTIPBKCOLOR); the border comes from WS_BORDER drawn in the text colour.
    SetWindowTheme(tooltip, L"", L"");
    const COLORREF background = palette.dark ? palette.menu : palette.panelStrong;
    SendMessageW(tooltip, TTM_SETTIPBKCOLOR, static_cast<WPARAM>(background), 0);
    SendMessageW(tooltip, TTM_SETTIPTEXTCOLOR, static_cast<WPARAM>(palette.text), 0);
    const int margin = ScaleDip(6, std::max<UINT>(USER_DEFAULT_SCREEN_DPI, dpi));
    RECT margins{ margin, margin / 2, margin, margin / 2 };
    SendMessageW(tooltip, TTM_SETMARGIN, 0, reinterpret_cast<LPARAM>(&margins));
    SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, ScaleDip(390, std::max<UINT>(USER_DEFAULT_SCREEN_DPI, dpi)));
    if (font) SendMessageW(tooltip, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
}

HWND MacOsUiFramework::CreateSharedTooltip(HWND owner, HINSTANCE instance) noexcept {
    if (!owner) return nullptr;
    return CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | WS_BORDER | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, owner, nullptr, instance, nullptr);
}

void MacOsUiFramework::PaintDragGrip(ID2D1RenderTarget* target, D2D1_POINT_2F center,
    ID2D1Brush* brush) const noexcept {
    if (!target || !brush) return;
    for (int dot = -2; dot <= 2; ++dot) {
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center.x + 6.0f * static_cast<float>(dot), center.y),
            1.6f, 1.6f), brush);
    }
}

namespace {
MacOsUiFramework::TraceSink gOverflowTrace = nullptr;
constexpr wchar_t kOverflowTextProperty[] = L"QuickSift.OverflowText";
constexpr UINT_PTR kOverflowTipTimer = 0x51F7;
constexpr UINT kOverflowTipDelayMs = 450;

LRESULT CALLBACK OverflowTipProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCHITTEST:
        return HTTRANSPARENT; // never takes the pointer from the hovered button
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        if (dc) {
            RECT client{};
            GetClientRect(window, &client);
            HBRUSH background = CreateSolidBrush(gOverflowTipBackground);
            HBRUSH border = CreateSolidBrush(gOverflowTipBorder);
            if (background) FillRect(dc, &client, background);
            if (border) FrameRect(dc, &client, border);
            wchar_t text[512]{};
            GetWindowTextW(window, text, static_cast<int>(std::size(text)));
            HGDIOBJ oldFont = gOverflowTipFont ? SelectObject(dc, gOverflowTipFont) : nullptr;
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, gOverflowTipText);
            RECT textRect = client;
            const int marginX = MulDiv(8, static_cast<int>(gOverflowTipDpi), 96), marginY = MulDiv(4, static_cast<int>(gOverflowTipDpi), 96);
            InflateRect(&textRect, -marginX, -marginY);
            DrawTextW(dc, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (oldFont) SelectObject(dc, oldFont);
            if (background) DeleteObject(background);
            if (border) DeleteObject(border);
        }
        EndPaint(window, &paint);
        return 0;
    }
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
} // namespace

void MacOsUiFramework::SetOverflowTraceSink(TraceSink sink) noexcept { gOverflowTrace = sink; }

void MacOsUiFramework::TraceOverflow(HWND button, const wchar_t* event, const wchar_t* detail) noexcept {
    if (!gOverflowTrace) return;
    wchar_t line[512]{};
    _snwprintf_s(line, _countof(line), _TRUNCATE, L"overflow-tip %ls button=%p id=%d tooltip=%p %ls",
        event ? event : L"", static_cast<void*>(button), button ? GetDlgCtrlID(button) : 0,
        static_cast<void*>(gOverflowTooltip), detail ? detail : L"");
    gOverflowTrace(line);
}

HWND MacOsUiFramework::CreateOverflowTooltip(HWND owner, HINSTANCE instance) noexcept {
    if (!owner) return nullptr;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    if (!GetClassInfoExW(instance, kOverflowTipClass, &windowClass)) {
        windowClass = WNDCLASSEXW{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_DROPSHADOW;
        windowClass.lpfnWndProc = OverflowTipProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kOverflowTipClass;
        if (!RegisterClassExW(&windowClass)) {
            wchar_t detail[64]{};
            _snwprintf_s(detail, _countof(detail), _TRUNCATE, L"RegisterClassExW error=%lu", GetLastError());
            TraceOverflow(nullptr, L"create-failed", detail);
            return nullptr;
        }
    }
    HWND tooltip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        kOverflowTipClass, L"", WS_POPUP, 0, 0, 0, 0, owner, nullptr, instance, nullptr);
    if (!tooltip) {
        wchar_t detail[64]{};
        _snwprintf_s(detail, _countof(detail), _TRUNCATE, L"CreateWindowExW error=%lu", GetLastError());
        TraceOverflow(nullptr, L"create-failed", detail);
        return nullptr;
    }
    TraceOverflow(nullptr, L"created");
    return tooltip;
}

void MacOsUiFramework::SetOverflowTooltip(HWND tooltip) noexcept {
    gOverflowTooltip = tooltip;
    gOverflowTipOwner = nullptr;
}

void MacOsUiFramework::SetButtonOverflowText(HWND button, const wchar_t* text) noexcept {
    if (!button) return;
    if (text && *text) {
        const bool had = GetPropW(button, kOverflowTextProperty) != nullptr;
        SetPropW(button, kOverflowTextProperty, reinterpret_cast<HANDLE>(const_cast<wchar_t*>(text)));
        if (!had) TraceOverflow(button, L"marked-truncated", text);
    } else if (GetPropW(button, kOverflowTextProperty)) {
        RemovePropW(button, kOverflowTextProperty);
        HideOverflowTip(button);
        TraceOverflow(button, L"unmarked");
    }
}

void MacOsUiFramework::ArmOverflowTip(HWND button) noexcept {
    if (!button) return;
    if (!GetPropW(button, kOverflowTextProperty)) return; // caption fits: nothing to show
    if (!gOverflowTooltip) {
        TraceOverflow(button, L"arm-skipped", L"no tooltip window");
        return;
    }
    const UINT_PTR timer = SetTimer(button, kOverflowTipTimer, kOverflowTipDelayMs, nullptr);
    TraceOverflow(button, L"armed", timer ? L"timer set" : L"SetTimer FAILED");
}

void MacOsUiFramework::ShowOverflowTip(HWND button) noexcept {
    if (!button) return;
    KillTimer(button, kOverflowTipTimer);
    const auto* text = static_cast<const wchar_t*>(GetPropW(button, kOverflowTextProperty));
    if (!gOverflowTooltip || !text || !IsWindowVisible(button)) {
        TraceOverflow(button, L"show-skipped", !gOverflowTooltip ? L"no tooltip" : (!text ? L"no text" : L"button hidden"));
        return;
    }
    SetWindowTextW(gOverflowTooltip, text);
    // Size from the real text extent (DPI-scaled font + margins).
    RECT measure{ 0, 0, ScaleDip(390, gOverflowTipDpi), 0 };
    if (HDC dc = GetDC(gOverflowTooltip)) {
        HGDIOBJ oldFont = gOverflowTipFont ? SelectObject(dc, gOverflowTipFont) : nullptr;
        DrawTextW(dc, text, -1, &measure, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        if (oldFont) SelectObject(dc, oldFont);
        ReleaseDC(gOverflowTooltip, dc);
    }
    const int tipW = static_cast<int>(measure.right - measure.left) + 2 * ScaleDip(8, gOverflowTipDpi);
    const int tipH = static_cast<int>(measure.bottom - measure.top) + 2 * ScaleDip(4, gOverflowTipDpi);
    RECT rc{};
    GetWindowRect(button, &rc); // screen coordinates
    const UINT dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(button));
    // Below the button, left-aligned with it; flipped above near the monitor
    // bottom and kept inside the work area (never under the cursor).
    int x = rc.left;
    int y = rc.bottom + ScaleDip(4, dpi);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(MONITORINFO);
    if (GetMonitorInfoW(MonitorFromWindow(button, MONITOR_DEFAULTTONEAREST), &monitor)) {
        if (y + tipH > monitor.rcWork.bottom) y = rc.top - ScaleDip(4, dpi) - tipH;
        if (x + tipW > monitor.rcWork.right) x = std::max<int>(monitor.rcWork.left, monitor.rcWork.right - tipW);
        if (x < monitor.rcWork.left) x = monitor.rcWork.left;
    }
    SetWindowPos(gOverflowTooltip, HWND_TOPMOST, x, y, tipW, tipH,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    RedrawWindow(gOverflowTooltip, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    gOverflowTipOwner = button;
    RECT shown{};
    GetWindowRect(gOverflowTooltip, &shown);
    wchar_t detail[200]{};
    _snwprintf_s(detail, _countof(detail), _TRUNCATE, L"at=%d,%d size=%dx%d window=%ld,%ld,%ld,%ld visible=%d",
        x, y, tipW, tipH, shown.left, shown.top, shown.right, shown.bottom,
        IsWindowVisible(gOverflowTooltip) ? 1 : 0);
    TraceOverflow(button, L"shown", detail);
}

void MacOsUiFramework::HideOverflowTip(HWND button) noexcept {
    if (button) KillTimer(button, kOverflowTipTimer);
    if (!gOverflowTooltip || !gOverflowTipOwner || (button && gOverflowTipOwner != button)) return;
    SetWindowPos(gOverflowTooltip, nullptr, 0, 0, 0, 0,
        SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    TraceOverflow(gOverflowTipOwner, L"hidden");
    gOverflowTipOwner = nullptr;
}

} // namespace quicksift::ui::framework

