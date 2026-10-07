# QuickSift UI Code Map

QuickSift uses native Win32 controls and a Direct2D/Direct3D photo canvas, but ordinary interface components are no longer painted independently by feature modules. A central macOS-inspired UI framework owns reusable control creation, interaction state, component rendering, popups, toasts, alerts, title-bar traffic lights, panels, labels, badges, cards, and overlay chrome.

No UI implementation file is included textually into another file. CMake compiles every module independently, and source verification rejects `.inl` implementation fragments and ordinary feature code that bypasses the central component renderer.

## Central UI framework

- `framework/ui_models.h/.cpp` — platform-neutral popup navigation, toast queue/timing, alert state, hit testing, and shared component presentation models.
- `framework/macos_ui_framework.h` — the single component facade used by application UI modules.
- `framework/macos_ui_framework.cpp` — native control creation plus shared button hover/focus behavior.
- `framework/macos_ui_framework_gdi.cpp` — macOS-style buttons, panels, labels, status surfaces, menus, title bar, traffic lights, tabs, and overlay scrollbars.
- `framework/macos_ui_framework_popup.cpp` — centralized dropdown window, work-area placement, high-DPI scrolling, keyboard/mouse routing, and rendering.
- `framework/macos_ui_framework_d2d.cpp` — toasts, modal alerts, photo cards, badges, and shared DirectWrite text rendering.

Feature modules describe a component with a presentation model and call `uiFramework_`. They do not recreate its fill, border, radius, hover, pressed, focus, destructive, theme, or DPI behavior.

Examples:

```cpp
ButtonPresentation button{};
button.text = caption;
button.role = ButtonRole::Danger;
button.state.hot = uiFramework_.IsButtonHot(hwnd);
uiFramework_.PaintButton(dc, rect, button, Palette(), dpi, normalFont, semiboldFont);
```

```cpp
uiFramework_.Toasts().Show(message);
uiFramework_.PaintToast(resources);
```

```cpp
const int command = uiFramework_.TrackDropdown(std::move(request));
```

Specialized photo work remains in `canvas_rendering_and_overlays.cpp`: bitmap composition, crop/zoom transforms, tile placement, and ambient background blobs are application rendering, not reusable control chrome. Cards, borders, labels, badges, alerts, and text inside that canvas still route through the framework.

## Shared UI foundation

- `ui_command_ids.h` — authoritative stable command/control IDs.
- `ui_control_catalog.h` — authoritative persistent main-window button definitions and memberships.
- `ui_layout_metrics.h` — shared device-independent geometry.
- `ui_design_system.h/.cpp` — semantic palettes, contrast, DPI helpers, and low-level failure-safe drawing primitives used by the framework.
- `../app/application_support.h/.cpp` — registered application classes, private messages, timer IDs, and application-specific subclass/property keys.
- `document_and_menu_models.cpp` — help, about, and diagnostics document models.

`ui_design_system` is the low-level visual vocabulary. `MacOsUiFramework` is the component layer. Feature modules should almost never call low-level drawing primitives directly.

## Feature modules

- `app_theme_documents_and_localization.cpp` — theme application, localization, help/about/diagnostics, tooltips, and global shortcuts.
- `main_window_chrome_and_messages.cpp` — class registration, message routing, fullscreen, sizing, DPI, timers, and shutdown coordination.
- `main_window_controls_layout_and_painting.cpp` — component declarations, semantic state mapping, fonts, layout, and left hover-pane composition.
- `popup_menus_commands_and_folder_tree.cpp` — dropdown requests, command dispatch, folder tree behavior, visible-catalog rebuild, and status text.
- `catalog_loading_and_result_integration.cpp` — scan batches, worker-result admission, metadata read queueing, and catalog ordering.
- `graphics_resources_and_image_cache.cpp` — graphics devices, GPU pressure response, bitmap/tile/WIC cache admission and eviction.
- `canvas_rendering_and_overlays.cpp` — thumbnail, single, and compare image composition plus calls to framework card/overlay components.
- `view_state_controller.cpp` — pane geometry, semantic zoom resolution, synchronized/independent state selection, and FaceLock-relative view materialization.
- `face_analysis_controller.cpp` — Windows detector capability state, FaceLock availability UI, centralized face-job admission, and bounded per-photo retry handling.
- `viewport_prefetch_controller.cpp` — conversion of viewport state into ordered decode requests.
- `navigation_input_and_session.cpp` — keyboard/mouse navigation, zoom/pan/rotation, mode changes, face scheduling, and session persistence.
- `metadata_history_and_file_safety.cpp` — metadata/file transaction-plan construction, asynchronous completion integration, and history replay orchestration.
- `settings_filters_and_file_commands.cpp` — settings/filter menu declarations and high-level copy/move/delete planning.

