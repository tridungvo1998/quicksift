// OWNER: Animator core (see ui_animation.h). Platform-neutral; part of QuickSiftCore.
#include "ui/framework/ui_animation.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace quicksift::ui::animation {

namespace {
constexpr float kSpringOmega = 7.0f; // ~critically damped settle inside the duration

float SpringLite(float t) noexcept {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    // x(t) = 1 - (1 + w t) e^(-w t), normalised so x(1) == 1 exactly. Its derivative
    // w^2 t e^(-w t) is non-negative, so the curve is monotonic (no overshoot).
    static const float normaliser = 1.0f - (1.0f + kSpringOmega) * std::exp(-kSpringOmega);
    const float wt = kSpringOmega * t;
    const float value = (1.0f - (1.0f + wt) * std::exp(-wt)) / normaliser;
    return std::clamp(value, 0.0f, 1.0f);
}
} // namespace

float Ease(Curve curve, float t) noexcept {
    const float x = tokens::Clamp01(t);
    switch (curve) {
    case Curve::Linear: return x;
    case Curve::EaseOutCubic: return tokens::EaseOutCubic(x);
    case Curve::EaseInOutCubic: return tokens::EaseInOutCubic(x);
    case Curve::Smoothstep: return x * x * (3.0f - 2.0f * x);
    case Curve::SpringLite: return SpringLite(x);
    }
    return x;
}

Appearance Resolve(const Preset& preset, float presence) noexcept {
    Appearance appearance{};
    const float p = tokens::Clamp01(presence);
    appearance.presence = p;
    appearance.opacity = preset.fromOpacity + (1.0f - preset.fromOpacity) * p;
    const float travel = (1.0f - p) * preset.offsetDip;
    switch (preset.direction) {
    case Direction::Down: appearance.offsetY = -travel; break;  // starts above, moves down
    case Direction::Up: appearance.offsetY = travel; break;     // starts below, moves up
    case Direction::Right: appearance.offsetX = -travel; break;
    case Direction::Left: appearance.offsetX = travel; break;
    }
    appearance.scale = preset.fromScale + (1.0f - preset.fromScale) * p;
    appearance.reveal = preset.fromReveal + (1.0f - preset.fromReveal) * p;
    return appearance;
}

void Animator::MarkDirty(const Entry& entry) {
    pendingDirty_ |= entry.surface;
    if (entry.window != 0 &&
        std::find(pendingWindows_.begin(), pendingWindows_.end(), entry.window) == pendingWindows_.end()) {
        pendingWindows_.push_back(entry.window);
    }
}

void Animator::SetActive(Entry& entry, bool active) {
    if (entry.active == active) return;
    entry.active = active;
    if (active) {
        const bool wasIdle = activeCount_ == 0;
        ++activeCount_;
        if (wasIdle && activationHook_) activationHook_();
    } else if (activeCount_ > 0) {
        --activeCount_;
    }
}

void Animator::RunAll(std::vector<Completion>& completions) {
    std::vector<Completion> local;
    local.swap(completions);
    for (Completion& completion : local) {
        if (completion) completion();
    }
}

bool Animator::AnimateTo(ElementId id, float target, const Options& options, TimePoint now,
    Completion done, std::optional<float> initial) {
    auto [it, inserted] = entries_.try_emplace(id);
    Entry& entry = it->second;
    if (inserted) {
        entry.value = initial.value_or(target);
        entry.from = entry.value;
        entry.to = entry.value;
    }
    entry.surface = options.surface;
    entry.window = options.window;
    entry.transient = options.transient;

    if (entry.active && entry.to == target) {
        if (done) entry.completions.push_back(std::move(done));
        return true;
    }
    if (!entry.active && entry.value == target) {
        // Already at rest on the target: nothing to animate.
        const bool forget = entry.transient;
        if (inserted || forget) MarkDirty(entry);
        if (forget) entries_.erase(it);
        if (done) done();
        return false;
    }

    // New trip (or a reversal): start from the current value; completions that
    // belonged to the previous target are dropped (that target is never reached).
    entry.completions.clear();
    entry.from = entry.value;
    entry.to = target;
    entry.start = now;
    entry.curve = options.curve;
    entry.sequence = nextSequence_++;
    const float distance = std::fabs(entry.to - entry.from);
    const float share = options.proportional ? std::min(1.0f, distance) : 1.0f;
    const auto micros = static_cast<long long>(std::lround(
        static_cast<double>(std::max(0, options.durationMs)) * 1000.0 * static_cast<double>(share)));
    entry.duration = std::chrono::microseconds(micros);

    if (!enabled_ || suspended_ || micros <= 0) {
        entry.value = target;
        SetActive(entry, false);
        MarkDirty(entry);
        if (entry.transient) entries_.erase(it);
        if (done) done();
        return false;
    }
    if (done) entry.completions.push_back(std::move(done));
    SetActive(entry, true);
    return true;
}

