# QuickSift 0.9.4.0: left-pane flicker and frost, search-field blocks, toast wrapping

**Tree:** `/workspace/quicksift/QuickSift-0.9.3.1-filmstrip-final-repair`. Not renamed, not zipped.

All line numbers refer to the files after the fix.

## 1. Left pane flickers (hovering buttons, switching tabs) and looks transparent

### Root causes

1. **The backdrop sat behind the canvas.**
   - `PlaceLeftPaneBackdrop` and `RedrawActiveFlyoutContents` called `SetWindowPos(flyoutBackdrop_, canvas_, …)`.
   - `hWndInsertAfter` places the window *below* the one named. The canvas is `HWND_BOTTOM` and `WS_CLIPSIBLINGS`, so it covered the backdrop completely.
   - Result: the pane had no surface at all. Thumbnails showed between the tabs and the content and behind the "Remove from library" button (as in the screenshot). Every repaint of a pane child flashed against the canvas.
2. **`LayoutControls` hid every pane child, then re-showed the active section, on every call.**
   - `HideAllLeftPaneSectionContent` ran at the top of the function. It is called on tab switches, pane-reveal flips, theme and resize.
   - Each hide exposed the area underneath and each show repainted the child.
   - `SWP_HIDEWINDOW` also triggers `ClearButtonHot`, so hovering was reset mid-fade.
3. **The tab switch forced two visible frames.**
   - `EraseLeftPaneBackdropFully` used `RDW_ERASE | RDW_UPDATENOW`: a solid panelStrong erase was painted and shown, then the real paint went on top.
   - The close path did the same, and the backdrop's `WM_ERASEBKGND` painted solid colour.
4. **Owner-drawn pane surfaces painted unbuffered, in layers, directly to the screen.**
   - `DrawGlassButton` → `PaintButton`: corner fill, shadow, fill, sheen, border, text. `PaintSidebarTab`, `PaintSectionTitle` and `PaintInfoPanel` worked the same way.
   - With the hover fade now animating about 8 frames per enter/leave, each frame was visible mid-paint.
5. **The library list** painted its panel in `WM_ERASEBKGND`, then rows, then a border through `GetDC`: three visible passes.

### Fixes

All in `src/ui/main_window_controls_layout_and_painting.cpp` unless noted.

- **Backdrop z-order** (lines 887 and 590 / 1114): `LeftPaneBackdropInsertAfter()` puts the backdrop directly above the canvas and the search input, and below all pane content.
  - Rules kept: no `SWP_SHOWWINDOW` in raise paths, and the region stays quantized to 2 DIP.
- **No hide/show churn** (lines 662 and 1352): `HideLeftPaneSectionContentExcept(keep)` hides only children that are not in the section being laid out. The kept section is moved with `SWP_NOREDRAW` and is never hidden or re-shown. `SetActiveFlyout` no longer hides everything.
- **No erase frames:**
  - `EraseLeftPaneBackdropFully` (line 696) only invalidates (`RDW_NOERASE`, no `UPDATENOW`).
  - The close path uses `RDW_NOERASE` (line 1642).
  - The backdrop's `WM_ERASEBKGND` returns 1 without painting (line 1805).
- **Buffered pane surfaces:**
  - `OnDrawItem` (line 1977) sends every left-pane surface through `MacOsUiFramework::PaintItemBuffered` (`macos_ui_framework_gdi.cpp:853`).
  - Each surface is painted off-screen on top of its own slice of the pane frost, then copied to the screen in one BitBlt.
  - Pane buttons drop the solid `parentSurface` (line 2137), so their anti-aliased corners blend into the real backdrop pixels.
- **What a hover now repaints:** only the button itself (`InvalidateRect(button, FALSE)` from the animator), as one blit. Nothing above it is repainted.
- **What a tab switch now does:** hide the old section quietly, move the new section quietly, invalidate the backdrop, then do one compose (`RedrawActiveFlyoutContents`). Every surface paints exactly once, and none of them erases first.

## 2. Frosted left pane, and the white strip in the Library list

### Frost

