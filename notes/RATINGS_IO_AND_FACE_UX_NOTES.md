# QuickSift 0.9.4.0: anti-aliased UI, face-outline UX, click-to-zoom toggle, Import / Export Ratings

Tree: `/workspace/quicksift/QuickSift-0.9.3.1-filmstrip-final-repair` (0.9.4.0, includes FaceLock multi-face). Edited in place: not zipped, not renamed, nothing removed. The "view jumps to the face when results arrive" behaviour is unchanged.

## 0. Right-click finding (checked before implementing)

- Canvas (`HandleCanvasMessage`, `src/ui/main_window_chrome_and_messages.cpp`) had **no** `WM_RBUTTONDOWN`, `WM_RBUTTONUP` or `WM_CONTEXTMENU` case. The messages fell through to `DefWindowProc`. The generated `WM_CONTEXTMENU` went to the main window, which does not handle it either. So right-click did nothing in Single/Compare.
- The only right-button handler in the app is on the custom **title bar overlay**: `TitleOverlaySubclassProcImpl` → `WM_RBUTTONUP` → `ShowWindowSystemMenu`. It is at `main_window_chrome_and_messages.cpp:730` before this change and `:729` now. It is a different window and is untouched.
- Therefore the toggle **was implemented**: new canvas case `WM_RBUTTONUP` at `main_window_chrome_and_messages.cpp:1312` → `ToggleFaceOutlinesPinned` (`view_state_controller.cpp`). It acts only when:
  - FaceLock is on;
  - the view is Single/Compare and the click is inside a photo pane (not the filmstrip);
  - no glass alert is open.

  Otherwise it falls through exactly as before.

## 1. Anti-aliased UI

### Audit result and changes

| Surface | Before | Now |
|---|---|---|
| D2D canvas (all overlays, toasts, alerts, face outlines, HUD) | default AA modes | `SetAntialiasMode(PER_PRIMITIVE)` + `SetTextAntialiasMode(GRAYSCALE)` every `BeginDraw`. Grayscale is used because the canvas is composited/translucent, where ClearType fringes are wrong. |
| `ui_design_system.cpp` `FillRoundedRect` | GDI `RoundRect` | GDI+ AA path (`SmoothingModeAntiAlias`, `PixelOffsetModeHalf`), border via `DrawRoundedRectGp` |
| Section title panel, status icons, menu checkmark | GDI `RoundRect`/`Ellipse`/`LineTo` | GDI+ `FillRectangle`/`DrawRoundedRectGp`/`FillEllipse`/`DrawLines` |
| Vertical tab text | GDI+ default hint after rotation | `TextRenderingHintAntiAlias` (ClearType is invalid on rotated text) |
| Glass buttons (toolbar, title bar, panels) | AA shape inside an exact-radius `SetWindowRgn` → jagged region edge | `ApplyAntialiasedControlRegion` (region radius − min(r, 3) px). The painter fills the slack with the exact parent surface (`ButtonPresentation::parentSurface`) and draws the AA shape inside it. |
| Document window buttons (About, Guide, Missing files) | exact region | same AA region. The parent colour is sampled from the document window's vertical gradient at the button's centre. |
| Canvas (DXGI swap chain) | exact region | AA region plus `PaintRoundedCornerUnderlay`: the true AA corner arc is drawn in each corner, coloured from the frosted backdrop pixels under that corner (light theme: bottom corners blended toward the shadow). |
| Popup menus | GDI region + hand-drawn border | Win11: `DWMWA_WINDOW_CORNER_PREFERENCE = ROUND` (33) + theme border colour (34); custom border skipped. Win10/failure: previous region path. |
| Glass alert (D2D) | text-agnostic button widths | widths measured with `IDWriteTextLayout` (EN/VI), optional tertiary button, 2 px accent focus ring |

### Not changed, by design

- **Left-pane flyout tabs and the status/EXIF/backdrop panels keep exact regions.** Their underlay is a live frosted bitmap, not a flat colour, so slack pixels cannot be painted to match. Their corners keep the previous (already polished) treatment. Changing them would risk the flicker/underlay regressions fixed earlier.
- **Text in GDI+ paths** keeps the existing ClearTypeGridFit/AntiAlias hints set in the framework helpers.
- **Raw GDI drawing in feature files.** Remaining raw GDI calls are invisible fills (erase backgrounds) inside the framework. The verifier already forbids raw GDI shape/text calls in feature files.

### Cost and flicker

- No new invalidations, timers or layered windows were added; everything happens inside existing paint passes.
- The corner underlay is four clipped strokes per frame.
- Region updates happen at the same call sites as before.

## 2. Face outlines: auto-hide

Implemented in `review/face_interaction_policy.*` (`FaceOutlineFade`, portable).

