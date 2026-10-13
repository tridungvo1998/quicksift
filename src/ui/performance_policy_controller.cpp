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
        if (!IndexForPath(reviewState_.ActivePath())) return;
        // 0.9.4.0: the former Predictive-priority pool requests were always cancelled
        // in Single View (SetInteractiveVisualFocus discards non-Interactive visual
        // work), so idle neighbour prefetch never produced a bitmap. Neighbours are
        // now view-lane prefetch jobs that yield to any foreground view request.
        const int direction = currentAppState_.navigationMomentum > 0 ? 1 :
            (currentAppState_.navigationMomentum < 0 ? -1 : 0);
        // UpdateAppState() runs often; republish only when the active image or the
        // travel direction changed since the last publication.
        const std::wstring prefix = reviewState_.ActivePath() + L"|" + std::to_wstring(direction) +
            L"|" + std::to_wstring(generation_) + L"|";
        if (lastViewPrefetchSignature_.rfind(prefix, 0) == 0) return;
        PublishViewPrefetchNeighbours(direction);
    }

    void QuickSiftApplicationImpl::ReconcileEvictedThumbnails() {
        // 0.9.4.0: EvictBitmapCache arms the visible-cell reconciliation sweep
        // (ID_TIMER_THUMBNAIL_RECONCILE) whenever it reclaims thumbnail bytes. This
        // hook is called from UpdateAppState(), which is reachable from the sweep's
        // own policy refresh, so it must not run the sweep synchronously.
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
            // 0.9.4.0 per-thumbnail-size floor: the budget must hold the visible
            // window + margin rows + one window of predictive rows at the *current*
            // decode size. At Large/Huge sizes on HiDPI the fixed half-budget could
            // be smaller than the pinned working set, so every predictive result was
            // admitted and immediately evicted (decode churn, margin cells blank).
            const auto layout = CurrentThumbnailLayout();
            if (layout.columns > 0 && layout.cellHeight > 0.0f) {
                const size_t target = static_cast<size_t>(std::max(1, ThumbnailDecodeTarget()));
                const size_t perThumbnail = target * target * 4u;
                const size_t windowRows = static_cast<size_t>(layout.canvasHeight / layout.cellHeight) + 3u;
                const size_t workingSet = windowRows * 2u * static_cast<size_t>(layout.columns) * perThumbnail;
                const size_t ceiling = targetBudget / 4 * 3;
                if (workingSet > thumbnailCacheLimit_) {
                    thumbnailCacheLimit_ = std::min(workingSet, ceiling);
                    const size_t remaining = targetBudget > thumbnailCacheLimit_ ? targetBudget - thumbnailCacheLimit_ : 0;
                    previewCacheLimit_ = remaining / 2;
                    fullCacheLimit_ = remaining / 2;
                }
            }
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
        // 0.9.4.0 UI-thread/worker separation contract:
        //  * workers decode, scale, and convert to premultiplied BGRA off the UI thread;
        //  * completions arrive through the bounded, short-lock BackgroundCompletionQueue;
        //  * this drain runs a hard time-budgeted slice (<= 4 ms) and only performs cheap
        //    admission + GPU upload + invalidate per item;
        //  * the remainder is continued after pending input/paint, never ahead of them.
        // Releasing the wake claim first means any producer that pushes from now on
        // may post exactly one new wake; nothing can be stranded behind this slice.
        completionQueue_.ReleaseWake();
        if (completionPumpTimerActive_ && hwnd_) {
            KillTimer(hwnd_, ID_TIMER_COMPLETION_PUMP);
            completionPumpTimerActive_ = false;
        }
        const auto started = std::chrono::steady_clock::now();
        const bool dragPriority = thumbnailScrollDragging_ || ThumbnailScrollPrefersPlaceholders();
        constexpr std::size_t kMaximumItemsPerSlice = 48;
        constexpr auto kMaximumSliceTime = std::chrono::milliseconds(4);
        const std::size_t itemBudget = dragPriority ? 4 : std::clamp<std::size_t>(
            performancePolicy_.uiCompletionItemBudget, 1, kMaximumItemsPerSlice);
        const auto timeBudget = dragPriority ? std::chrono::milliseconds(1) :
            std::min<std::chrono::milliseconds>(performancePolicy_.uiCompletionTimeBudget,
                kMaximumSliceTime);
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
        // responsible for remaining completions. Reposting here would turn a busy
        // decode stream into an input/message-queue flood. Outside a drag, continue
        // with exactly one coalesced continuation that yields to input and paint.
        if (!ThumbnailScrollPrefersPlaceholders() && completionQueue_.Size() != 0)
            ScheduleBackgroundCompletionContinuation();
    }

    void QuickSiftApplicationImpl::ScheduleBackgroundCompletionContinuation() {
        if (!hwnd_ || !IsWindow(hwnd_)) return;
        // Posted messages are retrieved before input and long before WM_PAINT. A
        // drain that re-posts itself while workers keep producing therefore starves
        // painting and input (the classic "UI freezes while thumbnails load" bug).
        // When input or paint is pending, continue from a one-shot WM_TIMER instead:
        // WM_TIMER is synthesized only after input and WM_PAINT have been serviced.
        const DWORD pending = HIWORD(GetQueueStatus(QS_INPUT | QS_PAINT));
        if ((pending & (QS_INPUT | QS_PAINT)) != 0) {
            if (!completionPumpTimerActive_)
                completionPumpTimerActive_ = StartUiTimer(ID_TIMER_COMPLETION_PUMP,
                    USER_TIMER_MINIMUM, L"completion continuation");
            if (completionPumpTimerActive_) return;
        }
        RequestBackgroundCompletionWake();
    }

    void QuickSiftApplicationImpl::RequestBackgroundCompletionWake() {
        if (!hwnd_ || !IsWindow(hwnd_) || completionQueue_.Size() == 0) return;
        if (!completionQueue_.TryClaimWake()) return; // a wake is already outstanding
        if (PostMessageW(hwnd_, WM_APP_BACKGROUND_COMPLETION, 0, 0) == FALSE)
            completionQueue_.ReleaseWake();
    }

} // namespace quicksift::app
