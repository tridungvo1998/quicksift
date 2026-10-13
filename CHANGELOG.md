## 0.9.4.0

### Indexed camera settings for fast search

- Search no longer re-reads metadata for focal length, ISO, aperture, shutter speed, camera, or lens.
- Schema v3 of the persistent cache stores these fields. `LookupCameraSettings` / `UpdateCameraSettings` are stamp-independent and never overwrite rating, faces, or dimensions.
- Search query language accepts bare forms `f/4`, `50mm`, `1/125` in addition to `iso:400`, `f:2.8`, `focal:50`, `shutter:1/125`, `camera:nikon`, `lens:24-70`.
- Left-pane Filter tab adds ISO, Aperture, Focal length, Shutter, Camera, and Hour-of-day dropdowns. Options are the distinct indexed values present in the current folder (e.g. 50mm / 70mm). Camera uses the full model name.
- The lightweight EXIF reader extracts the standard tags in the same bounded prefix read used for capture time.

### Metadata write hard timeout

- Writes that remain blocked after 15 s (`kHardTimeout`) are cancelled, cancellation is signalled, and the exclusive path gate is released so import, export, batch operations, and program close are never locked indefinitely.

### Shutdown no longer blocked by decoding

- `Worker::RequestStop()` clears decode queues and stops budgets without joining.
- On main-window destruction it is called immediately, before search/scanner cancel. Only metadata writes (subject to the hard timeout) may delay close.

Version bump: `src/version.h` (0.9.4.0 RC / file 0.9.4.0 / package 0.9.4-rc.0), `app.manifest`, `packaging/msix/AppxManifest.xml.in`, `vcpkg.json`, README, BUILDING, THIRD_PARTY_NOTICES, PRE_RELEASE_AUDIT, PRE_RELEASE_CHECKLIST, `scripts/build.ps1` example. CMake, the resource script, the verifiers and MSIX packaging derive from `version.h`.

### Bug fixes: left pane flicker + frost, search field blocks, toast wrapping

- **Left pane transparent / flickering.** The backdrop was positioned with `hWndInsertAfter = canvas_`, which put it *behind* the canvas, so the pane never had a surface. Every content repaint showed thumbnails between its children. It now sits directly above the canvas and the search input (`LeftPaneBackdropInsertAfter`).
  - **Flicker sources removed:**
    - `LayoutControls` hid and re-showed every pane child on each call, which also reset button hover. Now only inactive sections are hidden (`HideLeftPaneSectionContentExcept`).
    - The tab switch did a forced solid erase plus `RDW_UPDATENOW` before the frost paint. It is now invalidate-only, followed by one buffered compose.
    - The backdrop painted a solid colour in `WM_ERASEBKGND`; it no longer does.
    - Owner-drawn pane surfaces (tabs, buttons, section labels, info panel) painted in several unbuffered layers on every hover-fade frame. Each now paints in one off-screen pass over its slice of the pane material (`PaintItemBuffered`), then one BitBlt.
- **Frosted left pane.** The pane's background is now a cached, blurred snapshot of the canvas under the open pane, tinted with panelStrong (86 % dark / 82 % light), and it covers the whole pane.
  - Built by the portable `ui_frost` module: downscale ÷4, then 2 box-blur passes, tint, and bilinear upscale. Kept as one DIB in the framework.
  - Refreshed only when the pane opens, the theme changes, or the canvas under the pane changes, with at most one refresh every 450 ms. It is never refreshed during scroll motion. The pane is repainted only when the frost signature changes.
  - Falls back to opaque panelStrong when transparency is off or high contrast is on.
- **Library list.** Removed the native `WS_VSCROLL` bar, which was the white strip along the right edge. It is replaced by the framework overlay scrollbar, with drag, wheel and keyboard scrolling.
  - The list paints in one buffered pass: frost, panel, rows via `WM_PRINTCLIENT`, scrollbar, border.
  - `WM_CTLCOLORLISTBOX` returns the panel brush, and `LBS_NOINTEGRALHEIGHT` is set.
- **Search field blocks.** The glass sheen covered the field's top half, but the native input child clears with the flat fill, so a rectangle showed. The field body is now flat (`GlassSurfacePresentation::sheen`).
  - The input child is shown only once the field is fully opaque, so there is no opaque child over a half-faded field.
  - The ambient shadow uses 4 soft rounded rings instead of 2 hard steps.
  - The key shadow is drawn only under opaque bodies; under translucent glass it showed through as a displaced block.
- **Toast wrapping.** `PaintToast` measured the text at the max width, then re-wrapped it at the pixel-snapped body width. That width could be a fraction narrower, so the widest line wrapped again and the extra line hung outside the glass.
  - One DirectWrite layout is now measured and drawn as-is, centred. Explicit line breaks are normalised (`NormalizeOverlayText`).
  - The body size comes from the portable `ComputeToastGeometry`, rounded outward and capped to the window.
  - Alerts grow with long text up to the window height (`ComputeAlertCardGeometry`) instead of clipping at 340 DIP.
- **Verification.**
  - New check program `scripts/checks/overlay_and_frost_checks.cpp` (89 checks).
  - New verifier rule `verify_pane_frost_and_overlay_contracts`.

### Animation system (centralized motion)

- New portable `src/ui/framework/ui_animation.{h,cpp}`: one `Animator` registry keyed by element id, presets `Fade`, `SlideFade`, `DropDownReveal`, `PopScale`, `Crossfade`, `Toggle`, token durations 120/180/250 ms, curves ease-out cubic / ease-in-out cubic / smoothstep / spring-lite. Interruptible and reversible without jumps (proportional trips), ordered completion callbacks, batched per-surface invalidation, reduced-motion instant path, suspension while minimized, safe teardown.
- Driven by the existing `ID_TIMER_ANIMATION` only: 16 ms while anything animates (an activation hook switches cadence immediately), 33 ms idle. Retired the toast timer (`ID_TIMER_TOAST`) and the per-button hover timer.
- Now animated: dropdown menus (DropDownReveal open, Fade close via constant layered alpha, input-transparent while fading), toasts (SlideFade up 8 DIP, exit lands as the toast expires), alerts incl. ratings chooser (PopScale in, fade out with the last content), search field + count (Fade), filmstrip (Fade 180), button hover (fades in and out), thumbnail placeholder→image (Crossfade, skipped during scroll motion), face outlines (central curve + reduced motion). Left pane and sidebar tabs moved onto the Animator with identical maths.
- Verifier: `verify_animation_contracts` (no animation timers or ad-hoc tweening in feature code, framework timer allowlist, CMake/check registration). New check program `scripts/checks/animation_checks.cpp` (78 checks).

### UI polish (macOS / glass design system)

- **Design system:** new `src/ui/ui_design_tokens.h` (radii 6/10/14/20 + capsule + concentric, spacing 4/8/12/16/24, strokes, type scale with Segoe UI Variable -> Segoe UI fallback, motion 120/180/250 ms, glass material, accessibility policy). `ui_layout_metrics.h` maps every component radius onto a token; documented in `docs/DESIGN_SYSTEM.md`; `design_tokens_checks` (50 checks) and a new verifier section enforce it.
- **Colour:** palettes take the system accent (contrast-corrected to 4.5:1 for white text in light mode, 3:1 against panels in dark mode); new `accentText` / `menuSelectedText` roles replace hard-coded white on accent.
- **Glass:** one `PaintGlassSurface` recipe (ambient + key shadow, body, clipped sheen, 1 px top highlight, half-inset hairline, device-pixel snapping) for toasts, alerts and the floating search field. No per-frame blur.
- **Chrome:** `ApplyWindowChrome` is the single owner of DWM attributes (main, fullscreen, document, dialog, popup); Mica only on document/dialog windows while transparency is allowed; fullscreen is unrounded.
- **Controls:** subtle press (1 DIP inset + tint, no accent flash), pill buttons in alerts, capsule scrollbars and badges (measured width), rounded inset menu selection, consistent 1.5 DIP icon strokes, macOS-style focus rings that are never clipped, 120 ms eased hover with a timer only while animating.
- **Flicker / shape fixes:** status bar region/paint radius mismatch and black corner specks; document window double paint (now one buffered paint, `WS_CLIPCHILDREN`); square rich-edit corners poking through the document panel; left pane region vs. paint radius drift while animating; square thumbnail placeholders; un-centred toast text; off-centre badges; empty Library list painting unlocalized text over a stale background; theme switch flushing children several times (now one `RDW_UPDATENOW`).
- **Behaviour fixes:** stuck hover on buttons hidden/disabled/losing capture; popup items highlighting from pointer moves outside the menu; layout work while minimized; DPI change applying geometry before fonts; focus lost after Alt+Tab; caret blink ignoring the system rate; document windows ignoring `WM_DPICHANGED` and the inactive title state; diagnostic export messages and several settings section titles not localized (9 new EN/VI strings).
- **Motion:** filmstrip, pane, tab and search-field animations are duration-based (`StepToward`) and land exactly; the animation timer runs at 16 ms only while something animates, 33 ms otherwise.
- **Accessibility:** high contrast uses system colours and disables glass, shadows and blobs; `SPI_GETCLIENTAREAANIMATION` off snaps every animation; transparency off makes glass opaque and disables Mica. Changes apply live (`WM_SETTINGCHANGE`, `WM_THEMECHANGED`, `WM_SYSCOLORCHANGE`, `WM_DWMCOLORIZATIONCOLORCHANGED`).

