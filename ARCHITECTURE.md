# QuickSift 0.9 Architecture

QuickSift is organized around one rule: **each decision and mutable resource has one explicit owner**. The Win32 controller coordinates services, but catalog storage, history retention, background completion ownership, scanning, file mutation, metadata mutation, diagnostics, rendering policy, and deterministic calculations are not reimplemented in message handlers.

For a task-oriented route through the code, start with [`CODE_GUIDE.md`](CODE_GUIDE.md). For exact UI edit routing, see [`src/ui/README.md`](src/ui/README.md).

## Face-analysis capability and retry policy

`src/face_detector.cpp` is the Windows Media FaceAnalysis adapter. `src/core/face_detection_policy.*` owns portable capability and per-file retry state, while `src/ui/face_analysis_controller.cpp` is the only UI path allowed to enqueue face jobs. Unsupported capability disables FaceLock for the process lifetime; temporary capability probing retries after a delay; individual decode/detector failures back off and stop after five automatic attempts. A deliberate FaceLock toggle grants a fresh foreground retry for the active photo.

## Composition root

`src/main.cpp` is a thin process entry point. It initializes process-wide facilities, constructs `QuickSiftApplication`, and enters `wWinMain`. It is currently about 60 lines and is capped by source verification.

`src/app/quicksift_application.h` is a small PIMPL facade. The private composition root and the shrinking Win32 orchestration surface live in `src/app/quicksift_application_internal.h`; feature modules include that private header, while consumers of the application do not recompile against rendering, worker, catalog, or transaction state. Implementation fragments and textual inclusion are forbidden. CMake is the only composition mechanism.

The private controller remains the Win32 orchestration boundary, not the owner of every invariant. Catalog ordering, selection membership/range anchors, history retention, transaction serialization, and background completion ownership are concrete, independently tested modules.

## Module map

### `src/core`: portable policy and stores

- `app_types.h` — shared catalog, work-item, cache, filter, and view-domain types.
- `image_formats.*` — authoritative JPEG and camera-RAW extension policy.
- `catalog_policy.*` — deterministic filtering and sorting.
- `catalog_store.*` and `catalog_store_mutation.cpp` — authoritative photo storage, collision-rejecting path index, transactional batch append, strong-exception-guarantee edits, visible ordering, and reverse visible positions.
- `history_store.*` — bounded Undo/Redo ownership and memory accounting.
- `selection_store.*` — selected-path membership, pruning, memory release, and range-anchor ownership.
- `history_partition.h` — deterministic partial-history partitioning.
- `stable_file_identity.h` — rename-stable identity stamps used by guarded replay.
- `decode_budget.*` — priority/FIFO decoded-memory admission, cancellable waits, dynamic permit/byte ceilings, exclusive oversized reservations, and RAII release.
- `performance_policy.*` — the central Normal/Constrained/Critical/Recovery governor, pressure hysteresis, runtime decode/codec/read-ahead/buffer/cache limits, and UI integration budgets.
- `system_profile.*` — runtime sizing for workers, caches, codecs, and read-ahead.
- `work_queue_policy.h` — deterministic worker-priority mechanics.
- `face_policy.*` — FaceLock multi-face policy: centre-closest default ordering (ties: area, top, left) with a small-face eligibility rule, conservative detection noise filter, persisted-selection matching (IoU / nearest centre), target resolution, outline hit testing, and the click/framing constants.
- `face_quality_policy.*` — pure per-face quality analysis on BGRA (contrast-normalized Laplacian detail, directional anisotropy for motion blur, sclera-visibility closed-eye heuristic) with every threshold in `FaceQualityThresholds` and a versioned result (`kFaceQualityAnalysisVersion`).
- `image_metadata.h` — the authoritative metadata model contract: `ImageMetadataRecord`, the immutable epoch-tagged `CatalogSnapshot`, `FactsOf` views and `SameMetadataFacts` (which edits bump the epoch).
- `metadata_vocabulary.h` — the one canonical token table for stars, pick state and colour labels: values, export tokens, XMP label text, menu and filter labels, and the EN+VI search keywords.
- `photo_predicate.*` — the shared predicate engine used by both the Filter tab and Thumbnail search: `Clause`, `ClauseMatches` / `MatchesAll`, compilation of the Filter-tab criteria, and accent folding.
- `search_query.*` — the Thumbnail search query language (tokenizer, fuzzy keyword resolution, precedence), compiled to `predicate::Clause`.
- `exif_capture_time.*` — a bounded EXIF capture-time reader for JPEG APP1, TIFF/RAW IFDs and embedded TIFF blocks.

Portable modules build into `QuickSiftCore` and are exercised on Windows and non-Windows hosts.

### `src/transactions`: mutation boundaries

