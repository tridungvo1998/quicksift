// OWNER: Per-item metadata transaction execution, the write-lane lock-resolution
// retry loop, nested interactive (urgent) edits, and partial-completion accounting.
#include "metadata_transaction_service.h"

#include <algorithm>
#include <cwctype>
#include <limits>

namespace fs = std::filesystem;

namespace quicksift::transactions {
namespace wl = quicksift::core::write_lane;

namespace {
bool PathsEqualInsensitive(const fs::path& leftPath, const fs::path& rightPath) {
    const std::wstring left = leftPath.wstring();
    const std::wstring right = rightPath.wstring();
    return left.size() == right.size() && std::equal(left.begin(), left.end(),
        right.begin(), [](wchar_t a, wchar_t b) {
            return std::towlower(static_cast<wint_t>(a)) ==
                std::towlower(static_cast<wint_t>(b));
        });
}

const quicksift::core::MetadataSnapshot& StorageSnapshotOf(const MetadataChange& change,
    MetadataTransactionMode mode) noexcept {
    return mode == MetadataTransactionMode::UndoHistory ? change.before : change.after;
}

bool PlanDisjointFrom(const MetadataTransactionPlan& plan,
    const std::vector<fs::path>& gatedPaths) {
    for (const MetadataChange& change : plan.changes) {
        const fs::path& path = StorageSnapshotOf(change, plan.mode).path;
        for (const fs::path& gated : gatedPaths)
            if (PathsEqualInsensitive(path, gated)) return false;
    }
    return true;
}
} // namespace

void MetadataTransactionService::ServiceUrgentNested(
    const MetadataTransactionCallbacks& callbacks,
    const std::vector<fs::path>& gatedPaths) {
    std::optional<MetadataTransactionPlan> urgent;
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || !urgentPlan_) return;
        // Same-path edits must wait for the outer plan: they would race its CAS
        // expectations. Run() picks them up first once the outer plan completes.
        if (!PlanDisjointFrom(*urgentPlan_, gatedPaths)) return;
        urgent.emplace(std::move(*urgentPlan_));
        urgentPlan_.reset();
    }
    const std::size_t savedCompleted = progressCompleted_.load(std::memory_order_acquire);
    const std::size_t savedTotal = progressTotal_.load(std::memory_order_acquire);
    MetadataTransactionResult nested;
    nested.urgent = true;
    try {
        Execute(std::move(*urgent), callbacks, nested, false);
    } catch (...) {
        nested.failed = true;
        const std::size_t incomplete = static_cast<std::size_t>(std::count(
            nested.completed.begin(), nested.completed.end(), false));
        nested.failureCount = std::max(nested.failureCount, incomplete);
    }
    progressTotal_.store(savedTotal, std::memory_order_release);
    progressCompleted_.store(savedCompleted, std::memory_order_release);
    PublishResult(std::move(nested), false);
}

