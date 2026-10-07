// OWNER: Platform-neutral UI component state and interaction implementation.
#include "ui/framework/ui_models.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace quicksift::ui::framework {

PopupMenuModel::PopupMenuModel(std::vector<MenuItem> items)
    : items_(std::move(items)), hoveredIndex_(FirstSelectableIndex()) {}

bool PopupMenuModel::IsSelectable(int index) const noexcept {
    return index >= 0 && static_cast<std::size_t>(index) < items_.size() &&
        !items_[static_cast<std::size_t>(index)].separator &&
        !items_[static_cast<std::size_t>(index)].header;
}

void PopupMenuModel::SetHoveredIndex(int index) noexcept {
    hoveredIndex_ = IsSelectable(index) ? index : -1;
}

int PopupMenuModel::FirstSelectableIndex() const noexcept {
    for (std::size_t index = 0; index < items_.size(); ++index) {
        if (!items_[index].separator) return static_cast<int>(index);
    }
    return -1;
}

int PopupMenuModel::ItemHeight(int index, const PopupMenuMetrics& metrics) const noexcept {
    if (index < 0 || static_cast<std::size_t>(index) >= items_.size()) return 0;
    return items_[static_cast<std::size_t>(index)].separator
        ? std::max(0, metrics.separatorHeight) : std::max(0, metrics.itemHeight);
}

int PopupMenuModel::ItemTop(int index, const PopupMenuMetrics& metrics) const noexcept {
    if (index <= 0) return std::max(0, metrics.padding);
    int top = std::max(0, metrics.padding);
    const int capped = std::min(index, static_cast<int>(items_.size()));
    for (int item = 0; item < capped; ++item) {
        const int height = ItemHeight(item, metrics);
        if (top > std::numeric_limits<int>::max() - height) return std::numeric_limits<int>::max();
        top += height;
    }
    return top;
}

int PopupMenuModel::TotalHeight(const PopupMenuMetrics& metrics) const noexcept {
    int height = std::max(0, metrics.padding) * 2;
    for (int index = 0; index < static_cast<int>(items_.size()); ++index) {
        const int itemHeight = ItemHeight(index, metrics);
        if (height > std::numeric_limits<int>::max() - itemHeight) return std::numeric_limits<int>::max();
        height += itemHeight;
    }
    return height;
}

int PopupMenuModel::HitTestY(int y, const PopupMenuMetrics& metrics) const noexcept {
    if (y < std::max(0, metrics.padding)) return -1;
    for (int index = 0; index < static_cast<int>(items_.size()); ++index) {
        const int top = ItemTop(index, metrics);
        const int height = ItemHeight(index, metrics);
        if (y >= top && static_cast<long long>(y) - top < height) {
            return IsSelectable(index) ? index : -1;
        }
    }
    return -1;
}

int PopupMenuModel::MoveHover(int direction) noexcept {
    if (items_.empty() || direction == 0) return hoveredIndex_;
    const int step = direction < 0 ? -1 : 1;
    if (!IsSelectable(hoveredIndex_)) {
        if (step > 0) return hoveredIndex_ = FirstSelectableIndex();
        for (int index = static_cast<int>(items_.size()) - 1; index >= 0; --index) {
            if (IsSelectable(index)) return hoveredIndex_ = index;
        }
        return hoveredIndex_ = -1;
    }
    int index = hoveredIndex_;
    for (std::size_t attempt = 0; attempt < items_.size(); ++attempt) {
        index += step;
        if (index < 0) index = static_cast<int>(items_.size()) - 1;
        if (index >= static_cast<int>(items_.size())) index = 0;
        if (IsSelectable(index)) return hoveredIndex_ = index;
    }
    return hoveredIndex_;
}

int PopupMenuModel::HoveredCommand() const noexcept {
    return IsSelectable(hoveredIndex_)
        ? items_[static_cast<std::size_t>(hoveredIndex_)].command : 0;
}

