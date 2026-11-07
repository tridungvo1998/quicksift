# QuickSift Code Guide

This guide is the plain-English map for humans and coding assistants. Read it before moving code, fixing a bug, or adding a feature.

The central rule is simple:

> **Every decision has one owner. Put new code beside that owner. Do not create a second copy of the same rule.**

If a source comment says `CODE GUIDE`, it points back to this document. The comment should stay short; the explanation belongs here.

This file is the hub; every other document defers to it. Folders with their own `README.md` (`src/ui`, `src/app/state`, `src/review`, `src/metadata`) map their files. When two documents disagree, the code wins, then this guide; fix the other document.

## Documentation map

| You want to… | Read |
|---|---|
| Use QuickSift: features, shortcuts, settings | [`USER_GUIDE.md`](USER_GUIDE.md) |
| Understand or change the code: owners, rules, recipes | [`CODE_GUIDE.md`](CODE_GUIDE.md) (this file) |
| Module boundaries and runtime flows | [`ARCHITECTURE.md`](ARCHITECTURE.md) |
| Find the file that owns a change, fast | [`DEVELOPER_GUIDE.md`](DEVELOPER_GUIDE.md) |
| Build, sign or package; what a build error means | [`BUILDING.md`](BUILDING.md) |
| Automated checks and manual tests | [`TESTING.md`](TESTING.md) |
| Colours, spacing, type and motion tokens | [`docs/DESIGN_SYSTEM.md`](docs/DESIGN_SYSTEM.md) |
| Which UI file owns a window, control or menu | [`src/ui/README.md`](src/ui/README.md) |
| The application's state structs | [`src/app/state/README.md`](src/app/state/README.md) |
| What changed in each release | [`CHANGELOG.md`](CHANGELOG.md) |

---

## 0. Design philosophy and priorities

When goals conflict, they rank in this order:

1. **The photographer's files.** Original media is authoritative. Mutation is verified, non-overwriting and guarded by rollback; caches are disposable.
2. **A responsive UI thread.** The UI thread (P0) never does disk I/O, decoding or blocking waits.
3. **What the user is looking at.** Foreground work outranks visible work, which outranks predictive work, which outranks idle work (§8.1).
4. **One checkable owner per rule.** Decisions live in portable code that `scripts/checks/` can test off Windows.
5. **Throughput and polish**, within the limits above.

Rules that follow from this:

| Do | Never |
|---|---|
| Give each decision one owner (§2, §3) and extend it. | Copy a list, constant, rule or renderer into a second place. |
| Build an immutable plan in `src/ui` and submit it through `TransactionCoordinator` (`src/transactions`). | Mutate files or metadata from UI code or on the UI thread. |
| Let workers mirror review epochs to cancel stale work and push owned results into `BackgroundCompletionQueue`. | Let a worker own or edit review state, `PhotoItem`, `CatalogStore`, an `HWND` or a UI container. Workers read `CatalogSnapshot` only. |
| Put portable policy in `src/core` or `src/review` (no `windows.h`). | Put Win32 calls in `src/core`/`src/review`, or policy math in a message handler. |
| Put native orchestration in `src/ui`, `src/work` and `src/transactions`. | Grow `src/main.cpp` or the public facade `src/app/quicksift_application.h`. |
| Start app-owned threads with `token = workerLifetime_.Issue()`, post only through `PostFromWorker`, and join them in `StopAllWorkersForTeardown`. | Capture raw `this` in a thread, `PostMessageW` a worker completion directly, or tear down services before workers are joined. |
| Keep metadata (rating/pick/label) reads in their own lane below decode, with guaranteed progress (`core/metadata_lane_policy.h`). | Let metadata reads starve, or let them delay visible decoding. |
| Take layout sizes from named constants (`ui_layout_constants.h`, `ui_layout_metrics.h`). | Pass integer literals to `DipToPx`/`px`/`Px` in layout helpers. |
| Log a contained failure with `QS_LOG_AND_CONTINUE(category, site)`, or mark it `QS_CATCH_SILENT_BY_DESIGN("reason")`. | Write an empty `catch (...) {}` in the Windows app layer. |
| Name files after the one responsibility they own. | Add `helpers`, `misc`, `utils` or a second controller-shaped file. (`src/app/state/app_state_common.h` holds shared includes only.) |
| Pin an important invariant with a rule in **both** verifiers (§16). | Rely on a comment alone to keep an invariant. |

### How an AI (or a new human) should approach a change

1. Read §0–§3 of this guide; skim [`ARCHITECTURE.md`](ARCHITECTURE.md) for the flow you touch.
2. Find the owner row in §2. If none fits, create a responsibility-named owner and add a row; never a helper file.
3. Change only that owner. Callers adapt; they do not get a private copy of the rule.
4. If the rule is deterministic, put it in portable code and extend the matching `scripts/checks/*.cpp` program.
5. Run the checks in §14: both verifiers, `check-localization.py` and the check programs. Report anything you could not run, especially the native Windows build.
6. If users can see the change, update `CHANGELOG.md`, the EN and VI Help text in `src/localization.cpp`, and `USER_GUIDE.md`.
7. Never claim a Windows build or run that did not happen (§13).

---

## 1. The 60-second map

QuickSift has five cooperating layers:

```text
Windows starts the process
        |
        v
src/main.cpp                         Composition and startup only
        |
        v
src/app/quicksift_application.h      Public PIMPL facade (no state)
src/app/quicksift_application_internal.h
        |                            QuickSiftApplicationImpl: private orchestration surface
        +-- src/app/state/*.h        Its plain data, split into data-only structs
        |
        v
src/ui/*.cpp                         Native windows, commands, rendering, UI orchestration
        |
        +----------> src/work/*      Background decode, scanning, owned completion queues
        |
        +----------> src/transactions/* Serialized file/metadata mutation and replay
        |
        +----------> native services Cache, codecs, diagnostics, runtime helpers
        |
        v
src/core/* and src/review/*          Deterministic policy, review state, bounded stores
```

Use this shortcut:

- **A calculation or decision with no Win32 dependency** goes in `src/core`; coordinated thumbnail/zoom/Compare/FaceLock state and geometry go in `src/review`.
- **A reusable Windows-facing service** gets its own `.h/.cpp` pair under the relevant folder.
- **A main-window behavior** goes in the responsibility-named file under `src/ui`.
- **Background decoding/queueing** goes in `src/work/background_work_engine.*`; folder enumeration goes in `src/work/folder_scanner.*`.
- **File or metadata mutation** goes through the appropriate service under `src/transactions`.
- **Startup and object wiring only** belong in `src/main.cpp`.

