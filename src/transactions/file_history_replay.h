// OWNER: Platform-neutral guarded Undo/Redo execution for file history entries.
#pragma once

#include "transactions/file_transaction.h"

namespace quicksift::transactions {

[[nodiscard]] FileTransactionResult ExecuteFileHistoryReplay(
    const FileTransactionPlan& plan,
    VerifiedFileOperations& operations,
    const std::atomic<bool>& cancellationRequested);

} // namespace quicksift::transactions
