// CODE GUIDE: See docs/DESIGN_SYSTEM.md -> "Motion" and "Animation system".
// OWNER: The single animation system: value tweening keyed by element id, easing
// curves, presets (Fade, SlideFade, DropDownReveal, PopScale, Crossfade, Toggle),
// interruption/reversal, completion callbacks and batched dirty reporting.
// Platform-neutral (no Win32): unit-tested by scripts/checks/animation_checks.cpp.
//
// Rules:
//  - Feature code never owns timers or tweens for transient UI. It asks the
//    framework's Animator to Show/Hide/AnimateTo and reads Value()/Appear() in paint.
//  - The Animator is driven by the existing UI animation timer (16 ms while any
//    entry is active, the 33 ms idle poll otherwise). A nested modal loop that
//    cannot see that timer (TrackDropdown) owns a local Animator and pumps frames
//    from its own loop (no timer).
//  - Every animation starts from the CURRENT value, so a reversal mid-flight never
//    jumps. A completion runs only if its target is actually reached; reversing
//    drops the pending completion (a cancelled hide never destroys a window).
//  - Animations disabled (SPI_GETCLIENTAREAANIMATION off) or suspended (window
//    minimized): values jump to the target and completions run immediately.
#pragma once

#include "ui/ui_design_tokens.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace quicksift::ui::animation {

// ---- Easing ------------------------------------------------------------------------
enum class Curve : std::uint8_t {
    Linear,          // constant speed (reversible channels whose consumer eases)
    EaseOutCubic,    // appearing / following the pointer
    EaseInOutCubic,  // symmetric cross-fades
    Smoothstep,      // symmetric, gentle ends (left pane geometry)
    SpringLite,      // critically damped spring response normalised to land at 1
};

// Maps t in [0,1] to [0,1]; exact at both ends, monotonic for every curve.
[[nodiscard]] float Ease(Curve curve, float t) noexcept;

// ---- Element ids and surfaces ---------------------------------------------------------
using ElementId = std::uint64_t;

// Namespaces live in the top 8 bits; the low 56 bits are an index, handle or hash.
enum class Space : std::uint8_t {
    Chrome = 1,      // singletons: left pane, filmstrip, search field, toast, alert
    FlyoutTab = 2,   // index = tab slot
    Button = 3,      // index = HWND value
    Window = 4,      // index = top-level HWND value (layered fades)
    Thumbnail = 5,   // index = path hash (placeholder -> image crossfade)
    Popup = 6,       // local Animator inside a dropdown loop
};

[[nodiscard]] constexpr ElementId MakeId(Space space, std::uint64_t index) noexcept {
    return (static_cast<std::uint64_t>(space) << 56) | (index & 0x00FFFFFFFFFFFFFFull);
}
[[nodiscard]] constexpr Space SpaceOf(ElementId id) noexcept {
    return static_cast<Space>(id >> 56);
}

namespace element {
inline constexpr ElementId kLeftPane = MakeId(Space::Chrome, 1);
inline constexpr ElementId kFilmstrip = MakeId(Space::Chrome, 2);
inline constexpr ElementId kSearchField = MakeId(Space::Chrome, 3);
inline constexpr ElementId kToast = MakeId(Space::Chrome, 4);
inline constexpr ElementId kAlert = MakeId(Space::Chrome, 5);
inline constexpr ElementId kPopupReveal = MakeId(Space::Popup, 1);
[[nodiscard]] constexpr ElementId FlyoutTab(std::size_t slot) noexcept {
    return MakeId(Space::FlyoutTab, slot);
}
} // namespace element

// Surfaces are a bitmask so one tick reports "repaint the canvas" once no matter how
// many canvas animations advanced (batched invalidation: one invalidate per surface).
using SurfaceMask = std::uint32_t;
namespace surface {
inline constexpr SurfaceMask kNone = 0;
inline constexpr SurfaceMask kCanvas = 1u << 0;      // D2D canvas (toast, alert, search, thumbs, filmstrip)
inline constexpr SurfaceMask kLeftPane = 1u << 1;    // pane geometry frame (LayoutLeftPaneAnimationFrame)
inline constexpr SurfaceMask kFlyoutTabs = 1u << 2;  // sidebar tab highlight
inline constexpr SurfaceMask kWindow = 1u << 3;      // per-HWND target (buttons, layered windows)
inline constexpr SurfaceMask kPopup = 1u << 4;       // dropdown content (local animator)
} // namespace surface

// ---- Presets ----------------------------------------------------------------------------
enum class Direction : std::uint8_t { Up, Down, Left, Right };

