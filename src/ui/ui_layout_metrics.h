// CODE GUIDE: See CODE_GUIDE.md -> "The authoritative owners".
// OWNER: Shared device-independent UI dimensions; do not scatter duplicate layout numbers.

#pragma once

// Device-independent layout metrics shared by every QuickSift window and
// custom control. Add or tune spacing here instead of scattering magic numbers
// through drawing and layout code.
namespace quicksift::ui::layout {

inline constexpr int kTitleBarHeightDip = 44;
inline constexpr int kTitleBarExtraRowHeightDip = 34;
inline constexpr int kTitleBarBottomPaddingDip = 5;
inline constexpr int kTitleButtonHeightDip = 30;
inline constexpr int kTitleControlGapDip = 6;
inline constexpr int kFullscreenEdgeRevealDip = 7;
inline constexpr int kFullscreenKeepAliveDip = 12;
inline constexpr int kStatusHeightDip = 46;
// Narrow always-available hover target at the left edge. Hovering it opens the
// left command pane; the pane itself stays closed until then.
inline constexpr int kLeftPaneHitStripDip = 10;
// Vertical sidebar-tab column width inside the open left pane.
// Wide enough for "CULL & RATE" / "SETTINGS" with shared tab padding.
inline constexpr int kLeftPaneTabColumnWidthDip = 132;
inline constexpr int kLeftPaneTabHeightDip = 34;
inline constexpr int kLeftPaneTabGapDip = 4;
inline constexpr int kLeftPaneMinWidthDip = 300;
inline constexpr int kLeftPaneMaxWidthDip = 480;
// Pane width is clamp(clientWidth * fraction, min, max) for a given window size.
inline constexpr float kLeftPaneWidthFraction = 0.30f;
inline constexpr int kLeftPaneHideGraceMs = 280;
// Show pane children only once the expanding chrome is nearly full-width.
// Content is laid out at LeftPaneOpenRect; revealing too early lets children stick
// out past the still-narrow animated backdrop (open/draw flicker).
// Content stays shown for the rest of the open session (no per-frame show/hide).
inline constexpr float kLeftPaneContentRevealBlend = 0.82f;
// While closing, hide children once the backdrop has clearly started shrinking so
// they never overhang the closing chrome; hysteresis vs reveal avoids thrash.
inline constexpr float kLeftPaneContentHideBlend = 0.68f;
// Legacy alias kept so older comments/docs still resolve; equals hit-strip width.
inline constexpr int kAutoHideRailWidthDip = kLeftPaneHitStripDip;

// Shared button chrome metrics — PaintButton, width measure, and regions must agree.
inline constexpr int kButtonRadiusDip = 10;
// Shared left-pane content surfaces (library list, EXIF panel, section headers).
inline constexpr int kLeftPaneContentRadiusDip = 12;
inline constexpr int kButtonTextPaddingDip = 12;
inline constexpr int kTitleButtonTextPaddingDip = 14;
inline constexpr int kButtonPopupChevronDip = 20;
inline constexpr int kSidebarTabPadDip = 12;
inline constexpr int kSidebarTabRadiusDip = 10;
inline constexpr int kTextInputRadiusDip = 10;
inline constexpr int kTextInputBorderInsetDip = 1;
inline constexpr int kFolderPaneWidthDip = 306;
inline constexpr int kInfoPaneWidthDip = 334;
inline constexpr int kFlyoutBarHeightDip = 52;
inline constexpr float kThumbnailRailSafeInsetDip = 50.0f;
inline constexpr int kExifPaneMinHeightDip = 190;
inline constexpr int kOuterMarginDip = 12;
inline constexpr int kPanelRadiusDip = 20;

} // namespace quicksift::ui::layout