MetadataWriteOutcome MetadataTransactionService::WriteWithRetry(
    const MetadataTransactionCallbacks& callbacks,
    const quicksift::core::MetadataSnapshot& storage,
    const quicksift::core::MetadataPatch& patch, unsigned& attempts,
    const std::vector<fs::path>* gatedPaths, bool allowNestedUrgent) {
    const auto start = std::chrono::steady_clock::now();
    const unsigned divisor = std::max(1u, callbacks.retryTimeDivisor);
    wl::WriteRetryState retry;
    bool waitingPublished = false;
    const auto clearWaiting = [&]() noexcept {
        if (!waitingPublished) return;
        waitingPublished = false;
        waitingExternal_.store(false, std::memory_order_release);
        try {
            std::lock_guard lock(mutex_);
            waitingPath_.clear();
        } catch (...) {}
    };

    for (;;) {
        MetadataWriteOutcome outcome;
        try {
            outcome = callbacks.write(storage.path, patch, storage.storage,
                storage.jpeg, storage.safeJpegWrite);
        } catch (...) {
            outcome = MetadataWriteOutcome{};
            outcome.failureKind = wl::WriteFailureKind::Other;
            try {
                outcome.detail = L"A metadata write raised an exception; the affected file was not marked complete.";
            } catch (...) {}
        }
        ++attempts;
        if (!outcome.ok) {
            if (outcome.conflict) outcome.failureKind = wl::WriteFailureKind::Conflict;
            else if (outcome.failureKind == wl::WriteFailureKind::None)
                outcome.failureKind = wl::WriteFailureKind::Other;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start) * divisor;
        // Hard timeout: abort this write and release the exclusive path gate.
        // Returning here lets the caller drop the path gate; the file lock is
        // not held past this point. Also signal cancellation so any nested
        // wait exits immediately.
        if (elapsed >= wl::kHardTimeout) {
            cancellationRequested_.store(true, std::memory_order_release);
            clearWaiting();
            outcome.ok = false;
            outcome.failureKind = wl::WriteFailureKind::InUse;
            try {
                outcome.detail = L"Metadata write timed out; file lock released.";
            } catch (...) {}
            return outcome;
        }
        const wl::RetryDecision decision = retry.OnAttempt(
            outcome.ok ? wl::WriteFailureKind::None : outcome.failureKind, elapsed);
        if (decision.action != wl::RetryAction::RetryAfter) {
            if (!outcome.ok && callbacks.onRetry && attempts > 1) {
                try {
                    callbacks.onRetry(storage.path, attempts, outcome.failureKind,
                        outcome.win32Error, elapsed, retry.Phase() == wl::RetryPhase::External);
                } catch (...) {}
            }
            clearWaiting();
            return outcome;
        }
        const bool external = decision.phase == wl::RetryPhase::External;
        if (callbacks.onRetry) {
            try {
                callbacks.onRetry(storage.path, attempts, outcome.failureKind,
                    outcome.win32Error, elapsed, external);
            } catch (...) {}
        }
        try {
            std::lock_guard lock(mutex_);
            if (!waitingPublished) {
                waitingPath_ = storage.path.wstring();
                waitingSince_ = start;
                ++waitingSerial_;
                waitingPublished = true;
            }
        } catch (...) {}
        if (external) waitingExternal_.store(true, std::memory_order_release);

        const auto delay = std::max(std::chrono::milliseconds(1), decision.delay / divisor);
        bool stop = false;
        bool skip = false;
        try {
            std::unique_lock lock(mutex_);
            condition_.wait_for(lock, delay, [&] {
                return stopping_ || cancellationRequested_.load(std::memory_order_acquire) ||
                    skipWaitRequested_.load(std::memory_order_acquire) ||
                    (allowNestedUrgent && gatedPaths && urgentPlan_ &&
                        PlanDisjointFrom(*urgentPlan_, *gatedPaths));
            });
            stop = stopping_ || cancellationRequested_.load(std::memory_order_acquire);
        } catch (...) {
            stop = true;
        }
        skip = skipWaitRequested_.exchange(false, std::memory_order_acq_rel);
        if (stop || skip) {
            clearWaiting();
            outcome.ok = false;
            outcome.conflict = false;
            outcome.failureKind = wl::WriteFailureKind::InUse;
            try {
                outcome.detail = stop ?
                    L"The metadata batch was cancelled while waiting for a file to be released." :
                    L"Skipped while waiting for the file to be released by another app.";
            } catch (...) {}
            return outcome;
        }
        // A rating typed while we wait for a locked file runs now (disjoint paths).
        if (allowNestedUrgent && gatedPaths) ServiceUrgentNested(callbacks, *gatedPaths);
    }
}

