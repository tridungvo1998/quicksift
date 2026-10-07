// OWNER: Selected-path membership and range-anchor invariants.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

namespace quicksift::core {

class SelectionStore {
public:
    [[nodiscard]] bool Empty() const noexcept { return paths_.empty(); }
    [[nodiscard]] std::size_t Size() const noexcept { return paths_.size(); }
    [[nodiscard]] bool Contains(const std::wstring& path) const {
        return paths_.contains(path);
    }
    [[nodiscard]] const std::unordered_set<std::wstring>& Paths() const noexcept {
        return paths_;
    }
    [[nodiscard]] const std::wstring* First() const noexcept {
        return paths_.empty() ? nullptr : std::addressof(*paths_.begin());
    }

    bool Select(std::wstring path);
    bool Deselect(const std::wstring& path) noexcept;
    bool Toggle(std::wstring path);
    void SelectOnly(std::wstring path);
    void Clear(bool releaseMemory = false) noexcept;
    void Reset(bool releaseMemory = false) noexcept;
    void Retain(const std::function<bool(const std::wstring&)>& keep);

    [[nodiscard]] std::optional<std::size_t> Anchor() const noexcept { return anchor_; }
    void SetAnchor(std::size_t index) noexcept { anchor_ = index; }
    void ClearAnchor() noexcept { anchor_.reset(); }

private:
    std::unordered_set<std::wstring> paths_;
    std::optional<std::size_t> anchor_;
};

} // namespace quicksift::core
