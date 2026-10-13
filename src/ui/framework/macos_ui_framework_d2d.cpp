// OWNER: Central Direct2D component renderer for overlays, cards, badges, and text.
#include "ui/framework/macos_ui_framework.h"


#include <wrl/client.h>

#include "ui/ui_layout_metrics.h"

#include <algorithm>
#include <cmath>

namespace quicksift::ui::framework {
namespace {
using Microsoft::WRL::ComPtr;

D2D1_COLOR_F D2DColor(COLORREF color, float alpha = 1.0f) noexcept {
    return ToDirect2DColor(color, alpha);
}

FloatRect ToFloatRect(const D2D1_RECT_F& rect) noexcept {
    return { rect.left, rect.top, rect.right, rect.bottom };
}

D2D1_RECT_F Inset(const D2D1_RECT_F& rect, float amount) noexcept {
    return D2D1::RectF(rect.left + amount, rect.top + amount,
        rect.right - amount, rect.bottom - amount);
}

class ScopedTextAlignment {
public:
    explicit ScopedTextAlignment(IDWriteTextFormat* format) noexcept
        : format_(format), text_(format ? format->GetTextAlignment() : DWRITE_TEXT_ALIGNMENT_LEADING),
          paragraph_(format ? format->GetParagraphAlignment() : DWRITE_PARAGRAPH_ALIGNMENT_NEAR),
          wrapping_(format ? format->GetWordWrapping() : DWRITE_WORD_WRAPPING_WRAP) {}

    ScopedTextAlignment(const ScopedTextAlignment&) = delete;
    ScopedTextAlignment& operator=(const ScopedTextAlignment&) = delete;

    ~ScopedTextAlignment() {
        if (!format_) return;
        format_->SetTextAlignment(text_);
        format_->SetParagraphAlignment(paragraph_);
        format_->SetWordWrapping(wrapping_);
    }

private:
    IDWriteTextFormat* format_ = nullptr;
    DWRITE_TEXT_ALIGNMENT text_ = DWRITE_TEXT_ALIGNMENT_LEADING;
    DWRITE_PARAGRAPH_ALIGNMENT paragraph_ = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    DWRITE_WORD_WRAPPING wrapping_ = DWRITE_WORD_WRAPPING_WRAP;
};
}

void MacOsUiFramework::PaintToast(const ToastPaintResources& resources,
    ToastPresenter::TimePoint now) const {
    (void)now;
    if (!resources.target || !resources.writeFactory || !resources.textFormat) return;
    // Presence comes from the central animator (SlideFade up: enters with a short
    // spring-lite rise, leaves with a slower fade). A cleared toast keeps fading
    // out with its last text.
    const animation::Appearance appearance = animator_.Appear(animation::element::kToast,
        kToastTransition, toasts_.Visible() ? 1.0f : 0.0f);
    if (!appearance.Visible()) return;
    const std::wstring& rawText = toasts_.Visible() ? toasts_.CurrentText() : toastGhostText_;
    if (rawText.empty()) return;
    const std::wstring text = NormalizeOverlayText(rawText);
    if (text.empty()) return;
    const float opacity = appearance.opacity;
    // Measurement == paint (ui_models ComputeToastGeometry): ONE DirectWrite layout at
    // the toast's max text width measures the text (explicit line breaks and word
    // wrap included) and is drawn as-is, centred. The old painter re-wrapped the text
    // at the device-pixel-snapped body width, which could be a fraction narrower than
    // the measured line, so the widest line wrapped again and the extra line hung
    // below the glass body.
    const float layoutWidth = ToastTextLayoutWidth(resources.surfaceSize.width);
    if (layoutWidth <= 0.0f) return;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(resources.writeFactory->CreateTextLayout(text.c_str(),
        static_cast<UINT32>(text.size()), resources.textFormat,
        layoutWidth, 4096.0f, &layout)) || !layout) return;
    // The shared format may carry another caller's settings: pin what toasts need.
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) return;
    const ToastGeometry geometry = ComputeToastGeometry(resources.surfaceSize.width,
        resources.surfaceSize.height, metrics.widthIncludingTrailingWhitespace, metrics.height);
    if (!geometry.Valid()) return;
    const float centerX = resources.surfaceSize.width * 0.5f;
    const float bottom = resources.surfaceSize.height - kToastEdge + appearance.offsetY;
    const ThemePalette& palette = resources.palette;
    GlassSurfacePresentation glass{};
    // Outward-rounded body (whole DIPs) so snapping can only grow it.
    glass.rect = D2D1::RectF(std::floor(centerX - geometry.width * 0.5f), std::floor(bottom - geometry.height),
        std::ceil(centerX + geometry.width * 0.5f), std::ceil(bottom));
    glass.radius = static_cast<float>(layout::kToastRadiusDip);
    glass.fill = BlendColor(palette.panelStrong, palette.windowTop, palette.dark ? 0.28f : 0.55f);
    glass.border = BlendColor(palette.accent, palette.border, palette.dark ? 0.35f : 0.22f);
    glass.opacity = opacity;
    // Dark mode: the top-half sheen reads as a two-tone split on a small toast.
    glass.sheen = !palette.dark;
    const D2D1_RECT_F body = PaintGlassSurface(resources.target, glass, palette);
    ComPtr<ID2D1SolidColorBrush> textBrush;
    resources.target->CreateSolidColorBrush(D2DColor(palette.text, opacity), &textBrush);
    if (!textBrush) return;
    // Clip to the body's text box (only matters when the text exceeds the tallest
    // toast the window can show); the layout itself is never narrowed.
    const D2D1_RECT_F clip = D2D1::RectF(body.left, body.top + kToastPaddingY * 0.5f,
        body.right, body.bottom - kToastPaddingY * 0.5f);
    resources.target->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
    resources.target->DrawTextLayout(D2D1::Point2F(centerX - layoutWidth * 0.5f, body.top + geometry.textTop),
        layout.Get(), textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
    resources.target->PopAxisAlignedClip();
}