// A preset maps one animated "presence" scalar (0 hidden .. 1 shown, already eased)
// onto visual properties. One scalar per element keeps the system cheap and makes
// every property reverse together.
struct Preset {
    int inMs = tokens::kMotionFastMs;
    int outMs = tokens::kMotionFastMs;
    Curve inCurve = Curve::EaseOutCubic;
    Curve outCurve = Curve::EaseOutCubic;
    float fromOpacity = 0.0f;   // opacity at presence 0
    float offsetDip = 0.0f;     // distance travelled from presence 0 to 1
    Direction direction = Direction::Down;  // direction of travel while appearing
    float fromScale = 1.0f;     // scale at presence 0 (PopScale)
    float fromReveal = 1.0f;    // clip-reveal fraction at presence 0 (DropDownReveal)
    bool proportional = true;   // a partial trip takes a proportional share of the duration
};

namespace presets {
[[nodiscard]] constexpr Preset Fade(int inMs = tokens::kMotionFastMs,
    int outMs = tokens::kMotionFastMs) noexcept {
    Preset preset{};
    preset.inMs = inMs;
    preset.outMs = outMs;
    preset.inCurve = Curve::EaseOutCubic;
    preset.outCurve = Curve::EaseInOutCubic;
    return preset;
}
[[nodiscard]] constexpr Preset SlideFade(Direction direction, float distanceDip,
    int inMs = tokens::kMotionStandardMs, int outMs = tokens::kMotionSlowMs) noexcept {
    Preset preset = Fade(inMs, outMs);
    preset.direction = direction;
    preset.offsetDip = distanceDip;
    preset.inCurve = Curve::SpringLite;
    return preset;
}
// Dropdowns: quick fade + short travel away from the anchor + clip reveal.
[[nodiscard]] constexpr Preset DropDownReveal(bool opensUpward) noexcept {
    Preset preset = Fade(tokens::kMotionFastMs, tokens::kMotionFastMs);
    preset.direction = opensUpward ? Direction::Up : Direction::Down;
    preset.offsetDip = 4.0f;
    preset.fromReveal = 0.85f;
    return preset;
}
// Alerts/sheets: 0.96 -> 1 scale with fade (spring-lite settle), quicker exit.
[[nodiscard]] constexpr Preset PopScale() noexcept {
    Preset preset = Fade(tokens::kMotionStandardMs, tokens::kMotionFastMs);
    preset.inCurve = Curve::SpringLite;
    preset.fromScale = 0.96f;
    return preset;
}
// Content swap on small surfaces (placeholder -> decoded thumbnail).
[[nodiscard]] constexpr Preset Crossfade() noexcept {
    Preset preset = Fade(tokens::kMotionFastMs, tokens::kMotionFastMs);
    preset.inCurve = Curve::EaseInOutCubic;
    return preset;
}
// Reversible two-state channel (pane, tabs, hover). Linear progress at constant
// speed; the consumer applies its own symmetric curve, so mid-flight reversal is
// seamless in value AND velocity.
[[nodiscard]] constexpr Preset Toggle(int durationMs) noexcept {
    Preset preset{};
    preset.inMs = durationMs;
    preset.outMs = durationMs;
    preset.inCurve = Curve::Linear;
    preset.outCurve = Curve::Linear;
    return preset;
}
} // namespace presets

struct Appearance {
    float presence = 0.0f;
    float opacity = 0.0f;
    float offsetX = 0.0f;   // DIP to add to the element's resting position
    float offsetY = 0.0f;
    float scale = 1.0f;
    float reveal = 1.0f;    // fraction of the element revealed from its anchor edge
    [[nodiscard]] bool Visible() const noexcept { return opacity > 0.002f; }
    [[nodiscard]] bool AtRest() const noexcept { return presence >= 1.0f; }
};

[[nodiscard]] Appearance Resolve(const Preset& preset, float presence) noexcept;

// ---- Animator ------------------------------------------------------------------------------
class Animator {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Completion = std::function<void()>;

    struct Options {
        int durationMs = tokens::kMotionFastMs;
        Curve curve = Curve::EaseOutCubic;
        bool proportional = true;
        SurfaceMask surface = surface::kNone;
        std::uintptr_t window = 0;   // per-window target reported in DirtyWindows()
        bool transient = false;      // forget the entry once it reaches its target
    };

    struct TickResult {
        SurfaceMask dirty = surface::kNone;
        std::size_t completed = 0;
        bool active = false;
    };

    Animator() = default;
    Animator(const Animator&) = delete;
    Animator& operator=(const Animator&) = delete;
    // Never runs completions (they may reference owners that are being destroyed).
    ~Animator() = default;