- `file_transaction.*` and `file_transaction_group_support.h` — portable execution of verified copy/move/delete-to-folder media groups, including multiple image owners sharing one companion, atomic group rollback, cancellation boundaries, and per-item results.
- `file_history_replay.*` and `file_history_group_replay.*` — portable group-preserving Undo/Redo with identity-guarded primitives, compensation, and truthful partial-history recovery.
- `file_transaction_service.*` — one serialized transaction thread, cancellation, result ownership, and completion notification.
- `verified_file_operations_windows.*`, `verified_file_move_windows.cpp`, and `platform/locked_file_publication_windows.h` — exact-handle Windows copy/move, handle-bound publication, non-overwrite commit, byte verification, flushing, strong fingerprints, exact-object quarantine, and guarded deletion.
- `metadata_transaction_service.*` — one serialized metadata thread, one decoder exclusion handoff per direct-write batch, per-item completion, cancellation, and metadata Undo/Redo.
- `transaction_coordinator.*` — cross-domain serialization, completion tokens, cancellation, and close/logoff policy.

The UI may build immutable plans and integrate results. It must not execute transfer or metadata-write primitives on the UI thread.

### `src/work`: asynchronous decode and scanning

- `background_work_engine.*` and `background_performance_controller.cpp` — decoder execution, priority queues, guarded fairness, viewport cancellation, adaptive permits, telemetry, cache writing, path-scoped read-ahead, and decoder-exclusion coordination.
- `folder_scanner.*` — bounded folder enumeration, explicit success/partial/failure completion, native error reporting, and acknowledgement-backed batch publication.
- `background_completion_queue.h` — synchronized ownership of worker results and scan batches, priority-aware one-item removal, completion age, and scanner ordering barriers. Win32 messages are payload-free wake-ups; the queue retains the actual objects.

### `src/metadata`

- `xmp_simple_property.*` — conservative reading and updating of simple XMP properties.
- `metadata_value_parser.*` — bounded full-value parsing and write-domain validation.
- `jpeg_orientation.*` — bounded JPEG APP1/TIFF orientation parsing.
- `README.md` — metadata ownership, precedence, and extension rules.

### `src/review`: coordinated review subsystem

- `review_state_model.*` — authoritative thumbnail focus/scroll, Single/Compare identity, zoom state, Sync state, FaceLock offsets, navigation epoch, and thumbnail viewport epoch.
- `thumbnail_layout.*` — shared thumbnail render/hit-test/prefetch/keyboard geometry.
- `thumbnail_prefetch_policy.*` — deterministic velocity-, direction-, and measured-readiness-aware thumbnail-row planning.
- `review_async_policy.*` — shared navigation/viewport stale-result semantics for workers and UI result admission.

Workers mirror review epochs for execution/cancellation; they do not generate or own review-session state. UI feature modules adapt current Win32/catalog geometry to these portable policies. See `src/review/README.md`.

### `src/diagnostics`

- `platform/application_data_paths.h` — the single owner of executable-adjacent runtime paths: `QuickSiftData`, `.cache`, and `QuickSift.session.ini`. New runtime persistence must not invent a second root.
- `diagnostic_log.*` — thread-safe logging under executable-adjacent `QuickSiftData`, bounded viewer snapshots, 20-session/20-crash retention, UTF-8 log files, manual export, crash reports, and minidumps. `QuickSiftData` storage is a required startup dependency.

### `src/platform`

- `storage_space.*` — storage-headroom queries used during transaction preflight.

### `src/ui`: native presentation and orchestration

Central component framework and foundation:

- `framework/ui_models.*` — portable dropdown navigation, toast timing/queueing, alert state, and hit testing.
- `framework/macos_ui_framework.*` — one macOS-inspired facade for child-control creation, button interaction, panels, labels, status surfaces, title-bar traffic lights, dropdowns, toasts, alerts, cards, badges, and shared text rendering. GDI, popup-host, and Direct2D implementations are separately compiled.
- `ui_command_ids.h` — authoritative command/control IDs.
- `ui_control_catalog.h` — authoritative persistent main-window button catalog.
- `ui_layout_metrics.h` — shared device-independent geometry.
- `ui_design_tokens.h` — the design tokens (radii, spacing, strokes, type, motion, glass, accessibility policy); see `docs/DESIGN_SYSTEM.md`.
- `ui_design_system.*` — semantic palettes, contrast, DPI conversion, and low-level failure-safe primitives used by the component framework.
- `../app/application_support.*` — registered application classes, private messages, timers, and application-specific subclass/property keys.
- `../app/application_text_io.cpp` — verified atomic UTF-8/UTF-16 text publication used by XMP and session persistence.
- `document_and_menu_models.cpp` — help, about, and diagnostics document models.

Feature modules provide semantic presentation state and layout, then call `MacOsUiFramework`; they do not independently paint ordinary buttons, menus, panels, title bars, toasts, alerts, labels, badges, or cards. The canvas retains specialized image/bitmap composition and ambient background rendering, while its reusable chrome routes through the same framework.

Separately compiled feature modules:

