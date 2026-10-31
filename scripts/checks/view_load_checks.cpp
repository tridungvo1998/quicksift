// Portable checks for the Single View image-load rules (core/view_load_policy.h)
// and the decode-budget liveness properties the view lane depends on
// (core/decode_budget.h). Round 21 (t29u) load-stall audit regressions.
#include "core/decode_budget.h"
#include "core/performance_policy.h"
#include "core/view_load_policy.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

namespace vl = quicksift::core::view_load;
using quicksift::core::DecodeBudget;
using quicksift::core::DecodePriority;
using namespace std::chrono_literals;

namespace {
int gChecks = 0;
int gFailures = 0;

void Check(bool condition, const char* what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL: %s\n", what);
    }
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return predicate();
}

void DecodeCapChecks() {
    Check(vl::EffectiveMaximumDecodeEdge(12288, 0) == 12288, "unknown device limit keeps the profile cap");
    Check(vl::EffectiveMaximumDecodeEdge(12288, 8192) == 8192, "an 8192-texture device caps the 12288 bucket");
    Check(vl::EffectiveMaximumDecodeEdge(8192, 16384) == 8192, "a larger device limit never raises the profile cap");
    Check(vl::EffectiveMaximumDecodeEdge(0, 0) == 1, "degenerate profile cap stays positive");

    Check(vl::SatisfiableViewEdge(3000, 6000, 8192) == 3000, "fit demand below native and cap is unchanged");
    Check(vl::SatisfiableViewEdge(9504, 9504, 8192) == 8192, "zoomed 61 MP RAW on an 8192 profile settles at the cap");
    Check(vl::SatisfiableViewEdge(5000, 4000, 8192) == 4000, "native edge bounds the demand");
    Check(vl::SatisfiableViewEdge(5000, 0, 8192) == 5000, "unknown native edge uses the demand");
}

void FullRequestChecks() {
    Check(vl::ShouldRequestFullViewDecode(false, false), "unsatisfied display with no full result requests a decode");
    Check(!vl::ShouldRequestFullViewDecode(true, false), "a satisfied display never re-requests");
    // The RAW re-decode loop: half-size / pixel-cap output below the display edge.
    Check(!vl::ShouldRequestFullViewDecode(false, true),
        "an exact full-quality result is final even when it is below the display edge");
}

void InteractionEpochChecks() {
    Check(vl::ViewDecodeInteractionEpoch(true, 42) == 0, "view-lane decodes are not cancelled by pan/zoom");
    Check(vl::ViewDecodeInteractionEpoch(false, 42) == 42, "pool/tile decodes keep their interaction epoch");
}

void StaleRunningJobChecks() {
    Check(vl::RunningViewJobIsStaleFor(7, 3, 7, 5), "A -> B -> A: running job of epoch 3 is stale for epoch 5");
    Check(!vl::RunningViewJobIsStaleFor(7, 5, 7, 5), "a repeat paint of the same epoch is absorbed");
    Check(vl::RunningViewJobIsStaleFor(6, 5, 7, 5), "an older folder generation is stale");
    Check(!vl::RunningViewJobIsStaleFor(7, 0, 7, 5), "epoch-less work (prefetch) is never navigation-stale");
    Check(!vl::RunningViewJobIsStaleFor(7, 5, 7, 4), "a newer running job is not stale for an older request");
}

void StallWatchChecks() {
    vl::StallWatch watch(5000ms, 15000ms);
    const auto t0 = std::chrono::steady_clock::time_point{} + 1h;
    Check(!watch.Observe(L"a.jpg", false, t0), "first unsettled paint only starts the clock");
    Check(!watch.Observe(L"a.jpg", false, t0 + 4900ms), "no report before the threshold");
    Check(watch.Observe(L"a.jpg", false, t0 + 5000ms), "report at the threshold");
    Check(!watch.Observe(L"a.jpg", false, t0 + 6000ms), "a reported stall is not repeated on every paint");
    Check(watch.Observe(L"a.jpg", false, t0 + 20000ms), "a continuing stall is reported again after the repeat interval");
    Check(watch.Reports() == 2, "report counter");
    Check(!watch.Observe(L"b.jpg", false, t0 + 21000ms), "navigation restarts the clock");
    Check(!watch.Observe(L"b.jpg", false, t0 + 25000ms), "new image below threshold");
    Check(!watch.Observe(L"b.jpg", true, t0 + 27000ms), "settling clears the watch");
    Check(watch.StalledFor(t0 + 27000ms).count() == 0, "settled image reports no stall time");
    Check(!watch.Observe(L"b.jpg", false, t0 + 28000ms), "unsettled again restarts the clock");
    Check(!watch.Observe(L"b.jpg", false, t0 + 32000ms), "restarted clock below threshold");
    Check(watch.Observe(L"b.jpg", false, t0 + 33000ms), "restarted clock reports at its own threshold");
}

