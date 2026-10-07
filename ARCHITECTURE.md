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
- `face_policy.*` — deterministic primary-face ranking by area, frame-center proximity, then stable coordinates.

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
