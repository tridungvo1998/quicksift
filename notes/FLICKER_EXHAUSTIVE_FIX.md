# Left-pane open/draw/close flicker — exhaustive fix

Tree: `QuickSift-0.9.3.1-filmstrip-final-repair`

## Symptom

Flicker still occurred whenever the left command pane was drawn — on open, during the width animation, on tab switch / content reveal, and on close — even after prior frost, stacking, and rounded-corner fixes.

## Root causes killed

| # | Root cause | Why it flickered | Kill |
|---|---|---|---|
| 1 | **`InvalidateCanvas()` on every pane animation tick** | `UpdateAnimations` treated any animation change as “changed” and forced a full D2D thumbnail-canvas rebuild ~30 Hz under/around the growing opaque pane | Split filmstrip vs pane vs tab-blend; pane ticks only call `LayoutLeftPaneAnimationFrame`; canvas invalidates only when filmstrip opacity changes |
| 2 | **`SetWindowRgn(..., TRUE)` every ease tick** | Backdrop corner radius tracked `6+10*ease` continuously → `ApplyRoundedControlRegion` → system redraw of the HWND each frame, flashing over siblings | `ApplyRoundedRegion` / `ApplyRoundedControlRegion` take `redraw=false` for animation; radius quantized to 2-DIP steps; skip when W/H/radius unchanged |
| 3 | **`SWP_SHOWWINDOW` every `PlaceLeftPaneBackdrop` call** | Re-asserting show every frame caused visibility/z-order paint churn mid-animation | `SWP_SHOWWINDOW` only when the backdrop was not already visible; otherwise move/size/z-order with `SWP_NOREDRAW` |
| 4 | **Mid-layout bare `ShowWindow(SW_HIDE/SHOW)`** | Tabs and section children painted mid-`LayoutControls` before the opaque compose finished | Quiet `SetWindowPos` hide/show with `SWP_NOREDRAW`; `SWP_SHOWWINDOW` only on first show; regions applied with `redraw=false` |
| 5 | **Content reveal at blend 0.28 into `LeftPaneOpenRect`** | Children laid out at full-open geometry while the animated backdrop was still ~28% width → chrome stuck out over thumbnails (read as flicker/pop) | Raise reveal to **0.82** / hide hysteresis **0.68** so children appear only when the backdrop is nearly full-width; still shown once per open session |
| 6 | **Multiple synchronous paints per frame** | Tab switch / layout: erase backdrop → show children → `RedrawActiveFlyoutContents` erased again and `RDW_UPDATENOW` per child; `SetActiveFlyout` also `UPDATENOW` tabs | One quiet `HideAll` → one `EraseLeftPaneBackdropFully` before show → raise+invalidate children → `UpdateWindow` pane chrome only (no parent `RDW_ALLCHILDREN`, no per-child `UPDATENOW` storm) |
| 7 | **`SetActiveFlyout` double clear + tab `UPDATENOW`** | Extra erase before `LayoutControls` plus forced tab paints flashed the canvas | Tab switch only quiet-hides; `LayoutControls` owns the single erase+compose |
| 8 | **Parent erase under visible children mid-frame** | `RDW_ERASE` / parent invalidates while children were mid-show left holes or stacked paints | Main `WM_ERASEBKGND` remains suppressed; backdrop erase only when content is hidden or immediately before first show; compose uses `RDW_NOERASE` afterward (`DrawFlyoutBackdrop` is opaque + double-buffered) |

## Intentionally not applied

| Idea | Why skipped |
|---|---|
| **`WS_EX_COMPOSITED` on the main window** | Unsafe with the D2D HWND canvas child (compose conflicts / full-frame cost). Pane chrome is GDI double-buffered in `DrawFlyoutBackdrop` instead. |
| **Pane host HWND refactor** | Correct long-term compose container, but too invasive for this release blocker; sibling stacking + quiet show/raise preserved. |

## Preferred model (now implemented)

1. **Single double-buffered opaque compose** — `DrawFlyoutBackdrop` (underlay → frost `PaintPanel` → BitBlt); children owner-draw on top.
2. **No mid-animation `ShowWindow`** — show once at reveal / hide once at close threshold.
3. **No `SetWindowRgn TRUE` every frame** — quiet region + quantize + skip unchanged.
4. **Children shown once then moved/raised** — hysteresis `LeftPaneShouldShowContent`; animation frame raises + invalidates only.
5. **Invalidate once per frame** — animation frame invalidates backdrop (and visible tabs); no forced canvas repaint.
6. **Never erase parent under visible children mid-frame** — clear only in the hide→show gap; then opaque child paints.

## Preserved

- Frost opacity (opaque `panelStrong` + highlight wash; no AlphaBlend see-through)
- Stacking (single active section; `HideAllLeftPaneSectionContent`)
- Tab clear (opaque erase before new section children)
- Rounded corners / `ApplyRoundedControlRegion`
- Scroll hardening / settle decode
- External-change reconcile
- Filmstrip behavior (still drives canvas invalidate when its opacity eases)

## Files touched

- `src/ui/framework/macos_ui_framework.h` — `ApplyRoundedControlRegion` / `ApplyRoundedRegion` `redraw` flag
- `src/ui/framework/macos_ui_framework.cpp`
- `src/ui/framework/macos_ui_framework_gdi.cpp` — `SetWindowRgn(..., redraw)`
- `src/ui/ui_layout_metrics.h` — reveal/hide blends
- `src/ui/main_window_controls_layout_and_painting.cpp` — place / anim / layout / redraw / set-active
- `src/app/quicksift_application.cpp` — `UpdateAnimations` split invalidation
- `CHANGELOG.md`
- `/workspace/quicksift/FLICKER_EXHAUSTIVE_FIX.md`

## Residual risks

- Linux box cannot run the Win32/D2D GUI; verification is source review + `verify-source-portable.py` only.
- Manual Windows checks: open pane over a dense thumbnail grid (no flash); rapid LIBRARY/FILE/FILTER/INFO/SETTINGS switches; close ease without child overhang; hover-still content stays painted; scroll/filmstrip unchanged; frost still opaque.
- Content still pops in at the high reveal threshold (by design) rather than per-child opacity fade.

## Verifier

`python3 scripts/verify-source-portable.py --source-dir .`
