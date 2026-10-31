// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Canvas help overlays: the '?' keyboard shortcut sheet, the first-run tip
// card and the Caps Lock auto-advance indicator.
//
// The sheet is generated from core/keyboard_shortcuts.h (the same table OnKeyDown
// dispatches through), so it cannot drift from the real shortcuts. Text layouts are
// built once per language (and after a device reset) and drawn with the theme's
// cached brushes: painting a frame allocates nothing. All geometry is in DIPs
// (Direct2D canvas), so the overlays follow the window DPI; named sizes live in
// ui_layout_constants.h (help_overlay).
#include "app/quicksift_application_internal.h"
#include "core/auto_advance_policy.h"
#include "core/settings_migration.h"
#include "ui/ui_layout_constants.h"

#include <algorithm>
#include <string>

namespace quicksift::app {
namespace {

namespace sc = quicksift::core::shortcuts;
namespace ho = quicksift::ui::layout::help_overlay;

int LanguageKey(const quicksift::Localizer& localizer) noexcept {
    return static_cast<int>(localizer.CurrentLanguage());
}

// Sections per sheet column (left: culling + navigation; right: the rest).
constexpr std::array<sc::Section, 2> kLeftSections{ { sc::Section::Culling, sc::Section::Navigation } };
constexpr std::array<sc::Section, 3> kRightSections{ { sc::Section::View, sc::Section::Edit, sc::Section::Help } };

struct ColumnText {
    std::wstring text;
    std::wstring keysOnly;   // measured to place the tab stop
    std::vector<DWRITE_TEXT_RANGE> titles;
    std::vector<DWRITE_TEXT_RANGE> keys;
};

template <std::size_t N>
ColumnText BuildColumn(const std::array<sc::Section, N>& sections, const quicksift::Localizer& localizer) {
    ColumnText column;
    bool first = true;
    for (const sc::Section section : sections) {
        if (!first) { column.text += L"\n"; column.keysOnly += L"\n"; }
        first = false;
        const std::wstring title = localizer.Text(sc::SectionTitleKey(section));
        column.titles.push_back({ static_cast<UINT32>(column.text.size()), static_cast<UINT32>(title.size()) });
        column.text += title;
        column.text += L"\n";
        column.keysOnly += L"\n";
        for (const sc::Shortcut& row : sc::kShortcuts) {
            if (row.section != section) continue;
            const std::wstring keys = localizer.Text(row.keys);
            column.keys.push_back({ static_cast<UINT32>(column.text.size()), static_cast<UINT32>(keys.size()) });
            column.text += keys;
            column.text += L"\t";
            column.text += localizer.Text(row.description);
            column.text += L"\n";
            column.keysOnly += keys;
            column.keysOnly += L"\n";
        }
    }
    while (!column.text.empty() && column.text.back() == L'\n') column.text.pop_back();
    return column;
}

D2D1_SIZE_F LayoutSize(IDWriteTextLayout* layout) noexcept {
    DWRITE_TEXT_METRICS metrics{};
    if (!layout || FAILED(layout->GetMetrics(&metrics))) return D2D1::SizeF(0.0f, 0.0f);
    return D2D1::SizeF(std::ceil(metrics.widthIncludingTrailingWhitespace), std::ceil(metrics.height));
}

void PinLeading(IDWriteTextLayout* layout, bool wrap) noexcept {
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
}

} // namespace

void QuickSiftApplicationImpl::ReleaseHelpOverlayLayouts() noexcept {
    shortcutSheetLeftLayout_.Reset();
    shortcutSheetRightLayout_.Reset();
    shortcutSheetTitleLayout_.Reset();
    firstRunTipLayout_.Reset();
    capsIndicatorLayout_.Reset();
    shortcutSheetLayoutLanguage_ = -1;
    firstRunTipLayoutLanguage_ = -1;
    capsIndicatorLayoutLanguage_ = -1;
}

bool QuickSiftApplicationImpl::BuildShortcutSheetLayouts() {
    if (shortcutSheetLeftLayout_ && shortcutSheetLayoutLanguage_ == LanguageKey(localizer_)) return true;
    shortcutSheetLeftLayout_.Reset();
    shortcutSheetRightLayout_.Reset();
    shortcutSheetTitleLayout_.Reset();
    if (!dwriteFactory_ || !smallText_ || !mediumText_) return false;
    const auto build = [&](const ColumnText& column, ComPtr<IDWriteTextLayout>& out) -> bool {
        ComPtr<IDWriteTextLayout> keysLayout;
        if (FAILED(dwriteFactory_->CreateTextLayout(column.keysOnly.c_str(),
                static_cast<UINT32>(column.keysOnly.size()), smallText_.Get(), ho::kLayoutMaxDip,
                ho::kLayoutMaxDip, &keysLayout)) || !keysLayout) return false;
        PinLeading(keysLayout.Get(), false);
        keysLayout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, { 0, static_cast<UINT32>(column.keysOnly.size()) });
        const float keysWidth = LayoutSize(keysLayout.Get()).width;
        if (FAILED(dwriteFactory_->CreateTextLayout(column.text.c_str(), static_cast<UINT32>(column.text.size()),
                smallText_.Get(), ho::kLayoutMaxDip, ho::kLayoutMaxDip, &out)) || !out) return false;
        PinLeading(out.Get(), false);
        out->SetIncrementalTabStop(keysWidth + ho::kSheetColumnGapDip * 0.5f);
        for (const DWRITE_TEXT_RANGE& range : column.keys) out->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, range);
        for (const DWRITE_TEXT_RANGE& range : column.titles) {
            out->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
            out->SetFontSize(ho::kSheetSectionTitleSizeDip, range);
        }
        return true;
    };
    if (!build(BuildColumn(kLeftSections, localizer_), shortcutSheetLeftLayout_) ||
        !build(BuildColumn(kRightSections, localizer_), shortcutSheetRightLayout_)) {
        ReleaseHelpOverlayLayouts();
        return false;
    }
    const std::wstring title = Tr(L"Keyboard shortcuts") + L"\n" + Tr(L"Press ? or Esc to close");
    if (FAILED(dwriteFactory_->CreateTextLayout(title.c_str(), static_cast<UINT32>(title.size()),
            mediumText_.Get(), ho::kLayoutMaxDip, ho::kLayoutMaxDip, &shortcutSheetTitleLayout_)) ||
        !shortcutSheetTitleLayout_) {
        ReleaseHelpOverlayLayouts();
        return false;
    }
    PinLeading(shortcutSheetTitleLayout_.Get(), false);
    const UINT32 firstLine = static_cast<UINT32>(title.find(L'\n'));
    shortcutSheetTitleLayout_->SetFontSize(ho::kSheetHintSizeDip, { firstLine, static_cast<UINT32>(title.size()) - firstLine });
    shortcutSheetTitleLayout_->SetFontWeight(DWRITE_FONT_WEIGHT_NORMAL,
        { firstLine, static_cast<UINT32>(title.size()) - firstLine });
    shortcutSheetLeftSize_ = LayoutSize(shortcutSheetLeftLayout_.Get());
    shortcutSheetRightSize_ = LayoutSize(shortcutSheetRightLayout_.Get());
    shortcutSheetTitleSize_ = LayoutSize(shortcutSheetTitleLayout_.Get());
    shortcutSheetLayoutLanguage_ = LanguageKey(localizer_);
    return true;
}

