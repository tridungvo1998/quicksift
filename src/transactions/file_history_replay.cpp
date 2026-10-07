// OWNER: Guarded asynchronous dispatch of file Undo and Redo history.
#include "file_history_replay.h"
#include "file_history_group_replay.h"

#include <vector>

namespace quicksift::transactions {

FileTransactionResult ExecuteFileHistoryReplay(const FileTransactionPlan& plan,
    VerifiedFileOperations& operations, const std::atomic<bool>& cancellationRequested) {
    FileTransactionResult result;
    result.token = plan.token;
    result.label = plan.label;
    result.mode = plan.mode;
    result.historyItems = plan.historyReplayItems;
    result.replayCompleted.assign(result.historyItems.size(), false);

    try {
        const std::vector<detail::GroupRange> groups = detail::BuildGroupRanges(result.historyItems);
        if (plan.mode == FileTransactionMode::UndoHistory) {
            for (std::size_t reverse = groups.size(); reverse-- > 0;) {
                if (!detail::ReplayGroup(groups[reverse], true, result,
                    operations, cancellationRequested)) break;
            }
        } else {
            for (const detail::GroupRange& group : groups) {
                if (!detail::ReplayGroup(group, false, result,
                    operations, cancellationRequested)) break;
            }
        }
    } catch (...) {
        result.failed = true;
        if (result.detail.empty()) {
            try {
                result.detail = L"History replay stopped after an unexpected exception; completed media groups remain recorded.";
            } catch (...) {
                // Preserve the completion bitmap even if diagnostic allocation fails.
            }
        }
    }
    return result;
}

} // namespace quicksift::transactions
