// OWNER: Compiled QuickSiftApplication feature module.
#include "app/quicksift_application_internal.h"

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
            RenderThumbnails(bounds);
        } else if (reviewState_.Mode() == ViewMode::Single) {
            RenderSingle(bounds);
        } else {
            RenderCompare(bounds);
        }
        RenderFilmstrip(bounds);
        DrawSearchFieldOverlay(); // under toasts and the glass alert veil
        DrawToastOverlay();
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
        default:
            break;
        }
        if (action == PendingGlassAction::DeleteSelection) {
            FileOperation(false, true);
            return;
        }
        if (action == PendingGlassAction::CloseWithPendingOperations) {
            closingAfterTransactionCancellation_ = true;
            // "Close anyway" cancels pending work, including coalesced edits that
            // were never submitted (their optimistic values die with the window).
            pendingMetadataEdits_.Clear();
            transactions_.RequestCloseAtSafeBoundary();
            if (transactions_.ConsumeCloseRequestIfIdle()) {
                SaveSession();
                DestroyWindow(hwnd_);
            } else {
                ShowToast(L"Cancelling file operations at a safe boundary before closing…");
            }
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
        const auto telemetry = worker_.Telemetry();
        MEMORYSTATUSEX memory{ sizeof(memory) };
        GlobalMemoryStatusEx(&memory);
        const auto toMiB = [](std::uint64_t bytes) -> std::uint64_t { return bytes / kMebibyte; };
        const D2D1_SIZE_F size = renderTarget_->GetSize();
        const float width = 285.0f;
        const float height = 176.0f;
        const float right = size.width - 12.0f;
        const float top = 12.0f;
        const D2D1_RECT_F panel = D2D1::RectF(right - width, top, right, top + height);
        const FLOAT previousOpacity = overlayBrush_->GetOpacity();
        overlayBrush_->SetOpacity(0.78f);
        uiFramework_.PaintOverlayLabel(renderTarget_.Get(), panel,
            static_cast<float>(kOverlayPanelRadiusDip), L"", smallText_.Get(),
            overlayBrush_.Get(), textBrush_.Get());
        overlayBrush_->SetOpacity(previousOpacity);
        std::wstring hud;
        hud += L"PERFORMANCE\n";
        hud += L"Visual    " + std::to_wstring(telemetry.activeVisualJobs) + L" active / " +
            std::to_wstring(systemProfile_.logicalProcessors) + L" CPU\n";
        hud += L"Queues    I:" + std::to_wstring(telemetry.interactiveQueued) +
            L" V:" + std::to_wstring(telemetry.visibleQueued) +
            L" P:" + std::to_wstring(telemetry.predictiveQueued) + L"\n";
        hud += L"Decode    " + std::to_wstring(static_cast<long long>(telemetry.decodeMilliseconds)) + L" ms\n";
        hud += L"Queue     " + std::to_wstring(static_cast<long long>(telemetry.queueWaitMilliseconds)) + L" ms\n";
        hud += L"Cache     " + std::to_wstring(toMiB(static_cast<std::uint64_t>(TotalCacheBytes()))) + L" MiB\n";
        hud += L"Disk      hit " + std::to_wstring(telemetry.persistentCacheHits) +
            L" / miss " + std::to_wstring(telemetry.persistentCacheMisses) + L"\n";
        hud += L"RAM free  " + std::to_wstring(toMiB(memory.ullAvailPhys)) + L" MiB\n";
        hud += L"Focus     " + std::wstring(reviewState_.Mode() == ViewMode::Thumbnails ? L"THUMBNAILS" :
            reviewState_.Mode() == ViewMode::Single ? L"SINGLE VIEW" : L"COMPARE");
        uiFramework_.PaintText(renderTarget_.Get(), hud, smallText_.Get(),
            D2D1::RectF(panel.left + 12.0f, panel.top + 9.0f, panel.right - 12.0f, panel.bottom - 8.0f),
            textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
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
        const int rows = layout.rows;
        const float unclampedScroll = reviewState_.Thumbnails().scrollDip;
        reviewState_.SetThumbnailMaximumScroll(layout.maximumScroll);
        const float thumbnailScroll = reviewState_.Thumbnails().scrollDip;
        if (thumbnailScroll != unclampedScroll) thumbnailPrefetchPlanner_.Reset(thumbnailScroll);

        // Fast scroll (scrollbar fling or wheel fling) gets a deliberately cheap
        // render path. The normal card renderer performs per-card string/hash work,
        // cache LRU touches, text/badge drawing, and fallback bitmap creation. None
        // of that belongs on the hot path of a high-frequency scroll gesture.
        if (ThumbnailScrollPrefersPlaceholders()) {
            const int firstRow = std::max(0, static_cast<int>((thumbnailScroll - padding) / cellH));
            const int lastRow = std::min(rows - 1,
                static_cast<int>((thumbnailScroll + bounds.bottom) / cellH));
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

        const int firstRow = std::max(0, static_cast<int>((thumbnailScroll - padding) / cellH) - 1);
        const int lastRow = std::min(rows - 1, static_cast<int>((thumbnailScroll + bounds.bottom) / cellH) + 1);
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
                uiFramework_.PaintMediaCard(renderTarget_.Get(), cardPresentation,
                    { panelBrush_.Get(), glassBorderBrush_.Get(), accentBrush_.Get(), dangerBrush_.Get() });

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
        const int readyEdge = nativeEdge > 0 ? std::min(displayDesired, nativeEdge) : displayDesired;
        const bool previewIsFinal = loadOnlyRawJpegPreviews_ && IsRawExtension(photo.extension);
        return BitmapSatisfiesDisplay(bitmap, std::max(1, readyEdge), !previewIsFinal);
    }

    void QuickSiftApplicationImpl::RenderSingle(const D2D1_RECT_F& bounds) {
        const auto index = IndexForPath(reviewState_.SinglePath());
        if (!index) {
            EnterSingleFromSelection();
            return;
        }
        const PhotoItem& photo = VisiblePhoto(*index);
        protectedViewPaths_.insert(reviewState_.SinglePath());
        const D2D1_RECT_F imageArea = D2D1::RectF(8, 8, bounds.right - 8, bounds.bottom - 8);
        const int baseDesired = static_cast<int>(std::max(imageArea.right - imageArea.left,
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
            cells.reserve(count);
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


    void QuickSiftApplicationImpl::DrawThumbnailBadges(const PhotoItem& photo, D2D1_RECT_F area) {
        struct ThumbnailBadge {
            std::wstring text;
            int color = 0;
            bool matchCardSurface = false;
        };
        std::vector<ThumbnailBadge> badges;
        if (badgeStars_ && photo.ratingKnowledge == MetadataKnowledge::Known && photo.rating > 0)
            badges.push_back({ L"★" + std::to_wstring(photo.rating), 0, false });
        if (badgeColor_ && photo.colorLabelKnowledge == MetadataKnowledge::Known && photo.colorLabel > 0)
            badges.push_back({ L"●", photo.colorLabel, false });
        if (badgePick_ && photo.pickStateKnowledge == MetadataKnowledge::Known &&
            photo.pickState != 0) {
            badges.push_back({ Tr(photo.pickState > 0 ? L"PICK" : L"REJECT"),
                photo.pickState > 0 ? 3 : 1, false });
        }
        // The format badge sits on the card itself. Leaving its interior unpainted
        // avoids compositing a second translucent surface over the card and creating
        // the subtle off-color patch visible in both light and dark themes.
        if (badgePair_) badges.push_back({ FormatBadgeText(photo), 0, true });
        float x = area.left;
        const float badgeHeight = area.bottom - area.top;
        for (const ThumbnailBadge& item : badges) {
            // Measured once per text and format (was chars x 7 DIP, which made
            // "REJECT"/VI captions overflow and "●" far too wide), padded 6 DIP per
            // side, never narrower than a circle.
            float textWidth = 0.0f;
            if (auto cached = badgeTextWidthCache_.find(item.text); cached != badgeTextWidthCache_.end()) {
                textWidth = cached->second;
            } else {
                textWidth = quicksift::ui::framework::MacOsUiFramework::MeasureSingleLineText(
                    dwriteFactory_.Get(), smallText_.Get(), item.text);
                if (textWidth <= 0.0f) textWidth = static_cast<float>(item.text.size()) * 7.0f;
                if (badgeTextWidthCache_.size() > 256) badgeTextWidthCache_.clear();
                badgeTextWidthCache_.emplace(item.text, textWidth);
            }
            const float width = std::max(badgeHeight, std::ceil(textWidth) + 12.0f);
            if (x + width > area.right) break;
            const D2D1_RECT_F badge = D2D1::RectF(x, area.top, x + width, area.bottom);
            ID2D1Brush* brush = item.color == 0 ? textBrush_.Get() : accentBrush_.Get();
            ComPtr<ID2D1SolidColorBrush> labelBrush;
            if (item.color > 0 && item.color <= 5) {
                static const D2D1_COLOR_F colors[] = {
                    D2D1::ColorF(1,1,1), D2D1::ColorF(0.96f,0.28f,0.34f), D2D1::ColorF(1.0f,0.78f,0.20f),
                    D2D1::ColorF(0.27f,0.82f,0.45f), D2D1::ColorF(0.25f,0.67f,1.0f), D2D1::ColorF(0.72f,0.45f,0.95f)
                };
                if (SUCCEEDED(renderTarget_->CreateSolidColorBrush(colors[item.color], &labelBrush)))
                    brush = labelBrush.Get();
            }
            ID2D1Brush* fill = item.matchCardSurface ? nullptr : overlayBrush_.Get();
            ID2D1Brush* border = item.matchCardSurface ? glassBorderBrush_.Get() : nullptr;
            // Capsule badge with centred text.
            uiFramework_.PaintOverlayLabel(renderTarget_.Get(), badge,
                quicksift::ui::tokens::CapsuleRadius(badgeHeight),
                item.text, smallText_.Get(), fill, brush, border, 1.0f, /*centered=*/true);
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
        if (loadOnlyRawJpegPreviews_ && IsRawExtension(photo.extension)) return;
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
        renderTarget_->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for (int tileY = tileTop; tileY <= tileBottom && scheduled < scheduleLimit; ++tileY) {
            for (int tileX = tileLeft; tileX <= tileRight && scheduled < scheduleLimit;
                ++tileX, ++scheduled) {
                const int sourceX = tileX * sourceTileSpan;
                const int sourceY = tileY * sourceTileSpan;
                const int sourceWidth = std::min(sourceTileSpan, photo.sourceWidth - sourceX);
                const int sourceHeight = std::min(sourceTileSpan, photo.sourceHeight - sourceY);
                TileKey key{ photo.path.wstring(), sourceX, sourceY,
                    sourceWidth, sourceHeight, level };
                const auto iterator = tileCache_.find(key);
                if (iterator == tileCache_.end()) {
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
                iterator->second.lastUseTime = std::chrono::steady_clock::now();
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
        const int cacheTargetSize = 256;

        const D2D1_RECT_F bgRect = D2D1::RectF(
            bounds.right - kFilmstripWidth, bounds.top,
            bounds.right, bounds.bottom);
        const ThemePalette palette = Palette();
        ComPtr<ID2D1SolidColorBrush> bgBrush;
        const D2D1_COLOR_F filmstripBackground = {
            static_cast<float>(GetRValue(palette.panelStrong)) / 255.0f,
            static_cast<float>(GetGValue(palette.panelStrong)) / 255.0f,
            static_cast<float>(GetBValue(palette.panelStrong)) / 255.0f,
            0.90f * filmstripOpacity_
        };
        renderTarget_->CreateSolidColorBrush(filmstripBackground, &bgBrush);
        if (bgBrush) renderTarget_->FillRectangle(bgRect, bgBrush.Get());

        ComPtr<ID2D1SolidColorBrush> activeBorder;
        ComPtr<ID2D1SolidColorBrush> inactiveBorder;
        renderTarget_->CreateSolidColorBrush(
            D2D1::ColorF(0.0f, 0.55f, 1.0f, filmstripOpacity_), &activeBorder);
        renderTarget_->CreateSolidColorBrush(
            D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.40f * filmstripOpacity_), &inactiveBorder);

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
                const auto fitView = ResolveViewForPath(photo.path.wstring(), thumbRect, fitState,
                    bmp->sourceWidth, bmp->sourceHeight);
                if (fitView.valid) DrawBitmapFit(bmp, thumbRect, fitView, true);
            } else if (!failedThumbnailPaths_.empty() &&
                failedThumbnailPaths_.contains(NormalizedPathKey(photo.path))) {
                // 0.9.4.0: cache-only filmstrip; synchronous Shell extraction only for
                // definitively failed decodes, budgeted to one new bitmap per frame.
                if (ComPtr<ID2D1Bitmap> shell = GetOrCreateShellFallback(photo.path, cacheTargetSize)) {
                    renderTarget_->DrawBitmap(shell.Get(), thumbRect, filmstripOpacity_,
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                }
            }

            ID2D1Brush* border = i == currentIndex
                ? activeBorder.Get() : inactiveBorder.Get();
            if (border) renderTarget_->DrawRectangle(thumbRect, border, i == currentIndex ? 2.0f : 1.0f);
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
