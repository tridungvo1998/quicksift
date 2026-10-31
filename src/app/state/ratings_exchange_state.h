// OWNER: Import / Export Ratings worker and pending import (ratings_exchange_commands.cpp).
//
// Plain state of QuickSiftApplicationImpl (no behaviour). The class inherits
// this struct privately, so every member keeps its name and is used exactly as
// before; see app/quicksift_application_internal.h.
#pragma once

#include "app/state/app_state_common.h"
#include "core/ratings_exchange.h"

namespace quicksift::app::state {

struct RatingsExchangeState {
    struct RatingsImportCounts {
        std::size_t submitted = 0;
        std::size_t skipped = 0;   // already matching / missing-and-ignored / unsupported-neutral
        std::size_t failed = 0;    // could not be prepared (not in catalog)
    };

    struct RatingsWorkerResult {
        bool import = false;
        bool ok = false;
        std::wstring error;          // localized failure text (empty when ok)
        fs::path file;
        std::uint64_t generation = 0;
        std::size_t totalFiles = 0;  // export
        std::size_t exportedEntries = 0;
        quicksift::core::ratings_exchange::ParseResult parse; // import
    };
    std::thread ratingsWorker_;
    std::atomic<bool> ratingsWorkerCancel_{ false };
    std::atomic<bool> ratingsWorkerRunning_{ false };
    std::atomic<std::size_t> ratingsWorkerDone_{ 0 };
    std::atomic<std::size_t> ratingsWorkerTotal_{ 0 };
    mutable std::mutex ratingsWorkerMutex_;
    std::optional<RatingsWorkerResult> ratingsWorkerResult_;
    struct PendingRatingsImport {
        fs::path file;
        fs::path folder;
        std::uint64_t generation = 0;
        quicksift::core::ratings_exchange::ExportDocument document;
        quicksift::core::ratings_exchange::ImportReconciliation reconciliation;
        std::wstring missingText;
    };
    std::optional<PendingRatingsImport> pendingRatingsImport_;
    HWND ratingsMissingFilesWindow_ = nullptr;
    std::uint64_t ratingsImportToken_ = 0;
    RatingsImportCounts ratingsImportCounts_{};
    // The bulk metadata token above belongs to Batch Processing (summary wording).
    bool ratingsImportIsBatch_ = false;
};

} // namespace quicksift::app::state