float MacOsUiFramework::DevicePixelDip(ID2D1RenderTarget* target) noexcept {
    FLOAT dpiX = 96.0f;
    FLOAT dpiY = 96.0f;
    if (target) target->GetDpi(&dpiX, &dpiY);
    return dpiX > 0.0f ? 96.0f / dpiX : 1.0f;
}

D2D1_RECT_F MacOsUiFramework::SnapToDevicePixels(ID2D1RenderTarget* target,
    const D2D1_RECT_F& rect) noexcept {
    const float pixel = DevicePixelDip(target);
    const auto snap = [pixel](float value) { return std::round(value / pixel) * pixel; };
    return D2D1::RectF(snap(rect.left), snap(rect.top), snap(rect.right), snap(rect.bottom));
}

D2D1_RECT_F MacOsUiFramework::PaintGlassSurface(ID2D1RenderTarget* target,
    const GlassSurfacePresentation& presentation, const ThemePalette& palette) const {
    const D2D1_RECT_F body = SnapToDevicePixels(target, presentation.rect);
    const float opacity = std::clamp(presentation.opacity, 0.0f, 1.0f);
    if (!target || opacity <= 0.001f || body.right - body.left < 2.0f || body.bottom - body.top < 2.0f)
        return body;
    const float pixel = DevicePixelDip(target);
    const float radius = std::clamp(presentation.radius, 0.0f,
        std::min(body.right - body.left, body.bottom - body.top) * 0.5f);
    const tokens::GlassMaterial material = tokens::Glass(palette.dark, CurrentVisualEffects());
    const float elevation = std::max(0.0f, presentation.elevation);
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(target->CreateSolidColorBrush(D2DColor(RGB(0, 0, 0), 0.0f), &brush)) || !brush) return body;

    // Ambient shadow: four concentric rounded rings with a quarter of the alpha each,
    // so the falloff ramps instead of showing two hard-edged steps (no blur effect).
    if (material.ambientShadowAlpha > 0.0f && elevation > 0.0f) {
        constexpr int kRings = 4;
        const float drop = material.keyShadowOffset * elevation * 0.5f;
        for (int ring = kRings; ring >= 1; --ring) {
            const float spread = material.ambientShadowSpread * elevation *
                static_cast<float>(ring) / static_cast<float>(kRings);
            brush->SetColor(D2DColor(RGB(0, 0, 0),
                material.ambientShadowAlpha / static_cast<float>(kRings) * opacity));
            target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(body.left - spread,
                body.top - spread + drop, body.right + spread, body.bottom + spread + drop),
                radius + spread, radius + spread), brush.Get());
        }
    }
    // Key shadow: tight and offset downwards (the light comes from above). Only under
    // opaque bodies: under translucent glass the offset copy showed through the body
    // as a darker displaced block.
    if (material.keyShadowAlpha > 0.0f && elevation > 0.0f && presentation.opaque) {
        const float drop = std::round(material.keyShadowOffset * elevation / pixel) * pixel;
        brush->SetColor(D2DColor(RGB(0, 0, 0), material.keyShadowAlpha * opacity));
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(body.left,
            body.top + drop, body.right, body.bottom + drop), radius, radius), brush.Get());
    }
    // Body.
    const D2D1_ROUNDED_RECT shape = D2D1::RoundedRect(body, radius, radius);
    brush->SetColor(D2DColor(presentation.fill,
        (presentation.opaque ? 1.0f : material.fillAlpha) * opacity));
    target->FillRoundedRectangle(shape, brush.Get());
    // Vibrancy sheen over the top half and a 1 px specular edge along the top.
    const float height = body.bottom - body.top;
    if (material.sheenAlpha > 0.0f && presentation.sheen) {
        target->PushAxisAlignedClip(D2D1::RectF(body.left, body.top, body.right,
            body.top + std::round(height * 0.5f / pixel) * pixel), D2D1_ANTIALIAS_MODE_ALIASED);
        brush->SetColor(D2DColor(RGB(255, 255, 255), material.sheenAlpha * opacity));
        target->FillRoundedRectangle(shape, brush.Get());
        target->PopAxisAlignedClip();
    }
    const float border = std::max(pixel, std::round(presentation.borderWidth / pixel) * pixel);
    if (material.highlightAlpha > 0.0f && radius > 0.0f) {
        target->PushAxisAlignedClip(D2D1::RectF(body.left, body.top, body.right,
            body.top + std::max(radius, 2.0f * pixel)), D2D1_ANTIALIAS_MODE_ALIASED);
        brush->SetColor(D2DColor(RGB(255, 255, 255), material.highlightAlpha * opacity));
        const float inset = border + pixel * 0.5f;
        target->DrawRoundedRectangle(D2D1::RoundedRect(Inset(body, inset),
            std::max(0.0f, radius - inset), std::max(0.0f, radius - inset)), brush.Get(), pixel);
        target->PopAxisAlignedClip();
    }
    // Hairline border, fully inside the body (inset by half the stroke) so it is
    // crisp on whole device pixels and never spills over the shadow.
    brush->SetColor(D2DColor(presentation.border, material.borderAlpha * opacity));
    const float half = border * 0.5f;
    target->DrawRoundedRectangle(D2D1::RoundedRect(Inset(body, half),
        std::max(0.0f, radius - half), std::max(0.0f, radius - half)), brush.Get(), border);
    return body;
}

