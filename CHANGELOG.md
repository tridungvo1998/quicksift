## 0.9.6.0

0.9.6.0 collects the maintenance and Single View work done after 0.9.5.0 (rounds 19–23), plus a repository clean-up.

- **Shutdown and lifetime safety:** worker shutdown tokens, join-before-teardown, and a shared log-and-continue helper for `catch (...)`.
- **Diagnostics:** logs and crash files older than 7 days are cleaned up, a previous-session log browser was added, and build failures now print plain-language messages.
- **Single View:** load-stall and lost-result fixes (round 21). A filmstrip audit (round 22) and filmstrip fade, decode dropping, and rating/pick badges (round 23).
- **Thumbnail view:** blurry photos get a red dashed outline.
- **Maintainability:** the large functions were split, data-only state structs were added, layout constants are named, and CODE_GUIDE is the documentation hub.
- **Repository:** the stale historical notes were removed (`notes/`, `PRE_RELEASE_AUDIT.md`, `VALIDATION.md`, `MODULARIZATION_NOTES.md`), as were the generated `SOURCE_MANIFEST.sha256` and `codebase_bundle.*`. A `.gitignore` was added.

### Round 24: Lightroom interop, auto-advance, shortcut sheet

- **Fixed: the "Move selected photos to Deleted?" message was written inline in English and Vietnamese.** It now comes from the translation table, with separate one-photo and several-photo sentences. The file-conflict alert ("… was stopped before any files were changed") and its eight reasons were also untranslated in Vietnamese, and they are now localized too. `check-localization.py` adds an `inline-language` rule: Vietnamese text in `src/ui` or `src/app` outside `localization.cpp` fails the check, with a self-test.
- **RAW: "Load only JPG previews for RAW" is now on by default.** A new `SettingsVersion` session key tells 0.9.5 sessions, which always stored 0, apart from a deliberate 0.9.6 "off".
  - Existing users are switched on once, with a toast saying where to change it.
  - Zooming past Fit (or to 100%) in Single View now decodes that one RAW in full, including deep-zoom tiles. The worker keeps a lock-free per-path override (`Worker::SetFullRawOverridePath`). Returning to Fit, changing photo or leaving Single View goes back to the preview.
- **Lightroom Classic / Capture One compatible rejects.** A reject now writes `xmp:Rating=-1`, and the stars are kept in a new `quicksift:Rating` field so un-rejecting restores them.
  - A `-1` written by those apps reads as a reject, and a photo they un-rejected reads as not rejected.
  - The mapping applies to sidecar, direct Exiv2 and fast-JPEG paths (`core/xmp_interop_policy.h`). Filters, badges, export and Undo use the model values.
  - The new setting **Mark picks with a label** (None by default) adds a chosen colour label to unlabelled photos when they are picked and removes it when they are un-picked. It is part of the same write and the same Undo step. The mapping table is in USER_GUIDE §9.
- **Auto-advance.** The new setting **Advance after rating/pick/reject** is off by default, and **Caps Lock** turns auto-advance on live, with a pill at the top of the canvas.
  - It applies to the keys 1–5, 0, P, X and U, in Single and Thumbnail view, for one photo at a time.
  - A photo hidden by the filters hands over to the photo now in its place. At the last photo QuickSift stays and shows a toast.
- **Keyboard shortcut sheet.** Press `?` to open it, and `?` or Esc to close it, or use Settings → Keyboard shortcuts (?).
  - It is generated from the new shortcut table `core/keyboard_shortcuts.h`, which `OnKeyDown` now dispatches through (same bindings).
  - It is theme- and DPI-aware and EN/VI, and it caches its layouts per language, so it has no per-frame allocations.
- **First-run tip.** A welcome card explains the hover pane, the Pin button and `?`. It is shown once and stored as `FirstRunTipDismissed`, and **Settings → Show the welcome tip again** resets it.
- **Pin the command pane.** A **Pin pane** button at the bottom of the tab column keeps the pane open and reserves its width, so the canvas moves right. It is remembered (`LeftPanePinned`), follows the theme, has a tooltip and reopens after fullscreen.
- **Checks and verifiers.** New `xmp_interop_checks` and `keyboard_shortcut_checks`, and `jpeg_xmp_checks` covers `-1` and `quicksift:Rating`. Both verifiers guard the following:
  - the interop policy in every reader and writer;
  - `OnKeyDown` dispatching through the table;
  - the sheet built from `kShortcuts` without per-frame resource creation;
  - the full-RAW override;
  - the settings migration.
  The new framework primitive `PaintOverlayPanel` draws the overlays. Fixing the `WINDOWPLACEMENT` initializers also lowers the clang-cl warning count from 79 to 37.

