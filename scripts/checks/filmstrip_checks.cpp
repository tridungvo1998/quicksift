// OWNER: Deterministic, portable checks for the Single View filmstrip (round 22):
// window centring and clamping (empty, 1-image, ends, fast stepping, filtered-out
// current), band and cell hit-testing edges, reserved image width, toggle
// placement, wheel stepping, decode target and decode admission during view load.
// Round 23: fade target and logical hit-testing, the decode window and the drop
// rule for cells that scrolled out, badge sizing, and the blurry-photo outline
// (theme red contrast, dash pattern).
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Wpedantic -Wshadow -Werror -Isrc
//       scripts/checks/filmstrip_checks.cpp /tmp/qs-build/libQuickSiftCore.a -pthread

#include "core/blur_outline_policy.h"
#include "core/filmstrip_policy.h"
#include "core/priority_policy.h"

#include <cstdio>

namespace {

using namespace quicksift::core;
namespace fs = quicksift::core::filmstrip;

int gFailures = 0;
int gChecks = 0;

void Check(bool condition, const char* label) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL: %s\n", label);
    }
}

constexpr float kCell = fs::kThumbDip + fs::kPaddingDip;
constexpr float HeightFor(int cells) { return fs::kTopDip + kCell * static_cast<float>(cells) + 1.0f; }

void CheckWindow() {
    Check(fs::ComputeWindow(800.0f, 0, 0).Empty(), "empty folder: no window");
    Check(fs::ComputeWindow(800.0f, 10, fs::kNoCurrent).Empty(), "current not visible (filtered/deleted): no window");
    Check(fs::ComputeWindow(800.0f, 10, 10).Empty(), "current past visible count: no window");
    Check(fs::ComputeWindow(fs::kTopDip + kCell - 1.0f, 10, 3).Empty(), "too short for one cell: no window");
    const auto one = fs::ComputeWindow(800.0f, 1, 0);
    Check(!one.Empty() && one.firstIndex == 0 && one.lastIndex == 1, "1-image folder: one cell");
    const auto start = fs::ComputeWindow(HeightFor(5), 100, 0);
    Check(start.maxThumbs == 5 && start.firstIndex == 0 && start.lastIndex == 5, "first image clamps to start");
    const auto middle = fs::ComputeWindow(HeightFor(5), 100, 50);
    Check(middle.firstIndex == 48 && middle.lastIndex == 53, "middle image centred (2 above, 2 below)");
    const auto end = fs::ComputeWindow(HeightFor(5), 100, 99);
    Check(end.firstIndex == 95 && end.lastIndex == 100, "last image clamps to end");
    const auto even = fs::ComputeWindow(HeightFor(4), 100, 50);
    Check(even.firstIndex == 49 && even.lastIndex == 53, "even window: current at (n-1)/2");
    const auto fewer = fs::ComputeWindow(HeightFor(8), 3, 2);
    Check(fewer.firstIndex == 0 && fewer.lastIndex == 3, "fewer images than cells: all shown");
    // Fast stepping: every step keeps the current cell inside the window, and the
    // window moves by at most one per step (no jumps, no recomputation surprises).
    bool contained = true, smooth = true;
    std::size_t previousFirst = 0;
    for (std::size_t i = 0; i < 200; ++i) {
        const auto w = fs::ComputeWindow(HeightFor(7), 200, i);
        contained = contained && w.firstIndex <= i && i < w.lastIndex && w.lastIndex - w.firstIndex == 7;
        if (i > 0) smooth = smooth && w.firstIndex >= previousFirst && w.firstIndex - previousFirst <= 1;
        previousFirst = w.firstIndex;
    }
    Check(contained, "fast stepping: current always inside a full window");
    Check(smooth, "fast stepping: window advances at most one cell per step");
}

void CheckHitTesting() {
    const float width = 1000.0f, height = 700.0f;
    const float left = width - fs::kWidthDip;
    Check(fs::BandContains(left, 0.0f, width, height), "band: left edge, top inclusive");
    Check(fs::BandContains(width, height, width, height), "band: right/bottom edge inclusive");
    Check(!fs::BandContains(left - 0.5f, 100.0f, width, height), "band: left of strip excluded");
    Check(fs::BandContains(left + 5.0f, height - 2.0f, width, height),
        "band: background below the last cell is still the strip (no click-through)");
    Check(fs::BandContains(left + 5.0f, 4.0f, width, height), "band: area above the first cell is the strip");
    const auto w = fs::ComputeWindow(height, 50, 10);
    const float cellLeft = left + (fs::kWidthDip - fs::kThumbDip) * 0.5f;
    const float firstTop = fs::kTopDip + fs::kPaddingDip;
    Check(fs::HitCell(w, cellLeft, firstTop, width) == w.firstIndex, "cell: top-left corner hits first cell");
    Check(fs::HitCell(w, cellLeft + fs::kThumbDip, firstTop + fs::kThumbDip, width) == w.firstIndex,
        "cell: bottom-right corner inclusive");
    Check(fs::HitCell(w, cellLeft, firstTop + fs::kThumbDip + 1.0f, width) == fs::kNoCurrent, "cell: padding gap misses");
    Check(fs::HitCell(w, cellLeft, firstTop + kCell, width) == w.firstIndex + 1, "cell: second cell");
    Check(fs::HitCell(w, cellLeft - 1.0f, firstTop, width) == fs::kNoCurrent, "cell: side margin misses");
    Check(fs::HitCell(fs::Window{}, cellLeft, firstTop, width) == fs::kNoCurrent, "cell: empty window misses");
    const float lastBottom = firstTop + kCell * static_cast<float>(w.lastIndex - w.firstIndex);
    Check(fs::HitCell(w, cellLeft, lastBottom + 2.0f, width) == fs::kNoCurrent, "cell: below last cell misses");
}

