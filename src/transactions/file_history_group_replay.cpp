// OWNER: Atomic media-group history replay, compensation, and identity updates.
#include "file_history_group_replay.h"
#include "file_transaction_group_support.h"

#include <utility>
#include <vector>

namespace quicksift::transactions::detail {



FileIdentity RedoSourceIdentity(const quicksift::core::FileHistoryItem& item) noexcept {
    return { item.redoSourceSize, item.redoSourceStamp,
        item.redoSourceChangeStamp, item.redoSourceContentFingerprint };
}

void StoreDestinationIdentity(quicksift::core::FileHistoryItem& item,
    const FileIdentity& identity) noexcept {
    item.destinationSize = identity.size;
    item.destinationStamp = identity.stamp;
    item.destinationChangeStamp = identity.changeStamp;
    item.destinationContentFingerprint = identity.contentFingerprint;
}

void StoreRedoSourceIdentity(quicksift::core::FileHistoryItem& item,
    const FileIdentity& identity) noexcept {
    item.redoSourceSize = identity.size;
    item.redoSourceStamp = identity.stamp;
    item.redoSourceChangeStamp = identity.changeStamp;
    item.redoSourceContentFingerprint = identity.contentFingerprint;
    item.redoSourceFingerprintValid = true;
}

std::vector<GroupRange> BuildGroupRanges(
    const std::vector<quicksift::core::FileHistoryItem>& items) {
    std::vector<GroupRange> ranges;
    ranges.reserve(items.size());
    std::size_t first = 0;
    while (first < items.size()) {
        std::size_t last = first + 1;
        const std::uint64_t id = items[first].groupId;
        if (id != 0) {
            while (last < items.size() && items[last].groupId == id) ++last;
        }
        ranges.push_back({ first, last });
        first = last;
    }
    return ranges;
}

bool PreflightUndoGroup(const GroupRange& range,
    const std::vector<quicksift::core::FileHistoryItem>& items,
    VerifiedFileOperations& operations, std::wstring& detail) {
    // Undoing a copied group removes destinations. If a later member fails, the
    // original source must still be the exact recorded object so compensation
    // can recreate an already-removed destination without copying new content.
    for (std::size_t index = range.first; index < range.last; ++index) {
        const auto& item = items[index];
        if (!item.copied) continue;
        if (!item.redoSourceFingerprintValid ||
            !operations.IsUnchanged(item.source, RedoSourceIdentity(item))) {
            detail = L"A source file changed after this history entry was recorded; the media group was left untouched.";
            return false;
        }
    }
    return true;
}

bool ApplyUndo(quicksift::core::FileHistoryItem& item,
    VerifiedFileOperations& operations, FileIdentity& appliedIdentity,
    std::wstring& detail) {
    const FileIdentity expected = DestinationIdentity(item);
    if (item.copied) {
        appliedIdentity = expected;
        return operations.RemoveIfUnchanged(item.destination, expected, detail);
    }
    FileIdentity restoredIdentity;
    if (!operations.MoveIfUnchanged(item.destination, item.source,
        expected, restoredIdentity, detail)) return false;
    appliedIdentity = restoredIdentity;
    StoreRedoSourceIdentity(item, restoredIdentity);
    return true;
}

bool CompensateUndo(quicksift::core::FileHistoryItem& item,
    const FileIdentity& appliedIdentity, VerifiedFileOperations& operations,
    std::wstring& detail) {
    FileIdentity destinationIdentity;
    if (item.copied) {
        FileIdentity sourceIdentity;
        if (!operations.CopyIfUnchanged(item.source, item.destination,
            RedoSourceIdentity(item), destinationIdentity, sourceIdentity, detail)) {
            return false;
        }
    } else {
        if (!operations.MoveIfUnchanged(item.source, item.destination,
            appliedIdentity, destinationIdentity, detail)) return false;
    }
    StoreDestinationIdentity(item, destinationIdentity);
    return true;
}

bool ApplyRedo(quicksift::core::FileHistoryItem& item,
    VerifiedFileOperations& operations, FileIdentity& appliedIdentity,
    std::wstring& detail) {
    FileIdentity destinationIdentity;
    if (item.copied) {
        FileIdentity sourceIdentity;
        const bool succeeded = item.redoSourceFingerprintValid ?
            operations.CopyIfUnchanged(item.source, item.destination,
                RedoSourceIdentity(item), destinationIdentity, sourceIdentity, detail) :
            operations.CopyVerified(item.source, item.destination,
                destinationIdentity, sourceIdentity, detail);
        if (!succeeded) return false;
        StoreRedoSourceIdentity(item, sourceIdentity);
    } else {
        const bool succeeded = item.redoSourceFingerprintValid ?
            operations.MoveIfUnchanged(item.source, item.destination,
                RedoSourceIdentity(item), destinationIdentity, detail) :
            operations.MoveVerified(item.source, item.destination,
                destinationIdentity, detail);
        if (!succeeded) return false;
    }
    StoreDestinationIdentity(item, destinationIdentity);
    appliedIdentity = destinationIdentity;
    return true;
}

bool CompensateRedo(quicksift::core::FileHistoryItem& item,
    const FileIdentity& appliedIdentity, VerifiedFileOperations& operations,
    std::wstring& detail) {
    if (item.copied) {
        return operations.RemoveIfUnchanged(item.destination,
            appliedIdentity, detail);
    }
    FileIdentity restoredIdentity;
    if (!operations.MoveIfUnchanged(item.destination, item.source,
        appliedIdentity, restoredIdentity, detail)) return false;
    StoreRedoSourceIdentity(item, restoredIdentity);
    return true;
}

struct AppliedStep {
    std::size_t index = 0;
    FileIdentity identity{};
};

bool ReplayGroup(const GroupRange& range, bool undo,
    FileTransactionResult& result, VerifiedFileOperations& operations,
    const std::atomic<bool>& cancellationRequested) {
    if (undo && !PreflightUndoGroup(range, result.historyItems, operations, result.detail)) {
        result.failed = true;
        return false;
    }

    std::vector<AppliedStep> applied;
    applied.reserve(range.last - range.first);
    std::wstring rollbackFailures;
    rollbackFailures.reserve(512);
    bool succeeded = true;
    if (undo) {
        for (std::size_t reverse = range.last; reverse-- > range.first;) {
            if (cancellationRequested.load(std::memory_order_acquire)) {
                result.cancelled = true;
                succeeded = false;
                break;
            }
            FileIdentity identity;
            bool appliedStep = false;
            try {
                appliedStep = ApplyUndo(result.historyItems[reverse], operations,
                    identity, result.detail);
            } catch (...) {
                try { result.detail = L"A guarded Undo primitive raised an exception."; } catch (...) {}
            }
            if (!appliedStep) {
                if (cancellationRequested.load(std::memory_order_acquire)) result.cancelled = true;
                else result.failed = true;
                succeeded = false;
                break;
            }
            applied.push_back({ reverse, identity });
        }
    } else {
        for (std::size_t index = range.first; index < range.last; ++index) {
            if (cancellationRequested.load(std::memory_order_acquire)) {
                result.cancelled = true;
                succeeded = false;
                break;
            }
            FileIdentity identity;
            bool appliedStep = false;
            try {
                appliedStep = ApplyRedo(result.historyItems[index], operations,
                    identity, result.detail);
            } catch (...) {
                try { result.detail = L"A guarded Redo primitive raised an exception."; } catch (...) {}
            }
            if (!appliedStep) {
                if (cancellationRequested.load(std::memory_order_acquire)) result.cancelled = true;
                else result.failed = true;
                succeeded = false;
                break;
            }
            applied.push_back({ index, identity });
        }
    }

    if (succeeded) {
        for (const AppliedStep& step : applied) {
            result.replayCompleted[step.index] = true;
            ++result.lastingTransferItems;
        }
        return true;
    }

    // A history media group is atomic. Compensate every successful member when
    // a later member fails. Only a compensation failure may leave a truthful
    // partial completion in the bitmap/history stacks.
    {
        CancellationSourceOverride cancellationOverride(operations, cancellationRequested);
        for (auto iterator = applied.rbegin(); iterator != applied.rend(); ++iterator) {
            auto& item = result.historyItems[iterator->index];
            std::wstring rollbackDetail;
            bool rolledBack = false;
            try {
                rolledBack = undo ?
                    CompensateUndo(item, iterator->identity, operations, rollbackDetail) :
                    CompensateRedo(item, iterator->identity, operations, rollbackDetail);
            } catch (...) {
                // Continue compensating the rest of the media group.
            }
            if (!rolledBack) {
                result.replayCompleted[iterator->index] = true;
                ++result.lastingTransferItems;
                if (!rollbackDetail.empty()) {
                    try {
                        if (!rollbackFailures.empty()) rollbackFailures += L" ";
                        rollbackFailures += rollbackDetail;
                    } catch (...) {
                        // Completion bits still preserve truthful partial replay.
                    }
                }
            }
        }
    }
    if (!rollbackFailures.empty()) {
        if (!result.detail.empty()) result.detail += L" ";
        result.detail += L"History media-group compensation was incomplete: ";
        result.detail += rollbackFailures;
    }
    return false;
}


} // namespace quicksift::transactions::detail
