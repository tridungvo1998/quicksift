# Left-pane frost + tab-switch clear fix

Tree: `QuickSift-0.9.3.1-filmstrip-final-repair`

## Symptoms

1. Left pane looked **transparent / see-through to thumbnails** (not frosted).
2. Pane still **flickered** during open/close and tab changes.
3. **Switching tabs** left prior section chrome visible under/over the new section (messy stack in the shared content rect).

## Root causes

1. **Translucent GDI+ base alpha.** `PaintPanel` used `ToGpColor(panelStrong, 250)` when `PanelPresentation.translucent` was true. On a non-layered HWND, semi-transparent GDI+ fills AlphaBlend with whatever sits under the window — here the thumbnail `canvas_` sibling — so the “frosted” pane read as see-through glass instead of an opaque frosted surface. `DrawFlyoutBackdrop` also skipped an opaque underlay, and `FlyoutBackdropSubclassProc` returned `1` from `WM_ERASEBKGND` without filling while most redraws used `RDW_NOERASE`.

2. **Tab switch without opaque clear.** Sections share one `LeftPaneContentRect`. `HideAllLeftPaneSectionContent` + show-new ran in one `LayoutControls` pass without forcing an opaque backdrop erase/`UPDATENOW` between hide and show. Quiet paint of new children on top of uncleared prior pixels (and any residual translucent blend) produced a stacked mess. `ShowWindow(SW_HIDE)` mid-layout also contributed paint thrash.

3. **Flicker contributors.** Mid-frame `ShowWindow` on every section child during hide; backdrop invalidate without an opaque compose path; children painted without a guaranteed opaque backdrop pass first.

## Fix

| Change | Intent |
|---|---|
| `PaintPanel`: always opaque `panelStrong` base; `translucent` → top highlight wash only | Frosted look without see-through to thumbnails |
| `DrawFlyoutBackdrop`: opaque `PaintSolidSurface` underlay + frosted `PaintPanel` + double-buffer BitBlt | Stable opaque chrome; no mid-paint flicker |
| `FlyoutBackdropSubclassProc` `WM_ERASEBKGND`: fill opaque `panelStrong` | `RDW_ERASE` clears holes |
| `EraseLeftPaneBackdropFully()` | `RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW` |
| `SetActiveFlyout` tab switch: `HideAll` then `EraseLeftPaneBackdropFully` before `LayoutControls` | Clear shared content rect before new section |
| `LayoutControls`: erase fully after placing backdrop, before showing active section children | Same invariant for all layout paths |
| `HideAllLeftPaneSectionContent`: `SWP_HIDEWINDOW \| SWP_NOREDRAW` | No ShowWindow thrash mid-frame |
| `RedrawActiveFlyoutContents`: opaque erase backdrop, then raise/paint **active** section only | Stacking fix preserved |

## Preserved

- Single active section visibility (`HideAll` + show active only)
- Backdrop under content Z-order (`PlaceLeftPaneBackdrop` after `canvas_`, never `HWND_TOP` on raise with `SWP_SHOWWINDOW`)
- Scroll hardening / settle decode path
- External-change reconcile
- Filmstrip behavior
- Reveal/hide hysteresis (`LeftPaneShouldShowContent`)

## Files touched

- `src/ui/framework/macos_ui_framework_gdi.cpp` — `PaintPanel`
- `src/ui/main_window_controls_layout_and_painting.cpp` — backdrop draw/erase, tab switch, hide, layout
- `src/app/quicksift_application_internal.h` — `EraseLeftPaneBackdropFully`
- `CHANGELOG.md`
- `/workspace/quicksift/LEFT_PANE_FROST_CLEAR_FIX.md`

## Residual risks

- Linux box cannot run the Win32/D2D GUI; verification is source review + `verify-source-portable.py` only.
- Manual Windows checks: open pane over a dense thumbnail grid (no see-through); rapid LIBRARY / FILE / FILTER / INFO / SETTINGS switches (only one section visible, content rect cleared); open/close ease without flicker; hover-still content stays painted; scroll/filmstrip unchanged.

## Verifier

`python3 scripts/verify-source-portable.py --source-dir .`
