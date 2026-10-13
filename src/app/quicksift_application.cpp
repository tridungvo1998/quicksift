// OWNER: Application lifetime and composition root.
#include "app/quicksift_application.h"
#include "app/quicksift_application_internal.h"
#include "transactions/verified_file_operations_windows.h"

namespace quicksift::app {
namespace {

quicksift::core::PerformanceBaseline MakePerformanceBaseline(const SystemProfile& profile) noexcept {
    quicksift::core::PerformanceBaseline baseline;
    baseline.totalPhysicalBytes = profile.totalPhysicalBytes;
    baseline.lowMemoryTier = profile.lowMemory;
    baseline.workerPermits = std::max(1u, profile.logicalProcessors);
    baseline.codecThreadsPerDecode = profile.codecThreadsPerDecode;
    baseline.decodeByteLimit = profile.decodeBytesInFlightLimit;
    baseline.readAheadFiles = profile.readAheadFiles;
    baseline.mappedInputLimit = profile.mappedInputLimit;
    baseline.reusableBufferLimit = profile.reusableBufferLimit;
    baseline.retainedDecoderSessions = profile.retainedDecoderSessions;
    baseline.derivativeLevels = profile.derivativeLevels;
    // No adaptive CPU/RAM throttling: the worker pool stays at the hardware
    // maximum for the lifetime of the process. Memory/input/session limits remain
    // fixed safety boundaries selected once from the startup hardware profile.
    baseline.cacheWriteQueueLimit = profile.cacheWriteQueueLimit;
    baseline.codecThreadsPerDecode = profile.codecThreadsPerDecode;
    return baseline;
}

} // namespace

QuickSiftApplication::QuickSiftApplication()
    : implementation_(std::make_unique<QuickSiftApplicationImpl>()) {}

QuickSiftApplication::~QuickSiftApplication() = default;

int QuickSiftApplication::Run(HINSTANCE instance, int showCommand) {
    return implementation_->Run(instance, showCommand);
}

    QuickSiftApplicationImpl::QuickSiftApplicationImpl() : systemProfile_(DetectSystemProfile()),
        performanceGovernor_(MakePerformanceBaseline(systemProfile_)),
        performancePolicy_(performanceGovernor_.Current()),
        transactions_(std::make_unique<quicksift::transactions::WindowsVerifiedFileOperations>()),
        worker_(nullptr, &completionQueue_, &persistentCache_, systemProfile_),
        scanner_(nullptr, &completionQueue_, &persistentCache_, systemProfile_.cpuTopology) {
        formats_[FormatGroup::Jpeg] = true;
        formats_[FormatGroup::Png] = true;
        formats_[FormatGroup::Heif] = true;
        formats_[FormatGroup::Tiff] = true;
        formats_[FormatGroup::Basic] = true;
        formats_[FormatGroup::WebP] = true;
        formats_[FormatGroup::Raw] = true;
        RefreshPerformancePolicy();
        ConfigureCacheBudgets();
        // Metadata is background work. Keep only a small rolling window so a
        // large folder cannot flood the metadata/Exiv2 lane while the user is
        // navigating and force the storage device to seek between unrelated files.
        metadataQueueLimit_ = systemProfile_.lowMemory ? 16 :
            (systemProfile_.totalPhysicalBytes <= 18ull * kGibibyte ? 24 : 32);
        exifCacheLimit_ = systemProfile_.lowMemory ? 128 : 512;
        failedDecodeLimit_ = systemProfile_.lowMemory ? 4096 : 16384;
        historyMemoryLimit_ = systemProfile_.lowMemory ? 96ull * kMebibyte :
            (systemProfile_.totalPhysicalBytes <= 18ull * kGibibyte ? 192ull * kMebibyte : 384ull * kMebibyte);
        historyStore_.SetMemoryLimit(historyMemoryLimit_);
        ConfigureMetadataTransactions();
        // Disk-backed session/cache initialization is deferred until after the first frame.
        worker_.SetRawJpegPreviewOnly(loadOnlyRawJpegPreviews_);
        InitializeColorBlobs();
        QS_LOG_INFO(L"System", L"Runtime profile: " + std::to_wstring(systemProfile_.logicalProcessors) +
            L" logical CPUs, " + std::to_wstring(systemProfile_.totalPhysicalBytes / kMebibyte) +
            L" MiB RAM, " + std::to_wstring(systemProfile_.foregroundWorkers) + L" foreground workers, " +
            std::to_wstring(systemProfile_.idleWorkers) + L" idle workers");
    }

QuickSiftApplicationImpl::~QuickSiftApplicationImpl() {
    deferredStartupCancel_.store(true, std::memory_order_release);
    if (deferredStartupThread_.joinable()) deferredStartupThread_.join();
    StopRatingsWorker();
    StopBatchWorker();
    StopSearchWorker();
}

