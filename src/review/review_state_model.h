// OWNER: Authoritative review-session state for thumbnails, zoom, Compare synchronization, FaceLock, and async epochs.
#pragma once

#include "core/app_types.h"
#include "review/view_transform_policy.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quicksift::review {

struct FaceOffset {
    float x = 0.0f;
    float y = 0.0f;

    friend bool operator==(const FaceOffset&, const FaceOffset&) = default;
};

struct ThumbnailState {
    int sizeIndex = 2;
    int sizeDip = 180;
    float scrollDip = 0.0f;
    float maximumScrollDip = 0.0f;
};

struct ReviewEpochs {
    std::uint64_t navigation = 0;
    std::uint64_t thumbnailViewport = 1;
};

struct PathViewSnapshot {
    std::wstring path;
    quicksift::review::transform::ViewState view{};
};

// This model is intentionally UI-framework agnostic. It owns only durable/logical
// review state; pane geometry, face detection, selection, catalog lookup, drawing,
// and worker execution remain in their specialized owners.
class ReviewStateModel {
public:
    using ViewState = quicksift::review::transform::ViewState;
    using ViewMode = quicksift::core::ViewMode;

    [[nodiscard]] ViewMode Mode() const noexcept { return mode_; }
    void SetMode(ViewMode mode) noexcept { mode_ = mode; }

    [[nodiscard]] const ThumbnailState& Thumbnails() const noexcept { return thumbnails_; }
    void RestoreThumbnailSize(int index, int sizeDip) noexcept;
    void SetThumbnailSize(int index, int sizeDip) noexcept;
    void SetThumbnailScroll(float scrollDip) noexcept;
    void SetThumbnailMaximumScroll(float maximumScrollDip) noexcept;
    void ResetThumbnailViewport() noexcept;
    [[nodiscard]] std::uint64_t UpdateThumbnailViewport(std::uint64_t signature,
        std::span<const std::wstring> desiredPaths);
    [[nodiscard]] bool IsThumbnailPathDesired(const std::wstring& path) const noexcept;

    [[nodiscard]] const ReviewEpochs& Epochs() const noexcept { return epochs_; }
    [[nodiscard]] std::uint64_t AdvanceNavigationEpoch() noexcept;

    [[nodiscard]] bool SyncCompareView() const noexcept { return syncCompareView_; }
    [[nodiscard]] bool FaceLockEnabled() const noexcept { return faceLock_; }
    void RestorePreferences(bool syncCompareView, bool faceLock) noexcept;

    [[nodiscard]] const std::wstring& ThumbnailFocusPath() const noexcept {
        return thumbnailFocusPath_;
    }
    [[nodiscard]] const std::wstring& SinglePath() const noexcept { return singlePath_; }
    void SetThumbnailFocusPath(std::wstring path) { thumbnailFocusPath_ = std::move(path); }
    void SetSinglePath(std::wstring path) { singlePath_ = std::move(path); }
    void SetPrimaryPath(std::wstring path);
    void ClearPrimaryPaths() noexcept;
    [[nodiscard]] const std::wstring& ActivePath() const noexcept;

    [[nodiscard]] const std::vector<std::wstring>& ComparePaths() const noexcept {
        return comparePaths_;
    }
    [[nodiscard]] const std::wstring& ActiveComparePath() const noexcept {
        return activeComparePath_;
    }
    [[nodiscard]] std::optional<std::size_t> CompareSlotForPath(
        const std::wstring& path) const noexcept;
    [[nodiscard]] std::optional<std::size_t> ActiveCompareSlot() const noexcept;
    [[nodiscard]] bool IsCompared(const std::wstring& path) const noexcept;
    bool SetActiveComparePath(const std::wstring& path) noexcept;
    bool SetActiveCompareSlot(std::size_t slot) noexcept;

    // Compare entry always starts with a clean Fit view. If synchronization is
    // disabled, each slot receives its own independent Fit state immediately.
    bool EnterCompare(std::vector<std::wstring> paths);
    void ClearCompare() noexcept;
    bool ReplaceComparePath(const std::wstring& oldPath, const std::wstring& newPath);
    bool RetainComparedPaths(const std::unordered_set<std::wstring>& visiblePaths);

    [[nodiscard]] ViewState SharedView() const noexcept;
    void SetSharedView(ViewState state) noexcept { sharedView_ = state; }
    [[nodiscard]] ViewState BaseViewForPath(const std::wstring& path) const noexcept;
    void StoreBaseViewForPath(const std::wstring& path, ViewState state);

    [[nodiscard]] FaceOffset FaceOffsetForPath(const std::wstring& path) const noexcept;
    void StoreFaceOffsetForPath(const std::wstring& path, FaceOffset offset);
    void ResetFaceOffsetForPath(const std::wstring& path);

    // Synchronization transitions materialize exactly one authoritative form:
    // shared state when synchronized, per-path state when independent. Latent
    // stale copies are cleared rather than retained as a second source of truth.
    bool SetSyncCompareView(bool enabled);

    void EnableFaceLock();
    // visibleStates must contain the current materialized view of every pane that
    // needs to survive FaceLock disable. Multiple synchronized face-centered panes
    // necessarily become independent because their absolute centers differ.
    void DisableFaceLockPreservingViews(std::span<const PathViewSnapshot> visibleStates);

    // Folder changes preserve user preferences (thumbnail size, Sync preference,
    // FaceLock preference) but clear all image-specific/transient review state.
    void ResetForFolder() noexcept;

private:
    struct IndependentPaneState {
        ViewState view{};
        FaceOffset faceOffset{};
    };

    static ViewState FreshFitView() noexcept;
    static FaceOffset ClampFaceOffset(FaceOffset offset) noexcept;
    void InitializeIndependentCompareState();
    void ClearIndependentState() noexcept;

    ViewMode mode_ = ViewMode::Thumbnails;
    ThumbnailState thumbnails_{};
    ReviewEpochs epochs_{};
    std::uint64_t thumbnailViewportSignature_ = 0;
    std::unordered_set<std::wstring> desiredThumbnailPaths_;

    ViewState sharedView_{};
    bool syncCompareView_ = true;
    bool faceLock_ = false;
    FaceOffset sharedFaceOffset_{};

    std::wstring thumbnailFocusPath_;
    std::wstring singlePath_;
    std::vector<std::wstring> comparePaths_;
    std::wstring activeComparePath_;
    std::unordered_map<std::wstring, IndependentPaneState> independentPanes_;
};

} // namespace quicksift::review
