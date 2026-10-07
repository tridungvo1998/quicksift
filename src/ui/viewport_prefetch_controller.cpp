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
        std::unordered_set<std::wstring> residentThumbnailKeys;
        if (enqueueDecodes && !budget.placeholdersOnly) {
            residentThumbnailKeys.reserve(bitmapCache_.size());
            for (const auto& entry : bitmapCache_) {
                if (entry.first.cacheClass == CacheClass::Thumbnail && entry.first.size == target)
                    residentThumbnailKeys.insert(entry.first.path);
            }
        }
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

        unsigned encodedReadAheadQueued = 0;
        int enqueuedThisPass = 0;
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
            int retryAttempt = 0;
            bool forceWic = false;
            const bool visibleRequest = request.tier == quicksift::review::prefetch::PrefetchTier::Visible;
            const bool inCache = residentThumbnailKeys.contains(thumbnailKey);
            const bool inFlight = worker_.IsThumbnailJobPendingOrActive(photo.path);

            if (!inCache && !inFlight) {
                if (const auto retry = thumbnailRetryAttempts_.find(thumbnailKey);
                    retry != thumbnailRetryAttempts_.end()) {
                    retryAttempt = retry->second;
                    forceWic = retryAttempt == 3;
                }
            }

            if (failedThumbnailPaths_.contains(thumbnailKey)) {
                if (!visibleRequest) continue;
                const auto cooldownNow = std::chrono::steady_clock::now();
                if (const auto cooldown = thumbnailRetryCooldown_.find(thumbnailKey);
                    cooldown != thumbnailRetryCooldown_.end()) {
                    if (cooldownNow < cooldown->second) continue;
                    thumbnailRetryCooldown_.erase(cooldown);
                    thumbnailRetryAttempts_.erase(thumbnailKey);
                    failedThumbnailPaths_.erase(thumbnailKey);
                    retryAttempt = 0;
                    forceWic = false;
                } else {
                    const auto prior = thumbnailFailureRetryViewportEpochs_.find(thumbnailKey);
                    if (prior == thumbnailFailureRetryViewportEpochs_.end() || prior->second != viewportEpoch) {
                        thumbnailFailureRetryViewportEpochs_[thumbnailKey] = viewportEpoch;
                        failedThumbnailPaths_.erase(thumbnailKey);
                        thumbnailRetryAttempts_.erase(thumbnailKey);
                        retryAttempt = 0;
                        forceWic = false;
                    } else {
                        continue;
                    }
                }
            }
            const bool shouldEnqueueThumbnail = !inCache && !inFlight;
            if (visibleRequest) {
                if (const auto retry = thumbnailRetryAttempts_.find(thumbnailKey);
                    retry != thumbnailRetryAttempts_.end()) {
                    retryAttempt = retry->second;
                    forceWic = retryAttempt == 3;
                }
            }
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

} // namespace quicksift::app
