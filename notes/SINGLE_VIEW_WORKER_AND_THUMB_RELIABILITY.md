# QuickSift 0.9.4.0: Single View decode worker and Thumbnail reliability

Codebase: `QuickSift-0.9.3.1-filmstrip-final-repair/` (version 0.9.4.0). This builds on all earlier 0.9.4.0 work.

## 1. Single View: dedicated decode worker

### Design

| Aspect | Implementation |
|---|---|
| Thread | `Worker::viewThread_` runs `Run(false, threads_.size()+1, false, /*viewLane*/true)`. It services only `viewQueue_` (foreground) and `viewPrefetchQueue_` (neighbours). Pool threads never pop view queues, and the view thread never pops pool queues. |
| Budget | `viewDecodeBudget_` is a private `DecodeBudget` (max(profile bytes, 256 MiB), 4 permits). The thumbnail pool holding every shared permit cannot delay the on-screen image, and the reverse is also true. |
| API | `Worker::EnqueueViewDecode(path, kind, target, generation, class, epoch, isSingleView, viewInteractionEpoch, prefetch, retryAttempt)` and `Worker::SetViewPrefetchPaths(paths)`. On the UI side, `RequestViewDecode()` is the single routing point: it uses the view lane in Single/Compare and the pool otherwise. `GetOrRequestBitmap`, `GetOrRequestIntermediateViewBitmap`, `PrioritizeCompareLoads`, `SchedulePredictivePrefetch` and `DispatchIdlePrefetch` all use it. |
| No UI decode | `RenderSingle` and `RenderCompare` no longer create or draw the WIC `ID2D1ImageSource`, which decoded on the UI thread during `DrawImage` and opened the file during paint. Paint draws the best cached Full, Preview or Thumbnail entry (`FindBestViewBitmap`, scaled up). The swap-in is the normal completion: `CreateBitmap` upload plus cache insert. |
| Coalesced completion | Results go through the existing `BackgroundCompletionQueue` and its single outstanding wake. Foreground view results rank 0, so they upload before queued thumbnail and filmstrip results. Prefetch results rank 3 (predictive). |
| Latest request wins | Foreground jobs carry `requestEpoch` = navigation epoch. Navigation advances it: `SetNavigationEpoch` removes queued view jobs from older epochs, and `IsCancelled` aborts the in-flight one at its next checkpoint (WIC progress callback, bundled decoders). The view queue is bounded at 24, dropping the oldest. |
| Dedupe | Same-family requests coalesce in place: the target is upgraded and the interaction epoch refreshed so a zoom does not cancel queued work. A foreground request promotes a matching queued prefetch, and pulls an equivalent job out of the pool queues. |
| Prefetch | `PublishViewPrefetchNeighbours(direction)` covers two ahead and one behind (or +1/-1/+2 when stationary; one ahead on remote storage). Neighbours are decoded in the Preview class at the zoom-matched target, clamped to each image's native edge. RAW neighbours get only the embedded preview. Prefetch is cancelled when any foreground view job is queued, or when its path leaves the published set. |
| Adoption | If a foreground request matches the running prefetch (same path, kind, raw mode, target ≥), it sets `viewActiveAdoptedEpoch_` instead of starting a duplicate decode. The adopted job then follows the latest-wins rule for that epoch. |
| Compare | All panes are foreground view-lane jobs, focus pane first (the lane is FIFO). Neighbour prefetch is disabled in Compare. |
| Shutdown | On `WM_DESTROY`, the sequence is `worker_.SetNotify(nullptr)` then `worker_.Stop()`. Stop clears the view queues, stops the view budget (waking an `Acquire`), sets `stopping_` and joins `viewThread_`. `PushWait` returns on stop, and a null notify HWND never posts. `ID_TIMER_THUMBNAIL_RECONCILE` is killed. |
| View backstop | While the on-screen image is still a stand-in (`ViewBitmapSettled` is false) and not terminally failed, paint arms the 400 ms reconcile timer. In view modes the timer just repaints, which re-issues the deduped foreground request, so a decode cancelled by a zoom or pan interaction epoch is always recovered. |

## 2. Thumbnail View: every skip/drop path and how it is handled

