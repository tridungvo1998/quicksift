// CODE GUIDE: See CODE_GUIDE.md -> "The authoritative owners".
// OWNER: Named device-independent (DIP) constants for the main window's control
// layout (left-pane sections, status bar, minimum window size) and the document
// windows (Help / About / Diagnostic log). Round 19 moved these out of the layout
// helpers as literals; values are unchanged. Shared metrics stay in
// ui_layout_metrics.h; convert with DipToPx / the window's DPI.
#pragma once

#include "ui/ui_layout_metrics.h"

namespace quicksift::ui::layout {

// Main window minimum tracking size (clamped to the monitor work area).
namespace main_window {
inline constexpr int kMinTrackWidthDip = 1120;
inline constexpr int kMinTrackHeightDip = 520;
} // namespace main_window

// Left-pane section content (LayoutFoldersSection / LayoutInfoSection /
// LayoutButtonFlowSection / LayoutSettingsSection).
namespace left_pane {
// Folders: the library list leaves room for the centred "Remove folder" button.
inline constexpr int kFolderActionReserveDip = 50;
inline constexpr int kFolderActionHeightDip = 32;
inline constexpr int kFolderActionMaxWidthDip = 160;
// Info: EXIF panel, comment label, comment edit and Save button stacked.
inline constexpr int kInfoSaveButtonHeightDip = 32;
inline constexpr int kInfoLabelHeightDip = 22;
inline constexpr int kInfoGapDip = 8;
// Floor of the bottom pad that keeps the text-input border stroke unclipped.
inline constexpr int kInfoEdgePadMinDip = 2;
inline constexpr int kInfoCommentHeightDip = 110;
// When the pane is short the comment box shrinks (to kInfoCommentMinHeightDip)
// before the EXIF panel drops below kInfoExifMinHeightDip.
inline constexpr int kInfoExifMinHeightDip = 48;
inline constexpr int kInfoCommentMinHeightDip = 56;
// File / Cull / Filter wrapped button rows.
inline constexpr int kFlowRowHeightDip = 30;
inline constexpr int kFlowRowGapDip = 6;
inline constexpr int kFlowButtonFallbackWidthDip = 96;
// Settings sections.
inline constexpr int kSettingsRowHeightDip = 34;
inline constexpr int kSettingsSectionHeaderHeightDip = 22;
inline constexpr int kSettingsSectionControlGapDip = 5;
inline constexpr int kSettingsRowGapDip = 6;
inline constexpr int kSettingsSelectorHeightDip = 34;
inline constexpr int kSettingsButtonFallbackWidthDip = 110;
inline constexpr int kLanguageSelectorMaxWidthDip = 210;

static_assert(kInfoCommentMinHeightDip <= kInfoCommentHeightDip, "comment box can only shrink");
static_assert(kFolderActionHeightDip < kFolderActionReserveDip, "remove button fits its reserve");
} // namespace left_pane

// Status bar inside its kStatusHeightDip band at the bottom of the window.
namespace status_bar {
inline constexpr int kTopOffsetDip = 2;
inline constexpr int kHeightDip = 34;
inline constexpr int kHeightWithProgressDip = 44;
static_assert(kTopOffsetDip + kHeightWithProgressDip <= kStatusHeightDip, "status bar fits its band");
} // namespace status_bar

// Document windows (document_and_menu_models.cpp).
namespace document {
// Title bar: text area and the single close light (painted and hit-tested alike).
inline constexpr int kTitleTextLeftDip = 16;
inline constexpr int kTitleTextRightReserveDip = 80;
inline constexpr int kCloseLightDiameterDip = 12;
inline constexpr int kCloseLightRightMarginDip = 15;
inline constexpr int kCloseHitInflateXDip = 4;
inline constexpr int kCloseHitInflateYDip = 5;
inline constexpr int kResizeBorderDip = 7;
// Content panel / accent strip painting.
inline constexpr int kPanelInsetDip = 2;
inline constexpr int kAccentInsetXDip = 22;
inline constexpr int kAccentTopDip = 7;
inline constexpr int kAccentBottomDip = 10;
// Layout: the edit fills the content area above one action row.
inline constexpr int kHorizontalMarginDip = 8;
inline constexpr int kBottomMarginDip = 6;
inline constexpr int kContentGapBelowTitleDip = 18;
inline constexpr int kButtonGapDip = 12;
inline constexpr int kButtonWidthDip = 112;
// "Previous sessions…" (diagnostic log) needs more room than the other buttons.
inline constexpr int kWideButtonWidthDip = 152;
// Default window sizes. The diagnostic log is wider so its action row fits
// Previous sessions, Refresh, Export, Close and both checkboxes in EN and VI.
inline constexpr int kDocumentWidthDip = 760;
inline constexpr int kDocumentHeightDip = 680;
inline constexpr int kDiagnosticLogWidthDip = 900;
inline constexpr int kMissingFilesWidthDip = 640;
inline constexpr int kMissingFilesHeightDip = 460;
inline constexpr int kButtonHeightDip = 38;
inline constexpr int kCheckboxFallbackTextWidthDip = 90;
inline constexpr int kCheckboxGlyphReserveDip = 34;
inline constexpr int kCheckboxGapDip = 6;
static_assert(kAccentTopDip < kAccentBottomDip, "accent strip has height");
} // namespace document

// 0.9.6.0 canvas help overlays (ui/shortcut_sheet_and_tips_overlay.cpp) and the
// command-pane pin (ui/left_pane_pin_controller.cpp). Canvas values are DIPs used
// directly by Direct2D; the pin button is converted with DipToPx.
namespace help_overlay {
inline constexpr float kSheetPaddingDip = 22.0f;
inline constexpr float kSheetColumnGapDip = 24.0f;
inline constexpr float kSheetTitleGapDip = 12.0f;
inline constexpr float kSheetMarginDip = 24.0f;      // minimum gap to the canvas edge
inline constexpr float kSheetRadiusDip = 14.0f;
inline constexpr float kSheetSectionTitleSizeDip = 13.0f;
inline constexpr float kSheetHintSizeDip = 12.0f;
inline constexpr float kSheetVeilOpacity = 0.35f;
inline constexpr float kTipWidthDip = 300.0f;
inline constexpr float kTipPaddingDip = 14.0f;
inline constexpr float kTipEdgeDip = 18.0f;          // from the canvas's left/top edge
inline constexpr float kTipRadiusDip = 12.0f;
inline constexpr float kTipAccentBarDip = 3.0f;
inline constexpr float kIndicatorPaddingXDip = 10.0f;
inline constexpr float kIndicatorPaddingYDip = 5.0f;
inline constexpr float kIndicatorEdgeDip = 12.0f;
inline constexpr float kIndicatorRadiusDip = 10.0f;
inline constexpr float kLayoutMaxDip = 4096.0f;
inline constexpr float kBorderWidthDip = 1.0f;
} // namespace help_overlay

namespace left_pane_pin {
inline constexpr int kButtonHeightDip = 30;
inline constexpr int kInsetDip = 10;   // same inset as the tab column
} // namespace left_pane_pin

} // namespace quicksift::ui::layout