    int QuickSiftApplicationImpl::Run(HINSTANCE instance, int showCommand) {
        instance_ = instance;
        if (!RegisterClasses()) {
            quicksift::diagnostics::WriteLastError(quicksift::diagnostics::Level::Critical,
                L"Startup", L"Registering window classes");
            return 1;
        }
        if (!CreateMainWindow(showCommand)) {
            quicksift::diagnostics::WriteLastError(quicksift::diagnostics::Level::Critical,
                L"Startup", L"Creating the main window");
            return 1;
        }
        worker_.SetNotify(hwnd_);
        StartDeferredStartup();
        scanner_.SetNotify(hwnd_);
        // The sinks run on transaction threads: capture the window handle by value
        // instead of reading the UI-owned hwnd_ member concurrently (data race).
        // A lost post is harmless: the animation tick and the metadata write-lane
        // timer drain results too.
        transactions_.SetCompletionSinks(
            [target = hwnd_] {
                if (target && IsWindow(target)) {
                    PostMessageW(target, WM_APP_FILE_TRANSACTION_COMPLETION, 0, 0);
                }
            },
            [target = hwnd_] {
                if (target && IsWindow(target)) {
                    PostMessageW(target, WM_APP_METADATA_TRANSACTION_COMPLETION, 0, 0);
                }
            });
        resourcePressureMonitor_.Start(hwnd_, WM_APP_RESOURCE_PRESSURE, dxgiAdapter3_.Get());
        StartUiTimer(ID_TIMER_CACHE_FLUSH, 5000, L"cache-flush");
        StartUiTimer(ID_TIMER_BACKGROUND_ANALYSIS, 1200, L"background-analysis");
        StartUiTimer(ID_TIMER_LOG_PERFORMANCE, 5000, L"performance logging");
        // Idle cadence; UpdateAnimations switches to kAnimationFrameMs only while a
        // chrome animation is in flight (same timer id, no extra timers).
        animationTimerIntervalMs_ = quicksift::ui::tokens::kIdlePollMs;
        StartUiTimer(ID_TIMER_ANIMATION, animationTimerIntervalMs_, L"UI animation");
        // Any transition started between ticks (toast, alert, hover, dropdown fade-out,
        // thumbnail crossfade) switches the same timer to frame cadence at once.
        uiFramework_.Motion().SetActivationHook([this] { UpdateAnimationCadence(); });
        UpdatePerformanceHudTimer();
        QS_LOG_INFO(L"Lifecycle", L"Main window is visible; deferred startup is running asynchronously");

        MSG message{};
        BOOL messageStatus = FALSE;
        while ((messageStatus = GetMessage(&message, nullptr, 0, 0)) > 0) {
            if (HandleGlobalKey(message)) continue;
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        if (messageStatus < 0) {
            quicksift::diagnostics::WriteLastError(quicksift::diagnostics::Level::Critical,
                L"Runtime", L"Reading the Windows message queue");
            return 1;
        }
        return static_cast<int>(message.wParam);
    }

    void QuickSiftApplicationImpl::StartDeferredStartup() {
        if (deferredStartupThread_.joinable()) return;
        const auto start = std::chrono::steady_clock::now();
        deferredStartupThread_ = std::thread([this, start] {
            bool success = true;
            const auto cacheStart = std::chrono::steady_clock::now();
            if (!persistentCache_.InitializeStorage()) success = false;
            const auto cacheMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cacheStart).count();
            QS_LOG_EVENT(quicksift::diagnostics::Level::Info, L"Startup", L"cache_init_complete",
                {L"duration_ms", std::to_wstring(cacheMs)},
                {L"success", success ? L"1" : L"0"});
            if (deferredStartupCancel_.load(std::memory_order_acquire)) return;
            if (hwnd_ && IsWindow(hwnd_)) PostMessageW(hwnd_, WM_APP_STARTUP_READY, success ? 1 : 0, 0);
        });
        QS_LOG_INFO(L"Startup", L"Deferred startup thread launched after first window frame");
    }

