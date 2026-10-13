// OWNER: Compiled QuickSiftApplication feature module.
#include "app/quicksift_application_internal.h"

#include <unordered_set>

namespace quicksift::app {
namespace {


std::uint64_t ThumbnailViewportSignature(std::uint64_t generation, int target,
    const quicksift::review::prefetch::ThumbnailPrefetchPlan& plan) noexcept {
    std::uint64_t value = generation ^ 0x9e3779b97f4a7c15ull;
    auto mix = [&](std::uint64_t component) {
        value ^= component + 0x9e3779b97f4a7c15ull + (value << 6) + (value >> 2);
    };
    mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(target)));
    mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(plan.visibleFirstRow)));
    mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(plan.visibleLastRow)));
    mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(plan.scheduledFirstRow)));
    mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(plan.scheduledLastRow)));
    mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(plan.direction + 1)));
    return value;
}

} // namespace

// CODE GUIDE: See CODE_GUIDE.md -> "Scrolling thumbnails".
// OWNER: Translate viewport plans into ordered decode requests; do not duplicate prediction math.

    quicksift::review::prefetch::ScrollLoadBudget
    QuickSiftApplicationImpl::CurrentThumbnailScrollLoadBudget() const noexcept {
        const float velocity = thumbnailPrefetchPlanner_.EffectiveVelocityPixelsPerSecond();
        const int sizeDip = reviewState_.Thumbnails().sizeDip;
        int visibleEstimate = 0;
        try {
            const auto layout = CurrentThumbnailLayout();
            const int rows = std::max(1, static_cast<int>(std::ceil(
                std::max(1.0f, layout.canvasHeight) / std::max(1.0f, layout.cellHeight))) + 1);
            visibleEstimate = std::max(1, rows * std::max(1, layout.columns));
        } catch (...) {
            visibleEstimate = 0;
        }
        return quicksift::review::prefetch::ComputeScrollLoadBudget(velocity, sizeDip, visibleEstimate);
    }

    void QuickSiftApplicationImpl::ApplyThumbnailScrollLoadBudget(
        const quicksift::review::prefetch::ScrollLoadBudget& budget) {
        const bool motion = thumbnailScrollDragging_ ||
            budget.phase != quicksift::review::prefetch::ScrollMotionPhase::Settled;
        thumbnailDragLoadMode_ = budget.loadMode;
        thumbnailScrollConcurrentCap_ = budget.maxConcurrentDecodes;
        thumbnailDragVelocityPixelsPerSecond_ =
            std::abs(thumbnailPrefetchPlanner_.EffectiveVelocityPixelsPerSecond());
        if (motion) {
            if (!thumbnailScrollMotionActive_) {
                thumbnailScrollMotionActive_ = true;
                worker_.SetThumbnailDragActive(true);
            }
            worker_.SetThumbnailDragLoadMode(budget.loadMode);
            worker_.SetThumbnailScrollConcurrentCap(budget.maxConcurrentDecodes);
        } else if (thumbnailScrollMotionActive_ && !thumbnailScrollDragging_) {
            ClearThumbnailScrollMotionAdmission();
        }
    }

    void QuickSiftApplicationImpl::ClearThumbnailScrollMotionAdmission() {
        thumbnailScrollMotionActive_ = false;
        thumbnailDragLoadMode_ = 0;
        thumbnailScrollConcurrentCap_ = 0;
        thumbnailDragVelocityPixelsPerSecond_ = 0.0f;
        worker_.SetThumbnailDragLoadMode(0);
        worker_.SetThumbnailScrollConcurrentCap(0);
        worker_.SetThumbnailDragActive(false);
    }

    bool QuickSiftApplicationImpl::ThumbnailScrollPrefersPlaceholders() const noexcept {
        if (thumbnailScrollDragging_ && thumbnailDragLoadMode_ <= 0) return true;
        return thumbnailScrollMotionActive_ && thumbnailDragLoadMode_ <= 0;
    }

    std::uint64_t QuickSiftApplicationImpl::PublishThumbnailViewport(
        std::uint64_t signature, const std::vector<std::wstring>& desiredPaths) {
        const std::uint64_t epoch = reviewState_.UpdateThumbnailViewport(signature, desiredPaths);
        worker_.ApplyThumbnailViewport(epoch, desiredPaths, desiredPaths);
        return epoch;
    }

    void QuickSiftApplicationImpl::CancelThumbnailScrollSettle() {
        thumbnailScrollSettlePending_ = false;
        if (hwnd_) KillTimer(hwnd_, ID_TIMER_SCROLL_SETTLE);
    }

    void QuickSiftApplicationImpl::ArmThumbnailScrollSettle() {
        if (thumbnailScrollDragging_) return;
        thumbnailScrollSettlePending_ = true;
        StartUiTimer(ID_TIMER_SCROLL_SETTLE,
            static_cast<UINT>(quicksift::review::prefetch::ScrollSettleDebounceMilliseconds()),
            L"thumbnail scroll settle");
    }

    void QuickSiftApplicationImpl::RequestThumbnailScrollFrame() {
        // New motion cancels any pending settle enqueue so successive flings cannot
        // dump a backlog from a previous coast/settle onto the decode lane.
        CancelThumbnailScrollSettle();
        thumbnailScrollFramePending_ = true;
        thumbnailViewportNeedsUpdate_ = true;
        if (thumbnailScrollFrameTimerActive_ || thumbnailDragRenderTimerActive_) return;
        thumbnailScrollFrameTimerActive_ = StartUiTimer(ID_TIMER_SCROLL_FRAME,
            static_cast<UINT>(quicksift::review::prefetch::ScrollFrameIntervalMilliseconds()),
            L"thumbnail scroll frame");
    }

    void QuickSiftApplicationImpl::PublishThumbnailViewportForCurrentPlan(bool enqueueDecodes) {
        if (reviewState_.Mode() != ViewMode::Thumbnails) {
            PublishThumbnailViewport(0, {});
            return;
        }
        if (catalog_.VisibleEmpty()) {
            PublishThumbnailViewport(0, {});
            return;
        }

        auto budget = CurrentThumbnailScrollLoadBudget();
        if (thumbnailScrollDragging_ &&
            budget.phase == quicksift::review::prefetch::ScrollMotionPhase::Settled) {
            budget.phase = quicksift::review::prefetch::ScrollMotionPhase::Coasting;
            budget.allowPredictive = false;
            budget.maxPredictiveRows = 0;
            budget.placeholdersOnly = false;
            if (budget.maxConcurrentDecodes == 0) {
                budget.maxConcurrentDecodes =
                    quicksift::review::prefetch::ConcurrentThumbnailDecodeCap(
                        reviewState_.Thumbnails().sizeDip);
                budget.loadMode = budget.maxConcurrentDecodes <= 1u ? 1 : 2;
                budget.maxVisibleEnqueue = static_cast<int>(budget.maxConcurrentDecodes);
            }
        }
        ApplyThumbnailScrollLoadBudget(budget);

        const AdaptivePrefetchPolicy policy = CurrentPrefetchPolicy();
        const auto layout = CurrentThumbnailLayout();
        quicksift::review::prefetch::ThumbnailViewport viewport;
        viewport.itemCount = catalog_.VisibleCount();
        viewport.columns = layout.columns;
        viewport.scrollOffset = reviewState_.Thumbnails().scrollDip;
        viewport.viewportHeight = layout.canvasHeight;
        viewport.rowHeight = layout.cellHeight;
        const bool largeFolder = catalog_.VisibleCount() > 500;
        viewport.baseOverscanRows = largeFolder ? std::min(policy.extraThumbnailRows, 1) : policy.extraThumbnailRows;
        int maximumPredictiveRows = 0;
        if (budget.allowPredictive && enqueueDecodes) {
            const int predictiveRowCap = largeFolder ? 4 : (systemProfile_.lowMemory ? 8 : 24);
            maximumPredictiveRows = std::clamp(
                policy.extraThumbnailRows + policy.depth * 2, 1, predictiveRowCap);
            if (storageProfile_.remote) maximumPredictiveRows = std::min(maximumPredictiveRows, 5);
            else if (storageProfile_.removable) maximumPredictiveRows = std::min(maximumPredictiveRows, 8);
            else if (storageProfile_.seekPenalty) maximumPredictiveRows = std::min(maximumPredictiveRows, 10);
            maximumPredictiveRows = std::min(maximumPredictiveRows, std::max(0, budget.maxPredictiveRows));
        }
        viewport.maximumPredictiveRows = maximumPredictiveRows;
        viewport.predictionHorizonSeconds = policy.thumbnailPredictionHorizonSeconds;
        if (budget.phase != quicksift::review::prefetch::ScrollMotionPhase::Settled)
            viewport.baseOverscanRows = 0;

        const auto plan = thumbnailPrefetchPlanner_.PlanThumbnails(viewport);
        const int target = ThumbnailDecodeTarget();
        worker_.SetThumbnailTarget(target);

        // Fling coalesce: skip ApplyThumbnailViewport when the visible row window
        // has not jumped. Cancels only happen on real viewport jumps.
        const bool fling = budget.phase == quicksift::review::prefetch::ScrollMotionPhase::Fling ||
            budget.placeholdersOnly;
        if (fling && !enqueueDecodes &&
            plan.visibleFirstRow == thumbnailScrollLastPublishedFirstRow_ &&
            plan.visibleLastRow == thumbnailScrollLastPublishedLastRow_) {
            return;
        }

        std::vector<std::wstring> desiredPaths;
        std::vector<std::wstring> visiblePaths;
        desiredPaths.reserve(plan.requests.size());
        visiblePaths.reserve(static_cast<std::size_t>(std::max(0, plan.visibleLastRow - plan.visibleFirstRow + 1)) *
            static_cast<std::size_t>(std::max(1, layout.columns)));
        for (const auto& request : plan.requests) {
            if (request.index >= catalog_.VisibleCount()) continue;
            const auto path = VisiblePhoto(request.index).path.wstring();
            desiredPaths.push_back(path);
            if (request.tier == quicksift::review::prefetch::PrefetchTier::Visible)
                visiblePaths.push_back(path);
        }

        const uint64_t signature = ThumbnailViewportSignature(generation_, target, plan);
        const uint64_t viewportEpoch = reviewState_.UpdateThumbnailViewport(signature, desiredPaths);
        lastScheduledThumbnailViewportEpoch_ = viewportEpoch;
        worker_.ApplyThumbnailViewport(viewportEpoch, desiredPaths, visiblePaths);
        thumbnailScrollLastPublishedFirstRow_ = plan.visibleFirstRow;
        thumbnailScrollLastPublishedLastRow_ = plan.visibleLastRow;

        if (!enqueueDecodes || budget.placeholdersOnly) return;

        // 0.9.4.0: one worker-lock acquisition for the whole plan instead of one
        // prefix scan of every pending key per request.
        const std::unordered_set<std::wstring> inFlightThumbnailPaths =
            worker_.PendingOrActiveThumbnailPaths();
        unsigned encodedReadAheadQueued = 0;
        int enqueuedThisPass = 0;
        const auto retryNow = std::chrono::steady_clock::now();
        const int enqueueCap = std::max(0, budget.maxVisibleEnqueue);
        for (const auto& request : plan.requests) {
            if (request.index >= catalog_.VisibleCount()) continue;
            if (!budget.allowPredictive &&
                request.tier != quicksift::review::prefetch::PrefetchTier::Visible) continue;
            const PhotoItem& photo = VisiblePhoto(request.index);
            JobPriority priority = JobPriority::Idle;
            switch (request.tier) {
            case quicksift::review::prefetch::PrefetchTier::Visible:
                priority = JobPriority::Interactive;
                break;
            case quicksift::review::prefetch::PrefetchTier::Predictive:
                priority = JobPriority::Predictive;
                if (encodedReadAheadQueued < std::min(storageProfile_.readAheadFiles,
                    performancePolicy_.readAheadFiles)) {
                    worker_.QueueEncodedReadAhead(photo.path);
                    ++encodedReadAheadQueued;
                }
                break;
            case quicksift::review::prefetch::PrefetchTier::Background:
                priority = JobPriority::Idle;
                break;
            }
            const std::wstring thumbnailKey = NormalizedPathKey(photo.path);
            const std::wstring photoPath = photo.path.wstring();
            int retryAttempt = 0;
            bool forceWic = false;
            const bool visibleRequest = request.tier == quicksift::review::prefetch::PrefetchTier::Visible;
            // 0.9.4.0: O(1) residency probe with the exact bitmap-cache key. The former
            // code copied every cached thumbnail path into a temporary set on every
            // publish and then probed it with the *normalized* (absolute, lower-case)
            // key while the set held raw paths, so on most real paths cached
            // thumbnails were never recognised and were re-decoded and re-uploaded
            // after every scroll settle.
            const bool inCache = bitmapCache_.contains(
                CacheKey{ photoPath, target, CacheClass::Thumbnail });
            const bool inFlight = inFlightThumbnailPaths.contains(photoPath);
            const bool shouldEnqueueThumbnail = !inCache && !inFlight;

            // 0.9.4.0 bounded retry policy (see catalog_loading_and_result_integration):
            // terminal failures stay failed until file change / reload / manual retry;
            // a backing-off path waits for its deadline (the retry timer re-sweeps).
            // The former logic reset a failed path's attempts on every new viewport
            // epoch, i.e. every scroll re-decoded known-bad files without bound.
            if (shouldEnqueueThumbnail &&
                !ThumbnailDecodeAdmissible(photo, thumbnailKey, retryNow, retryAttempt, forceWic)) continue;
            if (!visibleRequest && retryAttempt > 0) continue; // retries are visible-only
            if (shouldEnqueueThumbnail) {
                if (enqueuedThisPass >= enqueueCap) break;
                if (IsActivelyInteracting() && budget.phase == quicksift::review::prefetch::ScrollMotionPhase::Settled &&
                    enqueuedThisPass >= 24) break;
                ++enqueuedThisPass;
                worker_.EnqueuePreview(photo.path, target, generation_, priority,
                    CacheClass::Thumbnail, 0, viewportEpoch, retryAttempt, forceWic);
            }
            if (reviewState_.FaceLockEnabled() && request.tier == quicksift::review::prefetch::PrefetchTier::Visible &&
                budget.phase == quicksift::review::prefetch::ScrollMotionPhase::Settled &&
                (!photo.facesScanned || (!photo.faces.empty() && !photo.faceBlurScanned))) {
                EnqueueFaceAnalysis(photo.path, reviewState_.Epochs().navigation, JobPriority::Face,
                    photo.facesScanned ? kFaceSharpnessDecodeSize : kFaceDecodeSize);
            }
        }
    }

    void QuickSiftApplicationImpl::ProcessThumbnailScrollFrame() {
        thumbnailScrollFramePending_ = false;
        auto budget = CurrentThumbnailScrollLoadBudget();
        // Captured scrollbar drag never opens a full settle dump mid-gesture.
        if (thumbnailScrollDragging_ &&
            budget.phase == quicksift::review::prefetch::ScrollMotionPhase::Settled) {
            budget.phase = quicksift::review::prefetch::ScrollMotionPhase::Coasting;
            budget.allowPredictive = false;
            budget.maxPredictiveRows = 0;
            budget.placeholdersOnly = false;
            if (budget.maxConcurrentDecodes == 0) {
                budget.maxConcurrentDecodes =
                    quicksift::review::prefetch::ConcurrentThumbnailDecodeCap(
                        reviewState_.Thumbnails().sizeDip);
                budget.loadMode = budget.maxConcurrentDecodes <= 1u ? 1 : 2;
                budget.maxVisibleEnqueue = static_cast<int>(budget.maxConcurrentDecodes);
            }
        }
        ApplyThumbnailScrollLoadBudget(budget);

        // Always paint this frame from cache/placeholders — never decode on paint.
        InvalidateCanvas();

        if (budget.phase == quicksift::review::prefetch::ScrollMotionPhase::Fling ||
            budget.placeholdersOnly) {
            // Publish/cancel only when the visible row window jumps.
            if (thumbnailViewportNeedsUpdate_) {
                PublishThumbnailViewportForCurrentPlan(false);
                thumbnailViewportNeedsUpdate_ = false;
            }
        } else if (budget.phase == quicksift::review::prefetch::ScrollMotionPhase::Coasting) {
            // Size-capped rolling window; still one layout/publish per frame max.
            PublishThumbnailViewportForCurrentPlan(true);
            thumbnailViewportNeedsUpdate_ = false;
        } else {
            // Velocity quiet: arm settle debounce instead of dumping Interactive work now.
            thumbnailViewportNeedsUpdate_ = false;
            ArmThumbnailScrollSettle();
        }

        const bool keepPumping = thumbnailScrollDragging_ ||
            thumbnailScrollFramePending_ ||
            budget.phase != quicksift::review::prefetch::ScrollMotionPhase::Settled;
        if (!keepPumping) {
            if (hwnd_) KillTimer(hwnd_, ID_TIMER_SCROLL_FRAME);
            thumbnailScrollFrameTimerActive_ = false;
            if (thumbnailDragRenderTimerActive_) {
                KillTimer(hwnd_, ID_TIMER_DRAG_RENDER);
                thumbnailDragRenderTimerActive_ = false;
            }
        }
    }

    void QuickSiftApplicationImpl::ScheduleVisibleWork() {
        const auto now = std::chrono::steady_clock::now();
        const auto budgetProbe = CurrentThumbnailScrollLoadBudget();
        ApplyThumbnailScrollLoadBudget(budgetProbe);

        // Motion path is owned by the virtualized frame pump. Callers that still
        // hit ScheduleVisibleWork during fling/coast are redirected so they cannot
        // thrash ApplyThumbnailViewport or enqueue a backlog.
        if (budgetProbe.phase == quicksift::review::prefetch::ScrollMotionPhase::Fling ||
            budgetProbe.placeholdersOnly ||
            (budgetProbe.phase == quicksift::review::prefetch::ScrollMotionPhase::Coasting &&
                (thumbnailScrollDragging_ || thumbnailScrollMotionActive_))) {
            RequestThumbnailScrollFrame();
            return;
        }

        // Settled: keep a mild coalesce so non-scroll callers (folder load, etc.)
        // do not stampede the worker.
        const auto throttleWindow = std::chrono::milliseconds(80);
        if (lastThrottledSchedule_.time_since_epoch().count() != 0 &&
            now - lastThrottledSchedule_ < throttleWindow) {
            if (!throttledSchedulePending_) {
                throttledSchedulePending_ = true;
                StartUiTimer(ID_TIMER_THROTTLED_SCHEDULE,
                    static_cast<UINT>(throttleWindow.count()), L"throttled schedule");
            }
            return;
        }
        lastThrottledSchedule_ = now;
        throttledSchedulePending_ = false;
        CancelThumbnailScrollSettle();
        PublishThumbnailViewportForCurrentPlan(true);
    }

    void QuickSiftApplicationImpl::SchedulePredictivePrefetch(int direction) {
        if (thumbnailScrollDragging_) return;
        const auto budget = CurrentThumbnailScrollLoadBudget();
        if (budget.phase != quicksift::review::prefetch::ScrollMotionPhase::Settled) {
            // Motion still elevated: keep decode admission throttled and only refresh
            // the visible plan. Predictive Single/Compare work waits until settle.
            ScheduleVisibleWork();
            return;
        }
        if (catalog_.VisibleEmpty()) return;
        if (reviewState_.Mode() == ViewMode::Thumbnails) {
            ScheduleVisibleWork();
            return;
        }
        PublishThumbnailViewport(0, {});
        if (reviewState_.Mode() == ViewMode::Single || reviewState_.Mode() == ViewMode::Compare) {
            // 0.9.4.0: Single/Compare decoding is owned by the dedicated view lane.
            // The current image is requested by paint (foreground, latest-wins);
            // this only publishes low-priority neighbour prefetch on the same lane.
            RefreshPerformancePolicy();
            PublishViewPrefetchNeighbours(direction);
            InvalidateCanvas();
            return;
        }
        const std::wstring base = reviewState_.ActivePath();
        const auto current = IndexForPath(base);
        if (!current) return;
        const AdaptivePrefetchPolicy policy = CurrentPrefetchPolicy();

        // Single View neighbours should be decoded at the same effective zoom
        // level as the image currently being viewed. Compute the display target
        // from the active view rather than using the generic prefetch target, then
        // clamp each neighbour to its own native edge so a deep zoom on one image
        // does not request pixels that cannot exist on a smaller neighbour.
        int zoomMatchedTarget = policy.prefetchTarget;
        if (reviewState_.Mode() == ViewMode::Single) {
            const std::wstring activePath = reviewState_.SinglePath();
            const auto activeIndex = IndexForPath(activePath);
            if (activeIndex) {
                const PhotoItem& activePhoto = VisiblePhoto(*activeIndex);
                const D2D1_RECT_F activeArea = ViewAreaForPath(activePath);
                const auto activeView = ResolveViewForPath(
                    activePath, activeArea, BaseViewForPath(activePath),
                    activePhoto.sourceWidth, activePhoto.sourceHeight);
                const float baseEdge = std::max(
                    1.0f, std::max(activeArea.right - activeArea.left,
                                   activeArea.bottom - activeArea.top) * CanvasPixelScale() * 1.25f);
                const float zoomFactor = std::max(1.0f, activeView.relativeZoom);
                zoomMatchedTarget = std::max(
                    policy.prefetchTarget,
                    static_cast<int>(baseEdge * zoomFactor * 1.10f));
                if (activePhoto.sourceWidth > 0 && activePhoto.sourceHeight > 0) {
                    zoomMatchedTarget = std::min(
                        zoomMatchedTarget,
                        std::max(activePhoto.sourceWidth, activePhoto.sourceHeight));
                }
            }
        }

        int depth = policy.depth;
        if (storageProfile_.remote) depth = std::min(depth, 2);
        else if (storageProfile_.removable) depth = std::min(depth, 3);
        else if (storageProfile_.seekPenalty) depth = std::min(depth, 4);
        depth = std::clamp(depth, 1, 10);
        std::vector<int> offsets;
        offsets.push_back(0);
        for (int distance = 1; distance <= depth; ++distance) {
            if (direction >= 0) { offsets.push_back(distance); if (direction == 0) offsets.push_back(-distance); }
            else { offsets.push_back(-distance); if (direction == 0) offsets.push_back(distance); }
        }
        for (size_t order = 0; order < offsets.size(); ++order) {
            const int index = static_cast<int>(*current) + offsets[order];
            if (index < 0 || index >= static_cast<int>(catalog_.VisibleCount())) continue;
            const PhotoItem& photo = VisiblePhoto(static_cast<size_t>(index));
            const JobPriority priority = order == 0 ? JobPriority::Interactive : JobPriority::Predictive;
            int target = order == 0 ? policy.currentTarget : zoomMatchedTarget;
            if (order > 0 && reviewState_.Mode() == ViewMode::Single) {
                if (photo.sourceWidth > 0 && photo.sourceHeight > 0) {
                    target = std::min(target, std::max(photo.sourceWidth, photo.sourceHeight));
                }
                target = std::max(target, 1);
            }
            if (order > 0 && order <= static_cast<size_t>(storageProfile_.readAheadFiles))
                worker_.QueueEncodedReadAhead(photo.path);
            if (IsRawExtension(photo.extension)) {
                worker_.EnqueuePreview(photo.path, target, generation_, priority, CacheClass::Preview, reviewState_.Epochs().navigation);
                // Only the active RAW is fully demosaiced. Neighbours retain an
                // embedded preview so switching stays instant without spending
                // CPU and hundreds of MiB on speculative full RAW conversions.
                if (order == 0) {
                    worker_.EnqueueFull(photo.path, target, generation_, priority, CacheClass::Preview, reviewState_.Epochs().navigation);
                }
            } else {
                worker_.EnqueueFull(photo.path, target, generation_, priority, CacheClass::Preview, reviewState_.Epochs().navigation);
            }
            if (reviewState_.FaceLockEnabled() && (!photo.facesScanned || (!photo.faces.empty() && !photo.faceBlurScanned))) {
                EnqueueFaceAnalysis(photo.path, reviewState_.Epochs().navigation,
                    order == 0 ? JobPriority::Face : JobPriority::Idle,
                    photo.facesScanned ? kFaceSharpnessDecodeSize : kFaceDecodeSize);
            }
        }
    }

    void QuickSiftApplicationImpl::PublishViewPrefetchNeighbours(int direction) {
        const ViewMode mode = reviewState_.Mode();
        if (mode != ViewMode::Single || catalog_.VisibleEmpty()) {
            // Compare panes are all foreground work (PrioritizeCompareLoads); there is
            // no meaningful "next" image to prefetch.
            worker_.SetViewPrefetchPaths({});
            lastViewPrefetchSignature_.clear();
            return;
        }
        const std::wstring base = reviewState_.ActivePath();
        const auto current = IndexForPath(base);
        if (!current) {
            worker_.SetViewPrefetchPaths({});
            return;
        }
        // Read the already-refreshed policy directly. CurrentPrefetchPolicy() refreshes
        // the governor -> UpdateAppState() -> DispatchIdlePrefetch() -> here, which
        // would recurse.
        AdaptivePrefetchPolicy policy;
        policy.depth = performancePolicy_.prefetchDepth;
        policy.prefetchTarget = performancePolicy_.prefetchDecodeTarget;

        // Neighbours are decoded at the zoom-matched display size of the current
        // image (clamped to each neighbour's native edge) in the Preview lane, so a
        // navigation can be satisfied by FindBestViewBitmap without a new decode.
        int zoomMatchedTarget = policy.prefetchTarget;
        if (mode == ViewMode::Single) {
            const PhotoItem& activePhoto = VisiblePhoto(*current);
            const D2D1_RECT_F activeArea = ViewAreaForPath(base);
            const auto activeView = ResolveViewForPath(base, activeArea, BaseViewForPath(base),
                activePhoto.sourceWidth, activePhoto.sourceHeight);
            const float baseEdge = std::max(1.0f, std::max(activeArea.right - activeArea.left,
                activeArea.bottom - activeArea.top) * CanvasPixelScale() * 1.25f);
            const float zoomFactor = std::max(1.0f, activeView.relativeZoom);
            zoomMatchedTarget = std::max(policy.prefetchTarget,
                static_cast<int>(baseEdge * zoomFactor * 1.10f));
        }

        // Modest window: two ahead in the travel direction and one behind (or
        // +1/-1/+2 when stationary). Storage-aware depth can only shrink it.
        int ahead = 2;
        if (storageProfile_.remote) ahead = 1;
        std::vector<int> offsets;
        if (direction < 0) {
            for (int distance = 1; distance <= ahead; ++distance) offsets.push_back(-distance);
            offsets.push_back(1);
        } else if (direction > 0) {
            for (int distance = 1; distance <= ahead; ++distance) offsets.push_back(distance);
            offsets.push_back(-1);
        } else {
            offsets = { 1, -1 };
            if (ahead > 1) offsets.push_back(2);
        }
        if (policy.depth <= 1 && offsets.size() > 2) offsets.resize(2);

        struct Neighbour { fs::path path; int target; bool raw; };
        std::vector<Neighbour> neighbours;
        std::vector<std::wstring> allowed;
        allowed.reserve(offsets.size() + 1);
        // The current image stays in the allowed set so a prefetch that is already
        // running for it (the user just navigated onto a neighbour) is not
        // cancelled before paint adopts it as the foreground request.
        allowed.push_back(VisiblePhoto(*current).path.wstring());
        for (const int offset : offsets) {
            const long long index = static_cast<long long>(*current) + offset;
            if (index < 0 || index >= static_cast<long long>(catalog_.VisibleCount())) continue;
            const PhotoItem& photo = VisiblePhoto(static_cast<size_t>(index));
            int target = zoomMatchedTarget;
            if (photo.sourceWidth > 0 && photo.sourceHeight > 0)
                target = std::min(target, std::max(photo.sourceWidth, photo.sourceHeight));
            neighbours.push_back({ photo.path, std::max(1, target), IsRawExtension(photo.extension) });
            allowed.push_back(photo.path.wstring());
        }
        worker_.SetViewPrefetchPaths(allowed);
        for (std::size_t order = 0; order < neighbours.size(); ++order) {
            const Neighbour& neighbour = neighbours[order];
            if (order < static_cast<std::size_t>(storageProfile_.readAheadFiles))
                worker_.QueueEncodedReadAhead(neighbour.path);
            const int bucket = BucketFor(neighbour.target);
            if (BitmapSatisfiesDisplay(FindBitmapNoTouch(neighbour.path, bucket, CacheClass::Preview),
                    std::min(bucket, neighbour.target), !neighbour.raw)) continue;
            // Only the active RAW is demosaiced; neighbours keep the embedded preview.
            RequestViewDecode(neighbour.path,
                neighbour.raw ? JobKind::DecodePreview : JobKind::DecodeFull,
                neighbour.target, CacheClass::Preview, JobPriority::Interactive, true);
        }
        for (const Neighbour& neighbour : neighbours) {
            const auto index = IndexForPath(neighbour.path.wstring());
            if (!index) continue;
            const PhotoItem& photo = VisiblePhoto(*index);
            if (reviewState_.FaceLockEnabled() && (!photo.facesScanned || (!photo.faces.empty() && !photo.faceBlurScanned))) {
                EnqueueFaceAnalysis(photo.path, reviewState_.Epochs().navigation, JobPriority::Idle,
                    photo.facesScanned ? kFaceSharpnessDecodeSize : kFaceDecodeSize);
            }
        }
        lastViewPrefetchSignature_ = base + L"|" + std::to_wstring(direction) + L"|" +
            std::to_wstring(generation_) + L"|" + std::to_wstring(zoomMatchedTarget);
    }

    void QuickSiftApplicationImpl::ArmThumbnailReconcile(UINT delayMs) {
        if (!hwnd_) return;
        if (thumbnailReconcileTimerActive_ && delayMs != 0) return;
        thumbnailReconcileTimerActive_ = true;
        StartUiTimer(ID_TIMER_THUMBNAIL_RECONCILE, std::max<UINT>(delayMs, 15u), L"visible reconcile");
    }

    void QuickSiftApplicationImpl::StopThumbnailReconcile() {
        if (hwnd_ && thumbnailReconcileTimerActive_) KillTimer(hwnd_, ID_TIMER_THUMBNAIL_RECONCILE);
        thumbnailReconcileTimerActive_ = false;
    }

    void QuickSiftApplicationImpl::ReconcileVisibleThumbnails(const wchar_t* reason) {
        constexpr UINT kReconcileIntervalMs = 400;
        if (!hwnd_ || catalog_.VisibleEmpty()) {
            StopThumbnailReconcile();
            return;
        }
        const ViewMode mode = reviewState_.Mode();
        if (mode == ViewMode::Single || mode == ViewMode::Compare) {
            // View backstop: RenderSingle/RenderCompare arm this while the displayed
            // image is not yet at full quality. Repainting re-issues the (deduped)
            // foreground request, recovering from a cancelled/dropped view decode.
            StopThumbnailReconcile();
            InvalidateCanvas();
            return;
        }
        if (mode != ViewMode::Thumbnails) {
            StopThumbnailReconcile();
            return;
        }
        const auto budget = CurrentThumbnailScrollLoadBudget();
        if (thumbnailScrollDragging_ || thumbnailScrollMotionActive_ ||
            ThumbnailScrollPrefersPlaceholders() ||
            budget.phase != quicksift::review::prefetch::ScrollMotionPhase::Settled) {
            // Never enqueue mid-gesture; check again once motion has settled.
            StopThumbnailReconcile();
            ArmThumbnailReconcile(kReconcileIntervalMs);
            return;
        }

        const auto layout = CurrentThumbnailLayout();
        if (layout.columns <= 0 || layout.cellHeight <= 0.0f) {
            StopThumbnailReconcile();
            return;
        }
        const float scroll = reviewState_.Thumbnails().scrollDip;
        // Viewport plus a one-row margin above and below (matches RenderThumbnails).
        const int firstRow = std::max(0, static_cast<int>(scroll / layout.cellHeight) - 1);
        const int lastRow = std::min(std::max(0, layout.rows - 1),
            static_cast<int>((scroll + layout.canvasHeight) / layout.cellHeight) + 1);
        const std::size_t firstIndex = static_cast<std::size_t>(firstRow) * static_cast<std::size_t>(layout.columns);
        const std::size_t endIndex = std::min(catalog_.VisibleCount(),
            static_cast<std::size_t>(lastRow + 1) * static_cast<std::size_t>(layout.columns));
        if (firstIndex >= endIndex) {
            StopThumbnailReconcile();
            return;
        }

        // The worker cancels thumbnail jobs for paths outside its viewport mirror.
        // If any cell in range is not in the published plan (resize, filter/sort
        // change, folder batch), republish first so the re-enqueued jobs survive.
        bool republish = false;
        for (std::size_t index = firstIndex; index < endIndex && !republish; ++index) {
            if (!reviewState_.IsThumbnailPathDesired(VisiblePhoto(index).path.wstring())) republish = true;
        }
        if (republish) PublishThumbnailViewportForCurrentPlan(true);

        const int target = ThumbnailDecodeTarget();
        const uint64_t viewportEpoch = reviewState_.Epochs().thumbnailViewport;
        const std::unordered_set<std::wstring> inFlight = worker_.PendingOrActiveThumbnailPaths();
        const auto now = std::chrono::steady_clock::now();
        constexpr int kMaximumRequeuePerSweep = 96;
        int missing = 0;
        int requeued = 0;
        for (std::size_t index = firstIndex; index < endIndex; ++index) {
            const PhotoItem& photo = VisiblePhoto(index);
            const std::wstring photoPath = photo.path.wstring();
            // Visible items are pinned against eviction on every sweep as well as
            // every paint (the fling paint path used not to pin at all).
            protectedThumbnailPaths_.insert(photoPath);
            const auto cached = bitmapCache_.find(CacheKey{ photoPath, target, CacheClass::Thumbnail });
            if (cached != bitmapCache_.end() && cached->second.bitmap) continue;
            if (inFlight.contains(photoPath)) {
                ++missing; // will complete (or fail into the retry policy)
                continue;
            }
            const std::wstring key = NormalizedPathKey(photo.path);
            int retryAttempt = 0;
            bool forceWic = false;
            if (!ThumbnailDecodeAdmissible(photo, key, now, retryAttempt, forceWic)) {
                // Terminal failures show the failed placeholder and need no backstop;
                // a path inside its backoff is serviced by the retry timer.
                continue;
            }
            ++missing;
            if (requeued >= kMaximumRequeuePerSweep) continue;
            worker_.EnqueuePreview(photo.path, target, generation_, JobPriority::Interactive,
                CacheClass::Thumbnail, 0, viewportEpoch, retryAttempt, forceWic);
            ++requeued;
        }
        if (requeued > 0 && quicksift::diagnostics::VerboseLogging()) {
            QS_LOG_EVENT(quicksift::diagnostics::Level::Trace, L"Thumbnails", L"reconcile_requeue",
                {L"reason", std::wstring(reason ? reason : L"")},
                {L"requeued", std::to_wstring(requeued)},
                {L"missing", std::to_wstring(missing)});
        }
        StopThumbnailReconcile();
        if (missing > 0) ArmThumbnailReconcile(kReconcileIntervalMs);
    }

} // namespace quicksift::app
