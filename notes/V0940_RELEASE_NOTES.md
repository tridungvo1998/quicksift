# QuickSift 0.9.4.0: release notes (engineering)

Source tree: `QuickSift-0.9.3.1-filmstrip-final-repair/` (folder name unchanged). It builds on every earlier iteration: reconcile paths, the left hover pane, wrapping toolbars, scroll virtualization/settle, UI polish, the MSVC `std::max` fix, and the pane stacking/frost/rounded/flicker fixes. All of that work is preserved.

## 1. Version bump (0.9.3.1 → 0.9.4.0)

| File | Change |
|---|---|
| `src/version.h` | MAJOR.MINOR.PATCH.BUILD = 0.9.4.0. Comma form `0,9,4,0`; display `"0.9.4.0 RC"` / `L"0.9.4.0 RC"`; file version `"0.9.4.0"`; package `"0.9.4-rc.0"` |
| `app.manifest` | `assemblyIdentity version="0.9.4.0"` |
| `packaging/msix/AppxManifest.xml.in` | `Version="0.9.4.0"` |
| `vcpkg.json` | `version-string` changed from `"0.9"` to `"0.9.4.0"` |
| `README.md`, `THIRD_PARTY_NOTICES.md` | Titles and the release label |
| `BUILDING.md` | MSIX artifact name `QuickSift-0.9.4.0-RC-x64.msix` |
| `PRE_RELEASE_AUDIT.md`, `PRE_RELEASE_CHECKLIST.md` | Baseline version |
| `scripts/build.ps1` | Example version string `'0.9.4.0 RC'` |
| `CHANGELOG.md` | New `## 0.9.4.0` section; historical entries left as they were |

These read the version from `version.h` automatically, so they needed no edits: the `CMakeLists.txt` regex, the `resource.rc.in` placeholders, `verify-source(.ps1/-portable.py)`, `verify-binary.ps1`, `package-msix.ps1`, and the About/localization strings (`QS_VERSION_WSTRING`). The "RC" label was kept because `build.ps1` requires a channel label.

These generated or historical artifacts still mention 0.9.3.x and were not regenerated: `codebase_bundle.txt` (run `codebase_bundle.ps1`), `SOURCE_MANIFEST.sha256` (regenerate on Windows before release), and `QuickSift-BuildLog-104323.txt`, an old build log.

## 2. Thumbnail View threading design

**Goal:** the UI thread never waits on decode workers or I/O, and a burst of completed thumbnails can never starve paint or input.

1. **Workers produce upload-ready data.** This was already the design and is unchanged. Decode, scaling and colour conversion all happen on workers, which return premultiplied BGRA (`WorkResult::pixels`). The UI thread only calls D2D `CreateBitmap` and inserts the result into the cache.
2. **Bounded, short-lock hand-off.** Results go into `BackgroundCompletionQueue`, a mutex-protected deque capped at 8192 entries, with `PushWait` and `PushTerminal` back-pressure. `TryPopNext` used to re-scan the queue prefix for every ScanComplete candidate (O(n²) under the lock the UI holds); it is now a single pass. I compiled the old and new headers side by side and replayed 3000 randomized push/pop sequences: the pop order was identical.
3. **Coalesced wakes.** The queue owns an atomic `wakePending_`, with `TryClaimWake` / `ReleaseWake` / `WakePending`. Producers (decode-result and metadata-abort posts in `background_work_engine.cpp`, and the three scanner posts in `folder_scanner.cpp`) call `PostMessage(WM_APP_BACKGROUND_COMPLETION)` only when they win the claim, and release it if the post fails. So at most one wake message is in the queue, however many workers finish at once. The UI releases the claim when a drain slice starts. The existing drag-time wake suppression is kept; on scrollbar mouse-up the claim is released and one wake is requested.
4. **Time-budgeted UI drain** (`DrainBackgroundCompletions`, `src/ui/performance_policy_controller.cpp`). Each slice is capped at 48 items or 4 ms; during scrollbar drag the cap is 4 items or 1 ms. `core/performance_policy.cpp` lowered the policy budget from 256 items / 12 ms to 48 / 4 ms.
5. **Continuation that respects paint and input** (`ScheduleBackgroundCompletionContinuation`). Win32 hands out posted messages before input and `WM_PAINT`, so the old self-reposting drain could starve both. Now, if work remains and `GetQueueStatus(QS_INPUT|QS_PAINT)` shows pending input or paint, the next slice runs from a one-shot `ID_TIMER_COMPLETION_PUMP` timer (`USER_TIMER_MINIMUM`). Timers are delivered after input and paint. Otherwise one coalesced wake is posted (`RequestBackgroundCompletionWake`). `OnTimer` kills the pump timer once the queue is empty or placeholders are preferred, and `WM_DESTROY` kills it as well. The existing periodic timer drain remains a safety net for any lost wake.
6. **Paint never blocks.** Grid and filmstrip cells without a decoded bitmap now draw a placeholder. Previously they called the synchronous Shell `IShellItemImageFactory::GetImage` inside `WM_PAINT`, which can mean disk I/O and a thumbnail-provider decode. The Shell fallback is now used only for paths whose decode failed (`failedThumbnailPaths_`). Even then, at most one uncached fallback is built per frame (`shellFallbackFrameBudget_`, or zero while placeholders are preferred); a deferred one invalidates and appears on the next frame.
7. **Unchanged:** the fling/coast/settle scroll policy, settle debounce, rolling-window admission, and the size-aware thumbnail caps.