void QuickSiftApplicationImpl::ToggleShortcutSheet() {
    shortcutSheetVisible_ = !shortcutSheetVisible_;
    // The sheet answers the tip's "press ?": the tip has done its job.
    if (shortcutSheetVisible_ && firstRunTipVisible_) DismissFirstRunTip();
    InvalidateCanvas();
}

void QuickSiftApplicationImpl::HideShortcutSheet() {
    if (!shortcutSheetVisible_) return;
    shortcutSheetVisible_ = false;
    InvalidateCanvas();
}

void QuickSiftApplicationImpl::DrawShortcutSheetOverlay() {
    if (!shortcutSheetVisible_ || !renderTarget_ || !textBrush_ || !panelBrush_ ||
        !backgroundBrush_ || !glassBorderBrush_ || !mutedBrush_) return;
    if (!BuildShortcutSheetLayouts()) return;
    const D2D1_SIZE_F size = renderTarget_->GetSize();
    const float pad = ho::kSheetPaddingDip;
    const float bodyHeight = std::max(shortcutSheetLeftSize_.height, shortcutSheetRightSize_.height);
    const float width = pad * 2.0f + shortcutSheetLeftSize_.width + ho::kSheetColumnGapDip +
        shortcutSheetRightSize_.width;
    const float height = pad * 2.0f + shortcutSheetTitleSize_.height + ho::kSheetTitleGapDip + bodyHeight;
    // A small canvas scales the whole sheet down instead of clipping rows.
    const float scale = std::min({ 1.0f,
        std::max(0.1f, size.width - ho::kSheetMarginDip * 2.0f) / width,
        std::max(0.1f, size.height - ho::kSheetMarginDip * 2.0f) / height });

    const FLOAT veilOpacity = backgroundBrush_->GetOpacity();
    backgroundBrush_->SetOpacity(ho::kSheetVeilOpacity);
    renderTarget_->FillRectangle(D2D1::RectF(0.0f, 0.0f, size.width, size.height), backgroundBrush_.Get());
    backgroundBrush_->SetOpacity(veilOpacity);

    D2D1_MATRIX_3X2_F previous{};
    renderTarget_->GetTransform(&previous);
    const float left = (size.width - width * scale) * 0.5f;
    const float top = (size.height - height * scale) * 0.5f;
    renderTarget_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) *
        D2D1::Matrix3x2F::Translation(left, top) * previous);
    const D2D1_ROUNDED_RECT panel{ D2D1::RectF(0.0f, 0.0f, width, height), ho::kSheetRadiusDip, ho::kSheetRadiusDip };
    uiFramework_.PaintOverlayPanel(renderTarget_.Get(), panel.rect, panel.radiusX,
        panelBrush_.Get(), glassBorderBrush_.Get(), ho::kBorderWidthDip);
    renderTarget_->DrawTextLayout(D2D1::Point2F(pad, pad), shortcutSheetTitleLayout_.Get(), textBrush_.Get());
    const float bodyTop = pad + shortcutSheetTitleSize_.height + ho::kSheetTitleGapDip;
    renderTarget_->DrawTextLayout(D2D1::Point2F(pad, bodyTop), shortcutSheetLeftLayout_.Get(), textBrush_.Get());
    renderTarget_->DrawTextLayout(D2D1::Point2F(pad + shortcutSheetLeftSize_.width + ho::kSheetColumnGapDip, bodyTop),
        shortcutSheetRightLayout_.Get(), textBrush_.Get());
    renderTarget_->SetTransform(previous);
}

