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
            const wchar_t* message = currentFolder_.empty() ? L"Open a folder to begin" :
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
        DrawToastOverlay();
        DrawGlassAlertOverlay();
        DrawPerformanceHudOverlay();
        DrawThumbnailScrollbarOverlay();

        const HRESULT hr = renderTarget_->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            DiscardDeviceResources();
        } else if (SUCCEEDED(hr) && swapChain_) {
            const HRESULT present = swapChain_->Present(ThumbnailScrollPrefersPlaceholders() ? 0 : 1, 0);
            if (present == DXGI_ERROR_DEVICE_REMOVED || present == DXGI_ERROR_DEVICE_RESET) {
                DiscardDeviceResources();
            }
        }
        EndPaint(canvas_, &paint);
    }


    void QuickSiftApplicationImpl::ShowToast(const std::wstring& messageKey) {
        uiFramework_.Toasts().Show(Tr(messageKey));
        if (uiFramework_.Toasts().Visible()) {
            StartUiTimer(ID_TIMER_TOAST, 33, L"toast animation");
            InvalidateCanvas();
        }
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
        KillTimer(hwnd_, ID_TIMER_TOAST);
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
        KillTimer(hwnd_, ID_TIMER_TOAST);
        SetFocus(canvas_ ? canvas_ : hwnd_);
        InvalidateCanvas();
    }


    void QuickSiftApplicationImpl::ResolveGlassAlert(bool accepted) {
        const auto actionToken = uiFramework_.Alerts().Resolve(accepted);
        InvalidateCanvas();
        if (!actionToken) return;
        const PendingGlassAction action = static_cast<PendingGlassAction>(*actionToken);
        if (action == PendingGlassAction::DeleteSelection) {
            FileOperation(false, true);
            return;
        }
        if (action == PendingGlassAction::CloseWithPendingOperations) {
            closingAfterTransactionCancellation_ = true;
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
        uiFramework_.PaintOverlayLabel(renderTarget_.Get(), panel, 10.0f, L"", smallText_.Get(),
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
            reviewState_.Thumbnails().sizeDip, catalog_.VisibleCount(), kThumbnailRailSafeInsetDip);
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
            kThumbnailRailSafeInsetDip);
        const float padding = layout.padding;
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
            const int firstRow = std::max(0, static_cast<int>(thumbnailScroll / cellH));
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
                    const auto it = bitmapCache_.find(cacheKey);
                    if (it != bitmapCache_.end() && it->second.bitmap) {
                        const auto view = quicksift::review::transform::ResolvedView{};
                        DrawBitmapFit(&it->second, imageRect, view, false);
                    } else if (panelBrush_) {
                        // Never invoke Shell/WIC or create a new bitmap while the
                        // thumb is moving. A cheap placeholder is preferable.
                        renderTarget_->FillRectangle(imageRect, panelBrush_.Get());
                    }
                }
            }
            return;
        }

        const int firstRow = std::max(0, static_cast<int>(thumbnailScroll / cellH) - 1);
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
                const bool selected = selection_.Contains(photo.path.wstring());
                const bool focused = reviewState_.ThumbnailFocusPath() == photo.path.wstring();

                quicksift::ui::framework::MediaCardPresentation cardPresentation{};
                cardPresentation.rect = cell;
                cardPresentation.radius = 16.0f;
                cardPresentation.selected = selected;
                cardPresentation.focused = focused;
                cardPresentation.warning = HasFaceFocusWarning(photo);
                uiFramework_.PaintMediaCard(renderTarget_.Get(), cardPresentation,
                    { panelBrush_.Get(), glassBorderBrush_.Get(), accentBrush_.Get(), dangerBrush_.Get() });

                protectedThumbnailPaths_.insert(photo.path.wstring());
                BitmapEntry* bitmap = GetOrRequestBitmap(photo.path, thumbnailTarget,
                    JobPriority::Interactive, false, CacheClass::Thumbnail);
                if (bitmap && bitmap->bitmap) {
                    const ViewState fitState{};
                    const auto fitView = ResolveViewForPath(photo.path.wstring(), imageRect, fitState,
                        bitmap->sourceWidth, bitmap->sourceHeight);
                    DrawBitmapFit(bitmap, imageRect, fitView, false);
                } else if (thumbnailScrollMotionActive_) {
                    if (panelBrush_) renderTarget_->FillRectangle(imageRect, panelBrush_.Get());
                } else {
                    const auto shell = GetOrCreateShellFallback(photo.path, thumbnailTarget);
                    if (shell) renderTarget_->DrawBitmap(shell.Get(), imageRect, 0.85f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    else DrawCenteredText(Tr(L"loading…"), imageRect, smallText_.Get(), mutedBrush_.Get());
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
        if (photo.pickState > 0) return Tr(L"Pick");
        if (photo.pickState < 0) return Tr(L"Reject");
        return Tr(L"Unmarked");
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
        WicImageSourceEntry* preferredImageSource = nullptr;
        if (!raw && SupportsModernWicImageSource(photo.path)) preferredImageSource = GetOrCreateWicImageSource(photo.path);
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
            if (preferredImageSource) {
                nativeEdge = std::max(nativeEdge,
                    static_cast<int>(std::max(preferredImageSource->width, preferredImageSource->height)));
            }
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

        // Worker decoding is authoritative, but retain the existing WIC image-source
        // fallback for standard formats. It is only reached when no worker bitmap is
        // available, so a worker failure cannot strand the user on Loading forever.
        WicImageSourceEntry* imageSource = preferredImageSource;
        if (!imageSource && !raw && !bitmap) imageSource = GetOrCreateWicImageSource(photo.path);
        fallbackWidth = bitmap ? bitmap->sourceWidth :
            (imageSource ? static_cast<int>(imageSource->width) : fallbackWidth);
        fallbackHeight = bitmap ? bitmap->sourceHeight :
            (imageSource ? static_cast<int>(imageSource->height) : fallbackHeight);
        resolvedView = ResolveViewForPath(reviewState_.SinglePath(), imageArea, state,
            fallbackWidth, fallbackHeight);
        bool drewImage = false;
        const bool zoomedBeyondFit = resolvedView.relativeZoom > 1.001f;
        if (bitmap && bitmap->bitmap) {
            DrawBitmapFit(bitmap, imageArea, resolvedView, true);
            drewImage = true;
        }
        RenderVisibleTiles(photo, imageArea, resolvedView, bitmap);
        // WIC is the preferred low-memory source at Fit, but it is also a valid
        // last-resort fallback at higher zoom levels when no suitable worker
        // bitmap is available. This keeps a failed Full decode from leaving the
        // user with a blank image while still making the Full bitmap authoritative
        // whenever it succeeds.
        const bool fullBitmapReady = bitmap &&
            BitmapSatisfiesDisplay(bitmap, displayDesired, true);
        if (imageSource && !drewImage && (!zoomedBeyondFit || !fullBitmapReady)) {
            drewImage = DrawWicImageSourceFit(imageSource, imageArea, resolvedView, true);
        }
        if (!drewImage) DrawCenteredText(Tr(L"Loading image…"), imageArea, largeText_.Get(), mutedBrush_.Get());

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
            compareCard.radius = 14.0f;
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
            WicImageSourceEntry* imageSource = nullptr;
            if (!raw && !bitmap) imageSource = GetOrCreateWicImageSource(photo.path);
            fallbackWidth = bitmap ? bitmap->sourceWidth :
                (imageSource ? static_cast<int>(imageSource->width) : fallbackWidth);
            fallbackHeight = bitmap ? bitmap->sourceHeight :
                (imageSource ? static_cast<int>(imageSource->height) : fallbackHeight);
            resolvedView = ResolveViewForPath(reviewState_.ComparePaths()[i], imageArea, state,
                fallbackWidth, fallbackHeight);
            bool drewImage = false;
            if (bitmap && bitmap->bitmap) {
                DrawBitmapFit(bitmap, imageArea, resolvedView, true);
                drewImage = true;
            }
            RenderVisibleTiles(photo, imageArea, resolvedView, bitmap);
            if (imageSource) drewImage = DrawWicImageSourceFit(imageSource, imageArea, resolvedView, true) || drewImage;
            if (!drewImage) DrawCenteredText(Tr(L"Loading…"), imageArea, smallText_.Get(), mutedBrush_.Get());

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
            uiFramework_.PaintOverlayLabel(renderTarget_.Get(), labelRect, 9.0f, label,
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
        for (const ThumbnailBadge& item : badges) {
            const float width = std::max(20.0f,
                10.0f + static_cast<float>(item.text.size()) * 7.0f);
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
            uiFramework_.PaintOverlayLabel(renderTarget_.Get(), badge, 7.0f,
                item.text, smallText_.Get(), fill, brush, border);
            x += width + 4.0f;
        }
    }


    void QuickSiftApplicationImpl::DrawBitmapFit(BitmapEntry* bitmap, D2D1_RECT_F area,
        const quicksift::review::transform::ResolvedView& view, bool clip) {
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
        renderTarget_->DrawBitmap(bitmap->bitmap.Get(), destination, 1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        if (view.state.rotation != 0) renderTarget_->SetTransform(oldTransform);
        if (clip) renderTarget_->PopAxisAlignedClip();
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
            } else if (ComPtr<ID2D1Bitmap> shell = GetOrCreateShellFallback(photo.path, cacheTargetSize)) {
                renderTarget_->DrawBitmap(shell.Get(), thumbRect, filmstripOpacity_,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
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