void CheckGeometry() {
    Check(fs::ReservedRightDip(true, true) == fs::kWidthDip, "open strip reserves its width in Single");
    Check(fs::ReservedRightDip(true, false) == 0.0f, "closed strip reserves nothing");
    Check(fs::ReservedRightDip(false, true) == 0.0f, "Compare/Grid never reserve");
    const int closed = fs::ToggleLeftPx(1600, 36, 6, 130, false);
    const int open = fs::ToggleLeftPx(1600, 36, 6, 130, true);
    Check(closed == 1600 - 36 - 6, "toggle closed: right edge");
    Check(open + 36 <= 1600 - 130, "toggle open: never overlaps the strip");
    Check(fs::ToggleLeftPx(20, 36, 6, 130, true) == 0, "toggle: clamped on tiny windows");
    // DPI: geometry is in DIPs; at 200% the strip is twice as wide in pixels and the
    // open toggle still clears it.
    Check(fs::ToggleLeftPx(3200, 72, 12, 260, true) + 72 <= 3200 - 260, "toggle open at 200% DPI clears strip");
}

void CheckWheel() {
    int acc = 0;
    Check(fs::TakeWheelSteps(acc, 120) == -1 && acc == 0, "wheel up one notch: previous image");
    Check(fs::TakeWheelSteps(acc, -120) == 1 && acc == 0, "wheel down one notch: next image");
    Check(fs::TakeWheelSteps(acc, -360) == 3, "three notches in one event: three steps");
    acc = 0;
    int steps = 0;
    for (int i = 0; i < 3; ++i) steps += fs::TakeWheelSteps(acc, -40);
    Check(steps == 1 && acc == 0, "precision wheel: 3 x 40 = one step");
    Check(fs::TakeWheelSteps(acc, -60) == 0 && acc == -60, "partial notch accumulates");
    Check(fs::TakeWheelSteps(acc, 60) == 0 && acc == 60, "direction change drops the opposite remainder");
    Check(fs::TakeWheelSteps(acc, 10, 0) == 0, "invalid wheel delta: no steps");
}

void CheckScheduling() {
    for (const int grid : { 128, 180, 256, 384, 512 })
        Check(fs::ThumbnailTarget(grid) == grid, "filmstrip uses the grid thumbnail target (shared cache)");
    Check(fs::ThumbnailTarget(0) == 1, "target never zero");
    Check(fs::ThumbnailDecodeAdmitted(false, 5), "no view load: filmstrip decodes unrestricted");
    Check(fs::ThumbnailDecodeAdmitted(true, 0, priority::kPoolCapDuringViewLoad), "view load: first thumbnail admitted");
    Check(!fs::ThumbnailDecodeAdmitted(true, priority::kPoolCapDuringViewLoad, priority::kPoolCapDuringViewLoad),
        "view load: filmstrip shares the one-thumbnail cap (below the main image)");
    // The general P2 rule is unchanged: other interactive work is always admitted.
    Check(priority::PoolLaneAdmitted(priority::PoolLane::Interactive, true, 99), "P2 interactive rule unchanged");
}


void CheckFade() {
    Check(fs::FadeTarget(true, true), "fade: open in Single fades in");
    Check(!fs::FadeTarget(true, false), "fade: closing fades out");
    Check(!fs::FadeTarget(false, true), "fade: open but not Single is hidden");
    Check(fs::SnapHiddenOnModeExit(false) && !fs::SnapHiddenOnModeExit(true),
        "fade: leaving Single snaps hidden, so the next entry fades in");
    // Hit-testing follows the logical state, never the animated opacity.
    Check(fs::Interactive(true, true, false), "fade-in: strip takes clicks from the first frame");
    Check(!fs::Interactive(true, false, false), "fade-out: strip stops taking clicks at once");
    Check(!fs::Interactive(true, true, true), "empty window never takes clicks");
    Check(fs::ReservedRightDip(true, fs::Interactive(true, false, false)) == 0.0f,
        "fade-out: image area released at once (no per-frame relayout)");
}

