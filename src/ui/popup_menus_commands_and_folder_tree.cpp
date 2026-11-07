// OWNER: Compiled QuickSiftApplication feature module.
#include "app/quicksift_application_internal.h"

#include <cmath>

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

} // namespace

// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Popup menus, command dispatch, folder tree, visible-catalog rebuild, and status text.

// UI command layer: custom popup menus, command dispatch, notifications, folder
// tree population, visible-catalog rebuilding, status text, and invalidation.

std::function<void()> QuickSiftApplicationImpl::DropdownMaintenancePump() {
    return [this] {
        // TrackDropdown owns a deliberately narrow nested message loop. Remove
        // its wake-up messages and integrate results directly so decoded pixel
        // buffers cannot pile up while any custom menu remains open.
        MSG wake{};
        while (PeekMessageW(&wake, hwnd_, WM_APP_BACKGROUND_COMPLETION,
            WM_APP_BACKGROUND_COMPLETION, PM_REMOVE)) {
        }
        if (completionQueue_.Size() != 0) DrainBackgroundCompletions();
    };
}


int QuickSiftApplicationImpl::TrackDropdownMenu(HWND anchor, std::vector<MenuItem> items) {
    if (!anchor || items.empty()) return 0;
    for (MenuItem& item : items) {
        if (!item.separator && !item.text.empty()) item.text = Tr(item.text);
    }

    quicksift::ui::framework::DropdownRequest request{};
    request.owner = hwnd_;
    request.anchor = anchor;
    request.instance = instance_;
    request.palette = Palette();
    request.font = uiFont_;
    request.dpi = currentDpi_;
    request.items = std::move(items);
    request.maintenancePump = DropdownMaintenancePump();
    request.errorSink = [](std::wstring_view operation) {
        quicksift::diagnostics::WriteLastError(quicksift::diagnostics::Level::Error,
            L"UI", operation);
    };

    const quicksift::core::ScopedBooleanFlag menuGuard(menuOpen_);
    fullscreenLeaveSince_ = {};
    const int result = uiFramework_.TrackDropdown(std::move(request));
    SetFocus(canvas_ ? canvas_ : hwnd_);
    lastFlyoutHover_ = std::chrono::steady_clock::now();
    return result;
}

// A dropdown owned by another top-level window of ours (the diagnostic log
// window). Item text is data (file labels), so it is shown as given.
int QuickSiftApplicationImpl::TrackDropdownMenuVerbatim(HWND owner, HWND anchor, std::vector<MenuItem> items) {
    if (!owner || !anchor || items.empty()) return 0;
    quicksift::ui::framework::DropdownRequest request{};
    request.owner = owner;
    request.anchor = anchor;
    request.instance = instance_;
    request.palette = Palette();
    request.font = uiFont_;
    request.dpi = quicksift::ui::GetWindowDpi(owner);
    request.items = std::move(items);
    request.maintenancePump = DropdownMaintenancePump();
    request.errorSink = [](std::wstring_view operation) {
        quicksift::diagnostics::WriteLastError(quicksift::diagnostics::Level::Error,
            L"UI", operation);
    };
    const quicksift::core::ScopedBooleanFlag menuGuard(menuOpen_);
    const int result = uiFramework_.TrackDropdown(std::move(request));
    if (IsWindow(owner)) SetFocus(owner);
    return result;
}


void QuickSiftApplicationImpl::UpdateLanguageSelectorButton() {
    if (!languageSelector_) return;
    const wchar_t* caption = localizer_.CurrentLanguage() == quicksift::Language::Vietnamese
        ? L"Tiếng Việt" : L"English"; // loc-native: matches the VI help heading / native language name
    // Only on change: WM_SETTEXT repaints a visible button synchronously.
    wchar_t current[64]{};
    GetWindowTextW(languageSelector_, current, static_cast<int>(std::size(current)));
    if (std::wcscmp(current, caption) != 0) SetWindowTextW(languageSelector_, caption);
}

