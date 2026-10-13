# Left-pane rounded corners fix

Tree: `QuickSift-0.9.3.1-filmstrip-final-repair`

## Symptoms (screenshots)

1. **Library** — sharp white border around the content panel (`libraryList_`); sharp rectangular container behind **Remove from library**.
2. **File** — same class of sharp content/action chrome (File action buttons already had regions; Library remove did not).
3. **Info** — **USER COMMENTS** header was a sharp rectangle vs rounded PHOTO DETAILS / comment input / Save.

## Root causes

| Surface | Cause |
|---|---|
| Library content border | `WS_EX_CLIENTEDGE` on the listbox drew a sharp system edge; no `ApplyRoundedControlRegion` / panel stroke. |
| Remove from library | `PaintButton` drew a rounded fill on a rectangular HWND with no matching window region (same class of underlay bug previously fixed for title/flyout buttons). |
| USER COMMENTS header | Owner-draw section title painted text only on a rectangular HWND; Settings headers already had `ApplyRoundedControlRegion`, and `PaintSectionTitle` did not fill rounded chrome. |

## Fix

| Change | Intent |
|---|---|
| Drop `WS_EX_CLIENTEDGE`; `SetWindowTheme("", "")` on library list | Remove sharp system edge |
| `ApplyRoundedControlRegion(libraryList_, …, kLeftPaneContentRadiusDip)` | Clip list HWND to panel radius |
| `PaintPanel` on erase / empty paint; `PaintControlBorder` stroke after items | Rounded frosted border without covering list items |
| `ApplyRoundedControlRegion` on **Remove from library** (`kButtonRadiusDip`) | Rounded action strip button |
| `ApplyRoundedControlRegion` on `userCommentLabel_` + rounded fill in `PaintSectionTitle` | Header matches photo details / inputs / Save / Settings headers |
| Metric `kLeftPaneContentRadiusDip` (12) | Shared content radius for library list + EXIF panel |

## Preserved

- Left-pane frost (opaque `panelStrong` + highlight wash)
- Tab-switch clear / single-section stacking
- Scroll hardening / filmstrip / external-change reconcile
- File/Cull/Filter/Settings buttons already used `ApplyRoundedControlRegion`

## Files touched

- `src/ui/ui_layout_metrics.h`
- `src/ui/framework/macos_ui_framework.h`
- `src/ui/framework/macos_ui_framework_gdi.cpp`
- `src/ui/main_window_controls_layout_and_painting.cpp`
- `CHANGELOG.md`
- `/workspace/quicksift/ROUNDED_CORNERS_FIX.md`

## Residual risks

- Linux box cannot run the Win32/D2D GUI; verification is source review + `verify-source-portable.py` only.
- Manual Windows checks: Library empty + populated list (rounded border, scrollbar still usable), Remove button corners, Info USER COMMENTS vs PHOTO DETAILS, File/Cull action buttons, Settings section headers, open/close frost and tab switches unchanged.

## Verifier

`python3 scripts/verify-source-portable.py --source-dir .`