void MacOsUiFramework::PaintFocusRing(ID2D1RenderTarget* target, const D2D1_RECT_F& shape,
    float radius, const ThemePalette& palette, float opacity) const {
    if (!target || opacity <= 0.001f) return;
    ComPtr<ID2D1SolidColorBrush> ring;
    target->CreateSolidColorBrush(D2DColor(palette.accent,
        (palette.highContrast ? 1.0f : 0.80f) * std::clamp(opacity, 0.0f, 1.0f)), &ring);
    if (!ring) return;
    // macOS ring: outside the shape (gap + half the ring width), concentric radius.
    const float offset = tokens::kFocusRingGap + tokens::kFocusRingWidth * 0.5f;
    target->DrawRoundedRectangle(D2D1::RoundedRect(Inset(shape, -offset),
        radius + offset, radius + offset), ring.Get(), tokens::kFocusRingWidth);
}

FloatingFieldLayout MacOsUiFramework::LayoutFloatingField(const D2D1_RECT_F& frame,
    bool showCount, bool showClear) noexcept {
    FloatingFieldLayout layout;
    layout.frame = frame;
    const float height = std::max(1.0f, frame.bottom - frame.top);
    const float centerY = (frame.top + frame.bottom) * 0.5f;
    layout.radius = height * 0.5f;
    const float glyphSize = 16.0f;
    layout.glyph = D2D1::RectF(frame.left + 12.0f, centerY - glyphSize * 0.5f,
        frame.left + 12.0f + glyphSize, centerY + glyphSize * 0.5f);
    float right = frame.right - 10.0f;
    if (showClear) {
        const float size = 20.0f;
        layout.clear = D2D1::RectF(right - size, centerY - size * 0.5f, right, centerY + size * 0.5f);
        right = layout.clear.left - 6.0f;
    }
    if (showCount) {
        const float width = std::min(104.0f, std::max(0.0f, (frame.right - frame.left) * 0.34f));
        layout.count = D2D1::RectF(right - width, frame.top, right, frame.bottom);
        right = layout.count.left - 6.0f;
    }
    const float inputLeft = layout.glyph.right + 6.0f;
    layout.input = D2D1::RectF(inputLeft, frame.top + 4.0f,
        std::max(inputLeft + 1.0f, right), frame.bottom - 4.0f);
    return layout;
}

COLORREF MacOsUiFramework::FloatingFieldFill(const ThemePalette& palette) noexcept {
    return BlendColor(palette.panelStrong, palette.windowTop, palette.dark ? 0.22f : 0.45f);
}