- `app_theme_documents_and_localization.cpp`
- `main_window_chrome_and_messages.cpp`
- `main_window_controls_layout_and_painting.cpp`
- `popup_menus_commands_and_folder_tree.cpp`
- `catalog_loading_and_result_integration.cpp`
- `graphics_resources_and_image_cache.cpp`
- `canvas_rendering_and_overlays.cpp`
- `view_state_controller.cpp`
- `viewport_prefetch_controller.cpp`
- `navigation_input_and_session.cpp`
- `metadata_history_and_file_safety.cpp`
- `settings_filters_and_file_commands.cpp`

These modules implement methods declared only in the private `quicksift_application_internal.h`; the public facade exposes no UI or rendering state. None is textually included into another source file.

## Runtime flows

### Folder loading and decoding

1. The UI starts `FolderScanner` with a new generation.
2. The scanner enumerates accepted files and pushes bounded batches into `BackgroundCompletionQueue`.
3. A payload-free private message wakes the UI, which drains the owned queue and updates `CatalogStore`.
4. `PrefetchPlanner` creates an ordered viewport plan.
5. `FixedPerformancePolicy` now exposes a fixed maximum-performance policy; CPU/RAM/GPU observations are telemetry only and never reduce visual decode capacity.
6. `Worker` prioritizes interactive and visible work, admits decode memory by priority, then gives metadata a guarded fairness turn after a bounded predictive/face burst.
7. Thumbnail paths that remain inside the next viewport retain useful queued/in-flight work; paths that leave the plan are canceled by viewport epoch.
8. Results carry generation/epoch information, enter the completion queue, and are integrated within an item/time budget. Scanner completion cannot overtake its preceding batches.


### Zoom, Compare Sync, and FaceLock

1. A `ViewState` stores semantic zoom intent (`Fit`, `FitWidth`, `FitHeight`, `ActualPixels`, `Fill`, or `Custom`) plus normalized source center and rotation. Custom mode stores absolute display pixels per source pixel rather than a pane-relative fit multiplier.
2. `view_state_controller.cpp` resolves the owning pane rectangle, oriented source dimensions, FaceLock-relative center, and current DPI into one `ViewGeometry`.
3. `review_state_model.*` supplies the authoritative per-mode/per-pane state and `review/view_transform_policy.*` produces the canonical `ResolvedView` used by rendering, pan, wheel anchoring, and decode refinement. Every pane resolves semantic modes independently; synchronized Custom mode therefore retains equal physical magnification across unequal panes.
4. FaceLock analysis and FaceLock recentering are separate actions. Ordinary pane activation requests missing analysis but preserves the pane's stored face-relative framing.
5. Disabling FaceLock materializes every visible pane's current face-relative framing into ordinary view state before offsets are cleared.
6. **Target face.** `PhotoItem::faces` is ordered by `core::RankFacesForAnchor` (centre-closest first). `TargetFaceIndexForPath` resolves the FaceLock target: the user's selection (`PhotoItem::faceSelection`, a normalized box) matched by `core::MatchFaceSelection`, else `faces[0]`. `PrimaryFaceCenter` (FaceLock recentering, offsets, wheel pivot) always means this target.
7. **Outlines and click-to-frame.** `DrawFaceOutlines` paints after the bitmap/tiles with the bitmap's own placement and rotation transform and records inflated screen-space hit boxes per pane (`faceOutlineRegions_`, rebuilt every paint). Mouse-down records a pending face click (`HitFaceOutline`), captures the toggle inputs (`CaptureFaceTogglePress`, before Compare slot activation) and starts a `PressGesture` with threshold `CanvasDragThreshold()` = max(SM_CXDRAG/SM_CYDRAG at window DPI, `kFaceClickMaxTravelDip`); moving beyond it turns the press into a pan; a release counts as a click only on the same face hit box; `WM_CAPTURECHANGED`/Escape cancel. Mouse-up calls `HandleFaceOutlineClick`, which asks `FaceZoomToggle::Decide` (`review/face_interaction_policy.*`) whether to revert to the stored pre-lock `ViewState` (same face, still at the lock zoom) or to call `SelectFaceAndFrame`, which stores the selection (session + persisted), resets the pane's face offset, and stores `transform::FrameSourceRect(face, kFaceClickFillFraction)` through `StoreEditableViewState`. Sync on: the shared view takes the framing and every pane keeps centring on its own target. Sync off: only the clicked pane's independent state changes.
8. **Zoom toggle state.** `faceZoomToggle_` keys entries by path (Compare + Sync: one shared key). `ClearFaceZoomToggle` runs on navigation epochs, wheel/keyboard zoom, pan conversion, rotation, reset, canvas resize, folder load and FaceLock/Sync toggles.
9. **Outline fade.** `faceOutlineFade_` (`FaceOutlineFade`) is observed per drawn path each frame (`BeginFrame/Observe/EndFrame`): new paths or face sets re-arm a 1 s hold + 250 ms fade, `Reveal` re-arms on target change, `SetPinned` (right-click, `ToggleFaceOutlinesPinned`; persisted as session key `FaceOutlinesPinned`, kept across `Reset`) disables fading. `UpdateAnimations` invalidates only while a fade is progressing. Opacity reaches `PaintFaceOutline` through `FaceOutlinePresentation::opacity`; hit boxes are recorded regardless of opacity.
10. **Wheel pivot.** With FaceLock the wheel pivots on `transform::SourceToScreen` of the target face (clamped to the pane), so letterboxing, pan and rotation do not make the face drift.

