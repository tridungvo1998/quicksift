# Application State Map

Read [`../../../CODE_GUIDE.md`](../../../CODE_GUIDE.md) first. This folder holds the plain data of `QuickSiftApplicationImpl`, split by area. Nothing here has behavior.

## How it works

- Each header declares one struct in namespace `quicksift::app::state`.
- `src/app/quicksift_application_internal.h` includes every header and inherits each struct **privately**. Members keep their names (`paneScrollOffset_`, `batchWorker_`, …) and feature modules use them exactly as before.
- `app_state_common.h` holds the shared includes only.
- Every header is registered in `CMakeLists.txt`.

## Headers

| Header | Struct | Holds | Main user |
|---|---|---|---|
| `chrome_state.h` | `ChromeState` | status-bar text cache, title-bar mouse tracking, fullscreen bookkeeping, animation tick | `ui/main_window_chrome_and_messages.cpp` |
| `pane_state.h` | `PaneState` | left-pane open/tab blends, backdrop and frost bookkeeping, per-tab scroll (`paneScrollOffset_`), `[Pane]` timing, library/tree scrollbar drag | `ui/main_window_controls_layout_and_painting.cpp` |
| `viewer_state.h` | `ViewerState` | Thumbnail/Single/Compare viewing: filmstrip, scroll pump, pointer and face-outline interaction, badge text cache, decorative background | `ui/canvas_rendering_and_overlays.cpp`, `ui/navigation_input_and_session.cpp` |
| `browse_state.h` | `BrowseState` | folders, library, filters, badges, theme and metadata-writing preferences, scan/session flags | `ui/settings_filters_and_file_commands.cpp`, `ui/catalog_loading_and_result_integration.cpp` |
| `metadata_state.h` | `MetadataState` | history, pending edits, write-lane retry state, visible-first metadata read queue | `ui/catalog_loading_and_result_integration.cpp`, `ui/metadata_history_and_file_safety.cpp`, `ui/metadata_write_lane_controller.cpp` |
| `ratings_exchange_state.h` | `RatingsExchangeState` | Import/Export Ratings worker and pending import | `ui/ratings_exchange_commands.cpp` |
| `batch_state.h` | `BatchState` | Batch Processing dialog, worker and pending batch | `ui/batch_processing_commands.cpp` |
| `search_state.h` | `SearchState` | search field, applied query, hydration bookkeeping, search-worker hand-off | `ui/thumbnail_search_controller.cpp` |
| `session_log_browser_state.h` | `SessionLogBrowserState` | previous-session log worker, its mutex-guarded result, the listed logs behind the open dropdown | `ui/session_log_browser_controller.cpp` |
| `hud_state.h` | `HudState` | Performance HUD visibility, background-face pacing, CPU telemetry samples | `ui/navigation_input_and_session.cpp`, `ui/catalog_loading_and_result_integration.cpp` |
| `app_state_common.h` | — | shared includes | all of the above |

`metadata_state.h` here is UI-side bookkeeping. The authoritative metadata knowledge model is `src/core/metadata_state.h` / `src/core/image_metadata.h`.

## Rules (verifier-enforced)

- **Data only.** No methods. Default member initializers are fine.
- **No `ComPtr`.** COM objects, services and anything with a release order stay in `QuickSiftApplicationImpl`.
- **A new field goes in the struct for its area.** Add a new struct only for a new area: register the header in CMake, include it from `quicksift_application_internal.h`, add the private base, and add it to `APP_STATE_STRUCTS` in both verifiers.
- Invariants that can be tested belong in a store or policy (`src/core`, `src/review`), not in a state struct.