    void SetEnabled(bool enabled) noexcept { enabled_ = enabled; }
    [[nodiscard]] bool Enabled() const noexcept { return enabled_; }
    // Minimized/occluded: in-flight entries finish at the next Tick and new ones are instant.
    void SetSuspended(bool suspended) noexcept { suspended_ = suspended; }
    [[nodiscard]] bool Suspended() const noexcept { return suspended_; }
    // Called synchronously when the animator goes from idle to active (e.g. to
    // switch the shared UI timer to the 16 ms frame cadence).
    void SetActivationHook(std::function<void()> hook) { activationHook_ = std::move(hook); }

    // Moves `id` toward `target` from its current value (or `initial` when the entry
    // is new). Idempotent: the same target while in flight (or already reached)
    // changes nothing; a provided completion is then queued on the existing trip
    // (or run at once when already at rest). Returns true while animating.
    bool AnimateTo(ElementId id, float target, const Options& options, TimePoint now,
        Completion done = {}, std::optional<float> initial = std::nullopt);

    // Preset helpers on the presence scalar (0..1). `initial` for a brand-new entry
    // is 0 for Show and 1 for Hide.
    bool Show(ElementId id, const Preset& preset, SurfaceMask surface, TimePoint now,
        Completion done = {}, std::uintptr_t window = 0, bool transient = false);
    bool Hide(ElementId id, const Preset& preset, SurfaceMask surface, TimePoint now,
        Completion done = {}, std::uintptr_t window = 0, bool transient = false);
    bool SetShown(ElementId id, bool shown, const Preset& preset, SurfaceMask surface,
        TimePoint now, Completion done = {}, std::uintptr_t window = 0);

    // Jumps to `value` and cancels any trip (pending completions are dropped).
    void Snap(ElementId id, float value, SurfaceMask surface = surface::kNone,
        std::uintptr_t window = 0);
    // Forgets an entry without running its completions (owner destroyed).
    void Remove(ElementId id) noexcept;
    void RemoveSpace(Space space) noexcept;
    // Finishes everything: values jump to their targets; completions run when asked
    // (window teardown) or are dropped (process shutdown).
    void FinishAll(bool runCompletions);

    // Advances every active entry to `now` and runs completions afterwards, ordered
    // by (scheduled finish time, start order). Completions may start new animations.
    TickResult Tick(TimePoint now);

    [[nodiscard]] float Value(ElementId id, float fallback = 0.0f) const noexcept;
    [[nodiscard]] float Target(ElementId id, float fallback = 0.0f) const noexcept;
    [[nodiscard]] bool Contains(ElementId id) const noexcept { return entries_.count(id) != 0; }
    [[nodiscard]] bool IsAnimating(ElementId id) const noexcept;
    [[nodiscard]] bool AnyActive() const noexcept { return activeCount_ > 0; }
    [[nodiscard]] std::size_t ActiveCount() const noexcept { return activeCount_; }
    [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }
    [[nodiscard]] Appearance Appear(ElementId id, const Preset& preset,
        float fallbackPresence = 0.0f) const noexcept {
        return Resolve(preset, Value(id, fallbackPresence));
    }
    // Windows whose value changed in the last Tick (deduplicated, in first-change order).
    [[nodiscard]] const std::vector<std::uintptr_t>& DirtyWindows() const noexcept { return dirtyWindows_; }

private:
    struct Entry {
        float value = 0.0f;
        float from = 0.0f;
        float to = 0.0f;
        TimePoint start{};
        std::chrono::microseconds duration{ 0 };
        Curve curve = Curve::Linear;
        SurfaceMask surface = surface::kNone;
        std::uintptr_t window = 0;
        std::uint64_t sequence = 0;
        bool active = false;
        bool transient = false;
        std::vector<Completion> completions;
    };
    struct Finished {
        TimePoint due{};
        std::uint64_t sequence = 0;
        ElementId id = 0;
        std::vector<Completion> completions;
    };

    void MarkDirty(const Entry& entry);
    void SetActive(Entry& entry, bool active);
    static void RunAll(std::vector<Completion>& completions);

    std::unordered_map<ElementId, Entry> entries_;
    std::vector<std::uintptr_t> dirtyWindows_;
    std::vector<std::uintptr_t> pendingWindows_;
    SurfaceMask pendingDirty_ = surface::kNone;
    std::function<void()> activationHook_;
    std::size_t activeCount_ = 0;
    std::uint64_t nextSequence_ = 1;
    bool enabled_ = true;
    bool suspended_ = false;
};

} // namespace quicksift::ui::animation
