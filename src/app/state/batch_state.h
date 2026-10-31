// OWNER: Batch Processing dialog, worker and pending batch (batch_processing_commands.cpp).
//
// Plain state of QuickSiftApplicationImpl (no behaviour). The class inherits
// this struct privately, so every member keeps its name and is used exactly as
// before; see app/quicksift_application_internal.h.
#pragma once

#include "app/state/app_state_common.h"
#include "core/batch_match_policy.h"
#include "localization.h"

namespace quicksift::app::state {

struct BatchState {
    struct BatchSettings {
        std::wstring names;
        int starRating = 0;  // dropdown index; 0 = No change, 1.. = 0..5 stars
        int pickState = 0;   // 0 = No change, 1 Pick, 2 Reject, 3 Unmark
        int colorLabel = 0;  // 0 = No change, 1.. = None, Red, ... Purple
        std::wstring extensions;
        int matchingMethod = 0; // 0 = Partial, 1 = Complete
    };
    BatchSettings batchSettings_{};   // remembered for the session
    HWND batchDialog_ = nullptr;
    quicksift::Language batchDialogLanguage_ = quicksift::Language::English;
    bool batchAwaitingGlass_ = false; // dialog hidden while a glass alert is up
    struct BatchWorkerResult {
        bool folderLoad = false;
        std::uint64_t serial = 0;
        std::uint64_t generation = 0;
        bool ok = false;
        std::wstring error;
        // Folder load
        std::wstring folder;
        quicksift::core::batch_match::MergeResult merge;
        std::size_t filesSeen = 0;
        // Preflight
        std::vector<quicksift::core::batch_match::NameEntry> entries;
        quicksift::core::batch_match::MatchResult match;
        std::vector<fs::path> matchedPaths;
    };

    std::thread batchWorker_;
    std::atomic<bool> batchWorkerCancel_{ false };
    std::atomic<bool> batchWorkerRunning_{ false };
    std::uint64_t batchWorkerSerial_ = 0;
    mutable std::mutex batchWorkerMutex_;
    std::optional<BatchWorkerResult> batchWorkerResult_;
    struct PendingBatch {
        fs::path folder;
        std::uint64_t generation = 0;
        std::vector<fs::path> paths;
        std::optional<int> starRating;
        std::optional<int> pickState;
        std::optional<int> colorLabel;
        std::wstring unmatchedText;
        std::size_t unmatchedCount = 0;
    };
    std::optional<PendingBatch> pendingBatch_;

    HWND batchUnmatchedWindow_ = nullptr;
};

} // namespace quicksift::app::state