| # | Path where a request can be discarded | Before | Now |
|---|---|---|---|
| 1 | **Job cancelled at dequeue or while waiting for the decode budget** (stale viewport, fling cancel, generation, drag) | The in-flight family (`activeVisualFamilies_`) was never released. `Enqueue` rejected the path as "already active", and `PendingOrActiveThumbnailPaths` reported it in flight. **The cell stayed blank permanently** until the folder was reloaded. | `ReleaseActiveVisualFamily()` runs on every exit: budget failure, cancelled-before-start, and normal end. |
| 2 | **Stale desired target after shrinking the thumbnail size** | Queued larger jobs were discarded without releasing `desiredVisualTargets_`, so every smaller request was "superseded". **Permanent blank.** | `IsSupersededByLargerDecode` ignores Thumbnail-class desired targets above `activeThumbnailTarget_`. |
| 3 | **Settle debounce while input is still trickling** | The settle handler returned without re-arming, so fling-cancelled cells were never re-requested. | It re-arms `ArmThumbnailScrollSettle()`. Settle also runs the reconciliation sweep. |
| 4 | **Fling cancellation / fling admission (loadMode ≤ 0)** | `Enqueue` returned false and in-flight jobs were cancelled, with no UI notification. | The post-settle sweep plus the backstop timer re-enqueue them (paint arms the timer for every placeholder cell, even during flings; the sweep defers while in motion). |
| 5 | **Per-pass enqueue caps** (`maxVisibleEnqueue`, the 24-per-pass cap while interacting) and the drag concurrency cap | Overflow cells waited for a future publish that might never come. | Sweep plus backstop. The sweep requeues up to 96 per tick and continues on the next tick. |
| 6 | **`ApplyThumbnailViewport` discards queued off-plan work** / stale viewport epoch checks | No re-request if the cell came back into view without a new publish. | If any visible cell is missing from the published plan, the sweep republishes first, then re-enqueues with the current viewport epoch. |
| 7 | **Generation / stale checks in `OnWorkReady`** (stale target size, `rowBytes` mismatch, `CreateBitmap` failure) | The result was dropped silently. | The pending key is released first (`AcknowledgePostedResult`). The cell is still missing, so the backstop sweep re-enqueues it. Device loss keeps its existing recovery. |
| 8 | **In-flight dedupe** (pending key or active family) | Correct only while the job really is in flight. | Paths 1–2 fixed. A posted-but-not-integrated result keeps its key until `OnWorkReady` acknowledges it, and the sweep counts it as "missing, in flight" (the timer keeps running). |
| 9 | **Eviction of visible items** | Only the normal card path pinned. The fling placeholder path did not, so an admission burst at fling end could evict on-screen bitmaps. | Both paint paths and the sweep pin visible ±1-row paths (`protectedThumbnailPaths_`). Eviction priority is now: pinned visible plus margin (never evicted) > recent > face > frequent > LRU offscreen. Any eviction that reclaims thumbnail bytes arms the sweep. |
| 10 | **Failed decode** | First failure added to `failedThumbnailPaths_`. Visible cells retried immediately up to 8 times. Every scroll (new viewport epoch) reset the attempts, giving unbounded retries of bad files, and the filmstrip cleared marks on every paint. | Bounded 3-attempt policy (§3). The filmstrip no longer clears marks. |
| 11 | **Timeout** | Retried with the same timeout. | Escalating timeout. The timeout result is posted as a failure, and the pending key and family are released (no stuck in-flight entry). |
| 12 | **Lost wake claim** (post failed, or wake suppressed during drag) | Covered by the timer drain. | Unchanged. The reconcile and retry timers also run `OnTimer`'s completion drain, giving one more backstop. |
| 13 | **Completion queue cap (8192)** | `PushWait` blocks the producer and never drops. | Unchanged (verified). |
| 14 | **Filter / sort / folder batch changes** | Depended on the caller scheduling visible work. | The sweep's plan check (#6) republishes if any visible path is not in the review-state desired set. |
| 15 | **Non-bucket cache sizes** | `FindBitmap` iterated fixed buckets, so entries at other sizes were invisible. | It iterates the path's indexed sizes. View requests are bucketed in `RequestViewDecode`. |
| 16 | **Single-view neighbour prefetch** | Predictive-priority pool jobs were always cancelled by `interactiveVisualFocus_`. | View-lane prefetch (§1). |

**Intended UX now:** once scrolling settles, every cell in the viewport plus one row of margin loads. Nothing visible stays blank: it is cached, in flight, backing off before a retry (the timer re-sweeps), or terminally failed with a visible "Couldn't load image" placeholder.

## 3. Retry / timeout policy