### Polishing pass (localization, bugs, priority, logging)

- **Localization:** new `scripts/check-localization.py` (in `verify-source-portable.py`) reports missing Vietnamese text, English passed to pre-localized sinks, untranslated entries, placeholder mismatches and concatenated keys. Its first run found 196 missing keys plus 6 untranslated status tags; about 211 Vietnamese entries were added and the checker now reports nothing. `Localizer` matches exact keys through a hash index and applies fragments longest-first (fixes "Batch processing cancelled" being half-translated). Help gained Search, Saving ratings (write lane) and Import/Export sections in both languages. The ratings export format stays English by design. New `localization_checks` (80 checks).
- **Priority model P0–P5** (`core/priority_policy`): demand signal, cooperative yields, pool capped to one thumbnail decode while Single/Compare decode is in flight, face/analysis paused during search, adaptive search hydration batches, OS priority hints (metadata writer NORMAL, ratings/batch workers BELOW_NORMAL, background lanes background mode).
- **Write lane:** interactive edits coalesce while a write runs (one transaction, one Undo step); locked files retry 50–800 ms for ~3 s, then up to 30 s with a "waiting" toast, then Retry / Skip; only conflicts, low storage, read-only and verification failures stop a write, and they restore the previous values with a localized message. Waits are interruptible; the UI never blocks.
- **UI-thread I/O removed:** post-write stamps and the storage preflight moved to the metadata worker; cache-database calls from the UI wait at most 30 ms, then defer (writes) or retry from a timer (comment panel, library list); the logger no longer writes to disk on the calling thread or under the producer lock.
- **Bug fixes:** refused edits left optimistic values on screen; rapid edits during a write got a modal refusal; search wake post failures could stall results; lost ratings/batch completion posts; sharing violations misclassified; search hydration opened files gated for writing; uninterruptible retry waits; `HWND` race in completion sinks; full catalog snapshot copied per epoch; fragment-order mistranslation; untranslated "All" and status tags; logging inside `noexcept` worker catch blocks could throw.
- **Logging:** new categories (Scheduler, WriteLane, Search, RatingsIO, Batch, FaceLock, ViewLane, ThumbReliability, MetadataStore, DbMigration), structured key=value events with file name + `path_id` instead of full paths, rate limiting for hot paths (verbose-only traces), `UserErrorCode` catalog mapped to localized messages, crash reports include a subsystem-state section.
- **Lock order:** documented in ARCHITECTURE.md; `core/lock_order.h` debug checker (`QS_DEBUG_LOCK_ORDER`) with `lock_order_checks`.

### Thumbnail View: UI thread decoupled from image workers

- Workers still decode to upload-ready premultiplied BGRA; the UI thread only does the D2D `CreateBitmap` upload and cache insert.
- Completions flow through the bounded (8192) short-lock `BackgroundCompletionQueue`. `TryPopNext` is now a single pass (was O(n^2) under the queue lock); pop order is unchanged (verified against the previous implementation on randomized sequences).
- Coalesced wake: producers (decode workers, metadata aborts, folder scanner) post `WM_APP_BACKGROUND_COMPLETION` only after `TryClaimWake()` succeeds, so at most one wake message is ever outstanding. The UI releases the claim at the start of each drain slice; failed posts release it.
- Time-budgeted drain: each slice is capped at 48 items / 4 ms (1 ms / 4 items during scrollbar drag); policy budget lowered from 256 / 12 ms.
- Paint-friendly continuation: when work remains and input or paint is pending (`GetQueueStatus`), the continuation is a one-shot `ID_TIMER_COMPLETION_PUMP` timer instead of a posted message, so a large burst can no longer starve `WM_PAINT` and mouse/keyboard input (posted messages outrank both).
- Paint never waits on I/O: the synchronous Shell `IShellItemImageFactory::GetImage` fallback is no longer called for not-yet-decoded grid/filmstrip cells (only for paths whose decode failed), and at most one uncached Shell fallback is built per frame; others show a placeholder and repaint next frame.
- Fling/coast/settle policy and the size-aware admission caps are unchanged.

### Performance

- No UI-thread `file_size` stat per enqueue: preview/full requests get a provisional adaptive timeout and the worker refines it from the real file size just before execution.
- Viewport prefetch residency probe fixed: it compared raw paths against normalized cache keys, so cached thumbnails were re-requested after every settle. Now an O(1) `bitmapCache_` probe.
- In-flight thumbnail paths are collected in one worker-lock pass (`PendingOrActiveThumbnailPaths`) instead of a per-request prefix scan of every pending key.
- Bitmap cache bookkeeping uses the path->size index: O(1) `UnindexBitmapSize`, index-driven `PruneSupersededViewBitmaps`, stale-thumbnail removal and reclaimable-bytes estimate; thumbnail inserts skip the superseded-view prune; `ClearBitmapCache` clears the index too.
- `FindBitmap` / `FindBitmapNoTouch` return immediately for paths with no indexed bitmap (previously 24 key copies and hash lookups per empty card per frame) and skip unindexed size buckets.
- Thumbnail results in Thumbnail view no longer enqueue Face jobs that the worker would always cancel.
- `SameDecodeFamily` (evaluated under the worker lock for each queued job) runs cheap comparisons first and lower-cases extensions only when needed.
- WIC file-identity re-verification for the Single/Compare image source is throttled to once per 500 ms instead of a file open on every paint. (Superseded below: Single/Compare paint no longer uses the WIC image source at all.)
- `core/performance_policy.cpp` guards its `windows.h` use with `_WIN32` so the documented portable core build compiles again.

### Single View: dedicated view-decode worker

- Single and Compare full-size, intermediate and embedded-preview decodes run on a new dedicated **view lane**: one worker thread with its own queues and its own decode budget. The thumbnail pool never runs view decodes and the view lane never runs thumbnails, so neither can starve the other.
- The UI thread never decodes. The former `ID2D1ImageSource` path (`CACHE_ON_DEMAND`) decoded JPEG/PNG/HEIC on the UI thread inside `DrawImage` and opened the file during paint; it is no longer used by `RenderSingle` / `RenderCompare`. Until the worker result lands, paint shows the best cached preview or a scaled-up thumbnail. The completion is a GPU upload only, and view-lane completions are drained before queued thumbnail results.
- Latest request wins. Foreground view requests carry the navigation epoch. `SetNavigationEpoch` drops queued older requests and `IsCancelled` aborts the in-flight one, so rapid arrow-key navigation never builds a backlog.
- Neighbour prefetch (two ahead and one behind in the travel direction) runs on the same lane as low-priority work. It yields to any queued foreground request and is cancelled when it leaves the published prefetch set. If the user navigates onto an image whose prefetch is already running, the foreground request **adopts** it instead of starting a duplicate decode. The former Predictive-priority neighbour prefetch was always cancelled in Single View by the interactive-focus filter, so it never produced a bitmap.
- Compare panes are foreground view-lane requests, focus pane first.
- Clean shutdown: `Worker::Stop()` clears the view queues, stops the view budget and joins the view thread. This happens on `WM_DESTROY` after `SetNotify(nullptr)`, so nothing posts to a destroyed window.

### Thumbnail View: no image is ever skipped or left blank