Files touched by the threading work: `src/work/background_completion_queue.h`, `src/work/background_work_engine.cpp/.h`, `src/work/folder_scanner.cpp`, `src/ui/performance_policy_controller.cpp`, `src/ui/navigation_input_and_session.cpp` (pump timer), `src/ui/main_window_chrome_and_messages.cpp` (WM_DESTROY, wake comment, mouse-up wake), `src/ui/canvas_rendering_and_overlays.cpp` (paint path), `src/app/application_support.h` (timer ID), `src/app/quicksift_application_internal.h` (declarations and members), `src/core/performance_policy.cpp` (budgets), and `ARCHITECTURE.md` (a design section).

## 3. Performance changes, with reasons

| # | Change | Where | Reason |
|---|---|---|---|
| P1 | Deferred adaptive timeout. `EnqueuePreview` / `EnqueueFull` use a provisional timeout (`probeFileSize=false`), flag the job `timeoutDeferred`, and the worker refines it with the real file size just before running it. | `background_work_engine.cpp/.h`, `core/app_types.h` (`timeoutDeferred`, `timeoutSingleView`), `catalog_loading_and_result_integration.cpp` (the thumbnail retry passes 0, meaning deferred) | Every enqueue from paint and viewport publish used to run `fs::file_size` on the UI thread, which stalls on network or slow disks. |
| P2 | Viewport residency probe fixed and made O(1): `bitmapCache_.contains(CacheKey{…})` | `viewport_prefetch_controller.cpp` | Bug: raw paths were compared with normalized cache keys, so already-cached thumbnails were re-requested and re-decoded after every scroll settle. |
| P3 | Batched in-flight query, `Worker::PendingOrActiveThumbnailPaths()`: one lock and one pass over the pending and active family keys | `background_performance_controller.cpp`, `viewport_prefetch_controller.cpp` | Was O(requests × pending) prefix scans under the worker mutex, contending with the decode workers. |
| P4 | Index-driven bitmap-cache bookkeeping: O(1) `UnindexBitmapSize` (probes sibling classes); `PruneSupersededViewBitmaps`, same-path stale-thumbnail removal and the reclaimable-bytes estimate use `pathToBitmapSizes_`; thumbnail inserts skip the superseded-view prune; `ClearBitmapCache` also clears the index | `graphics_resources_and_image_cache.cpp`, `catalog_loading_and_result_integration.cpp` | Each thumbnail result and each eviction used to scan the whole cache on the UI thread. |
| P5 | Early-out in `FindBitmap` / `FindBitmapNoTouch` when the path has no indexed sizes, and unindexed buckets are skipped | `graphics_resources_and_image_cache.cpp` | Paint probes every visible card every frame. A missing card cost 24 wstring key copies plus hash lookups. The index is complete because every insert goes through one site in `OnWorkReady`. |
| P6 | No Face jobs are enqueued for thumbnail results in Thumbnail view | `catalog_loading_and_result_integration.cpp` | The worker always cancels Face jobs while the grid viewport is active, so each thumbnail generated a wasted key build, lock round trip and dequeue/stat/cancel cycle. |
| P7 | `SameDecodeFamily` now runs its cheap tests first and lower-cases the extension only when the RAW-JPEG flags differ (the predicate is equivalent) | `background_decode_policy.cpp` | It runs for every queued job under the worker mutex during enqueue, and each call used to allocate two lower-cased strings. |
| P8 | `TryPopNext` is a single pass | `background_completion_queue.h` | See §2.2. Shorter lock hold for the UI and the producers. |
| P9 | WIC file-identity re-verification for the Single/Compare image source happens at most every 500 ms (`identityVerifiedAt`) | `graphics_resources_and_image_cache.cpp`, `application_support.h` | It used to open the file on every Single/Compare paint. |
| P10 | Smaller UI drain slices plus the timer continuation | §2.4–2.5 | A 12 ms / 256-item slice re-posted ahead of `WM_PAINT` was the main cause of stutter while thumbnails loaded. |
| P11 | `windows.h` use in `core/performance_policy.cpp` is wrapped in `#if defined(_WIN32)` | `core/performance_policy.cpp` | The documented portable core build (`cmake -S . -B build-portable`) failed on non-Windows hosts. No change on Windows. |