void MacOsUiFramework::PaintFloatingField(ID2D1RenderTarget* target,
    const FloatingFieldPresentation& presentation, const ThemePalette& palette,
    IDWriteTextFormat* countFormat) const {
    const float opacity = std::clamp(presentation.opacity, 0.0f, 1.0f);
    if (!target || opacity <= 0.001f) return;
    const FloatingFieldLayout& layout = presentation.layout;
    const D2D1_RECT_F& rect = layout.frame;
    if (rect.right - rect.left < 8.0f || rect.bottom - rect.top < 8.0f) return;
    const float radius = layout.radius;
    const COLORREF fillColor = FloatingFieldFill(palette);
    const COLORREF borderColor = presentation.focused ? palette.accent :
        BlendColor(palette.border, palette.text, palette.dark ? 0.10f : 0.04f);
    ComPtr<ID2D1SolidColorBrush> glyph, clearFill, countBrush;
    target->CreateSolidColorBrush(D2DColor(presentation.focused ? palette.accent : palette.muted, opacity), &glyph);
    target->CreateSolidColorBrush(D2DColor(palette.muted, (presentation.clearHot ? 0.42f : 0.22f) * opacity), &clearFill);
    target->CreateSolidColorBrush(D2DColor(presentation.noResults ? palette.dangerBorder : palette.muted, opacity),
        &countBrush);
    if (!glyph || !clearFill || !countBrush) return;

    // Shared glass recipe (shadows, specular top edge, hairline). The body stays
    // opaque: the borderless text input child clears with FloatingFieldFill().
    GlassSurfacePresentation glass{};
    glass.rect = rect;
    glass.radius = radius;
    glass.fill = fillColor;
    glass.border = borderColor;
    glass.opacity = opacity;
    glass.opaque = true;
    glass.sheen = false; // flat body == the input child's clear colour (no block)
    const D2D1_RECT_F body = PaintGlassSurface(target, glass, palette);
    // macOS focus ring: outside the capsule, never over the text.
    if (presentation.focused) PaintFocusRing(target, body, radius, palette, opacity * 0.55f);

    // Search glyph: lens + handle.
    const D2D1_RECT_F& g = layout.glyph;
    const float lens = (g.right - g.left) * 0.30f;
    const D2D1_POINT_2F lensCenter = D2D1::Point2F(g.left + lens + 1.5f, g.top + lens + 1.5f);
    target->DrawEllipse(D2D1::Ellipse(lensCenter, lens, lens), glyph.Get(), tokens::kIconStroke);
    const float handleStart = lens * 0.72f;
    target->DrawLine(D2D1::Point2F(lensCenter.x + handleStart, lensCenter.y + handleStart),
        D2D1::Point2F(g.right - 2.0f, g.bottom - 2.0f), glyph.Get(), tokens::kIconStroke);

    if (!presentation.countText.empty() && countFormat && layout.count.right > layout.count.left) {
        ScopedTextAlignment restore(countFormat);
        countFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        countFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        countFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        target->DrawTextW(presentation.countText.data(), static_cast<UINT32>(presentation.countText.size()),
            countFormat, layout.count, countBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    if (presentation.clearVisible && layout.clear.right > layout.clear.left) {
        const D2D1_RECT_F& c = layout.clear;
        const D2D1_POINT_2F center = D2D1::Point2F((c.left + c.right) * 0.5f, (c.top + c.bottom) * 0.5f);
        const float r = (c.right - c.left) * 0.5f;
        target->FillEllipse(D2D1::Ellipse(center, r, r), clearFill.Get());
        const float arm = r * 0.38f;
        target->DrawLine(D2D1::Point2F(center.x - arm, center.y - arm),
            D2D1::Point2F(center.x + arm, center.y + arm), glyph.Get(), tokens::kIconStroke);
        target->DrawLine(D2D1::Point2F(center.x - arm, center.y + arm),
            D2D1::Point2F(center.x + arm, center.y - arm), glyph.Get(), tokens::kIconStroke);
    }
}

void MacOsUiFramework::PaintAlert(const AlertPaintResources& resources) {
    // Hit regions are valid only for the most recently completed layout. Clear
    // them before any resource-dependent early return so a resize/device-loss
    // frame cannot leave clickable buttons at stale coordinates.
    alerts_.SetButtonRects({}, {});
    // PopScale from the central animator: 0.96 -> 1 with fade (spring-lite settle);
    // a resolved alert scales/fades out as a non-interactive ghost.
    const bool live = alerts_.Visible();
    const animation::Appearance appearance = animator_.Appear(animation::element::kAlert,
        kAlertTransition, live ? 1.0f : 0.0f);
    if (!appearance.Visible()) return;
    if (!resources.target || !resources.writeFactory || !resources.titleFormat ||
        !resources.messageFormat) return;
    const AlertRequest& request = live ? alerts_.Request() : alertGhost_;
    const ThemePalette& palette = resources.palette;
    ComPtr<ID2D1SolidColorBrush> veil;
    resources.target->CreateSolidColorBrush(D2DColor(RGB(5, 9, 14),
        (palette.dark ? 0.42f : 0.18f) * appearance.opacity), &veil);
    if (!veil) return;
    resources.target->FillRectangle(D2D1::RectF(0, 0,
        resources.surfaceSize.width, resources.surfaceSize.height), veil.Get());

    const float cardWidth = std::min(520.0f,
        std::max(300.0f, resources.surfaceSize.width - 48.0f));
    const float textWidth = cardWidth - 48.0f;
    ComPtr<IDWriteTextLayout> titleLayout;
    ComPtr<IDWriteTextLayout> messageLayout;
    // Measured with unbounded height (explicit breaks + word wrap); the card grows
    // with the text (ComputeAlertCardGeometry) and only clips at the surface limit.
    const std::wstring titleText = NormalizeOverlayText(request.title);
    const std::wstring messageText = NormalizeOverlayText(request.message);
    if (FAILED(resources.writeFactory->CreateTextLayout(titleText.c_str(),
        static_cast<UINT32>(titleText.size()), resources.titleFormat,
        textWidth, 4096.0f, &titleLayout)) ||
        FAILED(resources.writeFactory->CreateTextLayout(messageText.c_str(),
        static_cast<UINT32>(messageText.size()), resources.messageFormat,
        textWidth, 4096.0f, &messageLayout)) || !titleLayout || !messageLayout) return;
    titleLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    messageLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    DWRITE_TEXT_METRICS titleMetrics{};
    DWRITE_TEXT_METRICS messageMetrics{};
    if (FAILED(titleLayout->GetMetrics(&titleMetrics)) ||
        FAILED(messageLayout->GetMetrics(&messageMetrics))) return;
    const AlertCardGeometry cardGeometry = ComputeAlertCardGeometry(resources.surfaceSize.height,
        titleMetrics.height, messageMetrics.height);
    const float cardHeight = cardGeometry.cardHeight;
    // Clip box for the message: everything when it fits, otherwise the card's room.
    messageLayout->SetMaxHeight(std::max(1.0f, cardGeometry.messageBoxHeight));
    const D2D1_RECT_F card = SnapToDevicePixels(resources.target, D2D1::RectF(
        (resources.surfaceSize.width - cardWidth) / 2.0f,
        (resources.surfaceSize.height - cardHeight) / 2.0f,
        (resources.surfaceSize.width + cardWidth) / 2.0f,
        (resources.surfaceSize.height + cardHeight) / 2.0f));

    COLORREF accent = palette.accent;
    if (request.kind == AlertKind::Warning) accent = RGB(245, 169, 66);
    else if (request.kind == AlertKind::Error) accent = palette.dangerBorder;
    const COLORREF fillColor = BlendColor(palette.panelStrong, palette.windowTop,
        palette.dark ? 0.34f : 0.62f);
    const COLORREF borderColor = BlendColor(accent, palette.border, 0.42f);
    COLORREF primaryBase = accent;
    if (request.destructive || request.kind == AlertKind::Error) {
        primaryBase = palette.dark ? palette.dangerBorder :
            BlendColor(palette.dangerBorder, RGB(0, 0, 0), 0.09f);
    } else if (request.kind == AlertKind::Information && !palette.dark) {
        primaryBase = palette.accentBorder;
    }
    const COLORREF baseText = BestContrastText(primaryBase);
    const COLORREF hoverTarget = baseText == RGB(255, 255, 255)
        ? RGB(0, 0, 0) : RGB(255, 255, 255);
    const COLORREF primaryColor = live && alerts_.PrimaryHot()
        ? BlendColor(primaryBase, hoverTarget, 0.10f) : primaryBase;
    const COLORREF primaryOutline = BlendColor(primaryBase, RGB(0, 0, 0),
        palette.dark ? 0.18f : 0.10f);
    const COLORREF secondaryColor = live && alerts_.SecondaryHot() ? palette.buttonHot : palette.button;
    const COLORREF secondaryOutline = BlendColor(palette.border, secondaryColor, 0.10f);

    ComPtr<ID2D1SolidColorBrush> titleBrush, messageBrush;
    ComPtr<ID2D1SolidColorBrush> primaryFill, primaryBorder, primaryText;
    ComPtr<ID2D1SolidColorBrush> secondaryFill, secondaryBorder, secondaryText;
    resources.target->CreateSolidColorBrush(D2DColor(palette.text), &titleBrush);
    resources.target->CreateSolidColorBrush(D2DColor(palette.muted), &messageBrush);
    resources.target->CreateSolidColorBrush(D2DColor(primaryColor), &primaryFill);
    resources.target->CreateSolidColorBrush(D2DColor(primaryOutline), &primaryBorder);
    resources.target->CreateSolidColorBrush(D2DColor(BestContrastText(primaryColor)), &primaryText);
    resources.target->CreateSolidColorBrush(D2DColor(secondaryColor), &secondaryFill);
    resources.target->CreateSolidColorBrush(D2DColor(secondaryOutline), &secondaryBorder);
    resources.target->CreateSolidColorBrush(D2DColor(BestContrastText(secondaryColor)), &secondaryText);
    if (!titleBrush || !messageBrush ||
        !primaryFill || !primaryBorder || !primaryText || !secondaryFill ||
        !secondaryBorder || !secondaryText) return;

    // Card group transition: one scale transform about the card centre plus one
    // bounded opacity layer, only while the transition runs (at rest: no layer).
    ID2D1RenderTarget* const target = resources.target;
    D2D1_MATRIX_3X2_F restingTransform{};
    target->GetTransform(&restingTransform);
    const bool scaled = appearance.scale < 0.9999f;
    if (scaled) {
        target->SetTransform(D2D1::Matrix3x2F::Scale(appearance.scale, appearance.scale,
            D2D1::Point2F((card.left + card.right) * 0.5f, (card.top + card.bottom) * 0.5f)) *
            restingTransform);
    }
    ComPtr<ID2D1Layer> fadeLayer;
    if (appearance.opacity < 0.999f && SUCCEEDED(target->CreateLayer(nullptr, &fadeLayer)) && fadeLayer) {
        const float margin = 32.0f; // room for the elevation shadow
        target->PushLayer(D2D1::LayerParameters(D2D1::RectF(card.left - margin, card.top - margin,
            card.right + margin, card.bottom + margin), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
            D2D1::IdentityMatrix(), appearance.opacity), fadeLayer.Get());
    }
    const auto restoreCardGroup = [&] {
        if (fadeLayer) target->PopLayer();
        if (scaled) target->SetTransform(restingTransform);
    };

    GlassSurfacePresentation glass{};
    glass.rect = card;
    glass.radius = static_cast<float>(layout::kAlertRadiusDip);
    glass.fill = fillColor;
    glass.border = borderColor;
    glass.elevation = 2.5f; // modal sheet: deeper shadow than toasts
    glass.sheen = !palette.dark; // dark mode sheen reads as a two-tone split
    PaintGlassSurface(resources.target, glass, palette);
    const float textLeft = card.left + 24.0f;
    const float titleTop = card.top + 22.0f;
    resources.target->DrawTextLayout(D2D1::Point2F(textLeft, titleTop),
        titleLayout.Get(), titleBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    resources.target->DrawTextLayout(D2D1::Point2F(textLeft,
        titleTop + titleMetrics.height + 12.0f), messageLayout.Get(), messageBrush.Get(),
        D2D1_DRAW_TEXT_OPTIONS_CLIP);

    const bool confirmation = request.mode == AlertMode::Confirmation;
    const bool tertiary = confirmation && !request.tertiaryText.empty();
    // Button widths follow their (localized) captions so EN and VI both fit; the
    // historic widths remain the minimums. The tertiary button sits at the left
    // edge (macOS "extra action" position); Cancel/secondary left of the default.
    const auto captionWidth = [&](const std::wstring& text) {
        ComPtr<IDWriteTextLayout> layout;
        DWRITE_TEXT_METRICS metrics{};
        if (FAILED(resources.writeFactory->CreateTextLayout(text.c_str(),
                static_cast<UINT32>(text.size()), resources.titleFormat, 1000.0f, 60.0f, &layout)) ||
            FAILED(layout->GetMetrics(&metrics))) return 0.0f;
        return metrics.widthIncludingTrailingWhitespace;
    };
    const float available = cardWidth - 48.0f;
    float primaryWidth = std::max(confirmation ? 150.0f : 80.0f,
        captionWidth(request.primaryText) + 36.0f);
    float secondaryWidth = confirmation ? std::max(92.0f, captionWidth(request.secondaryText) + 36.0f) : 0.0f;
    float tertiaryWidth = tertiary ? std::max(120.0f, captionWidth(request.tertiaryText) + 36.0f) : 0.0f;
    const float gaps = (confirmation ? 10.0f : 0.0f) + (tertiary ? 16.0f : 0.0f);
    const float total = primaryWidth + secondaryWidth + tertiaryWidth + gaps;
    if (total > available && total > 0.0f) {
        const float scale = std::max(0.2f, (available - gaps) / (total - gaps));
        primaryWidth *= scale;
        secondaryWidth *= scale;
        tertiaryWidth *= scale;
    }
    const D2D1_RECT_F primaryRect = SnapToDevicePixels(resources.target,
        D2D1::RectF(card.right - 24.0f - primaryWidth,
        card.bottom - 50.0f, card.right - 24.0f, card.bottom - 16.0f));
    const D2D1_RECT_F secondaryRect = confirmation
        ? SnapToDevicePixels(resources.target, D2D1::RectF(primaryRect.left - 10.0f - secondaryWidth,
            primaryRect.top, primaryRect.left - 10.0f, primaryRect.bottom))
        : D2D1::RectF();
    const D2D1_RECT_F tertiaryRect = tertiary
        ? SnapToDevicePixels(resources.target, D2D1::RectF(card.left + 24.0f, primaryRect.top,
            std::min(card.left + 24.0f + tertiaryWidth, secondaryRect.left - 16.0f), primaryRect.bottom))
        : D2D1::RectF();
    // Capsule (pill) buttons with a hairline drawn inside the shape.
    const float pill = tokens::CapsuleRadius(primaryRect.bottom - primaryRect.top);
    const float hairline = DevicePixelDip(resources.target);
    const auto paintPill = [&](const D2D1_RECT_F& rect, ID2D1Brush* fillBrush, ID2D1Brush* borderBrush) {
        resources.target->FillRoundedRectangle(D2D1::RoundedRect(rect, pill, pill), fillBrush);
        const float half = hairline * 0.5f;
        resources.target->DrawRoundedRectangle(D2D1::RoundedRect(Inset(rect, half),
            pill - half, pill - half), borderBrush, hairline);
    };
    // Only the live alert is clickable (never the fading ghost).
    if (live) {
        alerts_.SetButtonRects(ToFloatRect(primaryRect), ToFloatRect(secondaryRect),
            ToFloatRect(tertiaryRect));
    }
    paintPill(primaryRect, primaryFill.Get(), primaryBorder.Get());
    if (confirmation) paintPill(secondaryRect, secondaryFill.Get(), secondaryBorder.Get());
    ComPtr<ID2D1SolidColorBrush> tertiaryFill, tertiaryBorder, tertiaryTextBrush;
    if (tertiary) {
        const COLORREF tertiaryColor = live && alerts_.TertiaryHot() ? palette.buttonHot : palette.button;
        resources.target->CreateSolidColorBrush(D2DColor(tertiaryColor), &tertiaryFill);
        resources.target->CreateSolidColorBrush(D2DColor(BlendColor(palette.border, tertiaryColor, 0.10f)),
            &tertiaryBorder);
        resources.target->CreateSolidColorBrush(D2DColor(BestContrastText(tertiaryColor)), &tertiaryTextBrush);
        if (tertiaryFill && tertiaryBorder) paintPill(tertiaryRect, tertiaryFill.Get(), tertiaryBorder.Get());
    }
    if (live && alerts_.FocusVisible()) {
        // Keyboard focus ring (Tab/arrow navigation), drawn outside the pill.
        D2D1_RECT_F focusRect = primaryRect;
        if (alerts_.FocusedButton() == AlertButton::Secondary && confirmation) focusRect = secondaryRect;
        else if (alerts_.FocusedButton() == AlertButton::Tertiary && tertiary) focusRect = tertiaryRect;
        PaintFocusRing(resources.target, focusRect, pill, palette);
    }

    const ScopedTextAlignment alignment(resources.titleFormat);
    resources.titleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    resources.titleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    resources.titleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    resources.target->DrawTextW(request.primaryText.c_str(),
        static_cast<UINT32>(request.primaryText.size()), resources.titleFormat,
        primaryRect, primaryText.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    if (confirmation) {
        resources.target->DrawTextW(request.secondaryText.c_str(),
            static_cast<UINT32>(request.secondaryText.size()), resources.titleFormat,
            secondaryRect, secondaryText.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    if (tertiary && tertiaryTextBrush) {
        resources.target->DrawTextW(request.tertiaryText.c_str(),
            static_cast<UINT32>(request.tertiaryText.size()), resources.titleFormat,
            tertiaryRect, tertiaryTextBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    restoreCardGroup();
}


void MacOsUiFramework::PaintOverlayScrollbar(ID2D1RenderTarget* target,
    const D2D1_RECT_F& thumb, const D2D1_RECT_F& track, bool hot, bool dragging,
    const ThemePalette& palette) const {
    if (!target || thumb.right <= thumb.left || thumb.bottom <= thumb.top) return;

    ComPtr<ID2D1SolidColorBrush> trackBrush;
    ComPtr<ID2D1SolidColorBrush> thumbBrush;
    if ((hot || dragging) && track.right > track.left && track.bottom > track.top) {
        if (SUCCEEDED(target->CreateSolidColorBrush(
                D2DColor(BlendColor(palette.border, palette.panel, 0.55f), 0.35f),
                &trackBrush))) {
            const float trackRadius = tokens::CapsuleRadius(
                std::min(track.right - track.left, track.bottom - track.top));
            target->FillRoundedRectangle(
                D2D1::RoundedRect(track, trackRadius, trackRadius), trackBrush.Get());
        }
    }

    if (SUCCEEDED(target->CreateSolidColorBrush(
            D2DColor(dragging ? palette.accent : palette.text,
                hot || dragging ? 0.65f : 0.25f),
            &thumbBrush))) {
        const float thumbRadius = tokens::CapsuleRadius(
            std::min(thumb.right - thumb.left, thumb.bottom - thumb.top));
        target->FillRoundedRectangle(
            D2D1::RoundedRect(thumb, thumbRadius, thumbRadius), thumbBrush.Get());
    }
}

void MacOsUiFramework::PaintImagePlaceholder(ID2D1RenderTarget* target,
    const D2D1_RECT_F& rect, float radius, ID2D1Brush* fill) const {
    if (!target || !fill || rect.right <= rect.left || rect.bottom <= rect.top) return;
    const D2D1_RECT_F snapped = SnapToDevicePixels(target, rect);
    const float r = std::max(0.0f, radius);
    target->FillRoundedRectangle(D2D1::RoundedRect(snapped, r, r), fill);
}

void MacOsUiFramework::PaintMediaCard(ID2D1RenderTarget* target,
    const MediaCardPresentation& presentation, const MediaCardBrushes& brushes) const {
    if (!target || !brushes.panel || !brushes.border) return;
    // Every stroke is inset by half its width so it lies inside the card shape with
    // a concentric radius (centred strokes used to spill half outside the card and
    // blur on fractional coordinates).
    const D2D1_RECT_F rect = SnapToDevicePixels(target, presentation.rect);
    const float radius = std::clamp(presentation.radius, 0.0f,
        std::min(rect.right - rect.left, rect.bottom - rect.top) * 0.5f);
    const float pixel = DevicePixelDip(target);
    const auto stroke = [&](ID2D1Brush* brush, float width, float inset) {
        const float offset = inset + width * 0.5f;
        target->DrawRoundedRectangle(D2D1::RoundedRect(Inset(rect, offset),
            std::max(0.0f, radius - offset), std::max(0.0f, radius - offset)), brush, width);
    };
    if (presentation.paintSurface) {
        target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brushes.panel);
        stroke(brushes.border, pixel, 0.0f);
    }
    const float selection = presentation.focused ? tokens::kSelectionStrokeFocused : tokens::kSelectionStroke;
    if (presentation.warning && brushes.danger) {
        stroke(brushes.danger, tokens::kSelectionStrokeFocused, 0.0f);
        if (presentation.selected && brushes.accent)
            stroke(brushes.accent, selection * 0.75f, tokens::kSelectionStrokeFocused + 1.0f);
    } else if (presentation.selected && brushes.accent) {
        stroke(brushes.accent, selection, 0.0f);
    }
}

void MacOsUiFramework::PaintRoundedCornerUnderlay(ID2D1RenderTarget* target,
    D2D1_SIZE_F size, float radius, const std::array<D2D1_COLOR_F, 4>& colors,
    ID2D1SolidColorBrush* brush) const {
    if (!target || !brush || !(radius > 0.5f) || !(size.width > radius * 2.0f) ||
        !(size.height > radius * 2.0f)) return;
    // The inner edge of a stroke of width w centred on a rounded rect inflated by
    // w/2 (corner radius r + w/2) is exactly the rounded rect with corner radius r:
    // the stroke covers precisely the area outside the visual shape.
    const float stroke = radius + 2.0f;
    const float half = stroke * 0.5f;
    const D2D1_ROUNDED_RECT ring = D2D1::RoundedRect(
        D2D1::RectF(-half, -half, size.width + half, size.height + half),
        radius + half, radius + half);
    const std::array<D2D1_RECT_F, 4> clips{{
        D2D1::RectF(0.0f, 0.0f, radius, radius),
        D2D1::RectF(size.width - radius, 0.0f, size.width, radius),
        D2D1::RectF(size.width - radius, size.height - radius, size.width, size.height),
        D2D1::RectF(0.0f, size.height - radius, radius, size.height) }};
    const D2D1_COLOR_F previousColor = brush->GetColor();
    const FLOAT previousOpacity = brush->GetOpacity();
    const D2D1_ANTIALIAS_MODE previousMode = target->GetAntialiasMode();
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    brush->SetOpacity(1.0f);
    for (std::size_t index = 0; index < clips.size(); ++index) {
        brush->SetColor(colors[index]);
        target->PushAxisAlignedClip(clips[index], D2D1_ANTIALIAS_MODE_ALIASED);
        target->DrawRoundedRectangle(ring, brush, stroke);
        target->PopAxisAlignedClip();
    }
    brush->SetColor(previousColor);
    brush->SetOpacity(previousOpacity);
    target->SetAntialiasMode(previousMode);
}

void MacOsUiFramework::PaintFaceOutline(ID2D1RenderTarget* target,
    const FaceOutlinePresentation& presentation,
    ID2D1SolidColorBrush* stroke, ID2D1SolidColorBrush* shadow) const {
    if (!target || !stroke || !shadow) return;
    const D2D1_RECT_F& rect = presentation.rect;
    const float width = rect.right - rect.left;
    const float height = rect.bottom - rect.top;
    if (!(width >= 3.0f && height >= 3.0f)) return;
    const float fade = std::clamp(presentation.opacity, 0.0f, 1.0f);
    if (fade <= 0.004f) return;
    const float radius = std::clamp(std::min(width, height) * 0.12f, 2.0f, 10.0f);
    const D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(rect, radius, radius);
    const FLOAT previousStrokeOpacity = stroke->GetOpacity();
    const FLOAT previousShadowOpacity = shadow->GetOpacity();
    const D2D1_COLOR_F previousStrokeColor = stroke->GetColor();
    // A dark under-stroke keeps the outline legible on bright skin; the light (or
    // faint red when flagged) stroke keeps it legible on dark backgrounds.
    shadow->SetOpacity((presentation.target ? 0.28f : 0.16f) * fade);
    target->DrawRoundedRectangle(rounded, shadow, presentation.target ? 2.6f : 2.0f);
    stroke->SetColor(presentation.flagged ? D2D1::ColorF(1.0f, 0.36f, 0.36f, 1.0f) :
        D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
    stroke->SetOpacity((presentation.target ? 0.48f : 0.26f) * fade);
    target->DrawRoundedRectangle(rounded, stroke, presentation.target ? 1.4f : 1.0f);
    stroke->SetColor(previousStrokeColor);
    stroke->SetOpacity(previousStrokeOpacity);
    shadow->SetOpacity(previousShadowOpacity);
}

void MacOsUiFramework::PaintOverlayLabel(ID2D1RenderTarget* target,
    const D2D1_RECT_F& rect, float radius, std::wstring_view text,
    IDWriteTextFormat* format, ID2D1Brush* fill, ID2D1Brush* textBrush,
    ID2D1Brush* border, float borderWidth, bool centered) const {
    if (!target || !format || !textBrush) return;
    const D2D1_RECT_F snapped = SnapToDevicePixels(target, rect);
    const float r = std::clamp(radius, 0.0f,
        std::min(snapped.right - snapped.left, snapped.bottom - snapped.top) * 0.5f);
    if (fill) target->FillRoundedRectangle(D2D1::RoundedRect(snapped, r, r), fill);
    if (border && borderWidth > 0.0f) {
        const float pixel = DevicePixelDip(target);
        const float width = std::max(pixel, std::round(borderWidth / pixel) * pixel);
        const float half = width * 0.5f;
        target->DrawRoundedRectangle(D2D1::RoundedRect(Inset(snapped, half),
            std::max(0.0f, r - half), std::max(0.0f, r - half)), border, width);
    }
    if (text.empty()) return;
    if (centered) {
        PaintCenteredText(target, text, format, snapped, textBrush);
        return;
    }
    // Leading text keeps the capsule's inner padding (8 DIP) instead of touching the edge.
    const float padding = static_cast<float>(tokens::kSpace2);
    PaintSingleLineText(target, text, format, D2D1::RectF(snapped.left + padding, snapped.top,
        std::max(snapped.left + padding, snapped.right - padding), snapped.bottom), textBrush);
}

float MacOsUiFramework::MeasureSingleLineText(IDWriteFactory* factory,
    IDWriteTextFormat* format, std::wstring_view text) noexcept {
    if (!factory || !format || text.empty()) return 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(factory->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format,
            4096.0f, 256.0f, &layout)) || !layout || FAILED(layout->GetMetrics(&metrics)))
        return 0.0f;
    return metrics.widthIncludingTrailingWhitespace;
}

void MacOsUiFramework::PaintSingleLineText(ID2D1RenderTarget* target,
    std::wstring_view text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
    ID2D1Brush* brush) const {
    if (!target || !format || !brush || text.empty()) return;
    const ScopedTextAlignment alignment(format);
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    PaintText(target, text, format, rect, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void MacOsUiFramework::PaintText(ID2D1RenderTarget* target, std::wstring_view text,
    IDWriteTextFormat* format, const D2D1_RECT_F& rect, ID2D1Brush* brush,
    D2D1_DRAW_TEXT_OPTIONS options) const {
    if (!target || !format || !brush || text.empty()) return;
    target->DrawTextW(text.data(), static_cast<UINT32>(text.size()), format,
        rect, brush, options);
}

void MacOsUiFramework::PaintCenteredText(ID2D1RenderTarget* target,
    std::wstring_view text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
    ID2D1Brush* brush) const {
    if (!target || !format || !brush) return;
    const ScopedTextAlignment alignment(format);
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    PaintText(target, text, format, rect, brush);
}

} // namespace quicksift::ui::framework