- **Hold and fade.** Outlines are fully visible for 1 s (`kOutlineHold`), then fade linearly over 250 ms (`kOutlineFade`), eased with smoothstep in the renderer.
- **Frame tracking.** `BeginFrame` / `Observe(path)` / `EndFrame` run per frame. A path that was not displayed in the previous completed frame re-arms (this covers photo change, and FaceLock being turned on); so does a changed face set (faces arrive). A frame that never reaches `EndFrame` (device loss) leaves the state intact.
- **Explicit reveal.** `Reveal(path)` runs on a lock change: face click (`SelectFaceAndFrame`) and click-to-zoom revert.
- **Opacity.** The value reaches `MacOsUiFramework::PaintFaceOutline` via `FaceOutlinePresentation::opacity`, which scales the shadow and stroke; nothing is drawn below 0.4%.
- **Hit boxes** come from current layout (`HitFaceOutline`), never from what was painted, so faded outlines stay clickable.
- **Repaint cost.** `UpdateAnimations` invalidates only while a fade is actually progressing (`NeedsRepaint`). The 1 s hold and the faded or pinned states cost nothing.
- **Right-click pin.**
  - `SetPinned` toggles "always shown".
  - It is persisted in the session file as `FaceOutlinesPinned` (default 0 = auto-hide), saved immediately like Sync/FaceLock, and kept across folder changes (`Reset` keeps the pin).
  - A localized toast confirms the change.

## 3. Click-to-zoom toggle and robust click detection

`FaceZoomToggle`, `PressGesture` and `ComputeDragThreshold` live in `review/face_interaction_policy.*`; the wiring is in `view_state_controller.cpp` and `navigation_input_and_session.cpp`.

- **Toggle.**
  - First click on a face: `SelectFaceAndFrame` (≈70% framing). Then `CommitLockZoom` stores the pre-lock `ViewState` and pane snapshot, and the framed snapshot.
  - Clicking the **same** face while the pane is still at that framing (`Decide` compares the current snapshot with the framed one) gives **Revert**: the pre-lock zoom/pan is restored through `StoreEditableViewState`. The face stays the target, and its persisted selection is not touched.
  - Clicking a **different** face while zoomed reframes but keeps the ORIGINAL pre-lock view (`keepExistingPreZoom`).
- **Compare.** Entries are keyed per pane path. With **Sync on**, one shared key is used, consistent with the earlier choice that the shared view takes the framing and the other panes stay centred on their own targets. The revert restores the shared view.
- **Decision inputs are captured at mouse-down** (`CaptureFaceTogglePress`), before Compare slot activation can alter any view.
- **Clears** (`ClearFaceZoomToggle` / `Clear(key)`):
  - wheel zoom;
  - zoom modes from keyboard or toolbar (`SetZoomMode` → navigation epoch);
  - a press that becomes a pan;
  - navigation (`AdvanceReviewNavigationEpoch`);
  - `RotateView`, `ResetView` and canvas resize (`ResizeCanvasTarget`);
  - folder load;
  - FaceLock and Sync toggles (Sync changes the key space).
- **Click vs drag.**
  - The press uses `SetCapture`.
  - Threshold = max(`SM_CXDRAG`, `SM_CYDRAG`) at the canvas DPI, converted to DIP, with a 4 DIP floor (`kFaceClickMaxTravelDip`). It uses `GetSystemMetricsForDpi` when present (Win10 1607+), else system metrics scaled by `LOGPIXELSX`.
  - Once the threshold is exceeded the press is a drag, never a click. Panning starts from the press origin so no travel is lost.
  - A release must hit the same path and face index, so releasing outside the hit box is not a click.
  - `WM_CAPTURECHANGED` (to another window) or Escape (`CancelCanvasPress`) cancels the press.
  - **Double-click**: Windows sends `WM_LBUTTONDBLCLK` instead of a second `WM_LBUTTONDOWN`. That message never reaches the toggle. The Fit/100% double-click is still suppressed for one double-click time after a face click, and the trailing button-up has no active press. So there is never a double toggle.
- **Checks**: `scripts/checks/face_interaction_checks.cpp`, 58 checks covering snapshots, toggle decisions (revert, keep-original, clears, Sync key), gesture thresholds and cancel, and the fade timeline, pin and Reset.

## 4. Import / Export Ratings

### Format (exact; single source of truth in `core/ratings_exchange.h`)

UTF-8 without BOM, CRLF line endings. Example:

```
Header: QuickSift version 0.9.4.0 - Ratings/Pick state / Color tag data
Exported time: HH:MM:SS DD Mmm YYYY
Total files count: N files total
Files that has been rated, picked or rejected, or color tagged: M files
—
Exported data:
FILENAME.EXTENSION | Star Rating | Pick State | Color Tag
IMG_0001.CR3 | 5 | Picked | Red
—
Footer: End of QuickSift exported data.
```

