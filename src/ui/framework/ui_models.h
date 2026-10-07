// OWNER: Platform-neutral UI component state and interaction models.
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
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
};

class AlertPresenter {
public:
    void Show(AlertRequest request);
    void Clear() noexcept;

    bool Visible() const noexcept { return visible_; }
    const AlertRequest& Request() const noexcept { return request_; }
    bool PrimaryHot() const noexcept { return primaryHot_; }
    bool SecondaryHot() const noexcept { return secondaryHot_; }
    const FloatRect& PrimaryRect() const noexcept { return primaryRect_; }
    const FloatRect& SecondaryRect() const noexcept { return secondaryRect_; }

    void SetButtonRects(FloatRect primary, FloatRect secondary) noexcept;
    bool UpdateHover(float x, float y) noexcept;
    AlertHit HitTest(float x, float y) const noexcept;
    std::optional<std::uint64_t> Resolve(bool accepted) noexcept;

private:
    AlertRequest request_{};
    FloatRect primaryRect_{};
    FloatRect secondaryRect_{};
    bool visible_ = false;
    bool primaryHot_ = false;
    bool secondaryHot_ = false;
};

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
    float Opacity(TimePoint now = Clock::now()) const noexcept;

private:
    void Begin(std::wstring text, TimePoint now);

    std::chrono::milliseconds duration_;
    TimePoint startedAt_{};
    std::wstring currentText_;
    std::deque<std::wstring> queue_;
    bool visible_ = false;
};

} // namespace quicksift::ui::framework