- Fixed a **permanent-blank** bug. A job cancelled at dequeue (stale viewport, fling, generation) or while waiting for the decode budget never released its in-flight family. Its path then looked "in flight" forever and `Enqueue` rejected every new request for it. All exits now call `ReleaseActiveVisualFamily`.
- Fixed a second permanent-blank bug. After the grid was shrunk, stale larger desired targets made every new, smaller thumbnail request "superseded". Thumbnail desired targets above the active thumbnail target are now ignored.
- Settle debounce: if input arrived during the debounce, the settle handler returned without re-arming. Visible cells cancelled during the fling were then never requested again. It now re-arms.
- Post-settle **reconciliation sweep** (`ReconcileVisibleThumbnails`): visible rows ±1 row with no cached bitmap and no pending or in-flight job are re-enqueued as Interactive with the current viewport epoch. It republishes the viewport first if any cell is missing from the worker's mirror (filter/sort/resize).
- **Backstop timer** `ID_TIMER_THUMBNAIL_RECONCILE` (400 ms) runs while any visible cell is still missing and stops when none are. Paint arms it for every placeholder cell, including during flings; while motion continues it only defers.
- Visible cells are pinned against eviction in the fling paint path too, not only in the normal card path. Any eviction that reclaims thumbnail bytes arms the sweep.
- `FindBitmap` / `FindBitmapNoTouch` iterate the path's indexed sizes, so entries stored at non-bucket sizes are found.
- Thumbnail budget floor per thumbnail size: the cache holds at least the visible window, the margin rows and one window of predictive rows at the current decode size (capped at 3/4 of the budget). Large and Huge grids on HiDPI no longer admit and immediately evict predictive results.

### Decode retry and timeout policy

- Up to 3 attempts per path, with worker timeouts escalating base → 2x → 4x (the old ladder repeated the same timeout on the first retry) and backoff of 150 ms, then 600 ms.
- The worker classifies failures (`DecodeFailureKind`):
  - transient: sharing or lock violation, network or device error, zero-length file still being written;
  - timeout;
  - missing;
  - deterministic decode failure.

  Transient failures and timeouts use all attempts; the last attempt switches to the WIC → Shell thumbnail fallback chain. Deterministic failures go straight to the fallback attempt. Missing files are terminal immediately.
- Per-path attempt count, last failure and file stamp are tracked. The failed placeholder ("Couldn't load image", localized) appears only after the final attempt.
- Recovery from a terminal failure is automatic when the file's size or modification time changes, on folder reload, and after 30 s for transient terminal failures while the image is visible. Manual retry: **F5** or **Ctrl+Shift+R**.
- A timed-out decode always releases its pending key and in-flight family, so it never blocks re-requests.
- Single and Compare use the same 3-attempt policy. A full decode that could only produce a Shell preview is remembered as final, so it is not re-run on every paint.
- The filmstrip no longer clears failure marks on every paint, which used to re-request undecodable files without bound.
- Fixed a misordered `EnqueueFull` argument list for RAW full decodes: `isSingleView` was being passed as `forceWic`.

### FaceLock: multiple faces, click-to-frame, per-face quality

