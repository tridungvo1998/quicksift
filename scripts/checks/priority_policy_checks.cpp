// OWNER: Deterministic, portable checks for the P0..P5 priority model: who yields to
// whom, pool admission while Single/Compare decodes, search hydration batch sizing,
// starvation warnings, the shared demand signal, the incremental snapshot budget and
// the diagnostic rate limiter.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Wpedantic -Wshadow -Werror -Isrc
//       scripts/checks/priority_policy_checks.cpp /tmp/qs-build/libQuickSiftCore.a -pthread

#include "core/log_rate_limiter.h"
#include "core/priority_policy.h"

#include <cstdio>
#include <thread>
#include <vector>

namespace {

using namespace quicksift::core;
using namespace quicksift::core::priority;
using namespace std::chrono_literals;

int gFailures = 0;
int gChecks = 0;

void Check(bool condition, const char* label) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL: %s\n", label);
    }
}

void CheckOrdering() {
    DemandSnapshot idle;
    Check(HighestDemand(idle) == Tier::Background, "idle -> background");
    for (const Tier tier : { Tier::Ui, Tier::MetadataWrite, Tier::ViewLoad, Tier::SearchRead,
            Tier::VisibleThumb, Tier::Background })
        Check(!ShouldYield(tier, idle), "nobody yields when nothing is waiting");

    DemandSnapshot write;
    write.metadataWritesPending = 1;
    Check(HighestDemand(write) == Tier::MetadataWrite, "writes are the top non-UI tier");
    Check(!ShouldYield(Tier::Ui, write) && !ShouldYield(Tier::MetadataWrite, write),
        "UI and writes never yield");
    Check(!ShouldYield(Tier::ViewLoad, write),
        "view loads do not yield globally to writes (only the same path is gated)");
    Check(ShouldYield(Tier::SearchRead, write) && ShouldYield(Tier::VisibleThumb, write) &&
        ShouldYield(Tier::Background, write), "P3..P5 yield to a pending write");

    DemandSnapshot view;
    view.viewForeground = 1;
    Check(HighestDemand(view) == Tier::ViewLoad, "view decode is P2");
    Check(ShouldYield(Tier::SearchRead, view) && ShouldYield(Tier::VisibleThumb, view) &&
        ShouldYield(Tier::Background, view), "search, thumbnails and background yield to the view");

    DemandSnapshot search;
    search.searchFocused = true;
    Check(HighestDemand(search) == Tier::SearchRead, "focused search box is P3");
    Check(!ShouldYield(Tier::SearchRead, search) && !ShouldYield(Tier::VisibleThumb, search),
        "search does not stall visible thumbnails");
    Check(ShouldYield(Tier::Background, search), "face / sharpness / cache maintenance pause during search");
    DemandSnapshot query;
    query.searchActive = true;
    Check(ShouldYield(Tier::Background, query), "applied query also pauses background work");

    DemandSnapshot interactive;
    interactive.interactiveWritePending = true;
    Check(HighestDemand(interactive) == Tier::MetadataWrite && ShouldYield(Tier::SearchRead, interactive),
        "a queued interactive edit counts as write demand");
    Check(TierName(Tier::ViewLoad) == L"P2-view-load", "tier names for logs");
}

void CheckPoolAdmission() {
    Check(PoolLaneAdmitted(PoolLane::Predictive, false, 5), "no view load: everything admitted");
    Check(PoolLaneAdmitted(PoolLane::Interactive, true, 9), "interactive always admitted");
    Check(PoolLaneAdmitted(PoolLane::Visible, true, 0), "first visible thumbnail admitted during view load");
    Check(!PoolLaneAdmitted(PoolLane::Visible, true, kPoolCapDuringViewLoad),
        "thumbnail pool capped at 1 during view load");
    for (const PoolLane lane : { PoolLane::Predictive, PoolLane::Face, PoolLane::Idle, PoolLane::Analysis })
        Check(!PoolLaneAdmitted(lane, true, 0), "prefetch / face / idle / analysis pause during view load");
}

void CheckSearchBatch() {
    DemandSnapshot demand;
    Check(SearchHydrateBatch(demand) == 32, "background hydration batch");
    demand.searchFocused = true;
    Check(SearchHydrateBatch(demand) == 16, "smaller batch while typing");
    demand.viewForeground = 1;
    Check(SearchHydrateBatch(demand) == 4, "tiny batch while the view decodes");
}

void CheckStarvation() {
    StarvationTracker tracker;
    const auto t0 = StarvationTracker::Clock::now();
    tracker.NoteWaiting(Tier::VisibleThumb, t0);
    Check(!tracker.TakeStarvationWarning(Tier::VisibleThumb, t0 + 1s), "short wait is fine");
    std::chrono::milliseconds waited{ 0 };
    Check(tracker.TakeStarvationWarning(Tier::VisibleThumb, t0 + 6s, &waited) && waited >= 5s,
        "long wait warns");
    Check(!tracker.TakeStarvationWarning(Tier::VisibleThumb, t0 + 9s), "warns once per episode");
    tracker.NoteWaiting(Tier::VisibleThumb, t0 + 10s);
    Check(!tracker.TakeStarvationWarning(Tier::VisibleThumb, t0 + 20s),
        "re-noting does not restart the episode start (still warned)");
    tracker.NoteServed(Tier::VisibleThumb);
    tracker.NoteWaiting(Tier::VisibleThumb, t0 + 30s);
    Check(tracker.TakeStarvationWarning(Tier::VisibleThumb, t0 + 36s), "new episode warns again");
    Check(!tracker.TakeStarvationWarning(Tier::Ui, t0 + 100s), "UI has no queue");
    Check(StarvationThreshold(Tier::MetadataWrite) < StarvationThreshold(Tier::Background),
        "higher tiers starve sooner");
}

void CheckDemandSignal() {
    ForegroundDemand demand;
    demand.SetViewForeground(2);
    demand.SetSearch(true, false);
    const DemandSnapshot snapshot = demand.Snapshot();
    Check(snapshot.viewForeground == 2 && snapshot.searchActive && !snapshot.searchFocused, "snapshot");
    const auto epoch = demand.Epoch();
    demand.Touch();
    Check(demand.Epoch() == epoch + 1, "epoch bump");
    // Concurrent writers / readers (wait-free).
    std::vector<std::thread> threads;
    for (int index = 0; index < 4; ++index)
        threads.emplace_back([&demand, index] {
            for (int step = 0; step < 2000; ++step) {
                demand.SetMetadataWritesPending(static_cast<unsigned>(step & 1));
                (void)demand.Snapshot();
                if (index == 0) demand.Touch();
            }
        });
    for (std::thread& thread : threads) thread.join();
    Check(demand.Epoch() == epoch + 1 + 2000, "concurrent touches counted");
    Check(&Demand() == &Demand(), "single process instance");
}

void CheckRateLimiter() {
    LogRateLimiter limiter(3, 1000ms);
    std::uint64_t dropped = 0;
    int allowed = 0;
    for (int index = 0; index < 10; ++index)
        if (limiter.Allow(5000, &dropped)) ++allowed;
    Check(allowed == 3, "burst of 3 per window");
    Check(limiter.Allow(6100, &dropped) && dropped == 7, "next window reports suppressed count");
    Check(limiter.Allow(6200, &dropped) && dropped == 0, "suppressed count reset");
}

} // namespace

int main() {
    CheckOrdering();
    CheckPoolAdmission();
    CheckSearchBatch();
    CheckStarvation();
    CheckDemandSignal();
    CheckRateLimiter();
    std::printf("priority_policy_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
