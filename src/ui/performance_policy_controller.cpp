// OWNER: Fixed performance-policy plumbing and telemetry; pressure observations are non-throttling.
#include "app/quicksift_application_internal.h"

namespace quicksift::app {
namespace {

bool WorkerPolicyChanged(const quicksift::core::PerformancePolicy& left,
    const quicksift::core::PerformancePolicy& right) noexcept {
    return left.activeDecodePermits != right.activeDecodePermits ||
        left.codecThreadsPerDecode != right.codecThreadsPerDecode ||
        left.decodeByteLimit != right.decodeByteLimit ||
        left.allowOversizedDecode != right.allowOversizedDecode ||
        left.readAheadFiles != right.readAheadFiles ||
        left.mappedInputLimit != right.mappedInputLimit ||
        left.reusableBufferLimit != right.reusableBufferLimit ||
        left.retainedDecoderSessions != right.retainedDecoderSessions ||
        left.derivativeLevels != right.derivativeLevels ||
        left.cacheWriteQueueLimit != right.cacheWriteQueueLimit ||
        left.allowPredictive != right.allowPredictive ||
        left.allowAnalysis != right.allowAnalysis ||
        left.allowDerivatives != right.allowDerivatives ||
        left.allowSpeculativeCacheWrites != right.allowSpeculativeCacheWrites;
}

} // namespace

    void QuickSiftApplicationImpl::UpdateAppState() {
        const ViewMode newMode = reviewState_.Mode();
        const bool modeSwitched = currentAppState_.mode != newMode;
        currentAppState_.mode = newMode;
        currentAppState_.faceLockEnabled = reviewState_.FaceLockEnabled();
        currentAppState_.filtersActive = dateFilter_ != DateFilter::Any ||
            ratingFilter_ != RatingFilter::All || pickFilter_ != PickFilter::All ||
            colorLabelFilter_ != ColorLabelFilter::All;
        currentAppState_.viewportScrolling = thumbnailScrollDragging_ ||
            thumbnailScrollMotionActive_ || treeScrollDragging_;
        currentAppState_.mouseOverScrollbar = thumbnailScrollbarHot_ || treeScrollbarHot_;
        const std::wstring focusKey = NormalizedPathKey(fs::path(reviewState_.ActivePath()));
        if (currentAppState_.currentFocusKey != focusKey) {
            currentAppState_.currentFocusKey = focusKey;
            worker_.SetFocusKey(focusKey);
            if (newMode == ViewMode::Single && !focusKey.empty()) (void)worker_.PromoteJob(focusKey, CacheClass::Full);
        }
        (void)modeSwitched;
        ReconcileEvictedThumbnails();
        DispatchIdlePrefetch();
    }

    void QuickSiftApplicationImpl::HandleNavigationEvent(int stepDirection) {
        lastInteractionTimestamp_ = std::chrono::steady_clock::now();
        if (std::abs(stepDirection) > 1) {
            currentAppState_.navigationMomentum = 0;
            worker_.PurgeSpeculative(CacheClass::Full);
        } else {
            currentAppState_.navigationMomentum += stepDirection;
        }
        UpdateAppState();
    }

    void QuickSiftApplicationImpl::DispatchIdlePrefetch() {
        if (currentAppState_.mode != ViewMode::Single || catalog_.VisibleEmpty()) return;
        if (std::chrono::steady_clock::now() - lastInteractionTimestamp_ < std::chrono::milliseconds(750) ||
            !worker_.IsQueueEmpty()) return;
        const auto index = IndexForPath(reviewState_.ActivePath());
        if (!index) return;
        const std::vector<int> offsets = currentAppState_.navigationMomentum > 0 ?
            std::vector<int>{1,2,3,-1,4} : currentAppState_.navigationMomentum < 0 ?
            std::vector<int>{-1,-2,-3,1,-4} : std::vector<int>{1,-1,2,-2};
        for (const int offset : offsets) {
            const auto target = static_cast<long long>(*index) + offset;
            if (target < 0 || target >= static_cast<long long>(catalog_.VisibleCount())) continue;
            const auto& photo = VisiblePhoto(static_cast<size_t>(target));
            worker_.EnqueueFull(photo.path, performancePolicy_.prefetchDecodeTarget, generation_,
                JobPriority::Predictive, CacheClass::Full, reviewState_.Epochs().navigation);
        }
    }

    void QuickSiftApplicationImpl::ReconcileEvictedThumbnails() {
        // Thumbnail viewport ownership is maintained by viewport_prefetch_controller.
        // Cache eviction reconciliation is naturally handled by the next paint/request.
    }