void QuickSiftApplicationImpl::ShowFirstRunTipIfNeeded() {
    firstRunTipVisible_ = quicksift::core::settings_migration::ShowFirstRunTip(firstRunTipDismissed_);
    InvalidateCanvas();
}

void QuickSiftApplicationImpl::DismissFirstRunTip() {
    if (!firstRunTipVisible_ && firstRunTipDismissed_) return;
    firstRunTipVisible_ = false;
    firstRunTipDismissed_ = true;
    firstRunTipRect_ = {};
    SaveSession();
    InvalidateCanvas();
}

void QuickSiftApplicationImpl::DrawFirstRunTipOverlay() {
    if (!firstRunTipVisible_ || shortcutSheetVisible_ || !renderTarget_ || !dwriteFactory_ || !smallText_ ||
        !panelBrush_ || !glassBorderBrush_ || !accentBrush_ || !textBrush_) {
        firstRunTipRect_ = {};
        return;
    }
    if (!firstRunTipLayout_ || firstRunTipLayoutLanguage_ != LanguageKey(localizer_)) {
        firstRunTipLayout_.Reset();
        const std::wstring title = Tr(L"Welcome to QuickSift");
        const std::wstring text = title + L"\n" +
            Tr(L"Move the pointer to the left edge to open the command pane, or pin it open with its Pin button.") +
            L"\n" + Tr(L"Press ? at any time for keyboard shortcuts.") + L"\n" + Tr(L"Click this tip to dismiss it.");
        if (FAILED(dwriteFactory_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                smallText_.Get(), ho::kTipWidthDip - ho::kTipPaddingDip * 2.0f, ho::kLayoutMaxDip,
                &firstRunTipLayout_)) || !firstRunTipLayout_) return;
        PinLeading(firstRunTipLayout_.Get(), true);
        firstRunTipLayout_->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, { 0, static_cast<UINT32>(title.size()) });
        firstRunTipTextSize_ = LayoutSize(firstRunTipLayout_.Get());
        firstRunTipLayoutLanguage_ = LanguageKey(localizer_);
    }
    const float left = ho::kTipEdgeDip;
    const float top = ho::kTipEdgeDip;
    firstRunTipRect_ = D2D1::RectF(left, top, left + ho::kTipWidthDip,
        top + firstRunTipTextSize_.height + ho::kTipPaddingDip * 2.0f);
    const D2D1_ROUNDED_RECT card{ firstRunTipRect_, ho::kTipRadiusDip, ho::kTipRadiusDip };
    uiFramework_.PaintOverlayPanel(renderTarget_.Get(), card.rect, card.radiusX,
        panelBrush_.Get(), accentBrush_.Get(), ho::kBorderWidthDip);
    renderTarget_->FillRectangle(D2D1::RectF(firstRunTipRect_.left + ho::kTipRadiusDip * 0.5f,
        firstRunTipRect_.top, firstRunTipRect_.right - ho::kTipRadiusDip * 0.5f,
        firstRunTipRect_.top + ho::kTipAccentBarDip), accentBrush_.Get());
    renderTarget_->DrawTextLayout(D2D1::Point2F(left + ho::kTipPaddingDip, top + ho::kTipPaddingDip),
        firstRunTipLayout_.Get(), textBrush_.Get());
}