- **Attempts:** at most 3 per path (`kMaxDecodeAttempts`). Worker timeout for attempt *n* is `base << n`, i.e. base, 2x, 4x. 8x is the ceiling for later retries. The file-size-adaptive multiplier still applies on top.
- **Backoff:** 150 ms before attempt 2, then 600 ms before attempt 3. A single `ID_TIMER_THUMBNAIL_RETRY_COOLDOWN` timer is armed only for future deadlines (the old timer could spin at 1 ms on expired entries). Each firing runs the visible sweep and repaints.
- **Classification** (worker, `ClassifyDecodeFailure`; it opens the file with the decoder share mode):
  - `Transient`: sharing or lock violation, access denied during an AV scan, network or device errors, or a zero-length file still being written. These get all attempts.
  - `Timeout`: also gets all attempts. The final attempt uses the WIC → Shell fallback chain (`forceWic`).
  - `Decode`: the file is readable but every codec failed. The next attempt is the fallback attempt (WIC, then Shell thumbnail provider). If that also fails, the path is terminal.
  - `Missing`: terminal immediately.
- **Tracking:** `DecodeRetryState` per `NormalizedPathKey` records attempts, last failure kind, next retry time, the terminal flag, and the file size and mtime at failure. There are separate maps for thumbnails and for view decodes.
- **Placeholder:** the failed placeholder ("Couldn't load image", Vietnamese translation added) is shown only when terminal. A cached Shell thumbnail, if any, is drawn instead, budgeted to one new Shell bitmap per frame.
- **Reset:**
  - automatically when the catalog's file size or mtime differs from the failure stamp;
  - on folder reload (`LoadFolder` and `ClearBitmapCache` clear all state);
  - after 30 s for transient terminal failures, when next visible;
  - manually with **F5** or **Ctrl+Shift+R** (`RetryFailedImageLoads`).
- **Single/Compare:** same 3 attempts and backoff. A deterministic failure gets one extra attempt (the view decode already runs the full codec chain, including Shell). A full decode that could only yield a Shell preview is recorded as final, so paint and the backstop never re-run it in a loop.
- **No stuck in-flight entry:** a timed-out job posts a failure result, so `OnWorkReady` releases the pending key, and `ReleaseActiveVisualFamily` runs at job end. A job that dies before execution releases both via `AcknowledgeUnpostedJob` plus the family release.

## 4. Caching correctness review

| Item | Finding / change |
|---|---|
| Key normalization | Bitmap cache keys are raw `path.wstring()`, the same string `CatalogItem.path` produces everywhere (paint, viewport plan, sweep, results). Retry and failure state uses `NormalizedPathKey`. The worker viewport mirror uses `ThumbnailPathKey` (absolute, lexically normal, lower-case). All producers use a single key family each, and the 0.9.4.0 residency-probe fix (raw vs normalized) is retained. No mixed-key lookups remain in the touched paths. |
| Eviction priority | Visible plus the ±1 row margin are pinned in both paint paths and in the sweep, then recent > face > frequent > LRU. Margin is pinned rather than merely preferred; offscreen and predictive entries are LRU. |
| Memory budget per thumbnail size | New floor in `ApplyDynamicCacheBudgets`: (window rows + 3) × 2 × columns × target² × 4 bytes, capped at 3/4 of the decode budget, with Preview/Full rebalanced from the remainder. |
| Invalidation on file change | Terminal failure state resets when the file stamp changes. Bitmap invalidation on external change still follows the existing reconciliation paths. |
| Duplicate decodes | The view lane dedupes by family and key, adopts running prefetch, promotes queued prefetch and pulls equivalent pool jobs. Fixing the leaked family removes false "in-flight" entries that previously blocked rather than duplicated. |

## 5. Files changed (this task)

- `src/core/app_types.h`: `DecodeFailureKind`; `WorkJob::viewLane/viewPrefetch`; `WorkResult::viewLane/failureKind`.
- `src/work/background_work_engine.h` and `.cpp`:
  - view lane (thread, queues, budget, `EnqueueViewDecode`, `SetViewPrefetchPaths`, `PopViewWork`, adoption, prefetch cancellation);
  - `ReleaseActiveVisualFamily` on all exits;
  - `IsSupersededByLargerDecode` thumbnail fix;
  - `ClassifyDecodeFailure`, escalating timeouts, and the Shell fallback in the `forceWic` attempt;
  - queue maintenance for the view queues.