### Face analysis data flow

1. Face jobs (only via `EnqueueFaceAnalysis`) look up the cache record first. A record supplies faces, per-face quality (only if `face_quality_version == kFaceQualityAnalysisVersion` and parallel to faces; otherwise reported as not analysed) and the persisted selection.
2. Detection (`DetectFacesFastBgra`) runs on a 512 px decode, converting to Gray8 when the detector supports it (Bgra8 fallback), drops obvious noise boxes, and ranks faces.
3. Quality runs on the worker for every face. Faces too small in the preview are re-judged once on a larger decode (`kFaceSharpnessDecodeSize` up to `kFaceQualityMaxDecodeSize`, the preview is released first). `faceBlurScanned` is true only when every face has a verdict; `faceBlurry` means any face is flagged (`FaceQualityFlagged`).
4. The UI merges results; a session/user selection wins over the persisted one. `PersistPhotoRecord` queues the record (faces, qualities with version, selection when known) on the worker's deduplicating cache-write queue.
5. **Cache schema v2.** `image_records` adds `face_quality`, `face_quality_version`, `face_selection`. v1 is migrated in place by `PersistentCache::MigrateSchema`; any other mismatch rebuilds the disposable cache. The upsert keeps the stored selection when the write carries no selection knowledge and the fingerprint is unchanged; a fingerprint change always drops it.

### File transaction

1. The UI discovers every supported media owner for each XMP companion, refuses external-owner theft, groups all selected owners plus the shared companion into an immutable atomic media plan, and checks conflicts, headroom, and fingerprints.
2. `TransactionCoordinator` admits only one file-or-metadata transaction and assigns its completion token.
3. `FileTransactionService` serializes execution away from the UI thread using an injected verified-operation backend.
4. `WindowsVerifiedFileOperations` locks the exact source object against writers/renames, copies through `CopyFileExW` with mid-file cancellation, verifies a private temporary object, and publishes that exact object through a handle-bound rename without overwrite.
5. A move retires the source only after the destination passes final verification.
6. A media group commits every owner and its shared companion or performs identity-guarded compensation; externally changed files are left untouched.
7. The UI integrates per-item/group results and records only truthful completed media groups in `HistoryStore`.

### Metadata transaction

1. The UI captures before/after snapshots and submits a batch.
2. Sidecar-only batches require no decoder disruption.
3. Direct-write batches block read-ahead and decoder handles only for affected normalized paths; unrelated image work continues.
4. The service performs each verified metadata write serially and records per-item completion.
5. Decoder exclusion is released once, including on cancellation or exception.
6. The UI updates the catalog and records only completed mutations in history.

### Ratings import / export

1. `core/ratings_exchange.*` (portable) owns the exact text format: literals, canonical tokens, `WriteDocumentUtf8`, the strict `ParseDocumentUtf8` (error enum + line number + verbatim line), `ReconcileWithFolder` (name folding injected; Windows uses invariant `LCMapStringEx` upper-casing) and `MissingLinesText`.
2. `ui/ratings_exchange_commands.cpp` orchestrates: the chooser (glass alert with a tertiary button), `IFileSaveDialog`/`IFileOpenDialog`, one `ratingsWorker_` thread (export: catalog snapshot + `ReadPhotoMetadata` for non-authoritative fields + atomic temp-file write; import: read ≤ 64 MiB + parse), completion via `WM_APP_RATINGS_WORKER_DONE`, generation checks, and the confirmation chain (`PendingGlassAction::RatingsImport*`, state in `pendingRatingsImport_`). The missing-files list is a `DocumentKind::MissingFiles` document window (plain-text RichEdit) that does not resolve the alert (`tertiaryKeepsOpen`).
3. Apply re-reconciles against the current catalog and calls `SubmitRatingsImport` (`metadata_history_and_file_safety.cpp`): one `MetadataTransactionPlan` (recordHistory=false), optimistic catalog edits, storage preflight, `SubmitMetadata`. `FinishMetadataTransaction` routes the token to `FinishRatingsImportTransaction` for the summary; incomplete items are restored by the shared path. Progress comes from `TransactionCoordinator::MetadataProgress()`; status text from `RatingsStatusLine()` via `FileOperationStatusLine`.

### Batch Processing

1. `core/batch_match_policy.*` (portable) owns:
   - list splitting (space/tab/`,`/`;`/CR/LF; double-quoted tokens keep spaces) and entry cleanup (trim, directory prefix, one trailing supported image extension);
   - case-insensitive dedupe (fold injected; Windows uses the shared `WindowsNameFold`) and `MergeNamesIntoText` for the folder load;
   - extension parsing/validation against `core/image_formats::AllSupportedImageExtensions()` (the same set `IsFormatAllowed` admits);
   - Complete and Partial matching (whole digit runs, leading-zero-insensitive and ordered; letter runs as substrings with an OSA distance ≤1 for runs ≥4);
   - one count per file, per-extension counts and the confirmation/unmatched text.
