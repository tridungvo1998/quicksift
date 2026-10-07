#pragma once

// OWNER: Internal atomic media-group replay and compensation contract.
#include "file_transaction.h"

#include <vector>

namespace quicksift::transactions::detail {

struct GroupRange {
    std::size_t first = 0;
    std::size_t last = 0; // one past end
};

std::vector<GroupRange> BuildGroupRanges(
    const std::vector<quicksift::core::FileHistoryItem>& items);

bool ReplayGroup(const GroupRange& range, bool undo,
    FileTransactionResult& result, VerifiedFileOperations& operations,
    const std::atomic<bool>& cancellationRequested);

} // namespace quicksift::transactions::detail