void MetadataTransactionService::Execute(MetadataTransactionPlan&& plan,
    const MetadataTransactionCallbacks& callbacks,
    MetadataTransactionResult& result, bool allowNestedUrgent) {
    // Move all authoritative plan state into the result before any write can
    // occur. If a later allocation or diagnostic operation throws, Run() can
    // publish this same object with its truthful partial completion bitmap.
    result.token = plan.token;
    result.label = std::move(plan.label);
    result.mode = plan.mode;
    result.recordHistory = plan.recordHistory;
    result.initialFailureCount = plan.initialFailures;
    result.failureCount = plan.initialFailures;
    result.failed = plan.initialFailures != 0;
    result.detail = std::move(plan.initialDetail);
    result.changes = std::move(plan.changes);
    result.completed.assign(result.changes.size(), false);
    progressCompleted_.store(0, std::memory_order_relaxed);
    progressTotal_.store(result.changes.size(), std::memory_order_release);

    // Cumulative free-space preflight runs here, not on the UI thread.
    if (callbacks.preflight && !result.changes.empty()) {
        std::vector<quicksift::core::MetadataSnapshot> targets;
        targets.reserve(result.changes.size());
        for (const MetadataChange& change : result.changes)
            targets.push_back(StorageSnapshotOf(change, result.mode));
        std::optional<MetadataStorageShortfall> shortfall;
        try { shortfall = callbacks.preflight(targets); }
        catch (...) { shortfall.reset(); }
        if (shortfall) {
            result.failed = true;
            result.failureCount += result.changes.size();
            for (MetadataChange& change : result.changes)
                change.failureKind = wl::WriteFailureKind::LowStorage;
            result.storageShortfall = std::move(shortfall);
            return;
        }
    }

    std::vector<fs::path> gatedPaths;
    gatedPaths.reserve(result.changes.size());
    for (const MetadataChange& change : result.changes) {
        const fs::path& path = StorageSnapshotOf(change, result.mode).path;
        const bool duplicate = std::any_of(gatedPaths.begin(), gatedPaths.end(),
            [&](const fs::path& existing) { return PathsEqualInsensitive(existing, path); });
        if (!duplicate) gatedPaths.push_back(path);
    }

    bool gateAcquired = gatedPaths.empty();
    if (!gateAcquired) {
        std::wstring gateDetail;
        gateAcquired = callbacks.beginExclusive(gatedPaths, gateDetail);
        if (!gateAcquired) {
            result.failed = true;
            result.failureCount += result.changes.size();
            if (!gateDetail.empty()) result.detail = std::move(gateDetail);
            else if (result.detail.empty()) {
                result.detail = L"Active decoders could not be released for the metadata batch.";
            }
            return;
        }
    }
    struct GateGuard {
        const MetadataTransactionCallbacks* callbacks = nullptr;
        const std::vector<fs::path>* paths = nullptr;
        ~GateGuard() noexcept {
            if (!callbacks || !paths || paths->empty()) return;
            try {
                callbacks->endExclusive(*paths);
            } catch (...) {
                // Decoder release is a cleanup boundary and must never terminate
                // the transaction thread while another exception is unwinding.
            }
        }
    } gateGuard{ &callbacks, &gatedPaths };

    for (std::size_t index = 0; index < result.changes.size(); ++index) {
        progressCompleted_.store(index, std::memory_order_release);
        // File boundary: let an interactive edit on other files run first.
        if (allowNestedUrgent) ServiceUrgentNested(callbacks, gatedPaths);
        if (cancellationRequested_.load(std::memory_order_acquire)) {
            result.cancelled = true;
            if (result.detail.empty()) {
                result.detail = L"The metadata batch was cancelled at a safe file boundary.";
            }
            break;
        }
        MetadataChange& change = result.changes[index];
        const quicksift::core::MetadataSnapshot& storageSnapshot =
            StorageSnapshotOf(change, result.mode);
        quicksift::core::MetadataPatch patch;
        if (result.mode == MetadataTransactionMode::Apply) {
            patch = change.patch;
        } else if (result.mode == MetadataTransactionMode::UndoHistory) {
            patch = quicksift::core::MakeReplayPatch(change.after.values, change.before.values);
        } else {
            patch = quicksift::core::MakeReplayPatch(change.before.values, change.after.values);
        }
        if (patch.Empty()) {
            // A no-op or invalid replay item is complete without touching storage.
            change.resultingValues = result.mode == MetadataTransactionMode::UndoHistory ?
                change.before.values : change.after.values;
            change.hasResultingValues = true;
            result.completed[index] = true;
            continue;
        }

        unsigned attempts = 0;
        MetadataWriteOutcome outcome = WriteWithRetry(callbacks, storageSnapshot, patch,
            attempts, &gatedPaths, allowNestedUrgent);
        change.attempts = attempts;
        if (!outcome.ok) {
            result.failed = true;
            ++result.failureCount;
            change.failureKind = outcome.failureKind;
            change.win32Error = outcome.win32Error;
            change.externalConflict = outcome.conflict;
            if (outcome.conflict) ++result.conflictCount;
            if (wl::IsTransient(outcome.failureKind)) ++result.inUseCount;
            if (result.detail.empty()) {
                try { result.detail = std::move(outcome.detail); } catch (...) {}
            }
            if (cancellationRequested_.load(std::memory_order_acquire)) {
                result.cancelled = true;
                break;
            }
            continue;
        }
        change.resultingValues = outcome.after;
        change.hasResultingValues = true;
        if (result.mode == MetadataTransactionMode::Apply) {
            change.before.values = outcome.before;
            change.after.values = outcome.after;
        }
        if (callbacks.stamps) {
            try { change.stamps = callbacks.stamps(storageSnapshot.path); }
            catch (...) { change.stamps = MetadataChange::FileStamps{}; }
        }
        result.completed[index] = true;
    }
    progressCompleted_.store(result.changes.size(), std::memory_order_release);
}

} // namespace quicksift::transactions
