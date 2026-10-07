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

std::optional<D2D1_POINT_2F> QuickSiftApplicationImpl::PrimaryFaceCenter(
    const std::wstring& path) const {
    const PhotoItem* photo = PhotoForPath(path);
    if (!photo || !photo->facesScanned || photo->faces.empty()) return std::nullopt;
    const auto& face = photo->faces.front();
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