Never add feature behavior or state to `src/app/quicksift_application.h`; it is a thin public PIMPL facade. Private Win32 orchestration method declarations belong in `src/app/quicksift_application_internal.h`. New plain fields belong in the matching struct in `src/app/state/` (see [`src/app/state/README.md`](src/app/state/README.md)). Independently testable invariants belong in their own store or service.

---

## 2. Where new code goes

Start with the first matching row.

| You are changing... | Put the code here |
|---|---|
| File-extension recognition | `src/core/image_formats.*` |
| Catalog format/date/rating/label/pick filtering or sorting | `src/core/catalog_policy.*` |
| Decoded-memory reservations | `src/core/decode_budget.*` |
| Worker priority ranking or queue promotion | `src/core/work_queue_policy.h` |
| Work tiers P0–P5, foreground-demand signal, yield and starvation rules | `src/core/priority_policy.*` |
| Metadata read lane: when a read may start (750 ms aging, 10 s cap behind pending writes) | `src/core/metadata_lane_policy.h` |
| Metadata read terminal states (apply, bounded retry, mark unreadable) | `src/core/metadata_progress.h` |
| Exiv2 gate admission (pending writes hold readers; the writer's own reads pass) | `src/core/metadata_gate_policy.h` |
| Visible-first order of the pending metadata queue | `src/core/metadata_queue_order.h` |
| Merging background-hydrated facts into the catalog | `src/core/hydrated_facts.h` |
| Distinct values behind the Date/Hour/ISO/Aperture/Focal/Shutter/Camera filter menus | `src/core/filter_value_index.h` |
| XMP sidecar write plan | `src/core/sidecar_write_policy.h` |
| Adaptive decode timeout per attempt and file type | `src/core/decode_timeout_policy.h` |
| Largest embedded JPEG preview in a RAW file | `src/core/embedded_jpeg_scan.h` |
| Interactive metadata edit coalescing | `src/core/metadata_edit_queue.h` |
| Write-lane failure classification and lock-resolution retry | `src/core/write_retry_policy.h`; UI side `src/ui/metadata_write_lane_controller.cpp` |
| "What is QuickSift waiting for?" text model | `src/core/blocking_wait.h`; dialog in `src/ui/blocking_wait_controller.cpp` |
| Performance HUD text model | `src/core/hud_snapshot.h` |
| Left-pane scroll geometry and caption-fit (truncation) test | `src/core/pane_scroll_policy.h` (`core::pane`, `core::caption::Truncated`) |
| Left-pane timing records (`[Pane]` / `[Startup]` lines) | `src/core/pane_timing.h` |
| Early theme value shared by the pre-window read and `LoadSession` | `src/core/presentation_settings.h` |
| Debug lock-order ranks | `src/core/lock_order.h` |
| Undo/redo item partitioning | `src/core/history_partition.h` |
| Rename-stable file identity for guarded Undo | `src/core/stable_file_identity.h` |
| Temporary boolean state that must survive exceptions | `src/core/scoped_boolean_flag.h` |
| Hardware-based worker/cache sizing | `src/core/system_profile.*` |
| Thumbnail/Single/Compare identity, zoom/FaceLock/sync transitions, or review epochs | `src/review/review_state_model.*` |
| Scroll direction, velocity, or row prediction math | `src/review/thumbnail_prefetch_policy.*` |
| Thumbnail row/column/hit-test geometry | `src/review/thumbnail_layout.*` |
| Zoom/pan/rotation geometry | `src/review/view_transform_policy.*` |
| Stale navigation/thumbnail result admission | `src/review/review_async_policy.*` |
| Narrow XMP property reading and insertion/replacement | `src/metadata/xmp_simple_property.*` |
| JPEG APP1/EXIF orientation parsing | `src/metadata/jpeg_orientation.*` |
| Parsing metadata text into safe values | `src/metadata/metadata_value_parser.*` |
| Metadata-folder component map and extension rules | `src/metadata/README.md` |
| Canonical executable-adjacent runtime-data paths | `src/platform/application_data_paths.h` |
| Session logs, manual log export, crash reports | `src/diagnostics/diagnostic_log.*` |
| Which diagnostic files are old enough to delete (7-day age, own names only, never the current log, never a reparse point) | `src/core/log_retention.h` (decision); `diagnostics::CleanupOldDiagnosticFiles` in `src/diagnostics/diagnostic_log.cpp` (listing and deleting) |
| Previous-session log browser: which logs are listed (own names, not the current session, no reparse points), newest-first order, menu labels, the 2 MiB read cap | `src/core/session_log_browser.h` (decision); `diagnostics::ListPreviousSessionLogs` / `ReadSessionLogTail` in `src/diagnostics/diagnostic_log.cpp` (read-only file access); `src/ui/session_log_browser_controller.cpp` (button, worker, dropdown, viewer window) |
| Crash-incident count cap (newest 20) and the 16 MiB session-log cap | `src/diagnostics/diagnostic_log.cpp` (`kRetainedCrashIncidents`, `kMaximumSessionLogBytes`) |
| Logging a failure inside `catch (...)` and continuing | `src/core/log_and_continue.h` (portable message format and per-site limit); `src/diagnostics/log_and_continue.h` (`QS_LOG_AND_CONTINUE`, `QS_CATCH_SILENT_BY_DESIGN`) |
| Rate limiting a repeating log line | `src/core/log_rate_limiter.h` |
| Log category names, `file=` / `path_id=` fields | `src/diagnostics/log_categories.h` |
| Internal error code to log event and user message | `src/core/user_error_catalog.h` |
| Shutdown token for app-owned worker threads | `src/core/worker_lifetime.h`; the app holds `workerLifetime_` and `PostFromWorker` (`src/app/quicksift_application_internal.h`) |
| SQLite and thumbnail cache persistence | `src/persistent_cache.*`; conservative derivative free-space query reuse: `core/derivative_space_budget.h` |
| Transparent bitmap-key probes, eviction rank and incoming-byte cap | `core/bitmap_cache_key.h`, `core/cache_eviction_policy.h`; UI scratch ownership stays in the graphics controller |
| Compare layout invalidation | `core/compare_layout_key.h`; cached cells in the canvas controller (bounds, DPI, generation, mode, ordered identities and resolved aspects) |
| Latest-wins immutable session persistence and final drain | `core/snapshot_writer.h`; UI builds snapshots in `SaveSession`, native write callback uses the existing atomic text publisher |
| Shell fallback isolation | `work/shell_preview_service.*`; identity/retry/size bounds in `core/shell_preview_policy.h`; private self-child dispatch in `main.cpp` before normal startup |
| Scan batch growth and metadata queue preparation rollback | `core/queue_append_transaction.h`; scanner grows bounded batches; `OnScanBatch` commits the queue guard only after catalog commit |
| WIC/RAW/AVIF/WebP decoding | `src/bundled_codecs.*` |
| Embedded metadata read/write | `src/embedded_metadata.*` |
| Face detection adapter | `src/face_detector.*` |
| Face capability and bounded retry policy | `src/core/face_detection_policy.*`, `src/ui/face_analysis_controller.cpp` |
| Process security, filesystem identity, hardware/storage helpers | `src/runtime_support.*` |
| Catalog photo storage, path lookup, and visible positions | `src/core/catalog_store.*` |
| Bounded Undo/Redo storage and memory accounting | `src/core/history_store.*` |
| Selected-path membership and range anchors | `src/core/selection_store.*` |
| Background decode queues and read-ahead | `src/work/background_work_engine.*` |
| Folder enumeration and bounded scan batches | `src/work/folder_scanner.*` |
| Worker/scanner result ownership | `src/work/background_completion_queue.h` |
| Portable media-group sequencing and rollback | `src/transactions/file_transaction.*`, `file_transaction_group_support.h` |
| Atomic grouped File Undo/Redo replay | `src/transactions/file_history_replay.*`, `file_history_group_replay.*` |
| Serialized file execution/cancellation | `src/transactions/file_transaction_service.*` |
| Exact-handle Windows copy/move/delete/publication primitives | `src/transactions/verified_file_operations_windows.*`, `verified_file_move_windows.cpp`, `src/platform/locked_file_publication_windows.h` |
| Serialized metadata writes and replay | `src/transactions/metadata_transaction_service.*` |
| Cross-domain transaction tokens, cancellation, and shutdown | `src/transactions/transaction_coordinator.*` |
| Portable popup/toast/alert state and hit testing | `src/ui/framework/ui_models.*` |
| Reusable native controls, component painting, dropdowns, toasts, alerts, cards, badges, and title bar | `src/ui/framework/macos_ui_framework*` |
| UI colors, contrast, DPI conversion, and low-level drawing primitives | `src/ui/ui_design_system.*` |
| Shared UI dimensions used across areas (title bar, status height, left-pane widths, radii) | `src/ui/ui_layout_metrics.h` (`quicksift::ui::layout::k*Dip`) |
| Per-area layout numbers (window minimum size, left-pane sections, Settings rows, status bar, document windows) | `src/ui/ui_layout_constants.h` (namespaces `main_window`, `left_pane`, `status_bar`, `document`; includes `ui_layout_metrics.h`) |
| Overflow tooltip for truncated button captions (`QuickSift.OverflowTip`) | `src/ui/framework/macos_ui_framework_gdi.cpp` (framework-owned; feature code never adds its own) |
| Design tokens (radius/spacing/motion/type scale, glass, accessibility policy) | `src/ui/ui_design_tokens.h` (`docs/DESIGN_SYSTEM.md`) |
| Stable command/control IDs | `src/ui/ui_command_ids.h` |
| Persistent main-window buttons and membership | `src/ui/ui_control_catalog.h` |
| Window messages, timers, subclass IDs, class names | `src/app/application_support.h` |
| Help/About/Diagnostics/popup window models | `src/ui/document_and_menu_models.cpp` |
| Theme, localization, help/about/diagnostics windows | `src/ui/app_theme_documents_and_localization.cpp` |
| Main-window message routing, title bar, fullscreen, DPI | `src/ui/main_window_chrome_and_messages.cpp` |
| Main-window component declarations, semantic state mapping, layout, and left hover-pane composition | `src/ui/main_window_controls_layout_and_painting.cpp` |
| Dropdown requests, command dispatch, folder tree, status text | `src/ui/popup_menus_commands_and_folder_tree.cpp` |
| Folder-load results and worker-result admission | `src/ui/catalog_loading_and_result_integration.cpp` |
| Graphics devices and image-cache admission/eviction | `src/ui/graphics_resources_and_image_cache.cpp` |
| Thumbnail/single/compare canvas drawing | `src/ui/canvas_rendering_and_overlays.cpp` |
| Turning a model-owned viewport plan/epoch into decode requests | `src/ui/viewport_prefetch_controller.cpp` |
| Mouse/keyboard navigation, zoom, session persistence | `src/ui/navigation_input_and_session.cpp` |
| Transaction-plan construction and result/history integration | `src/ui/metadata_history_and_file_safety.cpp` |
| Settings/filter menus and high-level file commands | `src/ui/settings_filters_and_file_commands.cpp` |
| Settings "Write metadata into RAW / JPG/PNG / TIFF files" toggles | `src/ui/settings_filters_and_file_commands.cpp` (`ID_SETTING_METADATA_*`) |
| Metadata write lane on the UI side (coalescing, urgent slot, retry and Skip) | `src/ui/metadata_write_lane_controller.cpp` |
| Thumbnail search box | `src/ui/thumbnail_search_controller.cpp` |
| Keyboard shortcut table (bindings, sheet rows, `Find`) | `src/core/keyboard_shortcuts.h`; dispatch in `DispatchShortcut` (`src/ui/navigation_input_and_session.cpp`) |
| Rating / pick / reject ↔ XMP (Lightroom, Capture One) and the pick label | `src/core/xmp_interop_policy.h` (every reader and writer calls it) |
| Auto-advance rules / Caps Lock | `src/core/auto_advance_policy.h`; `src/ui/auto_advance_controller.cpp` |
| Session settings migration (`SettingsVersion`, RAW preview default) | `src/core/settings_migration.h` |
| `?` shortcut sheet, first-run tip, Caps Lock pill | `src/ui/shortcut_sheet_and_tips_overlay.cpp` |
| Pinned command pane | `src/ui/left_pane_pin_controller.cpp` |
| Full-RAW-on-zoom exception to JPG-preview mode | `RawPreviewOnlyFor` / `UpdateFullRawOverride` (`src/ui/graphics_resources_and_image_cache.cpp`), `Worker::SetFullRawOverridePath` (`src/work/background_decode_policy.cpp`) |
| Import / Export Ratings commands | `src/ui/ratings_exchange_commands.cpp` |
| Batch Processing commands | `src/ui/batch_processing_commands.cpp` |
| Performance-policy plumbing and telemetry | `src/ui/performance_policy_controller.cpp` |
| Title-bar relayout skip guard (`TitleLayoutKey`, `MarkTitleLayoutDirty`) | `src/ui/main_window_controls_layout_and_painting.cpp` |
| Background worker job loop (`Worker::Run` and its named helpers) | `src/work/background_work_engine.cpp` (`JobActivity`, `JobRunContext` in `background_work_engine.h`) |
| RAW decode-mode switching and visual decode coalescing | `src/work/background_decode_policy.cpp` |
| Sidecar read/write and metadata precedence | `src/app/application_support.cpp` (`ReadSidecarMetadata`, `WritePhotoMetadataPatch`, `ReadPhotoMetadata`) |
| Atomic text-file publication (XMP, session) | `src/app/application_text_io.cpp` |
| Plain `QuickSiftApplicationImpl` fields | the matching struct in `src/app/state/*.h` |
| Private orchestration method declarations, COM objects, services | `src/app/quicksift_application_internal.h` |

When no row fits, do not create `helpers.*`, `misc.*`, or `utils.*`. Name the new component after the single responsibility it owns and add it to this table.

---

## 3. The authoritative owners

These rules must exist in exactly one place:

- Supported JPEG and RAW extensions: `core/image_formats`.
- Catalog filtering and sorting: `core/catalog_policy`.
- Work-priority order and promotion mechanics: `core/work_queue_policy`.
- Coordinated review-session state: `review/review_state_model`.
- Predictive thumbnail row math: `review/thumbnail_prefetch_policy`.
- Thumbnail geometry: `review/thumbnail_layout`.
- Zoom/rotation geometry: `review/view_transform_policy`.
- Async stale-result admission: `review/review_async_policy`.
- UI IDs: `ui_command_ids.h`.
- Persistent main-window control declarations: `ui_control_catalog.h`.
- Shared visual roles and low-level primitives: `ui_design_system`.
- Reusable UI component creation, state behavior, and rendering: `ui/framework`.
- Shared dimensions: `ui_layout_metrics.h`; per-area layout numbers: `ui_layout_constants.h`.
- Work tiers and yielding: `core/priority_policy`; metadata read-lane admission: `core/metadata_lane_policy.h`.
- Worker shutdown token: `core/worker_lifetime.h`; the shutdown order: `OnMainWindowDestroyed` (`ui/main_window_chrome_and_messages.cpp`).
- Diagnostic retention: `core/log_retention.h` deletes files older than 7 days at startup. The crash-incident cap (newest 20) and the 16 MiB session-log cap are in `diagnostics/diagnostic_log.cpp`. There is no session-log count limit.
- Log-and-continue message and rate limit: `core/log_and_continue.h`.
- Caption truncation test: `core::caption::Truncated` (`core/pane_scroll_policy.h`).
- Canonical rating/pick/label vocabulary: `core/metadata_vocabulary.h`; filter and search predicate engine: `core/photo_predicate`.
- `QuickSiftApplicationImpl` plain state: `src/app/state/*.h`.
- Timer IDs, private messages, subclass IDs, and property names: `src/app/application_support.h`.
- Public application ABI: `src/app/quicksift_application.h`.
- Private Win32 orchestration methods, COM objects and services: `src/app/quicksift_application_internal.h`.

Before writing a list, switch, constant, or lookup table, search for its owner. If an owner already exists, extend it instead of copying it.

Bad:

```cpp
bool IsRawHere(std::wstring_view extension) {
    return extension == L".cr3" || extension == L".nef";
}
```

Good:

```cpp
return quicksift::core::IsCameraRawExtension(extension);
```

---

## 4. How the application is assembled

`src/main.cpp` is a thin process entry point. It initializes process-wide facilities, constructs `QuickSiftApplication`, and runs startup/shutdown in `wWinMain`.

CMake separately compiles every `.cpp` implementation unit. UI modules declare their private implementation methods in `src/app/quicksift_application_internal.h`; no implementation source is textually included into another source file. `.inl` implementation fragments are forbidden by verification.

Ordinary native components have one rendering owner: `MacOsUiFramework`. Feature modules map application state into presentation structs and call the framework. They may own placement and application semantics, but must not create a second button, dropdown, toast, alert, panel, title-bar, badge, card, or shared-text renderer. Specialized photo bitmap/tile composition remains in the canvas module.

The controller coordinates Win32 state, while independent invariants live in concrete owners: `CatalogStore`, `HistoryStore`, `SelectionStore`, `BackgroundCompletionQueue`, `FolderScanner`, and `TransactionCoordinator` (which owns the file and metadata services). Add another service when a stateful responsibility can be independently tested and has a narrower contract than the window controller.

### 4.1 Building, testing, and opening Visual Studio

The build has one owner: `scripts/build.ps1`. The root `.cmd` files are intentionally tiny entry points:

- `build.cmd` verifies the source and builds a Debug configuration by default.
- `build-release.cmd` verifies, builds, checks, and packages Release.
- `open-in-visual-studio.cmd` configures an external solution and opens it.

When any of them fails, the trap in each script prints `Build failed:` (one-sentence cause), `What to do:` (1–3 steps) and `Technical detail:`, and keeps the non-zero exit code. Causes and steps live in one table, `scripts/build-failure-help.ps1` (ASCII only). Add a new known failure there, not in the calling script.

Do not add dependency discovery, generator selection, signing, or packaging logic to another script. Extend `scripts/build.ps1` and add a regression/source-verifier rule when practical.

CMake remains the only project definition. Never hand-edit or commit generated `.sln` or `.vcxproj` files. The generated solution lives under `%LOCALAPPDATA%\QuickSiftBuild`, and CMake declares `QuickSift` as the startup project.

`CMakePresets.json` exists for Visual Studio folder mode and command-line users who already have `VCPKG_ROOT`. The launcher scripts are the easiest path because they discover Visual Studio, CMake, and vcpkg.

---

## 5. Important runtime stories

Understanding these flows is faster than reading files alphabetically.

### 5.1 Opening a folder

```text
User chooses folder
  -> settings_filters_and_file_commands / command layer
  -> catalog_loading_and_result_integration::LoadFolder
  -> FolderScanner in src/work/folder_scanner
  -> core/catalog_policy accepts or rejects entries
  -> scanner pushes bounded batches into BackgroundCompletionQueue
  -> a payload-free message wakes the UI to drain the queue
  -> UI creates stable PhotoItem entries
  -> visible catalog is filtered/sorted
  -> viewport prefetch is scheduled
```

Bug clues:

- Missing formats: check `core/image_formats` and `core/catalog_policy`.
- Wrong order/filter: check `core/catalog_policy` and visible-catalog rebuild.
- UI freeze: look for filesystem or decoder work accidentally running on the UI thread.
- Results from an old folder: check generation/cancellation values when admitting scan results.
- Metadata queue never finishes: check both normal results and the no-allocation metadata-abort message.

### 5.2 Scrolling thumbnails

```text
Wheel/keyboard/scrollbar changes offset
  -> navigation_input_and_session observes velocity
  -> thumbnail_prefetch_policy ComputeScrollLoadBudget(velocity, cell size)
  -> Fling: placeholders/cache only; publish viewport to cancel stale jobs
  -> Coasting: size-capped visible enqueue; no predictive
  -> Settled: full visible then predictive work
  -> viewport_prefetch_controller asks PrefetchPlanner for ordered rows
  -> background_work_engine admits work under scroll concurrent cap
  -> result enters BackgroundCompletionQueue
  -> a payload-free message wakes the UI to drain owned results
  -> graphics_resources_and_image_cache admits the bitmap
  -> canvas invalidates and redraws
```

Bug clues:

- Blank rows while scrolling: inspect viewport-plan submission and stale-job replacement.
- Lag / viewport jumps on fast scroll: inspect `ComputeScrollLoadBudget`, fling placeholders, and size-aware concurrent caps.
- Wrong direction preloaded: inspect velocity expiry and reversal logic in `review/thumbnail_prefetch_policy`.
- Old rows keep winning: inspect queue promotion and removal from the old priority deque.
- Memory spikes: inspect `decode_budget`, target dimensions, and bitmap-cache admission.
- Large cells heavier than small during fling: inspect `ConcurrentThumbnailDecodeCap`.

### 5.3 Reading metadata

```text
Background worker requests metadata
  -> embedded_metadata reads container metadata
  -> the shared XMP accessor ignores comments and rejects ambiguous/unsupported forms
  -> bounded JPEG/EXIF helpers validate every offset inside its APP1 segment
  -> invalid or unsupported values are rejected
  -> result returns to the UI thread
  -> PhotoItem and persistent cache are updated
```

Do not parse ratings, labels, or pick states with ad-hoc `stoi`, regex, or clamping at a new call site. Use the metadata modules.

### 5.4 Writing metadata

```text
User changes rating/label/pick
  -> UI captures before/after snapshots
  -> MetadataTransactionService accepts one serialized batch
  -> direct-write paths acquire one decoder-exclusion handoff
  -> sidecar or temporary embedded copy is updated away from the UI thread
  -> output is flushed, reopened, and verified
  -> commit replaces the original only after verification
  -> service returns per-item completion
  -> UI/cache/history integrate only completed mutations
```

Original media is authoritative. A cache record is never proof that a media mutation succeeded.

Do not simplify this flow to “write and hope.” Preserve:

1. Preflight.
2. Exclusive access.
3. Temporary output where required.
4. Flush.
5. Verification.
6. Commit.
7. Guarded rollback.
8. Diagnostic context.

### 5.5 Copying or moving files

```text
High-level file command
  -> gather primary files and sidecars
  -> preflight destination and free space
  -> submit immutable plan to FileTransactionService
  -> locked Windows primitive copies to a private temporary destination
  -> flush and byte-verify
  -> publish without overwrite
  -> remove source only after final destination verification
  -> return per-item completion and identities
  -> UI records only completed operations for guarded Undo
```

Never replace this with a naked `std::filesystem::copy_file` or delete the source before the destination is verified.

### 5.6 Logging and crashes

```text
Code reaches a useful context boundary
  -> QS_LOG_* / QS_LOG_EVENT records category, event, thread, sequence and monotonic time
  -> bounded in-memory ring remains available for UI/crash diagnostics
  -> a dedicated disk-writer thread batches UTF-8 writes without blocking image workers
  -> periodic resource snapshots capture RAM/process/CPU state

Unhandled crash
  -> re-entry-safe crash handler
  -> text report + minidump + recent in-memory log entries
  -> subsystem state (priority demand snapshot) from lock-free atomics
  -> queued disk entries are flushed when the log locks are available (try-lock only)
  -> LATEST_CRASH.txt points to the newest report
```

Use QS_LOG_EVENT for structured diagnostics. Include operation, job kind, priority, generation/epoch, durations, cache hit/miss, dimensions, and relevant Win32 error codes at context boundaries. Avoid logging credentials, image pixels, or full paths unless the path itself is required to reproduce the issue. The logger is asynchronous on disk so high-volume image workloads do not serialize on file I/O. Log once where the operation, path, and Windows error still have meaning; do not spam the same low-level error at every stack level.

Rules added in 0.9.4.0 (polishing pass):

- **Categories** come from `diagnostics/log_categories.h`: `Search`, `MetadataStore`, `Batch`, `RatingsIO`, `FaceLock`, `ViewLane`, `ThumbReliability`, `Scheduler`, `WriteLane`, `DbMigration`, plus the existing `Metadata`, `Worker`, `UI`, `Lifecycle`, `Catalog`. Use the constant, not a new string.
- **Paths**: log `file=` (`LogFileName`, the file name only) and `path_id=` (`LogPathId`, an FNV-1a hash of the case-folded full path, stable within and across sessions so events about one file can be correlated). Never log the full path or user text (search queries, comments): log their shape instead (`clauses=`, `chars=`).
- **Error codes**: user-visible failures use `core/user_error_catalog.h`. Each `UserErrorCode` maps, in one table, to a stable event name (`write.in_use.waiting`, `cache.db.busy`, …) logged as `error_code=`, and to the English title/message keys that the UI shows through `Tr()`. Add the Vietnamese pair to `src/localization.cpp`; `check-localization.py` and `localization_checks.cpp` fail otherwise.
- **Rate limiting**: anything that can repeat per file or per frame goes through a function-local `static core::LogRateLimiter limiter(burst, window)`; pass the `suppressed=` count it returns. Terminal outcomes (gave up, marked failed) are always logged.
- **Hot paths** (dequeue loops, per-frame code) log only at `Trace` and only behind `diagnostics::VerboseLogging()`, so arguments are not even built otherwise.
- **The logger never does disk I/O on the calling thread.** Producers only append to the in-memory ring and queue under the state mutex. A Warning or Error asks the writer thread to flush the OS buffer on its next pass; only `Critical` drains synchronously (so the line is on disk if a crash follows). Disk writes happen under a separate stream mutex (lock order: stream mutex → state mutex).
- **Retention**: about 3 s after startup, a background-priority thread (`StartDiagnosticsCleanup`) deletes QuickSift's own session logs, crash reports and minidumps older than 7 days (`core/log_retention.h`), logging `event=old_file_deleted` and `cleanup_complete`. Crash files are also capped at the newest 20 incidents, and one session log at 16 MiB.
- **Previous sessions** (0.9.5.0 round 20): the Diagnostic log window's **Previous sessions…** button lists earlier session logs from `Logs` (newest first, start time and size, current session left out) and opens one read-only in a `DocumentKind::SessionLog` window. Listing and the bounded read (newest 2 MiB, cut at a line start) run on `sessionLogWorker_` with a lifetime token. The browser never writes, renames or deletes; only the 7-day cleanup removes logs. It logs `session_logs_listed` and `session_log_opened` (file name only).
- **Contained failures**: `catch (...)` in the Windows app layer uses `QS_LOG_AND_CONTINUE(category, site)`, which writes `<site> failed and continued: <what>`. It allows 3 lines per site per 30 s and reports the rest as `(N similar suppressed)`.
- **Crash reports** include a "Subsystem state" block read from lock-free atomics: the priority demand snapshot (highest tier, pending metadata writes, interactive write queued, view decodes, search active/focused).

---

## 6. How to add a feature

Use this order even for a small feature.

### Step 1: Describe the rule in one sentence

Example: “Show only photos whose rating is at least the selected value.”

That sentence identifies the owner: catalog policy, not a button handler.

### Step 2: Separate decision from side effects

A decision should be a small deterministic function when possible:

```cpp
bool IsPhotoVisible(const PhotoItem& photo, const FilterState& filters);
```

The UI handler should gather state, call the decision, update state, and redraw. It should not contain a second private filtering algorithm.

### Step 3: Add the deterministic test first or beside the change

Use the matching program in `scripts/checks/*.cpp` for portable policy. Each check program links against the portable `QuickSiftCore` library, and both verifiers require it to exist; see [`TESTING.md`](TESTING.md). Add a new program only for a new owner. Add a focused native test only when Win32 behavior is essential.

A bug fix is incomplete until the old bad input is represented by a regression test whenever the behavior can be tested deterministically.

### Step 4: Add UI declarations in their registries

For a persistent main-window button:

1. Add the ID to `ui_command_ids.h`.
2. Add one `ButtonDefinition` to `ui_control_catalog.h`.
3. Put behavior in the correct UI feature owner.
4. Build a `ButtonPresentation` and route creation/painting through `MacOsUiFramework`.
5. Use existing theme roles and layout metrics.

For a menu-only command, add only the stable ID and owning behavior. Build `MenuItem` data in the feature owner and let `MacOsUiFramework::TrackDropdown` own popup placement, input, and rendering.

### Step 5: Add diagnostics at the useful boundary

Log failures, unusual fallback decisions, and important lifecycle events. Do not log every successful pixel or every mouse move.

### Step 6: Validate the complete flow

Run the automated checks and manually exercise the feature’s success, cancellation, failure, and shutdown paths.

---

## 7. How to fix a bug

### 7.1 Reproduce the smallest failing case

Write down:

- Input or user action.
- Expected behavior.
- Actual behavior.
- Relevant log lines.
- Whether the bug survives a restart or cache clear.

### 7.2 Find the first wrong decision, not the last visible symptom

Examples:

- A blank thumbnail may be a queue-order problem, not a painting problem.
- A wrong rating may be malformed XMP insertion, not a stale label control.
- A failed undo may be an identity/verification problem, not a menu-state problem.

Trace the runtime story backward until the first invariant is broken.

### 7.3 Fix the owner

Do not patch every caller. Correct the one authoritative function or policy.

### 7.4 Add a regression test

Name the test after the behavior, not the implementation detail. Good test messages explain what must never break again.

### 7.5 Check neighboring states

For every fix, consider:

- Empty input.
- One item.
- Very large input.
- Invalid/corrupt input.
- Cancellation.
- Shutdown.
- Resource allocation failure.
- Old asynchronous results arriving late.
- Remote/removable storage.
- Files changing during an operation.

### 7.6 Remove the obsolete patch

A clean fix usually makes an old workaround unnecessary. Delete dead branches, duplicated rules, stale comments, and tests that only preserve the workaround.

---

## 8. Rules for asynchronous work

The UI thread owns UI state and native controls. Workers and transaction services own expensive or mutating work.

Follow these rules:

- Never touch an `HWND`, Direct2D target, or UI-owned container from a worker.
- Worker and scanner results are retained by `BackgroundCompletionQueue`; private Win32 messages carry no heap/object payload.
- A wake-up message means “drain the queue,” not “take ownership of this pointer.”
- Every result carries enough generation, epoch, token, or identity information for stale work to be rejected.
- File and metadata mutations run only through their serialized transaction services.
- Treat every selected image owner plus a shared XMP companion as one atomic media group; never flatten companions into unrelated history items.
- Publication must remain bound to the exact locked temporary object, and rollback must quarantine/delete that exact object rather than trusting a pathname.
- Scanner completion distinguishes success, partial enumeration, and failure; acknowledge batches only after catalog commit or intentional discard.
- The UI may preflight, but locked primitives must repeat the identity/conflict check immediately before mutation.
- Metadata direct-write batches acquire and release decoder exclusion once per batch, not once per file.
- UI progress counters increase only after a queue or service accepts the work.
- Cancellation stops at safe boundaries; never terminate a worker thread forcibly.
- Shutdown prevents new work, requests cancellation, revokes the worker token and joins every app-owned thread, then stops transaction services before the decoder worker, and only then closes the persistent cache (§8.1).

When adding a queue or transaction rule, extract deterministic sequencing into a portable module with a regression test.

### 8.1 Threads, shutdown, and priority lanes

**Threads.**
- The UI thread owns windows and UI state.
- The decode pool and the view lane (`viewThread_`) live in `src/work/background_work_engine.*`.
- `FolderScanner` enumerates folders.
- `TransactionCoordinator` owns the file and metadata service threads.
- App-owned threads are the ratings, batch, search, deferred-startup, diagnostics-cleanup and session-log-browser workers.

**Worker lifetime.**
- Every app-owned thread captures `token = workerLifetime_.Issue()` (`core/worker_lifetime.h`).
- It checks `token.StopRequested()` and wakes the UI only through `PostFromWorker(token, window, message, ...)`. That call posts only while the token is alive.
- It never reads `hwnd_`.
- `StopAllWorkersForTeardown` revokes the token first and then joins every thread.

**Shutdown order.** `WM_DESTROY` (normal close and Force quit) calls `OnMainWindowDestroyed`, which runs these steps in order:
1. `RequestBackgroundStopForShutdown`
2. `KillMainTimersForShutdown`
3. animation and brush teardown
4. `KillInteractionTimersForShutdown`
5. `StopAllWorkersForTeardown`
6. `CloseBatchDialog`
7. `StopServicesForShutdown`: resource-pressure monitor, then transactions, then scanner, then worker, then completion drain, then `persistentCache_.CloseForShutdown()`
8. `PostQuitMessage`

The log records each step as `event=shutdown_stage` (`app_workers_stop … tokens_left_N`, `resource_pressure_stop`, `transactions_stop`, `scanner_cancel`, `worker_stop`, `drain_completions`, `cache_close`, then `shutdown_sequence_complete`). Never close the cache, release COM objects or free state before step 5.

**Lanes.** There are two vocabularies:
- Queue priority (`JobPriority` in `core/app_types.h`): `Interactive` > `Visible` > `Predictive` > `Face` > `Idle` > `Analysis`. `core/work_queue_policy.h` ranks and promotes jobs between these lanes.
- Process-wide tiers (`core/priority_policy.h`): P0 UI, P1 metadata write, P2 Single/Compare view load (its own lane, thread and decode budget), P3 search reads, P4 visible thumbnails, P5 background. Lower tiers poll `priority::Demand()` and yield within bounded starvation thresholds.

**Metadata aging.**
- Metadata (rating/pick/label) reads stay below decode and user work. Once the lane has yielded with reads queued for 750 ms (`kMinShareAge`), a read may start even while foreground visual work (Interactive/Visible or a view-lane decode) is active or queued. Idle/Predictive prefetch and face analysis never hold reads back (`core/metadata_lane_policy.h`).
- Pending writes outrank reads for at most 10 s (`kWriteYieldCap`).
- A read that keeps failing is retried a bounded number of times and then marked Unreadable (`core/metadata_progress.h`), so "metadata pending" always ends.

---

## 9. Rules for sizes and arithmetic

Image and cache code handles untrusted dimensions and file contents.

Before multiplying dimensions or byte counts:

- Reject zero or negative dimensions.
- Convert to an unsigned wide type only after range validation.
- Check multiplication before allocating.
- Reject non-finite floating-point input before float-to-integer conversion.
- Clamp only when clamping is a defined product behavior; corruption should usually be rejected, not cosmetically normalized.

Use existing checked helpers when available. Do not introduce raw `width * height * 4` allocation arithmetic at a new call site.

---

## 10. Rules for native resources

- Check every required `CreateWindowExW`, COM factory, device, timer, subclass, handle, brush, bitmap, and file operation immediately.
- A required control failure aborts `WM_CREATE`; running with half an interface is worse than failing clearly.
- Use RAII wrappers for handles and GDI objects.
- Use `StartUiTimer`; direct main-window `SetTimer` calls bypass logging and are rejected by source verification.
- Use named subclass IDs and property keys from `../app/application_support.h`.
- Monitor/work-area queries need a fallback because display topology can change at runtime.
- Release device-dependent resources on device loss and recreate them lazily.

---

## 11. Naming rules

Names should explain themselves in a search result or crash stack.

Use:

- Verb-first actions: `ScheduleVisibleWork`, `CreateMainWindowButton`, `VerifyCopiedFile`.
- Value/query names: `CurrentPrefetchPolicy`, `ActivePaths`, `IsCameraRawExtension`.
- `Dip` suffix for device-independent layout units.
- `Px` suffix for physical pixels.
- `ID_<AREA>_<ACTION_OR_CONTROL>` for UI IDs.
- Responsibility names for files and classes.

Avoid:

- `App`, `Manager`, `Helper`, `Util`, `DoThing`, `Process`, `Temp2`.
- Boolean arguments whose meaning is invisible at the call site. Prefer an enum or a named function.
- Comments that retell the code. Explain the invariant or reason.

---

## 12. Comment rules

Comments should answer **why**, **ownership**, or **safety** questions.

Good:

```cpp
// Move the job to the new lane. Changing only its priority field leaves it
// waiting behind lower-priority work in the old deque.
```

Bad:

```cpp
// Set priority to Visible.
job.priority = JobPriority::Visible;
```

At the top of a component, keep the `CODE GUIDE` comment. It tells human reviewers and LLMs which section owns the code and which rule must not be duplicated.

When the implementation changes, update the guide and comment together.

---

## 13. LLM change contract

A coding assistant working on QuickSift should follow this checklist before editing:

1. Read this file (start with §0) and `ARCHITECTURE.md`.
2. State the one component that owns the requested behavior.
3. Search for existing registries, policies, and tests before adding anything.
4. Do not create a duplicate format list, ID list, control list, palette, UI component renderer, popup host, filter, timer wrapper, metadata parser, or queue-priority rule.
5. Route ordinary child controls and chrome through `MacOsUiFramework`; keep only specialized image composition in feature rendering code.
6. Extract deterministic logic from Win32 code when practical.
7. Add a regression test for each deterministic bug fix.
8. Preserve asynchronous ownership and stale-result checks.
9. Preserve file-operation preflight, flush, verification, commit, and rollback.
10. Use obvious names and remove obsolete workaround code.
11. Run the validation sequence below and report anything that could not be executed.

A coding assistant must not claim the Win32 executable was compiled or run unless it actually completed on Windows with the supported toolchain.

---

## 14. Validation sequence

### On any supported development host

```text
python scripts/verify-source-portable.py          # or --source-dir <root>; also runs check-localization.py
python scripts/check-localization.py
pwsh -NoProfile -File scripts/verify-source.ps1   # PowerShell 7 also works off Windows
cmake -S . -B build -DQS_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release
# then build and run each scripts/checks/*.cpp against libQuickSiftCore (see TESTING.md); each must exit 0
```

Run sanitizer builds where the compiler supports them.

### On Windows before release

```text
powershell -ExecutionPolicy Bypass -File scripts/verify-source.ps1
build-release.cmd
```

Then manually test:

- Opening local, removable, and network folders.
- Fast wheel, keyboard, and scrollbar navigation.
- Single and compare modes.
- Ratings, labels, picks, sidecars, and embedded metadata.
- Copy, move, delete, undo, cancellation, collision, disk-full, and locked-file paths.
- Diagnostics viewer and manual export.
- Forced crash report/minidump in a disposable test build.
- Shutdown while scanning and decoding.

---

## 15. Definition of done

A change is done when:

- The behavior has one obvious owner.
- No duplicate rule or parallel registry was added.
- Names explain the code without tribal knowledge.
- Failure and cancellation paths are handled.
- Deterministic behavior has tests.
- Diagnostics contain useful context for failures.
- Original media remains protected by verification and guarded rollback.
- Documentation points future reviewers to the right owner.
- Portable validation passes.
- Required Windows validation is completed or explicitly reported as not run.

That is the whole philosophy: **small owners, explicit flows, tested decisions, visible failures, and extreme caution around photographers’ files.**

---

## 16. Structural conventions enforced by the verifiers (0.9.5.0)

These conventions were added in 0.9.5.0 rounds 19 and 20. Both `scripts/verify-source-portable.py` and `scripts/verify-source.ps1` enforce them, except the function-size guideline; [`TESTING.md`](TESTING.md) lists the rule names.

- **Function size (guideline, not verified).** Prefer keeping the historically huge functions under about 160 lines by splitting them into named helpers: `Worker::Run`, `FileOperation`, `OnWorkReady`, `HandleMainMessage`, `LayoutControls`, `DocumentWindowSubclassProcImpl`, `Worker::Enqueue`, `HandleCanvasMessage`, `WriteEmbeddedMetadataAtomic`, `EvictBitmapCache`, `OnCommand`. New code in those areas should not grow them back into multi-hundred-line bodies. Add a named helper beside the function instead; for example, `Worker::Run` delegates to `WaitForNextJob`, `ExecuteJobKind`, `PublishJobResult` and similar helpers. This is advice for reviewers; no verifier enforces a line count.
- **State lives in `src/app/state/`.** `QuickSiftApplicationImpl` privately inherits `ChromeState`, `PaneState`, `ViewerState`, `BrowseState`, `MetadataState`, `RatingsExchangeState`, `BatchState`, `SearchState`, `HudState` and `SessionLogBrowserState`. The structs hold data only: no methods and no `ComPtr`, because COM release order stays in the main class. Each header must be registered in `CMakeLists.txt`.
- **Worker lifetime** as in §8.1.
- **Diagnostic retention** as in §5.6. The cleanup deletes only through `DeleteOwnDiagnosticFile`, which re-checks the opened handle, and never recurses.
- **The removed `wicImageSources_` cache stays removed.** The UI thread never decodes.
- **Title layout.** `LayoutTitleControls(width, false)` re-lays the title bar only when its `TitleLayoutKey` changes or `MarkTitleLayoutDirty()` was called (DPI, fonts, language, theme).
- **Log-and-continue** as in §0 and §5.6. Exempt: the logger (`diagnostic_log.cpp`), `main.cpp`, the `QuickSiftCore` library and `src/core/`, `src/ui/framework/` (which reports through its error sink) and `src/platform/`.
- **Session log browser (round 20)** as in §5.6. Read-only: the controller contains no delete, move or file-open call; `ReadSessionLogTail` validates the name, opens with `GENERIC_READ` and `FILE_FLAG_OPEN_REPARSE_POINT`, rejects reparse points and reads at most `kMaxDisplayBytes`. Teardown joins `sessionLogWorker_`, and `lifecycle_checks` runs `SessionLogBrowserChecks`.
- **Build failure help (round 20)** as in §4.1. Each build script dot-sources `build-failure-help.ps1` and has a trap that calls `Write-QuickSiftFailure` and exits 1. The `.cmd` wrappers keep `%ERRORLEVEL%` and explain a missing PowerShell (exit 9009).
- **Layout constants.** The left-pane section layouts, the status bar, `OnGetMinMaxInfo` and the document-window chrome take DIPs from `ui_layout_constants.h`.

### Thumbnail viewport priority (2026-08-16)

Thumbnail mode treats the currently visible thumbnail window as a foreground visual task. Visible thumbnails are queued at `Interactive` priority in strict left-to-right, row-major order. On viewport change, queued thumbnail decode work is discarded and rebuilt from the new viewport; in-flight thumbnail work that is no longer visible is canceled at worker cancellation checkpoints. Predictive/background thumbnails remain lower priority and are repopulated only after the current visible window. Thumbnail cache writes inherit the same priority and visible thumbnail cache writes are persisted ahead of metadata and lower-tier cache work.


## Optimization lifetime contracts (0.9.6.0)

- Bitmap cache identity remains case-sensitive exactly as before; transparent probes must hash/compare the same as owning keys. Do not normalize paths in only one producer/consumer. Probe views never outlive the lookup. Touch/recency accounting is unchanged.
- Compare cached cells are returned by const reference; all callers share the same key. Async geometry or rotation changes resolved aspects; pane pinning/fullscreen changes bounds; DPI, folder and mode are key inputs. No recomputation/path copying on a key hit.
- Gradient brushes are device-owned, recoloured by recreation on theme/blob changes, and released on device loss. Only their centre/radius change during resize. Never carry a brush across targets.
- The Shell service owns its worker thread, queues and native process handles (not UI state). It uses a single below-normal/background child of the current executable, an explicit inherited-handle list, an anonymous bounded mapping and a kill-on-close job. Process creation starts suspended so the job is installed before any handler runs. Only this audited self-launch is permitted by both source verifiers. No shell command, external executable, arbitrary command argument, detached thread or thread termination is allowed. Parent waits in 50 ms cancellation slices; extraction has a 5 s deadline. UI uploads at most one result per paint, after foreground view decode, and validates generation/file stamps/target and a monotonically increasing request sequence before admission. Explicit retry/device reset cannot let an older failure settle a fresh request. A pending UI entry is retried once the service becomes idle, so an exceptional result-delivery allocation failure cannot strand it permanently. Negative entries expire after 30 s. Queues are capped at 16 each; GPU fallback surfaces at 32 MiB / 128 identities. Device/folder changes drop stale resources. Shutdown stops the service before device/state teardown. Fallback is still only reached for terminal decode failure.
- `SnapshotWriter` is a self-contained joined service, like the scanner: it owns only immutable strings and its persistence callback, never an HWND or app state. It serializes all saves, coalesces queued snapshots and skips only content already successfully saved. Final close uses `SaveSession(true)`; failure stays visible and blocks ordinary close/logoff. Teardown drains pending settings before closing services. Session safety is never relaxed to optimize disposable derivatives.
- `QueueAppendTransaction` removes only the uncommitted metadata tail if allocation/catalog validation fails. Never pump/reorder that queue before committing the guard. Scans still atomically publish catalog/index/order/positions; the optimization amortizes prefixes, it does not eliminate every full-order merge.
- RAW cancellation is checked within both scanner loops roughly every 4096 visited scan steps (including malformed-candidate rewinds). Cancellation discards even an earlier valid candidate rather than publishing a potentially non-largest preview. Caller checks cancellation before trying any fallback.
- Derivative free-space observations are conservative reservations, not guarantees against concurrent external writers. Unknown free space fails closed; near reserve or for large writes, query again. Disk-full writes still fail through atomic-temp publication and are discarded. Keep checksum, flush, write-through rename, low-space reserve, and all original-media safety.
- Eviction scratch contains iterators only while one pass is running and is cleared by RAII before cache mutation can rehash it later. Capacity is reused, not a second cache. An allocation failure still enters the original allocation-free scan. Byte ceilings and protected-image policy are unchanged.
