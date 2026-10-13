// OWNER: Portable FaceLock interaction policy implementation.
#include "review/face_interaction_policy.h"

#include <algorithm>
#include <cmath>

namespace quicksift::review::face_interaction {
namespace {

bool Near(float left, float right, float tolerance) noexcept {
    return std::fabs(left - right) <= tolerance;
}

bool RectNear(const transform::ViewRect& a, const transform::ViewRect& b, float tolerance) noexcept {
    return Near(a.left, b.left, tolerance) && Near(a.top, b.top, tolerance) &&
        Near(a.right, b.right, tolerance) && Near(a.bottom, b.bottom, tolerance);
}

} // namespace

bool SnapshotsEquivalent(const PaneViewSnapshot& left, const PaneViewSnapshot& right) noexcept {
    if (left.view.mode != right.view.mode) return false;
    if (((left.view.rotation % 360) + 360) % 360 != ((right.view.rotation % 360) + 360) % 360)
        return false;
    const float zoomScale = std::max(std::fabs(left.view.zoom), std::fabs(right.view.zoom));
    if (std::fabs(left.view.zoom - right.view.zoom) >
        kSnapshotZoomRelativeTolerance * std::max(zoomScale, 1e-6f)) return false;
    if (!Near(left.view.centerX, right.view.centerX, kSnapshotCenterTolerance) ||
        !Near(left.view.centerY, right.view.centerY, kSnapshotCenterTolerance)) return false;
    return RectNear(left.area, right.area, kSnapshotAreaToleranceDip);
}

bool SameFace(const FaceIdentity& left, const FaceIdentity& right) noexcept {
    return left.path == right.path && RectNear(left.box, right.box, kFaceIdentityBoxTolerance);
}

const FaceZoomToggle::Entry* FaceZoomToggle::Find(const std::wstring& key) const noexcept {
    const auto found = entries_.find(key);
    return found == entries_.end() ? nullptr : &found->second;
}

FaceClickDecision FaceZoomToggle::Decide(const std::wstring& key, const FaceIdentity& clicked,
    const std::optional<PaneViewSnapshot>& framedPathNow) const {
    FaceClickDecision decision;
    const Entry* entry = Find(key);
    const bool stillFramed = entry && framedPathNow &&
        SnapshotsEquivalent(*framedPathNow, entry->framed);
    if (!stillFramed) return decision; // Idle (or stale): plain lock-zoom
    if (SameFace(entry->face, clicked)) {
        decision.action = FaceClickAction::Revert;
        decision.revertPath = entry->prePath;
        decision.revertTarget = entry->preZoom;
        return decision;
    }
    decision.keepExistingPreZoom = true;
    return decision;
}

void FaceZoomToggle::CommitLockZoom(const std::wstring& key, const FaceIdentity& face,
    bool keepExistingPreZoom, const std::wstring& prePath, const PaneViewSnapshot& preZoom,
    const std::wstring& framedPath, const PaneViewSnapshot& framed) {
    const auto existing = entries_.find(key);
    Entry entry;
    entry.face = face;
    if (keepExistingPreZoom && existing != entries_.end()) {
        entry.prePath = existing->second.prePath;
        entry.preZoom = existing->second.preZoom;
    } else {
        entry.prePath = prePath;
        entry.preZoom = preZoom;
    }
    entry.framedPath = framedPath;
    entry.framed = framed;
    entries_[key] = std::move(entry);
}

void FaceZoomToggle::Clear(const std::wstring& key) noexcept { entries_.erase(key); }
void FaceZoomToggle::ClearAll() noexcept { entries_.clear(); }

DragThreshold ComputeDragThreshold(int systemDragWidthPx, int systemDragHeightPx,
    float pixelsPerDip, float minimumDip) noexcept {
    const float scale = pixelsPerDip > 0.0f && std::isfinite(pixelsPerDip) ? pixelsPerDip : 1.0f;
    const float minimum = std::isfinite(minimumDip) ? std::max(0.0f, minimumDip) : 0.0f;
    DragThreshold threshold;
    threshold.x = std::max(minimum, static_cast<float>(std::max(0, systemDragWidthPx)) / scale);
    threshold.y = std::max(minimum, static_cast<float>(std::max(0, systemDragHeightPx)) / scale);
    return threshold;
}

void PressGesture::Begin(transform::ViewPoint down, std::uint64_t tickMs,
    DragThreshold threshold) noexcept {
    down_ = down;
    downTick_ = tickMs;
    threshold_ = threshold;
    active_ = true;
    drag_ = false;
}

bool PressGesture::Exceeds(transform::ViewPoint point) const noexcept {
    return std::fabs(point.x - down_.x) > threshold_.x ||
        std::fabs(point.y - down_.y) > threshold_.y;
}

bool PressGesture::Move(transform::ViewPoint point) noexcept {
    if (!active_ || drag_) return false;
    if (!Exceeds(point)) return false;
    drag_ = true;
    return true;
}

bool PressGesture::Release(transform::ViewPoint point, bool releaseInsideTarget) noexcept {
    if (!active_) return false;
    if (!drag_ && Exceeds(point)) drag_ = true; // a fast flick with no intermediate move
    const bool click = !drag_ && releaseInsideTarget;
    active_ = false;
    drag_ = false;
    return click;
}

void PressGesture::Cancel() noexcept {
    active_ = false;
    drag_ = false;
}

void FaceOutlineFade::BeginFrame() noexcept {
    observed_.clear();
    inFrame_ = true;
}

float FaceOutlineFade::Observe(const std::wstring& key, TimePoint now) {
    if (!displayed_.contains(key) && !observed_.contains(key)) Reveal(key, now);
    observed_.insert(key);
    const float progress = Progress(key, now);
    lastPainted_[key] = progress;
    return progress;
}

void FaceOutlineFade::EndFrame() {
    if (!inFrame_) return;
    inFrame_ = false;
    displayed_ = observed_;
    // Forget keys that left the screen so coming back reveals them again.
    for (auto it = fadeStart_.begin(); it != fadeStart_.end();) {
        if (!displayed_.contains(it->first)) it = fadeStart_.erase(it);
        else ++it;
    }
    for (auto it = lastPainted_.begin(); it != lastPainted_.end();) {
        if (!displayed_.contains(it->first)) it = lastPainted_.erase(it);
        else ++it;
    }
}

void FaceOutlineFade::Reveal(const std::wstring& key, TimePoint now) {
    fadeStart_[key] = now + kOutlineHold;
}

void FaceOutlineFade::RevealDisplayed(TimePoint now) {
    for (const std::wstring& key : displayed_) Reveal(key, now);
}

void FaceOutlineFade::SetPinned(bool pinned, TimePoint now) {
    if (pinned_ == pinned) return;
    pinned_ = pinned;
    if (!pinned_) {
        // Hide: start fading right away from fully visible.
        for (const std::wstring& key : displayed_) fadeStart_[key] = now;
    }
}

void FaceOutlineFade::Reset() noexcept {
    fadeStart_.clear();
    lastPainted_.clear();
    displayed_.clear();
    observed_.clear();
    inFrame_ = false; // pinned_ is a persistent user preference and survives Reset()
}

float FaceOutlineFade::Progress(const std::wstring& key, TimePoint now) const noexcept {
    if (pinned_) return 0.0f;
    const auto found = fadeStart_.find(key);
    if (found == fadeStart_.end()) return 1.0f;
    if (now <= found->second) return 0.0f;
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - found->second);
    const float fraction = static_cast<float>(elapsed.count()) /
        static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(kOutlineFade).count());
    return std::clamp(fraction, 0.0f, 1.0f);
}

bool FaceOutlineFade::NeedsRepaint(TimePoint now) const noexcept {
    for (const std::wstring& key : displayed_) {
        const auto painted = lastPainted_.find(key);
        const float last = painted == lastPainted_.end() ? -1.0f : painted->second;
        if (std::fabs(Progress(key, now) - last) > 1e-4f) return true;
    }
    return false;
}

} // namespace quicksift::review::face_interaction
