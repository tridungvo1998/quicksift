// OWNER: Deterministic, portable checks for the design tokens (docs/DESIGN_SYSTEM.md):
// radius/spacing scales, concentric and capsule radii, easing end points, duration
// based animation steps (exact at the target, instant when animations are off), the
// glass material under accessibility settings, and the toast fade timing.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Wpedantic -Wshadow -Werror -Isrc
//       scripts/checks/design_tokens_checks.cpp /tmp/qs-build/libQuickSiftCore.a -pthread

#include "ui/framework/ui_models.h"
#include "ui/ui_design_tokens.h"
#include "ui/ui_layout_metrics.h"

#include <chrono>
#include <cmath>
#include <cstdio>

namespace {

using namespace quicksift::ui;
using namespace std::chrono_literals;

int gFailures = 0;
int gChecks = 0;

void Check(bool condition, const char* label) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL: %s\n", label);
    }
}

bool Near(float a, float b, float epsilon = 1.0e-4f) { return std::fabs(a - b) <= epsilon; }

void CheckScales() {
    static_assert(tokens::kRadiusSmall < tokens::kRadiusMedium &&
        tokens::kRadiusMedium < tokens::kRadiusLarge && tokens::kRadiusLarge < tokens::kRadiusXLarge);
    static_assert(tokens::kSpace1 == 4 && tokens::kSpace2 == 8 && tokens::kSpace3 == 12 &&
        tokens::kSpace4 == 16 && tokens::kSpace5 == 24);
    static_assert(tokens::kMotionFastMs == 120 && tokens::kMotionStandardMs == 180 &&
        tokens::kMotionSlowMs == 250);
    Check(tokens::ConcentricRadius(20, 10) == 10, "concentric: pane 20 inset 10 -> content 10");
    Check(tokens::ConcentricRadius(14, 4) == 10, "concentric: card 14 inset 4 -> image well 10");
    Check(tokens::ConcentricRadius(6, 10) == 2, "concentric radius never collapses below 2");
    Check(Near(tokens::CapsuleRadius(34.0f), 17.0f), "capsule = half height");
    Check(Near(tokens::CapsuleRadius(-3.0f), 0.0f), "capsule of empty rect is 0");
    // Component metrics map onto the token scale (no one-off radii).
    Check(layout::kButtonRadiusDip == tokens::kRadiusMedium, "buttons use the medium radius");
    Check(layout::kPanelRadiusDip == tokens::kRadiusXLarge, "canvas uses the xlarge radius");
    Check(layout::kStatusRadiusDip == layout::kPanelRadiusDip, "status matches the canvas radius");
    Check(layout::kCardRadiusDip == tokens::kRadiusLarge, "grid + compare cards share one radius");
    Check(layout::kLeftPaneOpenRadiusDip == layout::kPanelRadiusDip, "open pane matches the canvas");
    Check(tokens::ConcentricRadius(layout::kLeftPaneOpenRadiusDip, 10) == layout::kLeftPaneContentRadiusDip,
        "pane content is concentric with the open pane");
}

void CheckEasing() {
    Check(Near(tokens::EaseOutCubic(0.0f), 0.0f) && Near(tokens::EaseOutCubic(1.0f), 1.0f),
        "ease-out cubic hits both end points exactly");
    Check(Near(tokens::EaseInOutCubic(0.0f), 0.0f) && Near(tokens::EaseInOutCubic(1.0f), 1.0f) &&
        Near(tokens::EaseInOutCubic(0.5f), 0.5f), "ease-in-out cubic is symmetric and exact");
    Check(Near(tokens::EaseOutCubic(2.0f), 1.0f) && Near(tokens::EaseOutCubic(-1.0f), 0.0f),
        "easing clamps out-of-range progress");
    Check(tokens::EaseOutCubic(0.5f) > 0.5f, "ease-out decelerates (ahead of linear at mid-point)");
    float previous = 0.0f;
    bool monotonic = true;
    for (int i = 1; i <= 100; ++i) {
        const float value = tokens::EaseOutCubic(static_cast<float>(i) / 100.0f);
        monotonic = monotonic && value >= previous;
        previous = value;
    }
    Check(monotonic, "ease-out cubic is monotonic");
}

