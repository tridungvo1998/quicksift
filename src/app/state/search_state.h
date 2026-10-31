// OWNER: Thumbnail search field, applied query, hydration bookkeeping and the search
// worker hand-off (thumbnail_search_controller.cpp).
//
// Plain state of QuickSiftApplicationImpl (no behaviour). The class inherits
// this struct privately, so every member keeps its name and is used exactly as
// before; see app/quicksift_application_internal.h.
#pragma once

#include "app/state/app_state_common.h"
#include "core/search_query.h"
#include "core/hydrated_facts.h"

namespace quicksift::app::state {

struct SearchState {
    HWND searchInput_ = nullptr;
    std::wstring searchText_;                 // live field text
    std::wstring searchAppliedText_;          // text of the applied query
    quicksift::core::search::SearchQuery searchQuery_; // applied (parsed) query
    bool searchActive_ = false;               // applied query has clauses
    bool searchDebounceArmed_ = false;
    bool searchResetScrollPending_ = false;
    std::vector<std::uint8_t> searchMatches_; // by catalog index, for searchMatchesEpoch_
    std::uint64_t searchMatchesEpoch_ = 0;
    std::uint64_t searchSerial_ = 0;          // latest requested evaluation
    std::uint64_t searchInFlightSerial_ = 0;  // worker evaluation outstanding (0 = none)
    std::optional<float> searchSavedScroll_;  // view to restore when the search clears
    std::wstring searchSavedFocus_;
    float searchFieldOpacity_ = 0.0f;
    bool searchClearHot_ = false;
    bool searchInputShown_ = false;
    bool searchFocusPainted_ = false;
    std::size_t searchBaseCount_ = 0;         // photos passing the Filter tab alone
    RECT searchInputPixels_{};
    // Hydration bookkeeping (UI thread): bit 1 = capture time queued, bit 2 = comment, bit 4 = camera settings.
    std::vector<std::uint8_t> searchHydrateQueued_;
    std::uint64_t searchHydrateGeneration_ = 0;
    std::size_t searchHydrateCheckedCount_ = 0;
    bool searchHydrateNeedCapture_ = false;
    bool searchHydrateNeedComments_ = false;
    bool searchHydrateNeedCamera_ = false;
    std::atomic<std::size_t> searchHydrateDone_{ 0 };
    std::atomic<std::size_t> searchHydrateTotal_{ 0 };
    // Worker (one thread; evaluation jobs pre-empt hydration between files).
    struct SearchHydrateItem {
        std::size_t index = 0;
        fs::path path;
        std::uint64_t fileSize = 0;      // image stamps: validate the camera index
        std::int64_t modifiedStamp = 0;
        bool capture = false;
        bool comment = false;
        bool camera = false;
    };
    struct SearchFactUpdate : quicksift::core::HydratedFacts {
        std::size_t index = 0;
        fs::path path;
    };
    struct SearchEvalJob {
        std::uint64_t serial = 0;
        quicksift::core::CatalogSnapshotPtr snapshot;
        std::vector<quicksift::core::predicate::Clause> clauses;
    };
    struct SearchEvalResult {
        std::uint64_t serial = 0;
        std::uint64_t epoch = 0;
        std::vector<std::uint8_t> matches;
    };
    std::thread searchWorker_;
    std::mutex searchMutex_;
    std::condition_variable searchCv_;
    bool searchWorkerStop_ = false;                         // guarded by searchMutex_
    std::optional<SearchEvalJob> searchEvalJob_;            // guarded
    std::deque<SearchHydrateItem> searchHydrateQueue_;      // guarded
    std::uint64_t searchWorkerGeneration_ = 0;              // guarded
    std::vector<SearchFactUpdate> searchFactUpdates_;       // guarded
    std::uint64_t searchFactGeneration_ = 0;                // guarded
    std::optional<SearchEvalResult> searchEvalResult_;      // guarded
    std::atomic<bool> searchMessagePosted_{ false };
    // Set by the search worker whenever it stores a result / fact batch; cleared by
    // OnSearchWorkerMessage. The animation tick consumes it if a wake post was lost.
    std::atomic<bool> searchUnconsumed_{ false };
    bool searchDemandPublishedActive_ = false;
    bool searchDemandPublishedFocused_ = false;
};

} // namespace quicksift::app::state