- **Portable builder** (`src/ui/framework/ui_frost.{h,cpp}`):
  1. box-average downscale ÷4;
  2. separable box blur, radius 3, two passes (approximately gaussian);
  3. tint with panelStrong at 86 % (dark) or 82 % (light) opacity;
  4. bilinear upscale back to pane size;
  5. a quantized signature.
- **Capture** (`MaybeCaptureLeftPaneFrost`, line 937, called from `Render` after `EndDraw`, `canvas_rendering_and_overlays.cpp:79`):
  - reads back only the canvas pixels under the open pane, via `CopyFromBitmap` into a CPU-readable bitmap and `Map`;
  - runs only when the frost is dirty (pane opened, theme or tint changed), or when the canvas re-rendered and at least 450 ms have passed;
  - never runs during scroll motion or fling (`ThumbnailScrollPrefersPlaceholders`).
- **Throttled captures** are finished by `ServiceLeftPaneFrost`, called from the animation tick (`quicksift_application.cpp:249`) with one canvas invalidate.
- **Repaint:** the pane is repainted only when the signature, origin or tint changes.
- **Painting:**
  - The frost is kept as one DIB in the framework (`SetPaneFrost` / `PaintPaneFrost`, `macos_ui_framework_gdi.cpp:744/788`).
  - The backdrop paints frost over the full pane rect (tab column, content area, bottom action row), plus a hairline and top wash (`PaintFrostedPanelChrome`, line 834).
  - Every child blits the same frost, so the pixels match exactly.
- **Fallback:** opaque panelStrong when transparency effects are off, high contrast is on, the swap-chain path is unavailable, or no snapshot exists yet.

### White strip in the Library list

- **Root cause:** the list was created with `WS_VSCROLL`, its theme was removed (`SetWindowTheme(L"", L"")`, classic light scrollbar), and there was no `WM_CTLCOLORLISTBOX`, so it used the default white `COLOR_WINDOW` brush. Any area the control painted itself came out light.
- **Fixes:**
  - Removed `WS_VSCROLL` and added `LBS_NOINTEGRALHEIGHT` (line 76).
  - `PaintLibraryList` (line 1070) is one buffered pass: frost corners, panel, rows (`WM_PRINTCLIENT` → `WM_DRAWITEM` into the buffer), framework overlay scrollbar, border.
  - Wheel (line 1825), thumb drag and keyboard scrolling are handled with redraw suspended, then one full repaint.
  - `WM_CTLCOLORLISTBOX` returns a panelStrong brush (`main_window_chrome_and_messages.cpp:1083`).
- **Other sections:**
  - The Info panel, section labels, tabs and buttons all paint fully opaque and are buffered.
  - The comment input is a D2D control with no native scrollbar.
  - No other pane child uses a native scrollbar.

## 3. Rectangular blocks under the search bar

### Root causes (`src/ui/framework/macos_ui_framework_d2d.cpp`)

- **Sheen vs. child fill:** `PaintFloatingField` used the glass recipe with a white sheen over the top half of the body. The borderless native input child clears with the flat `FloatingFieldFill`, so the child's rectangle showed as a darker block over the upper half of the field.
- **Child shown during the fade:** the child was already shown at fade opacity > 0.5 (`thumbnail_search_controller.cpp`). That put an opaque rectangle inside a half-transparent capsule while fading.
- **Ambient shadow steps:** the shadow was two stacked rings at half alpha each, giving hard visible steps.
- **Key shadow under translucent glass:** the key shadow is an offset copy of the body, and under translucent glass (toasts, alerts) it showed through as a displaced block.

### Fixes

- New `GlassSurfacePresentation::sheen`; the search field sets it to `false` (line 272), so the body equals the child's clear colour exactly.
- The input child is shown only at opacity ≥ 0.995 and hidden as soon as the field fades out (`kSearchInputShowOpacity`, `thumbnail_search_controller.cpp:164`). Ctrl+F jumps straight to fully opaque (line 406).
- The ambient shadow is now 4 concentric rounded rings at ¼ alpha each, a soft ramp (line 143).
- The key shadow is drawn only under opaque bodies (line 161).

## 4. Toast background does not cover multi-line text

### Root cause

`PaintToast` measured the text with a layout at the maximum width, then sized the body from `widthIncludingTrailingWhitespace`. After device-pixel snapping it called `layout->SetMaxWidth(body − padding)`.

