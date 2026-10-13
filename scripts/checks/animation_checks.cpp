// OWNER: Deterministic, portable checks for the central animation system
// (src/ui/framework/ui_animation.h, docs/DESIGN_SYSTEM.md "Animation system"):
// easing end points/monotonicity, duration tokens in the presets, interruption and
// reversal continuity, proportional durations, reduced-motion and suspended instant
// paths, completion ordering/cancellation, batched dirty surfaces, transient entries,
// activation hook and teardown safety.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Wpedantic -Wshadow -Werror -Isrc
//       scripts/checks/animation_checks.cpp /tmp/qs-build/libQuickSiftCore.a -pthread

#include "ui/framework/ui_animation.h"
#include "ui/ui_design_tokens.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace quicksift::ui;
using namespace quicksift::ui::animation;
using namespace std::chrono_literals;
using Clock = Animator::Clock;

int gFailures = 0;
int gChecks = 0;

void Check(bool condition, const char* label) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL: %s\n", label);
    }
}

bool Near(float a, float b, float epsilon = 1.0e-3f) { return std::fabs(a - b) <= epsilon; }

constexpr ElementId kA = MakeId(Space::Chrome, 100);
constexpr ElementId kB = MakeId(Space::Chrome, 101);

void CheckEasing() {
    const Curve curves[] = { Curve::Linear, Curve::EaseOutCubic, Curve::EaseInOutCubic,
        Curve::Smoothstep, Curve::SpringLite };
    for (Curve curve : curves) {
        Check(Ease(curve, 0.0f) == 0.0f, "curve starts exactly at 0");
        Check(Ease(curve, 1.0f) == 1.0f, "curve ends exactly at 1");
        Check(Ease(curve, -1.0f) == 0.0f && Ease(curve, 2.0f) == 1.0f, "curve clamps outside [0,1]");
        bool monotonic = true;
        bool bounded = true;
        float previous = 0.0f;
        for (int i = 1; i <= 200; ++i) {
            const float value = Ease(curve, static_cast<float>(i) / 200.0f);
            if (value + 1.0e-6f < previous) monotonic = false;
            if (value < 0.0f || value > 1.0f) bounded = false;
            previous = value;
        }
        Check(monotonic, "curve is monotonic (no overshoot / no jump back)");
        Check(bounded, "curve stays inside [0,1]");
    }
    Check(Ease(Curve::EaseOutCubic, 0.5f) > 0.8f, "ease-out decelerates (front-loaded)");
    Check(Near(Ease(Curve::EaseInOutCubic, 0.5f), 0.5f) && Near(Ease(Curve::Smoothstep, 0.5f), 0.5f),
        "symmetric curves pass through the midpoint");
    Check(Ease(Curve::SpringLite, 0.5f) > 0.8f && Ease(Curve::SpringLite, 0.9f) > 0.99f,
        "spring-lite settles well before the end");
}

void CheckPresets() {
    static_assert(tokens::kMotionFastMs == 120 && tokens::kMotionStandardMs == 180 &&
        tokens::kMotionSlowMs == 250);
    constexpr Preset fade = presets::Fade();
    static_assert(fade.inMs == tokens::kMotionFastMs && fade.outMs == tokens::kMotionFastMs);
    constexpr Preset slide = presets::SlideFade(Direction::Up, 8.0f);
    static_assert(slide.inMs == tokens::kMotionStandardMs && slide.outMs == tokens::kMotionSlowMs);
    constexpr Preset drop = presets::DropDownReveal(false);
    static_assert(drop.inMs == tokens::kMotionFastMs && drop.fromReveal < 1.0f && drop.offsetDip > 0.0f);
    constexpr Preset pop = presets::PopScale();
    static_assert(pop.inMs == tokens::kMotionStandardMs && pop.fromScale == 0.96f);
    constexpr Preset toggle = presets::Toggle(tokens::kMotionStandardMs);
    static_assert(toggle.inCurve == Curve::Linear && toggle.outCurve == Curve::Linear);
    Check(true, "preset durations come from the motion tokens");

    const Appearance hidden = Resolve(pop, 0.0f);
    const Appearance shown = Resolve(pop, 1.0f);
    Check(Near(hidden.opacity, 0.0f) && Near(hidden.scale, 0.96f), "PopScale starts at 0.96 and transparent");
    Check(Near(shown.opacity, 1.0f) && Near(shown.scale, 1.0f) && shown.AtRest(), "PopScale rests at 1");
    const Appearance up = Resolve(slide, 0.0f);
    Check(Near(up.offsetY, 8.0f) && Near(up.offsetX, 0.0f), "SlideFade Up starts below its rest position");
    const Appearance down = Resolve(presets::SlideFade(Direction::Down, 6.0f), 0.0f);
    Check(Near(down.offsetY, -6.0f), "SlideFade Down starts above its rest position");
    const Appearance reveal = Resolve(drop, 0.0f);
    Check(Near(reveal.reveal, drop.fromReveal) && Near(Resolve(drop, 1.0f).reveal, 1.0f),
        "DropDownReveal clips from fromReveal to fully revealed");
    Check(Near(Resolve(drop, 1.0f).offsetY, 0.0f), "DropDownReveal rests at its anchor position");
}

