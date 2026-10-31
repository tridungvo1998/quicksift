# QuickSift Developer Guide (routing card)

This is the shortest route from "I need to change something" to the component that owns it. It is deliberately short: [`CODE_GUIDE.md`](CODE_GUIDE.md) is the hub and holds the full owner table (§2), the authoritative owners (§3), the threading, shutdown and lane rules (§8.1), naming (§11), validation (§14) and the verifier-enforced conventions (§16). If this card and CODE_GUIDE disagree, CODE_GUIDE wins.

Other maps: [`ARCHITECTURE.md`](ARCHITECTURE.md) (boundaries and runtime flows), [`src/ui/README.md`](src/ui/README.md) (UI files), [`src/app/state/README.md`](src/app/state/README.md) (state structs), [`BUILDING.md`](BUILDING.md), [`TESTING.md`](TESTING.md), [`PRE_RELEASE_CHECKLIST.md`](PRE_RELEASE_CHECKLIST.md).

## Most common changes

| Goal | Owner |
|---|---|
| Recognize a new image type | `src/core/image_formats.*` |
| Catalog filtering, sorting, storage or visible positions | `src/core/catalog_policy.*`, `src/core/catalog_store.*` |
| Undo/Redo storage, selection, range anchor | `src/core/history_store.*`, `src/core/selection_store.*` |
| Review state, Sync, FaceLock, zoom/pan geometry | `src/review/review_state_model.*`, `src/review/view_transform_policy.*` |
| Thumbnail geometry or prediction | `src/review/thumbnail_layout.*`, `src/review/thumbnail_prefetch_policy.*` |
| Worker decode queues, priorities, timeouts | `src/work/background_work_engine.*`, `src/core/priority_policy.*`, `src/core/decode_timeout_policy.h` |
| File copy/move/delete and their Undo/Redo | `src/transactions/` (`file_transaction.*`, `verified_file_operations_windows.*`, `transaction_coordinator.*`) |
| Metadata writes and sidecars | `src/transactions/metadata_transaction_service.*`, `src/core/sidecar_write_policy.h` |
| Logging, crash reports, 7-day cleanup, previous-session browser | `src/diagnostics/diagnostic_log.*`, `src/core/log_retention.h`, `src/core/session_log_browser.h` |
| Start or stop an app-owned worker thread | `src/core/worker_lifetime.h`; `StopAllWorkersForTeardown` in `src/app/quicksift_application.cpp` |
| A plain `QuickSiftApplicationImpl` field | the matching struct in `src/app/state/*.h` |
| Command IDs, persistent buttons, window messages | `src/ui/ui_command_ids.h`, `src/ui/ui_control_catalog.h`, `src/app/application_support.h` |
| Reusable controls, colours, layout numbers | `src/ui/framework/macos_ui_framework*`, `src/ui/ui_design_system.*`, `src/ui/ui_layout_constants.h` |
| Build discovery, signing, packaging, failure messages | `scripts/build.ps1`, `scripts/build-failure-help.ps1` |

Anything else: CODE_GUIDE §2 "Where new code goes".

## File-safety rules (never weaken)

Never replace verified transactions with convenience filesystem calls. Preserve:

1. Conflict and capacity preflight.
2. Source locking and reparse-point rejection.
3. Private temporary destination.
4. Flush and byte verification.
5. Publication without overwrite.
6. Final source/destination identity verification.
7. Source retirement only after proof of destination.
8. Identity-guarded pair rollback and Undo/Redo.

## Before committing

Run `python scripts/verify-source-portable.py --source-dir .` and `pwsh -NoProfile -File scripts/verify-source.ps1`, build and run `scripts/checks/*.cpp`, then follow CODE_GUIDE §14 (Windows build with `QS_WARNINGS_AS_ERRORS=ON`, binary verification, manual scenarios, `build-release.cmd` before publishing). A non-Windows pass is useful evidence, not a substitute for a native Windows build.
