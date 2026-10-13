# Left-pane section stacking fix

Tree: `QuickSift-0.9.3.1-filmstrip-final-repair`

## Symptom

With LIBRARY (or another tab) active, the left pane content area showed **all** flyout sections at once — Settings section headers/buttons, Info EXIF text, User Comments (border clipped), File Delete, and Library “Remove from library” — overlapping in the translucent panel over the thumbnail grid.

## Root cause

1. **Shared content rect.** Every flyout section is laid out into the same `LeftPaneContentRect` / `FlyoutRect`. That is fine only if exactly one section’s children are `WS_VISIBLE`.

2. **Asymmetric hide.** `LayoutControls` SW_HIDE’d File/Cull/Filter/Settings controls when they were *not* the active panel, but:
   - When a panel *was* active, those controls were left at their prior visibility if `showPaneContent` was false (reveal/hide hysteresis from `LeftPaneShouldShowContent`).
   - Folders (`libraryList_`) and Info (`exif_`, comments) were only hidden when `activeFlyout_ !=` that panel — not when content should be fully hidden.
   So section children that had been shown into the shared rect could remain `WS_VISIBLE` while another tab was selected or while the pane was mid-animation.

3. **`SWP_SHOWWINDOW` on raise.** `RaiseLeftPaneContentAboveBackdrop` and `RedrawActiveFlyoutContents` raised children with `SWP_SHOWWINDOW`. Hardening notes said Z-order only; `SWP_SHOWWINDOW` reinforced visibility of anything still marked visible, stacking prior sections under/over the active one (Library list does not cover the full content rect, so Settings/Info chrome showed around it).

## Fix

| Change | Intent |
|---|---|
| `HideAllLeftPaneSectionContent()` | SW_HIDE every section child (buttons, labels, library, EXIF, comments) unconditionally |
| `LayoutControls` calls HideAll first, then shows only the active section when `LeftPaneShouldShowContent()` | Single-section invariant |
| Raise / redraw paths: drop `SWP_SHOWWINDOW` | Z-order above backdrop only; show/hide stays in LayoutControls |
| Info layout: fit EXIF + comments + save inside content height with edge pad | Full User comments border; no overflow clip |
| SW_HIDE flyout controls right after create | Avoid WS_VISIBLE defaults stacking at 0,0 before first layout |
| Fullscreen close uses `SetActiveFlyout(None)` | Same hide path as normal close |

Preserved: scroll hardening, reveal hysteresis, backdrop-under-content Z-order, external-change reconcile, filmstrip behavior.

## Files touched

- `src/ui/main_window_controls_layout_and_painting.cpp`
- `src/ui/main_window_chrome_and_messages.cpp`
- `src/app/quicksift_application_internal.h`
- `CHANGELOG.md`
- `/workspace/quicksift/LEFT_PANE_STACKING_FIX.md`

## Residual risks

- Linux box cannot run the Win32/D2D GUI; verification is source review + `verify-source-portable.py` only.
- Manual Windows checks: open pane → switch LIBRARY / FILE / FILTER / INFO / SETTINGS rapidly; only one section’s controls visible; open/close animation without stacking; Info comments full border at short window heights; no backdrop burying active controls; hover-still content stays painted.

## Verifier

`python3 scripts/verify-source-portable.py --source-dir .`
