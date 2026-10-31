// OWNER: Metadata pipeline: history, pending edits, write-lane retry state and the
// visible-first metadata read queue.
//
// Plain state of QuickSiftApplicationImpl (no behaviour). The class inherits
// this struct privately, so every member keeps its name and is used exactly as
// before; see app/quicksift_application_internal.h.
#pragma once

#include "app/state/app_state_common.h"
#include "core/history_store.h"
#include "core/metadata_edit_queue.h"
#include "core/metadata_queue_order.h"
#include "transactions/transaction_coordinator.h"

namespace quicksift::app::state {

struct MetadataState {
    quicksift::core::HistoryStore historyStore_;
    size_t historyMemoryLimit_ = 96ull * kMebibyte;
    bool replayingHistory_ = false;
    quicksift::core::PendingMetadataEdits pendingMetadataEdits_;
    // Apply-mode changes that stayed locked by another app (Retry / Skip choice).
    std::vector<quicksift::transactions::MetadataChange> inUseRetryChanges_;
    std::wstring inUseRetryLabel_;
    std::uint64_t metadataWaitingToastSerial_ = 0;
    ULONGLONG metadataWaitingToastTick_ = 0;
    bool metadataWriteLaneTimerActive_ = false;
    std::vector<fs::path> pendingExternalMetadataReconcilePaths_;
    std::optional<HistoryDirection> pendingExternalMetadataReconcileHistory_;
    uint64_t generation_ = 0;
    size_t pendingRatings_ = 0;
    size_t metadataJobsInFlight_ = 0;
    size_t metadataQueueLimit_ = 24;
    // Visible-first rating indexing: grid positions painted last frame, and the
    // window/queue size the pending queue was last ordered for (UI thread only).
    quicksift::core::metadata_order::VisibleWindow thumbnailShownPositions_;
    quicksift::core::metadata_order::VisibleWindow metadataPromotedWindow_;
    size_t metadataPromotedQueueSize_ = 0;

    std::deque<size_t> pendingMetadataIndices_;
    // Per catalog index: metadata reads that ended cancelled/failed/unknown (core/metadata_progress.h).
    std::unordered_map<size_t, int> metadataReadAttempts_;
    bool metadataUiDirty_ = false;
};

} // namespace quicksift::app::state
