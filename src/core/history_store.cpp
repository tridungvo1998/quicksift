// OWNER: Bounded Undo/Redo retention, replay transitions, and memory accounting.
#include "history_store.h"

#include <utility>

namespace quicksift::core {

const HistoryEntry* HistoryStore::PeekUndo() const noexcept {
    return undo_.empty() ? nullptr : &undo_.back();
}

const HistoryEntry* HistoryStore::PeekRedo() const noexcept {
    return redo_.empty() ? nullptr : &redo_.back();
}

std::optional<HistoryEntry> HistoryStore::TakeUndo() {
    if (undo_.empty()) return std::nullopt;
    HistoryEntry entry = std::move(undo_.back());
    undo_.pop_back();
    return entry;
}

std::optional<HistoryEntry> HistoryStore::TakeRedo() {
    if (redo_.empty()) return std::nullopt;
    HistoryEntry entry = std::move(redo_.back());
    redo_.pop_back();
    return entry;
}

void HistoryStore::Restore(HistoryDirection direction, HistoryEntry entry) {
    if (EmptyEntry(entry)) return;
    PrepareEntry(entry);
    if (direction == HistoryDirection::Undo) undo_.push_back(std::move(entry));
    else redo_.push_back(std::move(entry));
    Trim();
}

void HistoryStore::CompleteReplay(HistoryDirection direction,
    HistoryEntry completed, HistoryEntry remaining) {
    if (!EmptyEntry(completed)) PrepareEntry(completed);
    if (!EmptyEntry(remaining)) PrepareEntry(remaining);
    if (direction == HistoryDirection::Undo) {
        if (!EmptyEntry(completed)) redo_.push_back(std::move(completed));
        if (!EmptyEntry(remaining)) undo_.push_back(std::move(remaining));
    } else {
        if (!EmptyEntry(completed)) undo_.push_back(std::move(completed));
        if (!EmptyEntry(remaining)) redo_.push_back(std::move(remaining));
    }
    Trim();
}

bool HistoryStore::RecordNew(HistoryEntry entry) {
    if (EmptyEntry(entry)) return false;
    PrepareEntry(entry);
    redo_.clear();
    if (entry.estimatedBytes > memoryLimit_) return false;
    undo_.push_back(std::move(entry));
    Trim();
    return true;
}

void HistoryStore::PrepareEntry(HistoryEntry& entry) {
    if (entry.estimatedBytes == 0) entry.estimatedBytes = EstimateEntryBytes(entry);
}

bool HistoryStore::EmptyEntry(const HistoryEntry& entry) noexcept {
    return entry.kind == HistoryEntry::Kind::Metadata ?
        entry.before.empty() || entry.after.empty() : entry.files.empty();
}


} // namespace quicksift::core
