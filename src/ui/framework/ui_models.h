// OWNER: Platform-neutral UI component state and interaction models.
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quicksift::ui::framework {

enum class ButtonRole {
    Standard,
    TitleBar,
    Header,
    Danger,
};

enum class ButtonGlyph {
    None,
    Theme,
    RotateLeft,
    RotateRight,
    Help,
    LeftArrow,
    RightArrow,
};

struct ButtonVisualState {
    bool hot = false;
    bool pressed = false;
    bool disabled = false;
    bool focused = false;
    bool selected = false;
    bool popup = false;
};

struct ButtonPresentation {
    std::wstring text;
    ButtonRole role = ButtonRole::Standard;
    ButtonGlyph glyph = ButtonGlyph::None;
    ButtonVisualState state{};
    // Optional animated blends (0..1). When negative, PaintButton derives from state.
    float hoverBlend = -1.0f;
    float pressBlend = -1.0f;
    // Colour (COLORREF layout 0x00BBGGRR) of the surface directly behind the
    // control. When set, PaintButton paints the area outside the anti-aliased pill
    // with it so AA fringes blend into the real parent colour (no dark/light halo)
    // and the HWND may use the slack AA region (ApplyAntialiasedControlRegion).
    // When unset, the legacy rounded erase is used and the corners stay untouched.
    std::optional<std::uint32_t> parentSurface;
};

// Low-cost scalar motion helper for framework timers (tabs, pane, buttons).
[[nodiscard]] inline float EaseToward(float current, float target, float deltaSeconds,
    float speed = 14.0f) noexcept {
    if (!(deltaSeconds > 0.0f) || !(speed > 0.0f)) return current;
    if (current < target) {
        const float next = current + deltaSeconds * speed;
        return next < target ? next : target;
    }
    if (current > target) {
        const float next = current - deltaSeconds * speed;
        return next > target ? next : target;
    }
    return current;
}

[[nodiscard]] inline float Smoothstep01(float t) noexcept {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

struct MenuItem {
    std::wstring text;
    int command = 0;
    bool checked = false;
    bool separator = false;
    bool destructive = false;
    bool header = false;
};

struct PopupMenuMetrics {
    int itemHeight = 32;
    int separatorHeight = 9;
    int padding = 6;
};

class PopupMenuModel {
public:
    explicit PopupMenuModel(std::vector<MenuItem> items = {});

    const std::vector<MenuItem>& Items() const noexcept { return items_; }
    bool Empty() const noexcept { return items_.empty(); }
    int HoveredIndex() const noexcept { return hoveredIndex_; }
    void SetHoveredIndex(int index) noexcept;
    int FirstSelectableIndex() const noexcept;
    int HitTestY(int y, const PopupMenuMetrics& metrics) const noexcept;
    int MoveHover(int direction) noexcept;
    int HoveredCommand() const noexcept;
    int TotalHeight(const PopupMenuMetrics& metrics) const noexcept;
    int ItemTop(int index, const PopupMenuMetrics& metrics) const noexcept;
    int ItemHeight(int index, const PopupMenuMetrics& metrics) const noexcept;

private:
    bool IsSelectable(int index) const noexcept;

    std::vector<MenuItem> items_;
    int hoveredIndex_ = -1;
};

struct FloatRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    bool Empty() const noexcept { return right <= left || bottom <= top; }
    bool Contains(float x, float y) const noexcept {
        return !Empty() && x >= left && x <= right && y >= top && y <= bottom;
    }
};

enum class AlertKind {
    Information,
    Warning,
    Error,
};

enum class AlertMode {
    Notice,
    Confirmation,
};

enum class AlertHit {
    None,
    Primary,
    Secondary,
    ModalBackdrop,
    Tertiary,
};

// Keyboard focus inside the glass alert (Tab / Shift+Tab / arrows move it; Enter
// activates the focused button; Esc always cancels).
enum class AlertButton {
    Primary,
    Secondary,
    Tertiary,
};

struct AlertRequest {
    std::wstring title;
    std::wstring message;
    std::wstring primaryText;
    std::wstring secondaryText;
    AlertKind kind = AlertKind::Warning;
    AlertMode mode = AlertMode::Notice;
    bool destructive = false;
    std::uint64_t actionToken = 0;
    // Optional third (left-aligned) button for Confirmation alerts. It dispatches
    // tertiaryActionToken. With tertiaryKeepsOpen the alert stays visible (e.g.
    // "See missing files list" opens a child window without dismissing the alert).
    std::wstring tertiaryText;
    std::uint64_t tertiaryActionToken = 0;
    bool tertiaryKeepsOpen = false;
};

class AlertPresenter {
public:
    void Show(AlertRequest request);
    void Clear() noexcept;

    bool Visible() const noexcept { return visible_; }
    const AlertRequest& Request() const noexcept { return request_; }
    bool PrimaryHot() const noexcept { return primaryHot_; }
    bool SecondaryHot() const noexcept { return secondaryHot_; }
    bool TertiaryHot() const noexcept { return tertiaryHot_; }
    bool AnyButtonHot() const noexcept { return primaryHot_ || secondaryHot_ || tertiaryHot_; }
    const FloatRect& PrimaryRect() const noexcept { return primaryRect_; }
    const FloatRect& SecondaryRect() const noexcept { return secondaryRect_; }
    const FloatRect& TertiaryRect() const noexcept { return tertiaryRect_; }
    bool HasTertiary() const noexcept;

    void SetButtonRects(FloatRect primary, FloatRect secondary,
        FloatRect tertiary = FloatRect{}) noexcept;
    bool UpdateHover(float x, float y) noexcept;
    AlertHit HitTest(float x, float y) const noexcept;
    std::optional<std::uint64_t> Resolve(bool accepted) noexcept;
    // Dispatches the tertiary token. Clears the alert unless tertiaryKeepsOpen.
    std::optional<std::uint64_t> ActivateTertiary() noexcept;

    AlertButton FocusedButton() const noexcept { return focused_; }
    bool FocusVisible() const noexcept { return focusVisible_; }
    // Moves keyboard focus through the available buttons (visual order left to
    // right: tertiary, secondary, primary) and makes the focus ring visible.
    void MoveFocus(bool backwards) noexcept;

private:
    AlertRequest request_{};
    FloatRect primaryRect_{};
    FloatRect secondaryRect_{};
    FloatRect tertiaryRect_{};
    bool visible_ = false;
    bool primaryHot_ = false;
    bool secondaryHot_ = false;
    bool tertiaryHot_ = false;
    AlertButton focused_ = AlertButton::Primary;
    bool focusVisible_ = false;
};

// ---- Overlay text geometry (toasts, alerts) ------------------------------------------
// One portable model for how much room overlay text gets, so measurement and
// painting can never disagree. The painter measures text with a DirectWrite layout
// whose max width is ToastTextLayoutWidth() and then DRAWS THAT SAME LAYOUT
// (centred, never re-wrapped at a narrower width); the glass body comes from
// ComputeToastGeometry over the measured size. All values are DIPs.
inline constexpr float kToastPaddingX = 16.0f;
inline constexpr float kToastPaddingY = 12.0f;
inline constexpr float kToastEdge = 24.0f;
inline constexpr float kToastMaxWidth = 420.0f;
inline constexpr float kToastMinWidth = 160.0f;
inline constexpr float kToastMinHeight = 36.0f;

struct ToastGeometry {
    float width = 0.0f;        // glass body width
    float height = 0.0f;       // glass body height
    float layoutWidth = 0.0f;  // text layout box width (measurement == paint)
    float layoutHeight = 0.0f; // text box height inside the body (clip box)
    float textTop = 0.0f;      // text box top, relative to the body top
    bool clipped = false;      // text taller than the tallest allowed toast
    [[nodiscard]] bool Valid() const noexcept { return width > 0.0f && height > 0.0f && layoutWidth > 0.0f; }
};

// Max text width for a surface; <= 0 means no room for a toast.
[[nodiscard]] float ToastTextLayoutWidth(float surfaceWidth) noexcept;
// Body size from the MEASURED text block (DWRITE_TEXT_METRICS width/height of the
// layout created at ToastTextLayoutWidth). Rounds outward to whole DIPs so device
// pixel snapping can never make the body smaller than the text.
[[nodiscard]] ToastGeometry ComputeToastGeometry(float surfaceWidth, float surfaceHeight,
    float textWidth, float textHeight) noexcept;

struct AlertCardGeometry {
    float cardHeight = 0.0f;
    float messageBoxHeight = 0.0f; // room for the message (clip box)
    bool clipped = false;
};
// Card height from the measured title/message heights: grows with the text up to
// the surface (minus margins), and only then clips the message.
[[nodiscard]] AlertCardGeometry ComputeAlertCardGeometry(float surfaceHeight,
    float titleHeight, float messageHeight) noexcept;
inline constexpr float kAlertMinHeight = 150.0f;
inline constexpr float kAlertMaxHeight = 640.0f;
inline constexpr float kAlertChromeHeight = 100.0f; // 22 top + 12 title gap + 8 + 50 buttons + 8

// Overlay text normalisation: CRLF / CR -> LF, tabs -> space, trailing line breaks
// and spaces trimmed (they would add an empty measured line).
[[nodiscard]] std::wstring NormalizeOverlayText(std::wstring_view text);

class ToastPresenter {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit ToastPresenter(std::chrono::milliseconds duration = std::chrono::milliseconds(2000)) noexcept;

    void Show(std::wstring text, TimePoint now = Clock::now());
    void Clear() noexcept;
    bool Tick(TimePoint now = Clock::now());

    bool Visible() const noexcept { return visible_; }
    const std::wstring& CurrentText() const noexcept { return currentText_; }
    std::size_t QueuedCount() const noexcept { return queue_.size(); }
    // Time left before the current toast ends (zero when hidden). The framework's
    // animator starts the exit transition when this drops to the exit duration.
    std::chrono::milliseconds Remaining(TimePoint now = Clock::now()) const noexcept;
    // Fade in over tokens::kMotionFastMs (ease-out cubic), out over kMotionSlowMs.
    // With animations disabled the toast is fully opaque for its whole lifetime.
    float Opacity(TimePoint now = Clock::now()) const noexcept;
    void SetAnimationsEnabled(bool enabled) noexcept { animationsEnabled_ = enabled; }

private:
    void Begin(std::wstring text, TimePoint now);

    bool animationsEnabled_ = true;
    std::chrono::milliseconds duration_;
    TimePoint startedAt_{};
    std::wstring currentText_;
    std::deque<std::wstring> queue_;
    bool visible_ = false;
};

} // namespace quicksift::ui::framework
