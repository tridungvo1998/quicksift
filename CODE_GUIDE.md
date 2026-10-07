# QuickSift Code Guide

This guide is the plain-English map for humans and coding assistants. Read it before moving code, fixing a bug, or adding a feature.

The central rule is simple:

> **Every decision has one owner. Put new code beside that owner. Do not create a second copy of the same rule.**

If a source comment says `CODE GUIDE`, it points back to this document. The comment should stay short; the explanation belongs here.

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

Never add feature behavior or state to `../app/quicksift_application.h`; it is a thin public PIMPL facade. Private Win32 orchestration declarations belong in `../app/quicksift_application_internal.h`, and independently testable invariants belong in their own store or service.

---

## 2. Where new code goes

Start with the first matching row.

| You are changing... | Put the code here |
|---|---|
| File-extension recognition | `src/core/image_formats.*` |
| Catalog format/date/rating/label/pick filtering or sorting | `src/core/catalog_policy.*` |
| Decoded-memory reservations | `src/core/decode_budget.*` |
| Worker priority ranking or queue promotion | `src/core/work_queue_policy.h` |
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
| SQLite and thumbnail cache persistence | `src/persistent_cache.*` |
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
| UI spacing, dimensions, radii | `src/ui/ui_layout_metrics.h` |
| Stable command/control IDs | `src/ui/ui_command_ids.h` |
| Persistent main-window buttons and membership | `src/ui/ui_control_catalog.h` |
| Window messages, timers, subclass IDs, class names | `src/ui/../app/application_support.h` |
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
| Remaining private Win32 orchestration fields only | `src/app/quicksift_application_internal.h` |

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
- Shared dimensions: `ui_layout_metrics.h`.
- Timer IDs, private messages, subclass IDs, and property names: `../app/application_support.h`.
- Public application ABI: `../app/quicksift_application.h`.
- Remaining private Win32 orchestration fields: `../app/quicksift_application_internal.h`.

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
  -> queued disk entries are flushed when the log lock is available
  -> LATEST_CRASH.txt points to the newest report
```

Use QS_LOG_EVENT for structured diagnostics. Include operation, job kind, priority, generation/epoch, durations, cache hit/miss, dimensions, and relevant Win32 error codes at context boundaries. Avoid logging credentials, image pixels, or full paths unless the path itself is required to reproduce the issue. The logger is asynchronous on disk so high-volume image workloads do not serialize on file I/O. Log once where the operation, path, and Windows error still have meaning; do not spam the same low-level error at every stack level.

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

Use `tests/core_tests.cpp` for portable policy. Add a focused native test only when Win32 behavior is essential.

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
- Shutdown prevents new work, requests cancellation, stops transaction services before the decoder worker, and discards queue-owned completions safely.

When adding a queue or transaction rule, extract deterministic sequencing into a portable module with a regression test.

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

1. Read this file and `ARCHITECTURE.md`.
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
python scripts/verify-source-portable.py .
cmake -S . -B build -DQS_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release
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

### Thumbnail viewport priority (2026-08-16)

Thumbnail mode treats the currently visible thumbnail window as a foreground visual task. Visible thumbnails are queued at `Interactive` priority in strict left-to-right, row-major order. On viewport change, queued thumbnail decode work is discarded and rebuilt from the new viewport; in-flight thumbnail work that is no longer visible is canceled at worker cancellation checkpoints. Predictive/background thumbnails remain lower priority and are repopulated only after the current visible window. Thumbnail cache writes inherit the same priority and visible thumbnail cache writes are persisted ahead of metadata and lower-tier cache work.

