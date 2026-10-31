// OWNER: Portable checks that metadata reads always reach a terminal state and
// "metadata pending" reaches 0 under busy decode load, unreadable files and cancellations.
#include "core/metadata_lane_policy.h"
#include "core/metadata_progress.h"
#include "core/metadata_state.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <unordered_map>
#include <vector>

namespace {
int g_checks = 0;
int g_failures = 0;
void Check(bool condition, const char* name) {
    ++g_checks;
    if (!condition) { ++g_failures; std::printf("FAIL: %s\n", name); }
}
using namespace std::chrono_literals;
using namespace quicksift::core;
namespace ml = quicksift::core::metadata_lane;
namespace mp = quicksift::core::metadata_progress;

struct Photo { MetadataKnowledge rating = MetadataKnowledge::Unknown; bool unreadable = false; };

struct SimResult { std::size_t pending = 0; bool allTerminal = true; long ticks = 0; bool counterUnderflow = false; };

// Mirrors PumpMetadataQueue + the metadata lane + OnWorkReady/CompleteMetadataJob.
SimResult Simulate(std::size_t count, bool foregroundAlwaysBusy, bool writeAlwaysPending,
    int cancelEveryNth, int unreadableEveryNth, long maxTicks) {
    std::vector<Photo> photos(count);
    for (std::size_t i = 0; i < count; ++i)
        photos[i].unreadable = unreadableEveryNth > 0 && i % unreadableEveryNth == 0;
    std::deque<std::size_t> queue;
    std::size_t pending = 0;
    for (std::size_t i = 0; i < count; ++i) { queue.push_back(i); ++pending; }
    std::unordered_map<std::size_t, int> attempts;
    std::chrono::milliseconds waited{ 0 };
    long dispatched = 0;
    SimResult out;
    for (long tick = 0; tick < maxTicks && !queue.empty(); ++tick) {
        out.ticks = tick;
        ml::LaneInputs lane;
        lane.foregroundVisualActive = foregroundAlwaysBusy;
        lane.writePending = writeAlwaysPending;
        lane.waited = waited;
        if (!ml::ShouldStartRead(lane)) { waited += 8ms; continue; }
        waited = 0ms;
        const std::size_t index = queue.front();
        queue.pop_front();
        ++dispatched;
        const bool cancelled = cancelEveryNth > 0 && dispatched % cancelEveryNth == 0;
        PhotoMetadataValues values{ UnknownMetadataValue(), UnknownMetadataValue(), UnknownMetadataValue() };
        if (!cancelled) ResolveMetadataReadOutcome(values, !photos[index].unreadable);
        const bool anyUnknown = values.rating.knowledge == MetadataKnowledge::Unknown;
        int& a = attempts[index];
        const auto outcome = mp::Classify(cancelled, false, anyUnknown, a);
        if (outcome == mp::Outcome::Apply) photos[index].rating = values.rating.knowledge;
        else if (outcome == mp::Outcome::Retry) { ++a; queue.push_back(index); ++pending; }
        else photos[index].rating = MetadataKnowledge::Unreadable;
        if (pending == 0) out.counterUnderflow = true;
        else --pending; // CompleteMetadataJob: exactly once per dispatched job
    }
    out.pending = pending;
    for (const Photo& p : photos) if (p.rating == MetadataKnowledge::Unknown) out.allTerminal = false;
    return out;
}
}

int main() {
    // Lane policy.
    Check(ml::ShouldStartRead({}), "idle lane starts immediately");
    Check(!ml::ShouldStartRead({ true, false, false, 0ms }), "yields to an active foreground decode");
    Check(ml::ShouldStartRead({ true, false, false, ml::kMinShareAge }), "aging: starts after min-share age");
    Check(ml::ShouldStartRead({ false, true, false, ml::kMinShareAge }), "aging also beats a queued foreground decode");
    Check(!ml::ShouldStartRead({ false, false, true, 5s }), "writes outrank reads");
    Check(ml::ShouldStartRead({ false, false, true, ml::kWriteYieldCap }), "a stuck write lane cannot starve reads forever");
    Check(!ml::IsForegroundVisual(true, false, false, true), "face analysis is not foreground");
    Check(!ml::IsForegroundVisual(false, false, false, false), "Idle/Predictive prefetch is not foreground");
    Check(ml::IsForegroundVisual(false, false, false, true), "Visible thumbnail decode is foreground");
    Check(ml::IsForegroundVisual(false, true, false, false) && !ml::IsForegroundVisual(false, true, true, false),
        "view lane: current image foreground, neighbour prefetch not");

    // Terminal-state rule.
    Check(mp::Classify(false, false, false, 0) == mp::Outcome::Apply, "clean read applies");
    Check(mp::Classify(true, false, true, 0) == mp::Outcome::Retry, "cancelled read retries, never overwrites");
    Check(mp::Classify(false, true, false, mp::kMaxReadAttempts - 1) == mp::Outcome::MarkUnreadable,
        "retry budget is bounded");
    PhotoMetadataValues v{ UnknownMetadataValue(), UnknownMetadataValue(), UnknownMetadataValue() };
    ResolveMetadataReadOutcome(v, false);
    Check(v.rating.knowledge == MetadataKnowledge::Unreadable && v.pickState.knowledge == MetadataKnowledge::Unreadable,
        "file that cannot be opened is Unreadable, not Unknown");
    PhotoMetadataValues w{ UnknownMetadataValue(), UnknownMetadataValue(), UnknownMetadataValue() };
    ResolveMetadataReadOutcome(w, true);
    Check(w.rating.knowledge == MetadataKnowledge::Known && w.rating.value == 0, "openable file with no value is 0 stars");

    // Simulations: pending must reach 0 and every photo be terminal.
    const long cap = 2'000'000;
    auto busy = Simulate(500, true, false, 0, 0, cap);
    Check(busy.pending == 0 && busy.allTerminal, "constant foreground decode load: pending reaches 0");
    auto unreadable = Simulate(300, false, false, 0, 3, cap);
    Check(unreadable.pending == 0 && unreadable.allTerminal, "unreadable files: pending reaches 0");
    auto cancels = Simulate(300, true, false, 2, 0, cap);
    Check(cancels.pending == 0 && cancels.allTerminal, "every other read cancelled: pending reaches 0");
    auto everything = Simulate(400, true, true, 3, 5, cap);
    Check(everything.pending == 0 && everything.allTerminal && !everything.counterUnderflow,
        "busy decode + stuck writes + cancels + unreadable: pending reaches 0, no underflow");
    auto allCancelled = Simulate(50, false, false, 1, 0, cap);
    Check(allCancelled.pending == 0 && allCancelled.allTerminal, "always-cancelled reads end Unreadable, not pending");

    std::printf("metadata_progress_checks: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
