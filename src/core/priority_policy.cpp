// OWNER: Priority model decisions (see priority_policy.h).
#include "core/priority_policy.h"

namespace quicksift::core::priority {

std::wstring_view TierName(Tier tier) noexcept {
    switch (tier) {
    case Tier::Ui: return L"P0-ui";
    case Tier::MetadataWrite: return L"P1-metadata-write";
    case Tier::ViewLoad: return L"P2-view-load";
    case Tier::SearchRead: return L"P3-search-read";
    case Tier::VisibleThumb: return L"P4-visible-thumbnail";
    case Tier::Background: return L"P5-background";
    }
    return L"unknown";
}

Tier HighestDemand(const DemandSnapshot& demand) noexcept {
    if (demand.metadataWritesPending != 0 || demand.interactiveWritePending) return Tier::MetadataWrite;
    if (demand.viewForeground != 0) return Tier::ViewLoad;
    if (demand.searchActive || demand.searchFocused) return Tier::SearchRead;
    if (demand.thumbnailViewportActive) return Tier::VisibleThumb;
    return Tier::Background;
}

bool ShouldYield(Tier tier, const DemandSnapshot& demand) noexcept {
    switch (tier) {
    case Tier::Ui:
    case Tier::MetadataWrite:
    case Tier::ViewLoad:
        return false;
    case Tier::SearchRead:
        // Search reads give way to writes and to the image the user is waiting for.
        return demand.metadataWritesPending != 0 || demand.interactiveWritePending ||
            demand.viewForeground != 0;
    case Tier::VisibleThumb:
        return demand.metadataWritesPending != 0 || demand.interactiveWritePending ||
            demand.viewForeground != 0;
    case Tier::Background:
        return demand.metadataWritesPending != 0 || demand.interactiveWritePending ||
            demand.viewForeground != 0 || demand.searchFocused ||
            demand.searchActive;
    }
    return false;
}

bool PoolLaneAdmitted(PoolLane lane, bool viewForegroundActive,
    unsigned activeThumbnailJobs) noexcept {
    if (!viewForegroundActive) return true;
    switch (lane) {
    case PoolLane::Interactive: return true;
    case PoolLane::Visible: return activeThumbnailJobs < kPoolCapDuringViewLoad;
    case PoolLane::Predictive:
    case PoolLane::Face:
    case PoolLane::Idle:
    case PoolLane::Analysis:
        return false;
    }
    return false;
}

std::size_t SearchHydrateBatch(const DemandSnapshot& demand) noexcept {
    if (demand.metadataWritesPending != 0 || demand.interactiveWritePending ||
        demand.viewForeground != 0) return 4;
    return demand.searchFocused ? 16 : 32;
}

std::chrono::milliseconds StarvationThreshold(Tier tier) noexcept {
    switch (tier) {
    case Tier::Ui: return std::chrono::milliseconds(0);
    case Tier::MetadataWrite: return std::chrono::milliseconds(2000);
    case Tier::ViewLoad: return std::chrono::milliseconds(3000);
    case Tier::SearchRead: return std::chrono::milliseconds(8000);
    case Tier::VisibleThumb: return std::chrono::milliseconds(5000);
    case Tier::Background: return std::chrono::milliseconds(60000);
    }
    return std::chrono::milliseconds(60000);
}

void StarvationTracker::NoteWaiting(Tier tier, Clock::time_point now) noexcept {
    const auto index = static_cast<std::size_t>(tier);
    if (index >= kTierCount) return;
    if (waitingSince_[index] == Clock::time_point{}) {
        waitingSince_[index] = now;
        warned_[index] = false;
    }
}

void StarvationTracker::NoteServed(Tier tier) noexcept {
    const auto index = static_cast<std::size_t>(tier);
    if (index >= kTierCount) return;
    waitingSince_[index] = Clock::time_point{};
    warned_[index] = false;
}

bool StarvationTracker::TakeStarvationWarning(Tier tier, Clock::time_point now,
    std::chrono::milliseconds* waited) noexcept {
    const auto index = static_cast<std::size_t>(tier);
    if (index >= kTierCount || tier == Tier::Ui) return false;
    if (waitingSince_[index] == Clock::time_point{} || warned_[index]) return false;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - waitingSince_[index]);
    if (elapsed < StarvationThreshold(tier)) return false;
    warned_[index] = true;
    if (waited) *waited = elapsed;
    return true;
}

DemandSnapshot ForegroundDemand::Snapshot() const noexcept {
    DemandSnapshot snapshot;
    snapshot.metadataWritesPending = writes_.load(std::memory_order_acquire);
    snapshot.interactiveWritePending = interactiveWrite_.load(std::memory_order_acquire);
    snapshot.viewForeground = view_.load(std::memory_order_acquire);
    snapshot.searchActive = searchActive_.load(std::memory_order_acquire);
    snapshot.searchFocused = searchFocused_.load(std::memory_order_acquire);
    snapshot.thumbnailViewportActive = viewport_.load(std::memory_order_acquire);
    return snapshot;
}

ForegroundDemand& Demand() noexcept {
    static ForegroundDemand demand;
    return demand;
}

} // namespace quicksift::core::priority