2. `ui/framework/macos_ui_framework_form.cpp` is a reusable owned modal form dialog. It has:
   - document-window chrome and a `PaintPanel` content panel;
   - `PaintSectionTitle` headers, labelled rows, D2D text inputs (`CreateTextInput`), `CreateButton` buttons and `TrackDropdown` dropdowns;
   - inline field errors, a status line and a footer whose primary button uses the accent look;
   - Tab/Shift+Tab/Enter/Ctrl+Enter/Esc keyboard handling, focus rings after keyboard navigation, a double-buffered paint and `WM_DPICHANGED` handling;
   - owner disable while shown, and deferred destroy so it can be closed from its own callback.

   `PickFolder` wraps `IFileOpenDialog` with `FOS_PICKFOLDERS`. Feature code only describes rows (`FormDialogSpec`).
3. `ui/batch_processing_commands.cpp` orchestrates:
   - one `batchWorker_` thread for the folder listing/merge and for the preflight (catalog snapshot → `ParseNameList` → `MatchFiles`, cancellable), completing via `WM_APP_BATCH_WORKER_DONE` with serial and generation checks;
   - the glass confirmation (`PendingGlassAction::BatchConfirm` / `BatchShowUnmatched` / `BatchNoMatches`). The dialog is hidden, with its owner re-enabled, while the alert is up. Declining or dismissing returns to it, and `ID_TIMER_BATCH_GLASS_WATCH` restores it if another alert replaced the batch alert.
4. Apply calls `SubmitRatingsImport(items, counts, L"Batch processing")`, whose items now carry `std::optional` fields ("No change" = empty). `ratingsImportIsBatch_` selects the batch wording in `FinishRatingsImportTransaction` / `RatingsStatusLine`. No Undo record; Redo is cleared.

### Unified metadata model and Thumbnail search

```
                    ┌────────────── writers (UI thread only) ──────────────┐
 metadata reader ─┐  │ DrainMetadataTransactionResults ← MetadataTransaction │
 (scan / XMP)     ├─►│   service ← Cull keys · Ratings Import · Batch · Undo  │
 comment field ───┤  │ SaveUserComment / RefreshUserComment                 │
 search hydration ┘  │ ApplySearchFactUpdates (capture time, comments)      │
                    └──────────────────────────┬────────────────────────────┘
                                               ▼ CatalogStore::EditPhoto / Append / ReplacePath
                     ┌───────────────────────────────────────────────────────┐
                     │ CatalogStore — PhotoItem = authoritative record        │
                     │ rating/pick/label + Knowledge, captureTime(+source),  │
                     │ userComment(+known), name/ext/modified                 │
                     │ MetadataEpoch() bumps only when facts change           │
                     │ Snapshot() → shared_ptr<const CatalogSnapshot> (epoch) │
                     └───────┬──────────────────────┬──────────────────────┬──┘
           FactsOf(PhotoItem)│ (UI thread)          │ Snapshot (workers)   │ catalog values
                             ▼                      ▼                      ▼
        ┌─────────────────────────────┐  ┌──────────────────────┐  ┌─────────────────────┐
        │ core::predicate engine       │  │ search worker:       │  │ Ratings Export       │
        │  Filter tab → FilterCriteria │  │  MatchesAll over a   │  │  store-first; re-reads│
        │  search box → search_query   │  │  snapshot (>2000)    │  │  files only for       │
        │  CullAllowed = Filter∧Search │  │  + fact hydration    │  │  Unknown fields       │
        └──────────────┬──────────────┘  └──────────┬───────────┘  └─────────────────────┘
                       ▼                            ▼ WM_APP_SEARCH_WORKER (serial/generation)
              RebuildVisiblePhotos  ◄───────  ApplySearchResult (bitset by catalog index)
                       │
            visible order · selection retain · scroll · reconcile sweep
                 ▲
     metadata_vocabulary.h: one token table for export/import, XMP, menus, Batch, status text and search keywords
```

