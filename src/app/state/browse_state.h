// OWNER: Browsing and settings: folders, library, filters, badges, theme and
// metadata-writing preferences, scan/session flags.
//
// Plain state of QuickSiftApplicationImpl (no behaviour). The class inherits
// this struct privately, so every member keeps its name and is used exactly as
// before; see app/quicksift_application_internal.h.
#pragma once

#include "app/state/app_state_common.h"

namespace quicksift::app::state {

struct BrowseState {
    fs::path rootFolder_;
    fs::path currentFolder_;
    fs::path pendingTreeFolder_;
    fs::path restoredFolder_;
    fs::path sessionPath_;

    std::wstring restoredCurrentPath_;

    std::vector<std::wstring> libraryPaths_;
    std::unordered_map<FormatGroup, bool> formats_;

    ViewMode restoredMode_ = ViewMode::Thumbnails;
    SortMode sortMode_ = SortMode::NameAsc;
    DateFilter dateFilter_ = DateFilter::Any;
    RatingFilter ratingFilter_ = RatingFilter::All;
    PickFilter pickFilter_ = PickFilter::All;
    ColorLabelFilter colorLabelFilter_ = ColorLabelFilter::All;
    // Camera-settings filters (0 / empty / -1 = All). Options come from indexed values.
    int isoFilter_ = -1;
    double apertureFilter_ = 0.0;
    double focalFilter_ = 0.0;
    double shutterFilter_ = 0.0;
    std::wstring cameraFilter_;
    int hourFilter_ = -1;
    // Dynamic format/date filters (empty = all). Populated from the folder.
    std::wstring formatExtFilter_;   // e.g. L"nef" (no dot, lower)
    std::wstring dateExactFilter_;   // e.g. L"2026-03-14" or empty

    bool badgeStars_ = true;
    bool badgeColor_ = true;
    bool badgePick_ = true;
    bool badgePair_ = false;
    bool copyMoveRawWithJpg_ = false;
    bool loadOnlyRawJpegPreviews_ = false;
    AppTheme theme_ = AppTheme::Dark;

    std::uint32_t metadataDirectMask_ = kDefaultMetadataDirectMask;
    bool safeJpegMetadataWrites_ = true;

    bool scanComplete_ = false;
    bool sessionRestored_ = false;
};

} // namespace quicksift::app::state
