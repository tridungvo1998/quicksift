// OWNER: Per-item metadata transaction execution and partial-completion accounting.
#include "metadata_transaction_service.h"

#include <algorithm>
#include <cwctype>
#include <limits>

namespace fs = std::filesystem;

namespace quicksift::transactions {

void MetadataTransactionService::Execute(MetadataTransactionPlan&& plan,
    const MetadataTransactionCallbacks& callbacks,
    MetadataTransactionResult& result) {
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

    std::vector<fs::path> gatedPaths;
    gatedPaths.reserve(result.changes.size());
    for (const MetadataChange& change : result.changes) {
        const quicksift::core::MetadataSnapshot& storageSnapshot =
            result.mode == MetadataTransactionMode::UndoHistory ? change.before : change.after;
        const fs::path& path = storageSnapshot.path;
        const bool duplicate = std::any_of(gatedPaths.begin(), gatedPaths.end(),
            [&](const fs::path& existing) {
                const std::wstring left = existing.wstring();
                const std::wstring right = path.wstring();
                return left.size() == right.size() && std::equal(left.begin(), left.end(),
                    right.begin(), [](wchar_t a, wchar_t b) {
                        return std::towlower(static_cast<wint_t>(a)) ==
                            std::towlower(static_cast<wint_t>(b));
                    });
            });
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
        if (cancellationRequested_.load(std::memory_order_acquire)) {
            result.cancelled = true;
            if (result.detail.empty()) {
                result.detail = L"The metadata batch was cancelled at a safe file boundary.";
            }
            break;
        }
        MetadataChange& change = result.changes[index];
        const quicksift::core::MetadataSnapshot& storageSnapshot =
            result.mode == MetadataTransactionMode::UndoHistory ? change.before : change.after;
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

        MetadataWriteOutcome outcome;
        try {
            outcome = callbacks.write(storageSnapshot.path, patch, storageSnapshot.storage,
                storageSnapshot.jpeg, storageSnapshot.safeJpegWrite);
        } catch (...) {
            result.failed = true;
            ++result.failureCount;
            if (result.detail.empty()) {
                try {
                    result.detail = L"A metadata write raised an exception; the affected file was not marked complete.";
                } catch (...) {
                    // Keep the truthful bitmap even if diagnostics cannot allocate.
                }
            }
            continue;
        }
        if (!outcome.ok) {
            result.failed = true;
            ++result.failureCount;
            change.externalConflict = outcome.conflict;
            if (outcome.conflict) {
                try { ++result.conflictCount; } catch (...) {}
            }
            if (result.detail.empty()) {
                try { result.detail = std::move(outcome.detail); } catch (...) {}
            }
            continue;
        }
        change.resultingValues = outcome.after;
        change.hasResultingValues = true;
        if (result.mode == MetadataTransactionMode::Apply) {
            change.before.values = outcome.before;
            change.after.values = outcome.after;
        }
        result.completed[index] = true;
    }
}

} // namespace quicksift::transactions