void DecodeBudgetChecks() {
    Check(quicksift::core::PerformancePolicy{}.allowOversizedDecode,
        "policy default admits oversized decodes (otherwise they wait until cancelled)");
    {
        auto budget = std::make_shared<DecodeBudget>(100, 4);
        auto oversized = budget->Acquire(500, DecodePriority::Interactive);
        Check(oversized != nullptr, "an oversized reservation is admitted when nothing is in flight");
        Check(budget->OversizedActive(), "oversized reservation is exclusive");
        Check(budget->TryAcquire(10) == nullptr, "nothing else is admitted beside an oversized reservation");
        oversized.reset();
        Check(budget->BytesInFlightForTesting() == 0 && budget->ActiveReservationsForTesting() == 0,
            "releasing the oversized reservation leaks nothing");
    }
    {
        // An oversized waiter is admitted once the budget drains (no lost wakeup).
        auto budget = std::make_shared<DecodeBudget>(100, 4);
        auto held = budget->Acquire(40, DecodePriority::Visible);
        std::atomic<bool> admitted{ false };
        std::thread waiter([&] {
            auto reservation = budget->Acquire(500, DecodePriority::Interactive);
            admitted.store(reservation != nullptr);
        });
        std::this_thread::sleep_for(60ms);
        Check(!admitted.load(), "oversized waiter waits while bytes are in flight");
        held.reset();
        Check(WaitUntil([&] { return admitted.load(); }, 2000ms), "oversized waiter is admitted after the drain");
        waiter.join();
        Check(budget->BytesInFlightForTesting() == 0, "oversized waiter released its reservation");
    }
    {
        // A cancelled waiter returns without a reservation and leaves no state behind.
        auto budget = std::make_shared<DecodeBudget>(100, 4);
        auto held = budget->Acquire(80, DecodePriority::Visible);
        std::atomic<bool> cancel{ false };
        std::atomic<bool> returned{ false };
        std::atomic<bool> gotReservation{ true };
        std::thread waiter([&] {
            auto reservation = budget->Acquire(50, DecodePriority::Interactive,
                [&] { return cancel.load(); });
            gotReservation.store(reservation != nullptr);
            returned.store(true);
        });
        Check(WaitUntil([&] { return budget->PendingWaitersForTesting() == 1; }, 1000ms), "waiter registered");
        cancel.store(true);
        Check(WaitUntil([&] { return returned.load(); }, 1000ms), "a cancelled waiter returns promptly");
        waiter.join();
        Check(!gotReservation.load(), "a cancelled waiter gets no reservation");
        Check(budget->PendingWaitersForTesting() == 0, "a cancelled waiter is removed");
        Check(budget->BytesInFlightForTesting() == 80, "a cancelled waiter does not change the bytes in flight");
        held.reset();
        Check(budget->BytesInFlightForTesting() == 0 && budget->ActiveReservationsForTesting() == 0,
            "no permit or byte leak after cancellation");
    }
    {
        // ShrinkTo returns the unused part of a reservation immediately.
        auto budget = std::make_shared<DecodeBudget>(100, 4);
        auto reservation = budget->Acquire(90, DecodePriority::Visible);
        reservation->ShrinkTo(30);
        Check(budget->BytesInFlightForTesting() == 30, "ShrinkTo releases bytes");
        Check(budget->TryAcquire(60) != nullptr, "released bytes are reusable");
    }
}
} // namespace

int main() {
    DecodeCapChecks();
    FullRequestChecks();
    InteractionEpochChecks();
    StaleRunningJobChecks();
    StallWatchChecks();
    DecodeBudgetChecks();
    std::printf("view_load_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
