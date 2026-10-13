// OWNER: Deterministic, portable checks for the FaceLock interaction policy
// (click-to-zoom toggle state machine, click-vs-drag gesture, outline fade).
//
// Not a CTest suite (CTest stays retired). Time is injected, never read. Build:
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Isrc scripts/checks/face_interaction_checks.cpp
//       src/review/face_interaction_policy.cpp src/review/view_transform_policy.cpp
//       -o /tmp/face_interaction_checks && /tmp/face_interaction_checks
//
//   cl /std:c++20 /EHsc /W4 /utf-8 /Isrc scripts\checks\face_interaction_checks.cpp
//       src\review\face_interaction_policy.cpp src\review\view_transform_policy.cpp

#include "review/face_interaction_policy.h"
#include "review/view_transform_policy.h"

#include <chrono>
#include <cstdio>
#include <string>

namespace {

using namespace quicksift::review::face_interaction;
namespace transform = quicksift::review::transform;
using quicksift::core::ZoomMode;

int gFailures = 0;
int gChecks = 0;

void Check(bool condition, const char* what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL: %s\n", what);
    }
}

PaneViewSnapshot Snapshot(float zoom, float cx, float cy, ZoomMode mode = ZoomMode::Custom,
    int rotation = 0, transform::ViewRect area = { 8.0f, 8.0f, 808.0f, 608.0f }) {
    PaneViewSnapshot snapshot;
    snapshot.view.zoom = zoom;
    snapshot.view.centerX = cx;
    snapshot.view.centerY = cy;
    snapshot.view.mode = mode;
    snapshot.view.rotation = rotation;
    snapshot.area = area;
    return snapshot;
}

FaceIdentity Face(const wchar_t* path, float left, float top) {
    return { path, { left, top, left + 0.1f, top + 0.12f } };
}

void CheckSnapshots() {
    const PaneViewSnapshot base = Snapshot(2.0f, 0.4f, 0.5f);
    Check(SnapshotsEquivalent(base, Snapshot(2.0f + 1e-5f, 0.4f + 1e-5f, 0.5f)), "float noise is equivalent");
    Check(!SnapshotsEquivalent(base, Snapshot(2.2f, 0.4f, 0.5f)), "manual zoom detected");
    Check(!SnapshotsEquivalent(base, Snapshot(2.0f, 0.45f, 0.5f)), "manual pan detected");
    Check(!SnapshotsEquivalent(base, Snapshot(2.0f, 0.4f, 0.5f, ZoomMode::Custom, 90)), "rotation detected");
    Check(SnapshotsEquivalent(base, Snapshot(2.0f, 0.4f, 0.5f, ZoomMode::Custom, 360)), "rotation modulo 360");
    Check(!SnapshotsEquivalent(base, Snapshot(2.0f, 0.4f, 0.5f, ZoomMode::Fit)), "mode change detected");
    Check(!SnapshotsEquivalent(base, Snapshot(2.0f, 0.4f, 0.5f, ZoomMode::Custom, 0,
        { 8.0f, 8.0f, 708.0f, 608.0f })), "pane resize detected");
}