void CheckShowHideTiming() {
    Animator animator;
    const auto t0 = Clock::now();
    Check(animator.Show(kA, presets::Fade(), surface::kCanvas, t0), "Show starts an animation");
    Check(animator.AnyActive() && animator.IsAnimating(kA), "animator reports activity");
    Check(Near(animator.Value(kA), 0.0f), "new Show starts hidden");
    animator.Tick(t0 + 60ms);
    const float mid = animator.Value(kA);
    Check(mid > 0.5f && mid < 1.0f, "ease-out is past halfway at half time");
    const auto result = animator.Tick(t0 + std::chrono::milliseconds(tokens::kMotionFastMs));
    Check(animator.Value(kA) == 1.0f, "value lands exactly on the target");
    Check(!animator.AnyActive() && !result.active, "animator idles after the duration");
    Check((result.dirty & surface::kCanvas) != 0, "landing frame marks its surface dirty");
    const auto idle = animator.Tick(t0 + 500ms);
    Check(idle.dirty == surface::kNone, "idle ticks report no dirty surface");
    Check(!animator.Show(kA, presets::Fade(), surface::kCanvas, t0 + 500ms), "Show at rest is a no-op");
}

void CheckReversalContinuity() {
    Animator animator;
    const auto t0 = Clock::now();
    animator.Show(kA, presets::Fade(), surface::kCanvas, t0);
    animator.Tick(t0 + 40ms);
    const float before = animator.Value(kA);
    animator.Hide(kA, presets::Fade(), surface::kCanvas, t0 + 40ms);
    Check(Near(animator.Value(kA), before, 0.0f), "reversal keeps the current value (no jump)");
    animator.Tick(t0 + 41ms);
    const float after = animator.Value(kA);
    Check(after <= before && before - after < 0.1f, "reversal moves smoothly from the current value");
    // Proportional: the remaining trip back takes only its share of the duration.
    const auto share = std::chrono::microseconds(static_cast<long long>(
        std::ceil(before * static_cast<float>(tokens::kMotionFastMs) * 1000.0f)));
    animator.Tick(t0 + 40ms + share + 1ms);
    Check(animator.Value(kA) == 0.0f && !animator.AnyActive(), "a partial reversal takes a proportional time");

    // Toggle channel: constant speed, re-targeting repeatedly never jumps.
    Animator channel;
    Animator::Options options{};
    options.durationMs = tokens::kMotionStandardMs;
    options.curve = Curve::Linear;
    channel.AnimateTo(kB, 0.0f, options, t0, {}, 0.0f);
    channel.AnimateTo(kB, 1.0f, options, t0);
    float previous = 0.0f;
    bool continuous = true;
    for (int frame = 1; frame <= 30; ++frame) {
        const auto now = t0 + std::chrono::milliseconds(frame * 16);
        if (frame == 6) channel.AnimateTo(kB, 0.0f, options, now);
        if (frame == 9) channel.AnimateTo(kB, 1.0f, options, now);
        channel.Tick(now);
        const float value = channel.Value(kB);
        if (std::fabs(value - previous) > 16.0f / static_cast<float>(tokens::kMotionStandardMs) + 1.0e-3f)
            continuous = false;
        previous = value;
    }
    Check(continuous, "toggle channel never moves more than one frame's step");
    Check(channel.Value(kB) == 1.0f, "toggle channel lands exactly on its last target");
}