- **Single authority.** All metadata writes reach `CatalogStore` through `EditPhoto` (or `Append` / `ReplacePath`) on the UI thread. Transaction results (Cull keys, Ratings Import, Batch, Undo/Redo, reconcile) keep their CAS/verification and stale-read protection unchanged. The store only stamps a new epoch when `SameMetadataFacts` reports a change.
- **Workers never touch `PhotoItem`.** They receive `CatalogStore::Snapshot()`, an immutable copy cached per epoch. Search results carry the epoch they were computed for. On every animation tick the UI compares that epoch with the store and re-evaluates (400 ms debounce) when it is stale.
- **Filters and search share one engine.** `IsCullAllowed` compiles the Filter tab into `predicate::Clause`s (`FilterCriteriaFor` / `MatchesFilter`), and the search box compiles to the same clause type. `CullAllowed = FilterAllowed && SearchAllows`. `SearchAllows` uses the last result bitset, or evaluates the record directly for rows that are not covered yet (new scan batches, prospective items). `scripts/checks/metadata_store_checks.cpp` proves that the Filter tab's semantics are unchanged across all 224 filter combinations.
- **Search pipeline** (`ui/thumbnail_search_controller.cpp`):
  1. `EN_CHANGE` starts a 150 ms debounce, then `ParseSearchQuery` runs.
  2. Evaluation runs inline for ≤2000 photos, otherwise on `searchWorker_` against a snapshot. Requests are serial-numbered: newer requests replace queued ones and stale results are dropped.
  3. `ApplySearchResult` calls `RebuildVisiblePhotos`, resets scroll on a new query, and arms the reconcile sweep.
  4. Hydration of capture time (bounded 64 KB, then 512 KB reads with shared access; file-modified fallback) and comments (`PersistentCache::GetPhotoComments`, batched) runs in batches of 32. Visible photos go first and the work only happens for queries that need those facts. Batches are generation-checked and applied with path verification. Facts the store already knows, such as a comment the user just saved, win over hydrated values.
- **UI.** The field is canvas-painted chrome (`PaintFloatingField`) around a borderless framework `CreateTextInput` child. That child sits directly above the canvas in z-order and below every other sibling, so the left pane always covers it. It is hidden outside Thumbnail view and while a glass alert is shown. The grid reserves a top lane (`ThumbnailLayoutMetrics::topInset`) for it.

### Undo and Redo

History replay uses the same transaction services as forward operations and retains media-group boundaries. Identity checks include stable file identity, change time, size, and sampled content inside the locked primitive immediately before mutation. If any group member fails, already-replayed members are compensated before the result is partitioned into completed and untouched history.

### Shutdown

The window stops accepting new work, requests cancellation, and waits only for safe transaction boundaries. Transaction services stop before the decoder worker they may coordinate with. Queue-owned completions can be discarded without freeing raw message payloads because private messages contain no object pointers.

## Dependency direction

```text
UI orchestration -> transaction/work/native services -> portable policy and stores
Canvas rendering -> graphics/cache state -> viewport planner
Worker/scanner -> completion queue -> UI result integration
File/metadata services -> verified primitives -> filesystem/container metadata
```

Portable core and portable transaction modules must not include Win32 UI headers.

## Enforced guardrails

`scripts/verify-source-portable.py` and `scripts/verify-source.ps1` enforce, among other contracts:

- a thin `main.cpp` and bounded compiled modules;
- no `.inl` implementation fragments;
- CMake registration for compiled modules;
- authoritative UI IDs, control catalog, palette, metrics, private messages, timers, subclass IDs, and property keys;
- one registered central UI framework, portable popup/toast/alert models, and framework-owned native component rendering;
- no feature-local rounded/text/button/menu/toast/alert/title-bar/card renderers or legacy popup/button subclasses;
- payload-free cross-thread Win32 completion messages;
- queue-owned worker and scanner results;
- serialized asynchronous file and metadata mutation;
- asynchronous identity-guarded Undo/Redo;
- one decoder handoff per direct metadata batch;
- fixed maximum-performance image decoding, priority-aware decode admission, and bounded UI result integration;
- path-scoped decoder/read-ahead exclusion and overlap-preserving thumbnail cancellation;
- constructor-safe worker startup, non-allocating worker callback snapshots, and service-stop ordering;
- Windows logoff cancellation/reopening and mid-file verified-copy cancellation;
- absence of generated, editor-backup, and obsolete source files.

## Change rules

- Keep startup and wiring in `main.cpp`; put behavior in an owner module or service.
- Do not reintroduce textual implementation inclusion or a new catch-all utility file.
- Extract deterministic policy and stateful invariants behind narrow compiled interfaces with tests.
- Keep caches disposable and original media authoritative.
- Preserve preflight, exclusive access, flush, verification, non-overwrite commit, and guarded rollback for media mutation.
- Add a deterministic regression test for every bug fix that can be reproduced without the full UI.
- Treat a successful native Windows warnings-as-errors build and source/binary verification as mandatory before public release.

### Thumbnail viewport priority (2026-08-16)

Thumbnail mode treats the currently visible thumbnail window as a foreground visual task. Visible thumbnails are queued at `Interactive` priority in strict left-to-right, row-major order. On viewport change, queued thumbnail decode work is discarded and rebuilt from the new viewport; in-flight thumbnail work that is no longer visible is canceled at worker cancellation checkpoints. Predictive/background thumbnails remain lower priority and are repopulated only after the current visible window. Thumbnail cache writes inherit the same priority and visible thumbnail cache writes are persisted ahead of metadata and lower-tier cache work.


## Startup staging

QuickSift intentionally keeps first-frame startup free of synchronous cache/database initialization.
The main window is created and shown first; persistent cache storage is initialized on a background
startup thread, then the UI thread receives `WM_APP_STARTUP_READY` and restores session state.
Diagnostic startup no longer performs forced write-probe flushes or cache-directory I/O before the
first frame. This keeps storage contention from making the application appear hung immediately after launch.