void CheckToggle() {
    FaceZoomToggle toggle;
    const std::wstring key = L"C:/a.jpg";
    const FaceIdentity faceA = Face(L"C:/a.jpg", 0.2f, 0.2f);
    const FaceIdentity faceB = Face(L"C:/a.jpg", 0.6f, 0.3f);
    const PaneViewSnapshot pre = Snapshot(1.0f, 0.5f, 0.5f, ZoomMode::Fit);
    const PaneViewSnapshot framedA = Snapshot(4.0f, 0.25f, 0.26f);
    const PaneViewSnapshot framedB = Snapshot(4.2f, 0.65f, 0.36f);

    // Idle: first click lock-zooms.
    FaceClickDecision d = toggle.Decide(key, faceA, std::nullopt);
    Check(d.action == FaceClickAction::LockZoom && !d.keepExistingPreZoom, "idle click lock-zooms");
    toggle.CommitLockZoom(key, faceA, d.keepExistingPreZoom, key, pre, key, framedA);
    Check(toggle.Size() == 1, "lock-zoom recorded");

    // Same face, view unchanged: revert to the pre-zoom view.
    d = toggle.Decide(key, faceA, framedA);
    Check(d.action == FaceClickAction::Revert, "second click on same face reverts");
    Check(d.revertPath == key && SnapshotsEquivalent(d.revertTarget, pre), "revert target is the pre-zoom view");
    toggle.Clear(key);
    Check(toggle.Find(key) == nullptr, "revert returns to idle");

    // Different face while zoomed keeps the ORIGINAL pre-zoom view.
    toggle.CommitLockZoom(key, faceA, false, key, pre, key, framedA);
    d = toggle.Decide(key, faceB, framedA);
    Check(d.action == FaceClickAction::LockZoom && d.keepExistingPreZoom, "other face while zoomed re-frames, keeps pre");
    toggle.CommitLockZoom(key, faceB, d.keepExistingPreZoom, key, framedA /*ignored*/, key, framedB);
    d = toggle.Decide(key, faceB, framedB);
    Check(d.action == FaceClickAction::Revert && SnapshotsEquivalent(d.revertTarget, pre),
        "reverting after A->B returns to the view before A");
    toggle.ClearAll();

    // Manual change (zoom/pan/rotate/resize) invalidates: the next face click lock-zooms again.
    toggle.CommitLockZoom(key, faceA, false, key, pre, key, framedA);
    d = toggle.Decide(key, faceA, Snapshot(4.0f * 1.2f, 0.25f, 0.26f));
    Check(d.action == FaceClickAction::LockZoom && !d.keepExistingPreZoom, "manual zoom clears zoomed-to-face");
    d = toggle.Decide(key, faceA, Snapshot(4.0f, 0.30f, 0.26f));
    Check(d.action == FaceClickAction::LockZoom && !d.keepExistingPreZoom, "manual pan clears zoomed-to-face");
    d = toggle.Decide(key, faceA, std::nullopt);
    Check(d.action == FaceClickAction::LockZoom && !d.keepExistingPreZoom, "navigation away clears zoomed-to-face");
    // A stale entry is replaced by the fresh pre-zoom view on commit.
    const PaneViewSnapshot manual = Snapshot(3.0f, 0.3f, 0.3f);
    toggle.CommitLockZoom(key, faceA, d.keepExistingPreZoom, key, manual, key, framedA);
    d = toggle.Decide(key, faceA, framedA);
    Check(d.action == FaceClickAction::Revert && SnapshotsEquivalent(d.revertTarget, manual),
        "after a manual change, revert target is the view right before the new lock-zoom");
    toggle.ClearAll();

    // Compare: independent keys per pane (Sync off) never interfere.
    const std::wstring keyB = L"C:/b.jpg";
    const FaceIdentity faceOnB = Face(L"C:/b.jpg", 0.4f, 0.4f);
    toggle.CommitLockZoom(key, faceA, false, key, pre, key, framedA);
    d = toggle.Decide(keyB, faceOnB, std::nullopt);
    Check(d.action == FaceClickAction::LockZoom && !d.keepExistingPreZoom, "Sync off: other pane is independent");
    toggle.CommitLockZoom(keyB, faceOnB, false, keyB, pre, keyB, framedB);
    Check(toggle.Decide(key, faceA, framedA).action == FaceClickAction::Revert, "pane A still reverts independently");
    toggle.ClearAll();

    // Compare with Sync on: one shared key; a face in another pane re-frames and keeps pre.
    const std::wstring shared = L"\x1F" L"sync";
    toggle.CommitLockZoom(shared, faceA, false, key, pre, key, framedA);
    d = toggle.Decide(shared, faceOnB, framedA);
    Check(d.action == FaceClickAction::LockZoom && d.keepExistingPreZoom, "Sync on: face in other pane keeps original pre");
    toggle.CommitLockZoom(shared, faceOnB, true, keyB, framedB, keyB, framedB);
    const auto* entry = toggle.Find(shared);
    Check(entry && entry->prePath == key && SnapshotsEquivalent(entry->preZoom, pre),
        "Sync on: revert target stays the first pane's pre-zoom view");
    d = toggle.Decide(shared, faceOnB, framedB);
    Check(d.action == FaceClickAction::Revert && d.revertPath == key, "Sync on: revert restores the shared view via the first pane");

    // Face identity is box based (index independent) and path specific.
    Check(SameFace(faceA, Face(L"C:/a.jpg", 0.2f + 1e-5f, 0.2f)), "same face within tolerance");
    Check(!SameFace(faceA, Face(L"C:/b.jpg", 0.2f, 0.2f)), "same box on another photo is another face");
}

void CheckGesture() {
    const DragThreshold threshold = ComputeDragThreshold(4, 4, 1.0f, 4.0f);
    Check(threshold.x == 4.0f && threshold.y == 4.0f, "100%: 4 px system rect -> 4 DIP");
    const DragThreshold hiDpi = ComputeDragThreshold(8, 8, 2.0f, 4.0f);
    Check(hiDpi.x == 4.0f && hiDpi.y == 4.0f, "200%: 8 px -> 4 DIP");
    const DragThreshold big = ComputeDragThreshold(20, 12, 1.25f, 4.0f);
    Check(big.x == 16.0f && big.y == 9.6f, "larger system drag rect wins per axis");
    const DragThreshold tiny = ComputeDragThreshold(1, 1, 1.0f, 4.0f);
    Check(tiny.x == 4.0f && tiny.y == 4.0f, "4 DIP floor");
    Check(ComputeDragThreshold(4, 4, 0.0f, 4.0f).x == 4.0f, "invalid scale guarded");

    PressGesture press;
    press.Begin({ 100.0f, 100.0f }, 1000, threshold);
    Check(!press.Move({ 103.0f, 97.0f }), "jitter inside the rect is not a drag");
    Check(press.Release({ 103.0f, 97.0f }, true), "release inside target after jitter is a click");
    Check(!press.Active(), "release ends the press");

    press.Begin({ 100.0f, 100.0f }, 1000, threshold);
    Check(press.Move({ 105.0f, 100.0f }), "leaving the rect converts to a drag exactly once");
    Check(!press.Move({ 110.0f, 100.0f }), "drag conversion reported once");
    Check(!press.Release({ 100.0f, 100.0f }, true), "drag back to origin is never a click");

    press.Begin({ 100.0f, 100.0f }, 1000, threshold);
    Check(!press.Release({ 101.0f, 101.0f }, false), "release outside the hit box is not a click");

    press.Begin({ 100.0f, 100.0f }, 1000, threshold);
    Check(!press.Release({ 140.0f, 100.0f }, true), "fast flick without moves is a drag, not a click");

    press.Begin({ 100.0f, 100.0f }, 1000, threshold);
    press.Cancel(); // WM_CAPTURECHANGED / Escape
    Check(!press.Active() && !press.Release({ 100.0f, 100.0f }, true), "cancelled press never clicks");
    Check(!press.Release({ 100.0f, 100.0f }, true), "release without press (second click of a double-click) is ignored");
}