## 4. Verification done on Linux

- `python3 scripts/verify-source-portable.py --source-dir .` **passed**. Along the way it flagged one pinned token (`aRawJpegOnly == bRawJpegOnly`), which was restored inside the faster `SameDecodeFamily`.
- Portable core build: `cmake -S . -B <out-of-tree> -DQS_WARNINGS_AS_ERRORS=ON && cmake --build` with GCC 14 **succeeded** after the P11 guard. Before the guard, the unmodified baseline failed on `windows.h`. The portable build defines no ctest tests.
- Syntax check with the MinGW-w64 Windows headers (`x86_64-w64-mingw32-g++ -std=c++20 -fsyntax-only`, with the app's definitions) on every changed TU: no errors in changed code, and no `-Wunused` or `-Wshadow=local` warnings in the changed UI/work files. The only errors were in untouched `background_work_engine.cpp` lines 328–602, because MinGW's SDK lacks `IWICJpegFrameDecode` / `IWICBitmapCodecProgressNotification` / `IWICProgressiveLevelControl`; these exist in the Windows SDK.
- `TryPopNext` old-versus-new equivalence on 3000 randomized sequences: identical.

## 5. Residual risks to check on Windows

1. **MSVC compile and link** of all edits (`/W4 /permissive- /WX`). The MinGW syntax check is strong evidence but not proof.
2. **Feel of the timer continuation.** When paint or input is pending, completions integrate at about 4 ms per timer tick, which is roughly 16 ms with the default timer resolution. Large cached folders should still fill quickly, but compare fill speed against 0.9.3.1. To tune, adjust the slice cap in `DrainBackgroundCompletions` and the `uiCompletion*Budget` values.
3. **Visual change:** cells that haven't decoded yet show a "loading…" placeholder in the grid and a blank bordered cell in the filmstrip, instead of the old synchronous Shell thumbnail. The Shell fallback now appears only for failed decodes.
4. If a file changes externally, the WIC image source can be stale for up to 500 ms; the external-change reconcile paths still catch it.
5. `Present(1)` (vsync) still runs on the UI thread, and the Single-view WIC image source decode stays on the UI thread by D2D design. Neither affects Thumbnail View.
6. `AddLibraryFolder` / `GetLibraryFolders` and comment get/set still use SQLite synchronously from user actions. `AddLibraryFolder` can wait on `databaseMutex_` during cache maintenance. This is rare and was not changed.
7. Wake coalescing depends on every post site releasing the claim on failure. All post sites were audited and the timer drain acts as a backstop, but watch for "thumbnails only appear after mouse move" reports.
8. `codebase_bundle.txt` and `SOURCE_MANIFEST.sha256` are stale; regenerate them on Windows.

## Follow-up: Single View decode worker and thumbnail reliability

- Single/Compare decodes run on a dedicated view-lane worker thread with its own decode budget. Requests are latest-wins via the navigation epoch, neighbour prefetch runs on the same lane and can be adopted by a foreground request, and the UI thread never decodes (the WIC image source has been removed from paint).
- Thumbnail View: fixed two permanent-blank bugs (an in-flight family leaked on early job exits; stale desired targets after a size shrink). Added a post-settle reconciliation sweep, a 400 ms backstop timer while visible cells are missing, pinning during flings, and a per-size budget floor.
- Retry policy: 3 attempts with base/2x/4x timeouts and 150/600 ms backoff, using classified failures (transient, timeout, missing, decode). Deterministic failures fall back to Shell, and the failed placeholder appears only after the final attempt. Failed images reset on file change or reload, or manually with F5 / Ctrl+Shift+R.
- Details: SINGLE_VIEW_WORKER_AND_THUMB_RELIABILITY.md.
