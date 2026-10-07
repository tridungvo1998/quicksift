// OWNER: Authoritative review-session state for thumbnails, zoom, Compare synchronization, FaceLock, and async epochs.
#include "review/review_state_model.h"

#include <algorithm>
#include <cmath>

namespace quicksift::review {
namespace {

float ClampFinite(float value, float minimum, float maximum, float fallback) noexcept {
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, minimum, maximum);
}

} // namespace

ReviewStateModel::ViewState ReviewStateModel::FreshFitView() noexcept {
    return ViewState{ 1.0f, 0.5f, 0.5f, 0, quicksift::core::ZoomMode::Fit };
}

FaceOffset ReviewStateModel::ClampFaceOffset(FaceOffset offset) noexcept {
    offset.x = ClampFinite(offset.x, -1.0f, 1.0f, 0.0f);
    offset.y = ClampFinite(offset.y, -1.0f, 1.0f, 0.0f);
    return offset;
}

void ReviewStateModel::RestoreThumbnailSize(int index, int sizeDip) noexcept {
    thumbnails_.sizeIndex = std::clamp(index, 0, 4);
    thumbnails_.sizeDip = std::max(1, sizeDip);
}

void ReviewStateModel::SetThumbnailSize(int index, int sizeDip) noexcept {
    RestoreThumbnailSize(index, sizeDip);
}

void ReviewStateModel::SetThumbnailScroll(float scrollDip) noexcept {
    thumbnails_.scrollDip = ClampFinite(scrollDip, 0.0f,
        std::max(0.0f, thumbnails_.maximumScrollDip), 0.0f);
}

void ReviewStateModel::SetThumbnailMaximumScroll(float maximumScrollDip) noexcept {
    thumbnails_.maximumScrollDip = std::max(0.0f, maximumScrollDip);
    SetThumbnailScroll(thumbnails_.scrollDip);
}

void ReviewStateModel::ResetThumbnailViewport() noexcept {
    thumbnails_.scrollDip = 0.0f;
    thumbnails_.maximumScrollDip = 0.0f;
}

std::uint64_t ReviewStateModel::UpdateThumbnailViewport(std::uint64_t signature,
    std::span<const std::wstring> desiredPaths) {
    std::unordered_set<std::wstring> desired;
    desired.reserve(desiredPaths.size());
    for (const auto& path : desiredPaths) {
        if (!path.empty()) desired.insert(path);
    }
    if (signature == thumbnailViewportSignature_ && desired == desiredThumbnailPaths_) {
        return epochs_.thumbnailViewport;
    }
    thumbnailViewportSignature_ = signature;
    desiredThumbnailPaths_.swap(desired);
    ++epochs_.thumbnailViewport;
    if (epochs_.thumbnailViewport == 0) epochs_.thumbnailViewport = 1;
    return epochs_.thumbnailViewport;
}

bool ReviewStateModel::IsThumbnailPathDesired(const std::wstring& path) const noexcept {
    return !path.empty() && desiredThumbnailPaths_.contains(path);
}

std::uint64_t ReviewStateModel::AdvanceNavigationEpoch() noexcept {
    ++epochs_.navigation;
    return epochs_.navigation;
}

void ReviewStateModel::RestorePreferences(bool syncCompareView, bool faceLock) noexcept {
    syncCompareView_ = syncCompareView;
    faceLock_ = faceLock;
    sharedFaceOffset_ = {};
    ClearIndependentState();
}

void ReviewStateModel::SetPrimaryPath(std::wstring path) {
    thumbnailFocusPath_ = path;
    singlePath_ = std::move(path);
}

void ReviewStateModel::ClearPrimaryPaths() noexcept {
    thumbnailFocusPath_.clear();
    singlePath_.clear();
}

const std::wstring& ReviewStateModel::ActivePath() const noexcept {
    if (mode_ == ViewMode::Compare) return activeComparePath_;
    if (mode_ == ViewMode::Single) return singlePath_;
    return thumbnailFocusPath_;
}

std::optional<std::size_t> ReviewStateModel::CompareSlotForPath(
    const std::wstring& path) const noexcept {
    const auto iterator = std::find(comparePaths_.begin(), comparePaths_.end(), path);
    if (iterator == comparePaths_.end()) return std::nullopt;
    return static_cast<std::size_t>(std::distance(comparePaths_.begin(), iterator));
}