void CheckReducedMotion() {
    Animator animator;
    animator.SetEnabled(false);
    const auto t0 = Clock::now();
    int calls = 0;
    Check(!animator.Show(kA, presets::PopScale(), surface::kCanvas, t0, [&] { ++calls; }),
        "reduced motion: Show is instant");
    Check(animator.Value(kA) == 1.0f && !animator.AnyActive(), "reduced motion: end state at once");
    Check(calls == 1, "reduced motion: completion runs immediately");
    const auto result = animator.Tick(t0);
    Check((result.dirty & surface::kCanvas) != 0, "instant change still requests one repaint");
    animator.Hide(kA, presets::PopScale(), surface::kCanvas, t0, [&] { ++calls; });
    Check(animator.Value(kA) == 0.0f && calls == 2, "reduced motion: Hide is instant with completion");

    // Disabling mid-flight finishes on the next tick.
    Animator flight;
    flight.Show(kB, presets::Fade(), surface::kCanvas, t0);
    flight.SetEnabled(false);
    flight.Tick(t0 + 1ms);
    Check(flight.Value(kB) == 1.0f && !flight.AnyActive(), "disabling mid-flight snaps to the target");

    // Suspended (minimized): same instant behaviour, re-enabled afterwards.
    Animator suspended;
    suspended.SetSuspended(true);
    Check(!suspended.Show(kA, presets::Fade(), surface::kCanvas, t0), "suspended: Show is instant");
    suspended.SetSuspended(false);
    Check(suspended.Hide(kA, presets::Fade(), surface::kCanvas, t0), "resumed: animations run again");
}

void CheckCompletions() {
    Animator animator;
    const auto t0 = Clock::now();
    std::vector<std::string> order;
    animator.Show(kA, presets::Fade(tokens::kMotionStandardMs), surface::kCanvas, t0, [&] { order.push_back("A"); });
    animator.Show(kB, presets::Fade(tokens::kMotionFastMs), surface::kCanvas, t0, [&] { order.push_back("B"); });
    const ElementId kC = MakeId(Space::Chrome, 102);
    animator.Show(kC, presets::Fade(tokens::kMotionFastMs), surface::kCanvas, t0 + 0ms, [&] { order.push_back("C"); });
    // One late tick finishes all three: ordered by scheduled finish, then start order.
    const auto result = animator.Tick(t0 + 1s);
    Check(result.completed == 3, "late tick completes every finished animation");
    Check(order.size() == 3 && order[0] == "B" && order[1] == "C" && order[2] == "A",
        "completions run in (finish time, start order)");
    Check(animator.Value(kA) == 1.0f && animator.Value(kB) == 1.0f, "values are final before completions run");

    // Reversal cancels the pending completion (a cancelled hide never destroys).
    int destroyed = 0;
    animator.Hide(kA, presets::Fade(), surface::kWindow, t0 + 2s, [&] { ++destroyed; });
    animator.Tick(t0 + 2s + 30ms);
    animator.Show(kA, presets::Fade(), surface::kWindow, t0 + 2s + 30ms);
    animator.Tick(t0 + 5s);
    Check(destroyed == 0, "reversing a hide drops its completion");

    // Same target while in flight: the completion is queued, not restarted.
    int first = 0;
    int second = 0;
    animator.Hide(kA, presets::Fade(), surface::kCanvas, t0 + 6s, [&] { ++first; });
    animator.Tick(t0 + 6s + 50ms);
    const float midway = animator.Value(kA);
    animator.Hide(kA, presets::Fade(), surface::kCanvas, t0 + 6s + 50ms, [&] { ++second; });
    Check(Near(animator.Value(kA), midway, 0.0f), "re-requesting the same target does not restart");
    animator.Tick(t0 + 7s);
    Check(first == 1 && second == 1, "both completions for the same target run once");

    // A completion may start a new animation (chained transitions).
    int chained = 0;
    animator.Show(kB, presets::Fade(), surface::kCanvas, t0 + 8s);
    animator.Hide(kB, presets::Fade(), surface::kCanvas, t0 + 8s, [&] {
        animator.Show(kB, presets::Fade(), surface::kCanvas, t0 + 9s, [&] { ++chained; });
    });
    animator.Tick(t0 + 9s);
    Check(animator.IsAnimating(kB), "completion can start a new animation");
    animator.Tick(t0 + 10s);
    Check(chained == 1 && animator.Value(kB) == 1.0f, "chained animation completes");
}