void CheckSteps() {
    const tokens::VisualEffects on{};
    tokens::VisualEffects off{};
    off.animations = false;
    // Stepping at 60 fps reaches the target exactly (no float residue) and in time.
    float value = 0.0f;
    int frames = 0;
    while (value != 1.0f && frames < 1000) {
        value = tokens::StepToward(value, 1.0f, 1.0f / 60.0f, tokens::kMotionStandardMs, on);
        ++frames;
    }
    Check(value == 1.0f, "step reaches the exact target");
    Check(frames >= 10 && frames <= 12, "180 ms at 60 fps takes ~11 frames");
    value = 1.0f;
    for (int i = 0; i < 100 && value != 0.0f; ++i)
        value = tokens::StepToward(value, 0.0f, 0.033f, tokens::kMotionFastMs, on);
    Check(value == 0.0f, "reverse step reaches exactly 0");
    Check(tokens::StepToward(0.25f, 1.0f, 0.001f, tokens::kMotionSlowMs, off) == 1.0f,
        "animations off: snap to the target");
    Check(tokens::StepToward(0.25f, 1.0f, 0.0f, tokens::kMotionSlowMs, on) == 0.25f,
        "zero delta does not move");
    Check(tokens::StepToward(0.5f, 0.5f, 0.1f, tokens::kMotionFastMs, on) == 0.5f,
        "at target stays at target");
}

void CheckGlass() {
    tokens::VisualEffects normal{};
    tokens::VisualEffects highContrast{};
    highContrast.highContrast = true;
    tokens::VisualEffects opaque{};
    opaque.transparency = false;
    Check(normal.Glass() && !highContrast.Glass() && !opaque.Glass(), "glass gating");
    Check(normal != highContrast && normal == tokens::VisualEffects{}, "effect equality");
    for (const bool dark : { false, true }) {
        const tokens::GlassMaterial glass = tokens::Glass(dark, normal);
        Check(glass.fillAlpha > 0.9f && glass.fillAlpha < 1.0f, "glass body is translucent but legible");
        Check(glass.highlightAlpha > 0.0f && glass.sheenAlpha > 0.0f, "glass has a specular edge and sheen");
        for (const tokens::VisualEffects& effects : { highContrast, opaque }) {
            const tokens::GlassMaterial flat = tokens::Glass(dark, effects);
            Check(flat.fillAlpha == 1.0f, "reduced transparency / HC: opaque body");
            Check(flat.highlightAlpha == 0.0f && flat.sheenAlpha == 0.0f, "no specular effects");
            Check(flat.borderAlpha == 1.0f, "crisp border");
            Check(flat.ambientShadowAlpha == 0.0f, "no soft ambient shadow");
        }
    }
}

void CheckToastFade() {
    using Clock = framework::ToastPresenter::Clock;
    framework::ToastPresenter toast(2000ms);
    const Clock::time_point start = Clock::now();
    toast.Show(L"Saved", start);
    Check(Near(toast.Opacity(start), 0.0f), "toast starts transparent");
    Check(Near(toast.Opacity(start + std::chrono::milliseconds(tokens::kMotionFastMs)), 1.0f),
        "toast fully in after the fast duration");
    Check(toast.Opacity(start + 60ms) > 0.5f, "toast fade-in eases out");
    Check(Near(toast.Opacity(start + 1000ms), 1.0f), "toast holds");
    const float fading = toast.Opacity(start + 2000ms - std::chrono::milliseconds(tokens::kMotionSlowMs / 2));
    Check(fading > 0.4f && fading < 0.6f, "toast fades out over the slow duration");
    toast.SetAnimationsEnabled(false);
    Check(Near(toast.Opacity(start + 1ms), 1.0f) && Near(toast.Opacity(start + 1990ms), 1.0f),
        "animations off: toast is opaque for its whole life");
}

} // namespace

int main() {
    CheckScales();
    CheckEasing();
    CheckSteps();
    CheckGlass();
    CheckToastFade();
    std::printf("design_tokens_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