std::optional<std::size_t> ReviewStateModel::ActiveCompareSlot() const noexcept {
    return CompareSlotForPath(activeComparePath_);
}

bool ReviewStateModel::IsCompared(const std::wstring& path) const noexcept {
    return CompareSlotForPath(path).has_value();
}

bool ReviewStateModel::SetActiveComparePath(const std::wstring& path) noexcept {
    if (!IsCompared(path) || path == activeComparePath_) return false;
    activeComparePath_ = path;
    return true;
}

bool ReviewStateModel::SetActiveCompareSlot(std::size_t slot) noexcept {
    if (slot >= comparePaths_.size()) return false;
    return SetActiveComparePath(comparePaths_[slot]);
}

void ReviewStateModel::InitializeIndependentCompareState() {
    independentPanes_.clear();
    for (const auto& path : comparePaths_) {
        independentPanes_.emplace(path, IndependentPaneState{ sharedView_, sharedFaceOffset_ });
    }
}

void ReviewStateModel::ClearIndependentState() noexcept {
    independentPanes_.clear();
}

bool ReviewStateModel::EnterCompare(std::vector<std::wstring> paths) {
    std::vector<std::wstring> unique;
    unique.reserve(std::min<std::size_t>(paths.size(), 6));
    for (auto& path : paths) {
        if (path.empty() || std::find(unique.begin(), unique.end(), path) != unique.end()) continue;
        unique.push_back(std::move(path));
        if (unique.size() == 6) break;
    }
    if (unique.size() < 2) return false;

    comparePaths_ = std::move(unique);
    activeComparePath_ = comparePaths_.front();
    sharedView_ = FreshFitView();
    sharedFaceOffset_ = {};
    ClearIndependentState();
    if (!syncCompareView_) InitializeIndependentCompareState();
    mode_ = ViewMode::Compare;
    return true;
}

void ReviewStateModel::ClearCompare() noexcept {
    comparePaths_.clear();
    activeComparePath_.clear();
    ClearIndependentState();
    sharedView_ = FreshFitView();
    sharedFaceOffset_ = {};
}

bool ReviewStateModel::ReplaceComparePath(const std::wstring& oldPath,
    const std::wstring& newPath) {
    if (newPath.empty()) return false;
    const auto slot = CompareSlotForPath(oldPath);
    if (!slot || oldPath == newPath) return false;
    if (IsCompared(newPath)) return false;

    ViewState inherited = FreshFitView();
    if (!syncCompareView_) {
        if (const auto iterator = independentPanes_.find(oldPath);
            iterator != independentPanes_.end()) inherited = iterator->second.view;
    }

    comparePaths_[*slot] = newPath;
    if (activeComparePath_ == oldPath) activeComparePath_ = newPath;
    independentPanes_.erase(oldPath);
    if (!syncCompareView_) {
        independentPanes_[newPath] = IndependentPaneState{ inherited, {} };
    }
    return true;
}

bool ReviewStateModel::RetainComparedPaths(
    const std::unordered_set<std::wstring>& visiblePaths) {
    const bool wasCompare = mode_ == ViewMode::Compare;
    std::erase_if(comparePaths_, [&](const std::wstring& path) {
        return !visiblePaths.contains(path);
    });
    std::erase_if(independentPanes_, [&](const auto& entry) {
        return !visiblePaths.contains(entry.first) || !IsCompared(entry.first);
    });

    if (comparePaths_.size() < 2) {
        ClearCompare();
        if (wasCompare) mode_ = ViewMode::Thumbnails;
        return wasCompare;
    }
    if (!IsCompared(activeComparePath_)) activeComparePath_ = comparePaths_.front();
    return false;
}

ReviewStateModel::ViewState ReviewStateModel::SharedView() const noexcept {
    if (mode_ == ViewMode::Compare && !syncCompareView_) {
        return BaseViewForPath(activeComparePath_);
    }
    return sharedView_;
}

ReviewStateModel::ViewState ReviewStateModel::BaseViewForPath(
    const std::wstring& path) const noexcept {
    if (mode_ == ViewMode::Compare && !syncCompareView_) {
        if (const auto iterator = independentPanes_.find(path);
            iterator != independentPanes_.end()) return iterator->second.view;
        // An independent pane that lacks state is never allowed to inherit latent
        // shared state. This prevents zoom/rotation leakage into newly appearing images.
        return FreshFitView();
    }
    return sharedView_;
}

