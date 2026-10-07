// OWNER: Platform-neutral execution and rollback of immutable media-group plans.
#include "file_transaction.h"

#include "file_history_replay.h"
#include "file_transaction_group_support.h"

#include <limits>
#include <utility>

namespace quicksift::transactions {
namespace {

using detail::CancellationSourceOverride;
using detail::ExecuteOne;
using detail::GroupItemCount;
using detail::RollBackOne;

FileTransactionResult ExecuteTransfer(const FileTransactionPlan& plan,
    VerifiedFileOperations& operations, const std::atomic<bool>& cancellationRequested,
    const FileTransactionProgressSink& progressSink) {
    FileTransactionResult result;
    result.token = plan.token;
    result.label = plan.label;
    result.mode = plan.mode;
    result.copy = plan.copy;
    result.deleteToFolder = plan.deleteToFolder;
    result.recordHistory = plan.recordHistory;
    result.missingRawCompanion = plan.missingRawCompanion;

    std::size_t totalFiles = 0;
    for (const TransferGroup& group : plan.groups) {
        if (group.owners.size() > std::numeric_limits<std::size_t>::max() - totalFiles)
            throw std::length_error("file transaction progress capacity overflow");
        totalFiles += group.owners.size();
    }
    std::size_t completedFiles = 0;

    if (plan.recordHistory) {
        std::size_t historyCapacity = 0;
        for (const TransferGroup& group : plan.groups) {
            const std::size_t count = GroupItemCount(group);
            if (count > std::numeric_limits<std::size_t>::max() - historyCapacity) {
                throw std::length_error("file transaction history capacity overflow");
            }
            historyCapacity += count;
        }
        result.historyItems.reserve(historyCapacity);
    }

    try {
        for (std::size_t groupIndex = 0; groupIndex < plan.groups.size(); ++groupIndex) {
            const TransferGroup& group = plan.groups[groupIndex];
            if (group.owners.empty()) continue;
            if (cancellationRequested.load(std::memory_order_acquire)) {
                result.cancelled = true;
                result.detail = L"The operation was cancelled at a safe media-group boundary.";
                break;
            }

            const std::uint64_t groupId = static_cast<std::uint64_t>(groupIndex) + 1u;
            const std::size_t itemCount = GroupItemCount(group);
            std::vector<quicksift::core::FileHistoryItem> completed;
            completed.reserve(itemCount);
            // Allocate rollback bookkeeping before the first filesystem change.
            std::vector<bool> rolledBack(itemCount, false);
            std::wstring rollbackFailures;
            rollbackFailures.reserve(512);

            bool groupSucceeded = true;
            std::uint32_t order = 0;
            for (const TransferItem& owner : group.owners) {
                quicksift::core::FileHistoryItem history;
                bool executed = false;
                try {
                    executed = ExecuteOne(owner, plan.copy, groupId, order++, false,
                        operations, history, result.detail);
                } catch (...) {
                    try { result.detail = L"A verified file primitive raised an exception before completing its media group."; } catch (...) {}
                }
                if (!executed) { groupSucceeded = false; break; }
                ++result.lastingTransferItems;
                completed.push_back(std::move(history));
            }
            if (groupSucceeded && group.companion) {
                quicksift::core::FileHistoryItem history;
                bool executed = false;
                try {
                    executed = ExecuteOne(*group.companion, plan.copy, groupId, order, true,
                        operations, history, result.detail);
                } catch (...) {
                    try { result.detail = L"A verified companion-file primitive raised an exception."; } catch (...) {}
                }
                if (!executed) {
                    groupSucceeded = false;
                } else {
                    ++result.lastingTransferItems;
                    completed.push_back(std::move(history));
                }
            }

            if (groupSucceeded) {
                if (plan.recordHistory) {
                    for (auto& history : completed) {
                        result.historyItems.push_back(std::move(history));
                    }
                }
                completedFiles += group.owners.size();
                if (progressSink) progressSink(completedFiles, totalFiles);
                continue;
            }

            const bool cancellationDuringGroup =
                cancellationRequested.load(std::memory_order_acquire);
            {
                // Rollback is a safety obligation. Ignore foreground cancellation
                // until every already-published member has been compensated.
                CancellationSourceOverride cancellationOverride(operations,
                    cancellationRequested);
                for (std::size_t reverse = completed.size(); reverse-- > 0;) {
                    std::wstring rollbackDetail;
                    bool restored = false;
                    try {
                        restored = RollBackOne(completed[reverse], plan.copy,
                            operations, rollbackDetail);
                    } catch (...) {
                        // Continue compensating earlier members. A backend
                        // exception must not split the rest of the media group.
                    }
                    if (restored) {
                        rolledBack[reverse] = true;
                        if (result.lastingTransferItems > 0) --result.lastingTransferItems;
                    } else if (!rollbackDetail.empty()) {
                        try {
                            if (!rollbackFailures.empty()) rollbackFailures += L" ";
                            rollbackFailures += rollbackDetail;
                        } catch (...) {
                            // Rollback truth is retained by the bitmap/history.
                        }
                    }
                }
            }

            if (plan.recordHistory) {
                for (std::size_t index = 0; index < completed.size(); ++index) {
                    if (!rolledBack[index]) {
                        result.historyItems.push_back(std::move(completed[index]));
                    }
                }
            }
            if (cancellationDuringGroup) result.cancelled = true;
            else result.failed = true;
            if (!rollbackFailures.empty()) {
                if (!result.detail.empty()) result.detail += L" ";
                result.detail += L"Media-group rollback was incomplete: ";
                result.detail += rollbackFailures;
            }
            break;
        }
    } catch (...) {
        result.failed = true;
        if (result.detail.empty()) {
            try {
                result.detail = L"The file transaction stopped after an unexpected exception at a safe boundary.";
            } catch (...) {
                // Preserve completed history even when diagnostic allocation fails.
            }
        }
    }
    return result;
}

} // namespace

FileTransactionResult ExecuteFileTransaction(const FileTransactionPlan& plan,
    VerifiedFileOperations& operations, const std::atomic<bool>& cancellationRequested) {
    return ExecuteFileTransaction(plan, operations, cancellationRequested, {});
}

FileTransactionResult ExecuteFileTransaction(const FileTransactionPlan& plan,
    VerifiedFileOperations& operations, const std::atomic<bool>& cancellationRequested,
    const FileTransactionProgressSink& progressSink) {
    if (plan.mode != FileTransactionMode::Transfer) {
        return ExecuteFileHistoryReplay(plan, operations, cancellationRequested);
    }
    return ExecuteTransfer(plan, operations, cancellationRequested, progressSink);
}

} // namespace quicksift::transactions