void QuickSiftApplicationImpl::DrawCapsLockAutoAdvanceIndicator() {
    if (!renderTarget_ || !dwriteFactory_ || !smallText_ || !panelBrush_ || !accentBrush_ || !textBrush_) return;
    if (!quicksift::core::auto_advance::ShowCapsLockIndicator(autoAdvance_, (GetKeyState(VK_CAPITAL) & 1) != 0)) return;
    if (!capsIndicatorLayout_ || capsIndicatorLayoutLanguage_ != LanguageKey(localizer_)) {
        capsIndicatorLayout_.Reset();
        const std::wstring text = Tr(L"Auto-advance on (Caps Lock)");
        if (FAILED(dwriteFactory_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                smallText_.Get(), ho::kLayoutMaxDip, ho::kLayoutMaxDip, &capsIndicatorLayout_)) ||
            !capsIndicatorLayout_) return;
        PinLeading(capsIndicatorLayout_.Get(), false);
        capsIndicatorLayout_->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, { 0, static_cast<UINT32>(text.size()) });
        capsIndicatorTextSize_ = LayoutSize(capsIndicatorLayout_.Get());
        capsIndicatorLayoutLanguage_ = LanguageKey(localizer_);
    }
    const D2D1_SIZE_F size = renderTarget_->GetSize();
    const float width = capsIndicatorTextSize_.width + ho::kIndicatorPaddingXDip * 2.0f;
    const float height = capsIndicatorTextSize_.height + ho::kIndicatorPaddingYDip * 2.0f;
    const float left = (size.width - width) * 0.5f;
    const float top = ho::kIndicatorEdgeDip;
    const D2D1_ROUNDED_RECT pill{ D2D1::RectF(left, top, left + width, top + height),
        ho::kIndicatorRadiusDip, ho::kIndicatorRadiusDip };
    uiFramework_.PaintOverlayPanel(renderTarget_.Get(), pill.rect, pill.radiusX,
        panelBrush_.Get(), accentBrush_.Get(), ho::kBorderWidthDip);
    renderTarget_->DrawTextLayout(D2D1::Point2F(left + ho::kIndicatorPaddingXDip, top + ho::kIndicatorPaddingYDip),
        capsIndicatorLayout_.Get(), textBrush_.Get());
}

bool QuickSiftApplicationImpl::HandleHelpOverlayClick(float x, float y) {
    if (shortcutSheetVisible_) {
        HideShortcutSheet(); // any click closes the sheet (and is not passed on)
        return true;
    }
    if (firstRunTipVisible_ && x >= firstRunTipRect_.left && x < firstRunTipRect_.right &&
        y >= firstRunTipRect_.top && y < firstRunTipRect_.bottom) {
        DismissFirstRunTip();
        return true;
    }
    return false;
}

} // namespace quicksift::app
