// OWNER: Portable FaceLock interaction policy: click-to-zoom toggle state machine,
// click-versus-drag press gesture, and face-outline reveal/fade timing.
#pragma once

#include "review/view_transform_policy.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quicksift::review::face_interaction {

// ---- Click-to-zoom toggle ----------------------------------------------------------
// A pane's visible view plus the pane rectangle it was resolved against. Two
// snapshots are "the same view" when zoom/centre/rotation/mode and the pane rect
// match within tolerances that ignore float noise but catch any meaningful manual
// zoom, pan, rotation, or resize.
struct PaneViewSnapshot {
    transform::ViewState view{};
    transform::ViewRect area{};
};

inline constexpr float kSnapshotZoomRelativeTolerance = 0.005f; // 0.5 %
inline constexpr float kSnapshotCenterTolerance = 0.0015f;      // normalized source units
inline constexpr float kSnapshotAreaToleranceDip = 0.75f;

[[nodiscard]] bool SnapshotsEquivalent(const PaneViewSnapshot& left,
    const PaneViewSnapshot& right) noexcept;

// A face is identified by its photo and its normalized box (never an index, which
// re-detection may reorder).
struct FaceIdentity {
    std::wstring path;
    transform::ViewRect box{};
};

inline constexpr float kFaceIdentityBoxTolerance = 0.0005f;
[[nodiscard]] bool SameFace(const FaceIdentity& left, const FaceIdentity& right) noexcept;

enum class FaceClickAction {
    LockZoom, // select (if needed) and frame the clicked face at 70 % fill
    Revert,   // restore the view captured immediately before the first lock-zoom
};

struct FaceClickDecision {
    FaceClickAction action = FaceClickAction::LockZoom;
    // LockZoom: true when an earlier, still-valid lock-zoom exists for this key
    // (a different face was clicked) so the ORIGINAL pre-zoom view stays the
    // revert target. Revert: always false.
    bool keepExistingPreZoom = false;
    std::wstring revertPath;          // Revert only: pane path whose view is restored
    PaneViewSnapshot revertTarget{};  // Revert only
};

// Per-key state machine. A key is one pane path (Single, Compare with Sync off) or
// one shared key (Compare with Sync on, where every pane shares a single view).
//
//   Idle --face click--> ZoomedToFace(pre, framed)
//   ZoomedToFace --same face click, view still == framed--> Idle (revert to pre)
//   ZoomedToFace --other face click, view still == framed--> ZoomedToFace(pre kept)
//   ZoomedToFace --view != framed (manual zoom/pan/rotate/resize/navigation)--> Idle
//   any --Clear/ClearAll (explicit manual change, mode/folder/Sync/FaceLock change)--> Idle
class FaceZoomToggle {
public:
    struct Entry {
        FaceIdentity face;
        std::wstring prePath;
        PaneViewSnapshot preZoom{};
        std::wstring framedPath;
        PaneViewSnapshot framed{};
    };

    [[nodiscard]] const Entry* Find(const std::wstring& key) const noexcept;

    // framedPathNow: the CURRENT snapshot of the entry's framedPath, or nullopt when
    // that pane is no longer displayed (navigation/mode change). Pure: no mutation.
    [[nodiscard]] FaceClickDecision Decide(const std::wstring& key, const FaceIdentity& clicked,
        const std::optional<PaneViewSnapshot>& framedPathNow) const;

    // Records a completed lock-zoom. When keepExistingPreZoom is true and an entry
    // exists, its prePath/preZoom are preserved; otherwise prePath/preZoom are used.
    void CommitLockZoom(const std::wstring& key, const FaceIdentity& face,
        bool keepExistingPreZoom, const std::wstring& prePath, const PaneViewSnapshot& preZoom,
        const std::wstring& framedPath, const PaneViewSnapshot& framed);

    void Clear(const std::wstring& key) noexcept;
    void ClearAll() noexcept;
    [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }

private:
    std::unordered_map<std::wstring, Entry> entries_;
};