void ReviewStateModel::StoreBaseViewForPath(const std::wstring& path, ViewState state) {
    if (mode_ == ViewMode::Compare && !syncCompareView_ && !path.empty()) {
        independentPanes_[path].view = state;
    } else {
        sharedView_ = state;
    }
}

FaceOffset ReviewStateModel::FaceOffsetForPath(const std::wstring& path) const noexcept {
    if (mode_ == ViewMode::Compare && !syncCompareView_) {
        if (const auto iterator = independentPanes_.find(path);
            iterator != independentPanes_.end()) return iterator->second.faceOffset;
        return {};
    }
    return sharedFaceOffset_;
}

void ReviewStateModel::StoreFaceOffsetForPath(const std::wstring& path, FaceOffset offset) {
    offset = ClampFaceOffset(offset);
    if (mode_ == ViewMode::Compare && !syncCompareView_ && !path.empty()) {
        independentPanes_[path].faceOffset = offset;
    } else {
        sharedFaceOffset_ = offset;
    }
}

void ReviewStateModel::ResetFaceOffsetForPath(const std::wstring& path) {
    StoreFaceOffsetForPath(path, {});
}

bool ReviewStateModel::SetSyncCompareView(bool enabled) {
    if (enabled == syncCompareView_) return false;
    if (mode_ != ViewMode::Compare || comparePaths_.size() < 2) {
        syncCompareView_ = enabled;
        ClearIndependentState();
        return true;
    }

    if (!enabled) {
        syncCompareView_ = false;
        InitializeIndependentCompareState();
        // Independent panes are now the sole authority. Clear the dormant shared
        // copy so a future caller cannot accidentally observe stale synchronized state.
        sharedView_ = FreshFitView();
        sharedFaceOffset_ = {};
        return true;
    }

    const ViewState activeView = BaseViewForPath(activeComparePath_);
    const FaceOffset activeOffset = FaceOffsetForPath(activeComparePath_);
    sharedView_ = activeView;
    sharedFaceOffset_ = activeOffset;
    syncCompareView_ = true;
    ClearIndependentState();
    return true;
}

void ReviewStateModel::EnableFaceLock() {
    faceLock_ = true;
    sharedFaceOffset_ = {};
    if (mode_ == ViewMode::Compare && !syncCompareView_) {
        for (const auto& path : comparePaths_) independentPanes_[path].faceOffset = {};
    }
}

void ReviewStateModel::DisableFaceLockPreservingViews(
    std::span<const PathViewSnapshot> visibleStates) {
    if (!faceLock_) return;
    faceLock_ = false;
    sharedFaceOffset_ = {};
    if (mode_ == ViewMode::Compare && !comparePaths_.empty()) {
        if (syncCompareView_ && visibleStates.size() > 1) {
            // Different face anchors can produce different absolute centers even
            // with one synchronized relative offset. Materialize those views and
            // make the ownership explicit instead of silently losing framing.
            syncCompareView_ = false;
            sharedView_ = FreshFitView();
            independentPanes_.clear();
            for (const auto& snapshot : visibleStates) {
                if (IsCompared(snapshot.path)) {
                    independentPanes_[snapshot.path] = IndependentPaneState{ snapshot.view, {} };
                }
            }
            for (const auto& path : comparePaths_) {
                if (!independentPanes_.contains(path)) {
                    independentPanes_[path] = IndependentPaneState{ FreshFitView(), {} };
                }
            }
        } else if (!syncCompareView_) {
            for (const auto& snapshot : visibleStates) {
                if (IsCompared(snapshot.path)) independentPanes_[snapshot.path].view = snapshot.view;
            }
        } else if (!visibleStates.empty()) {
            sharedView_ = visibleStates.front().view;
        }
        return;
    }

    if (!visibleStates.empty()) sharedView_ = visibleStates.front().view;
}

void ReviewStateModel::ResetForFolder() noexcept {
    mode_ = ViewMode::Thumbnails;
    ClearCompare();
    ClearPrimaryPaths();
    sharedView_ = FreshFitView();
    sharedFaceOffset_ = {};
    ResetThumbnailViewport();
    thumbnailViewportSignature_ = 0;
    desiredThumbnailPaths_.clear();
    (void)AdvanceNavigationEpoch();
    ++epochs_.thumbnailViewport;
    if (epochs_.thumbnailViewport == 0) epochs_.thumbnailViewport = 1;
}

} // namespace quicksift::review