- **Re-wrap:** the snapped width could be a fraction narrower than the measured widest line, so that line wrapped again. With line breaks or long text the extra line hung below the glass.
- **Height cap:** the layout height was also capped at 160 DIP with `DRAW_TEXT_OPTIONS_CLIP`.
- **Shared format state:** the shared text format's wrapping and alignment were inherited from other callers.

### Fix

- **Portable model** (`ui_models.h`):
  - `ToastTextLayoutWidth` / `ComputeToastGeometry`: body from the measured block, rounded outward by +2 DIP, capped to the window.
  - `ComputeAlertCardGeometry` for alerts.
  - `NormalizeOverlayText`: CRLF/CR → LF, tab → space, trailing breaks trimmed.
- **Toast painter:** creates one DirectWrite layout with wrapping and centring pinned and unbounded height. It is measured and drawn unchanged, centred on the toast, with a clip only when the toast is taller than the window allows. The slide animation moves the measured body.
- **Alerts:** messages are no longer cut at 220 DIP or 340 DIP. The card grows with the text up to the window height minus 48, and only then clips the message inside the card.
- **Call sites covered:** every caller goes through `ShowToast` / `ShowToastText` → `PaintToast` (write-lock waiting, batch summary, ratings import/export summary, language and cache toasts) and `ShowGlassAlert` → `PaintAlert`.

## Verification (Linux)

- **Build:** the `-DQS_WARNINGS_AS_ERRORS=ON` portable build is clean.
- **Check programs:** all 13 pass, including the new `overlay_and_frost_checks` (89 checks).
  - Toast: body covers every measured line plus padding (1/2/3/6 lines, explicit breaks, narrow windows), the paint width equals the measurement width, outward rounding, minimums, the clip cap, text normalisation.
  - Alert: grows past 340 DIP, clips only at the window.
  - Frost: invalid inputs, uniform result for a solid source including the edges and stride padding, alpha 0/1, the blur flattens a checkerboard, the signature is deterministic, changes with content and ignores sub-visible noise, the no-blur path, 1×1 input, the solid fallback.
- **Verifier:** `verify-source-portable.py` passes, including the new `verify_pane_frost_and_overlay_contracts`. It guards:
  - the backdrop z-slot, no hide-all in `LayoutControls`, invalidate-only erase, no solid erase in the backdrop;
  - buffered pane surfaces, no `WS_VSCROLL` on the list, the frost capture hook;
  - toast/alert geometry and the flat search field.
- **Localization:** the checker and `--self-test` pass. No new strings.
- **MinGW-w64 syntax check:** every changed Windows file passes. The only output is the known `D2DERR_RECREATE_TARGET` sign-compare warning.

## What to check on Windows

1. **Pane surface:** the open pane is fully covered, with the tab column, content and bottom button row on a frosted surface and no thumbnails showing through. The frost appears within a frame or two of opening and follows the content after scrolling stops.
2. **Hover:** hover across pane buttons and tabs, and fade them in and out. There should be no flicker and no flash of the canvas.
3. **Tab switch:** switch tabs by hovering down the tab column. The new section should appear in one step, with no blank pane frame and no stacked sections.
4. **Library list:** no white strip. With many folders, the overlay thumb appears and drag, wheel and arrow keys scroll cleanly with no smeared border.
5. **Search field:** no rectangle inside the capsule at rest or while it fades in and out after switching Thumbnails ↔ Single. Ctrl+F still focuses it immediately.
6. **Toasts:** multi-line toasts (write-lock waiting, batch or import summaries, long messages in a narrow window). The glass should cover every line during the slide-in too.
7. **Alerts:** long alert messages grow the card instead of being cut.
8. **Fallbacks:** with transparency off (Settings → Personalization → Colors) or high contrast, the pane should be opaque panelStrong.
9. **Theme toggle with the pane open:** the frost re-tints after the next canvas frame, never with the old theme's colour.
10. **Idle CPU with the pane open:** there should be no continuous canvas re-render (the frost refreshes at most once per 450 ms, and only after the canvas changed).
11. **Window resize or DPI change with the pane open:** the frost is rebuilt and the pane stays fully covered.