- **Canonical tokens.**
  - Star Rating: `0`..`5`.
  - Pick State: `Picked`, `Rejected` or `None`.
  - Color Tag: `Red`, `Yellow`, `Green`, `Blue`, `Purple` or `None`.
- **Version** comes from `QS_VERSION_FILE_STRING`. The month is a 3-letter English abbreviation. The separator is U+2014 alone on its line.
- **Parser tolerance.** The parser accepts an optional BOM, LF or CRLF line endings, and surrounding spaces or tabs around tokens. It rejects everything else:
  - with an error code, a 1-based line number and the verbatim offending line;
  - on duplicates, including case-insensitive duplicates as judged on Windows during reconciliation;
  - on count mismatches;
  - on any text after the footer;
  - on files over 64 MiB.

### Flow

1. **Entry point.** The File tab gets a new **Import / Export Ratings** button (`ID_RATINGS_EXCHANGE` = 1052). It opens a glass chooser: **Export…** (primary, Enter), **Import…** (tertiary) or **Cancel** (Esc).
2. **Export.**
   1. Refused if no folder is open, the listing is incomplete, or a transaction or ratings job is busy.
   2. An `IFileSaveDialog` opens in the current folder with the default name `QuickSift-Ratings-YYYYMMDD-HHMMSS.txt` (local time) and the *.txt filter.
   3. A catalog snapshot is taken after the dialog closes.
   4. A worker thread (COM MTA, like the other workers) reads non-authoritative fields from disk (`ReadPhotoMetadata`) and keeps only rows with data.
   5. The file is written as temp `.quicksift-partial` → `FlushFileBuffers` → `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`.
   6. Status-bar progress ("Exporting ratings: d / t files"); a toast reports the result, an alert reports failure.
3. **Import.**
   1. An `IFileOpenDialog` (*.txt) opens. A worker reads and parses the file.
   2. On the UI thread the generation is checked; then a strict validation error produces an alert with the reason, line number and line, and nothing changes.
   3. Reconciliation against the current folder catalog uses Windows case-insensitive name folding (`LCMapStringEx`, invariant, uppercase).
   4. **Count mismatch** → "Total files count mismatch. Expected is x files, y files present. Continue?" (Continue / Cancel).
   5. **Missing files** → "Some files to be modified are missing. Ignore and continue?" (Yes / Cancel / **See missing files list**). The list opens in a `DocumentKind::MissingFiles` window:
      - read-only plain-text RichEdit, Consolas, themed, scrollable;
      - selectable and copyable (Ctrl+A, Ctrl+C), with the verbatim source lines;
      - it does **not** dismiss the warning (`tertiaryKeepsOpen`); pressing the button again re-focuses the window.
   6. "**Are you sure? This cannot be undone.**" (title "Import ratings for N files"; Import / Cancel).
   7. **Apply.**
      - Revalidates the folder and generation and re-reconciles against the catalog as it is now.
      - Calls `SubmitRatingsImport`: one `MetadataTransactionPlan` over the existing pipeline, with optimistic catalog edits, storage preflight, the serialized transaction, and expected-value conflict checks when the current value is authoritative.
      - Status-bar progress uses the new `MetadataTransactionProgress`.
   8. **Summary.** A toast reads "Ratings import complete: A applied, S skipped, F failed."; failures, cancellation or conflicts show an alert instead.
      - *Skipped* = missing files + files whose values are already identical (or Unsupported and neutral).
      - *Failed* = not in the catalog at submit time, or not completed by the transaction.
4. **Scope.**
   - Export covers every photo in the current folder's catalog and ignores view filters.
   - Import modifies only listed, present files. Unlisted files are never touched.
5. **Undo.** None: `recordHistory = false`, and Redo is cleared after a change. A whole-batch undo entry would make history replay depend on hundreds of external-change checks. Keeping it out is simpler and matches the "cannot be undone" confirmation. Export first for a backup (documented in the USER_GUIDE).
6. **Cancellation.**
   - Folder change: a pending import is dropped and the missing-files window closes.
   - Generation mismatch: "cancelled because the folder changed" toast.
   - Shutdown: `StopRatingsWorker` runs in `WM_DESTROY` and in the destructor, so no joinable `std::thread` is left behind.

### Dialogs

- **Glass alerts** (in-canvas, themed, rounded, AA D2D):
  - Enter activates the focused button (default primary); Esc cancels.
  - Tab / Shift+Tab and the arrow keys move focus, shown by a visible accent focus ring.
  - They are DPI-aware because the canvas is DIP-based, and every string is localized (EN/VI).
- **Missing-files window**: the existing themed document window, with AA rounded buttons and DPI scaling.
- **File pickers**: the native shell dialogs.

## Files

