// OWNER: Win32/catalog adapter for the coordinated review-state subsystem.
#include "app/quicksift_application_internal.h"

namespace quicksift::app {
namespace {

quicksift::review::transform::ViewRect ToViewRect(D2D1_RECT_F rect) noexcept {
    return { rect.left, rect.top, rect.right, rect.bottom };
}

D2D1_POINT_2F ToD2DPoint(quicksift::review::FaceOffset offset) noexcept {
    return D2D1::Point2F(offset.x, offset.y);
}

quicksift::review::FaceOffset ToFaceOffset(D2D1_POINT_2F offset) noexcept {
    return { offset.x, offset.y };
}

} // namespace

std::wstring QuickSiftApplicationImpl::EditableViewPath() const {
    return reviewState_.ActivePath();
}

ViewState QuickSiftApplicationImpl::BaseViewForPath(const std::wstring& path) const {
    return reviewState_.BaseViewForPath(path);
}

D2D1_RECT_F QuickSiftApplicationImpl::ViewAreaForPath(const std::wstring& path) const {
    const D2D1_SIZE_F canvas = CanvasSizeInDips();
    const D2D1_RECT_F bounds = D2D1::RectF(0.0f, 0.0f, canvas.width, canvas.height);
    if (reviewState_.Mode() == ViewMode::Compare) {
        if (const auto slot = reviewState_.CompareSlotForPath(path)) {
            const std::vector<D2D1_RECT_F> cells = BuildBestCompareLayout(bounds);
            if (*slot < cells.size()) {
                const D2D1_RECT_F cell = cells[*slot];
                return D2D1::RectF(cell.left + 2.0f, cell.top + 2.0f,
                    cell.right - 2.0f, cell.bottom - 28.0f);
            }
        }
    }
    return D2D1::RectF(8.0f, 8.0f,
        std::max(9.0f, bounds.right - 8.0f), std::max(9.0f, bounds.bottom - 8.0f));
}

quicksift::review::transform::ViewGeometry QuickSiftApplicationImpl::ViewGeometryForPath(
    const std::wstring& path, D2D1_RECT_F area, int fallbackWidth, int fallbackHeight) const {
    int sourceWidth = fallbackWidth;
    int sourceHeight = fallbackHeight;
    if (const PhotoItem* photo = PhotoForPath(path);
        photo && photo->sourceWidth > 0 && photo->sourceHeight > 0) {
        sourceWidth = photo->sourceWidth;
        sourceHeight = photo->sourceHeight;
    }
    return { ToViewRect(area), sourceWidth, sourceHeight, CanvasPixelScale() };
}

quicksift::review::transform::ResolvedView QuickSiftApplicationImpl::ResolveViewForPath(
    const std::wstring& path, D2D1_RECT_F area, const ViewState& state,
    int fallbackWidth, int fallbackHeight) const {
    return quicksift::review::transform::ResolveView(state,
        ViewGeometryForPath(path, area, fallbackWidth, fallbackHeight));
}

std::optional<std::size_t> QuickSiftApplicationImpl::TargetFaceIndexForPath(
    const std::wstring& path) const {
    const PhotoItem* photo = PhotoForPath(path);
    if (!photo || !photo->facesScanned || photo->faces.empty()) return std::nullopt;
    return quicksift::core::ResolveTargetFaceIndex(photo->faces, photo->faceSelection);
}

std::optional<D2D1_POINT_2F> QuickSiftApplicationImpl::PrimaryFaceCenter(
    const std::wstring& path) const {
    // "Primary" = the FaceLock target: the user's (persisted) selection when it still
    // matches a detected face, otherwise faces[0] (centre-closest default ordering).
    const PhotoItem* photo = PhotoForPath(path);
    const auto target = TargetFaceIndexForPath(path);
    if (!photo || !target || *target >= photo->faces.size()) return std::nullopt;
    const auto& face = photo->faces[*target];
    return D2D1::Point2F(
        Clamp(face.x + face.width * 0.5f, 0.0f, 1.0f),
        Clamp(face.y + face.height * 0.5f, 0.0f, 1.0f));
}

D2D1_POINT_2F QuickSiftApplicationImpl::FaceOffsetForPath(const std::wstring& path) const {
    return ToD2DPoint(reviewState_.FaceOffsetForPath(path));
}

void QuickSiftApplicationImpl::SetFaceOffsetForPath(
    const std::wstring& path, D2D1_POINT_2F offset) {
    reviewState_.StoreFaceOffsetForPath(path, ToFaceOffset(offset));
}

void QuickSiftApplicationImpl::ResetFaceOffsetForPath(const std::wstring& path) {
    reviewState_.ResetFaceOffsetForPath(path);
}

bool QuickSiftApplicationImpl::IsBeyondFitZoom(const std::wstring& path,
    D2D1_RECT_F area, const ViewState& state) const {
    return quicksift::review::transform::IsBeyondFit(state, ViewGeometryForPath(path, area));
}

ViewState QuickSiftApplicationImpl::ViewForPath(
    const std::wstring& path, D2D1_RECT_F area) const {
    ViewState state = reviewState_.BaseViewForPath(path);
    if (reviewState_.FaceLockEnabled() && IsBeyondFitZoom(path, area, state)) {
        if (const auto face = PrimaryFaceCenter(path)) {
            const D2D1_POINT_2F offset = FaceOffsetForPath(path);
            state.centerX = face->x + offset.x;
            state.centerY = face->y + offset.y;
        }
    }
    return quicksift::review::transform::NormalizeView(state, ViewGeometryForPath(path, area));
}

ViewState QuickSiftApplicationImpl::ViewForPath(const std::wstring& path) const {
    return ViewForPath(path, ViewAreaForPath(path));
}

ViewState QuickSiftApplicationImpl::EditableViewState() const {
    return ViewForPath(EditableViewPath());
}

void QuickSiftApplicationImpl::StoreEditableViewState(const ViewState& visibleState) {
    const std::wstring path = EditableViewPath();
    const D2D1_RECT_F area = ViewAreaForPath(path);
    ViewState state = quicksift::review::transform::NormalizeView(visibleState,
        ViewGeometryForPath(path, area));

    if (reviewState_.FaceLockEnabled()) {
        if (IsBeyondFitZoom(path, area, state)) {
            if (const auto face = PrimaryFaceCenter(path)) {
                SetFaceOffsetForPath(path,
                    D2D1::Point2F(state.centerX - face->x, state.centerY - face->y));
            }
        } else {
            ResetFaceOffsetForPath(path);
        }
    }
    reviewState_.StoreBaseViewForPath(path, state);
}

std::uint64_t QuickSiftApplicationImpl::AdvanceReviewNavigationEpoch() {
    // Navigation / zoom-mode / view-mode changes invalidate click-to-zoom reverts.
    ClearFaceZoomToggle();
    const std::uint64_t epoch = reviewState_.AdvanceNavigationEpoch();
    worker_.SetNavigationEpoch(epoch);
    return epoch;
}

void QuickSiftApplicationImpl::RecenterFaceLockForPath(
    const std::wstring& path, bool requestIfMissing) {
    if (!reviewState_.FaceLockEnabled() || path.empty()) return;
    reviewState_.ResetFaceOffsetForPath(path);
    const PhotoItem* photo = PhotoForPath(path);
    if (!photo) return;
    if (!photo->facesScanned) {
        if (requestIfMissing) {
            EnqueueFaceAnalysis(photo->path, reviewState_.Epochs().navigation, FaceAnalysisPriority());
        }
        return;
    }
    if (!photo->faces.empty() && IsBeyondFitZoom(path, ViewAreaForPath(path),
        reviewState_.BaseViewForPath(path))) InvalidateCanvas();
}

std::optional<QuickSiftApplicationImpl::FaceOutlineHit> QuickSiftApplicationImpl::HitFaceOutline(
    float x, float y) const {
    if (!reviewState_.FaceLockEnabled()) return std::nullopt;
    std::wstring path;
    if (reviewState_.Mode() == ViewMode::Compare) {
        const auto slot = HitCompareSlot(x, y);
        if (!slot || *slot >= reviewState_.ComparePaths().size()) return std::nullopt;
        path = reviewState_.ComparePaths()[*slot];
    } else if (reviewState_.Mode() == ViewMode::Single) {
        path = reviewState_.SinglePath();
    } else {
        return std::nullopt;
    }
    const PhotoItem* photo = PhotoForPath(path);
    if (!photo || !photo->facesScanned || photo->faces.empty()) return std::nullopt;
    const D2D1_RECT_F area = ViewAreaForPath(path);
    if (x < area.left || x > area.right || y < area.top || y > area.bottom) return std::nullopt;
    const auto geometry = ViewGeometryForPath(path, area);
    const auto view = quicksift::review::transform::ResolveView(ViewForPath(path, area), geometry);
    if (!view.valid) return std::nullopt;

    // Same boxes the renderer outlines (quarter turns keep them axis-aligned).
    std::vector<quicksift::core::FaceHitBox> boxes;
    boxes.reserve(photo->faces.size());
    for (std::size_t index = 0; index < photo->faces.size(); ++index) {
        const auto& face = photo->faces[index];
        const auto first = quicksift::review::transform::SourceToScreen(view, geometry, { face.x, face.y });
        const auto second = quicksift::review::transform::SourceToScreen(view, geometry,
            { face.x + face.width, face.y + face.height });
        const float left = std::min(first.x, second.x);
        const float right = std::max(first.x, second.x);
        const float top = std::min(first.y, second.y);
        const float bottom = std::max(first.y, second.y);
        if (!(right - left >= 3.0f && bottom - top >= 3.0f)) continue; // not outlined either
        boxes.push_back(quicksift::core::InflateFaceHitBox(left, top, right, bottom, index));
    }
    const auto faceIndex = quicksift::core::HitTestFaceBoxes(boxes, x, y);
    if (!faceIndex) return std::nullopt;
    return FaceOutlineHit{ path, *faceIndex };
}

void QuickSiftApplicationImpl::SelectFaceAndFrame(const std::wstring& path, std::size_t faceIndex) {
    if (!reviewState_.FaceLockEnabled() || path.empty()) return;
    const auto catalogIndex = CatalogIndexForPath(path);
    if (!catalogIndex) return;
    const PhotoItem& photo = catalog_.PhotoAt(*catalogIndex);
    if (!photo.facesScanned || faceIndex >= photo.faces.size()) return;
    const quicksift::NormalizedFaceRect face = photo.faces[faceIndex];

    // 1. Target: stored as a normalized box (never an index) so it survives
    //    re-detection; persisted off the UI thread through the record-cache queue.
    catalog_.EditPhoto(*catalogIndex, [&](PhotoItem& item) {
        item.faceSelectionKnown = true;
        item.faceSelection = face;
    });
    PersistPhotoRecord(catalog_.PhotoAt(*catalogIndex));

    // 2. Framing: only the clicked pane is edited (mouse-down already made it the
    //    active Compare slot). With Sync on, the shared zoom follows this framing and
    //    every other pane stays centred on its OWN target face (shared offset 0).
    //    With Sync off, only this pane's independent view changes.
    if (reviewState_.Mode() == ViewMode::Compare && EditableViewPath() != path)
        static_cast<void>(reviewState_.SetActiveComparePath(path));
    if (EditableViewPath() == path) {
        const D2D1_RECT_F area = ViewAreaForPath(path);
        const auto geometry = ViewGeometryForPath(path, area);
        ResetFaceOffsetForPath(path);
        const ViewState framed = quicksift::review::transform::FrameSourceRect(
            ViewForPath(path, area), geometry,
            { face.x, face.y, face.x + face.width, face.y + face.height },
            quicksift::core::kFaceClickFillFraction);
        StoreEditableViewState(framed);
        const auto interactionEpoch = viewInteractionEpoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
        worker_.SetViewInteractionEpoch(interactionEpoch);
        worker_.DiscardQueuedTiles();
        UpdateZoomModeButton();
    }
    // A target (lock) change re-shows the outlines for this pane.
    faceOutlineFade_.Reveal(path, std::chrono::steady_clock::now());
    UpdateStatus();
    InvalidateCanvas();
}

std::wstring QuickSiftApplicationImpl::FaceZoomKeyForPath(const std::wstring& path) const {
    // Sync on: every Compare pane shares one view, so one toggle entry covers the
    // whole Compare set (consistent with the earlier Sync framing semantics).
    if (reviewState_.Mode() == ViewMode::Compare && reviewState_.SyncCompareView())
        return std::wstring(L"\x1F" L"compare-sync");
    return path;
}

bool QuickSiftApplicationImpl::PathDisplayedInReview(const std::wstring& path) const {
    if (path.empty()) return false;
    if (reviewState_.Mode() == ViewMode::Single) return reviewState_.SinglePath() == path;
    if (reviewState_.Mode() == ViewMode::Compare) {
        const auto& paths = reviewState_.ComparePaths();
        return std::find(paths.begin(), paths.end(), path) != paths.end();
    }
    return false;
}

std::optional<quicksift::review::face_interaction::PaneViewSnapshot>
QuickSiftApplicationImpl::PaneSnapshotForPath(const std::wstring& path) const {
    if (!PathDisplayedInReview(path)) return std::nullopt;
    const D2D1_RECT_F area = ViewAreaForPath(path);
    quicksift::review::face_interaction::PaneViewSnapshot snapshot;
    snapshot.view = ViewForPath(path, area);
    snapshot.area = ToViewRect(area);
    return snapshot;
}

void QuickSiftApplicationImpl::CaptureFaceTogglePress(const FaceOutlineHit& hit) {
    faceTogglePress_ = {};
    const auto pre = PaneSnapshotForPath(hit.path);
    if (!pre) return;
    faceTogglePress_.key = FaceZoomKeyForPath(hit.path);
    if (const auto* entry = faceZoomToggle_.Find(faceTogglePress_.key))
        faceTogglePress_.framedNow = PaneSnapshotForPath(entry->framedPath);
    faceTogglePress_.prePath = hit.path;
    faceTogglePress_.pre = *pre;
    faceTogglePress_.valid = true;
}

void QuickSiftApplicationImpl::HandleFaceOutlineClick(const FaceOutlineHit& hit) {
    namespace fi = quicksift::review::face_interaction;
    FaceTogglePressCapture captured = std::move(faceTogglePress_);
    faceTogglePress_ = {};
    if (!reviewState_.FaceLockEnabled()) return;
    const PhotoItem* photo = PhotoForPath(hit.path);
    if (!photo || !photo->facesScanned || hit.faceIndex >= photo->faces.size()) return;
    const quicksift::NormalizedFaceRect box = photo->faces[hit.faceIndex];
    const fi::FaceIdentity identity{ hit.path,
        { box.x, box.y, box.x + box.width, box.y + box.height } };
    const std::wstring key = FaceZoomKeyForPath(hit.path);

    // Decision inputs come from mouse-down (before Compare slot activation could
    // move any view); fall back to the current state when unavailable.
    std::optional<fi::PaneViewSnapshot> framedNow;
    std::wstring prePath = hit.path;
    std::optional<fi::PaneViewSnapshot> pre;
    if (captured.valid && captured.key == key) {
        framedNow = captured.framedNow;
        prePath = captured.prePath;
        pre = captured.pre;
    } else {
        if (const auto* entry = faceZoomToggle_.Find(key))
            framedNow = PaneSnapshotForPath(entry->framedPath);
        pre = PaneSnapshotForPath(hit.path);
    }
    const fi::FaceClickDecision decision = faceZoomToggle_.Decide(key, identity, framedNow);

    if (decision.action == fi::FaceClickAction::Revert) {
        // Only zoom/pan revert; the clicked face stays the (persisted) target.
        faceZoomToggle_.Clear(key);
        if (!PathDisplayedInReview(decision.revertPath)) return;
        const std::wstring active = EditableViewPath();
        const bool switchActive = reviewState_.Mode() == ViewMode::Compare &&
            decision.revertPath != active;
        if (switchActive) static_cast<void>(reviewState_.SetActiveComparePath(decision.revertPath));
        if (EditableViewPath() == decision.revertPath) StoreEditableViewState(decision.revertTarget.view);
        if (switchActive && !active.empty())
            static_cast<void>(reviewState_.SetActiveComparePath(active));
        const auto interactionEpoch = viewInteractionEpoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
        worker_.SetViewInteractionEpoch(interactionEpoch);
        worker_.DiscardQueuedTiles();
        faceOutlineFade_.Reveal(hit.path, std::chrono::steady_clock::now());
        UpdateZoomModeButton();
        UpdateStatus();
        InvalidateCanvas();
        return;
    }

    SelectFaceAndFrame(hit.path, hit.faceIndex);
    const auto framed = PaneSnapshotForPath(hit.path);
    if (framed && pre) {
        faceZoomToggle_.CommitLockZoom(key, identity, decision.keepExistingPreZoom,
            prePath, *pre, hit.path, *framed);
    } else {
        faceZoomToggle_.Clear(key);
    }
}

void QuickSiftApplicationImpl::ClearFaceZoomToggle() noexcept {
    faceZoomToggle_.ClearAll();
    faceTogglePress_ = {};
}

quicksift::review::face_interaction::DragThreshold
QuickSiftApplicationImpl::CanvasDragThreshold() const {
    // SM_CXDRAG/SM_CYDRAG at the canvas DPI (GetSystemMetricsForDpi on Windows 10
    // 1607+, system-DPI metrics otherwise), converted to DIPs, floor 4 DIP.
    using GetMetricsForDpi = int(WINAPI*)(int, UINT);
    static const auto getMetricsForDpi = reinterpret_cast<GetMetricsForDpi>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi")));
    const float dpi = CanvasDpi();
    int cx = 0;
    int cy = 0;
    float pixelsPerDip = dpi / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    if (getMetricsForDpi) {
        const UINT dpiValue = static_cast<UINT>(std::lround(dpi));
        cx = getMetricsForDpi(SM_CXDRAG, dpiValue);
        cy = getMetricsForDpi(SM_CYDRAG, dpiValue);
    } else {
        cx = GetSystemMetrics(SM_CXDRAG);
        cy = GetSystemMetrics(SM_CYDRAG);
        HDC screen = GetDC(nullptr);
        if (screen) {
            pixelsPerDip = static_cast<float>(GetDeviceCaps(screen, LOGPIXELSX)) /
                static_cast<float>(USER_DEFAULT_SCREEN_DPI);
            ReleaseDC(nullptr, screen);
        }
    }
    return quicksift::review::face_interaction::ComputeDragThreshold(cx, cy, pixelsPerDip,
        quicksift::core::kFaceClickMaxTravelDip);
}

bool QuickSiftApplicationImpl::CancelCanvasPress() {
    const bool active = canvasPress_.Active() || pendingFaceClick_.has_value();
    canvasPress_.Cancel();
    pendingFaceClick_.reset();
    faceTogglePress_ = {};
    facePanDeferred_ = false;
    if (!active) return false;
    if (dragging_) {
        dragging_ = false;
        if (GetCapture() == canvas_) ReleaseCapture();
    }
    InvalidateCanvas();
    return true;
}

bool QuickSiftApplicationImpl::ToggleFaceOutlinesPinned(float x, float y) {
    if (!reviewState_.FaceLockEnabled()) return false;
    if (reviewState_.Mode() != ViewMode::Single && reviewState_.Mode() != ViewMode::Compare) return false;
    if (FilmstripContains(x, y)) return false;
    std::wstring path;
    if (reviewState_.Mode() == ViewMode::Compare) {
        const auto slot = HitCompareSlot(x, y);
        if (!slot || *slot >= reviewState_.ComparePaths().size()) return false;
        path = reviewState_.ComparePaths()[*slot];
    } else {
        path = reviewState_.SinglePath();
    }
    if (path.empty()) return false;
    const D2D1_RECT_F area = ViewAreaForPath(path);
    if (x < area.left || x > area.right || y < area.top || y > area.bottom) return false;
    faceOutlineFade_.SetPinned(!faceOutlineFade_.Pinned(), std::chrono::steady_clock::now());
    SaveSession(); // persistent preference (session file), like Sync / FaceLock
    ShowToast(faceOutlineFade_.Pinned() ? L"Face outlines: always shown (right-click to auto-hide)" :
        L"Face outlines: auto-hide after 1 second (right-click to keep shown)");
    InvalidateCanvas();
    return true;
}

void QuickSiftApplicationImpl::DisableFaceLockPreservingViews() {
    if (!reviewState_.FaceLockEnabled()) return;
    std::vector<quicksift::review::PathViewSnapshot> visibleStates;
    if (reviewState_.Mode() == ViewMode::Compare) {
        visibleStates.reserve(reviewState_.ComparePaths().size());
        for (const auto& path : reviewState_.ComparePaths()) {
            visibleStates.push_back({ path, ViewForPath(path, ViewAreaForPath(path)) });
        }
    } else {
        const std::wstring path = EditableViewPath();
        if (!path.empty()) visibleStates.push_back({ path, ViewForPath(path) });
    }
    reviewState_.DisableFaceLockPreservingViews(visibleStates);
}

} // namespace quicksift::app