`../app/quicksift_application.h` is the thin public facade. Remaining native orchestration state lives in `../app/quicksift_application_internal.h`; independently testable invariants belong in stores, services, or framework models instead of loose controller fields.

## Important non-UI owners

- Catalog containers and path index: `../core/catalog_store.*`
- Undo/Redo stacks and memory accounting: `../core/history_store.*`
- Worker/scanner result ownership: `../work/background_completion_queue.h`
- Folder enumeration: `../work/folder_scanner.*`
- File transaction execution: `../transactions/file_transaction*`
- Locked Windows transfer primitives: `../transactions/verified_file_operations_windows.*`
- Metadata transaction execution: `../transactions/metadata_transaction_service.*`

UI modules submit immutable plans and integrate results. They must not perform long-running file or metadata writes on the UI thread.

## Where to make common changes

| Change | Primary owner |
|---|---|
| Add/change a reusable component or its interaction behavior | `framework/macos_ui_framework*` and, when portable state is involved, `framework/ui_models.*` |
| Add/change a color or semantic visual role | `ui_design_system.*` |
| Change shared spacing or pane dimensions | `ui_layout_metrics.h` |
| Add a command/control ID | `ui_command_ids.h` |
| Add a persistent main-window button | `ui_control_catalog.h` plus the behavior owner; creation/painting still routes through the framework |
| Add a dropdown command | owning feature module builds `MenuItem` data; `MacOsUiFramework::TrackDropdown` owns the window and rendering |
| Change child composition or placement | `main_window_controls_layout_and_painting.cpp` |
| Change window messages/fullscreen/shutdown | `main_window_chrome_and_messages.cpp` |
| Change review state, zoom/pan/sync/FaceLock policy | `../review/*`; `view_state_controller.cpp` is the Win32/catalog adapter |
| Change bitmap composition | `canvas_rendering_and_overlays.cpp` |
| Change prediction math | `../review/thumbnail_prefetch_policy.*` |
| Change decode submission | `viewport_prefetch_controller.cpp` |
| Change file mutation semantics | `../transactions/file_transaction*` or `verified_file_operations_windows.*` |
| Change metadata batch semantics | `../transactions/metadata_transaction_service.*` |
| Change transaction UI planning/integration | `metadata_history_and_file_safety.cpp` or `settings_filters_and_file_commands.cpp` |

## Native-resource and component rules

- Use `uiFramework_.CreateControl`, `CreateButton`, `CreateOwnerDrawStatic`, and `CreateTooltip` for child controls. Direct `CreateWindowExW` is reserved for top-level application/document windows and framework-owned popup windows.
- Use framework presentation structs and paint methods for ordinary surfaces and chrome. Do not add feature-local rounded-rectangle, text, button, toast, alert, menu, title-bar, or badge renderers.
- Required control and subclass creation is checked. `WM_CREATE` returns `-1` rather than leave a half-built interface alive.
- Optional controls log and degrade safely.
- Use `StartUiTimer`; direct main-window `SetTimer` calls are rejected.
- Keep layout semantics in the feature owner, but take dimensions from `ui_layout_metrics.h` and component appearance from the framework.
- Monitor/work-area queries require fallbacks because display topology can change at runtime.
- Cross-thread messages are wake-ups only; object ownership remains in a synchronized queue or transaction service.

## Adding a UI feature safely

1. Decide whether it is a reusable component, application composition, or specialized photo rendering.
2. Put reusable component state/rendering in `framework/`; put only semantic declarations and layout in the feature module.
3. Add stable IDs and persistent controls to their authoritative registries.
4. Put deterministic interaction state in a portable model and test it.
5. Check required native resources immediately.
6. Submit expensive or mutating work to an existing service instead of blocking the UI.
7. Run portable verification/tests, then native Windows verification/tests and the relevant DPI/theme/input scenario.
