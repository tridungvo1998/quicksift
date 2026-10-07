# Coordinated Review Subsystem

`src/review` owns the portable state and deterministic policies that make thumbnail review, Single/Compare viewing, zoom, synchronization, and FaceLock behave as one system.

The design rule is strict:

> The UI owns presentation; `ReviewStateModel` owns review-session state; workers mirror immutable epochs/request stamps only.

## Ownership

| Concern | Authoritative owner |
|---|---|
| Current review mode | `ReviewStateModel` |
| Thumbnail size/scroll and viewport epoch | `ReviewStateModel` |
| Thumbnail focus, Single image, Compare membership, active Compare pane | `ReviewStateModel` |
| Shared/independent zoom, center, rotation, and zoom intent | `ReviewStateModel` |
| Shared/independent FaceLock offsets | the same per-pane record in `ReviewStateModel` |
| Sync transition semantics | `ReviewStateModel::SetSyncCompareView` |
| FaceLock enable/disable state transition semantics | `ReviewStateModel` |
| Navigation epoch | `ReviewStateModel` |
| Zoom/pan/rotation geometry | `view_transform_policy.*` |
| Thumbnail row/column/hit-test geometry | `thumbnail_layout.*` |
| Thumbnail velocity/direction/readiness prediction | `thumbnail_prefetch_policy.*` |
| Stale worker-result admission | `review_async_policy.*` |
| Win32 pointer/keyboard adaptation and catalog geometry lookup | `src/ui/*` adapters |
| Decode execution/caches/read-ahead | `src/work/*` and image-cache modules |
| Multi-selection membership | `SelectionStore` (separate by design) |
| Face detector capability/retry | face-detection policy/controller (separate by design) |

`SelectionStore` is intentionally not folded into the review state. A multi-selection and the one photo/pane currently being reviewed are different concepts.

## State model

The model has four identity states:

- Thumbnail focus path.
- Single-view path.
- Compare path list.
- Active Compare path.

`ActivePath()` selects the correct identity from the current mode. Callers must not maintain a parallel `currentPath`, `focusPath`, or active-pane index as authority.

Compare uses one `IndependentPaneState` record per path when Sync is off. That record owns both the view transform and FaceLock-relative offset, so they cannot drift in separate maps. When Sync is on, one shared view and one shared FaceLock offset are authoritative.

Sync transitions materialize state deliberately:

- Sync -> independent copies the currently visible shared transform/FaceLock offset into each pane, then clears dormant shared transform state.
- Independent -> Sync adopts the active pane and clears independent pane state.
- Disabling FaceLock materializes every visible absolute view first. If distinct synchronized FaceLock centers cannot be represented by one ordinary shared center, Sync is disabled so the exact visible framing is preserved. This is existing intended UX.

## Epochs and async work

The model owns two monotonically increasing epochs:

- **navigation**: invalidates image-view work after meaningful active-image navigation.
- **thumbnail viewport**: invalidates thumbnail work that no longer belongs to the desired viewport/path set.

Workers keep atomics with the same numbers only as execution snapshots. Workers never invent UI epochs.

`QuickSiftApplicationImpl::PublishThumbnailViewport` is the bridge: it asks the model whether the desired thumbnail viewport changed and publishes the resulting epoch/path set to the worker. Overlapping thumbnail paths are retagged and remain useful; paths that fell out of the viewport can be cancelled.

`review_async_policy.*` is the single admission policy for async completion:

- old-folder generations are always stale;
- stale navigation pixel decodes are discarded;
- a Face result that already completed remains reusable catalog knowledge even if navigation moved on;
- stale thumbnail results are discarded only when their path is no longer part of the current desired viewport.

That distinction preserves rapid-navigation performance without letting old pixels repaint the wrong image.

## Historical behavior that must not regress

These are requirements, not implementation accidents:

1. Fit/Fit Width/Fit Height/Fill/100% are semantic intents and re-resolve after pane-size, DPI, source-size, and rotation changes.
2. Custom synchronized zoom is an absolute physical scale, so unequal Compare panes retain equal source-pixel magnification.
3. Pointer-centered wheel zoom and drag pan use the current pane/image geometry and remain rotation-aware.
4. Unsynchronized Compare starts from clean Fit; replacing a pane inherits that slot's transform but not its previous photograph's FaceLock offset.
5. Clicking/focusing a pane never implicitly recenters FaceLock.
6. Disabling FaceLock preserves every visible pane's framing.
7. Current Compare hit testing is derived from current layout, not stale rectangles retained from the last paint.
8. Thumbnail hit testing/rendering/prefetch/keyboard reveal use one layout policy.
9. Rapid viewport changes preserve useful overlapping thumbnail work and cancel only work that became irrelevant.
10. Rapid image navigation rejects stale pixels while allowing already-completed reusable face metadata.
11. Deep zoom retains a useful lower-resolution decode as an intermediate while a larger full decode is in flight.
12. A tiny embedded JPEG/EXIF thumbnail cannot permanently satisfy a larger thumbnail bucket; it must refine to an adequate decode.
13. UI result integration remains time-sliced and priority-aware; the review model does not bypass the completion queue.

## UI adapter rules

UI modules may:

- calculate current pane rectangles from the actual window/canvas;
- translate pointer/keyboard input into portable model operations;
- resolve catalog source dimensions and detected-face rectangles;
- render the state returned by the model/policies;
- publish model-owned epochs to workers.

UI modules must not:

- add a second zoom/center/rotation state;
- retain paint-time Compare hit rectangles as input authority;
- generate navigation or thumbnail epochs independently;
- keep separate maps for pane transforms and FaceLock offsets;
- accept worker results with custom local stale-result rules.

## Tests

`QuickSift.ReviewSubsystem` is the portable behavioral specification. Add regressions there for state transitions, geometry, thumbnail planning/layout, FaceLock/sync interactions, and async epoch admission before adding UI-only guards.