    void QuickSiftApplicationImpl::HandleDeferredStartupReady(bool success) {
        deferredStartupReady_ = success;
        deferredStartupFailed_ = !success;
        if (deferredStartupThread_.joinable()) deferredStartupThread_.join();

        const auto start = std::chrono::steady_clock::now();
        worker_.SetPersistentCache(success ? &persistentCache_ : nullptr);
        LoadSession();
        worker_.SetRawJpegPreviewOnly(loadOnlyRawJpegPreviews_);
        RestoreSessionAfterCreate();
        const double totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        QS_LOG_EVENT(quicksift::diagnostics::Level::Info, L"Startup", L"ui_startup_ready",
            {L"success", success ? L"1" : L"0"},
            {L"post_cache_ui_ms", std::to_wstring(totalMs)});
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::UpdateAnimations() {
        const auto now = std::chrono::steady_clock::now();
        lastAnimationTick_ = now;

        // Every chrome transition runs on the framework's central Animator (one
        // registry, token durations/curves, reversible from the current value,
        // instant when Windows animations are off, finished at once while minimized).
        // This timer is its only driver; values are read back below and each
        // surface is invalidated at most once per frame.
        namespace anim = quicksift::ui::animation;
        namespace tokens = quicksift::ui::tokens;
        anim::Animator& motion = uiFramework_.Motion();
        const bool suspended = !hwnd_ || IsIconic(hwnd_) || !IsWindowVisible(hwnd_);
        motion.SetEnabled(quicksift::ui::CurrentVisualEffects().Animate());
        motion.SetSuspended(suspended);

        // Targets (idempotent while in flight). Pane and tabs are linear Toggle
        // channels: their consumers keep applying the symmetric Smoothstep, so the
        // pane geometry, region quantization and reveal hysteresis are unchanged.
        const auto toggle = [&](anim::ElementId id, bool on, int durationMs, anim::SurfaceMask surfaceMask,
            float current) {
            anim::Animator::Options options{};
            options.durationMs = durationMs;
            options.curve = anim::Curve::Linear;
            options.surface = surfaceMask;
            motion.AnimateTo(id, on ? 1.0f : 0.0f, options, now, {}, current);
        };
        toggle(anim::element::kLeftPane, activeFlyout_ != FlyoutPanel::None, tokens::kMotionStandardMs,
            anim::surface::kLeftPane, leftPaneOpenBlend_);
        const std::array<FlyoutPanel, 6> panels{{ FlyoutPanel::Folders, FlyoutPanel::File, FlyoutPanel::Cull,
            FlyoutPanel::Filter, FlyoutPanel::Info, FlyoutPanel::Settings }};
        for (size_t i = 0; i < flyoutTabActiveBlend_.size(); ++i) {
            toggle(anim::element::FlyoutTab(i), activeFlyout_ == panels[i], tokens::kMotionFastMs,
                anim::surface::kFlyoutTabs, flyoutTabActiveBlend_[i]);
        }
        if (!motion.Contains(anim::element::kFilmstrip))
            motion.Snap(anim::element::kFilmstrip, filmstripOpacity_, anim::surface::kCanvas);
        motion.SetShown(anim::element::kFilmstrip, filmstripVisible_,
            MacOsUiFramework::kFilmstripTransition, anim::surface::kCanvas, now);
        SyncSearchFieldTransition(now);

        const anim::SurfaceMask dirty = uiFramework_.TickAnimations(now, suspended);

        // Read back. Pane: geometry frame only (no canvas invalidate: full D2D frames
        // every tick were a primary open/close flicker source under the opaque pane).
        const float pane = motion.Value(anim::element::kLeftPane, leftPaneOpenBlend_);
        if (pane != leftPaneOpenBlend_) {
            leftPaneOpenBlend_ = pane;
            LayoutLeftPaneAnimationFrame();
        }
        bool tabBlendChanged = false;
        for (size_t i = 0; i < flyoutTabActiveBlend_.size(); ++i) {
            const float blend = motion.Value(anim::element::FlyoutTab(i), flyoutTabActiveBlend_[i]);
            if (blend == flyoutTabActiveBlend_[i]) continue;
            flyoutTabActiveBlend_[i] = blend;
            tabBlendChanged = true;
        }
        filmstripOpacity_ = motion.Value(anim::element::kFilmstrip, filmstripOpacity_);
        ApplySearchFieldTransition();

        // Canvas: toast, alert, search field, filmstrip and thumbnail crossfades share
        // ONE invalidate per frame. Face outline fades stay on the idle cadence (their
        // hold/fade policy lives in review::face_interaction).
        if ((dirty & anim::surface::kCanvas) != 0 || faceOutlineFade_.NeedsRepaint(now)) {
            InvalidateCanvas();
        }
        if (tabBlendChanged || (dirty & anim::surface::kFlyoutTabs) != 0) {
            for (HWND tab : flyoutTabs_) {
                if (tab && IsWindowVisible(tab))
                    RedrawWindow(tab, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
            }
        }
        UpdateSearchFieldAnimation();
        ServiceLeftPaneFrost(); // pending/dirty frost: one canvas frame, not per tick
        UpdateAnimationCadence();
    }

    void QuickSiftApplicationImpl::UpdateAnimationCadence() {
        // Adaptive cadence on the SAME timer: ~60 fps only while the Animator has
        // work, back to the idle poll (focus / search demand / toast hold / lost-wake
        // backstop) as soon as every value sits exactly on its target.
        namespace tokens = quicksift::ui::tokens;
        const UINT cadence = uiFramework_.Motion().AnyActive() ? tokens::kAnimationFrameMs : tokens::kIdlePollMs;
        if (cadence != animationTimerIntervalMs_ && hwnd_) {
            if (SetTimer(hwnd_, ID_TIMER_ANIMATION, cadence, nullptr) != 0)
                animationTimerIntervalMs_ = cadence;
        }
    }

    QuickSiftApplicationImpl::FilmstripLayout QuickSiftApplicationImpl::CurrentFilmstripLayout(const D2D1_RECT_F& bounds) const {
        const float height = bounds.bottom - bounds.top;
        const size_t visibleCount = catalog_.VisibleCount();
        const auto current = IndexForPath(reviewState_.SinglePath());
        const size_t currentIndex = current.value_or(std::numeric_limits<size_t>::max());

        if (filmstripLayoutCacheValid_ && filmstripLayoutCacheHeight_ == height &&
            filmstripLayoutCacheVisibleCount_ == visibleCount &&
            filmstripLayoutCacheCurrentIndex_ == currentIndex &&
            filmstripLayoutCacheGeneration_ == generation_) {
            return filmstripLayoutCache_;
        }

        FilmstripLayout layout;
        const float availableHeight = height - layout.top;
        if (availableHeight >= layout.thumbSize + layout.padding && visibleCount > 0 && current) {
            layout.maxThumbs = std::max(1, static_cast<int>(availableHeight /
                (layout.thumbSize + layout.padding)));
            const size_t window = std::min(visibleCount, static_cast<size_t>(layout.maxThumbs));
            const size_t leading = (window - 1) / 2;
            layout.firstIndex = *current > leading ? *current - leading : 0;
            if (layout.firstIndex + window > visibleCount) layout.firstIndex = visibleCount - window;
            layout.lastIndex = layout.firstIndex + window;
        }

        filmstripLayoutCache_ = layout;
        filmstripLayoutCacheHeight_ = height;
        filmstripLayoutCacheVisibleCount_ = visibleCount;
        filmstripLayoutCacheCurrentIndex_ = currentIndex;
        filmstripLayoutCacheGeneration_ = generation_;
        filmstripLayoutCacheValid_ = true;
        return layout;
    }


    bool QuickSiftApplicationImpl::NavigateToSingleIndex(size_t index, int /*direction*/, bool resetView) {
        if (reviewState_.Mode() != ViewMode::Single || index >= catalog_.VisibleCount()) return false;
        const std::wstring path = VisiblePhoto(index).path.wstring();
        if (path == reviewState_.SinglePath()) return false;

        // This helper owns only the shared Single View state mutation. Epochs,
        // FaceLock and navigation events remain in the caller so keyboard and
        // Filmstrip navigation retain their existing ordering.
        reviewState_.SetSinglePath(path);
        reviewState_.SetPrimaryPath(path);
        selection_.Clear();
        selection_.Select(path);
        if (resetView) ResetView();
        return true;
    }

    void QuickSiftApplicationImpl::PrefetchFilmstrip(const FilmstripLayout& layout) {
        if (layout.maxThumbs <= 0 || layout.lastIndex <= layout.firstIndex) return;

        // Filmstrip cells are small. Request the thumbnail lane and ignore the
        // grid viewport filter, otherwise Single View drops these jobs and the
        // strip stays empty. Rendering stays cache-only.
        // 0.9.4.0: failures go through the shared bounded retry policy
        // (GetOrRequestBitmap checks ThumbnailDecodeAdmissible). The former code
        // erased this window's failure marks on every call, which re-requested a
        // permanently undecodable file on every filmstrip paint.
        const int target = 256;
        for (size_t i = layout.firstIndex; i < layout.lastIndex; ++i) {
            GetOrRequestBitmap(VisiblePhoto(i).path, target, JobPriority::Interactive,
                false, CacheClass::Thumbnail, 0, true);
        }
    }

    bool QuickSiftApplicationImpl::FilmstripContains(float x, float y) const {
        if (reviewState_.Mode() != ViewMode::Single || !filmstripVisible_ || filmstripOpacity_ <= 0.001f)
            return false;
        const D2D1_SIZE_F canvas = CanvasSizeInDips();
        const FilmstripLayout layout = CurrentFilmstripLayout(
            D2D1::RectF(0.0f, 0.0f, canvas.width, canvas.height));
        if (layout.maxThumbs <= 0 || layout.lastIndex <= layout.firstIndex) return false;
        const float left = canvas.width - layout.width;
        const float top = layout.top;
        const float bottom = top + layout.padding +
            static_cast<float>(layout.lastIndex - layout.firstIndex) * (layout.thumbSize + layout.padding);
        return x >= left && x <= canvas.width && y >= top && y <= bottom;
    }

    std::optional<size_t> QuickSiftApplicationImpl::HitFilmstrip(float x, float y) const {
        if (!FilmstripContains(x, y)) return std::nullopt;
        const D2D1_SIZE_F canvas = CanvasSizeInDips();
        const FilmstripLayout layout = CurrentFilmstripLayout(
            D2D1::RectF(0.0f, 0.0f, canvas.width, canvas.height));
        const float thumbLeft = canvas.width - layout.width + (layout.width - layout.thumbSize) * 0.5f;
        const float thumbRight = thumbLeft + layout.thumbSize;
        if (x < thumbLeft || x > thumbRight) return std::nullopt;
        float currentY = layout.top + layout.padding;
        for (size_t i = layout.firstIndex; i < layout.lastIndex; ++i) {
            if (y >= currentY && y <= currentY + layout.thumbSize) return i;
            currentY += layout.thumbSize + layout.padding;
        }
        return std::nullopt;
    }

    void QuickSiftApplicationImpl::ToggleFilmstrip() {
        if (reviewState_.Mode() != ViewMode::Single) return;
        filmstripVisible_ = !filmstripVisible_;
        filmstripLayoutCacheValid_ = false;
        lastAnimationTick_ = std::chrono::steady_clock::now();
        if (filmstripVisible_) {
            // Allow a fresh decode attempt for the nearby filmstrip set without
            // clearing failure state globally or creating a paint-time retry loop.
            const auto current = IndexForPath(reviewState_.SinglePath());
            if (current) {
                const D2D1_SIZE_F canvas = CanvasSizeInDips();
                PrefetchFilmstrip(CurrentFilmstripLayout(D2D1::RectF(0.0f, 0.0f, canvas.width, canvas.height)));
            }
        }
        if (filmstripToggle_) {
            SetWindowTextW(filmstripToggle_, filmstripVisible_ ? L"‹" : L"›");
            SetWindowPos(filmstripToggle_, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::LogPerformanceSummary() {
        try {
            const auto telemetry = worker_.Telemetry();
            MEMORYSTATUSEX memory{};
            memory.dwLength = sizeof(memory);
            GlobalMemoryStatusEx(&memory);
            const double averageDecodeMs = telemetry.completedJobs == 0 ? 0.0 :
                telemetry.decodeMilliseconds / static_cast<double>(telemetry.completedJobs);
            QS_LOG_INFO(L"Performance", std::wstring(L"summary ") +
                L"active=" + std::to_wstring(telemetry.activeVisualJobs) +
                L" queued_i=" + std::to_wstring(telemetry.interactiveQueued) +
                L" queued_v=" + std::to_wstring(telemetry.visibleQueued) +
                L" queued_p=" + std::to_wstring(telemetry.predictiveQueued) +
                L" avg_decode_ms=" + std::to_wstring(static_cast<long long>(averageDecodeMs)) +
                L" cache_hits=" + std::to_wstring(telemetry.persistentCacheHits) +
                L" cache_misses=" + std::to_wstring(telemetry.persistentCacheMisses) +
                L" ram_load_pct=" + std::to_wstring(memory.dwMemoryLoad) +
                L" gpu_load=unavailable");
        } catch (...) {
        }
    }

} // namespace quicksift::app
