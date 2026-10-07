// OWNER: Compiled QuickSiftApplication feature module.
#include "app/quicksift_application_internal.h"

namespace quicksift::app {
namespace {

MenuItem MakeMenuItem(std::wstring text, int command, bool checked = false,
    bool destructive = false) {
    MenuItem item{};
    item.text = std::move(text);
    item.command = command;
    item.checked = checked;
    item.destructive = destructive;
    return item;
}

MenuItem MakeMenuSeparator() {
    MenuItem item{};
    item.separator = true;
    return item;
}

MenuItem MakeHeader(std::wstring text) {
    MenuItem item{};
    item.text = std::move(text);
    item.header = true;
    return item;
}

} // namespace

// CODE GUIDE: See CODE_GUIDE.md -> "How to add a feature".
// OWNER: Settings/filter UI and high-level file command orchestration; decisions belong to their policy owners.

// User commands: metadata/filter/settings menus, button captions, delete
// confirmation, and high-level copy/move/delete command orchestration.

void QuickSiftApplicationImpl::ShowRatingMenu() {
        const int active = ActiveRatingForUi();
        std::vector<MenuItem> items = {
            MakeMenuItem(L"0 ★ (Unrated)", ID_RATE_CLEAR, active == 0),
            MakeMenuItem(L"1 ★", ID_RATE_1, active == 1),
            MakeMenuItem(L"2 ★", ID_RATE_2, active == 2),
            MakeMenuItem(L"3 ★", ID_RATE_3, active == 3),
            MakeMenuItem(L"4 ★", ID_RATE_4, active == 4),
            MakeMenuItem(L"5 ★", ID_RATE_5, active == 5),
        };
        const int command = TrackDropdownMenu(controls_[ID_RATING_MENU], std::move(items));
        if (command == ID_RATE_CLEAR) {
            ApplyRating(0);
        } else if (command >= ID_RATE_1 && command <= ID_RATE_5) {
            ApplyRating(command - ID_RATE_1 + 1);
        }
    }

void QuickSiftApplicationImpl::ShowPickStateMenu() {
    const int active = ActivePickStateForUi();
    std::vector<MenuItem> items = {
        MakeMenuItem(L"Pick", ID_PICK_STATE_PICK, active == 1),
        MakeMenuItem(L"Reject", ID_PICK_STATE_REJECT, active == -1),
        MakeMenuItem(L"Unmark", ID_PICK_STATE_UNMARK, active == 0),
    };
    const int command = TrackDropdownMenu(controls_[ID_PICK_STATE_MENU], std::move(items));
    switch (command) {
    case ID_PICK_STATE_PICK: ApplyPickState(1); break;
    case ID_PICK_STATE_REJECT: ApplyPickState(-1); break;
    case ID_PICK_STATE_UNMARK: ApplyPickState(0); break;
    default: break;
    }
}

void QuickSiftApplicationImpl::ShowColorLabelMenu() {
        const int active = ActiveColorLabelForUi();
        std::vector<MenuItem> items = {
            MakeMenuItem(L"None", ID_LABEL_NONE, active == 0),
            MakeMenuItem(L"Red", ID_LABEL_RED, active == 1),
            MakeMenuItem(L"Yellow", ID_LABEL_YELLOW, active == 2),
            MakeMenuItem(L"Green", ID_LABEL_GREEN, active == 3),
            MakeMenuItem(L"Blue", ID_LABEL_BLUE, active == 4),
            MakeMenuItem(L"Purple", ID_LABEL_PURPLE, active == 5),
        };
        const int command = TrackDropdownMenu(controls_[ID_COLOR_LABEL], std::move(items));
        if (command >= ID_LABEL_NONE && command <= ID_LABEL_PURPLE) ApplyColorLabel(command - ID_LABEL_NONE);
    }