void QuickSiftApplicationImpl::ShowLanguageMenu() {
    std::vector<MenuItem> items = {
        MakeMenuItem(L"English", ID_SETTING_LANGUAGE_ENGLISH,
            localizer_.CurrentLanguage() == quicksift::Language::English),
        MakeMenuItem(L"Vietnamese", ID_SETTING_LANGUAGE_VIETNAMESE,
            localizer_.CurrentLanguage() == quicksift::Language::Vietnamese),
    };
    const int command = TrackDropdownMenu(languageSelector_, std::move(items));
    if (command == ID_SETTING_LANGUAGE_ENGLISH || command == ID_SETTING_LANGUAGE_VIETNAMESE) {
        const auto next = command == ID_SETTING_LANGUAGE_VIETNAMESE
            ? quicksift::Language::Vietnamese : quicksift::Language::English;
        if (localizer_.CurrentLanguage() != next) {
            localizer_.SetLanguage(next);
            UpdateLanguageSelectorButton();
            RefreshLocalizedText();
            SaveSession();
            ShowToast(L"Language changed.");
        }
    }
}


    // OnCommand dispatch (0.9.5.0 round 19: split into cohesive helpers with no
    // behaviour change). The commands handled before the alert guard run first and
    // skip MarkInteraction exactly as before; every other command is routed through
    // the grouped dispatchers below in the original switch order.
    void QuickSiftApplicationImpl::OnCommand(int id, int notifyCode, HWND source) {
        (void)source;
        if (HandleCommandBeforeAlertGuard(id, notifyCode)) return;
        if (uiFramework_.Alerts().Visible()) return;
        MarkInteraction();
        if (DispatchNavigationCommand(id)) return;
        if (DispatchCullingCommand(id)) return;
        if (DispatchFilterMenuCommand(id)) return;
        if (DispatchViewerCommand(id)) return;
        DispatchApplicationCommand(id);
    }

    bool QuickSiftApplicationImpl::HandleCommandBeforeAlertGuard(int id, int notifyCode) {
        if (id == ID_THUMBNAIL_SEARCH) {
            if (notifyCode == EN_CHANGE) OnSearchTextChanged();
            return true;
        }
        if (id == ID_SETTING_LANGUAGE_SELECTOR) { ShowLanguageMenu(); return true; }
        if (id == ID_SAVE_USER_COMMENT) { SaveUserComment(); return true; }
        if (id == ID_LIBRARY_LIST && notifyCode == LBN_SELCHANGE) {
            OnLibraryListSelectionChanged();
            return true;
        }
        if (id == ID_REMOVE_LIBRARY) {
            RemoveSelectedLibraryFolder();
            return true;
        }
        return false;
    }

    void QuickSiftApplicationImpl::OnLibraryListSelectionChanged() {
        const int sel=static_cast<int>(SendMessageW(libraryList_,LB_GETCURSEL,0,0));
        if (controls_.contains(ID_REMOVE_LIBRARY)) EnableWindow(controls_[ID_REMOVE_LIBRARY], sel != LB_ERR);
        if(sel!=LB_ERR && sel>=0 && sel<static_cast<int>(libraryPaths_.size())) { fs::path folder(libraryPaths_[sel]); if(folder!=currentFolder_) LoadFolder(folder); }
    }

    void QuickSiftApplicationImpl::RemoveSelectedLibraryFolder() {
        const int sel=static_cast<int>(SendMessageW(libraryList_,LB_GETCURSEL,0,0));
        if(sel!=LB_ERR && sel>=0 && sel<static_cast<int>(libraryPaths_.size())) { const fs::path folder(libraryPaths_[sel]); persistentCache_.WipeFolderCache(folder); persistentCache_.RemoveLibraryFolder(folder);
            for(auto it=bitmapCache_.begin(); it!=bitmapCache_.end();) { if(it->first.path.rfind(folder.wstring(),0)==0) { RemoveCacheBytes(it->second.cacheClass,it->second.bytes); UnindexBitmapSize(it->first); it=bitmapCache_.erase(it); } else ++it; }
            RefreshLibraryList(); if(folder==currentFolder_) { currentFolder_.clear(); reviewState_.ResetForFolder(); UpdateStatus(); InvalidateCanvas(); } }
    }

    // Folder picking, view modes, flyout tabs, navigation and file operations.
    bool QuickSiftApplicationImpl::DispatchNavigationCommand(int id) {
        switch (id) {
        case ID_PICK_FOLDER:
            ChooseFolder();
            return true;
        case ID_MODE_THUMBS:
            SetMode(ViewMode::Thumbnails);
            return true;
        case ID_MODE_SINGLE:
            EnterSingleFromSelection();
            return true;
        case ID_MODE_COMPARE:
            OnCompareModeCommand();
            return true;
        case ID_FLYOUT_TAB_FILE: SetActiveFlyout(FlyoutPanel::File); return true;
        case ID_FLYOUT_TAB_CULL: SetActiveFlyout(FlyoutPanel::Cull); return true;
        case ID_FLYOUT_TAB_FILTER: SetActiveFlyout(FlyoutPanel::Filter); return true;
        case ID_FLYOUT_TAB_FOLDERS: SetActiveFlyout(FlyoutPanel::Folders); return true;
        case ID_FLYOUT_TAB_INFO: SetActiveFlyout(FlyoutPanel::Info); return true;
        case ID_FLYOUT_TAB_SETTINGS: SetActiveFlyout(FlyoutPanel::Settings); return true;
        case ID_PREVIOUS:
            Navigate(-1, false);
            SetFocus(canvas_);
            return true;
        case ID_NEXT:
            Navigate(1, false);
            SetFocus(canvas_);
            return true;
        case ID_COPY:
            FileOperation(false, false);
            return true;
        case ID_MOVE:
            FileOperation(true, false);
            return true;
        case ID_DELETE:
            RequestDeleteOperation();
            return true;
        default:
            return false;
        }
    }

    void QuickSiftApplicationImpl::OnCompareModeCommand() {
        if (reviewState_.Mode() == ViewMode::Compare) {
            SetMode(ViewMode::Thumbnails);
        } else if (reviewState_.Mode() == ViewMode::Single) {
            // Leaving Single via Compare returns to Thumbnails with the selection
            // guidance toast (Compare needs 2–6 selected photos).
            SetMode(ViewMode::Thumbnails);
            ShowToast(L"Select 2 to 6 photos to enter Compare.");
        } else {
            EnterCompareFromSelection();
        }
    }

    // Ratings, pick state and colour labels.
    bool QuickSiftApplicationImpl::DispatchCullingCommand(int id) {
        switch (id) {
        case ID_RATE_CLEAR:
        case ID_RATE_1:
        case ID_RATE_2:
        case ID_RATE_3:
        case ID_RATE_4:
        case ID_RATE_5:
            ApplyRating(id == ID_RATE_CLEAR ? 0 : id - ID_RATE_1 + 1);
            return true;
        case ID_PICK_STATE_MENU:
            ShowPickStateMenu();
            return true;
        case ID_PICK_STATE_PICK:
            ApplyPickState(1);
            return true;
        case ID_PICK_STATE_REJECT:
            ApplyPickState(-1);
            return true;
        case ID_PICK_STATE_UNMARK:
            ApplyPickState(0);
            return true;
        case ID_COLOR_LABEL:
            ShowColorLabelMenu();
            return true;
        case ID_RATING_MENU:
            ShowRatingMenu();
            return true;
        default:
            return false;
        }
    }

    // Formats, date, sort, thumbnail size and the metadata filter menus.
    bool QuickSiftApplicationImpl::DispatchFilterMenuCommand(int id) {
        switch (id) {
        case ID_FORMATS: ShowFormatsMenu(); return true;
        case ID_DATE_FILTER: ShowDateFilterMenu(); return true;
        case ID_SORT: ShowSortMenu(); return true;
        case ID_THUMB_SIZE: ShowThumbnailSizeMenu(); return true;
        case ID_STAR_FILTER: ShowRatingFilterMenu(); return true;
        case ID_PICK_FILTER: ShowPickFilterMenu(); return true;
        case ID_LABEL_FILTER: ShowColorLabelFilterMenu(); return true;
        case ID_ISO_FILTER: ShowIsoFilterMenu(); return true;
        case ID_APERTURE_FILTER: ShowApertureFilterMenu(); return true;
        case ID_FOCAL_FILTER: ShowFocalFilterMenu(); return true;
        case ID_SHUTTER_FILTER: ShowShutterFilterMenu(); return true;
        case ID_CAMERA_FILTER: ShowCameraFilterMenu(); return true;
        case ID_HOUR_FILTER: ShowHourFilterMenu(); return true;
        default: return false;
        }
    }

    // Zoom, rotation, compare sync and face lock.
    bool QuickSiftApplicationImpl::DispatchViewerCommand(int id) {
        switch (id) {
        case ID_ZOOM_MODE:
            ShowZoomModeMenu();
            return true;
        case ID_ROTATE_LEFT:
            RotateView(-90);
            return true;
        case ID_ROTATE_RIGHT:
            RotateView(90);
            return true;
        case ID_SYNC_VIEW:
            ToggleSyncCompareViewCommand();
            return true;
        case ID_FACE_LOCK:
            ToggleFaceLockCommand();
            return true;
        default:
            return false;
        }
    }

    void QuickSiftApplicationImpl::ToggleSyncCompareViewCommand() {
        ClearFaceZoomToggle(); // toggle keys change (per pane <-> shared)
        reviewState_.SetSyncCompareView(!reviewState_.SyncCompareView());
        UpdateToggleButtons();
        UpdateZoomModeButton();
        SaveSession();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::ToggleFaceLockCommand() {
        ClearFaceZoomToggle();
        RefreshFaceDetectorCapability(false);
        if (faceAnalysisRetryPolicy_.Capability() !=
            quicksift::core::FaceDetectorCapability::Available) {
            RefreshFaceLockAvailabilityUi();
            UpdateStatus();
            return;
        }
        if (reviewState_.FaceLockEnabled()) {
            DisableFaceLockPreservingViews();
        } else {
            reviewState_.EnableFaceLock();
            faceAnalysisRetryPolicy_.AllowExplicitRetry(EditableViewPath(),
                quicksift::core::FaceAnalysisRetryPolicy::Clock::now());
            RecenterFaceLockForPath(EditableViewPath());
            SchedulePredictivePrefetch(0);
        }
        UpdateToggleButtons();
        SaveSession();
        InvalidateCanvas();
    }

    // Undo/redo, theme, fullscreen, help, tools and the settings commands.
    void QuickSiftApplicationImpl::DispatchApplicationCommand(int id) {
        switch (id) {
        case ID_UNDO:
            Undo();
            break;
        case ID_REDO:
            Redo();
            break;
        case ID_THEME_TOGGLE:
            ToggleThemeCommand();
            break;
        case ID_FULLSCREEN:
            ToggleFullscreen();
            break;
        case ID_HELP:
            ShowHelp();
            break;
        case ID_RATINGS_EXCHANGE:
            ShowRatingsExchangeChooser();
            break;
        case ID_BATCH_PROCESSING:
            OpenBatchProcessingDialog();
            break;
        case ID_FILMSTRIP:
            ToggleFilmstrip();
            break;
        case ID_SETTING_BADGE_STARS:
        case ID_SETTING_BADGE_COLOR:
        case ID_SETTING_BADGE_PICK:
        case ID_SETTING_BADGE_PAIR:
        case ID_SETTING_COPY_MOVE_RAW_WITH_JPG:
        case ID_SETTING_RAW_JPEG_PREVIEWS_ONLY:
        case ID_SETTING_PERFORMANCE_HUD:
        case ID_SETTING_THEME:
        case ID_SETTING_LANGUAGE_ENGLISH:
        case ID_SETTING_LANGUAGE_VIETNAMESE:
        case ID_SETTING_CLEAR_MEMORY_CACHE:
        case ID_SETTING_CLEAR_DISK_CACHE:
        case ID_SETTING_DIAGNOSTIC_LOG:
        case ID_SETTING_HELP:
        case ID_SETTING_ABOUT:
        case ID_SETTING_DIRECT_JPEG:
        case ID_SETTING_DIRECT_PNG:
        case ID_SETTING_DIRECT_TIFF:
        case ID_SETTING_DIRECT_RAW:
        case ID_SETTING_SAFE_JPEG:
        case ID_SETTING_AUTO_ADVANCE:
        case ID_SETTING_SHORTCUTS:
        case ID_SETTING_SHOW_TIP_AGAIN:
            ApplySettingCommand(id);
            break;
        case ID_SETTING_PICK_LABEL:
            ShowPickLabelMappingMenu();
            break;
        case ID_LEFT_PANE_PIN:
            ToggleLeftPanePinned();
            break;
        default:
            break;
        }
    }

    void QuickSiftApplicationImpl::ToggleThemeCommand() {
        theme_ = theme_ == AppTheme::Dark ? AppTheme::Light : AppTheme::Dark;
        SetWindowTextW(controls_[ID_THEME_TOGGLE], ThemeToggleText().c_str());
        // Do not leave keyboard focus painted inside the appearance pill.
        if (canvas_) SetFocus(canvas_);
        ApplyTheme();
        SaveSession();
    }


    LRESULT QuickSiftApplicationImpl::OnNotify(NMHDR*) { return 0; }

    void QuickSiftApplicationImpl::RefreshLibraryList() {
        if (!libraryList_) return;
        // 0.9.4.0 polish: bounded wait on the cache database; keep the current list
        // and retry shortly instead of blocking the UI thread behind a cache write.
        auto folders = persistentCache_.TryGetLibraryFolders();
        if (!folders) {
            SetTimer(hwnd_, ID_TIMER_LIBRARY_LIST_RETRY, 150, nullptr);
            return;
        }
        SendMessageW(libraryList_, WM_SETREDRAW, FALSE, 0);
        SendMessageW(libraryList_, LB_RESETCONTENT, 0, 0);
        libraryPaths_.clear();
        for (const auto& [path, timestamp] : *folders) {
            (void)timestamp;
            libraryPaths_.push_back(path);
            SendMessageW(libraryList_, LB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(fs::path(path).filename().c_str()));
        }
        const std::wstring current = currentFolder_.wstring();
        for (int i = 0; i < static_cast<int>(libraryPaths_.size()); ++i) {
            if (_wcsicmp(libraryPaths_[i].c_str(), current.c_str()) == 0) {
                SendMessageW(libraryList_, LB_SETCURSEL, i, 0);
                break;
            }
        }
        if (controls_.contains(ID_REMOVE_LIBRARY)) {
            const LRESULT sel = SendMessageW(libraryList_, LB_GETCURSEL, 0, 0);
            EnableWindow(controls_[ID_REMOVE_LIBRARY], sel != LB_ERR);
        }
        SendMessageW(libraryList_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(libraryList_, nullptr, TRUE);
    }

    void QuickSiftApplicationImpl::ChooseFolder() {
        const auto folder = PickFolder(hwnd_, Tr(L"Choose a photo folder").c_str()); if (!folder) return; rootFolder_=*folder; currentFolder_.clear(); LoadFolder(rootFolder_);
    }


    bool QuickSiftApplicationImpl::CullAllowed(const PhotoItem& item) const {
        // Filter tab AND thumbnail search: both evaluate core::predicate clauses over the
        // same authoritative record (see core/image_metadata.h).
        return FilterAllowed(item) && SearchAllows(item);
    }


    bool QuickSiftApplicationImpl::FilterAllowed(const PhotoItem& item) const {
        FormatSelection formatSelection;
        for (const auto& [group, enabled] : formats_) formatSelection.SetEnabled(group, enabled);
        if (!quicksift::core::IsFormatAllowed(item.extension, formatSelection) ||
            !quicksift::core::IsDateAllowed(item.modified, dateFilter_) ||
            !quicksift::core::IsCullAllowed(item, ratingFilter_, pickFilter_, colorLabelFilter_)) {
            return false;
        }
        if (isoFilter_ >= 0 && (!item.cameraSettingsKnown || item.iso != isoFilter_)) return false;
        if (apertureFilter_ > 0.0 && (!item.cameraSettingsKnown ||
            std::abs(item.aperture - apertureFilter_) > 0.05)) return false;
        if (focalFilter_ > 0.0 && (!item.cameraSettingsKnown ||
            std::abs(item.focalLength - focalFilter_) > 0.5)) return false;
        if (shutterFilter_ > 0.0 && (!item.cameraSettingsKnown ||
            std::abs(item.shutterSpeed - shutterFilter_) > shutterFilter_ * 0.05)) return false;
        if (!cameraFilter_.empty() && (!item.cameraSettingsKnown ||
            item.camera.find(cameraFilter_) == std::wstring::npos)) return false;
        if (hourFilter_ >= 0 && (item.captureTimeSource == quicksift::core::predicate::CaptureTimeSource::Unknown ||
            item.captureTime.hour != hourFilter_)) return false;
        if (!formatExtFilter_.empty()) {
            std::wstring ext = item.extension;
            for (wchar_t& ch : ext) ch = static_cast<wchar_t>(std::towlower(ch));
            if (!ext.empty() && ext.front() == L'.') ext.erase(ext.begin());
            if (ext != formatExtFilter_) return false;
        }
        if (!dateExactFilter_.empty()) {
            if (item.captureTimeSource == quicksift::core::predicate::CaptureTimeSource::Unknown) return false;
            std::wstring stamp = std::to_wstring(item.captureTime.year) + L"-" +
                (item.captureTime.month < 10 ? L"0" : L"") + std::to_wstring(item.captureTime.month) + L"-" +
                (item.captureTime.day < 10 ? L"0" : L"") + std::to_wstring(item.captureTime.day);
            if (dateExactFilter_ != stamp) return false;
        }
        return true;
    }


    std::optional<size_t> QuickSiftApplicationImpl::CatalogIndexForPath(std::wstring_view path) const {
        return catalog_.FindPath(path);
    }


    const PhotoItem& QuickSiftApplicationImpl::VisiblePhoto(size_t visibleIndex) const {
        return catalog_.PhotoAt(catalog_.VisibleCatalogIndex(visibleIndex));
    }


    bool QuickSiftApplicationImpl::IsPathVisible(const std::wstring& path) const {
        return IndexForPath(path).has_value();
    }


    void QuickSiftApplicationImpl::RebuildVisiblePhotos() {
        const std::wstring previousCurrent = reviewState_.SinglePath();
        const std::wstring previousFocus = reviewState_.ThumbnailFocusPath();
        const std::optional<size_t> previousCurrentIndex = IndexForPath(previousCurrent);
        const std::optional<size_t> previousFocusIndex = IndexForPath(previousFocus);
        const size_t selectedBeforeFilter = selection_.Size();
        const bool anyFilterActive = ratingFilter_ != RatingFilter::All ||
            pickFilter_ != PickFilter::All ||
            colorLabelFilter_ != ColorLabelFilter::All ||
            dateFilter_ != DateFilter::Any ||
            isoFilter_ >= 0 || apertureFilter_ > 0.0 || focalFilter_ > 0.0 ||
            shutterFilter_ > 0.0 || !cameraFilter_.empty() || hourFilter_ >= 0 ||
            std::any_of(formats_.begin(), formats_.end(),
                [](const auto& entry) { return !entry.second; });
        // One pass: the Filter tab decides the search box's "of N" total, the search
        // (when active) narrows it further.
        std::vector<size_t> visibleOrder;
        visibleOrder.reserve(catalog_.PhotoCount());
        size_t filterCount = 0;
        for (size_t catalogIndex = 0; catalogIndex < catalog_.PhotoCount(); ++catalogIndex) {
            const PhotoItem& photo = catalog_.PhotoAt(catalogIndex);
            if (!FilterAllowed(photo)) continue;
            ++filterCount;
            if (SearchAllows(photo)) visibleOrder.push_back(catalogIndex);
        }
        searchBaseCount_ = filterCount;
        std::stable_sort(visibleOrder.begin(), visibleOrder.end(),
            [this](size_t left, size_t right) {
                return PhotoSortLess(catalog_.PhotoAt(left), catalog_.PhotoAt(right));
            });
        catalog_.SetVisibleOrder(std::move(visibleOrder));
#ifndef NDEBUG
        ValidateVisibleCatalog();
#endif
        if (!previousCurrent.empty() && IsPathVisible(previousCurrent)) {
            reviewState_.SetSinglePath(previousCurrent);
        } else if (catalog_.VisibleEmpty()) {
            reviewState_.SetSinglePath({});
        } else {
            const size_t replacement = std::min(previousCurrentIndex.value_or(0),
                catalog_.VisibleCount() - 1);
            reviewState_.SetSinglePath(VisiblePhoto(replacement).path.wstring());
        }
        if (!previousFocus.empty() && IsPathVisible(previousFocus)) {
            reviewState_.SetThumbnailFocusPath(previousFocus);
        } else if (catalog_.VisibleEmpty()) {
            reviewState_.SetThumbnailFocusPath({});
        } else {
            const size_t replacement = std::min(previousFocusIndex.value_or(
                previousCurrentIndex.value_or(0)), catalog_.VisibleCount() - 1);
            reviewState_.SetThumbnailFocusPath(VisiblePhoto(replacement).path.wstring());
        }
        selection_.Retain([this](const std::wstring& path) {
            return IsPathVisible(path);
        });
        if (anyFilterActive && selection_.Size() < selectedBeforeFilter) {
            ShowToast(Tr(L"Selection is intentionally limited to visible results; hidden photos were removed from the selection."));
        }

        const bool compareExited = reviewState_.RetainComparedPaths(
            [this](const std::wstring& path) { return IsPathVisible(path); });
        if (compareExited) SetMode(ViewMode::Thumbnails);

        if (const auto focusIndex = IndexForPath(reviewState_.ThumbnailFocusPath())) selection_.SetAnchor(*focusIndex);
        else selection_.ClearAnchor();
        UpdateStatus();
    }


    std::wstring QuickSiftApplicationImpl::SingleStatusDetail() const {
        if (reviewState_.Mode() != ViewMode::Single) return {};
        const auto index = IndexForPath(reviewState_.SinglePath());
        if (!index) return {};
        const PhotoItem& photo = VisiblePhoto(*index);
        std::wostringstream detail;
        detail << photo.name;
        if (photo.sourceWidth > 0 && photo.sourceHeight > 0) {
            detail << L"  •  " << photo.sourceWidth << L"×" << photo.sourceHeight;
        }
        detail << L"  •  " << FormatBadgeText(photo)
            << L"  •  " << FormatFileSize(photo.fileSize)
            << L"  •  " << CullStateText(photo)
            << L"  •  " << RatingStateText(photo);
        return detail.str();
    }


    std::wstring QuickSiftApplicationImpl::FileOperationStatusLine() const {
        const auto progress = transactions_.FileProgress();
        // Ratings export/import progress shares the status progress line.
        if (!progress || progress->totalFiles == 0) return RatingsStatusLine();

        const std::wstring action = progress->deleteToFolder ? Tr(L"Deleting") :
            (progress->copy ? Tr(L"Copying") : Tr(L"Moving"));
        const std::wstring completeLabel = Tr(progress->completedFiles == 1 ?
            L"complete file" : L"complete files");
        const std::wstring totalLabel = Tr(progress->totalFiles == 1 ?
            L"total file" : L"total files");

        std::wostringstream text;
        text << action << L" " << progress->completedFiles << L" " << completeLabel
            << L" / " << progress->totalFiles << L" " << totalLabel << L" - ";
        if (progress->completedFiles == 0 || progress->elapsedSeconds <= 0.0) {
            text << Tr(L"calculating time remaining");
        } else if (progress->completedFiles >= progress->totalFiles) {
            text << L"0 " << Tr(L"minutes remaining");
        } else {
            const double remainingFiles = static_cast<double>(
                progress->totalFiles - progress->completedFiles);
            const double secondsPerFile = progress->elapsedSeconds /
                static_cast<double>(progress->completedFiles);
            const auto minutes = std::max<long long>(1, static_cast<long long>(
                std::ceil(secondsPerFile * remainingFiles / 60.0)));
            text << minutes << L" " << Tr(minutes == 1 ?
                L"minute remaining" : L"minutes remaining");
        }
        return text.str();
    }



    void QuickSiftApplicationImpl::UpdateStatus() {
        if (!status_) return;
        std::wostringstream text;
        if (currentFolder_.empty()) {
            text << L"Choose a folder to begin.";
        } else {
            text << catalog_.VisibleCount() << L" photo" << (catalog_.VisibleCount() == 1 ? L"" : L"s");
            if (!selection_.Empty()) text << L"  •  " << selection_.Size() << L" selected";
            if (dateFilter_ != DateFilter::Any) text << L"  •  date filter";
            if (ratingFilter_ != RatingFilter::All) text << L"  •  rating filter";
            if (pickFilter_ != PickFilter::All) text << L"  •  pick filter";
            if (colorLabelFilter_ != ColorLabelFilter::All) text << L"  •  label filter";
            if (std::any_of(formats_.begin(), formats_.end(),
                [](const auto& entry) { return !entry.second; })) text << L"  •  format filter";
            if (pendingRatings_ > 0) text << L"  •  metadata pending";
            const bool selectionFilterActive = ratingFilter_ != RatingFilter::All ||
                pickFilter_ != PickFilter::All || colorLabelFilter_ != ColorLabelFilter::All ||
                dateFilter_ != DateFilter::Any || std::any_of(formats_.begin(), formats_.end(),
                    [](const auto& entry) { return !entry.second; });
            if (selectionFilterActive && !selection_.Empty()) {
                text << L"  •  " << Tr(L"selection limited to visible results");
            }
            const auto faceCapability = faceAnalysisRetryPolicy_.Capability();
            if (faceCapability == quicksift::core::FaceDetectorCapability::Unavailable) {
                text << L"  •  " << Tr(L"Face Lock unavailable: Windows does not support face detection on this device.");
            } else if (faceCapability == quicksift::core::FaceDetectorCapability::TemporarilyFailed) {
                text << L"  •  " << Tr(L"Face Lock temporarily unavailable: QuickSift will retry Windows face detection.");
            } else if (faceAnalysisRetryPolicy_.Exhausted(ActiveInfoPath())) {
                text << L"  •  " << Tr(L"Face analysis paused for this photo after repeated temporary failures. Toggle Face Lock to retry now.");
            }
            if (reviewState_.Mode() == ViewMode::Single) text << L"  •  arrows: next/previous  •  wheel: zoom  •  1–5: rate  •  Esc: thumbnails";
            else if (reviewState_.Mode() == ViewMode::Compare) text << L"  •  synchronized wheel zoom and drag pan  •  click a pane, then 1–5 to rate";
            else text << L"  •  Enter: open selection  •  Ctrl+A: select all  •  1–5: rate selection";
        }
        std::wstring localizedStatus = Tr(text.str());
        // Red-border reason for the photo being reviewed (already localized pieces).
        if (!currentFolder_.empty() && reviewState_.Mode() != ViewMode::Thumbnails) {
            if (const PhotoItem* active = PhotoForPath(ActiveInfoPath())) {
                const std::wstring reason = FaceWarningReasonText(*active);
                if (!reason.empty()) localizedStatus += L"  •  " + reason;
            }
        }
        const std::wstring nextRightText = Tr(SingleStatusDetail());
        const std::wstring nextProgressText = FileOperationStatusLine();
        const bool progressVisibilityChanged = nextProgressText.empty() != statusProgressText_.empty();
        const bool statusChanged = localizedStatus != statusLeftText_ ||
            nextRightText != statusRightText_ || nextProgressText != statusProgressText_;
        statusLeftText_ = localizedStatus;
        statusRightText_ = nextRightText;
        statusProgressText_ = nextProgressText;
        if (progressVisibilityChanged && hwnd_) {
            RECT client{};
            if (GetClientRect(hwnd_, &client)) LayoutControls(client.right, client.bottom);
        }
        if (statusChanged) {
            SetWindowTextW(status_, statusLeftText_.c_str());
            InvalidateRect(status_, nullptr, FALSE);
        }
        const int nextRatingState = ActiveRatingForUi();
        if (nextRatingState != statusRatingState_) {
            statusRatingState_ = nextRatingState;
            UpdateRatingButton();
            for (int id : { ID_RATE_CLEAR, ID_RATING_MENU }) {
                if (controls_.contains(id)) InvalidateRect(controls_[id], nullptr, FALSE);
            }
        }
        const int nextPickState = ActivePickStateForUi();
        if (nextPickState != statusPickState_) {
            statusPickState_ = nextPickState;
            for (int id : { ID_PICK_STATE_MENU }) {
                if (controls_.contains(id)) InvalidateRect(controls_[id], nullptr, FALSE);
            }
        }
        UpdateHistoryButtons();
    }


    void QuickSiftApplicationImpl::InvalidateCanvas() {
        if (canvas_) InvalidateRect(canvas_, nullptr, FALSE);
    }

} // namespace quicksift::app