void CheckFade() {
    using Clock = FaceOutlineFade::Clock;
    const Clock::time_point t0{};
    const auto ms = [](int value) { return std::chrono::milliseconds(value); };
    FaceOutlineFade fade;
    const std::wstring a = L"C:/a.jpg";
    const std::wstring b = L"C:/b.jpg";

    fade.BeginFrame();
    Check(fade.Observe(a, t0) == 0.0f, "newly displayed pane is revealed");
    fade.EndFrame();
    Check(!fade.NeedsRepaint(t0 + ms(500)), "hold period needs no repaint");
    Check(fade.Progress(a, t0 + ms(999)) == 0.0f, "fully visible for 1 s");
    Check(fade.NeedsRepaint(t0 + ms(1050)), "fade frames requested after the hold");
    const float mid = fade.Progress(a, t0 + ms(1125));
    Check(mid > 0.45f && mid < 0.55f, "halfway through the 250 ms fade");
    Check(fade.Progress(a, t0 + ms(1250)) == 1.0f, "fully faded at 1.25 s");
    fade.BeginFrame();
    Check(fade.Observe(a, t0 + ms(1300)) == 1.0f, "still displayed: stays faded (no re-reveal)");
    fade.EndFrame();
    Check(!fade.NeedsRepaint(t0 + ms(5000)), "no repaint once faded");

    // Photo change: a new key reveals; the old key is forgotten and re-reveals on return.
    fade.BeginFrame();
    Check(fade.Observe(b, t0 + ms(6000)) == 0.0f, "photo change reveals new photo");
    fade.EndFrame();
    fade.BeginFrame();
    Check(fade.Observe(a, t0 + ms(9000)) == 0.0f, "returning to a photo reveals it again");
    fade.EndFrame();

    // Explicit reveal (target face changed / lock changed).
    fade.Reveal(a, t0 + ms(20000));
    Check(fade.Progress(a, t0 + ms(20500)) == 0.0f, "explicit reveal re-arms the hold");

    // Pin (right-click toggle on) keeps outlines visible; unpin fades immediately.
    fade.BeginFrame();
    Check(fade.Observe(a, t0 + ms(29000)) == 1.0f, "faded before pinning");
    fade.EndFrame();
    fade.SetPinned(true, t0 + ms(30000));
    Check(fade.Progress(a, t0 + ms(60000)) == 0.0f, "pinned outlines stay visible");
    Check(fade.NeedsRepaint(t0 + ms(30001)), "pinning repaints faded outlines");
    fade.BeginFrame();
    static_cast<void>(fade.Observe(a, t0 + ms(30001)));
    fade.EndFrame();
    fade.SetPinned(false, t0 + ms(61000));
    Check(fade.Progress(a, t0 + ms(61000)) == 0.0f && fade.Progress(a, t0 + ms(61250)) == 1.0f,
        "unpinning fades out over 250 ms starting now");

    // A frame that never reaches EndFrame (device loss) leaves state intact.
    fade.BeginFrame();
    static_cast<void>(fade.Observe(b, t0 + ms(70000)));
    fade.BeginFrame();
    fade.EndFrame();
    Check(fade.Progress(b, t0 + ms(70000)) == 1.0f, "keys absent from the completed frame are forgotten");
    fade.SetPinned(true, t0 + ms(80000));
    fade.Reset();
    Check(fade.Pinned() && !fade.NeedsRepaint(t0), "reset clears per-path state but keeps the pin preference");
    fade.SetPinned(false, t0 + ms(80001));
    Check(!fade.Pinned() && !fade.NeedsRepaint(t0 + ms(80001)), "unpinning with no displayed paths requests nothing");
}

} // namespace

int main() {
    CheckSnapshots();
    CheckToggle();
    CheckGesture();
    CheckFade();
    std::printf("face_interaction_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
