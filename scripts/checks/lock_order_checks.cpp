// Portable checks for the debug lock-order helper (core/lock_order.h).
// Built with QS_DEBUG_LOCK_ORDER so the checker is active.
#define QS_DEBUG_LOCK_ORDER 1
#include "core/lock_order.h"

#include <cstdio>
#include <thread>

namespace lo = quicksift::core::lock_order;

namespace {
int gChecks = 0;
int gFailures = 0;
int gViolations = 0;
lo::Rank gLastHeld{};
lo::Rank gLastAcquiring{};

void Check(bool condition, const char* what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL: %s\n", what);
    }
}

void Record(lo::Rank held, lo::Rank acquiring) noexcept {
    ++gViolations;
    gLastHeld = held;
    gLastAcquiring = acquiring;
}
} // namespace

int main() {
    lo::SetViolationHandler(&Record);
    {
        QS_LOCK_ORDER(lo::Rank::CacheDatabase);
        QS_LOCK_ORDER(lo::Rank::CacheDeferred);
        QS_LOCK_ORDER(lo::Rank::LogState);
        Check(lo::HeldDepth() == 3, "nested scopes are tracked");
    }
    Check(gViolations == 0, "increasing ranks are accepted");
    Check(lo::HeldDepth() == 0, "scopes unwind");
    {
        QS_LOCK_ORDER(lo::Rank::LogStream);
        QS_LOCK_ORDER(lo::Rank::LogState);
    }
    Check(gViolations == 0, "log streamMutex -> state mutex is the documented order");
    {
        QS_LOCK_ORDER(lo::Rank::LogState);
        QS_LOCK_ORDER(lo::Rank::LogStream); // inversion
    }
    Check(gViolations == 1, "inversion is reported");
    Check(gLastHeld == lo::Rank::LogState && gLastAcquiring == lo::Rank::LogStream,
        "inversion reports both ranks");
    {
        QS_LOCK_ORDER(lo::Rank::CacheDatabase);
        QS_LOCK_ORDER(lo::Rank::CacheDatabase); // recursive acquisition of a non-recursive mutex
    }
    Check(gViolations == 2, "re-acquiring the same rank is reported");
    {
        QS_LOCK_ORDER(lo::Rank::WorkerQueue);
        int otherThreadDepth = -1;
        std::thread([&otherThreadDepth] { otherThreadDepth = lo::HeldDepth(); }).join();
        Check(otherThreadDepth == 0, "held ranks are per thread");
    }
    {
        // Sequential (not nested) acquisition in any order is fine.
        { QS_LOCK_ORDER(lo::Rank::WorkerQueue); }
        { QS_LOCK_ORDER(lo::Rank::WorkerBlocked); }
    }
    Check(gViolations == 2, "sequential acquisition is not an inversion");
    std::printf("lock_order_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
