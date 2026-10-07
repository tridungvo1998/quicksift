// OWNER: Central Direct2D component renderer for overlays, cards, badges, and text.
#include "ui/framework/macos_ui_framework.h"


#include <wrl/client.h>

#include <algorithm>

namespace quicksift::ui::framework {
namespace {
using Microsoft::WRL::ComPtr;

D2D1_COLOR_F D2DColor(COLORREF color, float alpha = 1.0f) noexcept {
    return ToDirect2DColor(color, alpha);
}

FloatRect ToFloatRect(const D2D1_RECT_F& rect) noexcept {
    return { rect.left, rect.top, rect.right, rect.bottom };
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
    if (!resources.target || !resources.writeFactory || !resources.textFormat ||
        !toasts_.Visible() || toasts_.CurrentText().empty()) return;
    const float opacity = toasts_.Opacity(now);
    if (opacity <= 0.001f) return;
    const float maxWidth = std::min(resources.surfaceSize.width - 32.0f, 420.0f);
    if (maxWidth <= 40.0f) return;
    ComPtr<IDWriteTextLayout> layout;
    const std::wstring& text = toasts_.CurrentText();
    if (FAILED(resources.writeFactory->CreateTextLayout(text.c_str(),
        static_cast<UINT32>(text.size()), resources.textFormat,
        maxWidth - 34.0f, 160.0f, &layout))) return;
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) return;
    const float width = std::clamp(metrics.widthIncludingTrailingWhitespace + 34.0f,
        160.0f, maxWidth);
    const float height = std::max(34.0f, metrics.height + 22.0f);
    const D2D1_RECT_F rect = D2D1::RectF(
        (resources.surfaceSize.width - width) / 2.0f,
        resources.surfaceSize.height - height - 26.0f,
        (resources.surfaceSize.width + width) / 2.0f,
        resources.surfaceSize.height - 26.0f);
    const ThemePalette& palette = resources.palette;
    ComPtr<ID2D1SolidColorBrush> shadow;
    ComPtr<ID2D1SolidColorBrush> fill;
    ComPtr<ID2D1SolidColorBrush> border;
    ComPtr<ID2D1SolidColorBrush> textBrush;
    ComPtr<ID2D1SolidColorBrush> glow;
    const COLORREF fillColor = BlendColor(palette.panelStrong, palette.windowTop,
        palette.dark ? 0.28f : 0.55f);
    const COLORREF borderColor = BlendColor(palette.accent, palette.border,
        palette.dark ? 0.35f : 0.22f);
    const COLORREF glowColor = BlendColor(palette.accent, RGB(255, 255, 255), 0.38f);
    resources.target->CreateSolidColorBrush(D2DColor(RGB(0, 0, 0),
        (palette.dark ? 0.24f : 0.12f) * opacity), &shadow);
    resources.target->CreateSolidColorBrush(D2DColor(fillColor,
        (palette.dark ? 0.94f : 0.98f) * opacity), &fill);
    resources.target->CreateSolidColorBrush(D2DColor(borderColor, 0.92f * opacity), &border);
    resources.target->CreateSolidColorBrush(D2DColor(palette.text, opacity), &textBrush);
    resources.target->CreateSolidColorBrush(D2DColor(glowColor, 0.10f * opacity), &glow);
    if (!shadow || !fill || !border || !textBrush || !glow) return;
    const D2D1_RECT_F shadowRect = D2D1::RectF(rect.left, rect.top + 2.0f,
        rect.right, rect.bottom + 2.0f);
    const D2D1_RECT_F glowRect = D2D1::RectF(rect.left - 1.0f, rect.top - 1.0f,
        rect.right + 1.0f, rect.bottom + 1.0f);
    resources.target->FillRoundedRectangle(D2D1::RoundedRect(shadowRect, 15.0f, 15.0f), shadow.Get());
    resources.target->FillRoundedRectangle(D2D1::RoundedRect(glowRect, 16.0f, 16.0f), glow.Get());
    resources.target->FillRoundedRectangle(D2D1::RoundedRect(rect, 15.0f, 15.0f), fill.Get());
    resources.target->DrawRoundedRectangle(D2D1::RoundedRect(rect, 15.0f, 15.0f), border.Get(), 1.0f);
    resources.target->DrawTextLayout(D2D1::Point2F(rect.left + 17.0f, rect.top + 11.0f),
        layout.Get(), textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void MacOsUiFramework::PaintAlert(const AlertPaintResources& resources) {
    if (!alerts_.Visible()) return;
    // Hit regions are valid only for the most recently completed layout. Clear
    // them before any resource-dependent early return so a resize/device-loss
    // frame cannot leave clickable buttons at stale coordinates.
    alerts_.SetButtonRects({}, {});
    if (!resources.target || !resources.writeFactory || !resources.titleFormat ||
        !resources.messageFormat) return;
    const AlertRequest& request = alerts_.Request();
    const ThemePalette& palette = resources.palette;
    ComPtr<ID2D1SolidColorBrush> veil;
    resources.target->CreateSolidColorBrush(D2DColor(RGB(5, 9, 14),
        palette.dark ? 0.42f : 0.18f), &veil);
    if (!veil) return;
    resources.target->FillRectangle(D2D1::RectF(0, 0,
        resources.surfaceSize.width, resources.surfaceSize.height), veil.Get());

    const float cardWidth = std::min(520.0f,
        std::max(300.0f, resources.surfaceSize.width - 48.0f));
    const float textWidth = cardWidth - 48.0f;
    ComPtr<IDWriteTextLayout> titleLayout;
    ComPtr<IDWriteTextLayout> messageLayout;
    if (FAILED(resources.writeFactory->CreateTextLayout(request.title.c_str(),
        static_cast<UINT32>(request.title.size()), resources.titleFormat,
        textWidth, 72.0f, &titleLayout)) ||
        FAILED(resources.writeFactory->CreateTextLayout(request.message.c_str(),
        static_cast<UINT32>(request.message.size()), resources.messageFormat,
        textWidth, 180.0f, &messageLayout))) return;
    DWRITE_TEXT_METRICS titleMetrics{};
    DWRITE_TEXT_METRICS messageMetrics{};
    if (FAILED(titleLayout->GetMetrics(&titleMetrics)) ||
        FAILED(messageLayout->GetMetrics(&messageMetrics))) return;
    const float cardHeight = std::clamp(42.0f + titleMetrics.height +
        messageMetrics.height + 58.0f, 150.0f, 300.0f);
    const D2D1_RECT_F card = D2D1::RectF(
        (resources.surfaceSize.width - cardWidth) / 2.0f,
        (resources.surfaceSize.height - cardHeight) / 2.0f,
        (resources.surfaceSize.width + cardWidth) / 2.0f,
        (resources.surfaceSize.height + cardHeight) / 2.0f);

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
    const COLORREF primaryColor = alerts_.PrimaryHot()
        ? BlendColor(primaryBase, hoverTarget, 0.10f) : primaryBase;
    const COLORREF primaryOutline = BlendColor(primaryBase, RGB(0, 0, 0),
        palette.dark ? 0.18f : 0.10f);
    const COLORREF secondaryColor = alerts_.SecondaryHot() ? palette.buttonHot : palette.button;
    const COLORREF secondaryOutline = BlendColor(palette.border, secondaryColor, 0.10f);

    ComPtr<ID2D1SolidColorBrush> shadow, glow, fill, border, titleBrush, messageBrush;
    ComPtr<ID2D1SolidColorBrush> primaryFill, primaryBorder, primaryText;
    ComPtr<ID2D1SolidColorBrush> secondaryFill, secondaryBorder, secondaryText;
    resources.target->CreateSolidColorBrush(D2DColor(RGB(0, 0, 0), palette.dark ? 0.34f : 0.15f), &shadow);
    resources.target->CreateSolidColorBrush(D2DColor(accent, palette.dark ? 0.08f : 0.06f), &glow);
    resources.target->CreateSolidColorBrush(D2DColor(fillColor, palette.dark ? 0.97f : 0.985f), &fill);
    resources.target->CreateSolidColorBrush(D2DColor(borderColor, 0.94f), &border);
    resources.target->CreateSolidColorBrush(D2DColor(palette.text), &titleBrush);
    resources.target->CreateSolidColorBrush(D2DColor(palette.muted), &messageBrush);
    resources.target->CreateSolidColorBrush(D2DColor(primaryColor), &primaryFill);
    resources.target->CreateSolidColorBrush(D2DColor(primaryOutline), &primaryBorder);
    resources.target->CreateSolidColorBrush(D2DColor(BestContrastText(primaryColor)), &primaryText);
    resources.target->CreateSolidColorBrush(D2DColor(secondaryColor), &secondaryFill);
    resources.target->CreateSolidColorBrush(D2DColor(secondaryOutline), &secondaryBorder);
    resources.target->CreateSolidColorBrush(D2DColor(BestContrastText(secondaryColor)), &secondaryText);
    if (!shadow || !glow || !fill || !border || !titleBrush || !messageBrush ||
        !primaryFill || !primaryBorder || !primaryText || !secondaryFill ||
        !secondaryBorder || !secondaryText) return;

    const D2D1_RECT_F shadowRect = D2D1::RectF(card.left, card.top + 5.0f,
        card.right, card.bottom + 5.0f);
    const D2D1_RECT_F glowRect = D2D1::RectF(card.left - 2.0f, card.top - 2.0f,
        card.right + 2.0f, card.bottom + 2.0f);
    resources.target->FillRoundedRectangle(D2D1::RoundedRect(shadowRect, 22.0f, 22.0f), shadow.Get());
    resources.target->FillRoundedRectangle(D2D1::RoundedRect(glowRect, 24.0f, 24.0f), glow.Get());
    resources.target->FillRoundedRectangle(D2D1::RoundedRect(card, 22.0f, 22.0f), fill.Get());
    resources.target->DrawRoundedRectangle(D2D1::RoundedRect(card, 22.0f, 22.0f), border.Get(), 1.0f);
    const float textLeft = card.left + 24.0f;
    const float titleTop = card.top + 22.0f;
    resources.target->DrawTextLayout(D2D1::Point2F(textLeft, titleTop),
        titleLayout.Get(), titleBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    resources.target->DrawTextLayout(D2D1::Point2F(textLeft,
        titleTop + titleMetrics.height + 12.0f), messageLayout.Get(), messageBrush.Get(),
        D2D1_DRAW_TEXT_OPTIONS_CLIP);

    const bool confirmation = request.mode == AlertMode::Confirmation;
    const float primaryWidth = confirmation ? 150.0f : 80.0f;
    const float secondaryWidth = 92.0f;
    const D2D1_RECT_F primaryRect = D2D1::RectF(card.right - 24.0f - primaryWidth,
        card.bottom - 50.0f, card.right - 24.0f, card.bottom - 16.0f);
    const D2D1_RECT_F secondaryRect = confirmation
        ? D2D1::RectF(primaryRect.left - 10.0f - secondaryWidth, primaryRect.top,
            primaryRect.left - 10.0f, primaryRect.bottom)
        : D2D1::RectF();
    alerts_.SetButtonRects(ToFloatRect(primaryRect), ToFloatRect(secondaryRect));
    resources.target->FillRoundedRectangle(D2D1::RoundedRect(primaryRect, 17.0f, 17.0f), primaryFill.Get());
    resources.target->DrawRoundedRectangle(D2D1::RoundedRect(primaryRect, 17.0f, 17.0f), primaryBorder.Get(), 2.0f);
    if (confirmation) {
        resources.target->FillRoundedRectangle(D2D1::RoundedRect(secondaryRect, 17.0f, 17.0f), secondaryFill.Get());
        resources.target->DrawRoundedRectangle(D2D1::RoundedRect(secondaryRect, 17.0f, 17.0f), secondaryBorder.Get(), 2.0f);
    }

    const ScopedTextAlignment alignment(resources.titleFormat);
    resources.titleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    resources.titleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    resources.target->DrawTextW(request.primaryText.c_str(),
        static_cast<UINT32>(request.primaryText.size()), resources.titleFormat,
        primaryRect, primaryText.Get());
    if (confirmation) {
        resources.target->DrawTextW(request.secondaryText.c_str(),
            static_cast<UINT32>(request.secondaryText.size()), resources.titleFormat,
            secondaryRect, secondaryText.Get());
    }
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
            target->FillRoundedRectangle(
                D2D1::RoundedRect(track, 4.0f, 4.0f), trackBrush.Get());
        }
    }

    if (SUCCEEDED(target->CreateSolidColorBrush(
            D2DColor(dragging ? palette.accent : palette.text,
                hot || dragging ? 0.65f : 0.25f),
            &thumbBrush))) {
        target->FillRoundedRectangle(
            D2D1::RoundedRect(thumb, 4.0f, 4.0f), thumbBrush.Get());
    }
}

void MacOsUiFramework::PaintMediaCard(ID2D1RenderTarget* target,
    const MediaCardPresentation& presentation, const MediaCardBrushes& brushes) const {
    if (!target || !brushes.panel || !brushes.border) return;
    const D2D1_ROUNDED_RECT card = D2D1::RoundedRect(presentation.rect,
        presentation.radius, presentation.radius);
    if (presentation.paintSurface) {
        target->FillRoundedRectangle(card, brushes.panel);
        target->DrawRoundedRectangle(card, brushes.border, 1.0f);
    }
    if (presentation.selected && brushes.accent) {
        target->DrawRoundedRectangle(card, brushes.accent,
            presentation.focused ? 3.0f : 2.0f);
    }
    if (presentation.warning && brushes.danger) {
        target->DrawRoundedRectangle(card, brushes.danger, 3.0f);
        if (presentation.selected && brushes.accent) {
            const D2D1_RECT_F inner = D2D1::RectF(
                presentation.rect.left + 4.0f, presentation.rect.top + 4.0f,
                presentation.rect.right - 4.0f, presentation.rect.bottom - 4.0f);
            const float innerRadius = std::max(1.0f, presentation.radius - 4.0f);
            target->DrawRoundedRectangle(D2D1::RoundedRect(inner,
                innerRadius, innerRadius), brushes.accent,
                presentation.focused ? 2.0f : 1.5f);
        }
    }
}

void MacOsUiFramework::PaintOverlayLabel(ID2D1RenderTarget* target,
    const D2D1_RECT_F& rect, float radius, std::wstring_view text,
    IDWriteTextFormat* format, ID2D1Brush* fill, ID2D1Brush* textBrush,
    ID2D1Brush* border, float borderWidth) const {
    if (!target || !format || !textBrush) return;
    const D2D1_ROUNDED_RECT label = D2D1::RoundedRect(rect, radius, radius);
    if (fill) target->FillRoundedRectangle(label, fill);
    if (border && borderWidth > 0.0f) target->DrawRoundedRectangle(label, border, borderWidth);
    PaintSingleLineText(target, text, format, rect, textBrush);
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