- `src/work/background_decode_policy.cpp`, `src/work/background_performance_controller.cpp`: view queues added to raw-mode purge, the exclusive-write purge and budget wakeups.
- `src/work/background_completion_queue.h`: view-lane completion ranking.
- `src/app/application_support.h`: `ID_TIMER_THUMBNAIL_RECONCILE = 19`.
- `src/app/quicksift_application_internal.h`: retry state, the reconcile/view API, and new members (replacing the old attempt, cooldown and viewport-epoch maps).
- `src/app/quicksift_application.cpp`: the filmstrip no longer clears failure marks on every paint.
- `src/ui/catalog_loading_and_result_integration.cpp`: retry policy implementation and the `OnWorkReady` failure/success integration.
- `src/ui/graphics_resources_and_image_cache.cpp`: `RequestViewDecode` routing, thumbnail admissibility, `FindBitmap` over indexed sizes, eviction → sweep hook, and the argument-order fix.
- `src/ui/viewport_prefetch_controller.cpp`: `ReconcileVisibleThumbnails`, the reconcile timer helpers, `PublishViewPrefetchNeighbours`, and the bounded retry in the viewport publish.
- `src/ui/canvas_rendering_and_overlays.cpp`:
  - `RenderSingle` / `RenderCompare` without the UI-thread WIC source;
  - view backstop and failed text;
  - pinning in the fling paint;
  - placeholder-driven backstop and terminal placeholder.
- `src/ui/navigation_input_and_session.cpp`: settle re-arm and sweep, timers, F5/Ctrl+Shift+R, Compare on the view lane, mode-switch prefetch clear.
- `src/ui/performance_policy_controller.cpp`: idle prefetch via the view lane (recursion-safe), per-size thumbnail budget floor.
- `src/ui/main_window_chrome_and_messages.cpp`: kill the reconcile timer on `WM_DESTROY`.
- `src/localization.cpp`: "Couldn't load image".
- Docs: `CHANGELOG.md` (0.9.4.0), `ARCHITECTURE.md`, `USER_GUIDE.md`.

## 6. Verification (Linux box)

- `python3 scripts/verify-source-portable.py --source-dir .` passed.
- Portable CMake core build (out of tree in `/tmp/qs-build-portable`, then deleted): 0 warnings, exit 0.
- MinGW `-fsyntax-only -Wall -Wextra -Wshadow` on every changed TU: no new errors or warnings. The pre-existing MinGW-only WIC interface gaps in `background_work_engine.cpp` (lines < 605) and the pre-existing `missing-field-initializers` / HRESULT sign-compare notes are unchanged.
- No Win32/MSVC build or runtime test is possible here.

## 7. Residual risks to check on Windows

1. **Compare decodes are now serial** (one view thread). With 4–6 large panes, total time-to-all-sharp may be longer than the old parallel pool, although the focus pane arrives first. If this shows up, a second view thread for Compare is a small change (`activeViewJob_` would become per thread).
2. **Removing the WIC image source** means a standard JPEG in Single View shows the scaled thumbnail or intermediate for the length of one worker decode (normally < 100 ms with the intermediate bucket). Check first-open latency for very large TIFF/HEIC files.
3. **MSVC `/W4 /WX`:** the new code compiles cleanly with GCC `-Wall -Wextra -Wshadow`, but should be built under MSVC (a local struct in `EvictBitmapCache` accesses members; there are `std::clamp<long long>` / `std::max<UINT>` casts).
4. **`ClassifyDecodeFailure`** opens the file once after a failure. On slow network shares this adds one open per failed attempt (worker thread only).
5. **Backstop timer cadence (400 ms)** runs only while something visible is missing or a view stand-in is shown. Confirm no idle CPU on a folder with permanently failing files (terminal entries do not arm it) and on RAW files in JPG-preview-only mode (`ViewBitmapSettled` treats the preview as final there).
6. **Filmstrip thumbnails** use target 256 while the worker's `activeThumbnailTarget_` keeps the grid target. That pre-existing cancellation rule may still cancel filmstrip jobs when the grid target ≠ 256 (not changed in this pass). Watch for blank filmstrip cells.
7. **Adoption across classes:** a foreground Full request adopts a running Preview-class prefetch of target ≥, and the result lands in the Preview key. `FindBestViewBitmap` picks it up. Verify there is no second decode on arrow-key navigation (watch for `job_dequeued` pairs in the verbose log).