### Round 23: filmstrip fade, decode dropping, badges and dashed blur outline

- **Filmstrip fade.** The strip fades in and out when you toggle it (F7 or the button) and when you enter Single View. It uses the central animation system: the Fade preset with the standard 180 ms token, and it is instant when Windows animations are off. Leaving Single View hides the strip at once, so the next entry fades in again.
  - Before, the fade began only on the next idle animation poll, and since round 22 entering Single View didn't fade at all.
  - Hit-testing and the reserved image area follow the on/off state rather than the animated opacity. The photo is laid out once per toggle, and the strip stops taking clicks as soon as it starts fading out.
  - The frame timer runs only while the fade is active.
- **Filmstrip decodes for cells that scrolled out are dropped.** The strip publishes a decode window to the worker: the visible cells plus 2 on each side.
  - Queued filmstrip thumbnail decodes outside it are removed (pending keys released, read-ahead gate re-evaluated), and running ones are cancelled at their next cancellation check.
  - Closing the strip or leaving Single View drops them all.
  - Grid-wanted thumbnails and the main image are never affected.
  - A trace event `filmstrip_decodes_dropped` (verbose, rate-limited) records drops.
- **Rating and Pick/Reject badges on filmstrip thumbnails.** They are painted by the grid's own badge painter, follow the Settings badge options and theme, scale for the cell size and fade with the strip. As in the grid, a value that is still loading shows no badge, and painting never requests metadata.
  - The badge painter no longer allocates per card per frame. Before, it built a vector, star strings and translated captions each time.
- **Blurry photos: red dashed outline.** The face-focus warning on Thumbnail-view cards is now a dashed red outline instead of a solid one.
  - The red is theme-aware: deeper on light, lighter on dark, with at least 4.5:1 and 3:1 contrast against the card surface. High contrast uses the system colour.
  - The stroke style is created once and released on device loss. The dash is measured in stroke widths, so it scales with DPI.
  - Filmstrip thumbnails show the same outline. The tooltip and Help now say "red dashed border".
  - Single View's per-face outlines (Face Lock) are unchanged.
- `core/blur_outline_policy.h` is new and `core/filmstrip_policy.h` is extended. `filmstrip_checks` now runs 82 checks, and both verifiers guard the new wiring.

### Round 22: Single View filmstrip audit

- **Filmstrip thumbnails now appear reliably.** The strip requested 256 px thumbnails, but the grid's thumbnail size is usually different (HiDPI, the larger grid sizes, the memory profile). The worker cancelled these jobs or the UI dropped their results as obsolete, so cells could stay blank. The strip also evicted the grid's own thumbnail of the same photo. It now uses the grid's thumbnail size and shares its cache.
- **The photo is no longer hidden under the strip.** With the filmstrip open, Fit, 100% and panning now use the space left of the strip. Toggling the strip doesn't re-decode the image on screen.
- **The strip handles its own clicks, wheel and double-clicks.**
  - Scrolling over the strip steps through images; it used to zoom the hidden photo. Precision touchpads advance one image per wheel notch.
  - A double-click on a thumbnail no longer also toggles Fit/100% on the image it opened.
  - A click on the strip background below the last thumbnail no longer reaches the image.
