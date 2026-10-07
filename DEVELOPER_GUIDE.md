# QuickSift 0.9 Developer Guide

This is the shortest route from “I need to change something” to the component that owns it.

## Start here

- Plain-English code map and change recipes: [`CODE_GUIDE.md`](CODE_GUIDE.md)
- UI ownership and editing: [`src/ui/README.md`](src/ui/README.md)
- System boundaries: [`ARCHITECTURE.md`](ARCHITECTURE.md)
- Build setup: [`BUILDING.md`](BUILDING.md)
- Automated and manual validation: [`TESTING.md`](TESTING.md)
- Release gates: [`PRE_RELEASE_CHECKLIST.md`](PRE_RELEASE_CHECKLIST.md)

## Composition model

`src/main.cpp` is a thin native composition root. It initializes the process, constructs `QuickSiftApplication`, and enters the message loop. Feature code does not belong there.

CMake separately compiles every implementation unit. `.inl` implementation fragments and source-text inclusion are forbidden.

The project uses four code shapes:

1. **Portable policy and stores** under `src/core` and coordinated review state/policies under `src/review`.
2. **Portable transaction executors** under `src/transactions`.
3. **Native services** for diagnostics, cache, codecs, metadata containers, worker execution, scanning, and Windows file primitives.
4. **Native UI modules** under `src/ui`, which gather UI state, submit work, and integrate results.

A new component must own one coherent invariant. Do not create `helpers`, `misc`, or a second controller-shaped junk drawer.

## Change routing

| Goal | Owner |
|---|---|
| Recognize a new image type | `src/core/image_formats.*` |
| Change catalog filtering or sorting | `src/core/catalog_policy.*` |
| Change catalog storage/path lookup/visible positions | `src/core/catalog_store.*` |
| Change Undo/Redo storage or memory limits | `src/core/history_store.*` |
| Change selected-path or range-anchor behavior | `src/core/selection_store.*` |
| Change partial-history partitioning | `src/core/history_partition.h` |
| Change decoded-memory reservations | `src/core/decode_budget.*` |
| Change coordinated review state / Sync / FaceLock transitions | `src/review/review_state_model.*` |
| Change thumbnail prediction math | `src/review/thumbnail_prefetch_policy.*` |
| Change thumbnail geometry | `src/review/thumbnail_layout.*` |
| Change zoom/pan/rotation geometry | `src/review/view_transform_policy.*` |
| Change worker decode execution/queues | `src/work/background_work_engine.*` |
| Change folder enumeration/batching | `src/work/folder_scanner.*` |
| Change cross-thread completion ownership | `src/work/background_completion_queue.h` |
| Change portable file transaction sequencing/rollback | `src/transactions/file_transaction.*` |
| Change file Undo/Redo replay | `src/transactions/file_history_replay.*` |
| Change Windows locked copy/move/delete primitives | `src/transactions/verified_file_operations_windows.*` |
| Change file-transaction threading/cancellation | `src/transactions/file_transaction_service.*` |
| Change metadata batch threading/decoder handoff | `src/transactions/metadata_transaction_service.*` |
| Change cross-domain transaction admission/tokens/shutdown | `src/transactions/transaction_coordinator.*` |
| Change runtime-data locations | `src/platform/application_data_paths.h` |
| Change cache serialization/validation | `src/persistent_cache.*` |
| Change logging or crash reports | `src/diagnostics/diagnostic_log.*` |
| Change reusable UI components, dropdowns, toasts, alerts, cards, badges, or title-bar chrome | `src/ui/framework/macos_ui_framework*` |
| Change portable popup/toast/alert interaction state | `src/ui/framework/ui_models.*` |
| Change colors, contrast, or low-level drawing primitives | `src/ui/ui_design_system.*` |
| Change shared sizes or spacing | `src/ui/ui_layout_metrics.h` |
| Add a menu-only command | `src/ui/ui_command_ids.h`, then the owning UI module |
| Add a persistent main-window button | `src/ui/ui_command_ids.h` and `src/ui/ui_control_catalog.h`; behavior stays in its module |
| Change window messages or timers | `src/ui/main_window_chrome_and_messages.cpp` |
| Change main component composition/layout or semantic presentation mapping | `src/ui/main_window_controls_layout_and_painting.cpp` |
| Change canvas drawing | `src/ui/canvas_rendering_and_overlays.cpp` |
| Change scrolling decode submission | `src/ui/viewport_prefetch_controller.cpp` |
| Change transaction-plan construction/result integration | `src/ui/metadata_history_and_file_safety.cpp` or `src/ui/settings_filters_and_file_commands.cpp` |
| Change build discovery, solution generation, signing, or packaging | `scripts/build.ps1` |

## Thread and ownership rules

- The UI thread alone owns `HWND`, Direct2D targets, and UI containers.
- Ordinary controls and chrome are created/painted through the single `MacOsUiFramework`; feature modules own semantics and layout, not duplicate renderers.
- Specialized photo bitmap/tile composition may remain in the canvas module, but cards, badges, text, alerts, and other reusable chrome still use the framework.
- Worker/scanner objects push owned results into `BackgroundCompletionQueue`; `PostMessage` carries only a wake signal.
- File and metadata mutations are admitted through `TransactionCoordinator` and run only through its serialized services.
- The UI may perform preflight, but identity is rechecked inside the locked mutation primitive.
- Cancellation occurs during native copy/compare loops and at safe publication/history boundaries; threads are never forcibly terminated.
- Shutdown stops transaction services before the worker whose decoder gates they use.
- History records only completed mutations. Partial replay returns completed and untouched subsets.

## File-safety rules

Never replace verified transactions with convenience filesystem calls. Preserve:

1. Conflict and capacity preflight.
2. Source locking and reparse-point rejection.
3. Private temporary destination.
4. Flush and byte verification.
5. Publication without overwrite.
6. Final source/destination identity verification.
7. Source retirement only after proof of destination.
8. Identity-guarded pair rollback and Undo/Redo.

## Naming and editing rules

- One decision, one owner.
- Prefer names that remain clear in stack traces and search results.
- Files name their responsibility; avoid `common`, `helpers`, `misc`, and numbered patch files.
- Required Win32 resource creation is checked immediately.
- Broad catches belong at containment boundaries and must preserve useful error context when possible.
- Caches are disposable; original media is authoritative.
- Major compiled components begin with an `OWNER` header describing their boundary.

## Before committing

1. Run `python scripts/verify-source-portable.py --source-dir .`.
2. Configure a fresh build with `QS_WARNINGS_AS_ERRORS=ON`.
3. Build the clean Release target; the former CTest suite is retired.
4. Run a sanitizer build for portable targets when the toolchain supports it.
5. On Windows, run `scripts/verify-source.ps1`, binary verification, and the relevant manual UI/file-safety scenarios.
6. Generate/open the Visual Studio solution and verify QuickSift is the startup target.
7. Run `build-release.cmd` before publishing any binary.

A non-Windows portable pass is useful evidence, but it is not a substitute for a native Windows build and runtime validation.