// ---- Click versus drag -------------------------------------------------------------
struct DragThreshold {
    float x = 4.0f;
    float y = 4.0f;
};

// System drag rectangle (SM_CXDRAG/SM_CYDRAG in physical pixels at the window DPI)
// converted to DIPs, never smaller than minimumDip on either axis.
[[nodiscard]] DragThreshold ComputeDragThreshold(int systemDragWidthPx, int systemDragHeightPx,
    float pixelsPerDip, float minimumDip) noexcept;

// One mouse press on a click candidate. A press becomes a drag once the pointer
// leaves the drag rectangle on either axis; a drag is latched (returning to the
// origin never turns it back into a click). Release counts as a click only for an
// active, non-drag press released inside the target.
class PressGesture {
public:
    void Begin(transform::ViewPoint down, std::uint64_t tickMs, DragThreshold threshold) noexcept;
    // Returns true exactly once: when this move converts the press into a drag.
    bool Move(transform::ViewPoint point) noexcept;
    // Ends the press. Returns true when it is a click.
    bool Release(transform::ViewPoint point, bool releaseInsideTarget) noexcept;
    void Cancel() noexcept;

    [[nodiscard]] bool Active() const noexcept { return active_; }
    [[nodiscard]] bool IsDrag() const noexcept { return drag_; }
    [[nodiscard]] transform::ViewPoint Down() const noexcept { return down_; }
    [[nodiscard]] std::uint64_t DownTick() const noexcept { return downTick_; }

private:
    [[nodiscard]] bool Exceeds(transform::ViewPoint point) const noexcept;

    transform::ViewPoint down_{};
    DragThreshold threshold_{};
    std::uint64_t downTick_ = 0;
    bool active_ = false;
    bool drag_ = false;
};

// ---- Face outline reveal / fade ----------------------------------------------------
inline constexpr std::chrono::milliseconds kOutlineHold{ 1000 };
inline constexpr std::chrono::milliseconds kOutlineFade{ 250 };

// Tracks which panes currently display outlines. A pane key that was not displayed
// in the previous frame (photo changed, faces arrived, FaceLock turned on, image
// first drawn) is revealed automatically: fully visible for kOutlineHold, then fades
// out over kOutlineFade. Reveal() re-arms explicitly (e.g. target face changed).
// Pinned (right-click toggle) keeps every outline fully visible until unpinned;
// unpinning starts the fade immediately. Fade progress is linear 0..1; the caller
// applies its easing curve (Smoothstep01) and multiplies outline opacity.
class FaceOutlineFade {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    void BeginFrame() noexcept;
    // Marks key as displayed this frame and returns its linear fade progress.
    float Observe(const std::wstring& key, TimePoint now);
    void EndFrame();

    void Reveal(const std::wstring& key, TimePoint now);
    void RevealDisplayed(TimePoint now);
    void SetPinned(bool pinned, TimePoint now);
    [[nodiscard]] bool Pinned() const noexcept { return pinned_; }
    // Forgets all per-path fade state (folder change). Keeps the pin preference.
    void Reset() noexcept;

    // Linear progress: 0 = fully visible, 1 = fully faded. Unknown keys are faded.
    [[nodiscard]] float Progress(const std::wstring& key, TimePoint now) const noexcept;
    // True while some displayed key's progress differs from what was last painted
    // (i.e. a fade frame is due). The hold period never requests repaints.
    [[nodiscard]] bool NeedsRepaint(TimePoint now) const noexcept;

private:
    std::unordered_map<std::wstring, TimePoint> fadeStart_;
    std::unordered_map<std::wstring, float> lastPainted_;
    std::unordered_set<std::wstring> displayed_;
    std::unordered_set<std::wstring> observed_;
    bool pinned_ = false;
    bool inFrame_ = false;
};

} // namespace quicksift::review::face_interaction
