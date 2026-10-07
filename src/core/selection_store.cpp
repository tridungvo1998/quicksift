// OWNER: Selected-path membership and range-anchor invariants.
#include "selection_store.h"

#include <utility>

namespace quicksift::core {

bool SelectionStore::Select(std::wstring path) {
    if (path.empty()) return false;
    return paths_.insert(std::move(path)).second;
}

bool SelectionStore::Deselect(const std::wstring& path) noexcept {
    try {
        return paths_.erase(path) != 0;
    } catch (...) {
        return false;
    }
}

bool SelectionStore::Toggle(std::wstring path) {
    if (path.empty()) return false;
    if (paths_.erase(path) != 0) return false;
    paths_.insert(std::move(path));
    return true;
}

void SelectionStore::SelectOnly(std::wstring path) {
    std::unordered_set<std::wstring> replacement;
    if (!path.empty()) replacement.insert(std::move(path));
    paths_.swap(replacement);
}

void SelectionStore::Clear(bool releaseMemory) noexcept {
    try {
        if (releaseMemory) {
            std::unordered_set<std::wstring> empty;
            paths_.swap(empty);
        } else {
            paths_.clear();
        }
    } catch (...) {
        // Clearing selection is used during shutdown and memory-pressure recovery.
        // Preserve the existing selection rather than throwing across that boundary.
    }
}

void SelectionStore::Reset(bool releaseMemory) noexcept {
    Clear(releaseMemory);
    anchor_.reset();
}

void SelectionStore::Retain(const std::function<bool(const std::wstring&)>& keep) {
    for (auto iterator = paths_.begin(); iterator != paths_.end();) {
        if (!keep(*iterator)) iterator = paths_.erase(iterator);
        else ++iterator;
    }
}

} // namespace quicksift::core