## Background completion pump (0.9.4.0)

Decode workers and the folder scanner never touch UI state. They push results (upload-ready BGRA pixels, scan batches, terminals) into the bounded `BackgroundCompletionQueue` and post a payload-free `WM_APP_BACKGROUND_COMPLETION` only when `TryClaimWake()` wins, so at most one wake is outstanding. The UI thread releases the claim when a drain slice begins and integrates completions for at most 4 ms / 48 items (1 ms / 4 items while dragging the scrollbar). If work remains and input or paint is pending, the next slice is scheduled through the one-shot `ID_TIMER_COMPLETION_PUMP` timer (which Windows delivers after input and `WM_PAINT`); otherwise a single wake is re-posted. Paint only reads `bitmapCache_` and issues non-blocking enqueue requests; it never waits on workers, decodes, or disk I/O (the Shell thumbnail fallback is reserved for failed decodes and limited to one uncached build per frame).

## View decode lane and thumbnail reconciliation (0.9.4.0)

Single/Compare decodes go through `Worker::EnqueueViewDecode` onto the view lane: a dedicated thread (`viewThread_`) with `viewQueue_` (foreground, navigation-epoch tagged, FIFO) and `viewPrefetchQueue_` (neighbours, epoch 0), and a private `DecodeBudget`. Foreground requests are latest-wins via the navigation epoch. Prefetch yields to any queued foreground job (`viewForegroundQueued_`) and must stay in the set published by `SetViewPrefetchPaths`. A foreground request for the image whose prefetch is running adopts it (`viewActiveAdoptedEpoch_`). The UI routes view requests through `RequestViewDecode`, which falls back to the pool outside Single/Compare. Paint never decodes. In Thumbnail view, `ReconcileVisibleThumbnails` re-enqueues visible ±1-row cells that have neither a bitmap nor a pending/in-flight job. It runs after settle, on retry-backoff expiry, and from the `ID_TIMER_THUMBNAIL_RECONCILE` backstop while cells are missing. Decode failures follow the bounded retry policy in `catalog_loading_and_result_integration.cpp` (`DecodeRetryState`).

## Priority model P0–P5 (0.9.4.0 polish)

Owner: `src/core/priority_policy.{h,cpp}` (portable, `scripts/checks/priority_policy_checks.cpp`).

| Tier | Work | How it is enforced |
|---|---|---|
| P0 UI | UI thread | No disk I/O or waits on the UI thread: post-write stamps and the storage preflight run on the metadata worker; cache-database calls from the UI wait at most `PersistentCache::kUiDatabaseWait` (30 ms), then defer the write or retry the read from a one-shot timer; the logger never writes to disk on the calling thread; the catalog snapshot is chunked and rebuilt incrementally instead of copied per epoch. |
| P1 MetadataWrite | rating / pick / label writes (interactive), then ratings import and batch apply | Metadata service thread at `THREAD_PRIORITY_NORMAL` (above every worker, never above the UI). Interactive edits use the urgent slot and run before a queued import/batch plan, or nested inside a running plan at a file boundary when their paths are disjoint. `Demand().SetMetadataWrites`/`SetInteractiveWritePending` make P3–P5 readers yield. |
| P2 ViewLoad | Single / Compare foreground decode | Dedicated view lane. While a foreground view decode is queued or running (`ViewForegroundActiveLocked`), pool lanes admit only interactive jobs and at most `kPoolCapDuringViewLoad` (1) thumbnail decode; everything else waits (8 ms bounded waits + `cv_` notify from the view lane). |
| P3 SearchRead | capture-time / comment hydration for search | Search worker yields between batches while `ShouldYield(SearchRead)` (P1 or P2 demand), bounded to 2 s so search never starves; batch size from `SearchHydrateBatch` (4 under write/view demand, 16 while the field is focused, 32 otherwise). Files under a write gate are deferred, not opened. |
| P4 VisibleThumb | visible thumbnails | Existing viewport-ordered interactive queue; capped to 1 concurrent decode during P2. |
| P5 Background | prefetch, face analysis, sharpness, cache maintenance, folder metadata, ratings export reads | Background-mode threads (`THREAD_MODE_BACKGROUND_BEGIN`, low memory priority, EcoQoS); ratings/batch workers at `THREAD_PRIORITY_BELOW_NORMAL`; face and analysis queues pause while a search is active or the search field is focused (`SearchPausesBackgroundAnalysis`; paused workers re-check at least every 250 ms); ratings export yields up to 1 s per file. |

The demand signal (`priority::Demand()`) is a set of atomics; readers poll `Snapshot()` between work units. `StarvationThreshold` documents how long each tier may be held off (P1 2 s, P2 3 s, P3 8 s, P4 5 s, P5 60 s); every yield loop in the tree is bounded by its tier's threshold or less.

## Metadata write lane: lock resolution flow (0.9.4.0 polish)