void CheckBatchingAndTransient() {
    Animator animator;
    const auto t0 = Clock::now();
    for (std::uint64_t i = 0; i < 20; ++i)
        animator.Show(MakeId(Space::Thumbnail, i), presets::Crossfade(), surface::kCanvas, t0, {}, 0, true);
    animator.Show(kA, presets::Fade(), surface::kFlyoutTabs, t0);
    const auto frame = animator.Tick(t0 + 16ms);
    Check(frame.dirty == (surface::kCanvas | surface::kFlyoutTabs), "one dirty bit per surface per frame");
    Check(animator.ActiveCount() == 21, "every element animates independently");
    animator.Tick(t0 + 1s);
    Check(animator.Size() == 1, "transient entries are forgotten after landing");
    Check(animator.Value(MakeId(Space::Thumbnail, 3), 1.0f) == 1.0f, "forgotten entry reads its fallback");

    // Per-window targets are deduplicated.
    Animator windows;
    windows.Show(MakeId(Space::Button, 1), presets::Toggle(tokens::kMotionFastMs), surface::kWindow, t0, {}, 0x1234);
    windows.Show(MakeId(Space::Window, 1), presets::Fade(), surface::kWindow, t0, {}, 0x1234);
    windows.Show(MakeId(Space::Button, 2), presets::Toggle(tokens::kMotionFastMs), surface::kWindow, t0, {}, 0x5678);
    windows.Tick(t0 + 16ms);
    Check(windows.DirtyWindows().size() == 2, "dirty windows are reported once each");

    windows.RemoveSpace(Space::Button);
    Check(!windows.Contains(MakeId(Space::Button, 1)) && windows.ActiveCount() == 1,
        "removing a space forgets its entries and their activity");
}

void CheckActivationAndTeardown() {
    Animator animator;
    int activations = 0;
    animator.SetActivationHook([&] { ++activations; });
    const auto t0 = Clock::now();
    animator.Show(kA, presets::Fade(), surface::kCanvas, t0);
    animator.Show(kB, presets::Fade(), surface::kCanvas, t0);
    Check(activations == 1, "activation hook fires once when leaving idle");
    animator.Tick(t0 + 1s);
    animator.Hide(kA, presets::Fade(), surface::kCanvas, t0 + 1s);
    Check(activations == 2, "activation hook fires again after an idle period");

    int ran = 0;
    animator.Hide(kB, presets::Fade(), surface::kCanvas, t0 + 1s, [&] { ++ran; });
    animator.FinishAll(true);
    Check(ran == 1 && animator.Value(kB) == 0.0f && !animator.AnyActive(),
        "FinishAll(true) lands everything and runs completions (window teardown)");
    animator.Show(kB, presets::Fade(), surface::kCanvas, t0 + 2s, [&] { ++ran; });
    animator.FinishAll(false);
    Check(ran == 1 && animator.Value(kB) == 1.0f, "FinishAll(false) drops completions (shutdown)");
    {
        int never = 0;
        Animator scoped;
        scoped.Show(kA, presets::Fade(), surface::kCanvas, t0, [&] { ++never; });
        // Destructor must not run completions.
        (void)never;
    }
    Check(true, "destruction with pending completions is safe");

    animator.Snap(kA, 0.5f);
    Check(animator.Value(kA) == 0.5f && !animator.IsAnimating(kA), "Snap jumps and cancels");
    Animator::Options options{};
    Check(!animator.AnimateTo(MakeId(Space::Chrome, 200), 0.3f, options, t0),
        "AnimateTo on a new entry without an initial value rests at the target");
}

} // namespace

int main() {
    CheckEasing();
    CheckPresets();
    CheckShowHideTiming();
    CheckReversalContinuity();
    CheckReducedMotion();
    CheckCompletions();
    CheckBatchingAndTransient();
    CheckActivationAndTeardown();
    std::printf("animation_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
