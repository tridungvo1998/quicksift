// CODE GUIDE: See CODE_GUIDE.md -> "The authoritative owners" and docs/DESIGN_SYSTEM.md.
// OWNER: The single design-token set (radii, spacing, type scale, motion, strokes,
// glass material, accessibility effect policy). Platform-neutral and constexpr so
// the framework, layout metrics and portable checks share one vocabulary.
//
// Rules:
//  - New UI geometry/timing comes from here (or ui_layout_metrics.h, which maps
//    component metrics onto these tokens). No one-off radii, durations or alphas.
//  - Concentric corners: an inner surface inset by `inset` inside an outer surface of
//    radius R uses ConcentricRadius(R, inset) (macOS-style continuous nesting).
//  - Every animated value must reach its exact target (EaseToward clamps; easing
//    curves map 0->0 and 1->1 exactly) and must snap when animations are disabled.

#pragma once

namespace quicksift::ui::tokens {

// --- Corner radii (DIP) ------------------------------------------------------
inline constexpr int kRadiusSmall = 6;    // menu selection, labels, document panel, closed left pane
inline constexpr int kRadiusMedium = 10;  // buttons, inputs, tabs, content panels in the pane
inline constexpr int kRadiusLarge = 14;   // cards (grid, compare), toasts
inline constexpr int kRadiusXLarge = 20;  // window-level surfaces: panels, open left pane, status, alerts

// Capsule: half the height (pill buttons, search field, scrollbars, badges).
[[nodiscard]] constexpr float CapsuleRadius(float height) noexcept {
    return height > 0.0f ? height * 0.5f : 0.0f;
}
[[nodiscard]] constexpr int ConcentricRadius(int outerRadius, int inset) noexcept {
    return outerRadius - inset > 2 ? outerRadius - inset : 2;
}

// --- Spacing grid (DIP) --------------------------------------------------------
inline constexpr int kSpace1 = 4;
inline constexpr int kSpace2 = 8;
inline constexpr int kSpace3 = 12;
inline constexpr int kSpace4 = 16;
inline constexpr int kSpace5 = 24;

// --- Strokes (DIP) ---------------------------------------------------------------
inline constexpr float kHairline = 1.0f;     // borders and separators (1 device px min)
inline constexpr float kIconStroke = 1.5f;   // SF-like line icons (chevron, check, lens, x)
inline constexpr float kFocusRingWidth = 2.0f;
inline constexpr float kFocusRingGap = 1.0f; // ring sits this far outside the shape (macOS hugs the edge)
inline constexpr float kSelectionStroke = 2.0f;
inline constexpr float kSelectionStrokeFocused = 3.0f;

// --- Typography ----------------------------------------------------------------
// Point sizes at 96 DPI. Families resolve at run time (ui_design_system.cpp) to
// Segoe UI Variable where installed and plain Segoe UI otherwise (Windows 10).
inline constexpr int kTypeCaptionPt = 9;    // badges, section headers, compact labels
inline constexpr int kTypeBodyPt = 10;      // controls, lists, menus, status
inline constexpr int kTypeHeadlinePt = 11;  // window titles, dialog titles
inline constexpr wchar_t kFamilyText[] = L"Segoe UI Variable Text";
inline constexpr wchar_t kFamilyDisplay[] = L"Segoe UI Variable Display";
inline constexpr wchar_t kFamilyFallback[] = L"Segoe UI";

// --- Motion ----------------------------------------------------------------------
inline constexpr int kMotionFastMs = 120;      // hover, press, tab highlight, fades
inline constexpr int kMotionStandardMs = 180;  // panes and overlays appearing
inline constexpr int kMotionSlowMs = 250;      // toasts leaving, large surfaces
// Frame cadence while something animates vs. the idle UI poll.
inline constexpr unsigned kAnimationFrameMs = 16;
inline constexpr unsigned kIdlePollMs = 33;

// Linear progress rate (units per second) that covers 0..1 in `durationMs`.
[[nodiscard]] constexpr float SpeedForDuration(int durationMs) noexcept {
    return durationMs > 0 ? 1000.0f / static_cast<float>(durationMs) : 1.0e6f;
}

[[nodiscard]] constexpr float Clamp01(float t) noexcept {
    return t <= 0.0f ? 0.0f : (t >= 1.0f ? 1.0f : t);
}

// Decelerating curve used for everything that appears or follows the pointer
// (approximates a critically damped spring without per-frame physics state).
[[nodiscard]] constexpr float EaseOutCubic(float t) noexcept {
    const float u = 1.0f - Clamp01(t);
    return 1.0f - u * u * u;
}

// Symmetric curve for state cross-fades (hover in/out, tab highlight).
[[nodiscard]] constexpr float EaseInOutCubic(float t) noexcept {
    const float x = Clamp01(t);
    if (x < 0.5f) return 4.0f * x * x * x;
    const float u = -2.0f * x + 2.0f;
    return 1.0f - (u * u * u) * 0.5f;
}

// --- Glass material ------------------------------------------------------------------
// One recipe for every translucent-looking surface (toasts, alerts, floating search
// field, frosted pane wash, button sheen). Cheap: solid fills, one highlight stroke,
// two offset shadows. No blur is computed per frame.
struct GlassMaterial {
    float fillAlpha = 0.96f;          // body opacity over the canvas
    float highlightAlpha = 0.18f;     // 1 px inner top specular edge
    float sheenAlpha = 0.10f;         // top-half wash
    float borderAlpha = 0.85f;        // hairline outer edge
    float ambientShadowAlpha = 0.16f; // wide, soft
    float keyShadowAlpha = 0.22f;     // tight, offset downwards
    float ambientShadowSpread = 3.0f; // DIP
    float keyShadowOffset = 2.0f;     // DIP
};

struct VisualEffects {
    bool highContrast = false;   // SPI_GETHIGHCONTRAST: system colours, no glass, no blobs
    bool animations = true;      // SPI_GETCLIENTAREAANIMATION
    bool transparency = true;    // Personalize\EnableTransparency
    [[nodiscard]] constexpr bool Glass() const noexcept { return !highContrast && transparency; }
    [[nodiscard]] constexpr bool Animate() const noexcept { return animations; }
    constexpr bool operator==(const VisualEffects& other) const noexcept {
        return highContrast == other.highContrast && animations == other.animations &&
            transparency == other.transparency;
    }
    constexpr bool operator!=(const VisualEffects& other) const noexcept { return !(*this == other); }
};

[[nodiscard]] constexpr GlassMaterial Glass(bool dark, const VisualEffects& effects) noexcept {
    GlassMaterial material{};
    if (dark) {
        material.fillAlpha = 0.94f;
        material.highlightAlpha = 0.14f;
        material.sheenAlpha = 0.0f; // top-half wash reads as a two-tone split in dark mode
        material.ambientShadowAlpha = 0.22f;
        material.keyShadowAlpha = 0.30f;
    } else {
        material.fillAlpha = 0.97f;
        material.highlightAlpha = 0.60f;
        material.sheenAlpha = 0.14f;
        material.ambientShadowAlpha = 0.08f;
        material.keyShadowAlpha = 0.12f;
    }
    if (!effects.Glass()) {
        // Reduced transparency / high contrast: opaque body, no sheen or specular
        // edge; a single crisp shadow keeps the elevation readable.
        material.fillAlpha = 1.0f;
        material.highlightAlpha = 0.0f;
        material.sheenAlpha = 0.0f;
        material.borderAlpha = 1.0f;
        material.ambientShadowAlpha = 0.0f;
    }
    return material;
}

// Animation step: duration-based, exact at the target, instant when disabled.
[[nodiscard]] constexpr float StepToward(float current, float target, float deltaSeconds,
    int durationMs, const VisualEffects& effects) noexcept {
    if (!effects.Animate()) return target;
    if (!(deltaSeconds > 0.0f)) return current;
    const float step = deltaSeconds * SpeedForDuration(durationMs);
    if (current < target) return current + step < target ? current + step : target;
    if (current > target) return current - step > target ? current - step : target;
    return current;
}

} // namespace quicksift::ui::tokens