    void QuickSiftApplicationImpl::RefreshPerformancePolicy(bool forceUpdate) {
        const auto previous = performancePolicy_;
        UpdateAppState();
        quicksift::core::PerformanceInputs inputs;
        inputs.appState = currentAppState_;
        const auto next = performanceGovernor_.Update(inputs, quicksift::core::FixedPerformancePolicy::Clock::now(), forceUpdate);
        performancePolicy_ = next;
        ApplyDynamicCacheBudgets(next.decodeByteLimit);
        if (WorkerPolicyChanged(previous, next)) worker_.ApplyPerformancePolicy(next);
    }

    void QuickSiftApplicationImpl::ApplyDynamicCacheBudgets(size_t targetBudget) {
        if (currentAppState_.mode == ViewMode::Thumbnails) {
            thumbnailCacheLimit_ = targetBudget / 2;
            previewCacheLimit_ = targetBudget / 4;
            fullCacheLimit_ = targetBudget / 4;
        } else {
            thumbnailCacheLimit_ = targetBudget / 10;
            previewCacheLimit_ = targetBudget * 4 / 10;
            fullCacheLimit_ = targetBudget / 2;
        }
        EvictBitmapCache();
    }

    AdaptivePrefetchPolicy QuickSiftApplicationImpl::CurrentPrefetchPolicy() {
        RefreshPerformancePolicy();
        AdaptivePrefetchPolicy policy;
        policy.extraThumbnailRows = performancePolicy_.extraThumbnailRows;
        policy.depth = performancePolicy_.prefetchDepth;
        policy.currentTarget = performancePolicy_.currentDecodeTarget;
        policy.prefetchTarget = performancePolicy_.prefetchDecodeTarget;
        policy.thumbnailPredictionHorizonSeconds =
            performancePolicy_.thumbnailPredictionHorizonSeconds;
        return policy;
    }

    void QuickSiftApplicationImpl::DrainBackgroundCompletions() {
        const auto started = std::chrono::steady_clock::now();
        const bool dragPriority = thumbnailScrollDragging_ || ThumbnailScrollPrefersPlaceholders();
        const std::size_t itemBudget = dragPriority ? 4 : std::max<std::size_t>(1,
            performancePolicy_.uiCompletionItemBudget);
        const auto timeBudget = dragPriority ? std::chrono::milliseconds(1) :
            performancePolicy_.uiCompletionTimeBudget;
        std::size_t processed = 0;
        quicksift::work::BackgroundCompletion completion;
        while (processed < itemBudget &&
            std::chrono::steady_clock::now() - started < timeBudget &&
            completionQueue_.TryPopNext(completion)) {
            switch (completion.kind) {
            case quicksift::work::BackgroundCompletionKind::WorkResult:
                if (ThumbnailScrollPrefersPlaceholders() && completion.workResult &&
                    completion.workResult->cacheClass == CacheClass::Thumbnail) {
                    // Decoded pixels are already obsolete during a fast fling/drag.
                    // Acknowledge ownership, but do not perform the expensive UI-side
                    // work: D2D bitmap creation, cache admission, eviction, catalog
                    // persistence, or a repaint.
                    worker_.AcknowledgePostedResult(*completion.workResult);
                } else {
                    OnWorkReady(completion.workResult);
                }
                break;
            case quicksift::work::BackgroundCompletionKind::ScanBatch:
                OnScanBatch(completion.scanBatch);
                break;
            case quicksift::work::BackgroundCompletionKind::ScanComplete:
                OnScanComplete(completion.generation, completion.scanStatus,
                    std::move(completion.detail), completion.nativeError);
                break;
            case quicksift::work::BackgroundCompletionKind::MetadataJobAborted:
                OnMetadataJobAborted(completion.generation);
                break;
            }
            completion = {};
            ++processed;
        }
        // During a scrollbar drag, the periodic drag/timer pump is deliberately
        // responsible for remaining completions. Reposting WM_APP here would turn
        // a busy decode stream into an input/message-queue flood. Outside a drag,
        // maintain one coalesced wake for the remaining queue.
        if (!ThumbnailScrollPrefersPlaceholders() && completionQueue_.Size() != 0 && hwnd_ && IsWindow(hwnd_)) {
            PostMessageW(hwnd_, WM_APP_BACKGROUND_COMPLETION, 0, 0);
        }
    }

} // namespace quicksift::app
