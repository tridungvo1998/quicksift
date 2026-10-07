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