bool Animator::Show(ElementId id, const Preset& preset, SurfaceMask surfaceMask, TimePoint now,
    Completion done, std::uintptr_t window, bool transient) {
    Options options{};
    options.durationMs = preset.inMs;
    options.curve = preset.inCurve;
    options.proportional = preset.proportional;
    options.surface = surfaceMask;
    options.window = window;
    options.transient = transient;
    return AnimateTo(id, 1.0f, options, now, std::move(done), 0.0f);
}

bool Animator::Hide(ElementId id, const Preset& preset, SurfaceMask surfaceMask, TimePoint now,
    Completion done, std::uintptr_t window, bool transient) {
    Options options{};
    options.durationMs = preset.outMs;
    options.curve = preset.outCurve;
    options.proportional = preset.proportional;
    options.surface = surfaceMask;
    options.window = window;
    options.transient = transient;
    return AnimateTo(id, 0.0f, options, now, std::move(done), 1.0f);
}

bool Animator::SetShown(ElementId id, bool shown, const Preset& preset, SurfaceMask surfaceMask,
    TimePoint now, Completion done, std::uintptr_t window) {
    if (!entries_.count(id)) {
        // First sighting: start hidden so a first Show animates and a first Hide is a no-op.
        Snap(id, 0.0f, surfaceMask, window);
    }
    return shown ? Show(id, preset, surfaceMask, now, std::move(done), window)
                 : Hide(id, preset, surfaceMask, now, std::move(done), window);
}

void Animator::Snap(ElementId id, float value, SurfaceMask surfaceMask, std::uintptr_t window) {
    Entry& entry = entries_[id];
    if (surfaceMask != surface::kNone) entry.surface = surfaceMask;
    if (window != 0) entry.window = window;
    entry.completions.clear();
    SetActive(entry, false);
    if (entry.value != value) MarkDirty(entry);
    entry.value = value;
    entry.from = value;
    entry.to = value;
}

void Animator::Remove(ElementId id) noexcept {
    const auto it = entries_.find(id);
    if (it == entries_.end()) return;
    if (it->second.active && activeCount_ > 0) --activeCount_;
    entries_.erase(it);
}

void Animator::RemoveSpace(Space space) noexcept {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (SpaceOf(it->first) == space) {
            if (it->second.active && activeCount_ > 0) --activeCount_;
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

void Animator::FinishAll(bool runCompletions) {
    std::vector<Finished> finished;
    for (auto it = entries_.begin(); it != entries_.end();) {
        Entry& entry = it->second;
        if (entry.active) {
            entry.value = entry.to;
            SetActive(entry, false);
            MarkDirty(entry);
            finished.push_back({ entry.start + entry.duration, entry.sequence, it->first,
                std::move(entry.completions) });
            entry.completions.clear();
            if (entry.transient) {
                it = entries_.erase(it);
                continue;
            }
        }
        ++it;
    }
    if (!runCompletions) return;
    std::sort(finished.begin(), finished.end(), [](const Finished& a, const Finished& b) {
        return a.due != b.due ? a.due < b.due : a.sequence < b.sequence;
    });
    for (Finished& item : finished) RunAll(item.completions);
}

Animator::TickResult Animator::Tick(TimePoint now) {
    TickResult result{};
    std::vector<Finished> finished;
    for (auto it = entries_.begin(); it != entries_.end();) {
        Entry& entry = it->second;
        if (!entry.active) {
            ++it;
            continue;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - entry.start);
        const float previous = entry.value;
        if (!enabled_ || suspended_ || elapsed >= entry.duration) {
            entry.value = entry.to;
            SetActive(entry, false);
            finished.push_back({ entry.start + entry.duration, entry.sequence, it->first,
                std::move(entry.completions) });
            entry.completions.clear();
        } else if (elapsed.count() > 0) {
            const float t = static_cast<float>(elapsed.count()) /
                static_cast<float>(entry.duration.count());
            entry.value = entry.from + (entry.to - entry.from) * Ease(entry.curve, t);
        }
        if (entry.value != previous) MarkDirty(entry);
        if (!entry.active && entry.transient) {
            MarkDirty(entry);
            it = entries_.erase(it);
            continue;
        }
        ++it;
    }
    std::sort(finished.begin(), finished.end(), [](const Finished& a, const Finished& b) {
        return a.due != b.due ? a.due < b.due : a.sequence < b.sequence;
    });
    for (Finished& item : finished) RunAll(item.completions);
    result.completed = finished.size();

    result.dirty = pendingDirty_;
    dirtyWindows_.swap(pendingWindows_);
    pendingWindows_.clear();
    pendingDirty_ = surface::kNone;
    result.active = activeCount_ > 0;
    return result;
}

float Animator::Value(ElementId id, float fallback) const noexcept {
    const auto it = entries_.find(id);
    return it == entries_.end() ? fallback : it->second.value;
}

float Animator::Target(ElementId id, float fallback) const noexcept {
    const auto it = entries_.find(id);
    return it == entries_.end() ? fallback : it->second.to;
}

bool Animator::IsAnimating(ElementId id) const noexcept {
    const auto it = entries_.find(id);
    return it != entries_.end() && it->second.active;
}

} // namespace quicksift::ui::animation
