// OWNER: Application lifetime and composition root.
#include "app/quicksift_application.h"
#include "app/quicksift_application_internal.h"
#include "transactions/verified_file_operations_windows.h"
#include "diagnostics/log_and_continue.h"

namespace quicksift::app {
namespace {

// Old diagnostic files are deleted a few seconds after the first frame, so the
// cleanup never competes with startup I/O.
constexpr int kDiagnosticsCleanupDelayMs = 3000;

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
    StopAllWorkersForTeardown();
}

bool QuickSiftApplicationImpl::PostFromWorker(const quicksift::core::worker_lifetime::Token& token,
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept {
    bool posted = false;
    token.RunIfAlive([&]() noexcept {
        posted = window && IsWindow(window) && PostMessageW(window, message, wParam, lParam) != FALSE;
    });
    return posted;
}

void QuickSiftApplicationImpl::StopAllWorkersForTeardown() noexcept {
    // 1. No worker can post to the window any more (blocks until an in-flight
    //    post returns). 2. Every worker sees its stop/cancel flag. 3. Join all,
    //    so none can touch a member after this returns.
    workerLifetime_.Revoke();
    if (workersStoppedForTeardown_) return;
    workersStoppedForTeardown_ = true;
    const auto started = std::chrono::steady_clock::now();
    deferredStartupCancel_.store(true, std::memory_order_release);
    ratingsWorkerCancel_.store(true, std::memory_order_release);
    batchWorkerCancel_.store(true, std::memory_order_release);
    try {
        if (deferredStartupThread_.joinable()) deferredStartupThread_.join();
        if (diagnosticsCleanupThread_.joinable()) diagnosticsCleanupThread_.join();
    } catch (...) {
        QS_LOG_AND_CONTINUE(L"Lifecycle", L"StopAllWorkersForTeardown");
        // Shutdown boundary: never throw.
    }
    StopRatingsWorker();
    StopBatchWorker();
    StopSearchWorker();
    StopSessionLogWorker();
    try {
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        QS_LOG_EVENT(quicksift::diagnostics::Level::Info, L"Lifecycle", L"shutdown_stage",
            {L"stage", L"app_workers_stop"}, {L"ms", std::to_wstring(ms)},
            {L"tokens_left", std::to_wstring(workerLifetime_.Outstanding())});
    } catch (...) {
        QS_CATCH_SILENT_BY_DESIGN("the guarded statement is the shutdown_stage log write");
    }
}

    int QuickSiftApplicationImpl::Run(HINSTANCE instance, int showCommand) {
        instance_ = instance;
        // Theme and language must be known before any window, brush or D2D resource
        // exists and before the first paint. LoadSession() only runs after the
        // deferred cache startup, so a Light user used to get a Dark first frame
        // (title bar, brushes, scrollbars) that was never re-themed.
        LoadEarlyPresentationSettings();
        earlyPresentationLoaded_ = true;
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
        if (deferredStartupThread_.joinable() || workersStoppedForTeardown_) return;
        // The window handle and the token are captured on the UI thread; the
        // worker never reads hwnd_. Teardown joins it before the cache closes.
        deferredStartupThread_ = std::thread([this, window = hwnd_, token = workerLifetime_.Issue()] {
            bool success = true;
            const auto cacheStart = std::chrono::steady_clock::now();
            if (!persistentCache_.InitializeStorage()) success = false;
            const auto cacheMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cacheStart).count();
            QS_LOG_EVENT(quicksift::diagnostics::Level::Info, L"Startup", L"cache_init_complete",
                {L"duration_ms", std::to_wstring(cacheMs)},
                {L"success", success ? L"1" : L"0"});
            if (deferredStartupCancel_.load(std::memory_order_acquire) || token.StopRequested()) return;
            PostFromWorker(token, window, WM_APP_STARTUP_READY, success ? 1 : 0, 0);
        });
        QS_LOG_INFO(L"Startup", L"Deferred startup thread launched after first window frame");
        StartDiagnosticsCleanup();
    }