void CheckDecodeWindow() {
    const auto w = fs::ComputeWindow(HeightFor(5), 100, 50);
    const auto r = fs::DecodeWindow(w, 100);
    Check(r.first == w.firstIndex - fs::kDecodeMarginCells && r.last == w.lastIndex + fs::kDecodeMarginCells,
        "decode window = visible cells + margin each side");
    const auto start = fs::DecodeWindow(fs::ComputeWindow(HeightFor(5), 100, 0), 100);
    Check(start.first == 0 && start.last == 5 + fs::kDecodeMarginCells, "decode window clamps at the start");
    const auto end = fs::DecodeWindow(fs::ComputeWindow(HeightFor(5), 100, 99), 100);
    Check(end.last == 100 && end.first == 95 - fs::kDecodeMarginCells, "decode window clamps at the end");
    Check(fs::DecodeWindow(fs::Window{}, 100).Empty(), "no window: empty decode window");
    Check(fs::DecodeWindow(fs::ComputeWindow(800.0f, 1, 0), 1) == fs::Range{ 0, 1 }, "1-image folder: one cell");
    // Fast stepping: a cell several steps behind falls out; one step back stays.
    const auto later = fs::DecodeWindow(fs::ComputeWindow(HeightFor(5), 100, 60), 100);
    Check(!later.Contains(50), "stepped 10 ahead: old cells leave the decode window");
    const auto oneStep = fs::DecodeWindow(fs::ComputeWindow(HeightFor(5), 100, 51), 100);
    Check(oneStep.Contains(w.firstIndex), "one step: previous first cell kept by the margin (no churn)");
    // Drop rule.
    Check(fs::DecodeDropped(true, true, false, false), "filmstrip job out of window: dropped");
    Check(!fs::DecodeDropped(true, true, true, false), "filmstrip job in window: kept");
    Check(fs::DecodeDropped(true, false, true, false), "strip closed / left Single: dropped");
    Check(!fs::DecodeDropped(true, true, false, true), "grid still wants it: kept (shared key)");
    Check(!fs::DecodeDropped(false, false, false, false), "grid / main-image jobs never dropped");
}

void CheckBadges() {
    Check(fs::BadgeHeightDip(fs::kThumbDip) == fs::kBadgeHeightDip, "100 DIP cell: grid badge height");
    Check(fs::BadgeHeightDip(60.0f) < fs::kBadgeHeightDip && fs::BadgeHeightDip(60.0f) >= fs::kBadgeMinHeightDip,
        "small cell: badges scale down");
    Check(fs::BadgeHeightDip(20.0f) == fs::kBadgeMinHeightDip, "tiny cell: minimum readable height");
    Check(fs::BadgeHeightDip(400.0f) == fs::kBadgeHeightDip, "large cell: never taller than the grid's");
    Check(fs::kBadgeInsetDip * 2.0f + fs::kBadgeHeightDip < fs::kThumbDip, "badges fit inside the cell");
}

void CheckBlurOutline() {
    namespace blur = quicksift::core::blur_outline;
    // Card surfaces (ui_design_system.cpp GetThemePalette: panel / panelStrong).
    constexpr blur::Rgb lightPanel{ 242, 247, 251 }, lightPanelStrong{ 252, 254, 255 };
    constexpr blur::Rgb darkPanel{ 29, 37, 48 }, darkPanelStrong{ 42, 52, 66 };
    Check(blur::Contrast(blur::Color(false), lightPanel) >= 4.5, "light red >= 4.5:1 on the light card");
    Check(blur::Contrast(blur::Color(false), lightPanelStrong) >= 4.5, "light red >= 4.5:1 on the light strip");
    Check(blur::Contrast(blur::Color(true), darkPanel) >= 3.0, "dark red >= 3:1 on the dark card");
    Check(blur::Contrast(blur::Color(true), darkPanelStrong) >= 3.0, "dark red >= 3:1 on the dark strip");
    for (const bool dark : { false, true }) {
        const auto c = blur::Color(dark);
        Check(c.r > c.g + 100 && c.r > c.b + 100, "outline colour reads as red");
    }
    Check(blur::DashLengthDip(blur::kStrokeDip) >= 4.0f && blur::GapLengthDip(blur::kStrokeDip) >= 3.0f,
        "dash and gap visible at 100%");
    Check(blur::DashLengthDip(2.0f * blur::kStrokeDip) == 2.0f * blur::DashLengthDip(blur::kStrokeDip),
        "dash scales with the (DIP) stroke width");
    Check(blur::kDashOn > blur::kDashOff, "dash longer than the gap (reads as an outline)");
}

} // namespace

int main() {
    CheckWindow();
    CheckHitTesting();
    CheckGeometry();
    CheckWheel();
    CheckScheduling();
    CheckFade();
    CheckDecodeWindow();
    CheckBadges();
    CheckBlurOutline();
    std::printf("filmstrip_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
