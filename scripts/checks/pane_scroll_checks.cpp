// OWNER: Portable checks for left-pane (Settings) scrolling: offset clamping on every
// path, child repositioning from the natural layout model (the 0.9.6.0 "clump at
// the top" fix), saturating huge deltas and precision-touchpad accumulation
// (core/pane_scroll_policy.h).
//
//   g++ -std=c++20 -O2 -Isrc scripts/checks/pane_scroll_checks.cpp

#include "core/pane_scroll_policy.h"

#include <array>
#include <climits>
#include <cstdio>

namespace {
int gFailures = 0;
int gChecks = 0;
void Check(bool condition, const char* label) {
    ++gChecks;
    if (!condition) { ++gFailures; std::printf("FAIL: %s\n", label); }
}

namespace pane = quicksift::core::pane;

// Minimal model of RepositionPaneScroll: children at `live` tops, laid out at `natural`.
constexpr int kChildren = 8;
constexpr int kChildHeight = 40;
constexpr int kViewTop = 100;
constexpr int kViewHeight = 200;
struct Child { int natural = 0; int live = 0; bool visible = true; };
struct Pane {
    std::array<Child, kChildren> children{};
    int content = kChildren * kChildHeight;
    int offset = 0;
    Pane() { for (int i = 0; i < kChildren; ++i) children[i] = { kViewTop + i * kChildHeight, kViewTop + i * kChildHeight, true }; }
    void Reposition(bool legacyRule) {
        offset = pane::ClampScroll(offset, content, kViewHeight);
        for (auto& child : children) {
            const int top = pane::ScrolledTop(child.natural, offset);
            const bool hidden = pane::Classify(top, top + kChildHeight, kViewTop, kViewTop + kViewHeight) == pane::Visibility::Hidden;
            if (hidden) { if (child.visible) { child.live = top; child.visible = false; } continue; }
            const bool move = legacyRule ? (offset != 0 || !child.visible) :
                pane::NeedsReposition(0, child.live, 0, top, child.visible);
            if (move) { child.live = top; child.visible = true; }
        }
    }
    void ScrollBy(long long delta, bool legacyRule = false) {
        const int after = pane::ScrollBy(offset, delta, content, kViewHeight);
        if (after == offset) return;
        offset = after;
        Reposition(legacyRule);
    }
    bool Consistent() const {
        for (const auto& child : children) {
            if (child.visible && child.live != child.natural - offset) return false;
        }
        for (int i = 1; i < kChildren; ++i) {
            if (children[i].visible && children[i - 1].visible && children[i].live - children[i - 1].live != kChildHeight)
                return false; // overlapping ("clumped") controls
        }
        return true;
    }
};
} // namespace

int main() {
    // Clamp: [0, max(0, content - viewport)].
    Check(pane::MaxScroll(320, 200) == 120 && pane::MaxScroll(150, 200) == 0 && pane::MaxScroll(500, 0) == 0, "max scroll");
    Check(pane::ScrollBy(0, -48, 320, 200) == 0, "negative wheel delta at the top stays at 0");
    Check(pane::ScrollBy(10, -48, 320, 200) == 0, "scroll past the top lands exactly on 0");
    Check(pane::ScrollBy(100, 48, 320, 200) == 120, "scroll past the bottom lands on max");
    Check(pane::ScrollBy(0, LLONG_MAX / 2, 320, 200) == 120 && pane::ScrollBy(120, -(LLONG_MAX / 2), 320, 200) == 0,
        "huge deltas saturate without overflow");
    Check(pane::ScrollBy(INT_MAX, 1, 320, 200) == 120, "stale huge offset re-clamped");
    Check(pane::ScrollBy(50, 10, 150, 200) == 0, "content shorter than viewport: always 0");
    Check(pane::ClampScroll(120, 320, 280) == 40, "viewport grows while scrolled: offset re-clamped");
    Check(pane::ClampScroll(120, 320, 100) == 120, "viewport shrinks while scrolled: offset kept");
    Check(pane::ClampScroll(240, 480, 300) == 180 && pane::ClampScroll(240, 320, 200) == 120,
        "DPI change (content and viewport rescaled): offset re-clamped");

    // Wheel steps / precision touchpad accumulation.
    Check(pane::WheelStep(120, 16, 3) == -48 && pane::WheelStep(-120, 16, 3) == 48, "one notch");
    Check(pane::WheelStep(INT_MAX, 1 << 12, 100) < 0, "huge wheel delta does not overflow");
    pane::WheelAccumulator wheel{};
    Check(pane::TakeWheelPixels(wheel, 120, 16, 3) == -48 && wheel.remainder == 0, "notch accumulates nothing");
    int pixels = 0;
    for (int i = 0; i < 12; ++i) pixels += pane::TakeWheelPixels(wheel, -10, 16, 3);
    Check(pixels == 48 && wheel.remainder == 0, "twelve 10-unit touchpad deltas equal one notch (no truncation loss)");
    Check(pane::TakeWheelPixels(wheel, -1, 16, 3) == 0 && wheel.remainder == 48, "sub-pixel delta accumulates");
    Check(pane::TakeWheelPixels(wheel, 1, 16, 3) == 0 && wheel.remainder == -48, "direction change drops opposite remainder");
    Check(pane::TakeWheelPixels(wheel, 0, 16, 3) == 0 && pane::TakeWheelPixels(wheel, 120, 0, 3) == 0, "invalid input: no step");
    pane::WheelAccumulator big{};
    Check(pane::TakeWheelPixels(big, -32768, 1 << 15, 1 << 10) == (1 << 30), "huge touchpad burst saturates");

    // Repositioning model: the reported Settings clump.
    {
        Pane legacy;
        legacy.ScrollBy(60, true);
        legacy.ScrollBy(-1000, true); // over-scroll the top
        Check(legacy.offset == 0 && !legacy.Consistent(), "legacy rule reproduces the clump at the top");
    }
    {
        Pane fixed;
        fixed.ScrollBy(60);
        Check(fixed.Consistent(), "scrolled children placed from the natural model");
        fixed.ScrollBy(-1000);
        Check(fixed.offset == 0 && fixed.Consistent(), "over-scroll at the top restores every child to its natural slot");
        fixed.ScrollBy(-48);
        Check(fixed.offset == 0 && fixed.Consistent(), "further negative wheel at the top is a no-op");
        for (int step = 0; step < 20; ++step) fixed.ScrollBy(step % 3 == 0 ? -37 : 23);
        fixed.ScrollBy(-100000);
        Check(fixed.offset == 0 && fixed.Consistent(), "mixed wheel gestures then top: consistent");
        fixed.ScrollBy(100000);
        Check(fixed.offset == 120 && fixed.Consistent(), "bottom clamp consistent");
        fixed.content = 250; fixed.Reposition(false); // content relayout shrinks while scrolled
        Check(fixed.offset == 50 && fixed.Consistent(), "content shrink re-clamps and repositions");
        fixed.content = 150; fixed.Reposition(false);
        Check(fixed.offset == 0 && fixed.Consistent(), "content shorter than viewport resets to natural");
    }
    Check(!pane::NeedsReposition(4, 100, 4, 100, true), "unchanged shown child is not moved (no extra work)");
    Check(pane::NeedsReposition(4, 52, 4, 100, true), "shown child at a stale scrolled position moves even at offset 0");
    Check(pane::NeedsReposition(4, 100, 4, 100, false), "hidden child is re-shown");
    Check(pane::ScrolledTop(300, 120) == 180, "scrolled top derived from natural top");

    std::printf("pane_scroll_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