- **The toggle button sits left of the open strip** instead of covering a thumbnail.
- **The filmstrip on/off state is remembered**: across Grid and Compare round trips, and between sessions (session key `Filmstrip`).
- **Theme and fades.** The current-image and thumbnail borders use the theme accent and border colours; the fixed white border was invisible on the light theme. Thumbnails now fade in and out with the strip.
- **Performance.** The strip reuses three brushes instead of creating three per frame. Filmstrip decodes share the one-thumbnail limit while the image on screen is still decoding, so fast stepping keeps the disk for the main image.
- **Device loss.** Shell-fallback thumbnails (for files that can't be decoded) are released with the other GPU resources. Before, they were drawn on the new render target after a display-driver reset.
- `core/filmstrip_policy.h` holds the named geometry and rules. The new check program `scripts/checks/filmstrip_checks.cpp` covers them, and both verifiers guard the wiring.

### Round 21: Single View image-load audit (deadlocks, stalls and lost results)

- **Panning or zooming no longer restarts the image load.** Each pan or wheel-zoom step used to cancel the running Single/Compare decode, and a result that finished anyway was thrown away. A slow RAW never sharpened while you kept moving. Whole-image view decodes are now independent of pan/zoom (tiles are unchanged).
- **No endless re-decoding of large RAWs when zoomed.** When a RAW's native size is above the decode cap (for example 9504 px on the 8192 px low-memory profile), or LibRaw had to decode at half size, every repaint (and the 400 ms backstop) started a new full demosaic. Once the exact full-quality result exists it is now final, and the view counts as settled.
- **Cancelled or timed-out full decodes stop at once.** The full-decode path ran its WIC and Shell-thumbnail fallbacks even after cancellation or timeout. The next image waited behind them, and a rating write waited for their file handle. A timed-out decode whose Shell fallback succeeded was also dropped as "cancelled", so the timeout never escalated and the same attempt repeated forever.
- **Fast A → B → A stepping.** A still-running decode of A from before the round trip swallowed the new request for A. A then appeared only after that decode unwound plus the backstop. The new request is now queued straight behind it.
- **Images larger than the GPU texture limit.** Decode sizes are now capped by the Direct2D device's maximum bitmap size. Before, `CreateBitmap` failed silently on every result and the decode repeated forever. Pixels that still cannot be shown now count as a failed attempt in the normal bounded retry policy (logged as `decoded_pixels_rejected`, rate-limited).
- Cancelled jobs re-evaluate the read-ahead gate, which a burst of cancelled foreground jobs could leave paused.
- **Diagnostics.** A Single View stall watchdog logs `single_view_load_stalled` (ViewLane category, rate-limited) when the image on screen has not reached final quality after 5 s, then every 15 s. It records the bitmap, desired, native and cap edges, the retry state, and a view-lane snapshot: queues, the active job and its epoch, adoption, pending keys, view-budget bytes, reservations and waiters, the blocked path and exclusive writes.
- `core/lock_order.h`: `WorkerDesired` moved to rank 44 (after `WorkerQueue`), matching the real `mutex_ → desiredMutex_` nesting (it is a leaf). Documentation only; these mutexes are not annotated.
- New `core/view_load_policy.h` with `scripts/checks/view_load_checks.cpp` (also decode-budget liveness: oversized drain, cancelled waiter, no leaks). Both verifiers guard the fixes.

### Round 20: previous session logs, plain-language build errors, leaner docs

- **Browse previous session logs.** The Diagnostic log window has a new **Previous sessions…** button.
  - It lists the saved session logs in the `Logs` folder, newest first, with start time and size. The current session is left out because the window already shows it.
  - Picking one opens it read-only in its own log window. Only the newest 2 MB of a large log is read, with a note saying so.
  - Listing and reading run on a worker thread (lifetime token, joined at close). Reparse points are skipped and only QuickSift's own log names are opened. Nothing is ever deleted from here.
  - Policy lives in `core/session_log_browser.h` (covered by `lifecycle_checks`), the UI in `src/ui/session_log_browser_controller.cpp`, and file access in `diagnostics::ListPreviousSessionLogs` / `ReadSessionLogTail`. Strings and Help are in English and Vietnamese.
- **Build failures explain themselves.** `build.cmd`, `build-release.cmd`, `build.bat`, `open-in-visual-studio.cmd`, `package-msix.bat` and the scripts they run now end a failure with `Build failed:` (the cause), `What to do:` (1–3 steps) and `Technical detail:` (the short original error).
  - Covered causes include missing Visual Studio, Windows SDK, CMake, Python or PowerShell, signing, a path that is too long, library downloads, compiler errors and verifier findings.
  - Exit codes and successful builds are unchanged. The table is `scripts/build-failure-help.ps1`.
- **Verifier rule relaxed.** The "former monolith functions stay under 160 lines" check is gone from both verifiers. Function size is now a guideline in CODE_GUIDE §16 and TESTING.
- **Docs.** README and CODE_GUIDE open with a documentation map. DEVELOPER_GUIDE is trimmed to a short routing card. This changelog keeps 0.9.5.0 and later in full and summarises older releases.
- Fixed a stale `lifetime_checks` reference (now `lifecycle_checks`) in `core/worker_lifetime.h`.

### Round 19: shutdown safety, log retention and maintainability (no feature changes)

- Fixed a shutdown crash risk.
  - Every background thread now holds a lifetime token (`core/worker_lifetime.h`) instead of a raw `this`.
  - Posts back to the UI thread go through a liveness-checked path.
  - Close and Force quit both stop and join every worker before teardown. `OnMainWindowDestroyed` runs `StopAllWorkersForTeardown` before `StopServicesForShutdown`, and the log shows `stage_app_workers_stop … tokens_left_0`.
- Diagnostic-file retention (`core/log_retention.h`).
  - A few seconds after startup, a low-priority background thread deletes session logs, crash reports and minidumps older than 7 days, by last-write time.
  - It never deletes the current session's log, touches only QuickSift-named files in `Logs` and `CrashReports`, and never follows reparse points.
  - Each deleted file is logged as `old_file_deleted`, followed by a `cleanup_complete` summary.
  - Documented in the Help (EN and VI) and USER_GUIDE.
- Removed the dead `wicImageSources_` cache and every reference to it.
- `LayoutTitleControls` now runs only when its inputs change: width, DPI, fullscreen, view mode, theme or the measured caption widths (language and captions). A dirty flag and an input key track this. Ordinary layout passes skip it.
- `QuickSiftApplicationImpl`'s plain state moved into data-only structs in `src/app/state/`, which it inherits privately:
  - chrome, pane, viewer, browse, metadata, ratings exchange, batch, search and HUD
  - shared types in `app_state_common.h`
  - COM objects stay in the class, so their release order is unchanged.
- Split the eleven largest functions into named helpers with no behaviour change (lines before → after):

  | Function | Lines |
  |---|---|
  | `Worker::Run` | 840 → 120 |
  | `FileOperation` | 416 → 55 |
  | `OnWorkReady` | 410 → 34 |
  | `HandleMainMessage` | 370 → 145 |
  | `DocumentWindowSubclassProcImpl` | 362 → 112 |
  | `LayoutControls` | 334 → 87 |
  | `Worker::Enqueue` | 281 → 27 |
  | `HandleCanvasMessage` | 256 → 75 |
  | `WriteEmbeddedMetadataAtomic` | 235 → 93 |
  | `EvictBitmapCache` | 234 → 42 |
  | `OnCommand` | 227 → 11 |

  - `Worker::Run` is now: wait, then admission (abandon without budget, requeue a blocked metadata job, abandon a cancelled job), then per-kind execution, then publish, then accounting.
  - Each split was checked against the original for identical statements (moved lines only), and the Settings, Info and other pane tabs render identically to the previous build.
- New shared log-and-continue helper for `catch (...)` blocks (`core/log_and_continue.h`, `diagnostics/log_and_continue.h`).
  - `QS_LOG_AND_CONTINUE(category, site)` writes one Warning, `<site> failed and continued: <what()>`.
  - It never throws, even under allocation failure or a throwing sink.
  - Each call site has its own constant-initialised rate limiter: 3 lines per 30 s, with later repeats counted as `(N similar suppressed)`. A hot path that keeps failing costs one atomic operation.
  - Converted all 38 empty `catch (...)` blocks in the Windows app layer: 34 now log, and 4 are marked `QS_CATCH_SILENT_BY_DESIGN("reason")` because the guarded statement is itself a log write. Also added logging to 40 silent fallback handlers (failed pixel-buffer allocations, cache, read-ahead, face detector, batch and ratings workers, search worker).
  - Example: the Windows face-detector capability probe's exception, which used to be invisible, now logs `QueryFaceDetectorCapability failed and continued: …`.
- New layout constants header, `src/ui/ui_layout_constants.h`. It holds named DIP values, unchanged, for:
  - the left-pane Folders, Info, button-flow and Settings sections
  - the status bar
  - the window minimum size (1120 × 520 DIP)
  - the Help, About and Diagnostic-log document windows: the title close light, hit-test border, action row and accent strip

  The constants replace the integer literals in those helpers, and static_asserts encode the relationships between them (for example, the status bar fits its band).
- New verifier rules (py and ps1), each negative-tested:
  - no raw-`this` worker capture
  - the dead cache stays gone
  - the title-layout guard
  - the state-struct split is data-only and registered in CMake
  - the split functions stay at or under 160 lines
  - no empty `catch (...)` in the app layer, and the helper's contract
  - no integer-literal DIPs in the converted layout helpers
- `lifecycle_checks` now also covers the log-and-continue helper (56 checks).
- Build: the clang-cl warning count dropped from 79 to 77.

## 0.9.5.0

Earlier 0.9.5.0 rounds (1–18), most significant first.

- Fixed: the UI froze the first time the left pane was drawn. Root cause: `PaintSidebarTab` drew the tab labels with GDI+ (`Gdiplus::FontFamily`/`Font`/`DrawString`). It was the app's only GDI+ text call. The first GDI+ font made from a family name builds GDI+'s installed-font collection, synchronously on the UI thread, inside the first `UpdateWindow` of the pane reveal. Under Wine the first tab paint took 187–193 ms, and the reveal took 220–230 ms; a standalone probe measured 176–191 ms for the first `Gdiplus::Font` and 0.1 ms for the second. That cost grows with the number of installed fonts. Fix: the rounded tab fill and border stay GDI+. The label is drawn with GDI `DrawTextW`, using a cached, DPI-scaled `HFONT` (`SidebarTabFont`). Under Wine the reveal now takes 33–38 ms, and the slowest child paint is 1–2 ms. The dead GDI+ text helpers (`MeasureVerticalTabTextExtent`, `PaintVerticalTab`) were removed. New verifier rule (py and ps1): no GDI+ font or text API anywhere in `src`.
- Diagnostic log: new `[Pane]` timing events (`core/pane_timing.h`).
  - `pane_open` (first open) or `pane_section_switch`.
  - `pane_reveal`, with per-segment times (`title`, `hide`, `canvas`, `tabs`, `place`, `scroll`, `compose`, `slowest_paint`).
  - `pane_first_paint`: ms from the open request to the first backdrop paint.
  - `pane_frost_capture`.
  - `pane_slow_paint`: the control id of any child paint of 50 ms or more.
  - The first time an event happens, or whenever it is slow (50 ms or more), it is logged at INFO. Otherwise it is logged at TRACE.
- Refactor (no behaviour change):
  - Removed dead code: the never-returned `CloseWithPendingOperations` close decision, its dead branch and its string; `ShowSettingsMenu` and its menu helpers; `ActiveSourceSize`, `BatchApplyActive`, `DrawActiveFlyoutPanel`, `HideAllLeftPaneSectionContent`, `ResetPaneScroll` and `SearchFieldAnimating`; `BlendEditorColor`; an unused `FilesAreByteIdentical` overload; and unused locals.
  - Three duplicated loops over the active section's children are now `ForEachActiveSectionChild`.
  - The two filmstrip-toggle placement blocks in `LayoutControls` are now `PlaceFilmstripToggle`/`SetFilmstripToggleGlyph`, which set the caption only when it changes.
  - The close-at-safe-boundary verifier checks now target the live Force-quit path (`blocking_wait_controller.cpp`).
  - clang-cl warnings dropped from 83 to 79.

- Fixed: a second Language selector was painted below the pane, over the status bar, and stayed there until a theme switch. Root cause: `LayoutControls` called `UpdateLanguageSelectorButton()` right after placing the selector at its natural, unscrolled y. That calls `SetWindowTextW`, and a visible BUTTON repaints synchronously on `WM_SETTEXT`, so the selector was drawn there at once. `RepositionPaneScroll` then moved it with `SWP_NOREDRAW`, which never invalidates the vacated rect. It was the only pane control whose caption was set inside layout. Reproduced with the real app under Wine; the fixed build no longer shows the ghost. Fix: layout no longer touches the caption, and `UpdateLanguageSelectorButton` only calls `SetWindowTextW` when the text changes. Every scroll move and hide, and every child laid out outside the viewport, now invalidates what was under its old rect (parent and status bar, `RDW_ALLCHILDREN`). New verifier rule (py and ps1): no `SetWindowTextW`, `UpdateWindow` or `RDW_UPDATENOW` for pane children inside the layout pass, and the vacated-rect invalidation must stay.
- Fixed: overflow tooltips still did not appear. Tracing the real app (Wine, with a new debug trace) showed that hover tracking, truncation marking, the 450 ms timer and `TTM_TRACKACTIVATE` all ran (`visible=1`, correct screen rect). The weak link was the comctl32 tracking tooltip itself. Its content depends on comctl32's own paint, theme and fade path, `TTM_GETBUBBLESIZE` returned a garbage size, and nothing confirmed it ever drew. The overflow tip is now the framework's own self-painted popup (`QuickSift.OverflowTip`): palette colours, DPI-scaled font and margins, sized from `DrawTextW(DT_CALCRECT)`, shown with `SWP_SHOWWINDOW | SWP_NOACTIVATE`, kept in the monitor work area. It is `WS_EX_TRANSPARENT` and `HTTRANSPARENT`, so it can never take the pointer and fire `WM_MOUSELEAVE` on the button. Every decision is logged at TRACE under `[Tooltip]` (enable Verbose logging): `marked-truncated`, `hover-enter`, `armed`, `timer-fired`, `shown` (rect, visible), `show-skipped` (reason), `hover-leave`, `hidden`, `create-failed` (Win32 error). A standalone hover harness drives the real framework button and tooltip with synthetic input (`scripts/harness/overflow_tooltip_harness.cpp`).
- Fixed: the Settings pane overflowed after toggles became full-width rows. The Language header and selector were drawn below the pane, over the status bar, and stayed there after a tab switch or pane close. Root cause: the left pane had no scrolling. Section children were placed at their natural y with no clip, and hiding them used `SWP_NOREDRAW`, so pixels outside the backdrop were never repainted. Every tab now scrolls when its content overflows: mouse wheel (the pane wins over the canvas under the pointer), a themed overlay scrollbar in the pane gutter, and keyboard focus scrolled into view (framework buttons post `kButtonFocusedMessage`). Children move with the offset, are clipped to the viewport by a window region, and are hidden when fully out of view, so hit-testing matches what is drawn. The offset is kept per tab and re-clamped whenever content changes. Hiding a section child now repaints whatever was under it when it stood outside the backdrop (`core/pane_scroll_policy.h`).
- Fixed: the Performance HUD opened under the search bar. Its drag handle could not be grabbed, and the first line ("PERFORMANCE THUMBNAILS") was hidden. Root cause: the default and the clamp used the whole canvas (`top = 12 DIP`) and ignored overlays. Placement now uses the canvas content area (minus the left-rail inset). Obstacles are the search field plus every visible sibling control over the canvas (search input, filmstrip toggle, pane, tabs). The default sits top-right below them. Any saved, dragged or clamped position that would put the handle or first line under an obstacle is pushed below it, or beside it when there is no room (`hud::PlacePanelAvoiding`).
- Fixed: overflow tooltips for truncated buttons ("Show diagnostic …", "Clear memory ca…", "Help and docum…") never appeared. The round-14 tools were `TTF_SUBCLASS` tools on buttons that already carry the framework hover subclass and are painted through the buffered pane pass. The hover was not reliably relayed to comctl32, and `TTM_ADDTOOL` results went unchecked. The framework now shows one themed, DPI-scaled tracking tooltip from its own hover tracking: about 450 ms after the pointer enters a button with a truncated caption, below the button and flipped on the monitor edge. It hides on leave, click, hide, disable or scroll. Truncation detection is a pure function (`core::caption::Truncated`) covered by `ui_state_checks`, as are the new pane-scroll and HUD-avoidance rules.
- Fixed: RAW rating and picking hung or were very slow. The metadata service thread holds the Exiv2 "write preferred" flag for the whole write. The sidecar writer then read the RAW's embedded values through the same gate, and the gate made every reader wait while that flag was set, so the write waited on itself forever. The writer thread's own reads are now admitted (`core/metadata_gate_policy.h`, `EmbeddedMetadataWriterScope`). Each sidecar write also used to read all sources twice (full Exiv2 parse of the RAW plus the Windows Shell property store, before and after). It now uses the sidecar text for "before", reads the RAW at most once (only when the sidecar does not answer every field), writes every known value into the sidecar so later edits are sidecar-only, and verifies by re-reading only the sidecar (`core/sidecar_write_policy.h`, `sidecar_write_checks`). The UI update stays optimistic.
- Settings → Metadata: the three "XMP vs embedded metadata for …" buttons did change the storage mode, but showed no on/off state. The menu header also said "Use XMP instead…" while ticking the embedded state. They are now switches named "Write metadata into RAW files", "… JPG/PNG files" and "… TIFF files" (highlighted = on, off = XMP sidecar). They show the same confirmation as the old Direct-write options and reload the folder with the new source. JPG and PNG are normalised together.
- Settings toggles get one full-width row each; any button whose caption is cut off shows a tooltip with the full text. All tooltips use the app palette (light/dark), DPI-scaled font and margins, and are re-styled on theme, language and DPI changes.
- Performance HUD: the panel is sized from the measured text (DirectWrite, DPI-aware, padded) so nothing is clipped. It can be dragged by a visible grip strip that moves with it; the rest stays click-through. It is clamped to the window and its position is saved in the session (`HudX`/`HudY`, read before the first paint).
- Build: fixed MSVC C4456 (`width` shadowed a local) in the diagnostic-log checkbox layout. Made two int-to-size_t conversions explicit in canvas rendering.
- Build: `scripts/verify-source.ps1` still required the old "File operations are still running" close confirmation, so the Windows build failed. It now checks the blocking-wait close dialog. It also mirrors the portable verifier's newer rules: theme loaded before window creation, the central relocalize pass, no native checkboxes, ApplyTheme redrawing everything, the popup chevron registry, `HydrateMetadataForFilters` and `FilterValues()` filter menus, the camera-settings cache columns, and all 20 check programs.
- Close, ratings import/export and Batch Processing never block silently. A live dialog (updated every 0.5 s) names the operation, the file, how many are left, the elapsed time and the reason: file locked by another app, write retry, metadata write, file operation, ratings worker, batch worker or unsaved edits. Close offers **Force quit**, which cancels workers, flushes the session and cache, and logs what was abandoned. The others offer **Cancel operation** or **Stop waiting**. Waits are logged under `BlockingWait` (`core/blocking_wait.h`).
- The Performance HUD shows decode queues by priority, view/prefetch, metadata, face and write queues, in-flight jobs, pending metadata/rating/pick counts, the oldest waiting job age, cache MiB and disk hit rate, the last RAW and JPEG decode times, worker states and the current blocking reason. Catalog counts are refreshed at most once a second (`core/hud_snapshot.h`).
- Diagnostic log: Refresh scrolls to the newest entry, and a new **Auto refresh** checkbox (off by default) refreshes once a second, only when the text changed. Checkboxes are custom-drawn (`PaintCheckbox`): theme- and DPI-aware with hover and focus states. Native Win32 checkboxes are rejected by the verifier.
- Language switch: one central pass (`RefreshLocalizedText`) now also refreshes the colour/pick/rating filter, fullscreen and zoom buttons, the comments pane, settings headers, HUD and an open blocking-wait dialog, then re-lays out. The Comments **Save** and **Remove from library** buttons are localized. The verifier requires every `Update*Button` in that pass.
- Theme switch redraws everything, including the closed left-rail indicator (`flyoutBackdrop_` was never invalidated) and all child windows.
- Light theme at startup: theme and language are read from the session before any window class, brush or window is created, so the title bar and controls start in the right theme with no dark flash. The Theme setting is now saved immediately.

- Fixed: "Metadata pending", "Rating pending" and "Pick state pending" could stay forever.
  - The metadata lane started no read while ANY visual job ran or was queued, including Idle thumbnail prefetch and background face analysis, so large folders starved it. Only foreground decodes now make it yield, and an aging rule (`core/metadata_lane_policy.h`, 750 ms; 10 s cap behind pending writes) guarantees progress.
  - Predictive metadata reads (visible-first reads, and all reads while a rating/pick/label filter is on) were cancelled by the speculative-decode throttle; the empty Unknown result overwrote the photo and was never queued again. Metadata/EXIF reads are exempt from that throttle, and a cancelled, failed or still-unknown result is retried (at most 3 attempts) and then marked Unreadable (`core/metadata_progress.h`).
  - A file that cannot be opened is Unreadable, not Unknown (`ResolveMetadataReadOutcome`).
  - "Clear cache" dropped queued metadata reads without releasing their dedupe keys or UI slots; they are now kept.
  - `metadata_progress_checks` simulates busy decode, stuck writes, cancellations and unreadable files and checks that pending reaches 0 exactly once per job.
- Search accepts format tokens (`NEF`, `ARW`, `CR2`, `JPG`), relative dates (`older than 7 days`, `newer than 2 months`, `today`, `cũ hơn 7 ngày`, `hôm nay`), and combinations. `not` / `-` / `không` excludes the next clause; `+` is explicit AND.
- Format and Date filter menus list the extensions and capture dates actually present in the folder.
- Toggling “Load only JPG previews for RAW” while in Single View reloads the current image with the embedded JPEG instead of leaving a blank pane.
- Single View RAW decode is prioritized over background indexing and thumbnail work.
- Fixed: with "Load only JPG previews for RAW" on, Single View did not show the largest embedded JPEG. The LibRaw thumbnail was decoded at the grid size and rejected by a 1/3 rule, and the fallback scanner stopped at the first SOS marker so it never accepted any JPEG. New `core/embedded_jpeg_scan` walks entropy data to EOI (FF00/RSTn aware, skips nested APP1 thumbnails, ignores lossless SOF3) and the largest frame is decoded at full quality; LibRaw's thumbnail at native size is the fallback (`raw_preview_checks`).
- Fixed: RAW decodes in Single/Compare were choppy and sometimes stuck. Only Single-view Full-class requests got the foreground timeout; Compare decodes, Preview-class view decodes and the active RAW's full demosaic were cancelled after 1.5 s and retried at 3 s/6 s. All non-prefetch view-lane decodes and the active RAW demosaic now get the foreground budget (>= 12 s for RAW). Policy in `core/decode_timeout_policy.h`.
- Fixed: Labels, ISO, Aperture, Focal, Shutter, Camera and Hour filter buttons had no dropdown chevron or chevron width reserve. A hardcoded switch is replaced by `kPopupMenuButtonIds`; `verify-source-portable.py` fails if a `TrackDropdownMenu` anchor is missing from it.
- Info-tab metadata for the selected file is prioritized above background work, below the active view decode.
- Filter menus (Date, ISO, Aperture, Focal, Shutter, Camera, Hour) no longer read files or the cache on the UI thread: `HydrateMetadataForFilters` queues capture/camera reads on the background search worker (P3, yields to decodes and user work, cancelled on folder change) and active filters re-apply as facts arrive. Merge rule in `core/hydrated_facts.h` (`hydrated_facts_checks`).
- Metadata indexing: JPEG XMP fast path (no Exiv2 parse for ordinary JPEGs), one header open for capture time + camera, batched stamp-validated camera-index lookups. Fixes: persisting a record wiped the camera index; camera index ignored file changes (stale values); NUL-padded EXIF text; LONG ISO; duplicated make in camera names; JPEG scan data searched for TIFF magic.
- Visible-first rating indexing: with no rating/pick/label filter, the pending metadata queue is stably reordered so on-screen and nearby photos dispatch first at Predictive and the rest stay Idle; such a filter (or rating sort) dispatches everything at Predictive (`core/metadata_queue_order.h`). Pending counters are unchanged (reorder only); an out-of-range queue entry now decrements the pending counter.
- Filter menus read `CatalogStore::FilterValues()`, a reference-counted distinct-value index maintained on append/edit/clear (rebuilt only if an incremental update failed), instead of scanning the catalog on every open (`core/filter_value_index.h`, `filter_index_and_queue_checks`).
- Face detector: kept as a lazily created per-worker-thread instance; sharing one WinRT `FaceDetector` across threads is not documented as safe for concurrent `DetectFacesAsync`, and a mutex would serialize face analysis.
- **Fix: "0 stars" missed unrated photos** (55-photo folder: 39 results). Photos with no rating in any source that could be read (no sidecar, no XMP that Exiv2 could parse, no Windows rating property) kept rating knowledge *Unknown* forever, were displayed as unrated, but never matched `0 stars`, `unrated` or the Unrated filter. `ReadPhotoMetadata` now resolves such fields to unset (0 stars / no label / unmarked) when the file opens (`ResolveUnsetMetadataAsNone`); records cached as Unknown are re-read once. Unreadable and custom values are unchanged. A rating/pick/label search now also raises rating indexing to Predictive and shows "…" while ratings are pending.
- Search: rating ranges in English and Vietnamese (`at least 3 stars`, `3 stars or more`, `5 stars or less`, `under 3 stars`, `more than 2 stars`, `3 sao trở lên`, `5 sao trở xuống`, `tối đa 4 sao`, `không quá 4 sao`, `dưới 5 sao`, `trên 2 sao`, … with or without diacritics) and spaced symbols (`≥ 3 stars`). The star unit is required; existing forms (`9/10`, plain numbers as text) are unchanged.
- Import/export/batch: a locked file is skipped instead of cancelling the batch; partial folder listings are reported as errors; zero-byte write errors no longer show "success (0)".
- Browsing: thumbnail painting is virtualized to visible rows, badge brushes are created once per render target, deep-zoom tiles are no longer starved by the per-frame schedule limit.


## 0.9.4.0

Summary; the full 0.9.4.0 notes are in the 0.9.4.0 source release.

- **Search and metadata:** an indexed cache of camera settings makes search fast. The catalog record is the single authoritative metadata model, and Filters and Search share one predicate engine. Metadata writes that are still blocked after 15 s are cancelled, so they never lock close, import or export.
- **New features:** Import / Export Ratings, Batch Processing, the Thumbnail view search box (Ctrl+F), and FaceLock with several faces, click-to-frame, per-face quality and auto-hiding outlines.
- **Responsiveness:** the UI thread never decodes. Single and Compare use a dedicated view-decode worker, and a P0–P5 priority model is in place. Decode retry and timeout policy was reworked, two permanent-blank thumbnail bugs were fixed, and decoding no longer blocks shutdown.
- **UI:** a central animation system, a design-token system with accent-aware palettes, anti-aliased drawing, and fixes for left-pane flicker and frost.
- **Quality:** a localization checker (`scripts/check-localization.py`) and a broad polishing pass over logging, priorities and Vietnamese text.

## Earlier releases

The 0.9 line (0.9 and 0.9 R4) introduced compiled module boundaries, serialized metadata transactions, the central macOS-style UI framework, diagnostics, and the Visual Studio build entry points. It also added media-unit-atomic Copy/Move/Delete with live progress and the "Load only JPG previews for RAW" setting. The detailed per-fix diary of that period (left-pane, thumbnail-scroll, filmstrip, logging and decode-retry entries) was removed in 0.9.5.0 round 20; it describes code that has since been replaced. Those notes ship with the 0.9.4.0 and older source releases.