- **New**:
  - `src/core/ratings_exchange.{h,cpp}`;
  - `src/review/face_interaction_policy.{h,cpp}`;
  - `src/ui/ratings_exchange_commands.cpp`;
  - `scripts/checks/ratings_exchange_checks.cpp` and `scripts/checks/face_interaction_checks.cpp`.
- **Changed**:
  - `CMakeLists.txt`;
  - app: `src/app/application_support.h`, `src/app/quicksift_application_internal.h`, `src/app/quicksift_application.cpp`;
  - `src/localization.cpp` (EN→VI);
  - transactions: `metadata_transaction_service.{h,cpp}`, `metadata_transaction_execution.cpp`, `transaction_coordinator.{h,cpp}`;
  - UI: `ui_command_ids.h`, `ui_control_catalog.h`, `ui_design_system.cpp`, `canvas_rendering_and_overlays.cpp`, `view_state_controller.cpp`, `navigation_input_and_session.cpp`, `main_window_chrome_and_messages.cpp`, `main_window_controls_layout_and_painting.cpp`, `document_and_menu_models.cpp`, `app_theme_documents_and_localization.cpp`, `metadata_history_and_file_safety.cpp`, `popup_menus_commands_and_folder_tree.cpp`, `catalog_loading_and_result_integration.cpp`, `graphics_resources_and_image_cache.cpp`;
  - UI framework: `framework/macos_ui_framework{.h,.cpp,_d2d.cpp,_gdi.cpp,_popup.cpp}`, `framework/ui_models.{h,cpp}`;
  - docs: `CHANGELOG.md`, `USER_GUIDE.md`, `ARCHITECTURE.md`, `src/review/README.md`.

## Verification (Linux box)

- **Source verifier**: `python3 scripts/verify-source-portable.py --source-dir .` → **passed**, with no rule changes. The corner-underlay D2D painter was placed in the framework because feature files may not call `Draw*RoundedRect*`.
- **Portable build**: `cmake -S . -B /tmp/qs-build -DQS_WARNINGS_AS_ERRORS=ON && cmake --build /tmp/qs-build` → OK (QuickSiftCore, including the new core/review modules).
- **Portable checks** (all built with `-Wall -Wextra -Wpedantic -Wshadow`):
  - `ratings_exchange_checks`: 70/70 pass;
  - `face_interaction_checks`: 58/58 pass;
  - `face_policy_checks`: 49/49 pass.
- **MinGW cross syntax check** (`-std=c++20 -Wall -Wextra -Wshadow`) of every changed Windows translation unit: no errors and no new warnings. The only warnings are pre-existing ones:
  - HRESULT sign-compare at canvas:65 and catalog_loading:919;
  - `context` shadow at app_theme:216;
  - `FARPROC` casts in ui_design_system:66/75.
- **Not done on this box**: a full MSVC/Windows build, linking, and runtime UI testing.

## Windows risks / things to test on a real machine

1. **AA region slack.** The slack is painted with the exact parent colour. A control placed over a gradient or non-flat parent other than the title bar, panels or document windows would show a faint halo at its corners. Glass buttons were audited, but check high-contrast themes.
2. **Canvas corner underlay.**
   - It depends on `backgroundPixels_` being current. During a live window resize the corners can lag by one frame.
   - The light-theme bottom-corner blend is an approximation of the drop shadow.
   - Check both themes at 100/150/200% DPI.
3. **DWM popup rounding** (attributes 33/34) is Win11-only. On Win10 the call fails and the old region path is used. Check that menu borders still look right on Win11 with a custom accent colour.
4. **`GetSystemMetricsForDpi`** is resolved dynamically. On per-monitor-v2 mixed-DPI setups confirm that the drag threshold feels right on both monitors.
5. **Swap-chain plus region** interaction: verify there is no flicker when the left pane opens over the canvas (the underlay is painted inside the existing D2D frame).
6. **RichEdit plain text.** The missing-files window relies on `EM_SETTEXTMODE(TM_PLAINTEXT)` on an empty control. If RichEdit is unavailable the EDIT fallback is used, where Ctrl+A depends on the common-controls version (v6 manifest is present).
7. **IFileDialog** in a heavily locked-down shell (policies disabling Save As) returns an error; the export or import then silently does nothing beyond the cancelled dialog.
8. **Large imports** go through ONE metadata transaction. It is serialized and blocks other metadata and file operations until done (progress is shown). Cancellation follows the existing transaction-cancel behaviour.
9. **Pre-existing, not changed**: `SubmitMetadataEdit` does not restore optimistic catalog values when storage preflight fails. `SubmitRatingsImport` does restore them.
10. `SOURCE_MANIFEST.sha256`, `SOURCE_CODE.txt` and `codebase_bundle.txt` were already stale before this work (54 mismatching hashes before the FaceLock change set). They were not regenerated here (no script in the tree regenerates them; refresh them before packaging if they are used).
