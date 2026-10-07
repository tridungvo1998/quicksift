#pragma once

// OWNER: Internal media-group execution and rollback helpers.
#include "file_transaction.h"

#include <type_traits>

namespace quicksift::transactions::detail {

static_assert(std::is_nothrow_move_constructible_v<quicksift::core::FileHistoryItem>);

class CancellationSourceOverride {
public:
    CancellationSourceOverride(VerifiedFileOperations& operations,
        const std::atomic<bool>& source) noexcept
        : operations_(operations), source_(source) {
        operations_.SetCancellationSource(nullptr);
    }
    ~CancellationSourceOverride() { operations_.SetCancellationSource(&source_); }
    CancellationSourceOverride(const CancellationSourceOverride&) = delete;
    CancellationSourceOverride& operator=(const CancellationSourceOverride&) = delete;
private:
    VerifiedFileOperations& operations_;
    const std::atomic<bool>& source_;
};

inline FileIdentity DestinationIdentity(
    const quicksift::core::FileHistoryItem& item) noexcept {
    return { item.destinationSize, item.destinationStamp,
        item.destinationChangeStamp, item.destinationContentFingerprint };
}

inline bool ExecuteOne(const TransferItem& transfer, bool copy,
    std::uint64_t groupId, std::uint32_t groupOrder, bool companion,
    VerifiedFileOperations& operations, quicksift::core::FileHistoryItem& history,
    std::wstring& detail) {
    // Allocate all history state before touching the filesystem. After a
    // successful primitive, only scalar assignment remains.
    history.source = transfer.source;
    history.destination = transfer.destination;
    history.copied = copy;
    history.groupId = groupId;
    history.groupOrder = groupOrder;
    history.companion = companion;

    FileIdentity destinationIdentity;
    FileIdentity sourceIdentity;
    const bool succeeded = copy ?
        operations.CopyVerified(transfer.source, transfer.destination,
            destinationIdentity, sourceIdentity, detail) :
        operations.MoveVerified(transfer.source, transfer.destination,
            destinationIdentity, detail);
    if (!succeeded) return false;

    history.destinationSize = destinationIdentity.size;
    history.destinationStamp = destinationIdentity.stamp;
    history.destinationChangeStamp = destinationIdentity.changeStamp;
    history.destinationContentFingerprint = destinationIdentity.contentFingerprint;
    history.redoSourceSize = sourceIdentity.size;
    history.redoSourceStamp = sourceIdentity.stamp;
    history.redoSourceChangeStamp = sourceIdentity.changeStamp;
    history.redoSourceContentFingerprint = sourceIdentity.contentFingerprint;
    history.redoSourceFingerprintValid = copy;
    return true;
}

inline bool RollBackOne(const quicksift::core::FileHistoryItem& history, bool copy,
    VerifiedFileOperations& operations, std::wstring& detail) {
    const FileIdentity identity = DestinationIdentity(history);
    if (copy) return operations.RemoveIfUnchanged(history.destination, identity, detail);
    FileIdentity restoredIdentity;
    return operations.MoveIfUnchanged(history.destination, history.source,
        identity, restoredIdentity, detail);
}

inline std::size_t GroupItemCount(const TransferGroup& group) noexcept {
    return group.owners.size() + (group.companion ? 1u : 0u);
}

} // namespace quicksift::transactions::detail