- **Default target** with several faces is the face whose centre is closest to the dead centre of the image (distance in pixel-proportional units), replacing largest-first. Ties: larger area, then top, then left (quantized, deterministic). Faces whose linear size is under 20% of the largest are kept but ordered after every larger face, so a tiny background face at the centre cannot steal the target. One face: that face. (`core/face_policy.*`: `RankFacesForAnchor`, `FaceAnchorComesBefore`.)
- **Noise filter** at detection: a box is dropped only if its short edge is below BOTH 8 px in the 512 px detection bitmap and 1.5% of the image short edge.
- **Outlines**: with FaceLock on, every detected face gets a very faint rounded outline in Single and Compare, drawn in image space under the same transform as the bitmap (follows zoom, pan, rotation). The FaceLock target is marginally stronger; a flagged face is tinted faint red. Painted through the central UI framework (`PaintFaceOutline`).
- **Click-to-frame**: a click (press and release within 4 DIP, so dragging still pans) inside a face's slightly inflated outline makes it the FaceLock target and zooms so the face fills ~70% of the pane, centred, limited by the 32x-Fit maximum zoom (`transform::FrameSourceRect`). In Compare it applies to the clicked pane; with Sync on the shared zoom follows that framing and the other panes stay centred on their own targets; with Sync off only the clicked pane changes. A double-click right after a face click no longer toggles Fit/100%.
- **Selection persistence**: the chosen face is stored per image in the cache DB as a normalized box (never an index), keyed by path + fingerprint, written off the UI thread through the existing record-cache queue. On load it is matched to the re-detected faces by IoU (>= 0.30) or nearest centre (<= 0.6 face sizes, size ratio <= 2); otherwise the default target is used. A changed file fingerprint drops it.
- **Red border** now means ANY face is out of focus, motion-blurred, or has closed eyes. Each face is analysed on the worker (`core/face_quality_policy.*`): contrast-normalized, noise-corrected Laplacian detail on a resampled face crop (more sensitive than the old primary-face test: threshold 6.0 vs ~4.6), directional-detail anisotropy for motion blur, and a conservative sclera-visibility heuristic for closed eyes. Faces too small in the detection preview are re-judged once on a larger decode (1536 up to 2048 px); faces still under 64 px are never judged. All thresholds live in `FaceQualityThresholds`.
- **Reasons**: the red-border tooltip lists per-face causes (e.g. "Face 2: eyes closed; Face 1: motion blur", faces numbered left to right), and the status bar shows the same reason for the active photo in Single/Compare.
- **Detector input**: the detection bitmap is converted to Gray8 on the worker (the detector's native format) when supported; any failure falls back permanently, for the process, to the previous Bgra8 path.
- **Wheel zoom pivot** with FaceLock uses the face's true on-screen position (`transform::SourceToScreen`), fixing drift with letterboxing, pan, or rotation. The view jump to the face when results arrive is unchanged (intended).
- **Cache schema v2**: `image_records` gains `face_quality` (per-face 16-byte records), `face_quality_version`, and `face_selection`. v1 databases are migrated in place (`ALTER TABLE` inside a versioned `MigrateSchema` step); other versions still rebuild. Verdicts from older analysis versions read back as "not analysed" and are re-analysed lazily when the photo is next needed. Verifiers updated to the versioned-migration contract.
- Portable deterministic checks: `scripts/checks/face_policy_checks.cpp` (49 checks; no CTest).

### Anti-aliased UI

- **D2D canvas**: every frame sets `D2D1_ANTIALIAS_MODE_PER_PRIMITIVE` and `D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE` explicitly (grayscale is correct over the composited, possibly translucent canvas).
- **GDI → GDI+**: `ui_design_system` `FillRoundedRect`, the section-title panel, status icons (circle + check), the popup-menu checkmark and the glass buttons now draw through GDI+ with `SmoothingModeAntiAlias` + `PixelOffsetModeHalf` (the framework helpers already did). Vertical tab text uses `TextRenderingHintAntiAlias` after rotation (ClearType is invalid on rotated text).
- **Window regions** (the source of jagged rounded corners): buttons, the canvas, and document-window buttons use the new `MacOsUiFramework::ApplyAntialiasedControlRegion`, a region with a radius smaller by up to 3 px; the painter fills the slack with the exact parent surface (`ButtonPresentation::parentSurface`; the title bar's colour comes from `TitleBarSurfaceColor`, document windows sample their own gradient) and draws the real AA rounded shape inside. The swap-chain canvas cannot paint outside its region, so `PaintRoundedCornerUnderlay` draws the true AA corner arc in the four corners, coloured from the frosted backdrop pixels under each corner.
- **Popup menus**: on Windows 11 they ask DWM for rounded corners (`DWMWA_WINDOW_CORNER_PREFERENCE`, border colour from the theme) and skip the hand-drawn rounded border; Windows 10 keeps the previous region path.
- The left-pane flyout tabs and the status/EXIF/backdrop panels keep exact regions (their underlay is a live frosted bitmap, not a flat colour); see the notes file.
- No new invalidations: all changes are within existing paint passes (no flicker or underlay change).

### Face outlines: auto-hide

- With FaceLock on, outlines in Single and Compare show fully for 1 s, then fade out over 250 ms (`review/face_interaction_policy.*`: `FaceOutlineFade`). They re-appear when the photo changes, when face results arrive, when the lock target changes (face click or click-to-zoom revert), and when FaceLock is turned on (outlines appear again). Hit boxes stay clickable while faded.
- **Right-click** on the image pane toggles "always shown" vs auto-hide (persistent: saved in the session file as `FaceOutlinesPinned`; a toast confirms). Only while FaceLock is on, in Single/Compare, and never while a glass alert is open. Right-click previously did nothing on the canvas.

### Face click: zoom toggle and robust click detection

- Clicking the same face again after the ~70% face zoom restores the zoom and pan you had before; the face stays the FaceLock target (and stays persisted). Clicking a different face while zoomed reframes but keeps the ORIGINAL pre-zoom view as the restore target.
- Compare: per pane with Sync off; with Sync on there is one shared toggle (the shared view is what was zoomed).
- The restore target is forgotten after a meaningful manual zoom (wheel, keys, toolbar), a pan, navigation, rotation, view reset, pane resize, or FaceLock/Sync toggles.
- Click vs drag: the press captures the mouse; the drag threshold is max(`SM_CXDRAG`/`SM_CYDRAG` at the window DPI, 4 DIP). A drag is never a click; releasing outside the face's hit box is not a click; `WM_CAPTURECHANGED` or Escape cancels the press; a double-click never toggles twice (the second press is ignored and the Fit/100% double-click toggle stays suppressed after a face click).
- Portable checks: `scripts/checks/face_interaction_checks.cpp` (58 checks).

### Import / Export Ratings

- New **File tab → Import / Export Ratings** button. A themed chooser offers **Export…** / **Import…** / Cancel.
- **Export**: `IFileSaveDialog` with default name `QuickSift-Ratings-YYYYMMDD-HHMMSS.txt`; UTF-8 (no BOM, CRLF) in the exact documented format; the version comes from `QS_VERSION_FILE_STRING`; only files that are rated, picked/rejected or colour-tagged are listed, with canonical tokens (`0`–`5`, `Picked`/`Rejected`/`None`, `Red`/`Yellow`/`Green`/`Blue`/`Purple`/`None`). Values not yet read are read from disk on a worker thread; the file is written to a temporary file, flushed and atomically moved into place. Progress in the status bar; a toast reports the result.
- **Import**: `IFileOpenDialog` (*.txt), read and strictly validated off the UI thread (header, time, counts, separators, every data line, duplicates, footer, nothing after it). Any error shows a dialog with the reason, line number and offending line; nothing is changed. Then, in order: the **count mismatch** question ("Total files count mismatch. Expected is x files, y files present. Continue?"), the **missing files** question (Yes / Cancel / **See missing files list**, which opens a scrollable, selectable, copyable window of the verbatim lines without closing the question), and the final "**Are you sure? This cannot be undone.**"
- Applied through the existing verified metadata transaction pipeline (optimistic catalog update, storage preflight, serialized transaction with external-change conflict checks) with status-bar progress and a summary toast ("N applied, N skipped, N failed"); failures and cancellations show an alert. Only listed files are modified; files already holding the imported values are skipped. Import is **not** recorded in Undo history (by design; Redo is cleared).
- Glass alerts gained an optional third button, keyboard focus (Tab/Shift+Tab, arrows, Enter/Space on the focused button, Esc cancels) with a visible focus ring, and text-measured button widths (EN and VI).
- Metadata transactions now expose progress (`MetadataTransactionProgress`).
- Portable checks: `scripts/checks/ratings_exchange_checks.cpp` (70 checks).

### Batch Processing

- New **File tab → Batch Processing** button. It opens a themed modal form: **File name list** (a multiline, scrollable text area plus **Load names from folder**), **Do what?** (Star rating, Pick state and Color tag dropdowns, each defaulting to *No change*), **Additional settings** (an **Extensions** field and a **Matching method** dropdown: Partial / Complete), then **Execute** and **Cancel**.
- Names may be separated by spaces, commas, semicolons, tabs or line breaks. Entries are trimmed, any directory prefix and one trailing supported image extension are removed, and duplicates count once (case-insensitive, whether typed or loaded). A name containing spaces must be written in double quotes; unquoted, it is split into separate names.
- **Load names from folder** opens a folder picker. The folder is read off the UI thread (non-recursive; hidden and system files skipped; supported image types only). The names, without extensions, are appended to the list, skipping any already present. The list stays editable.
- **Extensions** accepts `jpg`, `.JPG` or `*.jpg`, separated by spaces, commas or semicolons, case-insensitive and deduplicated. Unknown or unsupported types show an inline error and block Execute. An empty field means every supported type, and the confirmation says so.
- **Complete matching**: the file name without its extension equals the entry, case-insensitively. **Partial matching**:
  - numbers must match whole digit runs, ignoring leading zeros (`123` matches `IMG_0123`, but not `IMG_1234` or `IMG_51230`);
  - letter runs match as case-insensitive substrings;
  - letter runs of 4 or more characters also tolerate one typo (insertion, deletion, substitution or adjacent swap).
- **Scope**: every photo of the open folder (non-recursive; view filters and format toggles are ignored, like Ratings Export), narrowed to the target extensions. Each file is counted once even if several names match it.
- **Execute** checks the inputs, then matches off the UI thread. The confirmation reads "Batch operations: x files (y JPG, z ARW) will be processed. Confirm? This cannot be undone." It lists only nonzero extensions, shows the chosen changes, and reports names that matched nothing (count plus **See unmatched names**, which uses the missing-files list window). If no file matches, an information notice is shown instead. Cancelling the confirmation, or dismissing that notice, returns to the dialog with every input intact. Settings are remembered for the session.
- Changes are applied through the same verified metadata transaction pipeline as Ratings Import (`SubmitRatingsImport`, now with optional fields and a label). Progress shows in the status bar and a summary toast reports applied / skipped / failed. **Not added to Undo** (Redo is cleared). Batch Processing is refused while another file, metadata or ratings operation runs.
- New reusable framework elements in `src/ui/framework`:
  - `TextInputOptions` / `CreateTextInput`: a multiline or single-line D2D input with placeholder, error border, parent-surface corners, overlay scrollbar, selection, clipboard and change notifications;
  - `CreateFormDialog` and the `FormRow` model (`macos_ui_framework_form.cpp`);
  - `PickFolder`.
- Text input fixes (these also apply to the User comments field):
  - Backspace no longer deletes two characters;
  - selection highlighting works past 128 hit-test runs;
  - the D2D target uses 96 DPI, so pixel coordinates are exact (the full rounded border is never clipped);
  - typed digits, letters P/X/U, arrows, Home/End and Delete now reach a focused text input instead of firing global shortcuts.
- Portable core: `src/core/batch_match_policy.*`, plus the shared registry `AllSupportedImageExtensions()` in `core/image_formats`. Checks: `scripts/checks/batch_match_checks.cpp` (62 checks).

### Thumbnail search box

- A floating, rounded search field sits at the top right of **Thumbnail view only**. It fades in and out with the view, stays clear of the left pane, the scrollbar and toasts, follows the theme and DPI, and sits under the glass alert veil. The grid reserves a 54-DIP lane above the first row so the field never covers a card at the top of the list.
- **Ctrl+F** focuses it and selects the text, in Thumbnail view. In Single and Compare views, Ctrl+F keeps its previous meaning (zoom to fit).
- **Esc** clears the query; a second Esc returns focus to the grid. **Enter**, **Tab** or **Down** apply the query immediately and move focus to the grid. While the field has focus, typed keys never trigger global shortcuts (digits, P/X/U, Ctrl+Z/Y included).
- Shows a result count, e.g. "37 of 1,024" ("37 / 1.024" in Vietnamese). The total is the number of photos passing the Filter tab. The count turns red when nothing matches, and "…" is appended while facts are still being read.
- Queries are applied about 150 ms after typing stops, ANDed with the Filter tab, and use the normal visible-list machinery:
  - selection is kept for photos that remain visible;
  - a new query scrolls to the top;
  - clearing restores the scroll position from before the search, or reveals the focused photo if you moved to a result.
- Query language (all fuzzy, English and Vietnamese, accent-insensitive):
  - file names, with partial and typo-tolerant matching like Batch Processing;
  - user comments;
  - times: `8AM`, `8:30pm`, `20h`, `time:20`, `time:8 pm`;
  - dates: `date:12`, `date:fri`, `date:12/10`, `2026-10-09`;
  - stars: `5 stars`, `5 sta`, `>=4 stars`, `4+`, `★★★`, `rating:3`;
  - colour labels: `red`, `màu:đỏ`;
  - pick state: `picked`, `rejected`, `unmarked`, `pick:rej`.
  See USER_GUIDE.md → *Thumbnail search* for the full syntax and the precedence rules.
- Capture time is the EXIF DateTimeOriginal (falling back to DateTimeDigitized, then DateTime, then the file's modified time in local time). It is read lazily, in the background and only when a query needs it, with a bounded read (64 KB, then at most 512 KB per file). Comments are likewise loaded in background batches. Large folders (more than 2,000 photos) are evaluated on a worker against an immutable metadata snapshot. No disk I/O happens on the UI thread.

### Unified authoritative metadata

- The catalog record (`PhotoItem` in `CatalogStore`) is now the single authoritative metadata model. It holds rating, pick and colour knowledge from the transaction pipeline, plus capture time and user comment. The store keeps a **metadata epoch** that bumps only when facts change, and `Snapshot()` returns an immutable, epoch-tagged copy that worker threads can read safely (`core/image_metadata.h`).
- **Filters and Search share one predicate engine** (`core/photo_predicate.*`). The Filter tab compiles to the same clauses as the search box. Semantics are unchanged: an active rating, pick or label filter requires Known values. `scripts/checks/metadata_store_checks.cpp` checks this exhaustively against a reference implementation of the old rules.
- **One canonical token table** (`core/metadata_vocabulary.h`) for stars, pick and colour is shared by:
  - Ratings Import/Export (file format unchanged);
  - the XMP label writers;
  - the Cull and Filter menus and the Batch Processing dropdowns;
  - the status-bar text;
  - search keywords.
- Ratings Export reads the store and re-reads files only for fields the store does not know yet. Import and Batch still go through the verified metadata transaction service, which updates the store and so feeds search and filters.
- The User comments field reads and writes through the store, so search sees saved comments immediately.
- New checks: `scripts/checks/search_query_checks.cpp` (168) and `scripts/checks/metadata_store_checks.cpp` (23).

## Left-pane open/draw/close flicker exhaustive fix

- Eliminated remaining pane open/draw/close flicker with a single double-buffered opaque compose path.
- Animation ticks no longer call `SetWindowRgn(..., TRUE)` every frame (quiet region + 2-DIP radius quantize), no mid-animation `ShowWindow`, and no D2D `InvalidateCanvas` on pane-only blend changes.
- Children are shown once at a high reveal threshold (0.82 / hide 0.68) then moved/raised/clipped; tab switch uses one quiet hide + one opaque erase + one raise/compose (no per-child `UPDATENOW` thrash, no parent `RDW_ALLCHILDREN` over the canvas).
- Preserved frost opacity, stacking, tab clear, rounded corners, scroll hardening, and external-change reconcile.

## Left-pane rounded corners polish

- Library content panel no longer uses `WS_EX_CLIENTEDGE` (sharp white system edge). It paints a frosted `PaintPanel` / `PaintControlBorder` stroke and applies `ApplyRoundedControlRegion` with `kLeftPaneContentRadiusDip`.
- Library **Remove from library** action button now gets the same `ApplyRoundedControlRegion` (`kButtonRadiusDip`) as File/Cull/Filter buttons, so PaintButton chrome cannot leave a sharp rectangular underlay.
- Info **USER COMMENTS** section header matches Settings section headers: rounded window region plus `PaintSectionTitle` rounded fill/border (was a sharp rectangle vs rounded photo details / comment input / Save).
- Shared metric `kLeftPaneContentRadiusDip` (12) aligns library list, EXIF panel paint, and content region radius. Settings section labels / language selector use `kButtonRadiusDip` consistently.
- Preserved left-pane frost opacity, tab-switch clear, stacking, and scroll hardening.

## Left-pane frosted backdrop and tab-switch clear

- Restored opaque frosted left-pane backdrop paint. `PaintPanel` no longer uses a semi-transparent GDI+ base fill (`alpha 250`) when `translucent` is set — that AlphaBlend path showed the thumbnail canvas through the pane. Frost is now an opaque `panelStrong` base plus a highlight wash on top.
- `DrawFlyoutBackdrop` double-buffers (opaque underlay → frosted `PaintPanel` → BitBlt) and `FlyoutBackdropSubclassProc` fills opaque `panelStrong` on `WM_ERASEBKGND`.
- Tab switches call `HideAllLeftPaneSectionContent` (quiet `SWP_HIDEWINDOW|SWP_NOREDRAW`) then `EraseLeftPaneBackdropFully` (`RDW_ERASE|RDW_UPDATENOW`) before `LayoutControls` shows the new section, so old UI cannot stack in the shared content rect.
- Flicker hardening: no mid-frame `ShowWindow` thrash on section hide; opaque backdrop paint before children; composite redraw still raises only the active section.

## Left-pane section stacking fix

- Fixed critical left-pane bug where LIBRARY (and other tabs) could show every flyout section at once in the shared content rect (Settings buttons, EXIF/Info, Delete, User Comments overlapping Library).
- Root cause: visibility was asymmetric — inactive File/Cull/Filter/Settings were SW_HIDE'd, but the *active* section and Folders/Info chrome were left WS_VISIBLE whenever `showPaneContent` was false (reveal hysteresis), and `RaiseLeftPaneContentAboveBackdrop` / `RedrawActiveFlyoutContents` used `SWP_SHOWWINDOW`, which could keep prior section children composited in the one shared `LeftPaneContentRect`.
- `LayoutControls` now `HideAllLeftPaneSectionContent()` (SW_HIDE every section child) then shows only the active section when content should be visible; raise paths are Z-order only (no ShowWindow).
- Info User comments layout sizes EXIF/comment/save to fit inside the content rect with edge pad so the D2D border stroke is not clipped.
- Flyout section controls are SW_HIDE'd immediately after create (CreateButton/owner-draw default to WS_VISIBLE).

## Left-pane flicker root cause and virtualized scroll hardening

- Left command pane: fixed content vanishing except on hover. Root cause was the animated backdrop being raised to `HWND_TOP` every animation tick (and `SetWindowRgn` redraw), burying sibling tabs/buttons under an opaque panel until a hover invalidate briefly resurfaced them. Backdrop now stays above the canvas and below pane children; region updates skip when geometry is unchanged; children stay shown for the open session (hysteresis) without per-frame ShowWindow churn; composite raise+invalidate keeps them painted while open.
- Thumbnail View scroll: refactored to a virtualized viewport pattern — coalesce drag/wheel/keyboard motion to one layout/paint per frame; fling paints placeholders/cache and publishes viewport only on visible-row jumps (cancel stale, no decode); coast admits a size-capped rolling window once per frame; settle debounce (~140 ms) before full visible enqueue so successive flings cannot queue a backlog. Paint never enqueues thumbnail decodes during motion; fling cancels in-flight off-viewport thumbnail work immediately.

## Left-pane animation and UI bugfixes

- Left command pane open/close now animates width (hit-strip ↔ full pane) via `leftPaneOpenBlend_` / `EaseToward` instead of a discrete show/hide snap; content appears after a reveal threshold to avoid flicker and cramped mid-animation layout.
- Closed hit strip paints a small accent rail + grip dots so it reads as interactable.
- `PaintButton` erases with the same rounded/pill radius as the fill (and matching window region); removes the dark rectangular underlay that peeked behind selected title-bar pills such as Single.
- Info tab User comments D2D text input insets its border and resizes the render target so the right/bottom edges are no longer clipped.
- From Single View, Compare switches to Thumbnails and shows the existing “Select 2 to 6 photos to enter Compare.” toast.
- Shared button/tab padding and radius metrics in `ui_layout_metrics.h` unify PaintButton, width measure, sidebar tabs, and Labels dropdown spacing; tab column widened so labels are not clipped.

## Thumbnail scroll smoothness and UI polish

- Thumbnail View scroll/load priority now prefers UI smoothness: fast scrollbar or wheel flings stay cache/placeholder-only, coasting admits a size-capped rolling decode window, and settle restores full visible then predictive work.
- Larger thumbnail sizes keep lower concurrent decode caps during motion so Huge/Large cells cannot enqueue heavier fling work than Tiny/Small.
- Shared UI framework buttons gain richer borders, a light press inset, dual focus rings, and a short hover fade; left-pane open chrome eases instead of a hard pop.

## Left hover pane and toolbar wrapping

- Replaced the floating left-edge flyout tabs with a left command pane that appears on hover over a narrow left-edge hit strip (or over the pane itself) and hides after a short grace period when the pointer leaves.
- Former flyout sections are vertical sidebar tabs inside the pane; section content fills the remaining pane width. Pane width is a deterministic function of client size (`clamp(width × 0.30, min, max)`).
- File / Cull / Filter / Settings buttons and drop-down menus wrap onto new rows when horizontal space in the pane runs out; title-bar controls already wrapped and continue to do so.
- Sidebar tabs paint through the centralized `PaintSidebarTab` framework control; buttons and dropdowns continue to use `CreateButton` / `PaintButton` / `TrackDropdown`.
- Filmstrip toggle glyphs no longer fight between layout and toggle (`‹`/`›` everywhere). Opening or refreshing the filmstrip clears failure marks only for the visible filmstrip window so previously failed thumbnails can decode again without wiping the global failure map.

## External-change metadata reconciliation

- When a rating, pick-state, or color-label write is refused because on-disk identity or compare-and-swap values diverged (changed by another app), QuickSift now offers an explicit reconcile choice instead of only reporting failure.
- **Refresh from disk** abandons the stuck write, refreshes catalog stamps/cache, marks metadata pending and re-reads authoritative values from disk so future edits use the current baseline, and leaves on-disk bytes untouched. If the failure happened during metadata Undo/Redo, the stuck remaining history entry is also relinquished.
- **Keep catalog** preserves the prior refuse-and-leave-untouched behavior (optimistic UI already rolled back from the before snapshot).
- Sidecar atomic-publication identity failures are now classified as external conflicts so they enter the same pathway. Choices are logged.


## External-change history reconciliation

- When Undo or Redo is blocked because a file identity no longer matches history (changed by another app), QuickSift now offers an explicit reconcile choice instead of only reporting failure.
- **Relinquish history** drops the stuck Undo/Redo entry, refreshes catalog stamps/cache for the affected paths from disk, and leaves on-disk bytes untouched. **Keep history** preserves the prior refuse-and-leave-untouched behavior.
- The same pathway is offered when a mid-flight guarded Undo/Redo stops because remaining items diverged from recorded identity.
- Choices are logged; QuickSift still never silently overwrites unknown external bytes.


## Compare View priority and metadata responsiveness

- Compare no longer enters itself from a paint. A short window that rejected every scored arrangement now falls back to a plain grid instead of a blank canvas.
- Full-size decodes for the images being compared outrank other interactive work. Clicking, zooming, or moving to a pane moves that image to the front of the decode queue if it is not finished.
- Rating, pick, and color-label changes update the catalog and canvas immediately. The verified file write still runs afterward, and a failed item is restored from its before snapshot. In-flight decodes on a path being written are cancelled at the next checkpoint so the write is not stuck behind a full demosaic.


## Culling writes outrank metadata reads

- Rating, label, and pick updates no longer wait for a stuck metadata read to finish before the exclusive path handoff. Metadata reads are not exclusive-write blockers; decode, face, and EXIF handles still are.
- The shared Exiv2 gate is writer-preferring. A culling batch announces interest before it waits for decoders, and the metadata lane will not start another XMP read while that interest or an exclusive write is outstanding.
- An in-flight Exiv2 call still runs to completion. The library is not safe to preempt; the write proceeds as soon as that single call releases the gate, instead of waiting behind the metadata backlog.

## Single View filmstrip

- Filmstrip cells now fit a real cached thumbnail into the cell. They previously passed an invalid view into `DrawBitmapFit`, which returns immediately, so the strip painted empty frames.
- Filmstrip decode requests use the thumbnail lane and ignore the grid viewport filter, so Single View no longer drops them.
- Clicks hit the same cell rectangles that are painted, and a hit navigates Single View instead of falling through to pan.


## Shutdown responsiveness

- Made close-path cancellation broadcast before joins so active image work observes shutdown sooner.
- Removed duplicate forced SQLite WAL checkpointing from `WM_DESTROY`; cache shutdown now releases handles without synchronous checkpoint work.
- Added per-stage shutdown diagnostics (`resource_pressure_stop`, `transactions_stop`, `scanner_cancel`, `worker_stop`, `drain_completions`, `cache_close`).


## Diagnostic logging overhaul

- Added structured diagnostic events with sequence IDs, process/thread IDs, monotonic timestamps, and key/value context.
- Added asynchronous disk logging so decode workers do not synchronously serialize on file I/O.
- Expanded image-pipeline diagnostics: job dequeue/completion timing, decode-budget waits, persistent-cache hits/misses, viewport changes, view-mode changes, and folder-scan durations.
- Added periodic resource snapshots with total/available RAM and QuickSift process working/private memory.
- Expanded crash/session snapshots with retained/dropped-entry counts and log queue-drop counts.
- Added diagnostic startup/shutdown resource snapshots.


### JPEG decoder parallelism
- JPEG decoding now permits up to **6 independent TurboJPEG decoder sessions** to execute concurrently.
- Each session owns its own TurboJPEG handle; the limit is enforced only for JPEG decode sessions and does not reduce the general image worker pool.
- This keeps Single/Compare and high-throughput thumbnail workloads parallel while preventing an unbounded number of simultaneous TurboJPEG sessions.
# QuickSift 0.9.3.0 RC — Maximum-performance image pipeline

- Removed adaptive CPU/RAM/GPU throttling from thumbnail, single-view, and compare-view image loading.
- Visual workers now occupy the full logical CPU pool, with one idle-capable lane retained for background work.
- Decode, mapped-input, reusable-buffer, retained-session, read-ahead, and cache-write ceilings are no longer reduced by runtime pressure.
- View-image retention no longer churns in response to adaptive pressure states.

# Changelog

## 0.9 R4 — Media-unit atomicity and failure-path hardening

- Added **Settings → Load only JPG previews for RAW** (default off). When enabled, RAW display/face-analysis jobs may use only embedded JPEG thumbnails; full LibRaw demosaic, WIC/Shell RAW fallback, persistent RAW bitmap reuse, and RAW deep-zoom tiles are blocked. Policy changes cancel/coalesce safely across queued and in-flight jobs.
- Added live Copy/Move/Delete-to-`Deleted` progress as a second bottom-status line. Progress counts committed media owners rather than sidecars and advances only after an atomic media group succeeds; ETA is derived from completed-file throughput.
- Added guarded close behavior for active file operations: **Close anyway** requests cancellation at a safe boundary and closes after transaction completion is integrated; **Wait** dismisses the prompt without changing the operation.
- Added transaction-progress race tests, sidecar-counting regression coverage, RAW decode-policy source contracts, and close/progress verifier guards.


### Coordinated review subsystem refactor

- Consolidated thumbnail focus/scroll, Single/Compare identity, semantic zoom, Sync, FaceLock offsets, navigation epochs, and thumbnail viewport epochs into one portable `ReviewStateModel` under `src/review`.
- Replaced separate unsynchronized Compare transform/FaceLock maps with one per-pane record and removed parallel `currentPath`/`focusPath`/paint-hit state from the Win32 application object.
- Moved thumbnail layout, thumbnail prefetch prediction, view transforms, and async stale-result admission beside the state owner as stateless review policies. Workers now mirror model-owned epochs rather than generating viewport state.
- Preserved historical behavior for semantic zoom re-resolution, true synchronized physical magnification, rotation-aware pointer anchoring, FaceLock framing, replacement-pane inheritance, overlapping thumbnail cancellation, stale pixel rejection, completed-face reuse, and current-layout Compare hit testing.
- Added `QuickSift.ReviewSubsystem` as the portable behavioral contract and migrated review-specific regressions out of unrelated core/UI-model suites.

### Source-media safety

- Replaced one-image/one-sidecar transfer planning with atomic media groups that can contain multiple selected image owners and one shared XMP companion. A failure or cancellation rolls back the whole group.
- Preserved media-group identity in Undo/Redo history and added compensating replay, preventing image/XMP splits during partial history failure.
- Expanded guarded history identity with change time and sampled-content fingerprints so same-size, timestamp-preserving external edits are refused.
- Reworked verified copy, move, XMP/session publication, and safe embedded-metadata publication around exact locked handles and handle-bound rename requests. Final-path verification now proves that the published pathname resolves to the exact object that was verified.
- Safe embedded writes now compare canonical decoded pixel essence before and after metadata editing, in addition to metadata/container validation.
- Added exact-object quarantine and restoration paths so failed publication cannot delete or overwrite an unrelated replacement at the same pathname.

### Failure semantics and recovery

- Added explicit successful, partial, and failed scanner completions with native error details; access, provider, device, and mid-enumeration failures are no longer reported as an empty successful folder.
- Made scan-batch integration transactional and delayed scanner acknowledgement until commit or intentional discard.
- Made completion draining ownership-preserving and no-throw after the consumer container exists, so low-memory failure cannot discard queued results.
- Made `CatalogStore` reject duplicate paths before mutation and gave photo edits a strong exception guarantee.
- Made metadata-history partitioning conservative when completion vectors are short and preserved truthful partial completion through service exception boundaries.
- Removed allocating recovery work from `noexcept` transaction-thread emergency paths.
- Enforced the configured history-memory cap even when only one oversized entry remains.
- Replaced piecemeal INI writes with one atomically published session document.

### Zoom, synchronized comparison, and FaceLock correctness

- Replaced stale fit-relative zoom scalars with semantic zoom intent plus an absolute physical scale for custom zoom. Fit Width, Fit Height, Fill, and 100% now resolve against the current pane, DPI, orientation, and source dimensions on every render.
- Added one portable, pane-aware, rotation-aware view-transform policy for rendering, drag panning, cursor-anchored wheel zoom, legal center clamping, and synchronized magnification.
- Made Compare wheel input target the pane under the pointer, initialized unsynchronized Compare panes from a clean Fit state, and gave replacement panes an explicit slot-transform inheritance policy.
- Preserved every visible pane when disabling FaceLock, stopped ordinary pane activation from recentering FaceLock, and separated analysis requests from explicit recentering.
- Added a larger analysis decode when the primary face is too small for a valid sharpness crop, preserved pending sharpness state when analysis did not run, and made primary-face ordering deterministic.
- Added portable regression coverage for semantic zoom re-resolution, true 100%, synchronized physical scale, rotated pan/clamp geometry, cursor anchoring, and deterministic face ranking.
- Added capability-gated FaceLock scheduling: unsupported Windows face detection now disables the control and stops all background/foreground face jobs, temporary capability failures retry at a controlled interval, and per-photo failures use bounded exponential backoff with an explicit user retry path.
- Added localized FaceLock availability text, status reporting, failure diagnostics, and portable retry-policy regression coverage.

### Thumbnail, folder-open, and diagnostics performance

- Centralized thumbnail geometry in one layout policy used by rendering, hit testing, prefetch, keyboard reveal, and size-change anchoring. Thumbnail clicks now resolve against current scroll/catalog state instead of stale paint rectangles, and changing thumbnail size preserves the focused/visible anchor.
- Reject undersized embedded JPEG previews when they cannot satisfy the requested thumbnail edge, refine inadequate stand-ins with a real scaled decode, and self-heal legacy persistent thumbnail entries whose stored pixels are materially smaller than the bucket they claim.
- Reworked folder discovery into a cheap, generation-cancellable persistent scanner that publishes `WIN32_FIND_DATA` records immediately and defers strong fingerprints, sidecar probes, metadata hydration, EXIF, and face work to bounded background workers. Switching folders no longer synchronously joins an old scan.
- Removed N+1 folder-tree probing by using optimistic child placeholders, added a short folder-selection debounce, and made format/date filtering rebuild the in-memory catalog instead of rescanning the filesystem.
- Moved session diagnostics to `QuickSiftData` beside the executable. Startup now verifies create/write/delete access to `Logs` and `CrashReports` and refuses to launch with a visible error if the required data root is unusable. The newest 20 session logs and 20 crash incidents are retained, with a 16 MiB per-session log safety ceiling.
- Consolidated all QuickSift-owned runtime persistence under that same `QuickSiftData` root: `QuickSift.session.ini` for session/preferences and `.cache` for disposable SQLite/bitmap/tile data. Existing executable-adjacent and LocalAppData session/cache locations are migration sources only; new writes never fall back outside `QuickSiftData`.

### UI and lifecycle hardening

- Added a central adaptive performance governor with Normal, Constrained, Critical, and Recovery states; runtime decode permits, memory ceilings, read-ahead, mapped input, retained decoder sessions, derivative generation, and cache-write policy now respond together with hysteresis.
- Replaced first-waiter decode memory admission with priority/FIFO, cancellable reservations. Interactive work cannot be overtaken by older idle work, oversized decodes are exclusive, and critical pressure can refuse them entirely.
- Replaced global decoder suspension during metadata writes with path-scoped read-ahead/decoder exclusion. Unrelated files continue decoding, overlapping exclusions are reference-counted, and a write never starts while affected read-ahead still owns a source handle.
- Time-sliced UI completion integration by item count and elapsed time, prioritized terminal/current-image work, and preserved scan-batch-before-scan-complete ordering.
- Added viewport-specific thumbnail cancellation that retains overlapping queued/in-flight paths while canceling work that left the new plan.
- Added guarded metadata fairness, hardware-topology-aware worker sizing, measured queue/decode telemetry, completion-age feedback, and readiness-aware thumbnail lead distance.

- Kept background decode completions flowing through every centralized custom dropdown without dispatching unrelated owner commands, preventing long-open menus from retaining large pixel results.
- Buffered the bottom status surface, suppressed owner-draw background erasure, and stopped repainting rating, cull, and history controls when their state has not changed.
- Added a genuine middle-resolution full decode during deep zoom, retained lower-resolution jobs across wheel steps, and replaced nearest-neighbour interaction scaling with linear filtering.
- Isolated the synchronous framework dropdown pump to the popup window hierarchy so scanner, transaction, timer, close, and shutdown messages remain queued for the owner instead of re-entering suspended command handlers.
- Added shutdown-block reasoning while a transaction is active and canceled-shutdown recovery without permanently disabling future work.
- Added portable regression coverage for shared-sidecar rollback, grouped history compensation, duplicate catalog rejection, strong catalog edits, strict history limits, conservative metadata recovery, and service cancellation.
- Expanded Windows-native file-operation coverage for timestamp-preserving external edits.

## 0.9 — Compiled modules, serialized transactions, diagnostics, and pre-release hardening

### True compiled boundaries and transaction ownership

- Added a thin public PIMPL facade and moved the remaining Win32 composition surface to a private internal header.
- Added `SelectionStore`, removed raw selection/hash-set and sentinel-anchor ownership from the window controller, and added portable regression coverage.
- Made `CatalogStore` and `HistoryStore` invariant-owning APIs instead of exposing mutable backing containers.
- Added `TransactionCoordinator` for cross-domain serialization, tokens, cancellation, and Windows close/logoff behavior.
- Fixed transaction-service startup data races by starting workers only after all observed state is constructed.
- Replaced worker-side allocating `std::function` copies with shared immutable callback snapshots.
- Added stop-independent scanner terminal publication and canceled-logoff reopening.
- Added cancellable `CopyFileExW` and byte-comparison loops while preserving private publication and guarded rollback.
- Replaced textual `.inl` composition with a thin process entry point and separately compiled UI, worker, scanner, transaction, store, and platform modules.
- Reduced `src/main.cpp` to roughly 60 lines and made CMake the sole implementation composition mechanism.
- Extracted `CatalogStore`, `HistoryStore`, `FolderScanner`, `BackgroundCompletionQueue`, `FileTransactionService`, and `MetadataTransactionService`.
- Replaced raw heap payloads in private Win32 messages with synchronized queue ownership and payload-free wake-ups.
- Moved copy, move, delete-to-folder, metadata edits, and file/metadata Undo/Redo off the UI thread.
- Added one decoder-exclusion handoff per direct metadata batch instead of one global barrier per file.
- Added cancellation at safe file boundaries, identity-guarded replay inside locked primitives, paired-sidecar rollback, and partial-history recovery.
- Added portable tests for catalog storage, history retention, completion ownership, file transactions, and metadata transactions, plus Windows-native verified-file-operation tests.
- Updated source verification to reject implementation fragments, unregistered compiled modules, raw pointer message payloads, synchronous UI-thread mutation, and missing shutdown contracts.

### Central macOS-style UI framework

- Added `src/ui/framework`, a central native component layer for child-control creation, shared button interaction, panels, labels, status surfaces, title-bar traffic lights, dropdowns, toasts, alerts, media cards, badges, overlay labels, and shared text rendering.
- Added platform-neutral `PopupMenuModel`, `ToastPresenter`, and `AlertPresenter` state with portable regression tests.
- Converted main-window and document buttons, tooltips, labels, tabs, status panels, EXIF panels, flyout surfaces, title-bar chrome, scrollbars, toasts, alerts, dropdowns, cards, badges, and overlay text to framework calls.
- Converted dropdown call sites to declare one framework `MenuItem` model carrying text, command, checked state, separator state, and destructive styling; removed parallel visual-item and command arrays.
- Centralized owner-drawn button hover/focus behavior and removed feature-local button subclass/property state.
- Preserved specialized canvas bitmap/tile/zoom composition while routing reusable canvas chrome through the framework.
- Added source verification that rejects feature-local ordinary component painting, legacy popup/button paths, child `CreateWindowExW` calls outside the framework, and reintroduction of parallel menu models.
- Expanded the Windows UI-foundation target to compile every framework implementation and smoke-test centralized control creation and GDI component painting.
- Added high-DPI dropdown viewport scrolling, mouse-wheel support, Home/End/Page navigation, automatic keyboard-item visibility, and a macOS-style overlay scroll thumb.
- Replaced Release-disabled `assert()` checks in selection/completion tests with always-active assertions and made source verification reject future use of `assert()` in tests.
- Fixed centralized alert empty-rectangle hit testing, compare-card border repainting, popup repaint colors, document font refresh after theme/DPI changes, DirectWrite alignment restoration, and a malformed Windows-only title-bar function definition discovered during the migration.

### UI architecture and readability

- Unified native UI ownership under `src/ui` without recreating a single-file monolith.
- Added one command/control ID registry, one main-window control catalog, one layout-metric registry, one semantic design system, and one native window contract.
- Centralized native class names, messages, timer IDs, subclass IDs, window-property keys, button creation metadata, UI-area membership, and title-bar minimum widths.
- Renamed the native controller from `App` to `QuickSiftApplication`.
- Renamed feature files by explicit responsibility and documented exact edit routing.
- Began the responsibility-named UI split that is now completed as separately compiled modules.
- Added source guardrails for UI location, module size, controller naming, ID uniqueness, subclass/property uniqueness, and timer routing.

### UI reliability fixes

- Fail `WM_CREATE` when required common controls, child controls, or subclasses cannot be created, preventing half-built interfaces.
- Add actionable logging for help/about/diagnostic window creation and state publication failures.
- Add monitor/work-area fallbacks for popup placement and `WM_GETMINMAXINFO` sizing.
- Route UI timers through checked `StartUiTimer` creation.
- Preserve an existing document panel brush when replacement allocation fails.
- Add failure-safe shared GDI solid-fill and line helpers.
- Replace anonymous child and subclass IDs with named, collision-tested constants.

### Diagnostics

- Added a thread-safe 5,000-entry diagnostic ring and persistent UTF-8 session logs.
- Added a themed **Settings → Diagnostic log** viewer with Refresh and Export controls.
- Added manual snapshot export, unhandled-exception text reports, minidumps, and `LATEST_CRASH.txt`.
- Verify manual exports, surface write failures, close session logs cleanly, and guard crash-report re-entry.

### Responsiveness

- Added a centralized runtime performance governor with memory, CPU, and GPU hysteresis; dynamic decode permits, codec threads, mapped-input and reusable-buffer ceilings; pressure-aware read-ahead, decoder sessions, derivatives, and cache writing; and bounded UI completion integration.
- Replaced FIFO decode-memory races with cancellable priority/FIFO admission, exclusive oversized reservations, read-ahead suspension during oversized work, viewport epochs for stale thumbnail cancellation, and guarded metadata fairness.
- Replaced global metadata-write worker pauses with path-scoped decoder/read-ahead exclusion so unrelated images continue decoding safely.
- Added worker latency/cancellation telemetry and throughput-aware prefetch lead based on measured queue, decode, and UI-backlog delay.
- Added velocity- and direction-aware thumbnail planning.
- Added a `Predictive` worker priority between visible work and background work.
- Queue current rows first, leading rows second, and trailing overscan last.
- Replace stale queued thumbnail work when the viewport changes.
- Trigger planning immediately from wheel and keyboard scrolling and use bounded read-ahead in the predicted direction.

### Code guide and exhaustive bug sweep

- Added `CODE_GUIDE.md`, a plain-English routing map for humans and coding assistants, with runtime flow diagrams, edit recipes, naming rules, asynchronous ownership rules, file-safety invariants, and a definition of done.
- Added concise `CODE GUIDE` and `OWNER` headers to major source and test components; portable verification rejects new components that omit them.
- Fixed queued work promotion so a higher-priority request is physically moved to the correct queue instead of retaining its old waiting position.
- Hardened predictive prefetch math against NaN, infinity, invalid row heights, and extreme viewport values before float-to-integer conversion.
- Replaced ad-hoc XMP string insertion with a tested simple-property reader/writer that correctly handles self-closing `rdf:Description` tags, XML escaping, existing attributes/elements, and unfamiliar documents.
- Added regression tests for queue promotion, malformed self-closing XMP insertion, XML escaping, non-finite prefetch inputs, and extreme viewport geometry.

### Visual Studio and build entry points

- Added `build.cmd` for a strict tested Debug build.
- Added `build-release.cmd` for Release build, tests, PE verification, optional signing, packaging, and SHA-256 output.
- Added `open-in-visual-studio.cmd`, which discovers Visual Studio/CMake/vcpkg, generates an external solution, and opens it.
- Added Visual Studio 2026 and Visual Studio 2022 CMake presets plus `.vsconfig` workload metadata.
- Made `QuickSift` the generated solution startup project and unified the MSVC runtime across the application and test targets.
- Added source guardrails that require the build entry points and Visual Studio startup/runtime contracts.

### Automated tests and release pipeline

- Added CTest coverage for prefetch behavior, catalog filters/sorts, RAW recognition, date boundaries, decode-budget accounting, and shutdown wake-up.
- Added Windows diagnostics and UI-foundation tests, including theme contrast and GDI drawing primitives.
- Added portable and PowerShell source-structure verification.
- Release builds run source checks, warnings-as-errors, automated tests, PE verification, optional signing, and packaging in order.

### Previous 0.9 hardening retained

- Unified all application-facing versions at 0.9.
- Consolidated the build on CMake.
- Enabled and verifies CFG, high-entropy ASLR, DEP, CET compatibility, stack/security diagnostics, and restricted dependent-DLL loading. EH-continuation metadata is opt-in because bundled static dependencies are not currently compatible.
- Retained verified file transactions, guarded rollback, decode-budget fixes, exclusive metadata-write/read-ahead coordination, and persistent-cache validation.

### Thumbnail viewport priority (2026-08-16)

Thumbnail mode treats the currently visible thumbnail window as a foreground visual task. Visible thumbnails are queued at `Interactive` priority in strict left-to-right, row-major order. On viewport change, queued thumbnail decode work is discarded and rebuilt from the new viewport; in-flight thumbnail work that is no longer visible is canceled at worker cancellation checkpoints. Predictive/background thumbnails remain lower priority and are repopulated only after the current visible window. Thumbnail cache writes inherit the same priority and visible thumbnail cache writes are persisted ahead of metadata and lower-tier cache work.
## Image pipeline audit fixes

- Thumbnail viewport work is strictly priority-monotonic; visible thumbnails remain Interactive even when the renderer re-requests them.
- Visible thumbnail cache writes are drained ahead of metadata/background cache writes, with newest viewport epochs preferred.
- Face analysis is paused/cancelled while the thumbnail viewport is foreground-active so it cannot occupy decode capacity needed to show the grid.
- TurboJPEG waits with a cancellation-aware timed semaphore instead of a spin/yield loop; at most six independent TurboJPEG sessions run concurrently.
- WIC retained decoder sessions remain fixed hardware-tier limits rather than scaling with logical CPU count.
- Decoded pixel, mapped-input, reusable-buffer, and aggregate decode budgets are fixed safety limits selected at startup; they never adapt downward under pressure.
- Removed the unused runtime critical-pressure flag from the worker.


### Startup responsiveness
- Deferred persistent-cache disk initialization until after the first main-window frame.
- Removed forced pre-window diagnostic write probes and cache-directory creation.
- Deferred session restoration until disk-backed startup is ready.
- Added `WM_APP_STARTUP_READY` lifecycle handoff and startup timing diagnostics.

## Aggressive thumbnail decode retry

- Added four-attempt thumbnail retry state with 3s / 6s / 6s / 3s per-attempt timeouts.
- Attempt 4 forces WIC-only decoding.
- Retry attempt and decoder mode are part of thumbnail job identity and result telemetry.
- Permanently failed thumbnails are suppressed until a viewport change brings them visible again, which resets the retry sequence.
- Visible retries retain Interactive priority; off-screen failed thumbnails are not retried until visible.
## Scrollbar drag responsiveness
- Thumbnail scrollbar drag now adapts decode admission to measured scroll velocity.
- High-speed drags are cache-only; slow/moderate drags allow at most three rolling thumbnail decodes.
- Background completion processing is budgeted more aggressively while dragging so posted worker results cannot monopolize the UI message pump.
- Thumbnail decode cancellation, enqueue admission, paint-time requests, and completion handling are all synchronized around the drag mode.