    void QuickSiftApplicationImpl::ShowRatingFilterMenu() {
        const int active = static_cast<int>(ratingFilter_);
        std::vector<MenuItem> items = {
            MakeMenuItem(L"All ratings", ID_RATING_FILTER_ALL, active == 0),
            MakeMenuItem(L"No stars", ID_RATING_FILTER_NONE, active == 1),
            MakeMenuItem(L"Exactly 1 star", ID_RATING_FILTER_1, active == 2),
            MakeMenuItem(L"Exactly 2 stars", ID_RATING_FILTER_2, active == 3),
            MakeMenuItem(L"Exactly 3 stars", ID_RATING_FILTER_3, active == 4),
            MakeMenuItem(L"Exactly 4 stars", ID_RATING_FILTER_4, active == 5),
            MakeMenuItem(L"Exactly 5 stars", ID_RATING_FILTER_5, active == 6),
            MakeMenuItem(L"4 stars and up", ID_RATING_FILTER_4PLUS, active == 7),
        };
        const int command = TrackDropdownMenu(controls_[ID_STAR_FILTER], std::move(items));
        if (command < ID_RATING_FILTER_ALL || command > ID_RATING_FILTER_4PLUS) return;
        ratingFilter_ = static_cast<RatingFilter>(command - ID_RATING_FILTER_ALL);
        UpdateRatingFilterButton();
        RebuildVisiblePhotos();
        SaveSession();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::ShowPickFilterMenu() {
        const int active = static_cast<int>(pickFilter_);
        std::vector<MenuItem> items = {
            MakeMenuItem(L"All pick states", ID_PICK_FILTER_ALL, active == 0),
            MakeMenuItem(L"Picks only", ID_PICK_FILTER_PICKS, active == 1),
            MakeMenuItem(L"Rejected only", ID_PICK_FILTER_REJECTS, active == 2),
            MakeMenuItem(L"Unmarked only", ID_PICK_FILTER_UNMARKED, active == 3),
        };
        const int command = TrackDropdownMenu(controls_[ID_PICK_FILTER], std::move(items));
        if (command < ID_PICK_FILTER_ALL || command > ID_PICK_FILTER_UNMARKED) return;
        pickFilter_ = static_cast<PickFilter>(command - ID_PICK_FILTER_ALL);
        UpdatePickFilterButton();
        RebuildVisiblePhotos();
        SaveSession();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::ShowColorLabelFilterMenu() {
        const int active = static_cast<int>(colorLabelFilter_);
        std::vector<MenuItem> items = {
            MakeMenuItem(L"All labels", ID_LABEL_FILTER_ALL, active == 0),
            MakeMenuItem(L"Unlabeled only", ID_LABEL_FILTER_NONE, active == 1),
            MakeMenuItem(L"Red only", ID_LABEL_FILTER_RED, active == 2),
            MakeMenuItem(L"Yellow only", ID_LABEL_FILTER_YELLOW, active == 3),
            MakeMenuItem(L"Green only", ID_LABEL_FILTER_GREEN, active == 4),
            MakeMenuItem(L"Blue only", ID_LABEL_FILTER_BLUE, active == 5),
            MakeMenuItem(L"Purple only", ID_LABEL_FILTER_PURPLE, active == 6),
        };
        const int command = TrackDropdownMenu(controls_[ID_LABEL_FILTER], std::move(items));
        if (command < ID_LABEL_FILTER_ALL || command > ID_LABEL_FILTER_PURPLE) return;
        colorLabelFilter_ = static_cast<ColorLabelFilter>(command - ID_LABEL_FILTER_ALL);
        UpdateColorLabelFilterButton();
        RebuildVisiblePhotos();
        SaveSession();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::ShowZoomModeMenu() {
        const int active = static_cast<int>(EditableViewState().mode);
        std::vector<MenuItem> items = {
            MakeMenuItem(L"Fit image", ID_ZOOM_FIT, active == 0),
            MakeMenuItem(L"Fit width", ID_ZOOM_FIT_WIDTH, active == 1),
            MakeMenuItem(L"Fit height", ID_ZOOM_FIT_HEIGHT, active == 2),
            MakeMenuItem(L"100% pixels", ID_ZOOM_100, active == 3),
            MakeMenuItem(L"Fill pane", ID_ZOOM_FILL, active == 4),
        };
        const int command = TrackDropdownMenu(controls_[ID_ZOOM_MODE], std::move(items));
        if (command < ID_ZOOM_FIT || command > ID_ZOOM_FILL) return;
        SetZoomMode(static_cast<ZoomMode>(command - ID_ZOOM_FIT));
    }

    void QuickSiftApplicationImpl::ApplySettingCommand(int command) {
        std::uint32_t toggledMetadataFlag = 0;
        switch (command) {
        case ID_SETTING_BADGE_STARS: badgeStars_ = !badgeStars_; break;
        case ID_SETTING_BADGE_COLOR: badgeColor_ = !badgeColor_; break;
        case ID_SETTING_BADGE_PICK: badgePick_ = !badgePick_; break;
        case ID_SETTING_BADGE_PAIR: badgePair_ = !badgePair_; break;
        case ID_SETTING_COPY_MOVE_RAW_WITH_JPG: copyMoveRawWithJpg_ = !copyMoveRawWithJpg_; break;
        case ID_SETTING_PERFORMANCE_HUD:
            performanceHudVisible_ = !performanceHudVisible_;
            UpdatePerformanceHudTimer();
            SaveSession();
            ShowToast(performanceHudVisible_ ? L"Performance HUD enabled." : L"Performance HUD disabled.");
            InvalidateCanvas();
            return;
        case ID_SETTING_RAW_JPEG_PREVIEWS_ONLY:
            loadOnlyRawJpegPreviews_ = !loadOnlyRawJpegPreviews_;
            worker_.SetRawJpegPreviewOnly(loadOnlyRawJpegPreviews_);
            worker_.DiscardQueuedTiles();
            ClearBitmapCache();
            failedDecodes_.clear();
            ScheduleVisibleWork();
            InvalidateCanvas();
            SaveSession();
            ShowToast(loadOnlyRawJpegPreviews_ ? L"RAW decoding disabled; using embedded JPG previews only." :
                L"Full RAW decoding enabled.");
            return;
        case ID_SETTING_THEME:
            theme_ = theme_ == AppTheme::Dark ? AppTheme::Light : AppTheme::Dark;
            SetWindowTextW(controls_[ID_THEME_TOGGLE], ThemeToggleText().c_str());
            ApplyTheme();
            break;
        case ID_SETTING_DIRECT_JPEG: toggledMetadataFlag = MetadataDirectJpeg; break;
        case ID_SETTING_METADATA_RAW:
            metadataDirectMask_ ^= MetadataDirectRaw;
            gMetadataDirectMask.store(metadataDirectMask_, std::memory_order_relaxed);
            SaveSession();
            break;
        case ID_SETTING_METADATA_LOSSY:
            metadataDirectMask_ ^= (MetadataDirectJpeg | MetadataDirectPng);
            gMetadataDirectMask.store(metadataDirectMask_, std::memory_order_relaxed);
            SaveSession();
            break;
        case ID_SETTING_METADATA_TIFF:
            metadataDirectMask_ ^= MetadataDirectTiff;
            gMetadataDirectMask.store(metadataDirectMask_, std::memory_order_relaxed);
            SaveSession();
            break;
        case ID_SETTING_SAFE_JPEG:
            safeJpegMetadataWrites_ = !safeJpegMetadataWrites_;
            if (safeJpegMetadataWrites_) {
                ShowGlassAlert(L"Safe JPEG writes enabled",
                    L"JPEG metadata edits will use a verified temporary copy, post-write validation, and atomic replacement. This is slower, especially for large JPEGs or slow storage, but it protects the original from partial writes.",
                    GlassAlertKind::Information);
            } else {
                ShowGlassAlert(L"Fast JPEG writes enabled",
                    L"JPEG metadata will be written directly to the original file. This is faster, but a crash, power loss, storage fault, or metadata-library failure during the write can damage the JPEG. Enable Safe JPEG writes or use XMP sidecars when file protection matters more than speed.",
                    GlassAlertKind::Warning);
            }
            SaveSession();
            return;
        case ID_SETTING_LANGUAGE_ENGLISH:
        case ID_SETTING_LANGUAGE_VIETNAMESE: {
            const auto next = command == ID_SETTING_LANGUAGE_VIETNAMESE ?
                quicksift::Language::Vietnamese : quicksift::Language::English;
            if (localizer_.CurrentLanguage() != next) {
                localizer_.SetLanguage(next);
                RefreshLocalizedText();
                SaveSession();
                ShowToast(L"Language changed.");
            }
            return;
        }
        case ID_SETTING_CLEAR_MEMORY_CACHE:
            ClearBitmapCache();
            persistentCache_.TrimMemory(true);
            InvalidateCanvas();
            ShowToast(L"Cache cleared.");
            return;
        case ID_SETTING_CLEAR_DISK_CACHE:
            ClearBitmapCache();
            worker_.ClearPersistentCache();
            InvalidateCanvas();
            ScheduleVisibleWork();
            ShowToast(L"Cache cleared.");
            return;
        case ID_SETTING_DIAGNOSTIC_LOG: ShowDiagnosticLog(); return;
        case ID_SETTING_HELP: ShowHelp(); return;
        case ID_SETTING_ABOUT: ShowAbout(); return;
        case ID_SETTING_DIRECT_PNG: toggledMetadataFlag = MetadataDirectPng; break;
        case ID_SETTING_DIRECT_TIFF: toggledMetadataFlag = MetadataDirectTiff; break;
        case ID_SETTING_DIRECT_RAW: toggledMetadataFlag = MetadataDirectRaw; break;
        default: return;
        }
        if (toggledMetadataFlag != 0) {
            metadataDirectMask_ ^= toggledMetadataFlag;
            gMetadataDirectMask.store(metadataDirectMask_, std::memory_order_relaxed);
            if (toggledMetadataFlag == MetadataDirectRaw &&
                (metadataDirectMask_ & MetadataDirectRaw) != 0) {
                ShowGlassAlert(L"Direct RAW metadata enabled",
                    L"QuickSift will update a verified temporary copy and atomically replace the original only after validation. Even so, proprietary RAW containers are less universally writable than JPEG, PNG, or TIFF. XMP sidecars remain the safer default. Existing metadata is not automatically migrated between storage modes.",
                    GlassAlertKind::Warning);
            } else {
                ShowToast((metadataDirectMask_ & toggledMetadataFlag) != 0 ?
                    L"Future edits will be written inside that file format." :
                    L"Future edits will be written to XMP sidecars for that format.");
            }
            if (!currentFolder_.empty()) LoadFolder(currentFolder_);
        }
        SaveSession();
        InvalidateCanvas();
        UpdateToggleButtons();
    }

    void QuickSiftApplicationImpl::ShowSettingsMenu() {
        std::vector<MenuItem> items;

        items.push_back(MakeHeader(L"Thumbnails"));
        items.push_back(MakeMenuItem(L"Show star badges", ID_SETTING_BADGE_STARS, badgeStars_));
        items.push_back(MakeMenuItem(L"Show color-label badges", ID_SETTING_BADGE_COLOR, badgeColor_));
        items.push_back(MakeMenuItem(L"Show pick/reject badges", ID_SETTING_BADGE_PICK, badgePick_));
        items.push_back(MakeMenuItem(L"Show RAW/JPG badges", ID_SETTING_BADGE_PAIR, badgePair_));
        items.push_back(MakeMenuSeparator());

        items.push_back(MakeHeader(L"RAW handling"));
        items.push_back(MakeMenuItem(L"Copy/Move RAW with JPG", ID_SETTING_COPY_MOVE_RAW_WITH_JPG, copyMoveRawWithJpg_));
        items.push_back(MakeMenuItem(L"Load only JPG previews for RAW", ID_SETTING_RAW_JPEG_PREVIEWS_ONLY, loadOnlyRawJpegPreviews_));
        items.push_back(MakeMenuSeparator());

        items.push_back(MakeHeader(L"Metadata handling"));
        items.push_back(MakeMenuItem(L"Safe JPG writes", ID_SETTING_SAFE_JPEG, safeJpegMetadataWrites_));
        const bool rawEmbedded = (metadataDirectMask_ & MetadataDirectRaw) != 0;
        const bool lossyEmbedded = (metadataDirectMask_ & (MetadataDirectJpeg | MetadataDirectPng)) != 0;
        const bool tiffEmbedded = (metadataDirectMask_ & MetadataDirectTiff) != 0;
        items.push_back(MakeHeader(L"Use XMP instead of metadata write for"));
        items.push_back(MakeMenuItem(L"RAW files", ID_SETTING_METADATA_RAW, rawEmbedded));
        items.push_back(MakeMenuItem(L"JPG, PNG and lossy formats", ID_SETTING_METADATA_LOSSY, lossyEmbedded));
        items.push_back(MakeMenuItem(L"TIFF", ID_SETTING_METADATA_TIFF, tiffEmbedded));
        items.push_back(MakeMenuSeparator());

        items.push_back(MakeHeader(L"Help & Diagnostics"));
        items.push_back(MakeMenuItem(L"Show performance HUD", ID_SETTING_PERFORMANCE_HUD, performanceHudVisible_));
        items.push_back(MakeMenuItem(L"Show diagnostic log", ID_SETTING_DIAGNOSTIC_LOG));
        items.push_back(MakeMenuItem(L"Clear memory cache", ID_SETTING_CLEAR_MEMORY_CACHE));
        items.push_back(MakeMenuItem(L"Clear disk cache", ID_SETTING_CLEAR_DISK_CACHE));
        items.push_back(MakeMenuItem(L"Help and documentation", ID_SETTING_HELP));
        items.push_back(MakeMenuItem(L"About this app", ID_SETTING_ABOUT));

        const int command = TrackDropdownMenu(hwnd_, std::move(items));
        if (command != 0) ApplySettingCommand(command);
    }

    void QuickSiftApplicationImpl::ShowFormatsMenu() {
        std::vector<MenuItem> items = {
            MakeMenuItem(L"JPEG", ID_FMT_JPEG, formats_[FormatGroup::Jpeg]),
            MakeMenuItem(L"PNG", ID_FMT_PNG, formats_[FormatGroup::Png]),
            MakeMenuItem(L"AVIF / HEIF", ID_FMT_HEIF, formats_[FormatGroup::Heif]),
            MakeMenuItem(L"TIFF", ID_FMT_TIFF, formats_[FormatGroup::Tiff]),
            MakeMenuItem(L"BMP / GIF", ID_FMT_BASIC, formats_[FormatGroup::Basic]),
            MakeMenuItem(L"WebP", ID_FMT_WEBP, formats_[FormatGroup::WebP]),
            MakeMenuItem(L"Camera RAW", ID_FMT_RAW, formats_[FormatGroup::Raw]),
            MakeMenuSeparator(),
            MakeMenuItem(L"Enable all", ID_FMT_ALL),
        };
        const int command = TrackDropdownMenu(controls_[ID_FORMATS], std::move(items));
        if (command == 0) return;
        if (command == ID_FMT_ALL) {
            for (auto& [group, enabled] : formats_) enabled = true;
        } else {
            const std::unordered_map<int, FormatGroup> ids = {
                { ID_FMT_JPEG, FormatGroup::Jpeg }, { ID_FMT_PNG, FormatGroup::Png },
                { ID_FMT_HEIF, FormatGroup::Heif }, { ID_FMT_TIFF, FormatGroup::Tiff },
                { ID_FMT_BASIC, FormatGroup::Basic }, { ID_FMT_WEBP, FormatGroup::WebP },
                { ID_FMT_RAW, FormatGroup::Raw }
            };
            auto it = ids.find(command);
            if (it != ids.end()) formats_[it->second] = !formats_[it->second];
        }
        UpdateFormatFilterButton();
        SaveSession();
        RebuildVisiblePhotos();
        ScheduleVisibleWork();
        UpdateStatus();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::UpdateFormatFilterButton() {
        if (!controls_.contains(ID_FORMATS)) return;

        static const std::array<std::pair<FormatGroup, const wchar_t*>, 7> labels{{
            {FormatGroup::Jpeg, L"JPEG"},
            {FormatGroup::Png, L"PNG"},
            {FormatGroup::Heif, L"AVIF / HEIF"},
            {FormatGroup::Tiff, L"TIFF"},
            {FormatGroup::Basic, L"BMP / GIF"},
            {FormatGroup::WebP, L"WebP"},
            {FormatGroup::Raw, L"Camera RAW"},
        }};

        std::vector<const wchar_t*> enabledNames;
        enabledNames.reserve(labels.size());
        for (const auto& [group, name] : labels) {
            auto it = formats_.find(group);
            if (it != formats_.end() && it->second) enabledNames.push_back(name);
        }

        std::wstring value;
        if (enabledNames.size() == labels.size()) {
            value = Tr(L"All");
        } else if (enabledNames.empty()) {
            value = Tr(L"None");
        } else if (enabledNames.size() == 1) {
            value = Tr(enabledNames.front());
        } else {
            value = std::to_wstring(enabledNames.size()) + L" " + Tr(L"formats");
        }

        const std::wstring text = Tr(L"Format") + L": " + value;
        SetWindowTextW(controls_[ID_FORMATS], text.c_str());
        RelayoutTitleControlsAfterCaptionChange();
    }

    void QuickSiftApplicationImpl::UpdateDateFilterButton() {
        static const wchar_t* labels[] = {
            L"Date: Any", L"Date: 24h", L"Date: 7d", L"Date: 30d", L"Date: 1y"
        };
        const std::wstring label = Tr(labels[std::clamp(static_cast<int>(dateFilter_), 0, 4)]);
        SetWindowTextW(controls_[ID_DATE_FILTER], label.c_str());
    }

    void QuickSiftApplicationImpl::ShowDateFilterMenu() {
        const int selected = static_cast<int>(dateFilter_);
        std::vector<MenuItem> items = {
            MakeMenuItem(L"Any modified date", ID_DATE_ANY, selected == 0),
            MakeMenuItem(L"Modified in last 24 hours", ID_DATE_24H, selected == 1),
            MakeMenuItem(L"Modified in last 7 days", ID_DATE_7D, selected == 2),
            MakeMenuItem(L"Modified in last 30 days", ID_DATE_30D, selected == 3),
            MakeMenuItem(L"Modified in last year", ID_DATE_1Y, selected == 4),
        };
        const int command = TrackDropdownMenu(controls_[ID_DATE_FILTER], std::move(items));
        if (command < ID_DATE_ANY || command > ID_DATE_1Y) return;
        dateFilter_ = static_cast<DateFilter>(command - ID_DATE_ANY);
        UpdateDateFilterButton();
        SaveSession();
        RebuildVisiblePhotos();
        ScheduleVisibleWork();
        UpdateStatus();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::UpdateSortButton() {
        static const wchar_t* labels[] = {
            L"Sort: A–Z", L"Sort: Z–A", L"Sort: Newest",
            L"Sort: Oldest", L"Sort: Rating", L"Sort: Format"
        };
        const std::wstring label = Tr(labels[std::clamp(static_cast<int>(sortMode_), 0, 5)]);
        SetWindowTextW(controls_[ID_SORT], label.c_str());
        RelayoutTitleControlsAfterCaptionChange();
    }

    void QuickSiftApplicationImpl::ShowSortMenu() {
        const int selected = static_cast<int>(sortMode_);
        std::vector<MenuItem> items = {
            MakeMenuItem(L"Name A–Z", ID_SORT_NAME_ASC, selected == 0),
            MakeMenuItem(L"Name Z–A", ID_SORT_NAME_DESC, selected == 1),
            MakeMenuItem(L"Newest first", ID_SORT_NEWEST, selected == 2),
            MakeMenuItem(L"Oldest first", ID_SORT_OLDEST, selected == 3),
            MakeMenuItem(L"Rating: high first", ID_SORT_RATING, selected == 4),
            MakeMenuItem(L"File format", ID_SORT_EXTENSION, selected == 5),
        };
        const int command = TrackDropdownMenu(controls_[ID_SORT], std::move(items));
        if (command < ID_SORT_NAME_ASC || command > ID_SORT_EXTENSION) return;
        sortMode_ = static_cast<SortMode>(command - ID_SORT_NAME_ASC);
        UpdateSortButton();
        SortPhotos();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::UpdateThumbnailSizeButton() {
        static const wchar_t* labels[] = {
            L"Thumbnail size: XS", L"Thumbnail size: S", L"Thumbnail size: M",
            L"Thumbnail size: L", L"Thumbnail size: XL"
        };
        const std::wstring label = Tr(labels[std::clamp(reviewState_.Thumbnails().sizeIndex, 0, 4)]);
        SetWindowTextW(controls_[ID_THUMB_SIZE], label.c_str());
        RelayoutTitleControlsAfterCaptionChange();
    }

    void QuickSiftApplicationImpl::ShowThumbnailSizeMenu() {
        std::vector<MenuItem> items = {
            MakeMenuItem(L"Tiny", ID_SIZE_TINY, reviewState_.Thumbnails().sizeIndex == 0),
            MakeMenuItem(L"Small", ID_SIZE_SMALL, reviewState_.Thumbnails().sizeIndex == 1),
            MakeMenuItem(L"Medium", ID_SIZE_MEDIUM, reviewState_.Thumbnails().sizeIndex == 2),
            MakeMenuItem(L"Large", ID_SIZE_LARGE, reviewState_.Thumbnails().sizeIndex == 3),
            MakeMenuItem(L"Huge", ID_SIZE_HUGE, reviewState_.Thumbnails().sizeIndex == 4),
        };
        const int command = TrackDropdownMenu(controls_[ID_THUMB_SIZE], std::move(items));
        if (command < ID_SIZE_TINY || command > ID_SIZE_HUGE) return;
        const int nextIndex = command - ID_SIZE_TINY;
        static constexpr int values[] = { 72, 112, 180, 280, 420 };
        PreserveThumbnailAnchorForSizeChange(nextIndex, values[nextIndex]);
        UpdateThumbnailSizeButton();
        ScheduleVisibleWork();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::RequestDeleteOperation() {
        const std::vector<fs::path> active = ActivePaths();
        if (active.empty() || currentFolder_.empty()) return;
        const fs::path destination = currentFolder_ / L"Deleted";
        std::wostringstream message;
        if (localizer_.CurrentLanguage() == quicksift::Language::Vietnamese) {
            message << L"QuickSift sẽ chuyển " << active.size()
                << L" ảnh đã chọn cùng các tệp XMP sidecar tương ứng vào:\n\n"
                << destination.wstring()
                << L"\n\nThao tác này không dùng Thùng rác. Bạn có thể Hoàn tác khi bản ghi lịch sử được bảo vệ vẫn còn khả dụng.";
        } else {
            message << L"QuickSift will move " << active.size() << L" selected photo"
                << (active.size() == 1 ? L"" : L"s")
                << L" and matching XMP sidecars into:\n\n"
                << destination.wstring()
                << L"\n\nThis does not use the Recycle Bin. You can Undo while the protected file-operation history remains available.";
        }
        ShowGlassConfirmation(L"Move selected photos to Deleted?", message.str(),
            L"Move to Deleted", PendingGlassAction::DeleteSelection, GlassAlertKind::Warning);
    }

    void QuickSiftApplicationImpl::FileOperation(bool move, bool remove) {
        if (transactions_.Busy()) {
            ShowGlassAlert(L"A serialized transaction is still running",
                L"QuickSift runs file and metadata transactions serially to prevent races and accidental overwrites.",
                GlassAlertKind::Warning);
            return;
        }
        std::vector<fs::path> active = ActivePaths();
        if (active.empty()) return;
        const std::wstring operationName = remove ? L"delete-to-folder" : (move ? L"move" : L"copy");
        QS_LOG_INFO(L"Files", L"Starting " + operationName + L" for " +
            std::to_wstring(active.size()) + L" selected item(s)");

        bool missingRawCompanion = false;
        if (copyMoveRawWithJpg_ && !remove) {
            struct RawDirectoryIndex {
                bool scanned = false;
                bool succeeded = false;
                std::unordered_map<std::wstring, std::vector<fs::path>> byStem;
            };
            std::unordered_map<std::wstring, RawDirectoryIndex> rawDirectories;
            std::unordered_set<std::wstring> includedPaths;
            includedPaths.reserve(active.size() * 2 + 1);
            for (const fs::path& path : active) includedPaths.insert(NormalizedPathKey(path));

            const std::vector<fs::path> selected = active;
            bool pairingScanFailed = false;
            for (const fs::path& jpeg : selected) {
                if (!IsJpegExtension(ExtensionLower(jpeg))) continue;
                const fs::path parent = jpeg.parent_path();
                const std::wstring directoryKey = NormalizedPathKey(parent);
                RawDirectoryIndex& index = rawDirectories[directoryKey];
                if (!index.scanned) {
                    index.scanned = true;
                    std::error_code scanError;
                    fs::directory_iterator entry(parent, scanError);
                    const fs::directory_iterator end;
                    if (!scanError) {
                        while (entry != end) {
                            std::error_code statusError;
                            const fs::file_status status = entry->symlink_status(statusError);
                            if (statusError) {
                                scanError = statusError;
                                break;
                            }
                            if (fs::is_regular_file(status) &&
                                IsRawExtension(ExtensionLower(entry->path()))) {
                                index.byStem[ToLower(entry->path().stem().wstring())].push_back(entry->path());
                            }
                            entry.increment(scanError);
                            if (scanError) break;
                        }
                    }
                    index.succeeded = !scanError;
                    if (index.succeeded) {
                        for (auto& [stem, matches] : index.byStem) {
                            (void)stem;
                            std::sort(matches.begin(), matches.end(), [](const fs::path& left, const fs::path& right) {
                                return ToLower(left.filename().wstring()) < ToLower(right.filename().wstring());
                            });
                        }
                    }
                }
                if (!index.succeeded) {
                    pairingScanFailed = true;
                    break;
                }

                const auto matches = index.byStem.find(ToLower(jpeg.stem().wstring()));
                if (matches == index.byStem.end() || matches->second.empty()) {
                    missingRawCompanion = true;
                    continue;
                }
                for (const fs::path& raw : matches->second) {
                    if (includedPaths.insert(NormalizedPathKey(raw)).second) active.push_back(raw);
                }
            }
            if (pairingScanFailed) {
                ShowGlassAlert(L"RAW pairing check failed — nothing changed",
                    L"QuickSift could not safely inspect the source folder for corresponding RAW files. It stopped before copying or moving anything.",
                    GlassAlertKind::Warning);
                return;
            }
        }

        fs::path destinationFolder;
        if (remove) {
            destinationFolder = currentFolder_ / L"Deleted";
        } else {
            const std::wstring destinationTitle = Tr(move ? L"Move selected photos to" : L"Copy selected photos to");
            const auto destination = PickFolder(hwnd_, destinationTitle.c_str());
            if (!destination) return;
            destinationFolder = *destination;
        }

        std::error_code destinationStatusError;
        const fs::file_status destinationStatus = fs::symlink_status(destinationFolder, destinationStatusError);
        if (!destinationStatusError && fs::exists(destinationStatus) &&
            (fs::is_symlink(destinationStatus) || !fs::is_directory(destinationStatus))) {
            ShowGlassAlert(remove ? L"Deleted folder conflict" : L"Destination conflict",
                L"The destination path is not a normal folder. QuickSift stopped before changing any files.",
                GlassAlertKind::Warning);
            return;
        }
        if (destinationStatusError && destinationStatusError != std::errc::no_such_file_or_directory) {
            ShowGlassAlert(remove ? L"Deleted folder unavailable" : L"Destination unavailable",
                L"QuickSift could not safely inspect the destination folder. No files were changed.",
                GlassAlertKind::Error);
            return;
        }

        struct PlannedTransfer {
            fs::path source;
            fs::path destination;
            fs::path sourceSidecar;
            fs::path destinationSidecar;
            bool sidecarPresent = false;
        };
        std::vector<PlannedTransfer> plan;
        plan.reserve(active.size());
        std::unordered_set<std::wstring> plannedDestinations;
        plannedDestinations.reserve(active.size() * 2 + 1);
        std::unordered_set<std::wstring> operationSourceKeys;
        operationSourceKeys.reserve(active.size() * 2 + 1);
        for (const fs::path& source : active) operationSourceKeys.insert(NormalizedPathKey(source));
        // Sidecar destination ownership is tracked separately from image names.
        // Several selected RAW owners may share the same source/destination XMP;
        // that is one atomic media group, not a collision.
        std::unordered_map<std::wstring, std::wstring> plannedSidecarSources;
        plannedSidecarSources.reserve(active.size() + 1);
        std::wstring conflictDetail;
        int conflictCount = 0;

        auto recordConflict = [&](const fs::path& path, const std::wstring& reason) {
            ++conflictCount;
            if (conflictDetail.empty()) {
                conflictDetail = reason + L"\n\n" + path.filename().wstring();
            }
        };

        struct RawSidecarDirectoryIndex {
            bool scanned = false;
            bool succeeded = false;
            std::unordered_map<std::wstring, std::vector<std::wstring>> ownersBySidecar;
        };
        std::unordered_map<std::wstring, RawSidecarDirectoryIndex> rawSidecarDirectories;
        rawSidecarDirectories.reserve(4);
        auto mediaSidecarHasExternalOwner = [&](const fs::path& source,
            const fs::path& sidecar, bool& scanSucceeded) {
            const fs::path parent = source.parent_path();
            RawSidecarDirectoryIndex& index = rawSidecarDirectories[NormalizedPathKey(parent)];
            if (!index.scanned) {
                index.scanned = true;
                std::error_code scanError;
                fs::directory_iterator sibling(parent, scanError);
                const fs::directory_iterator end;
                while (!scanError && sibling != end) {
                    std::error_code typeError;
                    const fs::file_status type = sibling->symlink_status(typeError);
                    if (typeError) {
                        scanError = typeError;
                        break;
                    }
                    const std::wstring extension = ExtensionLower(sibling->path());
                    const quicksift::core::FormatSelection allSupportedFormats;
                    if (fs::is_regular_file(type) &&
                        quicksift::core::IsFormatAllowed(extension, allSupportedFormats)) {
                        index.ownersBySidecar[NormalizedPathKey(SidecarPathFor(sibling->path()))]
                            .push_back(NormalizedPathKey(sibling->path()));
                    }
                    sibling.increment(scanError);
                }
                index.succeeded = !scanError;
            }
            scanSucceeded = index.succeeded;
            if (!index.succeeded) return false;
            const auto owners = index.ownersBySidecar.find(NormalizedPathKey(sidecar));
            if (owners == index.ownersBySidecar.end()) return false;
            return std::any_of(owners->second.begin(), owners->second.end(),
                [&](const std::wstring& owner) { return !operationSourceKeys.contains(owner); });
        };

        // Preflight the complete batch before creating folders or touching any
        // image. A single collision stops the whole operation: no overwrite,
        // no automatic “(2)” name, and no partially processed selection.
        for (const fs::path& source : active) {
            std::error_code ec;
            const fs::file_status sourceStatus = fs::symlink_status(source, ec);
            if (ec || fs::is_symlink(sourceStatus) || !fs::is_regular_file(sourceStatus)) {
                recordConflict(source, fs::is_symlink(sourceStatus) ?
                    L"Symbolic-link image paths are not moved or deleted for safety." :
                    L"The source file is missing or inaccessible.");
                continue;
            }

            PlannedTransfer transfer;
            transfer.source = source;
            transfer.destination = destinationFolder / source.filename();
            transfer.sourceSidecar = SidecarPathFor(source);
            transfer.destinationSidecar = SidecarPathFor(transfer.destination);

            bool transferValid = true;
            ec.clear();
            const fs::file_status sidecarStatus = fs::symlink_status(transfer.sourceSidecar, ec);
            if (ec && ec != std::errc::no_such_file_or_directory) {
                recordConflict(transfer.sourceSidecar,
                    L"QuickSift could not safely inspect the matching XMP sidecar.");
                transferValid = false;
            } else if (!ec && fs::exists(sidecarStatus)) {
                if (fs::is_symlink(sidecarStatus)) {
                    recordConflict(transfer.sourceSidecar,
                        L"The matching XMP is a symbolic link and was left untouched for safety.");
                    transferValid = false;
                } else if (!fs::is_regular_file(sidecarStatus)) {
                    recordConflict(transfer.sourceSidecar,
                        L"The matching XMP path is not a normal file.");
                    transferValid = false;
                } else {
                    transfer.sidecarPresent = true;
                }
            }
            if (!transferValid) continue;

            if ((move || remove) && transfer.sidecarPresent) {
                bool scanSucceeded = false;
                const bool sharedSidecar = mediaSidecarHasExternalOwner(
                    source, transfer.sourceSidecar, scanSucceeded);
                if (!scanSucceeded) {
                    recordConflict(source.parent_path(),
                        L"QuickSift could not finish checking whether this XMP is shared.");
                    continue;
                }
                if (sharedSidecar) {
                    recordConflict(transfer.sourceSidecar,
                        L"This XMP sidecar is shared by another media file, so moving it would detach metadata from that file.");
                    continue;
                }
            }

            if (NormalizedPathKey(source) == NormalizedPathKey(transfer.destination)) {
                recordConflict(transfer.destination, L"The destination is the same as the source.");
                continue;
            }
            bool destinationIsSymlink = false;
            if (PathEntryExists(transfer.destination, &destinationIsSymlink)) {
                recordConflict(transfer.destination, destinationIsSymlink ?
                    L"A symbolic link already occupies the destination filename." :
                    L"A file with the same name already exists at the destination.");
                continue;
            }
            bool sidecarDestinationIsSymlink = false;
            if (PathEntryExists(transfer.destinationSidecar, &sidecarDestinationIsSymlink)) {
                recordConflict(transfer.destinationSidecar, sidecarDestinationIsSymlink ?
                    L"A symbolic link already occupies the destination XMP filename." :
                    (transfer.sidecarPresent ? L"A matching XMP sidecar already exists at the destination." :
                        L"An XMP sidecar already exists for this filename, but the selected source has no matching sidecar."));
                continue;
            }

            const std::wstring imageKey = NormalizedPathKey(transfer.destination);
            if (!plannedDestinations.insert(imageKey).second) {
                recordConflict(transfer.destination, L"Two selected files would produce the same destination name.");
                continue;
            }
            if (transfer.sidecarPresent) {
                const std::wstring sidecarKey = NormalizedPathKey(transfer.destinationSidecar);
                const std::wstring sourceSidecarKey = NormalizedPathKey(transfer.sourceSidecar);
                const auto [existing, inserted] = plannedSidecarSources.emplace(
                    sidecarKey, sourceSidecarKey);
                if (!inserted && existing->second != sourceSidecarKey) {
                    recordConflict(transfer.destinationSidecar,
                        L"Different source sidecars would produce the same destination XMP name.");
                    continue;
                }
            }
            plan.push_back(std::move(transfer));
        }

        if (conflictCount > 0 || plan.size() != active.size()) {
            std::wostringstream message;
            message << (remove ? L"Delete-to-folder" : (move ? L"Move" : L"Copy"))
                << L" was stopped before any files were changed.\n\n"
                << conflictDetail;
            if (conflictCount > 1) message << L"\n\nPlus " << (conflictCount - 1) << L" other conflict(s).";
            message << L"\n\nQuickSift never overwrites or automatically renames files.";
            ShowGlassAlert(L"File conflict — nothing changed", message.str(), GlassAlertKind::Warning);
            return;
        }

        // The plan owns the paths from here. Release duplicate selection and
        // collision-check storage before potentially long filesystem work.
        active.clear();
        active.shrink_to_fit();
        std::unordered_set<std::wstring>().swap(plannedDestinations);

        // Verified copies need room for the complete destination bytes plus a
        // reserve for filesystem metadata and other applications. Same-volume
        // renames do not duplicate file data, while cross-volume moves do.
        std::uint64_t requiredDestinationBytes = 0;
        bool storageSizeKnown = true;
        const auto destinationVolume = VolumeRootForPath(destinationFolder);
        std::unordered_set<std::wstring> sizedSidecars;
        sizedSidecars.reserve(plan.size() + 1);
        for (const PlannedTransfer& transfer : plan) {
            const auto sourceVolume = VolumeRootForPath(transfer.source);
            const bool duplicatesBytes = !move && !remove || !sourceVolume || !destinationVolume ||
                *sourceVolume != *destinationVolume;
            if (!duplicatesBytes) continue;
            std::error_code sizeError;
            const std::uintmax_t imageBytes = fs::file_size(transfer.source, sizeError);
            if (sizeError) { storageSizeKnown = false; break; }
            requiredDestinationBytes = SaturatingAdd(requiredDestinationBytes,
                static_cast<std::uint64_t>(imageBytes));
            if (transfer.sidecarPresent &&
                sizedSidecars.insert(NormalizedPathKey(transfer.sourceSidecar)).second) {
                sizeError.clear();
                const std::uintmax_t sidecarBytes = fs::file_size(transfer.sourceSidecar, sizeError);
                if (sizeError) { storageSizeKnown = false; break; }
                requiredDestinationBytes = SaturatingAdd(requiredDestinationBytes,
                    static_cast<std::uint64_t>(sidecarBytes));
            }
        }
        if (!storageSizeKnown) {
            ShowGlassAlert(L"Storage check failed — nothing changed",
                L"QuickSift could not determine the complete size of the selected files. It stopped before copying or moving anything.",
                GlassAlertKind::Warning);
            return;
        }
        if (requiredDestinationBytes > 0) {
            constexpr std::uint64_t kCopyReserve = 128ull * 1024ull * 1024ull;
            std::uint64_t available = 0;
            if (!HasStorageHeadroom(destinationFolder, requiredDestinationBytes, kCopyReserve, &available)) {
                std::wostringstream message;
                message << (remove ? L"Delete-to-folder" : (move ? L"Move" : L"Copy"))
                    << L" was stopped before any files were changed.\n\n"
                    << L"Required working space: "
                    << FormatFileSize(SaturatingAdd(requiredDestinationBytes, kCopyReserve))
                    << L"\nAvailable space: " << FormatFileSize(available)
                    << L"\n\nQuickSift needs enough room to finish and verify every copied byte before publishing the destination filenames.";
                ShowGlassAlert(L"Low storage — nothing changed", message.str(), GlassAlertKind::Warning);
                return;
            }
        }

        std::error_code createError;
        fs::create_directories(destinationFolder, createError);
        if (createError) {
            ShowGlassAlert(remove ? L"Deleted folder unavailable" : L"Destination unavailable",
                L"QuickSift could not create or access the destination folder. No files were changed.",
                GlassAlertKind::Error);
            return;
        }

        const std::wstring historyLabel = remove ? L"Delete to subfolder" :
            (move ? L"Move files" : L"Copy files");
        std::vector<std::pair<std::size_t, std::size_t>> historyPathLengths;
        historyPathLengths.reserve(plan.size() * 2);
        std::unordered_set<std::wstring> historySidecars;
        historySidecars.reserve(plan.size() + 1);
        for (const PlannedTransfer& transfer : plan) {
            historyPathLengths.emplace_back(transfer.source.native().size(),
                transfer.destination.native().size());
            if (transfer.sidecarPresent &&
                historySidecars.insert(NormalizedPathKey(transfer.sourceSidecar)).second) {
                historyPathLengths.emplace_back(transfer.sourceSidecar.native().size(),
                    transfer.destinationSidecar.native().size());
            }
        }
        const std::size_t estimatedHistoryBytes =
            quicksift::core::HistoryStore::EstimateFileBytesForPathLengths(
                historyPathLengths, historyLabel);

        quicksift::transactions::FileTransactionPlan transaction;
        transaction.label = historyLabel;
        transaction.copy = !move && !remove;
        transaction.deleteToFolder = remove;
        transaction.recordHistory = estimatedHistoryBytes <= historyStore_.MemoryLimit();
        transaction.missingRawCompanion = missingRawCompanion;
        transaction.groups.reserve(plan.size());
        std::unordered_map<std::wstring, std::size_t> mediaGroupBySidecar;
        mediaGroupBySidecar.reserve(plan.size() + 1);
        for (PlannedTransfer& transfer : plan) {
            if (!transfer.sidecarPresent) {
                quicksift::transactions::TransferGroup group;
                group.owners.push_back({ std::move(transfer.source),
                    std::move(transfer.destination) });
                transaction.groups.push_back(std::move(group));
                continue;
            }

            const std::wstring sidecarKey = NormalizedPathKey(transfer.sourceSidecar);
            const auto [iterator, inserted] = mediaGroupBySidecar.emplace(
                sidecarKey, transaction.groups.size());
            if (inserted) {
                quicksift::transactions::TransferGroup group;
                group.companion = quicksift::transactions::TransferItem{
                    std::move(transfer.sourceSidecar),
                    std::move(transfer.destinationSidecar) };
                transaction.groups.push_back(std::move(group));
            }
            transaction.groups[iterator->second].owners.push_back({
                std::move(transfer.source), std::move(transfer.destination) });
        }

        const std::uint64_t token = transactions_.SubmitFile(transaction);
        if (token == 0) {
            ShowGlassAlert(L"Another file operation is already running",
                L"QuickSift serializes verified file transactions so two operations cannot race over the same media. The new request was not started.",
                GlassAlertKind::Warning);
            return;
        }
        StartUiTimer(ID_TIMER_FILE_PROGRESS, 1000, L"file-operation progress");
        UpdateStatus();
        ShowToast(remove ? L"Moving selected files to Deleted…" :
            (move ? L"Moving and verifying…" : L"Copying and verifying…"));
    }

    void QuickSiftApplicationImpl::DrainFileTransactionResults() {
        if (auto result = transactions_.TakeFileResult()) {
            FinishFileTransaction(std::move(*result));
        }
    }

    void QuickSiftApplicationImpl::FinishFileTransaction(
        quicksift::transactions::FileTransactionResult result) {
        if (!transactions_.AcceptFileResult(result.token)) {
            QS_LOG_WARNING(L"Files", L"Discarded a stale file-transaction completion");
            return;
        }
        KillTimer(hwnd_, ID_TIMER_FILE_PROGRESS);
        UpdateStatus();

        if (result.mode != quicksift::transactions::FileTransactionMode::Transfer) {
            const bool undoReplay = result.mode ==
                quicksift::transactions::FileTransactionMode::UndoHistory;
            HistoryEntry replayed;
            replayed.kind = HistoryEntry::Kind::Files;
            replayed.label = result.label;
            replayed.files = std::move(result.historyItems);
            HistoryEntry completed = FileHistorySubset(
                replayed, result.replayCompleted, true);
            HistoryEntry remaining = FileHistorySubset(
                replayed, result.replayCompleted, false);
            const bool offerExternalReconcile =
                (result.failed || result.cancelled) &&
                HistoryEntryHasExternalIdentityDivergence(remaining, undoReplay);

            replayingHistory_ = false;
            historyStore_.CompleteReplay(
                undoReplay ? quicksift::core::HistoryDirection::Undo :
                    quicksift::core::HistoryDirection::Redo,
                std::move(completed), std::move(remaining));
            RefreshAfterHistory();

            if (transactions_.ConsumeCloseRequestIfIdle()) {
                SaveSession();
                DestroyWindow(hwnd_);
                return;
            }

            if (result.failed || result.cancelled) {
                std::wstring message = undoReplay ?
                    L"Undo stopped at a guarded file boundary. Completed items remain available in Redo; untouched items remain available in Undo." :
                    L"Redo stopped at a guarded file boundary. Completed items remain available in Undo; untouched items remain available in Redo.";
                if (!result.detail.empty()) message += L"\n\n" + result.detail;
                if (offerExternalReconcile) {
                    OfferExternalChangeHistoryReconcile(
                        undoReplay ? HistoryDirection::Undo : HistoryDirection::Redo,
                        message);
                } else {
                    ShowGlassAlert(undoReplay ? L"Undo stopped for safety" : L"Redo stopped for safety",
                        message, result.failed ? GlassAlertKind::Error : GlassAlertKind::Warning);
                }
            } else {
                ShowToast(undoReplay ? L"File Undo complete" : L"File Redo complete");
            }
            return;
        }

        if (result.recordHistory && !result.historyItems.empty()) {
            HistoryEntry history;
            history.kind = HistoryEntry::Kind::Files;
            history.label = result.label;
            history.files = std::move(result.historyItems);
            PushHistory(std::move(history));
        } else {
            FinishUnrecordedAction(result.lastingTransferItems > 0);
        }

        if (!result.copy && result.lastingTransferItems > 0) {
            selection_.Clear();
            if (!currentFolder_.empty()) LoadFolder(currentFolder_);
        } else {
            UpdateStatus();
            UpdateHistoryButtons();
        }

        if (transactions_.ConsumeCloseRequestIfIdle()) {
            if (shutdownBlocked_) {
                ShutdownBlockReasonDestroy(hwnd_);
                shutdownBlocked_ = false;
            }
            SaveSession();
            DestroyWindow(hwnd_);
            return;
        }

        const std::wstring operationName = result.deleteToFolder ? L"delete-to-folder" :
            (result.copy ? L"copy" : L"move");
        if (result.failed) {
            QS_LOG_ERROR(L"Files", operationName + L" stopped after " +
                std::to_wstring(result.lastingTransferItems) + L" successful transfer item(s)");
            std::wstring message = result.recordHistory ?
                L"A filesystem error occurred. QuickSift stopped at a safe boundary and retained verified completed files in Undo." :
                L"A filesystem error occurred. QuickSift stopped at a safe boundary. This unusually large batch was not retained in memory for Undo.";
            if (!result.detail.empty()) message += L"\n\n" + result.detail;
            ShowGlassAlert(L"File operation stopped", message, GlassAlertKind::Error);
            return;
        }
        if (result.cancelled) {
            std::wstring message = L"The file operation was cancelled at a safe boundary. Already verified files were preserved and remain guarded by Undo when history capacity allowed.";
            if (!result.detail.empty()) message += L"\n\n" + result.detail;
            ShowGlassAlert(L"File operation cancelled", message, GlassAlertKind::Warning);
            return;
        }
        if (result.lastingTransferItems > 0 && !result.deleteToFolder) {
            QS_LOG_INFO(L"Files", operationName + L" completed with " +
                std::to_wstring(result.lastingTransferItems) + L" transfer item(s)");
            if (!result.detail.empty()) {
                ShowGlassAlert(result.copy ? L"Copy completed with a warning" :
                    L"Move completed with a warning", result.detail, GlassAlertKind::Warning);
            } else {
                ShowToast(result.copy ? L"Done copying" : L"Done moving");
            }
            if (result.missingRawCompanion) {
                ShowToast(L"At least one corresponding RAW(s) is missing, please check.");
            }
        }
    }

} // namespace quicksift::app
