// OWNER: Compiled QuickSiftApplication feature module.
#include "app/quicksift_application_internal.h"
#include "core/blur_outline_policy.h"
#include "core/hud_snapshot.h"
#include "core/log_rate_limiter.h"
#include "diagnostics/log_categories.h"
#include "diagnostics/log_and_continue.h"

namespace quicksift::app {

// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Canvas drawing and hit testing only; policy belongs in core/navigation modules.

// Canvas renderer: thumbnails, single/compare views, overlays, alerts, badges,
// hit testing, and per-photo view-state calculations.

    void QuickSiftApplicationImpl::Render() {
        PAINTSTRUCT paint{};
        BeginPaint(canvas_, &paint);
        EnsureDeviceResources();
        if (!renderTarget_) {
            EndPaint(canvas_, &paint);
            return;
        }

        renderTarget_->BeginDraw();
        // Anti-aliased geometry and text for every canvas frame (overlays, face
        // outlines, alerts, toasts). Grayscale text AA is used because the canvas
        // composes translucent overlays, where ClearType would fringe.
        renderTarget_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        renderTarget_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        faceOutlineFade_.BeginFrame();
        // Overlay presence targets before painting, so an alert/toast raised since the
        // last tick starts its transition from this frame (no layout in paint: this
        // only sets animation targets).
        uiFramework_.SyncOverlayTransitions(std::chrono::steady_clock::now());
        // Paint never waits on decode workers or storage: at most one synchronous Shell
        // fallback (failed decodes only) may be created per frame (0.9.4.0).
        shellFallbackFrameBudget_ = ThumbnailScrollPrefersPlaceholders() ? 0 : 1;
        shellFallbackDeferred_ = false;
        const ThemePalette palette = Palette();
        renderTarget_->Clear(ToD2D(palette.windowBottom));
        DrawCanvasBackground();
        protectedThumbnailPaths_.clear();
        protectedViewPaths_.clear();

        const D2D1_SIZE_F canvasSize = renderTarget_->GetSize();
        const D2D1_RECT_F bounds = D2D1::RectF(0, 0, canvasSize.width, canvasSize.height);
        if (catalog_.VisibleEmpty()) {
            const bool metadataFilterActive = ratingFilter_ != RatingFilter::All ||
                pickFilter_ != PickFilter::All || colorLabelFilter_ != ColorLabelFilter::All;
            const bool searchHidesAll = searchActive_ && searchBaseCount_ > 0;
            const bool searchBusy = searchInFlightSerial_ != 0 ||
                searchHydrateDone_.load(std::memory_order_acquire) < searchHydrateTotal_.load(std::memory_order_acquire);
            const wchar_t* message = currentFolder_.empty() ? L"Open a folder to begin" :
                searchHidesAll ? (searchBusy ? L"Searching\u2026" : L"No photos match the search") :
                (catalog_.PhotoCount() > 0 && metadataFilterActive && pendingRatings_ > 0 ?
                    L"Reading metadata for the current filters" :
                    (catalog_.PhotoCount() > 0 ? L"No photos match the current filters" :
                        L"No included photos in this folder"));
            DrawCenteredText(Tr(message), bounds, largeText_.Get(), mutedBrush_.Get());
        } else if (reviewState_.Mode() == ViewMode::Thumbnails) {
            UpdateFullRawOverride({}, false);
            RenderThumbnails(bounds);
        } else if (reviewState_.Mode() == ViewMode::Single) {
            RenderSingle(bounds); // sets or clears the full-RAW override itself
        } else {
            UpdateFullRawOverride({}, false);
            RenderCompare(bounds);
        }
        RenderFilmstrip(bounds);
        DrawCapsLockAutoAdvanceIndicator();
        DrawFirstRunTipOverlay();
        DrawSearchFieldOverlay(); // under toasts and the glass alert veil
        DrawToastOverlay();
        DrawShortcutSheetOverlay(); // above toasts, below the glass alert
        DrawGlassAlertOverlay();
        DrawPerformanceHudOverlay();
        DrawThumbnailScrollbarOverlay();
        DrawCanvasCornerUnderlay();
        faceOutlineFade_.EndFrame();

        const HRESULT hr = renderTarget_->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            DiscardDeviceResources();
        } else if (SUCCEEDED(hr) && swapChain_) {
            // Left-pane frost: read the finished frame under the open pane back (only
            // when dirty / throttled, never during scroll motion) before Present.
            MaybeCaptureLeftPaneFrost();
            const HRESULT present = swapChain_->Present(ThumbnailScrollPrefersPlaceholders() ? 0 : 1, 0);
            if (present == DXGI_ERROR_DEVICE_REMOVED || present == DXGI_ERROR_DEVICE_RESET) {
                DiscardDeviceResources();
            }
        }
        EndPaint(canvas_, &paint);
        // A deferred Shell fallback is created on the next frame (one per frame).
        if (shellFallbackDeferred_ && !ThumbnailScrollPrefersPlaceholders()) InvalidateCanvas();
    }


    void QuickSiftApplicationImpl::ShowToast(const std::wstring& messageKey) {
        uiFramework_.Toasts().Show(Tr(messageKey));
        // Enter/hold/exit run on the central Animator (UpdateAnimations); the sync
        // starts the enter transition now so the timer switches to frame cadence.
        uiFramework_.SyncOverlayTransitions(std::chrono::steady_clock::now());
        if (uiFramework_.Toasts().Visible()) InvalidateCanvas();
    }


    void QuickSiftApplicationImpl::DrawToastOverlay() {
        if (!renderTarget_ || !dwriteFactory_ || !mediumText_) return;
        quicksift::ui::framework::ToastPaintResources resources{};
        resources.target = renderTarget_.Get();
        resources.writeFactory = dwriteFactory_.Get();
        resources.textFormat = mediumText_.Get();
        resources.surfaceSize = renderTarget_->GetSize();
        resources.palette = Palette();
        uiFramework_.PaintToast(resources);
    }


    void QuickSiftApplicationImpl::ShowGlassAlert(const std::wstring& title,
        const std::wstring& message, GlassAlertKind kind) {
        quicksift::ui::framework::AlertRequest request{};
        request.title = Tr(title);
        request.message = Tr(message);
        request.primaryText = Tr(L"OK");
        request.kind = kind;
        request.mode = GlassAlertMode::Notice;
        uiFramework_.Alerts().Show(std::move(request));
        uiFramework_.Toasts().Clear();
        uiFramework_.SyncOverlayTransitions(std::chrono::steady_clock::now());
        SetFocus(canvas_ ? canvas_ : hwnd_);
        InvalidateCanvas();
    }


    void QuickSiftApplicationImpl::ShowGlassConfirmation(const std::wstring& title,
        const std::wstring& message, const std::wstring& confirmText,
        PendingGlassAction action, GlassAlertKind kind, const std::wstring& secondaryText) {
        quicksift::ui::framework::AlertRequest request{};
        request.title = Tr(title);
        request.message = Tr(message);
        request.primaryText = Tr(confirmText);
        request.secondaryText = Tr(secondaryText);
        request.kind = kind;
        request.mode = GlassAlertMode::Confirmation;
        request.destructive = action == PendingGlassAction::DeleteSelection;
        request.actionToken = static_cast<std::uint64_t>(action);
        uiFramework_.Alerts().Show(std::move(request));
        uiFramework_.Toasts().Clear();
        uiFramework_.SyncOverlayTransitions(std::chrono::steady_clock::now());
        SetFocus(canvas_ ? canvas_ : hwnd_);
        InvalidateCanvas();
    }


    void QuickSiftApplicationImpl::ShowGlassAlertText(const std::wstring& title,
        const std::wstring& message, GlassAlertKind kind, PendingGlassAction token) {
        quicksift::ui::framework::AlertRequest request{};
        request.title = title;
        request.message = message;
        request.primaryText = Tr(L"OK");
        request.kind = kind;
        request.mode = GlassAlertMode::Notice;
        // Notices never dispatch; the token only identifies the notice on dismissal.
        request.actionToken = static_cast<std::uint64_t>(token);
        uiFramework_.Alerts().Show(std::move(request));
        uiFramework_.Toasts().Clear();
        uiFramework_.SyncOverlayTransitions(std::chrono::steady_clock::now());
        SetFocus(canvas_ ? canvas_ : hwnd_);
        InvalidateCanvas();
    }


    void QuickSiftApplicationImpl::ShowGlassChoice(const std::wstring& title,
        const std::wstring& message, const std::wstring& primaryText,
        PendingGlassAction primaryAction, const std::wstring& secondaryText,
        GlassAlertKind kind, const std::wstring& tertiaryText,
        PendingGlassAction tertiaryAction, bool tertiaryKeepsOpen) {
        quicksift::ui::framework::AlertRequest request{};
        request.title = title;
        request.message = message;
        request.primaryText = primaryText;
        request.secondaryText = secondaryText;
        request.kind = kind;
        request.mode = GlassAlertMode::Confirmation;
        request.actionToken = static_cast<std::uint64_t>(primaryAction);
        request.tertiaryText = tertiaryText;
        request.tertiaryActionToken = static_cast<std::uint64_t>(tertiaryAction);
        request.tertiaryKeepsOpen = tertiaryKeepsOpen;
        uiFramework_.Alerts().Show(std::move(request));
        uiFramework_.Toasts().Clear();
        uiFramework_.SyncOverlayTransitions(std::chrono::steady_clock::now());
        SetFocus(canvas_ ? canvas_ : hwnd_);
        InvalidateCanvas();
    }


    void QuickSiftApplicationImpl::ActivateGlassAlertTertiary() {
        const auto token = uiFramework_.Alerts().ActivateTertiary();
        InvalidateCanvas();
        if (token) DispatchGlassAction(static_cast<PendingGlassAction>(*token));
    }


    void QuickSiftApplicationImpl::HandleGlassAlertKey(UINT key, bool shift) {
        using quicksift::ui::framework::AlertButton;
        auto& alerts = uiFramework_.Alerts();
        if (!alerts.Visible()) return;
        switch (key) {
        case VK_ESCAPE:
            DismissGlassAlert();
            return;
        case VK_TAB:
            alerts.MoveFocus(shift);
            InvalidateCanvas();
            return;
        case VK_LEFT:
        case VK_UP:
            alerts.MoveFocus(true);
            InvalidateCanvas();
            return;
        case VK_RIGHT:
        case VK_DOWN:
            alerts.MoveFocus(false);
            InvalidateCanvas();
            return;
        case VK_RETURN:
        case VK_SPACE:
            if (alerts.FocusVisible() && alerts.FocusedButton() == AlertButton::Tertiary) {
                ActivateGlassAlertTertiary();
            } else if (alerts.FocusVisible() && alerts.FocusedButton() == AlertButton::Secondary) {
                DismissGlassAlert();
            } else {
                ConfirmGlassAlert();
            }
            return;
        default:
            return;
        }
    }


    void QuickSiftApplicationImpl::ResolveGlassAlert(bool accepted) {
        const PendingGlassAction shownAction = uiFramework_.Alerts().Visible() ?
            static_cast<PendingGlassAction>(uiFramework_.Alerts().Request().actionToken) :
            PendingGlassAction::None;
        const auto actionToken = uiFramework_.Alerts().Resolve(accepted);
        InvalidateCanvas();
        if (!actionToken) {
            // Declining any step of the ratings import chain cancels the import.
            if (shownAction == PendingGlassAction::RatingsImportCountMismatchContinue ||
                shownAction == PendingGlassAction::RatingsImportMissingContinue ||
                shownAction == PendingGlassAction::RatingsImportFinalConfirm) {
                CancelRatingsImport();
            }
            // Declining the batch confirmation (or dismissing the no-match notice)
            // returns to the Batch Processing dialog with its inputs intact.
            if (shownAction == PendingGlassAction::BatchConfirm ||
                shownAction == PendingGlassAction::BatchNoMatches) {
                ReturnToBatchDialog();
            }
            // Skip: the locked files keep their rolled-back values.
            if (shownAction == PendingGlassAction::MetadataRetryInUse) SkipInUseMetadataChanges();
            // "Wait" / "Stop waiting": the operation keeps running; nothing else waits.
            if (shownAction == PendingGlassAction::BlockingWaitPrimary) {
                KillTimer(hwnd_, ID_TIMER_BLOCKING_WAIT);
                if (blockingWaitCommand_ == quicksift::core::blocking_wait::Command::Close)
                    ShowToast(L"QuickSift is still waiting. Close again to see progress.");
                blockingWaitCommand_.reset();
            }
            return;
        }
        DispatchGlassAction(static_cast<PendingGlassAction>(*actionToken));
    }


    void QuickSiftApplicationImpl::DispatchGlassAction(PendingGlassAction action) {
        switch (action) {
        case PendingGlassAction::RatingsExport:
            BeginRatingsExport();
            return;
        case PendingGlassAction::RatingsImport:
            BeginRatingsImport();
            return;
        case PendingGlassAction::RatingsImportCountMismatchContinue:
        case PendingGlassAction::RatingsImportMissingContinue:
        case PendingGlassAction::RatingsImportFinalConfirm:
            ContinueRatingsImport(action);
            return;
        case PendingGlassAction::RatingsImportShowMissing:
            ShowRatingsMissingFilesWindow();
            return;
        case PendingGlassAction::BatchConfirm:
            ApplyBatchProcessing();
            return;
        case PendingGlassAction::BatchShowUnmatched:
            ShowBatchUnmatchedWindow();
            return;
        case PendingGlassAction::BatchNoMatches:
            ReturnToBatchDialog();
            return;
        case PendingGlassAction::MetadataRetryInUse:
            RetryInUseMetadataChanges();
            return;
        case PendingGlassAction::BlockingWaitPrimary:
            OnBlockingWaitPrimary();
            return;
        default:
            break;
        }
        if (action == PendingGlassAction::DeleteSelection) {
            FileOperation(false, true);
            return;
        }
        if (action == PendingGlassAction::RelinquishUndoHistory) {
            RelinquishGuardedHistory(HistoryDirection::Undo);
            return;
        }
        if (action == PendingGlassAction::RelinquishRedoHistory) {
            RelinquishGuardedHistory(HistoryDirection::Redo);
            return;
        }
        if (action == PendingGlassAction::RefreshMetadataFromDisk) {
            RefreshCatalogFromDiskAfterExternalMetadataConflict();
            return;
        }
    }


    void QuickSiftApplicationImpl::DismissGlassAlert() { ResolveGlassAlert(false); }


    void QuickSiftApplicationImpl::ConfirmGlassAlert() {
        const bool confirmation = uiFramework_.Alerts().Visible() &&
            uiFramework_.Alerts().Request().mode == GlassAlertMode::Confirmation;
        ResolveGlassAlert(confirmation);
    }


    bool QuickSiftApplicationImpl::HandleGlassAlertClick(float x, float y) {
        using quicksift::ui::framework::AlertHit;
        switch (uiFramework_.Alerts().HitTest(x, y)) {
        case AlertHit::Primary:
            ConfirmGlassAlert();
            return true;
        case AlertHit::Secondary:
            DismissGlassAlert();
            return true;
        case AlertHit::Tertiary:
            ActivateGlassAlertTertiary();
            return true;
        case AlertHit::ModalBackdrop:
            return true;
        case AlertHit::None:
        default:
            return false;
        }
    }


    void QuickSiftApplicationImpl::UpdateGlassAlertHover(float x, float y) {
        if (uiFramework_.Alerts().UpdateHover(x, y)) InvalidateCanvas();
    }


    void QuickSiftApplicationImpl::DrawGlassAlertOverlay() {
        if (!renderTarget_ || !dwriteFactory_ || !smallText_ || !mediumText_) return;
        quicksift::ui::framework::AlertPaintResources resources{};
        resources.target = renderTarget_.Get();
        resources.writeFactory = dwriteFactory_.Get();
        resources.titleFormat = mediumText_.Get();
        resources.messageFormat = smallText_.Get();
        resources.surfaceSize = renderTarget_->GetSize();
        resources.palette = Palette();
        uiFramework_.PaintAlert(resources);
    }


    void QuickSiftApplicationImpl::DrawCanvasCornerUnderlay() {
        if (!renderTarget_ || !backgroundBrush_ || !canvas_ || !hwnd_) return;
        // The canvas HWND region is a few pixels larger than this radius
        // (ApplyAntialiasedControlRegion); paint the corner patches in the colour of
        // the main-window background behind each corner so the visible edge is an AA
        // arc instead of the 1-bit region staircase.
        RECT canvasRect{};
        if (!GetWindowRect(canvas_, &canvasRect)) return;
        MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&canvasRect), 2);
        const ThemePalette palette = Palette();
        const float radius = static_cast<float>(kPanelRadiusDip);
        const D2D1_SIZE_F size = renderTarget_->GetSize();
        const float pixelsPerDip = std::max(0.5f, CanvasPixelScale());
        const int insetPx = std::max(1, static_cast<int>(std::lround(radius * pixelsPerDip * 0.3f)));
        const auto sample = [&](int x, int y) -> COLORREF {
            // backgroundPixels_ is the downsampled main-window background (0x00RRGGBB).
            if (backgroundPixels_.empty() || backgroundCacheWidth_ <= 0 || backgroundCacheHeight_ <= 0 ||
                backgroundSurfaceWidth_ <= 0 || backgroundSurfaceHeight_ <= 0) {
                const float t = backgroundCacheHeight_ > 0 ? std::clamp(static_cast<float>(y) /
                    static_cast<float>(backgroundCacheHeight_), 0.0f, 1.0f) : 1.0f;
                return BlendColor(palette.windowTop, palette.windowBottom, t);
            }
            const int sx = std::clamp(static_cast<int>((static_cast<long long>(x) * backgroundSurfaceWidth_) /
                backgroundCacheWidth_), 0, backgroundSurfaceWidth_ - 1);
            const int sy = std::clamp(static_cast<int>((static_cast<long long>(y) * backgroundSurfaceHeight_) /
                backgroundCacheHeight_), 0, backgroundSurfaceHeight_ - 1);
            const std::uint32_t pixel = backgroundPixels_[static_cast<std::size_t>(sy) *
                static_cast<std::size_t>(backgroundSurfaceWidth_) + static_cast<std::size_t>(sx)];
            return RGB((pixel >> 16) & 0xFF, (pixel >> 8) & 0xFF, pixel & 0xFF);
        };
        COLORREF topLeft = sample(canvasRect.left + insetPx, canvasRect.top + insetPx);
        COLORREF topRight = sample(canvasRect.right - insetPx, canvasRect.top + insetPx);
        COLORREF bottomRight = sample(canvasRect.right - insetPx, canvasRect.bottom - insetPx);
        COLORREF bottomLeft = sample(canvasRect.left + insetPx, canvasRect.bottom - insetPx);
        if (theme_ == AppTheme::Light) {
            // PaintMainWindow draws a 1-DIP-low rounded shadow under the canvas in
            // the light theme; the bottom corner edge mostly sits on it.
            const COLORREF shadow = BlendColor(palette.windowBottom, RGB(0, 0, 0), 0.038f);
            bottomRight = BlendColor(bottomRight, shadow, 0.5f);
            bottomLeft = BlendColor(bottomLeft, shadow, 0.5f);
        }
        const std::array<D2D1_COLOR_F, 4> colors{{ ToD2D(topLeft), ToD2D(topRight),
            ToD2D(bottomRight), ToD2D(bottomLeft) }};
        uiFramework_.PaintRoundedCornerUnderlay(renderTarget_.Get(), size, radius, colors,
            backgroundBrush_.Get());
    }


    void QuickSiftApplicationImpl::DrawCanvasBackground() {
        if (!renderTarget_) return;
        const D2D1_SIZE_F size = renderTarget_->GetSize();
        const float alpha = theme_ == AppTheme::Dark ? 0.115f : 0.11f;
        for (const ColorBlob& blob : backgroundBlobs_) {
            D2D1_GRADIENT_STOP stops[] = {
                { 0.0f, ToD2D(blob.color, alpha) },
                { 0.60f, ToD2D(blob.color, alpha * (theme_ == AppTheme::Dark ? 0.26f : 0.42f)) },
                { 1.0f, ToD2D(blob.color, 0.0f) }
            };
            ComPtr<ID2D1GradientStopCollection> collection;
            if (FAILED(renderTarget_->CreateGradientStopCollection(stops, static_cast<UINT32>(std::size(stops)),
                D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &collection))) continue;
            const float radius = blob.radius * std::max(size.width, size.height) * (theme_ == AppTheme::Dark ? 2.95f : 2.05f);
            ComPtr<ID2D1RadialGradientBrush> brush;
            const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES properties{
                D2D1::Point2F(blob.x * size.width, blob.y * size.height),
                D2D1::Point2F(0.0f, 0.0f), radius, radius
            };
            if (FAILED(renderTarget_->CreateRadialGradientBrush(properties, collection.Get(), &brush))) continue;
            renderTarget_->FillRectangle(D2D1::RectF(0.0f, 0.0f, size.width, size.height), brush.Get());
        }
    }


    void QuickSiftApplicationImpl::DrawPerformanceHudOverlay() {
        if (!performanceHudVisible_ || !renderTarget_ || !smallText_ || !overlayBrush_ || !textBrush_) return;
        namespace hud = quicksift::core::hud;
        const auto telemetry = worker_.Telemetry(); // one short lock; heads-only lane scan
        const auto now = std::chrono::steady_clock::now();
        // O(n) catalog counts and the blocker probe are refreshed at most once a second.
        if (now - hudCountsAt_ >= std::chrono::seconds(1)) {
            hudCountsAt_ = now;
            hudPendingRating_ = 0;
            hudPendingPick_ = 0;
            for (std::size_t index = 0; index < catalog_.PhotoCount(); ++index) {
                const PhotoItem& photo = catalog_.PhotoAt(index);
                if (photo.ratingKnowledge == MetadataKnowledge::Unknown) ++hudPendingRating_;
                if (photo.pickStateKnowledge == MetadataKnowledge::Unknown) ++hudPendingPick_;
            }
            MEMORYSTATUSEX memory{ sizeof(memory) };
            hudRamFreeMiB_ = GlobalMemoryStatusEx(&memory) ? memory.ullAvailPhys / kMebibyte : 0;
            const auto blockers = CurrentBlockers(!blockingWaitCommand_ ||
                *blockingWaitCommand_ == quicksift::core::blocking_wait::Command::Close);
            hudBlockingReason_ = blockers.empty() ? std::wstring{} :
                Tr(quicksift::core::blocking_wait::ReasonKey(blockers.front().reason));
        }
        hud::HudSnapshot snapshot;
        snapshot.interactive = telemetry.interactiveQueued;
        snapshot.visible = telemetry.visibleQueued;
        snapshot.predictive = telemetry.predictiveQueued;
        snapshot.idle = telemetry.idleQueued;
        snapshot.analysis = telemetry.analysisQueued;
        snapshot.view = telemetry.viewQueued;
        snapshot.viewPrefetch = telemetry.viewPrefetchQueued;
        snapshot.faces = telemetry.faceQueued;
        snapshot.metadata = telemetry.metadataQueued + pendingMetadataIndices_.size();
        snapshot.writes = telemetry.cacheWritesQueued + pendingMetadataEdits_.Size();
        snapshot.activeVisual = telemetry.activeVisualJobs;
        snapshot.activeForeground = telemetry.activeForegroundVisualJobs;
        snapshot.metadataInFlight = metadataJobsInFlight_;
        snapshot.pendingMetadata = pendingRatings_;
        snapshot.pendingRating = hudPendingRating_;
        snapshot.pendingPick = hudPendingPick_;
        snapshot.oldestQueuedMs = telemetry.oldestQueuedMs;
        snapshot.cacheMiB = static_cast<std::uint64_t>(TotalCacheBytes()) / kMebibyte;
        snapshot.ramFreeMiB = hudRamFreeMiB_;
        snapshot.diskHits = telemetry.persistentCacheHits;
        snapshot.diskMisses = telemetry.persistentCacheMisses;
        snapshot.lastJpegMs = telemetry.lastDecodeMillisecondsByFormat[0];
        snapshot.lastRawMs = telemetry.lastDecodeMillisecondsByFormat[1];
        snapshot.avgDecodeMs = telemetry.decodeMilliseconds;
        snapshot.queueWaitMs = telemetry.queueWaitMilliseconds;
        snapshot.metadataLaneState = telemetry.metadataLaneState;
        snapshot.metadataWriteBusy = transactions_.MetadataBusy();
        snapshot.fileOperationBusy = transactions_.FileBusy();
        snapshot.ratingsWorkerBusy = RatingsWorkerBusy();
        snapshot.batchWorkerBusy = BatchWorkerBusy();
        snapshot.workerStopping = telemetry.stopping;
        snapshot.blockingReason = hudBlockingReason_;
        snapshot.focus = reviewState_.Mode() == ViewMode::Thumbnails ? L"THUMBNAILS" :
            reviewState_.Mode() == ViewMode::Single ? L"SINGLE VIEW" : L"COMPARE";
        const std::wstring text = hud::FormatHud(snapshot);

        // Size from the measured text (DirectWrite works in DIPs, so this is DPI-aware).
        // The layout is rebuilt only when the text changes; the width only grows while
        // the HUD is shown so changing digits do not make the panel jitter.
        if (!hudLayout_ || text != hudLayoutText_) {
            hudLayout_.Reset();
            if (!dwriteFactory_ || FAILED(dwriteFactory_->CreateTextLayout(text.c_str(),
                    static_cast<UINT32>(text.size()), smallText_.Get(), 4096.0f, 4096.0f, &hudLayout_))) {
                return;
            }
            hudLayout_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            hudLayout_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            hudLayout_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            hudLayoutText_ = text;
        }
        DWRITE_TEXT_METRICS metrics{};
        if (FAILED(hudLayout_->GetMetrics(&metrics))) return;
        hudTextWidthDip_ = std::max(hudTextWidthDip_, std::ceil(metrics.widthIncludingTrailingWhitespace));
        const float textHeight = std::ceil(metrics.height);

        const D2D1_SIZE_F size = renderTarget_->GetSize();
        // Content area = canvas minus the left hover-rail inset; obstacles = every
        // overlay painted on the canvas (search field) and every sibling control
        // over it (search input, filmstrip toggle, pane, tabs...). The drag handle
        // and first line are never left under one of them.
        const hud::PanelRect content{ std::min(kThumbnailRailSafeInsetDip * 0.5f, size.width * 0.25f), 0.0f,
            size.width, size.height };
        std::vector<hud::PanelRect> obstacles;
        if (SearchFieldAvailable()) {
            const D2D1_RECT_F frame = SearchFieldFrameDip();
            obstacles.push_back({ frame.left - 4.0f, frame.top - 4.0f, frame.right + 4.0f,
                frame.bottom + 8.0f });
        }
        if (canvas_ && hwnd_) {
            RECT canvasScreen{};
            GetWindowRect(canvas_, &canvasScreen);
            for (HWND child = GetWindow(hwnd_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
                if (child == canvas_ || !IsWindowVisible(child)) continue;
                RECT rc{};
                GetWindowRect(child, &rc);
                RECT overlap{};
                if (!IntersectRect(&overlap, &rc, &canvasScreen)) continue;
                const auto topLeft = CanvasPixelsToDips(static_cast<int>(overlap.left - canvasScreen.left),
                    static_cast<int>(overlap.top - canvasScreen.top));
                const auto bottomRight = CanvasPixelsToDips(static_cast<int>(overlap.right - canvasScreen.left),
                    static_cast<int>(overlap.bottom - canvasScreen.top));
                obstacles.push_back({ topLeft.x, topLeft.y, bottomRight.x, bottomRight.y });
            }
        }
        const hud::PanelRect placed = hud::PlacePanelAvoiding(hudX_, hudY_, hudTextWidthDip_, textHeight,
            content, obstacles.data(), obstacles.size());
        hudPanelRect_ = placed;
        const D2D1_RECT_F panel = D2D1::RectF(placed.left, placed.top, placed.right, placed.bottom);
        const FLOAT previousOpacity = overlayBrush_->GetOpacity();
        overlayBrush_->SetOpacity(0.80f);
        uiFramework_.PaintOverlayLabel(renderTarget_.Get(), panel,
            static_cast<float>(kOverlayPanelRadiusDip), L"", smallText_.Get(),
            overlayBrush_.Get(), textBrush_.Get());
        overlayBrush_->SetOpacity(previousOpacity);
        // Visible drag handle: a grip pill centred in the top strip.
        const hud::PanelRect handle = hud::HandleRect(placed);
        const float centerX = (handle.left + handle.right) * 0.5f;
        const float centerY = handle.top + (handle.bottom - handle.top) * 0.55f;
        const FLOAT textOpacity = textBrush_->GetOpacity();
        textBrush_->SetOpacity(hudDragging_ || hudHandleHot_ ? 0.85f : 0.45f);
        uiFramework_.PaintDragGrip(renderTarget_.Get(), D2D1::Point2F(centerX, centerY), textBrush_.Get());
        textBrush_->SetOpacity(textOpacity);
        renderTarget_->DrawTextLayout(D2D1::Point2F(placed.left + hud::kPaddingDip,
            placed.top + hud::kPaddingDip * 0.5f + hud::kHandleDip), hudLayout_.Get(), textBrush_.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }


    bool QuickSiftApplicationImpl::HudHandleHit(float x, float y) const noexcept {
        return performanceHudVisible_ &&
            quicksift::core::hud::Contains(quicksift::core::hud::HandleRect(hudPanelRect_), x, y);
    }

    D2D1_RECT_F QuickSiftApplicationImpl::ThumbnailScrollbarThumbRect() const {
        if (reviewState_.Mode() != ViewMode::Thumbnails) return D2D1::RectF();
        const auto layout = CurrentThumbnailLayout();
        if (layout.maximumScroll <= 0.0f) return D2D1::RectF();

        const float margin = 6.0f;
        const float width = 8.0f;
        const float trackTop = margin;
        const float trackBottom = layout.canvasHeight - margin;
        const float trackHeight = std::max(1.0f, trackBottom - trackTop);
        const float thumbHeight = std::clamp(
            (layout.canvasHeight / std::max(1.0f, layout.contentHeight)) * trackHeight, 32.0f, trackHeight);
        const float travel = std::max(0.0f, trackHeight - thumbHeight);
        const float ratio = std::clamp(reviewState_.Thumbnails().scrollDip / layout.maximumScroll, 0.0f, 1.0f);
        const float top = trackTop + travel * ratio;
        return D2D1::RectF(layout.canvasWidth - margin - width, top,
            layout.canvasWidth - margin, top + thumbHeight);
    }

    void QuickSiftApplicationImpl::DrawThumbnailScrollbarOverlay() {
        if (reviewState_.Mode() != ViewMode::Thumbnails || !renderTarget_) return;
        const D2D1_RECT_F thumb = ThumbnailScrollbarThumbRect();
        if (thumb.right <= thumb.left) return;

        const ThemePalette palette = Palette();
        const auto layout = CurrentThumbnailLayout();
        const D2D1_RECT_F track = D2D1::RectF(thumb.left, 6.0f, thumb.right, layout.canvasHeight - 6.0f);
        uiFramework_.PaintOverlayScrollbar(renderTarget_.Get(), thumb, track,
            thumbnailScrollbarHot_, thumbnailScrollDragging_, palette);
    }

    void QuickSiftApplicationImpl::DrawCenteredText(const std::wstring& text, D2D1_RECT_F rect, IDWriteTextFormat* format, ID2D1Brush* brush) {
        uiFramework_.PaintCenteredText(renderTarget_.Get(), text, format, rect, brush);
    }


    quicksift::review::thumbnail::ThumbnailLayoutMetrics QuickSiftApplicationImpl::CurrentThumbnailLayout() const {
        const D2D1_SIZE_F canvas = CanvasSizeInDips();
        return quicksift::review::thumbnail::CalculateThumbnailLayout(canvas.width, canvas.height,
            reviewState_.Thumbnails().sizeDip, catalog_.VisibleCount(), kThumbnailRailSafeInsetDip,
            ThumbnailSearchLaneDip());
    }


    D2D1_RECT_F QuickSiftApplicationImpl::ThumbnailRectForIndex(size_t index) const {
        const auto rect = quicksift::review::thumbnail::ThumbnailCardRect(
            CurrentThumbnailLayout(), index, reviewState_.Thumbnails().scrollDip);
        return D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom);
    }


    void QuickSiftApplicationImpl::RenderThumbnails(const D2D1_RECT_F& bounds) {
        const int thumbnailTarget = ThumbnailDecodeTarget();
        worker_.SetThumbnailTarget(thumbnailTarget);
        const auto layout = quicksift::review::thumbnail::CalculateThumbnailLayout(
            bounds.right, bounds.bottom, reviewState_.Thumbnails().sizeDip, catalog_.VisibleCount(),
            kThumbnailRailSafeInsetDip, ThumbnailSearchLaneDip());
        // First-row origin: padding, or the lane reserved for the floating search field.
        const float padding = layout.topInset;
        const float leftInset = layout.leftInset;
        const float cellW = layout.cellWidth;
        const float cellH = layout.cellHeight;
        const int columns = layout.columns;
        const float unclampedScroll = reviewState_.Thumbnails().scrollDip;
        reviewState_.SetThumbnailMaximumScroll(layout.maximumScroll);
        const float thumbnailScroll = reviewState_.Thumbnails().scrollDip;
        if (thumbnailScroll != unclampedScroll) thumbnailPrefetchPlanner_.Reset(thumbnailScroll);
        {
            // Remember which grid positions are on screen so rating indexing can
            // go visible-first (PumpMetadataQueue reorders on its next call).
            const auto shownRows = quicksift::review::thumbnail::VisibleThumbnailRows(
                layout, thumbnailScroll, bounds.bottom);
            quicksift::core::metadata_order::VisibleWindow shown;
            if (!shownRows.Empty() && catalog_.VisibleCount() > 0) {
                shown.first = static_cast<size_t>(shownRows.first) * static_cast<size_t>(columns);
                shown.last = std::min(catalog_.VisibleCount(),
                    static_cast<size_t>(shownRows.last + 1) * static_cast<size_t>(columns)) - 1;
                if (shown.first > shown.last) shown = {};
            }
            thumbnailShownPositions_ = shown;
        }

        // Fast scroll (scrollbar fling or wheel fling) gets a deliberately cheap
        // render path. The normal card renderer performs per-card string/hash work,
        // cache LRU touches, text/badge drawing, and fallback bitmap creation. None
        // of that belongs on the hot path of a high-frequency scroll gesture.
        if (ThumbnailScrollPrefersPlaceholders()) {
            const auto visibleRows = quicksift::review::thumbnail::VisibleThumbnailRows(
                layout, thumbnailScroll, bounds.bottom);
            const int firstRow = visibleRows.first;
            const int lastRow = visibleRows.last;
            for (int row = firstRow; row <= lastRow; ++row) {
                for (int column = 0; column < columns; ++column) {
                    const size_t index = static_cast<size_t>(row * columns + column);
                    if (index >= catalog_.VisibleCount()) break;
                    const float x = leftInset + column * cellW;
                    const float y = padding + row * cellH - thumbnailScroll;
                    const D2D1_RECT_F imageRect = D2D1::RectF(
                        x + 4, y + 4, x + 4 + reviewState_.Thumbnails().sizeDip,
                        y + 4 + reviewState_.Thumbnails().sizeDip);
                    const PhotoItem& photo = VisiblePhoto(index);
                    const CacheKey cacheKey{ photo.path.wstring(), thumbnailTarget, CacheClass::Thumbnail };
                    // 0.9.4.0: pin what is on screen during flings too. Only the
                    // normal card path used to pin, so an admission burst at fling
                    // end could evict the very bitmaps being painted here.
                    protectedThumbnailPaths_.insert(cacheKey.path);
                    const auto it = bitmapCache_.find(cacheKey);
                    if (it != bitmapCache_.end() && it->second.bitmap) {
                        const auto view = quicksift::review::transform::ResolvedView{};
                        DrawBitmapFit(&it->second, imageRect, view, false);
                    } else {
                        if (panelBrush_) {
                            // Never invoke Shell/WIC or create a new bitmap while the
                            // thumb is moving. A cheap placeholder is preferable
                            // (rounded like the image well, not a hard square).
                            const float placeholderRadius = static_cast<float>(
                                quicksift::ui::tokens::ConcentricRadius(kCardRadiusDip, 4));
                            uiFramework_.PaintImagePlaceholder(renderTarget_.Get(), imageRect,
                                placeholderRadius, panelBrush_.Get());
                        }
                        // Backstop: the sweep defers itself until motion settles.
                        ArmThumbnailReconcile(400);
                    }
                }
            }
            return;
        }

        // Virtualized: only rows that intersect the viewport are painted. The former
        // +/-1 row margin painted (and requested at Interactive priority) whole rows
        // of off-screen cards every frame; neighbours belong to the prefetch planner.
        const auto visibleRows = quicksift::review::thumbnail::VisibleThumbnailRows(
            layout, thumbnailScroll, bounds.bottom);
        const int firstRow = visibleRows.first;
        const int lastRow = visibleRows.last;
        for (int row = firstRow; row <= lastRow; ++row) {
            for (int column = 0; column < columns; ++column) {
                const size_t index = static_cast<size_t>(row * columns + column);
                if (index >= catalog_.VisibleCount()) break;
                const float x = leftInset + column * cellW;
                const float y = padding + row * cellH - thumbnailScroll;
                D2D1_RECT_F cell = D2D1::RectF(x, y, x + cellW - 8, y + cellH - 8);
                D2D1_RECT_F imageRect = D2D1::RectF(cell.left + 4, cell.top + 4,
                    cell.left + 4 + reviewState_.Thumbnails().sizeDip,
                    cell.top + 4 + reviewState_.Thumbnails().sizeDip);
                const PhotoItem& photo = VisiblePhoto(index);
                // One path materialization per card (fs::path::wstring() copies).
                const std::wstring photoPath = photo.path.wstring();
                const bool selected = selection_.Contains(photoPath);
                const bool focused = reviewState_.ThumbnailFocusPath() == photoPath;

                quicksift::ui::framework::MediaCardPresentation cardPresentation{};
                cardPresentation.rect = cell;
                cardPresentation.radius = static_cast<float>(kCardRadiusDip);
                cardPresentation.selected = selected;
                cardPresentation.focused = focused;
                cardPresentation.warning = HasFaceFocusWarning(photo);
                // Round 23: blurry photo = theme red DASHED outline (was solid danger).
                uiFramework_.PaintMediaCard(renderTarget_.Get(), cardPresentation,
                    { panelBrush_.Get(), glassBorderBrush_.Get(), accentBrush_.Get(),
                      blurOutlineBrush_ ? blurOutlineBrush_.Get() : dangerBrush_.Get(),
                      blurOutlineStrokeStyle_.Get() });

                protectedThumbnailPaths_.insert(photoPath);
                BitmapEntry* bitmap = GetOrRequestBitmap(photo.path, thumbnailTarget,
                    JobPriority::Interactive, false, CacheClass::Thumbnail);
                if (bitmap && bitmap->bitmap) {
                    const ViewState fitState{};
                    const auto fitView = ResolveViewForPath(photoPath, imageRect, fitState,
                        bitmap->sourceWidth, bitmap->sourceHeight);
                    DrawBitmapFit(bitmap, imageRect, fitView, false, ThumbnailRevealOpacity(photoPath));
                } else if (thumbnailScrollMotionActive_) {
                    if (panelBrush_) {
                        const float placeholderRadius = static_cast<float>(
                            quicksift::ui::tokens::ConcentricRadius(kCardRadiusDip, 4));
                        uiFramework_.PaintImagePlaceholder(renderTarget_.Get(), imageRect,
                                placeholderRadius, panelBrush_.Get());
                    }
                    NoteThumbnailPlaceholder(photoPath);
                    ArmThumbnailReconcile(400);
                } else {
                    // 0.9.4.0: a not-yet-decoded thumbnail paints a cheap placeholder;
                    // the worker result arrives through the completion queue. The
                    // failed placeholder appears only after the retry policy's final
                    // attempt (terminal); the cached Shell thumbnail, budgeted to one
                    // per frame, is drawn behind it when Windows can provide one.
                    const bool terminalFailure = !failedThumbnailPaths_.empty() &&
                        failedThumbnailPaths_.contains(NormalizedPathKey(photo.path));
                    ComPtr<ID2D1Bitmap> shell;
                    if (terminalFailure) shell = GetOrCreateShellFallback(photo.path, thumbnailTarget);
                    if (shell) {
                        renderTarget_->DrawBitmap(shell.Get(), imageRect, 0.85f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    } else if (terminalFailure) {
                        DrawCenteredText(Tr(L"Couldn't load image"), imageRect, smallText_.Get(), mutedBrush_.Get());
                    } else {
                        DrawCenteredText(Tr(L"loading…"), imageRect, smallText_.Get(), mutedBrush_.Get());
                        NoteThumbnailPlaceholder(photoPath);
                        // Nothing visible may stay blank permanently: keep the
                        // low-rate reconciliation sweep alive while cells are missing.
                        ArmThumbnailReconcile(400);
                    }
                }

                const D2D1_RECT_F nameRect = D2D1::RectF(cell.left + 5,
                    imageRect.bottom + 5, cell.right - 5, cell.bottom - 21);
                uiFramework_.PaintSingleLineText(renderTarget_.Get(), photo.name,
                    smallText_.Get(), nameRect, textBrush_.Get());

                DrawThumbnailBadges(photo, D2D1::RectF(cell.left + 5,
                    cell.bottom - 19, cell.right - 5, cell.bottom - 2));
            }
        }
    }

    std::wstring QuickSiftApplicationImpl::Stars(int rating) const {
        if (rating <= 0) return Tr(L"Unrated");
        std::wstring result;
        for (int i = 0; i < rating; ++i) result += L'★';
        return result;
    }


    std::wstring QuickSiftApplicationImpl::RatingStateText(const PhotoItem& photo) const {
        switch (photo.ratingKnowledge) {
        case MetadataKnowledge::Known: return Stars(photo.rating);
        case MetadataKnowledge::Unknown: return Tr(L"Rating pending");
        case MetadataKnowledge::Unreadable: return Tr(L"Rating unreadable");
        case MetadataKnowledge::Unsupported: return Tr(L"Custom rating");
        }
        return Tr(L"Rating pending");
    }


    std::wstring QuickSiftApplicationImpl::CullStateText(const PhotoItem& photo) const {
        switch (photo.pickStateKnowledge) {
        case MetadataKnowledge::Unknown: return Tr(L"Pick state pending");
        case MetadataKnowledge::Unreadable: return Tr(L"Pick state unreadable");
        case MetadataKnowledge::Unsupported: return Tr(L"Custom pick state");
        case MetadataKnowledge::Known: break;
        }
        return Tr(quicksift::core::vocabulary::Pick(photo.pickState).state);
    }


    bool QuickSiftApplicationImpl::ViewBitmapSettled(const BitmapEntry* bitmap,
        const PhotoItem& photo, int displayDesired) const {
        // True once the on-screen view bitmap is the final quality the current
        // request can produce: large enough for the display (or the native edge,
        // whichever is smaller) and not an embedded preview unless JPG-preview-only
        // mode makes the preview final for RAW.
        if (!bitmap || !bitmap->bitmap) return false;
        int nativeEdge = std::max(photo.sourceWidth, photo.sourceHeight);
        nativeEdge = std::max(nativeEdge, std::max(bitmap->sourceWidth, bitmap->sourceHeight));
        // Round 21 (t29u): bounded by the decode cap too, and an exact full-quality
        // result for the requested bucket is final even when LibRaw half-size kept it
        // below that edge. Otherwise a zoomed large RAW/JPEG never "settled" and the
        // 400 ms backstop repainted (and, for RAW, re-decoded) forever.
        const int readyEdge = quicksift::core::view_load::SatisfiableViewEdge(displayDesired,
            nativeEdge, MaximumViewDecodeEdge());
        const bool previewIsFinal = IsRawExtension(photo.extension) && RawPreviewOnlyFor(photo.path.native());
        if (BitmapSatisfiesDisplay(bitmap, readyEdge, !previewIsFinal)) return true;
        if (previewIsFinal) return false;
        const auto exact = bitmapCache_.find(CacheKey{ photo.path.wstring(),
            BucketFor(std::max(1, displayDesired)), CacheClass::Full });
        return exact != bitmapCache_.end() && exact->second.bitmap && !exact->second.previewOnly;
    }

    void QuickSiftApplicationImpl::RenderSingle(const D2D1_RECT_F& bounds) {
        const auto index = IndexForPath(reviewState_.SinglePath());
        if (!index) {
            EnterSingleFromSelection();
            return;
        }
        const PhotoItem& photo = VisiblePhoto(*index);
        protectedViewPaths_.insert(reviewState_.SinglePath());
        const D2D1_RECT_F imageArea = SingleImageArea(bounds);
        // Decode size from the unreserved area: toggling the filmstrip must not
        // change the bucket and re-decode the image on screen (round 22).
        const float reservedDip = FilmstripReservedDip(bounds);
        const int baseDesired = static_cast<int>(std::max(imageArea.right - imageArea.left + reservedDip,
            imageArea.bottom - imageArea.top) * CanvasPixelScale() * 1.25f);

        BitmapEntry* bitmap = nullptr;
        const bool raw = IsRawExtension(photo.extension);
        // 0.9.4.0: no UI-thread decoding. The former WIC image source
        // (D2D1_IMAGE_SOURCE_LOADING_OPTIONS_CACHE_ON_DEMAND) decoded the JPEG/PNG/
        // HEIC on the UI thread inside DrawImage, and opened the file on paint.
        // Every pixel now comes from the dedicated view-lane worker; until it lands,
        // paint shows the best cached preview or a scaled-up thumbnail.
        if (raw) {
            // Always make the embedded JPEG the first interactive request, then let
            // the full RAW decode replace it at the same cache size.
            bitmap = GetOrRequestBitmap(photo.path, baseDesired, JobPriority::Interactive, false);
            BitmapEntry* full = GetOrRequestBitmap(photo.path, baseDesired, JobPriority::Interactive, true);
            if (full) bitmap = full;
        } else {
            // WIC is a useful immediate fallback, but it must never suppress the
            // authoritative worker Full decode. The previous branch skipped the
            // Full request whenever a modern WIC image source existed, which could
            // leave Single View permanently dependent on the source path.
            const int intermediateTarget = IntermediateViewDecodeTarget(768, baseDesired);
            if (intermediateTarget > 0 && intermediateTarget < baseDesired) {
                BitmapEntry* intermediate = GetOrRequestIntermediateViewBitmap(
                    photo.path, intermediateTarget, JobPriority::Interactive);
                if (intermediate) bitmap = intermediate;
            }
            BitmapEntry* full = GetOrRequestBitmap(photo.path, baseDesired,
                JobPriority::Interactive, true, CacheClass::Full);
            if (full) bitmap = full;
        }

        // Semantic modes are resolved against the current pane on every frame;
        // Custom stores an absolute physical scale shared correctly across panes.
        const ViewState state = ViewForPath(reviewState_.SinglePath(), imageArea);
        int fallbackWidth = bitmap ? bitmap->sourceWidth : 0;
        int fallbackHeight = bitmap ? bitmap->sourceHeight : 0;
        auto resolvedView = ResolveViewForPath(reviewState_.SinglePath(), imageArea, state,
            fallbackWidth, fallbackHeight);
        // JPG-preview mode: zooming past Fit on a RAW decodes that one RAW in full.
        UpdateFullRawOverride(reviewState_.SinglePath(),
            resolvedView.relativeZoom > 1.001f && IsRawExtension(photo.extension));
        int displayDesired = baseDesired;
        if (resolvedView.relativeZoom > 1.001f) {
            // Once the user zooms past Fit, request the actual native-resolution
            // Full entry. Do not derive the decode target only from the Fit-sized
            // bitmap: doing so can leave a highly zoomed view sampling the same
            // low-resolution Fit image.
            int nativeEdge = std::max(photo.sourceWidth, photo.sourceHeight);
            if (bitmap) nativeEdge = std::max(nativeEdge, std::max(bitmap->sourceWidth, bitmap->sourceHeight));
            displayDesired = std::max(baseDesired, nativeEdge);
            if (displayDesired <= baseDesired) {
                displayDesired = std::max(baseDesired,
                    static_cast<int>(baseDesired * resolvedView.relativeZoom * 1.10f));
            }

            const int intermediateDesired = IntermediateViewDecodeTarget(baseDesired, displayDesired);
            if (intermediateDesired > BucketFor(baseDesired) &&
                intermediateDesired < BucketFor(displayDesired)) {
                BitmapEntry* intermediate = GetOrRequestIntermediateViewBitmap(
                    photo.path, intermediateDesired, JobPriority::Interactive);
                if (intermediate && BitmapMaximumEdge(intermediate) > BitmapMaximumEdge(bitmap))
                    bitmap = intermediate;
            }

            // Zoomed Single View is Full-quality only. The WIC image source is
            // deliberately reserved for Fit/near-Fit rendering below.
            BitmapEntry* refined = GetOrRequestBitmap(
                photo.path, displayDesired, JobPriority::Interactive, true, CacheClass::Full);
            if (refined) bitmap = refined;
        }

        fallbackWidth = bitmap ? bitmap->sourceWidth : fallbackWidth;
        fallbackHeight = bitmap ? bitmap->sourceHeight : fallbackHeight;
        if (fallbackWidth <= 0 || fallbackHeight <= 0) {
            fallbackWidth = photo.sourceWidth;
            fallbackHeight = photo.sourceHeight;
        }
        resolvedView = ResolveViewForPath(reviewState_.SinglePath(), imageArea, state,
            fallbackWidth, fallbackHeight);
        bool drewImage = false;
        if (bitmap && bitmap->bitmap) {
            DrawBitmapFit(bitmap, imageArea, resolvedView, true);
            drewImage = true;
        }
        RenderVisibleTiles(photo, imageArea, resolvedView, bitmap);
        if (drewImage) DrawFaceOutlines(photo, imageArea, resolvedView);
        const bool fullBitmapReady = ViewBitmapSettled(bitmap, photo, displayDesired);
        const bool viewFailed = ViewDecodeTerminallyFailed(photo.path);
        if (!drewImage) {
            DrawCenteredText(Tr(viewFailed ? L"Couldn't load image" : L"Loading image…"),
                imageArea, largeText_.Get(), mutedBrush_.Get());
        }
        // View backstop: while the on-screen image is still a stand-in, a low-rate
        // timer repaints so a cancelled/dropped view decode is re-requested.
        if (!fullBitmapReady && !viewFailed) ArmThumbnailReconcile(400);
        // Round 21 (t29u) stall watchdog: when the image on screen has not settled
        // within 5 s (then every 15 s), log the UI and view-lane state once so a stalled
        // load can be diagnosed from the log. Rate-limited globally as well.
        if (singleViewStallWatch_.Observe(photo.path.wstring(), fullBitmapReady || viewFailed,
                std::chrono::steady_clock::now())) {
            LogSingleViewStall(photo, bitmap, displayDesired);
        }

    }


    void QuickSiftApplicationImpl::LogSingleViewStall(const PhotoItem& photo, const BitmapEntry* bitmap,
        int displayDesired) {
        static quicksift::core::LogRateLimiter limiter(4, std::chrono::seconds(60));
        std::uint64_t suppressed = 0;
        if (!limiter.Allow(&suppressed)) return;
        try {
            const auto now = std::chrono::steady_clock::now();
            std::wstring retry = L"none";
            if (const auto it = viewRetryState_.find(NormalizedPathKey(photo.path)); it != viewRetryState_.end()) {
                const auto waitMs = it->second.nextRetryAt == std::chrono::steady_clock::time_point::max() ? -1LL :
                    static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                        it->second.nextRetryAt - now).count());
                retry = L"attempts=" + std::to_wstring(it->second.failedAttempts) +
                    L",terminal=" + (it->second.terminal ? std::wstring(L"1") : std::wstring(L"0")) +
                    L",kind=" + std::to_wstring(static_cast<int>(it->second.lastFailure)) +
                    L",next_ms=" + std::to_wstring(waitMs);
            }
            const int nativeEdge = std::max(photo.sourceWidth, photo.sourceHeight);
            QS_LOG_EVENT(quicksift::diagnostics::Level::Warning, quicksift::diagnostics::category::kViewLane,
                L"single_view_load_stalled",
                { L"file", quicksift::diagnostics::LogFileName(photo.path) },
                { L"path_id", quicksift::diagnostics::LogPathId(photo.path) },
                { L"stalled_ms", std::to_wstring(singleViewStallWatch_.StalledFor(now).count()) },
                { L"report", std::to_wstring(singleViewStallWatch_.Reports()) },
                { L"has_bitmap", bitmap && bitmap->bitmap ? L"1" : L"0" },
                { L"bitmap_edge", std::to_wstring(BitmapMaximumEdge(bitmap)) },
                { L"bitmap_preview", bitmap && bitmap->previewOnly ? L"1" : L"0" },
                { L"bitmap_class", std::to_wstring(bitmap ? static_cast<int>(bitmap->cacheClass) : -1) },
                { L"desired_edge", std::to_wstring(displayDesired) },
                { L"native_edge", std::to_wstring(nativeEdge) },
                { L"decode_cap", std::to_wstring(MaximumViewDecodeEdge()) },
                { L"retry", retry },
                { L"nav_epoch", std::to_wstring(reviewState_.Epochs().navigation) },
                { L"worker", worker_.DescribeViewLoadState(photo.path) },
                { L"suppressed", std::to_wstring(suppressed) });
        } catch (...) {
            QS_LOG_AND_CONTINUE(L"Rendering", L"QuickSiftApplicationImpl::LogSingleViewStall");
        }
    }

    float QuickSiftApplicationImpl::AspectForPath(const std::wstring& path) const {
        const auto index = IndexForPath(path);
        if (index) {
            const PhotoItem& photo = VisiblePhoto(*index);
            if (photo.sourceWidth > 0 && photo.sourceHeight > 0) {
                const bool quarterTurn = ((BaseViewForPath(path).rotation / 90) & 1) != 0;
                return quarterTurn ? static_cast<float>(photo.sourceHeight) / photo.sourceWidth :
                    static_cast<float>(photo.sourceWidth) / photo.sourceHeight;
            }
        }
        return 1.5f;
    }


    std::vector<D2D1_RECT_F> QuickSiftApplicationImpl::BuildBestCompareLayout(const D2D1_RECT_F& bounds) const {
        const auto& comparePaths = reviewState_.ComparePaths();
        const int count = static_cast<int>(comparePaths.size());
        std::vector<std::vector<int>> arrangements;
        std::vector<int> current;
        std::function<void(int)> enumerate = [&](int remaining) {
            if (remaining == 0) {
                arrangements.push_back(current);
                return;
            }
            for (int inRow = 1; inRow <= remaining; ++inRow) {
                current.push_back(inRow);
                enumerate(remaining - inRow);
                current.pop_back();
            }
        };
        enumerate(count);

        const float gap = 6.0f;
        const float labelHeight = 27.0f;
        const float totalW = std::max(1.0f, bounds.right - bounds.left);
        const float totalH = std::max(1.0f, bounds.bottom - bounds.top);
        float bestScore = -1.0f;
        std::vector<D2D1_RECT_F> best;

        for (const auto& rows : arrangements) {
            const float rowH = (totalH - gap * (static_cast<float>(rows.size()) + 1.0f)) /
                static_cast<float>(rows.size());
            if (rowH < 80.0f) continue;

            std::vector<D2D1_RECT_F> cells;
            cells.reserve(static_cast<std::size_t>(count));
            float score = 0.0f;
            size_t imageIndex = 0;
            for (size_t row = 0; row < rows.size(); ++row) {
                const int columns = rows[row];
                const float cellW = (totalW - gap * (columns + 1.0f)) / columns;
                if (cellW < 90.0f) {
                    score = -1.0f;
                    break;
                }
                const float top = bounds.top + gap + row * (rowH + gap);
                for (int column = 0; column < columns; ++column, ++imageIndex) {
                    const float left = bounds.left + gap + column * (cellW + gap);
                    const D2D1_RECT_F cell = D2D1::RectF(left, top, left + cellW, top + rowH);
                    cells.push_back(cell);

                    const float imageW = std::max(1.0f, cellW - 4.0f);
                    const float imageH = std::max(1.0f, rowH - labelHeight - 4.0f);
                    const float aspect = std::max(0.10f, AspectForPath(comparePaths[imageIndex]));
                    const float fittedW = std::min(imageW, imageH * aspect);
                    const float fittedH = fittedW / aspect;
                    score += fittedW * fittedH;
                }
            }
            // Tiny penalty for additional rows prevents nearly identical scores from
            // producing unnecessarily fragmented layouts.
            score -= static_cast<float>(rows.size()) * 8.0f;
            if (score > bestScore && cells.size() == static_cast<size_t>(count)) {
                bestScore = score;
                best = std::move(cells);
            }
        }
        if (best.size() == static_cast<size_t>(count)) return best;
        // Scoring can reject every arrangement on a short window. A plain grid is
        // better than painting nothing and swallowing clicks.
        std::vector<D2D1_RECT_F> fallback;
        fallback.reserve(static_cast<size_t>(count));
        const int columns = count <= 2 ? count : (count <= 4 ? 2 : 3);
        const int rows = (count + columns - 1) / columns;
        const float cellW = (totalW - gap * (columns + 1.0f)) / columns;
        const float cellH = (totalH - gap * (rows + 1.0f)) / rows;
        for (int index = 0; index < count; ++index) {
            const int row = index / columns;
            const int column = index % columns;
            const float left = bounds.left + gap + column * (cellW + gap);
            const float top = bounds.top + gap + row * (cellH + gap);
            fallback.push_back(D2D1::RectF(left, top, left + cellW, top + cellH));
        }
        return fallback;
    }


    void QuickSiftApplicationImpl::RenderCompare(const D2D1_RECT_F& bounds) {
        if (reviewState_.ComparePaths().size() < 2) {
            DrawCenteredText(Tr(L"Select 2 to 6 photos to compare"), bounds,
                largeText_.Get(), mutedBrush_.Get());
            return;
        }

        const std::vector<D2D1_RECT_F> cells = BuildBestCompareLayout(bounds);
        if (cells.size() != reviewState_.ComparePaths().size()) {
            DrawCenteredText(Tr(L"Compare layout is unavailable for this window"), bounds,
                largeText_.Get(), mutedBrush_.Get());
            return;
        }
        for (size_t i = 0; i < reviewState_.ComparePaths().size(); ++i) {
            const D2D1_RECT_F cell = cells[i];
            const float cellW = cell.right - cell.left;
            const float cellH = cell.bottom - cell.top;
            quicksift::ui::framework::MediaCardPresentation compareCard{};
            compareCard.rect = cell;
            compareCard.radius = static_cast<float>(kCardRadiusDip);
            uiFramework_.PaintMediaCard(renderTarget_.Get(), compareCard,
                { panelBrush_.Get(), glassBorderBrush_.Get(), accentBrush_.Get(), dangerBrush_.Get() });
            const auto index = IndexForPath(reviewState_.ComparePaths()[i]);
            if (!index) continue;
            const PhotoItem& photo = VisiblePhoto(*index);
            protectedViewPaths_.insert(reviewState_.ComparePaths()[i]);
            const bool raw = IsRawExtension(photo.extension);
            const D2D1_RECT_F imageArea = D2D1::RectF(cell.left + 2, cell.top + 2, cell.right - 2, cell.bottom - 28);
            const int baseDesired = static_cast<int>(std::max(cellW, cellH) * CanvasPixelScale() * 1.30f);

            BitmapEntry* bitmap = nullptr;
            // Every image in Compare is part of the exact user-selected task.
            // Do not make inactive panes second-class work; the comparison is only
            // useful when all selected images become available at maximum priority.
            const JobPriority comparePriority = JobPriority::Interactive;
            if (raw) {
                bitmap = GetOrRequestBitmap(photo.path, baseDesired,
                    comparePriority, false);
                BitmapEntry* full = GetOrRequestBitmap(photo.path, baseDesired,
                    comparePriority, true);
                if (full) bitmap = full;
            } else {
                const int intermediateTarget = IntermediateViewDecodeTarget(768, baseDesired);
                if (intermediateTarget > 0 && intermediateTarget < baseDesired) {
                    BitmapEntry* intermediate = GetOrRequestIntermediateViewBitmap(
                        photo.path, intermediateTarget, JobPriority::Interactive);
                    if (intermediate) bitmap = intermediate;
                }
                BitmapEntry* full = GetOrRequestBitmap(photo.path, baseDesired,
                    JobPriority::Interactive, true, CacheClass::Full);
                if (full) bitmap = full;
            }

            const ViewState state = ViewForPath(reviewState_.ComparePaths()[i], imageArea);
            int fallbackWidth = bitmap ? bitmap->sourceWidth : 0;
            int fallbackHeight = bitmap ? bitmap->sourceHeight : 0;
            auto resolvedView = ResolveViewForPath(reviewState_.ComparePaths()[i], imageArea, state,
                fallbackWidth, fallbackHeight);
            int displayDesired = baseDesired;
            if (resolvedView.relativeZoom > 1.001f) {
                displayDesired = std::max(baseDesired,
                    static_cast<int>(baseDesired * resolvedView.relativeZoom * 1.10f));
                const JobPriority viewPriority = JobPriority::Interactive;
                const int intermediateDesired = IntermediateViewDecodeTarget(baseDesired, displayDesired);
                if (intermediateDesired > BucketFor(baseDesired) &&
                    intermediateDesired < BucketFor(displayDesired)) {
                    BitmapEntry* intermediate = GetOrRequestIntermediateViewBitmap(
                        photo.path, intermediateDesired, viewPriority);
                    if (intermediate && BitmapMaximumEdge(intermediate) > BitmapMaximumEdge(bitmap))
                        bitmap = intermediate;
                }
                if (raw) GetOrRequestBitmap(photo.path, displayDesired, viewPriority, false);
                const JobPriority refinementPriority = JobPriority::Interactive;
                BitmapEntry* refined = GetOrRequestBitmap(photo.path, displayDesired,
                    refinementPriority, true, CacheClass::Full);
                if (refined) bitmap = refined;
            }
            // 0.9.4.0: no UI-thread WIC image source; the view lane supplies pixels.
            fallbackWidth = bitmap ? bitmap->sourceWidth : fallbackWidth;
            fallbackHeight = bitmap ? bitmap->sourceHeight : fallbackHeight;
            if (fallbackWidth <= 0 || fallbackHeight <= 0) {
                fallbackWidth = photo.sourceWidth;
                fallbackHeight = photo.sourceHeight;
            }
            resolvedView = ResolveViewForPath(reviewState_.ComparePaths()[i], imageArea, state,
                fallbackWidth, fallbackHeight);
            bool drewImage = false;
            if (bitmap && bitmap->bitmap) {
                DrawBitmapFit(bitmap, imageArea, resolvedView, true);
                drewImage = true;
            }
            RenderVisibleTiles(photo, imageArea, resolvedView, bitmap);
            if (drewImage) DrawFaceOutlines(photo, imageArea, resolvedView);
            const bool compareFailed = ViewDecodeTerminallyFailed(photo.path);
            if (!drewImage) {
                DrawCenteredText(Tr(compareFailed ? L"Couldn't load image" : L"Loading…"),
                    imageArea, smallText_.Get(), mutedBrush_.Get());
            }
            if (!compareFailed && !ViewBitmapSettled(bitmap, photo, displayDesired))
                ArmThumbnailReconcile(400);

            const auto activeCompareSlot = reviewState_.ActiveCompareSlot();
            const bool active = activeCompareSlot && *activeCompareSlot == i;
            if (active) {
                compareCard.paintSurface = false;
                compareCard.selected = true;
                compareCard.focused = true;
                uiFramework_.PaintMediaCard(renderTarget_.Get(), compareCard,
                    { panelBrush_.Get(), glassBorderBrush_.Get(), accentBrush_.Get(), dangerBrush_.Get() });
            }
            std::wstring label = photo.name + L"   " + CullStateText(photo) + L"   " + RatingStateText(photo);
            if (bitmap && bitmap->previewOnly) label += L"   " + Tr(L"Preview");
            const D2D1_RECT_F labelRect = D2D1::RectF(cell.left + 6,
                cell.bottom - 27, cell.right - 6, cell.bottom - 3);
            // Concentric with the card (inset 6 DIP); text keeps 8 DIP padding.
            uiFramework_.PaintOverlayLabel(renderTarget_.Get(), labelRect,
                static_cast<float>(quicksift::ui::tokens::ConcentricRadius(kCardRadiusDip, 6)), label,
                smallText_.Get(), overlayBrush_.Get(), textBrush_.Get());
        }
    }


    std::wstring QuickSiftApplicationImpl::FormatBadgeText(const PhotoItem& photo) const {
        if (IsRawExtension(photo.extension)) return L"RAW";
        std::wstring extension = photo.extension;
        if (!extension.empty() && extension.front() == L'.') extension.erase(extension.begin());
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value) {
            return static_cast<wchar_t>(std::towupper(value));
        });
        if (extension == L"JPEG") extension = L"JPG";
        if (extension == L"TIF") extension = L"TIFF";
        return extension.empty() ? Tr(L"IMAGE") : extension;
    }


    void QuickSiftApplicationImpl::RefreshBadgeCaptions() {
        const int language = static_cast<int>(localizer_.CurrentLanguage());
        if (language == badgeCaptionLanguage_) return;
        badgeCaptionLanguage_ = language;
        badgePickCaption_ = Tr(L"PICK");
        badgeRejectCaption_ = Tr(L"REJECT");
        badgeFormatCaptionCache_.clear();
    }

    void QuickSiftApplicationImpl::DrawThumbnailBadges(const PhotoItem& photo, D2D1_RECT_F area,
        const ThumbnailBadgeStyle& style) {
        // Round 23: allocation-free per frame (fixed badge array, string views into
        // static or cached captions, heterogeneous width-cache lookup); shared by
        // the grid and the filmstrip. Pending metadata (not Known) shows no badge,
        // and painting never requests metadata.
        struct ThumbnailBadge {
            std::wstring_view text;
            int color = 0;
            bool matchCardSurface = false;
        };
        static constexpr std::array<std::wstring_view, 6> kRatingCaptions{{
            L"", L"★1", L"★2", L"★3", L"★4", L"★5" }};
        static constexpr std::wstring_view kColorCaption = L"●";
        RefreshBadgeCaptions();
        std::array<ThumbnailBadge, 4> badges{};
        std::size_t count = 0;
        if (badgeStars_ && photo.ratingKnowledge == MetadataKnowledge::Known &&
            photo.rating > 0 && photo.rating <= 5)
            badges[count++] = { kRatingCaptions[static_cast<std::size_t>(photo.rating)], 0, false };
        if (style.includeColorLabel && badgeColor_ &&
            photo.colorLabelKnowledge == MetadataKnowledge::Known && photo.colorLabel > 0)
            badges[count++] = { kColorCaption, photo.colorLabel, false };
        if (badgePick_ && photo.pickStateKnowledge == MetadataKnowledge::Known &&
            photo.pickState != 0) {
            badges[count++] = { photo.pickState > 0 ? std::wstring_view(badgePickCaption_)
                : std::wstring_view(badgeRejectCaption_), photo.pickState > 0 ? 3 : 1, false };
        }
        // The format badge sits on the card itself. Leaving its interior unpainted
        // avoids compositing a second translucent surface over the card and creating
        // the subtle off-color patch visible in both light and dark themes.
        if (style.includeFormat && badgePair_) {
            auto format = badgeFormatCaptionCache_.find(std::wstring_view(photo.extension));
            if (format == badgeFormatCaptionCache_.end()) {
                if (badgeFormatCaptionCache_.size() > 64) badgeFormatCaptionCache_.clear();
                format = badgeFormatCaptionCache_.emplace(photo.extension, FormatBadgeText(photo)).first;
            }
            badges[count++] = { format->second, 0, true };
        }
        if (count == 0) return;
        const float opacity = std::clamp(style.opacity, 0.0f, 1.0f);
        if (opacity <= 0.004f) return;
        // Fade: scale the shared brushes' opacity for these draws and restore it
        // (the framework's PaintFaceOutline does the same); no per-frame brushes.
        const auto withOpacity = [opacity](ID2D1Brush* brush, auto&& draw) {
            if (!brush || opacity >= 0.999f) { draw(); return; }
            const float previous = brush->GetOpacity();
            brush->SetOpacity(previous * opacity);
            draw();
            brush->SetOpacity(previous);
        };
        float x = area.left;
        const float badgeHeight = area.bottom - area.top;
        for (std::size_t index = 0; index < count; ++index) {
            const ThumbnailBadge& item = badges[index];
            // Measured once per text and format (was chars x 7 DIP, which made
            // "REJECT"/VI captions overflow and "●" far too wide), padded 6 DIP
            // per side, never narrower than a circle.
            float textWidth = 0.0f;
            if (auto cached = badgeTextWidthCache_.find(item.text); cached != badgeTextWidthCache_.end()) {
                textWidth = cached->second;
            } else {
                textWidth = quicksift::ui::framework::MacOsUiFramework::MeasureSingleLineText(
                    dwriteFactory_.Get(), smallText_.Get(), item.text);
                if (textWidth <= 0.0f) textWidth = static_cast<float>(item.text.size()) * 7.0f;
                if (badgeTextWidthCache_.size() > 256) badgeTextWidthCache_.clear();
                badgeTextWidthCache_.emplace(std::wstring(item.text), textWidth);
            }
            const float width = std::max(badgeHeight, std::ceil(textWidth) + 12.0f);
            if (x + width > area.right) break;
            const D2D1_RECT_F badge = D2D1::RectF(x, area.top, x + width, area.bottom);
            ID2D1Brush* brush = item.color == 0 ? textBrush_.Get() : accentBrush_.Get();
            if (item.color > 0 && item.color <= 5) {
                static const D2D1_COLOR_F colors[] = {
                    D2D1::ColorF(1,1,1), D2D1::ColorF(0.96f,0.28f,0.34f), D2D1::ColorF(1.0f,0.78f,0.20f),
                    D2D1::ColorF(0.27f,0.82f,0.45f), D2D1::ColorF(0.25f,0.67f,1.0f), D2D1::ColorF(0.72f,0.45f,0.95f)
                };
                // Device-dependent brushes are created once per render target (they
                // were created per badge per card per frame) and released with it.
                ComPtr<ID2D1SolidColorBrush>& cached = badgeColorBrushes_[static_cast<std::size_t>(item.color)];
                if (!cached) renderTarget_->CreateSolidColorBrush(colors[item.color], &cached);
                if (cached) brush = cached.Get();
            }
            ID2D1Brush* fill = item.matchCardSurface ? nullptr : overlayBrush_.Get();
            ID2D1Brush* border = item.matchCardSurface ? glassBorderBrush_.Get() : nullptr;
            // Capsule badge with centred text.
            withOpacity(fill, [&] { withOpacity(brush, [&] { withOpacity(border, [&] {
                uiFramework_.PaintOverlayLabel(renderTarget_.Get(), badge,
                    quicksift::ui::tokens::CapsuleRadius(badgeHeight),
                    item.text, smallText_.Get(), fill, brush, border, 1.0f, /*centered=*/true);
            }); }); });
            x += width + 4.0f;
        }
    }


    void QuickSiftApplicationImpl::DrawBitmapFit(BitmapEntry* bitmap, D2D1_RECT_F area,
        const quicksift::review::transform::ResolvedView& view, bool clip, float opacity) {
        if (!bitmap || !bitmap->bitmap || bitmap->width <= 0 || bitmap->height <= 0 ||
            !view.valid) return;
        const float geometryWidth = static_cast<float>(
            bitmap->sourceWidth > 0 ? bitmap->sourceWidth : bitmap->width);
        const float geometryHeight = static_cast<float>(
            bitmap->sourceHeight > 0 ? bitmap->sourceHeight : bitmap->height);
        const float drawWidth = geometryWidth * view.scaleDip;
        const float drawHeight = geometryHeight * view.scaleDip;
        const float areaCenterX = (area.left + area.right) * 0.5f;
        const float areaCenterY = (area.top + area.bottom) * 0.5f;
        const float left = areaCenterX - view.state.centerX * drawWidth;
        const float top = areaCenterY - view.state.centerY * drawHeight;
        const D2D1_RECT_F destination = D2D1::RectF(
            left, top, left + drawWidth, top + drawHeight);
        // Panned fully out of its pane (zoomed Single/Compare): nothing to draw, so
        // skip the clip push and the bitmap draw. Rotated views keep the full path.
        if (clip && view.state.rotation == 0 && (destination.right <= area.left ||
            destination.left >= area.right || destination.bottom <= area.top ||
            destination.top >= area.bottom)) return;
        if (clip) renderTarget_->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        D2D1_MATRIX_3X2_F oldTransform{};
        renderTarget_->GetTransform(&oldTransform);
        if (view.state.rotation != 0) {
            renderTarget_->SetTransform(D2D1::Matrix3x2F::Rotation(
                static_cast<float>(view.state.rotation),
                D2D1::Point2F(areaCenterX, areaCenterY)) * oldTransform);
        }
        renderTarget_->DrawBitmap(bitmap->bitmap.Get(), destination, std::clamp(opacity, 0.0f, 1.0f),
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        if (view.state.rotation != 0) renderTarget_->SetTransform(oldTransform);
        if (clip) renderTarget_->PopAxisAlignedClip();
    }


    void QuickSiftApplicationImpl::DrawFaceOutlines(const PhotoItem& photo, D2D1_RECT_F area,
        const quicksift::review::transform::ResolvedView& view) {
        if (!reviewState_.FaceLockEnabled() || !photo.facesScanned || photo.faces.empty() ||
            !view.valid || !renderTarget_ || !faceOutlineBrush_ || !faceOutlineShadowBrush_) return;
        // Reveal/fade: always observed (so a re-show is detected), painted only
        // while visible. Hit boxes stay clickable regardless (HitFaceOutline).
        const float opacity = FaceOutlineOpacityForPath(photo.path.wstring());
        if (opacity <= 0.004f) return;
        const auto target = quicksift::core::ResolveTargetFaceIndex(photo.faces, photo.faceSelection);
        const bool qualityKnown = photo.faceBlurScanned &&
            photo.faceQualities.size() == photo.faces.size();

        // Same placement as DrawBitmapFit (image space: the outline is drawn under the
        // identical rotation transform so it follows zoom, pan and rotation exactly).
        // Click hit testing recomputes the same boxes from current state
        // (HitFaceOutline + transform::SourceToScreen); nothing is retained from paint.
        const float areaCenterX = (area.left + area.right) * 0.5f;
        const float areaCenterY = (area.top + area.bottom) * 0.5f;
        const float drawWidth = view.drawWidthDip;
        const float drawHeight = view.drawHeightDip;
        const float originX = areaCenterX - view.state.centerX * drawWidth;
        const float originY = areaCenterY - view.state.centerY * drawHeight;
        const D2D1::Matrix3x2F rotation = D2D1::Matrix3x2F::Rotation(
            static_cast<float>(view.state.rotation), D2D1::Point2F(areaCenterX, areaCenterY));

        renderTarget_->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        D2D1_MATRIX_3X2_F oldTransform{};
        renderTarget_->GetTransform(&oldTransform);
        if (view.state.rotation != 0) renderTarget_->SetTransform(rotation * oldTransform);
        for (std::size_t index = 0; index < photo.faces.size(); ++index) {
            const auto& face = photo.faces[index];
            const D2D1_RECT_F rect = D2D1::RectF(originX + face.x * drawWidth,
                originY + face.y * drawHeight, originX + (face.x + face.width) * drawWidth,
                originY + (face.y + face.height) * drawHeight);
            if (!(rect.right - rect.left >= 3.0f && rect.bottom - rect.top >= 3.0f)) continue;
            quicksift::ui::framework::FaceOutlinePresentation outline{};
            outline.rect = rect;
            outline.opacity = opacity;
            outline.target = target && *target == index;
            outline.flagged = qualityKnown &&
                quicksift::core::FaceQualityFlagged(photo.faceQualities[index]);
            uiFramework_.PaintFaceOutline(renderTarget_.Get(), outline,
                faceOutlineBrush_.Get(), faceOutlineShadowBrush_.Get());
        }
        if (view.state.rotation != 0) renderTarget_->SetTransform(oldTransform);
        renderTarget_->PopAxisAlignedClip();
    }


    float QuickSiftApplicationImpl::FaceOutlineOpacityForPath(const std::wstring& path) {
        // Hold/fade timing is the review policy (FaceOutlineFade, tested); the curve
        // and the reduced-motion rule come from the central animation system.
        static_assert(quicksift::review::face_interaction::kOutlineFade.count() ==
            quicksift::ui::tokens::kMotionSlowMs, "outline fade uses the slow motion token");
        float progress = faceOutlineFade_.Observe(path, std::chrono::steady_clock::now());
        if (!uiFramework_.Motion().Enabled() && progress > 0.0f) progress = 1.0f; // instant hide
        return 1.0f - quicksift::ui::animation::Ease(quicksift::ui::animation::Curve::Smoothstep, progress);
    }


    // ---- Placeholder -> thumbnail crossfade (central Animator, Crossfade preset) ----
    // Only a cell that was actually painted as a placeholder fades its image in;
    // cached thumbnails scrolled into view appear at once. Never during scroll motion
    // or fling (the cheap placeholder path), never beyond a small concurrent budget,
    // and entries are transient (forgotten as soon as they land).
    namespace {
    constexpr std::size_t kThumbnailRevealBudget = 48;
    constexpr std::size_t kThumbnailPlaceholderMemory = 4096;
    quicksift::ui::animation::ElementId ThumbnailRevealId(const std::wstring& path) {
        return quicksift::ui::animation::MakeId(quicksift::ui::animation::Space::Thumbnail,
            static_cast<std::uint64_t>(std::hash<std::wstring>{}(path)));
    }
    } // namespace

    void QuickSiftApplicationImpl::NoteThumbnailPlaceholder(const std::wstring& path) {
        if (thumbnailPlaceholderShown_.size() >= kThumbnailPlaceholderMemory) thumbnailPlaceholderShown_.clear();
        thumbnailPlaceholderShown_.insert(path);
    }

    float QuickSiftApplicationImpl::ThumbnailRevealOpacity(const std::wstring& path) {
        namespace anim = quicksift::ui::animation;
        anim::Animator& motion = uiFramework_.Motion();
        const anim::ElementId id = ThumbnailRevealId(path);
        if (!thumbnailPlaceholderShown_.empty()) {
            const auto shown = thumbnailPlaceholderShown_.find(path);
            if (shown != thumbnailPlaceholderShown_.end()) {
                thumbnailPlaceholderShown_.erase(shown);
                if (!thumbnailScrollMotionActive_ && !ThumbnailScrollPrefersPlaceholders() &&
                    motion.ActiveCount() < kThumbnailRevealBudget) {
                    // Starts at 0 this frame; one canvas invalidate per animator tick.
                    motion.Show(id, MacOsUiFramework::kThumbnailTransition, anim::surface::kCanvas,
                        std::chrono::steady_clock::now(), {}, 0, /*transient=*/true);
                }
            }
        }
        return motion.Appear(id, MacOsUiFramework::kThumbnailTransition, 1.0f).opacity;
    }

    void QuickSiftApplicationImpl::RenderVisibleTiles(const PhotoItem& photo,
        const D2D1_RECT_F& area, const quicksift::review::transform::ResolvedView& view,
        const BitmapEntry* baseBitmap) {
        if (IsRawExtension(photo.extension) && RawPreviewOnlyFor(photo.path.native())) return;
        if (!systemProfile_.enablePersistentPyramid || IsActivelyInteracting() ||
            view.state.rotation != 0 || view.relativeZoom < 1.35f ||
            photo.sourceWidth <= 0 || photo.sourceHeight <= 0 ||
            std::max(photo.sourceWidth, photo.sourceHeight) < 4096 || !view.valid) return;
        const float scale = view.scaleDip;
        const float physicalScale = view.physicalScale;

        const int maximumLevel = systemProfile_.lowMemory ? 3 :
            (systemProfile_.totalPhysicalBytes <= 18ull * kGibibyte ? 4 : 5);
        int level = 0;
        while (level < maximumLevel &&
            physicalScale * static_cast<float>(1 << (level + 1)) <= 0.92f) {
            ++level;
        }
        const int divisor = 1 << level;
        const int sourceTileSpan = kTileSourceSize * divisor;
        if (baseBitmap && baseBitmap->sourceWidth > 0 && baseBitmap->sourceHeight > 0 &&
            (baseBitmap->sourceWidth != photo.sourceWidth ||
                baseBitmap->sourceHeight != photo.sourceHeight)) return;
        const float tileResolution = 1.0f / static_cast<float>(divisor);
        const float requiredResolution = std::min(1.0f, physicalScale);
        if (tileResolution + 0.0001f < requiredResolution * 0.98f) return;
        if (baseBitmap && baseBitmap->width > 0 && baseBitmap->height > 0) {
            const float baseResolutionX = static_cast<float>(baseBitmap->width) /
                std::max(1, photo.sourceWidth);
            const float baseResolutionY = static_cast<float>(baseBitmap->height) /
                std::max(1, photo.sourceHeight);
            const float baseResolution = std::min(baseResolutionX, baseResolutionY);
            if (baseResolution >= requiredResolution * 0.98f ||
                tileResolution <= baseResolution * 1.08f) return;
        }

        const float drawWidth = photo.sourceWidth * scale;
        const float drawHeight = photo.sourceHeight * scale;
        const float left = (area.left + area.right) * 0.5f -
            view.state.centerX * drawWidth;
        const float top = (area.top + area.bottom) * 0.5f -
            view.state.centerY * drawHeight;
        const int sourceLeft = std::clamp(static_cast<int>(
            std::floor((area.left - left) / scale)), 0, photo.sourceWidth - 1);
        const int sourceTop = std::clamp(static_cast<int>(
            std::floor((area.top - top) / scale)), 0, photo.sourceHeight - 1);
        const int sourceRight = std::clamp(static_cast<int>(
            std::ceil((area.right - left) / scale)), 1, photo.sourceWidth);
        const int sourceBottom = std::clamp(static_cast<int>(
            std::ceil((area.bottom - top) / scale)), 1, photo.sourceHeight);
        const int tileLeft = std::max(0, sourceLeft / sourceTileSpan - 1);
        const int tileTop = std::max(0, sourceTop / sourceTileSpan - 1);
        const int tileRight = std::min((photo.sourceWidth - 1) / sourceTileSpan,
            sourceRight / sourceTileSpan + 1);
        const int tileBottom = std::min((photo.sourceHeight - 1) / sourceTileSpan,
            sourceBottom / sourceTileSpan + 1);
        const int scheduleLimit = systemProfile_.lowMemory ? 16 : 36;
        int scheduled = 0;
        // Hoisted out of the tile loop: one path copy and one clock read per frame.
        const std::wstring photoPath = photo.path.wstring();
        const auto frameTime = std::chrono::steady_clock::now();
        renderTarget_->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        // The schedule limit bounds NEW tile decodes per frame. It used to count
        // every visited tile (cached ones included), so at deep zoom with many
        // visible tiles the later, already-decoded tiles were never drawn.
        for (int tileY = tileTop; tileY <= tileBottom; ++tileY) {
            for (int tileX = tileLeft; tileX <= tileRight; ++tileX) {
                const int sourceX = tileX * sourceTileSpan;
                const int sourceY = tileY * sourceTileSpan;
                const int sourceWidth = std::min(sourceTileSpan, photo.sourceWidth - sourceX);
                const int sourceHeight = std::min(sourceTileSpan, photo.sourceHeight - sourceY);
                TileKey key{ photoPath, sourceX, sourceY,
                    sourceWidth, sourceHeight, level };
                const auto iterator = tileCache_.find(key);
                if (iterator == tileCache_.end()) {
                    if (scheduled >= scheduleLimit) continue;
                    ++scheduled;
                    worker_.EnqueueTile(photo.path, sourceX, sourceY, sourceWidth,
                        sourceHeight, level, generation_, reviewState_.Epochs().navigation);
                    continue;
                }
                if (iterator->second.sourceWidth != photo.sourceWidth ||
                    iterator->second.sourceHeight != photo.sourceHeight) {
                    tileCacheBytes_ = iterator->second.bytes > tileCacheBytes_ ?
                        0 : tileCacheBytes_ - iterator->second.bytes;
                    tileCache_.erase(iterator);
                    continue;
                }
                iterator->second.lastUse = ++useCounter_;
                iterator->second.lastUseTime = frameTime;
                const D2D1_RECT_F destination = D2D1::RectF(
                    left + sourceX * scale, top + sourceY * scale,
                    left + (sourceX + sourceWidth) * scale,
                    top + (sourceY + sourceHeight) * scale);
                renderTarget_->DrawBitmap(iterator->second.bitmap.Get(), destination,
                    1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        }
        renderTarget_->PopAxisAlignedClip();
    }


    void QuickSiftApplicationImpl::RenderFilmstrip(const D2D1_RECT_F& bounds) {
        if (reviewState_.Mode() != ViewMode::Single || filmstripOpacity_ <= 0.001f || !renderTarget_) return;

        const FilmstripLayout layout = CurrentFilmstripLayout(bounds);
        if (layout.maxThumbs <= 0 || layout.lastIndex <= layout.firstIndex) return;
        const float kFilmstripWidth = layout.width;
        const float kThumbSize = layout.thumbSize;
        const float kPadding = layout.padding;
        const float topOffset = bounds.top + layout.top;
        const auto currentIndexOpt = IndexForPath(reviewState_.SinglePath());
        if (!currentIndexOpt) return;
        const size_t currentIndex = *currentIndexOpt;
        // Round 22: the grid's bucket, matching PrefetchFilmstrip (shared cache).
        const int cacheTargetSize = quicksift::core::filmstrip::ThumbnailTarget(ThumbnailDecodeTarget());

        const D2D1_RECT_F bgRect = D2D1::RectF(
            bounds.right - kFilmstripWidth, bounds.top,
            bounds.right, bounds.bottom);
        const ThemePalette palette = Palette();
        // Round 22: cached brushes, theme colours (accent / border instead of fixed
        // blue and white, which vanished on the light theme's panel).
        const D2D1_COLOR_F backgroundColor = ToD2D(palette.panelStrong, 0.90f * filmstripOpacity_);
        const D2D1_COLOR_F activeColor = ToD2D(palette.accent, filmstripOpacity_);
        const D2D1_COLOR_F inactiveColor = ToD2D(palette.border, 0.90f * filmstripOpacity_);
        auto ensure = [this](ComPtr<ID2D1SolidColorBrush>& brush, const D2D1_COLOR_F& color) {
            if (brush) brush->SetColor(color);
            else renderTarget_->CreateSolidColorBrush(color, &brush);
        };
        ensure(filmstripBackgroundBrush_, backgroundColor);
        ensure(filmstripActiveBrush_, activeColor);
        ensure(filmstripInactiveBrush_, inactiveColor);
        if (filmstripBackgroundBrush_) renderTarget_->FillRectangle(bgRect, filmstripBackgroundBrush_.Get());

        float currentY = topOffset + kPadding;
        for (size_t i = layout.firstIndex; i < layout.lastIndex; ++i) {
            const size_t catalogIndex = catalog_.VisibleCatalogIndex(i);
            const PhotoItem& photo = catalog_.PhotoAt(catalogIndex);
            const D2D1_RECT_F thumbRect = D2D1::RectF(
                bounds.right - kFilmstripWidth + (kFilmstripWidth - kThumbSize) * 0.5f,
                currentY,
                bounds.right - kFilmstripWidth + (kFilmstripWidth + kThumbSize) * 0.5f,
                currentY + kThumbSize);

            // Rendering is cache-only. Filmstrip scheduling is owned by
            // PrefetchFilmstrip(), so a repaint can never enqueue thumbnail work.
            // DrawBitmapFit ignores an invalid view, so a default ResolvedView
            // left every cell blank even when a thumbnail was already cached.
            BitmapEntry* bmp = FindBitmapNoTouch(photo.path, cacheTargetSize, CacheClass::Thumbnail);
            if (bmp && bmp->bitmap) {
                const ViewState fitState{};
                const auto fitView = ResolveViewForPath(photo.path.native(), thumbRect, fitState,
                    bmp->sourceWidth, bmp->sourceHeight);
                // Round 22: thumbnails fade with the strip (were drawn opaque).
                if (fitView.valid) DrawBitmapFit(bmp, thumbRect, fitView, true, filmstripOpacity_);
            } else if (!failedThumbnailPaths_.empty() &&
                failedThumbnailPaths_.contains(NormalizedPathKey(photo.path))) {
                // 0.9.4.0: cache-only filmstrip; synchronous Shell extraction only for
                // definitively failed decodes, budgeted to one new bitmap per frame.
                if (ComPtr<ID2D1Bitmap> shell = GetOrCreateShellFallback(photo.path, cacheTargetSize)) {
                    renderTarget_->DrawBitmap(shell.Get(), thumbRect, filmstripOpacity_,
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                }
            }

            // Round 23: the grid's blurry-photo cue (red dashed outline, inside the
            // cell) and the grid's rating / pick badges, both fading with the strip.
            const bool current = i == currentIndex;
            const bool blurry = blurOutlineBrush_ && HasFaceFocusWarning(photo);
            if (blurry) {
                namespace blur = quicksift::core::blur_outline;
                const float half = blur::kFilmstripStrokeDip * 0.5f;
                const float previous = blurOutlineBrush_->GetOpacity();
                blurOutlineBrush_->SetOpacity(previous * filmstripOpacity_);
                renderTarget_->DrawRectangle(D2D1::RectF(thumbRect.left + half, thumbRect.top + half,
                    thumbRect.right - half, thumbRect.bottom - half), blurOutlineBrush_.Get(),
                    blur::kFilmstripStrokeDip, blurOutlineStrokeStyle_.Get());
                blurOutlineBrush_->SetOpacity(previous);
                // The current-image accent stays visible just outside the dashes.
                if (current && filmstripActiveBrush_)
                    renderTarget_->DrawRectangle(D2D1::RectF(thumbRect.left - 2.0f, thumbRect.top - 2.0f,
                        thumbRect.right + 2.0f, thumbRect.bottom + 2.0f), filmstripActiveBrush_.Get(), 2.0f);
            } else {
                ID2D1Brush* border = current ? filmstripActiveBrush_.Get() : filmstripInactiveBrush_.Get();
                if (border) renderTarget_->DrawRectangle(thumbRect, border, current ? 2.0f : 1.0f);
            }
            {
                namespace film = quicksift::core::filmstrip;
                const float badgeHeight = film::BadgeHeightDip(kThumbSize);
                const D2D1_RECT_F badgeArea = D2D1::RectF(thumbRect.left + film::kBadgeInsetDip,
                    thumbRect.bottom - film::kBadgeInsetDip - badgeHeight,
                    thumbRect.right - film::kBadgeInsetDip, thumbRect.bottom - film::kBadgeInsetDip);
                ThumbnailBadgeStyle badgeStyle{};
                badgeStyle.includeColorLabel = false;
                badgeStyle.includeFormat = false;
                badgeStyle.opacity = filmstripOpacity_;
                DrawThumbnailBadges(photo, badgeArea, badgeStyle);
            }
            currentY += kThumbSize + kPadding;
        }
    }

    std::optional<size_t> QuickSiftApplicationImpl::IndexForPath(const std::wstring& path) const {
        const auto catalog = CatalogIndexForPath(path);
        if (!catalog) return std::nullopt;
        const size_t catalogIndex = *catalog;
        if (catalogIndex >= catalog_.PhotoCount()) return std::nullopt;
        const size_t visibleIndex = catalog_.VisiblePosition(catalogIndex);
        if (visibleIndex == kInvalidVisiblePosition || visibleIndex >= catalog_.VisibleCount() ||
            catalog_.VisibleCatalogIndex(visibleIndex) != catalogIndex) return std::nullopt;
        return visibleIndex;
    }


    std::optional<size_t> QuickSiftApplicationImpl::HitThumbnail(float x, float y) const {
        return quicksift::review::thumbnail::HitTestThumbnail(CurrentThumbnailLayout(),
            catalog_.VisibleCount(), reviewState_.Thumbnails().scrollDip, x, y);
    }


    std::optional<size_t> QuickSiftApplicationImpl::HitCompareSlot(float x, float y) const {
        if (reviewState_.Mode() != ViewMode::Compare) return std::nullopt;
        const D2D1_SIZE_F canvas = CanvasSizeInDips();
        const auto cells = BuildBestCompareLayout(D2D1::RectF(0.0f, 0.0f, canvas.width, canvas.height));
        for (size_t slot = 0; slot < cells.size() && slot < reviewState_.ComparePaths().size(); ++slot) {
            const D2D1_RECT_F& rect = cells[slot];
            if (x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom) return slot;
        }
        return std::nullopt;
    }



} // namespace quicksift::app