void AlertPresenter::Show(AlertRequest request) {
    request_ = std::move(request);
    visible_ = true;
    primaryRect_ = {};
    secondaryRect_ = {};
    primaryHot_ = false;
    secondaryHot_ = false;
}

void AlertPresenter::Clear() noexcept {
    request_ = {};
    primaryRect_ = {};
    secondaryRect_ = {};
    visible_ = false;
    primaryHot_ = false;
    secondaryHot_ = false;
}

void AlertPresenter::SetButtonRects(FloatRect primary, FloatRect secondary) noexcept {
    primaryRect_ = primary;
    secondaryRect_ = request_.mode == AlertMode::Confirmation ? secondary : FloatRect{};
}

bool AlertPresenter::UpdateHover(float x, float y) noexcept {
    const bool primary = visible_ && primaryRect_.Contains(x, y);
    const bool secondary = visible_ && request_.mode == AlertMode::Confirmation &&
        secondaryRect_.Contains(x, y);
    const bool changed = primary != primaryHot_ || secondary != secondaryHot_;
    primaryHot_ = primary;
    secondaryHot_ = secondary;
    return changed;
}

AlertHit AlertPresenter::HitTest(float x, float y) const noexcept {
    if (!visible_) return AlertHit::None;
    if (primaryRect_.Contains(x, y)) return AlertHit::Primary;
    if (request_.mode == AlertMode::Confirmation && secondaryRect_.Contains(x, y)) {
        return AlertHit::Secondary;
    }
    return AlertHit::ModalBackdrop;
}

std::optional<std::uint64_t> AlertPresenter::Resolve(bool accepted) noexcept {
    if (!visible_) return std::nullopt;
    const bool dispatch = accepted && request_.mode == AlertMode::Confirmation && request_.actionToken != 0;
    const std::uint64_t token = request_.actionToken;
    Clear();
    return dispatch ? std::optional<std::uint64_t>(token) : std::nullopt;
}

ToastPresenter::ToastPresenter(std::chrono::milliseconds duration) noexcept
    : duration_(std::max(duration, std::chrono::milliseconds(1))) {}

void ToastPresenter::Begin(std::wstring text, TimePoint now) {
    currentText_ = std::move(text);
    startedAt_ = now;
    visible_ = !currentText_.empty();
}

void ToastPresenter::Show(std::wstring text, TimePoint now) {
    if (text.empty()) return;
    if (!visible_) {
        Begin(std::move(text), now);
        return;
    }
    if (text == currentText_) return;
    if (queue_.empty() || queue_.back() != text) queue_.push_back(std::move(text));
}

void ToastPresenter::Clear() noexcept {
    visible_ = false;
    currentText_.clear();
    queue_.clear();
}

bool ToastPresenter::Tick(TimePoint now) {
    if (!visible_) return false;
    if (now - startedAt_ < duration_) return true;
    if (queue_.empty()) {
        visible_ = false;
        currentText_.clear();
        return false;
    }
    std::wstring next = std::move(queue_.front());
    queue_.pop_front();
    Begin(std::move(next), now);
    return visible_;
}

float ToastPresenter::Opacity(TimePoint now) const noexcept {
    if (!visible_) return 0.0f;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startedAt_);
    if (elapsed.count() < 0 || elapsed >= duration_) return 0.0f;
    constexpr auto fadeIn = std::chrono::milliseconds(140);
    constexpr auto fadeOut = std::chrono::milliseconds(240);
    if (elapsed < fadeIn) {
        return std::clamp(static_cast<float>(elapsed.count()) /
            static_cast<float>(fadeIn.count()), 0.0f, 1.0f);
    }
    const auto remaining = duration_ - elapsed;
    if (remaining < fadeOut) {
        return std::clamp(static_cast<float>(remaining.count()) /
            static_cast<float>(fadeOut.count()), 0.0f, 1.0f);
    }
    return 1.0f;
}

} // namespace quicksift::ui::framework
