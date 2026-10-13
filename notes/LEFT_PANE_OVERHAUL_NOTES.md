# Left pane overhaul notes

Tree: `QuickSift-0.9.3.1-filmstrip-final-repair`

## Layout behavior

- **Closed:** A narrow left-edge hit strip (`kLeftPaneHitStripDip`, painted via the existing flyout backdrop) stays visible for discoverability.
- **Open:** Hovering the hit strip (or remaining over the pane) opens one left command pane. Width is deterministic for a given client size:

  `clamp(round(clientWidth × kLeftPaneWidthFraction), min, max)`

  with an available-width ceiling so a usable canvas remains (`kLeftPaneMinWidthDip` / `kLeftPaneMaxWidthDip` / `kLeftPaneWidthFraction` in `ui_layout_metrics.h`).
- **Tabs:** Former floating flyout tabs are a vertical sidebar column inside the pane (`PaintSidebarTab`, horizontal labels). Selecting a tab switches the section; content fills the pane’s content rectangle to the right of the tab column.
- **Wrapping:** File / Cull / Filter / Settings toolbar buttons and drop-down menus flow left-to-right and wrap to a new row when they exceed the content width. Title-bar buttons already wrapped the same way.
- **Hide:** Leaving the pane and hit strip for `kLeftPaneHideGraceMs` closes the pane. The last selected section is restored on the next open. Fullscreen still forces the pane closed.
- **Canvas:** The photo canvas keeps full content bounds; the pane is layered above it (no reflow jump).

## Files changed

- `src/ui/ui_layout_metrics.h` — left-pane metrics
- `src/ui/framework/macos_ui_framework.h` — `PaintSidebarTab`
- `src/ui/framework/macos_ui_framework_gdi.cpp` — sidebar tab painting
- `src/app/quicksift_application_internal.h` — left-pane geometry API + `lastFlyoutSection_`
- `src/ui/main_window_controls_layout_and_painting.cpp` — hover pane layout/hover/paint
- `src/app/quicksift_application.cpp` — filmstrip failure-retry for visible window
- `USER_GUIDE.md`, `CHANGELOG.md`, `CODE_GUIDE.md`, `src/ui/README.md`

External-change reconcile (history + metadata Refresh from disk) was left untouched.

## Speed / loading fixes

- **Filmstrip glyph regression:** Layout and toggle disagreed (`▶`/`◀` vs `‹`/`›`); unified on `‹`/`›`.
- **Filmstrip decode retry:** `PrefetchFilmstrip` now clears failure/cooldown marks only for the current filmstrip index window before enqueueing, matching the “fresh attempt” intent without wiping the global failure map. Jobs still use `ignoreThumbnailViewport` on the thumbnail lane.

No other high-confidence loading regressions were changed (prefetch/metadata/scan paths already matched the recent filmstrip + reconcile work).

## Residual risks

- Linux box cannot run the Win32/D2D GUI; layout and hover grace were verified by source review + `verify-source-portable.py` only. Manual Windows check: hit-strip discovery, tab switching, wrap at narrow widths, Settings sections, Library/Info content, and fullscreen hide.
- Settings still uses a two-column sectioned flow inside the shared content rect; very short windows may clip the lowest settings rows (same class of risk as before, mitigated by full pane height).
- `PaintVerticalTab` remains in the framework for compatibility but is unused by the main window after this change.
- Hover polling is still timer-driven (`ID_TIMER_AUTOHIDE`); very fast mouse exits can briefly keep the pane until grace expires (intentional).

## Verifier

`python3 scripts/verify-source-portable.py --source-dir .` — passed.