    void QuickSiftApplicationImpl::StartDiagnosticsCleanup() {
        if (diagnosticsCleanupThread_.joinable() || workersStoppedForTeardown_) return;
        try {
            diagnosticsCleanupThread_ = std::thread([token = workerLifetime_.Issue()]() noexcept {
                // Background mode lowers CPU, I/O and memory priority; a short
                // cancellable delay keeps it out of the way of startup I/O.
                SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
                for (int waited = 0; waited < kDiagnosticsCleanupDelayMs && !token.StopRequested(); waited += 100)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (!token.StopRequested())
                    static_cast<void>(quicksift::diagnostics::CleanupOldDiagnosticFiles(
                        [&token] { return token.StopRequested(); }));
                SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
            });
        } catch (...) {
            QS_LOG_WARNING(L"Diagnostics", L"Old diagnostic file cleanup could not start");
        }
    }

    void QuickSiftApplicationImpl::HandleDeferredStartupReady(bool success) {
        deferredStartupReady_ = success;
        deferredStartupFailed_ = !success;
        if (deferredStartupThread_.joinable()) deferredStartupThread_.join();

        const auto start = std::chrono::steady_clock::now();
        worker_.SetPersistentCache(success ? &persistentCache_ : nullptr);
        const AppTheme themeBefore = theme_;
        const auto languageBefore = localizer_.CurrentLanguage();
        LoadSession();
        // Normally a no-op (both were read before window creation); re-apply only if
        // the session file changed in between.
        if (theme_ != themeBefore) {
            if (controls_.contains(ID_THEME_TOGGLE)) SetWindowTextW(controls_[ID_THEME_TOGGLE], ThemeToggleText().c_str());
            ApplyTheme();
        }
        if (localizer_.CurrentLanguage() != languageBefore) RefreshLocalizedText();
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
        SyncFilmstripMotion(now);
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

    // Round 23: one place drives the filmstrip fade (central Animator, Fade preset,
    // standard motion token; instant when Windows animations are off). Called from
    // the animation tick and directly from ToggleFilmstrip / SetMode so a toggle
    // starts fading on the same frame instead of on the next idle poll; the
    // Animator's activation hook switches the shared timer to frame cadence only
    // while the fade runs.
    void QuickSiftApplicationImpl::SyncFilmstripMotion(std::chrono::steady_clock::time_point now) {
        namespace anim = quicksift::ui::animation;
        namespace film = quicksift::core::filmstrip;
        anim::Animator& motion = uiFramework_.Motion();
        const bool single = reviewState_.Mode() == ViewMode::Single;
        if (!motion.Contains(anim::element::kFilmstrip))
            motion.Snap(anim::element::kFilmstrip, filmstripOpacity_, anim::surface::kCanvas);
        if (film::SnapHiddenOnModeExit(single)) {
            if (filmstripOpacity_ != 0.0f || motion.IsAnimating(anim::element::kFilmstrip)) {
                motion.Snap(anim::element::kFilmstrip, 0.0f, anim::surface::kCanvas);
                filmstripOpacity_ = 0.0f;
            }
            return;
        }
        motion.SetShown(anim::element::kFilmstrip, film::FadeTarget(single, filmstripVisible_),
            MacOsUiFramework::kFilmstripTransition, anim::surface::kCanvas, now);
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
        const auto window = quicksift::core::filmstrip::ComputeWindow(height, visibleCount, currentIndex);
        layout.maxThumbs = window.maxThumbs;
        layout.firstIndex = window.firstIndex;
        layout.lastIndex = window.lastIndex;

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

    // Round 23: tells the worker which filmstrip cells are still wanted (the visible
    // window plus filmstrip::kDecodeMarginCells). Published only when the range,
    // folder generation or visible list changes; an empty range drops every
    // filmstrip-owned decode (strip closed or Single View left).
    void QuickSiftApplicationImpl::PublishFilmstripDecodeWindow(quicksift::core::filmstrip::Range range) {
        const size_t visibleCount = catalog_.VisibleCount();
        if (range.last > visibleCount) range.last = visibleCount;
        if (range.last < range.first) range.first = range.last;
        if (filmstripDecodePublished_ && filmstripDecodeRange_ == range &&
            filmstripDecodeGeneration_ == generation_ && filmstripDecodeVisibleCount_ == visibleCount) return;
        std::vector<std::wstring> paths;
        paths.reserve(range.last - range.first);
        for (size_t i = range.first; i < range.last; ++i) paths.push_back(VisiblePhoto(i).path.wstring());
        worker_.SetFilmstripDecodeWindow(paths);
        filmstripDecodePublished_ = true;
        filmstripDecodeRange_ = range;
        filmstripDecodeGeneration_ = generation_;
        filmstripDecodeVisibleCount_ = visibleCount;
    }

    void QuickSiftApplicationImpl::PrefetchFilmstrip(const FilmstripLayout& layout) {
        if (layout.maxThumbs <= 0 || layout.lastIndex <= layout.firstIndex) {
            PublishFilmstripDecodeWindow({});
            return;
        }
        {
            // Publish BEFORE enqueueing: a filmstrip job is dropped unless its photo
            // is in the published window.
            quicksift::core::filmstrip::Window window;
            window.maxThumbs = layout.maxThumbs;
            window.firstIndex = layout.firstIndex;
            window.lastIndex = layout.lastIndex;
            PublishFilmstripDecodeWindow(quicksift::core::filmstrip::DecodeWindow(window, catalog_.VisibleCount()));
        }

        // Filmstrip cells are small. Request the thumbnail lane and ignore the
        // grid viewport filter, otherwise Single View drops these jobs and the
        // strip stays empty. Rendering stays cache-only.
        // 0.9.4.0: failures go through the shared bounded retry policy
        // (GetOrRequestBitmap checks ThumbnailDecodeAdmissible). The former code
        // erased this window's failure marks on every call, which re-requested a
        // permanently undecodable file on every filmstrip paint.
        // Round 22: the grid's thumbnail bucket, not a fixed 256 px (see
        // filmstrip::ThumbnailTarget): 256 was cancelled/dropped on HiDPI and
        // high-memory profiles, leaving off-grid cells blank forever.
        const int target = quicksift::core::filmstrip::ThumbnailTarget(ThumbnailDecodeTarget());
        for (size_t i = layout.firstIndex; i < layout.lastIndex; ++i) {
            GetOrRequestBitmap(VisiblePhoto(i).path, target, JobPriority::Interactive,
                false, CacheClass::Thumbnail, 0, true);
        }
    }

    bool QuickSiftApplicationImpl::FilmstripContains(float x, float y) const {
        // Round 23: logical state only (not the animated opacity), matching the
        // reserved image area: see filmstrip::Interactive.
        if (reviewState_.Mode() != ViewMode::Single || !filmstripVisible_) return false;
        const D2D1_SIZE_F canvas = CanvasSizeInDips();
        const FilmstripLayout layout = CurrentFilmstripLayout(
            D2D1::RectF(0.0f, 0.0f, canvas.width, canvas.height));
        if (!quicksift::core::filmstrip::Interactive(true, filmstripVisible_,
            layout.maxThumbs <= 0 || layout.lastIndex <= layout.firstIndex)) return false;
        // Round 22: the full drawn band. It used to end at the last cell, so a click
        // on the strip background below it reached the image (pan / click-to-zoom).
        return quicksift::core::filmstrip::BandContains(x, y, canvas.width, canvas.height);
    }

    std::optional<size_t> QuickSiftApplicationImpl::HitFilmstrip(float x, float y) const {
        if (!FilmstripContains(x, y)) return std::nullopt;
        const D2D1_SIZE_F canvas = CanvasSizeInDips();
        const FilmstripLayout layout = CurrentFilmstripLayout(
            D2D1::RectF(0.0f, 0.0f, canvas.width, canvas.height));
        quicksift::core::filmstrip::Window window;
        window.maxThumbs = layout.maxThumbs;
        window.firstIndex = layout.firstIndex;
        window.lastIndex = layout.lastIndex;
        const size_t hit = quicksift::core::filmstrip::HitCell(window, x, y, canvas.width);
        if (hit == quicksift::core::filmstrip::kNoCurrent) return std::nullopt;
        return hit;
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
        if (!filmstripVisible_) PublishFilmstripDecodeWindow({}); // round 23: drop strip decodes
        SyncFilmstripMotion(lastAnimationTick_);
        filmstripWheelAccumulator_ = 0;
        if (filmstripToggle_ && hwnd_) {
            // Round 22: re-place (not just re-glyph) - the toggle sits left of the
            // open strip so it never covers a cell.
            RECT client{};
            GetClientRect(hwnd_, &client);
            PlaceFilmstripToggle(client.right - client.left, client.bottom - client.top);
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
            QS_CATCH_SILENT_BY_DESIGN("the guarded statement is the periodic telemetry log write");
        }
    }

} // namespace quicksift::app
