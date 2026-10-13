# UI bugfix notes

Tree: `QuickSift-0.9.3.1-filmstrip-final-repair`

## Bugs fixed

1. **Left pane flicker** — Root cause was discrete backdrop resize (strip ↔ full) plus opaque child erase without compositing. Backdrop width/left now lerp with `leftPaneOpenBlend_` (`Smoothstep01` + `EaseToward` on `ID_TIMER_ANIMATION`). Children wait for `kLeftPaneContentRevealBlend` before layout/show. Light `LayoutLeftPaneAnimationFrame` repositions the backdrop each tick; full `LayoutControls` only on reveal threshold flips.

2. **User comments clipped border** — D2D HWND target could be stale (smaller than client) and the 0.5px stroke sat on the clip edge. Paint now resizes the target, fills then strokes an inset rounded rect (`kTextInputBorderInsetDip` / `kTextInputRadiusDip`), and applies a matching rounded window region.

3. **Hit strip affordance** — When blend is low, `DrawFlyoutBackdrop` paints a thin accent rail and three grip dots on the strip.

4. **Button underlay/shadow mismatch** — `PaintButton` used rectangular `FillSolidRect` under a pill/rounded fill. Underlay is now `FillRoundedRectGp` with the **same** radius as the chrome; title/header use height/2 pill radius; shadow keeps identical bounds/radius (vertical offset only). Title and flyout buttons get matching `ApplyRoundedControlRegion`.

5. **Compare from Single** — `ID_MODE_COMPARE` in Single now `SetMode(Thumbnails)` + existing toast `Select 2 to 6 photos to enter Compare.` Compare mode still toggles back to Thumbnails; Thumbnails still uses `EnterCompareFromSelection()`.

6. **Tab / Labels spacing** — Shared metrics: `kButtonTextPaddingDip`, `kTitleButtonTextPaddingDip`, `kButtonPopupChevronDip`, `kSidebarTabPadDip`, `kButtonRadiusDip`. `FlyoutButtonWidthPx` and `MeasuredTitleButtonWidthsPx` use the same pads as `PaintButton`. Tab column width raised to 132 DIP.

7. **Open/close animation** — Real width animation as in (1); raised chrome radius eases with blend; hover over the animating pane during close cancels hide.

## Preserved prior work

External-change reconcile, left hover pane + vertical tabs, scroll polish / prefetch policy, MSVC `std::max` cast fixes in framework GDI.

## Files touched

- `src/ui/ui_layout_metrics.h`
- `src/ui/framework/macos_ui_framework_gdi.cpp`
- `src/ui/framework/macos_ui_framework.cpp`
- `src/ui/main_window_controls_layout_and_painting.cpp`
- `src/ui/main_window_chrome_and_messages.cpp`
- `src/ui/popup_menus_commands_and_folder_tree.cpp`
- `src/app/quicksift_application.cpp`
- `src/app/quicksift_application_internal.h`
- `CHANGELOG.md`

## Residual risks

- Linux cannot run the Win32/D2D GUI; verification is source review + `verify-source-portable.py` only.
- Manual Windows checks: left-pane open/close ease (no flash), hit-strip grip visibility, Info comments full border, selected Single/Compare/Thumbnails pills without dark corner underlay, Labels dropdown alignment vs other Filter buttons, Compare-from-Single → Thumbnails + toast, tab labels not clipped at 100%/125%/150% DPI.
- Content pops in at the reveal threshold rather than fading; acceptable low-cost tradeoff vs per-child opacity.
- Very short animation frames on slow machines may still show one frame of empty expanding chrome before content reveal.