```text
UI edit (rating / pick / label)
  -> optimistic value shown immediately
  -> service idle and nothing queued?  submit directly
     otherwise                        PendingMetadataEdits.Record (coalesce: last value wins,
                                       per-file order kept, a value reverted to its "before"
                                       becomes a no-op) and FlushPendingMetadataEdits:
                                       urgent slot free -> SubmitUrgent, else wait for the
                                       running plan; a refused submit is re-queued
  -> metadata service thread, per file:
       BeginExclusivePathWrites: our decoders/readers release the file (P3-P5 also yield)
       write through the safe path (temp copy / sidecar, verify, atomic replace)
       failure classified by ClassifyWin32Error:
         InUse / DeviceBusy / AccessDenied -> quick phase: 50, 100, 200, 400, 800, 1000 ms ... (~3 s)
         InUse after ~3 s                  -> external phase: every 1 s up to 30 s; the UI shows
                                              "Waiting for <file> - in use by another app"
                                              (re-shown about every 6 s, logged once per episode)
         DeviceBusy / AccessDenied after 3 s -> stop for this file
         Conflict / LowStorage / ReadOnly / Verification / Unsupported -> stop now
                                              (corruption risk: nothing is overwritten)
       waits use condition_.wait_for, so Stop, Cancel and Skip interrupt them at once
  -> result on the UI thread (FinishMetadataTransaction):
       success: apply the worker's post-write stamps, rebase queued edits
       failure: restore the previous value, drop queued edits for that path,
                show the localized UserErrorCode message (one clear cause) or a summary;
                every failure in use by another app -> glass choice Retry / Skip
       then flush the next coalesced batch
```

Interactive writes never block the UI. A nested urgent edit runs only when its paths are disjoint from the running plan's gated paths; otherwise it runs right after the plan. Known limits: ratings import reports in-use files in its summary without a Retry; the exclusive gate is held during the 30 s external wait, so the view lane cannot decode that file meanwhile.

## Lock order (0.9.4.0 polish)

Locks may only be taken in increasing rank. Ranks are declared in `src/core/lock_order.h`; builds with `QS_DEBUG_LOCK_ORDER` check them at run time (`QS_LOCK_ORDER(rank)` next to the lock; currently used by the logger and the cache database), and `scripts/checks/lock_order_checks.cpp` tests the checker.

| Rank | Mutex | Rules |
|---|---|---|
| 20 | `MetadataTransactionService::mutex_` | Never held while calling callbacks or the completion sink. Retry waits release it (`condition_.wait_for`). |
| 21 | `FileTransactionService::mutex_` | Same rules as the metadata service. |
| 30 | search `searchMutex_` | Never held over file I/O, cache-database calls or `PostMessageW`. |
| 31 / 32 | `ratingsWorkerMutex_` / `batchWorkerMutex_` | Held only to move a result in or out. |
| 40–43 | Worker `blockedMutex_`, `readAheadGateMutex_`, `desiredMutex_`, `mutex_` | `BeginExclusivePathWrites` takes them one after another, never nested. `PopWork` and all dequeue decisions run under `mutex_` (may log). |
| 50 | `PersistentCache::databaseMutex_` (timed) | UI callers use `try_lock_for(30 ms)`. Every holder drains deferred UI writes first. |
| 51 | `PersistentCache::deferredMutex_` | Leaf except for logging. |
| 60 | logger stream mutex | Disk writes only; taken by the writer thread, Critical sync drains and shutdown. |
| 61 | logger state mutex | Innermost: any thread may log while holding any lock above. Crash handler uses try-lock only. |

Lock-free shared state: `priority::Demand()` atomics, `searchMessagePosted_` / `searchUnconsumed_`, worker counters, the crash-state snapshot. `TransactionCoordinator` and `CatalogStore` (including `Snapshot()` chunk rebuilds) are UI-thread objects; workers only receive immutable `CatalogSnapshot` chunks via `shared_ptr<const ...>`.

Shutdown/join paths: WM_CLOSE flushes or (after confirmation) discards queued edits, then the coordinator stops services (`Stop()` notifies `condition_`, so a 30 s wait ends at once), workers are joined after their stop flags and condition variables are signalled, and the ratings/batch/search workers are stopped before the window is destroyed. Completion sinks capture the `HWND` by value, so a late post after destruction is harmless.

## Localization (0.9.4.0 polish)

`Localizer::Text` (`src/localization.cpp`) looks up a hashed index of exact keys from both tables, then runs the fragment pass longest-first (a shorter fragment listed earlier can no longer pre-empt a longer one). `scripts/check-localization.py`, wired into `scripts/verify-source-portable.py`, fails the build on missing Vietnamese text, English literals passed to pre-localized sinks (`ShowGlassAlertText`, `ShowGlassChoice`, `ShowToastText`, `SetWindowTextW`), concatenated keys, untranslated (identical) entries, placeholder mismatches and duplicate keys. The ratings export file format (header, footer, column names, tokens) is deliberately English and never goes through `Tr()`; see USER_GUIDE "Import / Export Ratings".
