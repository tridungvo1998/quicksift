// OWNER: Compiled QuickSiftApplication feature module.
#include "app/quicksift_application_internal.h"
#include "platform/application_data_paths.h"

namespace quicksift::app {

// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Navigation, zoom/pan/rotation, selection, modes, and session persistence.

// Navigation/session controller: face analysis scheduling, zoom/pan/rotation, input,
// selection, mode changes, timers, and persistent user session state.

    std::uint64_t QuickSiftApplicationImpl::FileTimeValue(const FILETIME& time) {
        ULARGE_INTEGER value{};
        value.LowPart = time.dwLowDateTime;
        value.HighPart = time.dwHighDateTime;
        return value.QuadPart;
    }

    bool QuickSiftApplicationImpl::CpuHasBackgroundHeadroom() {
        FILETIME idle{}, kernel{}, user{};
        if (!GetSystemTimes(&idle, &kernel, &user)) return false;
        const std::uint64_t idleValue = FileTimeValue(idle);
        const std::uint64_t kernelValue = FileTimeValue(kernel);
        const std::uint64_t userValue = FileTimeValue(user);
        if (!cpuSampleValid_) {
            previousCpuIdle_ = idleValue;
            previousCpuKernel_ = kernelValue;
            previousCpuUser_ = userValue;
            cpuSampleValid_ = true;
            return false;
        }
        if (idleValue < previousCpuIdle_ || kernelValue < previousCpuKernel_ ||
            userValue < previousCpuUser_) {
            previousCpuIdle_ = idleValue;
            previousCpuKernel_ = kernelValue;
            previousCpuUser_ = userValue;
            return false;
        }
        const std::uint64_t idleDelta = idleValue - previousCpuIdle_;
        const std::uint64_t kernelDelta = kernelValue - previousCpuKernel_;
        const std::uint64_t userDelta = userValue - previousCpuUser_;
        previousCpuIdle_ = idleValue;
        previousCpuKernel_ = kernelValue;
        previousCpuUser_ = userValue;
        if (kernelDelta > std::numeric_limits<std::uint64_t>::max() - userDelta) return false;
        const std::uint64_t totalDelta = kernelDelta + userDelta;
        if (totalDelta == 0 || idleDelta > totalDelta) return false;
        const double idleShare = static_cast<double>(idleDelta) / static_cast<double>(totalDelta);
        const double threshold = systemProfile_.logicalProcessors >= 12 ? 0.45 : 0.55;
        return idleShare >= threshold;
    }

    bool QuickSiftApplicationImpl::PathHasResidentPixels(const std::wstring& path) const {
        for (const auto& [key, entry] : bitmapCache_) {
            (void)entry;
            if (key.path == path) return true;
        }
        if (wicImageSources_.contains(path)) return true;
        for (const auto& [key, entry] : tileCache_) {
            (void)entry;
            if (key.path == path) return true;
        }
        return false;
    }

    void QuickSiftApplicationImpl::ScheduleBackgroundFaceAnalysis() {
        if (!performancePolicy_.allowAnalysis) return;
        RefreshFaceDetectorCapability(false);
        if (faceAnalysisRetryPolicy_.Capability() !=
            quicksift::core::FaceDetectorCapability::Available) return;
        const auto now = std::chrono::steady_clock::now();
        if (!scanComplete_ || catalog_.Empty() || IsIconic(hwnd_) || IsActivelyInteracting() ||
            now < backgroundFacePauseUntil_) return;
        if (now - lastInteraction_ < std::chrono::seconds(2)) return;
        if (worker_.AnalysisBacklog() >= 1 || !CpuHasBackgroundHeadroom()) return;

        const size_t count = catalog_.PhotoCount();
        const size_t attemptsThisTick = std::min<size_t>(count, 256);
        for (size_t attempt = 0; attempt < attemptsThisTick; ++attempt) {
            const size_t index = backgroundFaceCursor_++ % count;
            ++backgroundFaceAttemptsSinceWork_;
            const PhotoItem& photo = catalog_.PhotoAt(index);
            const bool needsAnalysis = !photo.facesScanned ||
                (!photo.faces.empty() && !photo.faceBlurScanned);
            if (needsAnalysis && !PathHasResidentPixels(photo.path.wstring())) {
                if (EnqueueFaceAnalysis(photo.path, 0, JobPriority::Analysis,
                    photo.facesScanned ? kFaceSharpnessDecodeSize : kFaceDecodeSize)) {
                    backgroundFaceAttemptsSinceWork_ = 0;
                    return;
                }
            }
            if (backgroundFaceAttemptsSinceWork_ >= count) {
                // A completed no-work sweep sleeps before checking again. This
                // avoids repeatedly walking a 100k-image catalog every timer tick,
                // while still retrying images whose transient decode later clears.
                backgroundFaceAttemptsSinceWork_ = 0;
                backgroundFacePauseUntil_ = now + std::chrono::seconds(30);
                return;
            }
        }
    }

    const PhotoItem* QuickSiftApplicationImpl::PhotoForPath(const std::wstring& path) const {
        if (const auto index = IndexForPath(path)) return &VisiblePhoto(*index);
        return nullptr;
    }

    void QuickSiftApplicationImpl::ApplyFaceLockForActive(bool requestIfMissing ) {
        if (!reviewState_.FaceLockEnabled()) return;
        const std::wstring path = EditableViewPath();
        if (path.empty()) return;
        const PhotoItem* photo = PhotoForPath(path);
        if (!photo) return;
        if (!photo->facesScanned) {
            if (requestIfMissing) {
                EnqueueFaceAnalysis(photo->path, reviewState_.Epochs().navigation, FaceAnalysisPriority());
            }
            return;
        }
        // Activating an existing pane must not erase its saved face-relative pan.
        // Detection completion merely makes the stored offset renderable.
        if (!photo->faces.empty() && IsBeyondFitZoom(path,
            ViewAreaForPath(path), BaseViewForPath(path))) InvalidateCanvas();
    }

    std::pair<int, int> QuickSiftApplicationImpl::ActiveSourceSize() const {
        const PhotoItem* photo = PhotoForPath(EditableViewPath());
        if (photo && photo->sourceWidth > 0 && photo->sourceHeight > 0) return { photo->sourceWidth, photo->sourceHeight };
        return { 0, 0 };
    }

    void QuickSiftApplicationImpl::SetZoomMode(ZoomMode mode) {
        const std::wstring path = EditableViewPath();
        ViewState state = EditableViewState();
        if (mode == ZoomMode::Custom) {
            state.zoom = quicksift::review::transform::CustomPhysicalScaleFor(state,
                ViewGeometryForPath(path, ViewAreaForPath(path)));
        }
        state.mode = mode;
        if (reviewState_.FaceLockEnabled()) {
            ResetFaceOffsetForPath(path);
            if (const auto face = PrimaryFaceCenter(path)) {
                state.centerX = face->x;
                state.centerY = face->y;
            }
        } else {
            state.centerX = 0.5f;
            state.centerY = 0.5f;
        }
        StoreEditableViewState(state);
        static_cast<void>(AdvanceReviewNavigationEpoch());
        UpdateZoomModeButton();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::RotateView(int degrees) {
        ViewState state = EditableViewState();
        state.rotation = ((state.rotation + degrees) % 360 + 360) % 360;
        StoreEditableViewState(state);
        InvalidateCanvas();
    }

    int QuickSiftApplicationImpl::ActiveColorLabelForUi() const {
        const auto presentation = [](const MetadataFieldValue& field) {
            switch (field.knowledge) {
            case MetadataKnowledge::Known: return field.value;
            case MetadataKnowledge::Unknown: return -2;
            case MetadataKnowledge::Unsupported: return -3;
            case MetadataKnowledge::Unreadable: return -4;
            }
            return -2;
        };
        if (reviewState_.Mode() == ViewMode::Thumbnails && selection_.Size() > 1) {
            std::optional<MetadataFieldValue> common;
            for (size_t index = 0; index < catalog_.VisibleCount(); ++index) {
                const PhotoItem& photo = VisiblePhoto(index);
                if (!selection_.Contains(photo.path.wstring())) continue;
                const MetadataFieldValue field{ photo.colorLabelKnowledge, photo.colorLabel, photo.rawColorLabel };
                if (!common) common = field;
                else if (!MetadataFieldEquivalent(*common, field)) return -1;
            }
            return common ? presentation(*common) : -2;
        }
        const PhotoItem* photo = PhotoForPath(EditableViewPath());
        return photo ? presentation({ photo->colorLabelKnowledge, photo->colorLabel,
            photo->rawColorLabel }) : -2;
    }

    void QuickSiftApplicationImpl::UpdateColorLabelButton() {
        if (!controls_.contains(ID_COLOR_LABEL)) return;
        const int label = ActiveColorLabelForUi();
        std::wstring value;
        if (label == -1) value = Tr(L"Mixed");
        else if (label == -2) value = Tr(L"Reading…");
        else if (label == -3) value = Tr(L"Custom");
        else if (label == -4) value = Tr(L"Unreadable");
        else value = Tr(LabelDisplayName(label));
        const std::wstring text = Tr(L"Label") + L": " + value;
        SetWindowTextW(controls_[ID_COLOR_LABEL], text.c_str());
    }

    void QuickSiftApplicationImpl::UpdateRatingButton() {
        if (!controls_.contains(ID_RATING_MENU)) return;

        const int rating = ActiveRatingForUi();
        std::wstring value;
        if (rating == -2) value = Tr(L"Mixed");
        else if (rating == -1) value = Tr(L"Reading…");
        else if (rating == 0) value = Tr(L"Unrated");
        else if (rating >= 1 && rating <= 5)
            value = std::to_wstring(rating) + L" ★";
        else
            value = Tr(L"Custom");

        const std::wstring text = Tr(L"Rating") + L": " + value;
        SetWindowTextW(controls_[ID_RATING_MENU], text.c_str());
    }

    void QuickSiftApplicationImpl::UpdateRatingFilterButton() {
        if (!controls_.contains(ID_STAR_FILTER)) return;
        static const wchar_t* labels[] = {
            L"Stars: All", L"Stars: None", L"Stars: 1", L"Stars: 2", L"Stars: 3",
            L"Stars: 4", L"Stars: 5", L"Stars: 4+"
        };
        const std::wstring label = Tr(labels[std::clamp(static_cast<int>(ratingFilter_), 0, 7)]);
        SetWindowTextW(controls_[ID_STAR_FILTER], label.c_str());
    }

    void QuickSiftApplicationImpl::UpdatePickFilterButton() {
        if (!controls_.contains(ID_PICK_FILTER)) return;
        static const wchar_t* labels[] = {
            L"Pick state: All", L"Pick state: Pick", L"Pick state: Reject", L"Pick state: Unmarked"
        };
        const std::wstring label = Tr(labels[std::clamp(static_cast<int>(pickFilter_), 0, 3)]);
        SetWindowTextW(controls_[ID_PICK_FILTER], label.c_str());
    }

    void QuickSiftApplicationImpl::UpdateColorLabelFilterButton() {
        if (!controls_.contains(ID_LABEL_FILTER)) return;
        static const wchar_t* labels[] = {
            L"Labels: All", L"Labels: None", L"Labels: Red", L"Labels: Yellow",
            L"Labels: Green", L"Labels: Blue", L"Labels: Purple"
        };
        SetWindowTextW(controls_[ID_LABEL_FILTER],
            Tr(labels[std::clamp(static_cast<int>(colorLabelFilter_), 0, 6)]).c_str());
    }

    void QuickSiftApplicationImpl::UpdateZoomModeButton() {
        if (!controls_.contains(ID_ZOOM_MODE)) return;
        static const wchar_t* labels[] = { L"Zoom: Fit", L"Zoom: Width", L"Zoom: Height", L"Zoom: 100%", L"Zoom: Fill", L"Zoom: Custom" };
        const ZoomMode mode = EditableViewState().mode;
        const std::wstring label = Tr(labels[std::clamp(static_cast<int>(mode), 0, 5)]);
        SetWindowTextW(controls_[ID_ZOOM_MODE], label.c_str());
    }

    void QuickSiftApplicationImpl::UpdateFullscreenButton() {
        if (!controls_.contains(ID_FULLSCREEN)) return;
        SetWindowTextW(controls_[ID_FULLSCREEN], FullscreenButtonText().c_str());
        InvalidateRect(controls_[ID_FULLSCREEN], nullptr, FALSE);
    }

    void QuickSiftApplicationImpl::RelayoutTitleControlsAfterCaptionChange() {
        if (!hwnd_ || !canvas_) return;
        RECT client{};
        if (!GetClientRect(hwnd_, &client)) return;
        if (fullscreen_) {
            LayoutTitleControls(client.right - client.left);
            RaiseFullscreenTitleOverlay();
        } else {
            LayoutControls(client.right - client.left, client.bottom - client.top);
        }
        InvalidateTitleBar();
    }

    void QuickSiftApplicationImpl::UpdateToggleButtons() {
        if (controls_.contains(ID_SYNC_VIEW)) {
            const std::wstring text = Tr(reviewState_.SyncCompareView() ? L"Sync: On" : L"Sync: Off");
            SetWindowTextW(controls_[ID_SYNC_VIEW], text.c_str());
        }
        RefreshFaceLockAvailabilityUi();
        if (controls_.contains(ID_THEME_TOGGLE)) SetWindowTextW(controls_[ID_THEME_TOGGLE],
            ThemeToggleText().c_str());
        UpdateColorLabelButton();
        UpdateRatingFilterButton();
        UpdatePickFilterButton();
        if (controls_.contains(ID_PICK_STATE_MENU)) {
            const int pick = ActivePickStateForUi();
            const wchar_t* label = pick == 1 ? L"Pick state: Pick" : pick == -1 ? L"Pick state: Reject" :
                pick == 0 ? L"Pick state: Unmarked" : L"Pick state: Mixed";
            SetWindowTextW(controls_[ID_PICK_STATE_MENU], Tr(label).c_str());
        }
        UpdateColorLabelFilterButton();
        UpdateZoomModeButton();
        UpdateFullscreenButton();
        RelayoutTitleControlsAfterCaptionChange();
    }

    void QuickSiftApplicationImpl::FlushMetadataUi() {
        if (!metadataUiDirty_) return;
        metadataUiDirty_ = false;
        const bool metadataAffectsMembershipOrOrder =
            sortMode_ == SortMode::RatingHigh || ratingFilter_ != RatingFilter::All ||
            pickFilter_ != PickFilter::All || colorLabelFilter_ != ColorLabelFilter::All;
        if (metadataAffectsMembershipOrOrder) RebuildVisiblePhotos();
        else UpdateStatus();
        UpdateColorLabelButton();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::OnTimer(UINT_PTR timer) {
        if (!ThumbnailScrollPrefersPlaceholders()) {
            UpdateAnimations();
            if (completionQueue_.Size() != 0) DrainBackgroundCompletions();
            DrainFileTransactionResults();
            DrainMetadataTransactionResults();
        } else {
            // Keep animation clock advancing lightly; skip heavy completion drain.
            UpdateAnimations();
        }
        if (timer == ID_TIMER_DRAG_RENDER || timer == ID_TIMER_SCROLL_FRAME) {
            // Shared virtualized scroll frame pump (scrollbar drag + wheel/keyboard).
            ProcessThumbnailScrollFrame();
            if (!thumbnailScrollDragging_ && !thumbnailScrollFramePending_ &&
                CurrentThumbnailScrollLoadBudget().phase ==
                    quicksift::review::prefetch::ScrollMotionPhase::Settled) {
                if (timer == ID_TIMER_DRAG_RENDER) {
                    KillTimer(hwnd_, ID_TIMER_DRAG_RENDER);
                    thumbnailDragRenderTimerActive_ = false;
                }
                if (timer == ID_TIMER_SCROLL_FRAME) {
                    KillTimer(hwnd_, ID_TIMER_SCROLL_FRAME);
                    thumbnailScrollFrameTimerActive_ = false;
                }
            }
            return;
        }
        if (timer == ID_TIMER_SCROLL_SETTLE) {
            KillTimer(hwnd_, ID_TIMER_SCROLL_SETTLE);
            thumbnailScrollSettlePending_ = false;
            if (thumbnailScrollDragging_ || IsActivelyInteracting()) {
                // Motion resumed during debounce — do not dump Interactive work.
                return;
            }
            if (!thumbnailScrollDragging_) ClearThumbnailScrollMotionAdmission();
            PublishThumbnailViewportForCurrentPlan(true);
            SchedulePredictivePrefetch(0);
            InvalidateCanvas();
            return;
        }
        if (timer == ID_TIMER_THROTTLED_SCHEDULE) {
            KillTimer(hwnd_, ID_TIMER_THROTTLED_SCHEDULE);
            throttledSchedulePending_ = false;
            ScheduleVisibleWork();
            return;
        }
        if (timer == ID_TIMER_LOG_PERFORMANCE) {
            LogPerformanceSummary();
            return;
        }
        if (timer == ID_TIMER_PERFORMANCE_HUD) {
            if (performanceHudVisible_) InvalidateCanvas();
            else KillTimer(hwnd_, ID_TIMER_PERFORMANCE_HUD);
            return;
        }
        if (timer == ID_TIMER_FILE_PROGRESS) {
            if (!transactions_.FileBusy()) {
                KillTimer(hwnd_, ID_TIMER_FILE_PROGRESS);
            }
            UpdateStatus();
            return;
        }
        if (timer == ID_TIMER_FOLDER_SELECTION) {
            KillTimer(hwnd_, ID_TIMER_FOLDER_SELECTION);
            const fs::path folder = pendingTreeFolder_;
            pendingTreeFolder_.clear();
            if (!folder.empty() && folder != currentFolder_) LoadFolder(folder);
            return;
        }
        if (timer == ID_TIMER_BACKGROUND_ANALYSIS) {
            ScheduleBackgroundFaceAnalysis();
            return;
        }
        if (timer == ID_TIMER_VIEW_RESOURCE_TRIM) {
            KillTimer(hwnd_, ID_TIMER_VIEW_RESOURCE_TRIM);
            TrimInactiveViewResources();
            return;
        }
        if (timer == ID_TIMER_METADATA_UI) {
            KillTimer(hwnd_, ID_TIMER_METADATA_UI);
            FlushMetadataUi();
            return;
        }
        if (timer == ID_TIMER_AUTOHIDE) {
            UpdateFullscreenTitleFromCursor();
            UpdateAutoHideFromCursor();
            return;
        }
        if (timer == ID_TIMER_CACHE_FLUSH) {
            RefreshPerformancePolicy();
            worker_.RequestCacheFlush();
            TrimCachesForMemoryPressure();
            return;
        }
        if (timer == ID_TIMER_IDLE_QUALITY) {
            KillTimer(hwnd_, ID_TIMER_IDLE_QUALITY);
            if (!IsActivelyInteracting() && !thumbnailScrollDragging_) {
                // Prefer the dedicated settle debounce so successive flings cannot
                // race an immediate Interactive dump from the generic idle timer.
                ArmThumbnailScrollSettle();
            }
            return;
        }
        if (timer == ID_TIMER_THUMBNAIL_RETRY_COOLDOWN) {
            KillTimer(hwnd_, ID_TIMER_THUMBNAIL_RETRY_COOLDOWN);
            if (!IsActivelyInteracting()) {
                ScheduleVisibleWork();
                InvalidateCanvas();
            }
            // One timer services all cooldown entries. Re-arm it for the next earliest
            // expiration so a later-failing thumbnail cannot remain stuck indefinitely.
            const auto now = std::chrono::steady_clock::now();
            auto nextCooldown = std::chrono::steady_clock::time_point::max();
            for (const auto& [path, expiresAt] : thumbnailRetryCooldown_) {
                (void)path;
                if (expiresAt < nextCooldown) nextCooldown = expiresAt;
            }
            if (nextCooldown != std::chrono::steady_clock::time_point::max()) {
                const auto remaining = std::max(
                    std::chrono::milliseconds(1),
                    std::chrono::duration_cast<std::chrono::milliseconds>(nextCooldown - now));
                const auto bounded = std::min<std::int64_t>(
                    remaining.count(), static_cast<std::int64_t>(std::numeric_limits<UINT>::max()));
                StartUiTimer(ID_TIMER_THUMBNAIL_RETRY_COOLDOWN,
                    static_cast<UINT>(std::max<std::int64_t>(1, bounded)),
                    L"thumbnail retry cooldown");
            }
            return;
        }
        if (timer == ID_TIMER_TOAST) {
            const bool visible = uiFramework_.Toasts().Tick();
            if (!visible) KillTimer(hwnd_, ID_TIMER_TOAST);
            InvalidateCanvas();
            return;
        }
    }

    void QuickSiftApplicationImpl::UpdatePerformanceHudTimer() {
        if (performanceHudVisible_) StartUiTimer(ID_TIMER_PERFORMANCE_HUD, 250, L"performance HUD");
        else KillTimer(hwnd_, ID_TIMER_PERFORMANCE_HUD);
    }

    void QuickSiftApplicationImpl::LoadSession() {
        const fs::path sessionDirectory = quicksift::platform::DataRootPath();
        sessionPath_ = quicksift::platform::SessionFilePath();

        auto readInt = [&](const wchar_t* key, int fallback) {
            return static_cast<int>(GetPrivateProfileIntW(kAppTitle, key, fallback, sessionPath_.c_str()));
        };
        wchar_t buffer[32768]{};
        GetPrivateProfileStringW(kAppTitle, L"Folder", L"", buffer, static_cast<DWORD>(std::size(buffer)), sessionPath_.c_str());
        restoredFolder_ = buffer;
        GetPrivateProfileStringW(kAppTitle, L"Current", L"", buffer, static_cast<DWORD>(std::size(buffer)), sessionPath_.c_str());
        restoredCurrentPath_ = buffer;
        restoredMode_ = static_cast<ViewMode>(std::clamp(readInt(L"Mode", 0), 0, 2));
        performanceHudVisible_ = readInt(L"PerformanceHud", 0) != 0;
        sortMode_ = static_cast<SortMode>(std::clamp(readInt(L"Sort", 0), 0, 5));
        dateFilter_ = static_cast<DateFilter>(std::clamp(readInt(L"DateFilter", 0), 0, 4));
        int savedRatingFilter = readInt(L"RatingFilter", 0);
        // QuickSift 0.9 previously stored “5 stars and up” as 8. Ratings are
        // capped at five, so migrate that legacy value to Exact5 rather than
        // silently changing it to the new four-stars-and-up entry.
        if (savedRatingFilter == 8) savedRatingFilter = 6;
        ratingFilter_ = static_cast<RatingFilter>(std::clamp(savedRatingFilter, 0, 7));
        pickFilter_ = static_cast<PickFilter>(std::clamp(readInt(L"PickFilter", 0), 0, 3));
        colorLabelFilter_ = static_cast<ColorLabelFilter>(std::clamp(readInt(L"ColorLabelFilter", 0), 0, 6));
        const int restoredThumbnailSizeIndex = std::clamp(readInt(L"ThumbSize", 2), 0, 4);
        static constexpr int thumbValues[] = { 72, 112, 180, 280, 420 };
        reviewState_.RestoreThumbnailSize(restoredThumbnailSizeIndex,
            thumbValues[restoredThumbnailSizeIndex]);
        reviewState_.RestorePreferences(readInt(L"Sync", 1) != 0,
            readInt(L"FaceLock", 0) != 0);
        badgeStars_ = readInt(L"BadgeStars", 1) != 0;
        badgeColor_ = readInt(L"BadgeColor", 1) != 0;
        badgePick_ = readInt(L"BadgePick", 1) != 0;
        badgePair_ = readInt(L"BadgePair", 0) != 0;
        copyMoveRawWithJpg_ = readInt(L"CopyMoveRawWithJpg", 0) != 0;
        loadOnlyRawJpegPreviews_ = readInt(L"LoadOnlyRawJpegPreviews", 0) != 0;
        theme_ = readInt(L"Theme", 0) == 1 ? AppTheme::Light : AppTheme::Dark;
        localizer_.SetLanguage(readInt(L"Language", 0) == 1 ?
            quicksift::Language::Vietnamese : quicksift::Language::English);
        metadataDirectMask_ = static_cast<std::uint32_t>(readInt(L"MetadataDirectMask",
            static_cast<int>(kDefaultMetadataDirectMask))) &
            (MetadataDirectJpeg | MetadataDirectPng | MetadataDirectTiff | MetadataDirectRaw);
        gMetadataDirectMask.store(metadataDirectMask_, std::memory_order_relaxed);
        safeJpegMetadataWrites_ = readInt(L"SafeJpegMetadataWrites", 1) != 0;
        const int formatMask = readInt(L"FormatMask", 0x7F);
        formats_[FormatGroup::Jpeg] = (formatMask & (1 << 0)) != 0;
        formats_[FormatGroup::Png] = (formatMask & (1 << 1)) != 0;
        formats_[FormatGroup::Heif] = (formatMask & (1 << 2)) != 0;
        formats_[FormatGroup::Tiff] = (formatMask & (1 << 3)) != 0;
        formats_[FormatGroup::Basic] = (formatMask & (1 << 4)) != 0;
        formats_[FormatGroup::WebP] = (formatMask & (1 << 5)) != 0;
        formats_[FormatGroup::Raw] = (formatMask & (1 << 6)) != 0;
        savedPlacement_.rcNormalPosition.left = readInt(L"WindowLeft", 0);
        savedPlacement_.rcNormalPosition.top = readInt(L"WindowTop", 0);
        savedPlacement_.rcNormalPosition.right = readInt(L"WindowRight", 0);
        savedPlacement_.rcNormalPosition.bottom = readInt(L"WindowBottom", 0);
        savedPlacement_.showCmd = readInt(L"WindowShow", SW_SHOWNORMAL);
        const std::int64_t savedWidth = static_cast<std::int64_t>(savedPlacement_.rcNormalPosition.right) -
            static_cast<std::int64_t>(savedPlacement_.rcNormalPosition.left);
        const std::int64_t savedHeight = static_cast<std::int64_t>(savedPlacement_.rcNormalPosition.bottom) -
            static_cast<std::int64_t>(savedPlacement_.rcNormalPosition.top);
        hasSavedPlacement_ = savedWidth > 320 && savedHeight > 240;
    }

    void QuickSiftApplicationImpl::SaveSession() {
        if (sessionPath_.empty()) {
            sessionPath_ = quicksift::platform::SessionFilePath();
        }

        std::wstring document = L"[";
        document += kAppTitle;
        document += L"]\r\n";
        auto append = [&](std::wstring_view key, std::wstring value) {
            std::replace(value.begin(), value.end(), L'\r', L' ');
            std::replace(value.begin(), value.end(), L'\n', L' ');
            document.append(key);
            document.push_back(L'=');
            document.append(value);
            document += L"\r\n";
        };
        auto appendInt = [&](std::wstring_view key, int value) {
            append(key, std::to_wstring(value));
        };

        append(L"Folder", currentFolder_.wstring());
        append(L"Current", EditableViewPath());
        appendInt(L"Mode", static_cast<int>(reviewState_.Mode()));
        appendInt(L"Sort", static_cast<int>(sortMode_));
        appendInt(L"DateFilter", static_cast<int>(dateFilter_));
        appendInt(L"RatingFilter", static_cast<int>(ratingFilter_));
        appendInt(L"PickFilter", static_cast<int>(pickFilter_));
        appendInt(L"ColorLabelFilter", static_cast<int>(colorLabelFilter_));
        appendInt(L"ThumbSize", reviewState_.Thumbnails().sizeIndex);
        appendInt(L"Sync", reviewState_.SyncCompareView() ? 1 : 0);
        appendInt(L"FaceLock", reviewState_.FaceLockEnabled() ? 1 : 0);
        appendInt(L"BadgeStars", badgeStars_ ? 1 : 0);
        appendInt(L"BadgeColor", badgeColor_ ? 1 : 0);
        appendInt(L"BadgePick", badgePick_ ? 1 : 0);
        appendInt(L"BadgePair", badgePair_ ? 1 : 0);
        appendInt(L"CopyMoveRawWithJpg", copyMoveRawWithJpg_ ? 1 : 0);
        appendInt(L"LoadOnlyRawJpegPreviews", loadOnlyRawJpegPreviews_ ? 1 : 0);
        appendInt(L"Theme", theme_ == AppTheme::Light ? 1 : 0);
        appendInt(L"Language", localizer_.CurrentLanguage() ==
            quicksift::Language::Vietnamese ? 1 : 0);
        appendInt(L"MetadataDirectMask", static_cast<int>(metadataDirectMask_));
        appendInt(L"SafeJpegMetadataWrites", safeJpegMetadataWrites_ ? 1 : 0);
        appendInt(L"PerformanceHud", performanceHudVisible_ ? 1 : 0);
        int formatMask = 0;
        if (formats_[FormatGroup::Jpeg]) formatMask |= 1 << 0;
        if (formats_[FormatGroup::Png]) formatMask |= 1 << 1;
        if (formats_[FormatGroup::Heif]) formatMask |= 1 << 2;
        if (formats_[FormatGroup::Tiff]) formatMask |= 1 << 3;
        if (formats_[FormatGroup::Basic]) formatMask |= 1 << 4;
        if (formats_[FormatGroup::WebP]) formatMask |= 1 << 5;
        if (formats_[FormatGroup::Raw]) formatMask |= 1 << 6;
        appendInt(L"FormatMask", formatMask);

        if (hwnd_) {
            WINDOWPLACEMENT placement{ sizeof(WINDOWPLACEMENT) };
            bool havePlacement = false;
            if (fullscreen_) {
                placement = preFullscreenPlacement_;
                havePlacement = true;
            } else {
                havePlacement = GetWindowPlacement(hwnd_, &placement) != FALSE;
            }
            if (havePlacement) {
                appendInt(L"WindowLeft", placement.rcNormalPosition.left);
                appendInt(L"WindowTop", placement.rcNormalPosition.top);
                appendInt(L"WindowRight", placement.rcNormalPosition.right);
                appendInt(L"WindowBottom", placement.rcNormalPosition.bottom);
                appendInt(L"WindowShow", placement.showCmd == SW_SHOWMAXIMIZED ?
                    SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
            }
        }

        AtomicFileSnapshot expected;
        if (!CaptureAtomicFileSnapshot(sessionPath_, expected) ||
            !WriteTextFile(sessionPath_, document,
                TextFileEncoding::Utf16LittleEndian, expected)) {
            QS_LOG_ERROR(L"Session", L"The session file could not be published atomically: " +
                sessionPath_.wstring());
        }
    }

    void QuickSiftApplicationImpl::RestoreSessionAfterCreate() {
        UpdateSortButton();
        UpdateDateFilterButton();
        UpdateThumbnailSizeButton();
        UpdateFormatFilterButton();
        UpdateToggleButtons();
        if (!restoredFolder_.empty()) {
            std::error_code ec;
            if (fs::is_directory(restoredFolder_, ec)) {
                rootFolder_ = restoredFolder_;
                if (currentFolder_.empty()) LoadFolder(rootFolder_);
                else RefreshLibraryList();
            }
        }
    }

    void QuickSiftApplicationImpl::OnCanvasLeftDown(float x, float y, WPARAM keys) {
        mouseDown_ = { x, y };
        lastMouse_ = mouseDown_;
        if (reviewState_.Mode() == ViewMode::Thumbnails) {
            const auto hit = HitThumbnail(x, y);
            if (!hit) {
                if (!(keys & MK_CONTROL)) selection_.Clear();
                InvalidateCanvas();
                return;
            }
            const std::wstring path = VisiblePhoto(*hit).path.wstring();
            if (keys & MK_SHIFT && selection_.Anchor()) {
                const size_t anchor = *selection_.Anchor();
                if (!(keys & MK_CONTROL)) selection_.Clear();
                const size_t lo = std::min(anchor, *hit);
                const size_t hi = std::max(anchor, *hit);
                for (size_t i = lo; i <= hi && i < catalog_.VisibleCount(); ++i)
                    selection_.Select(VisiblePhoto(i).path.wstring());
            } else if (keys & MK_CONTROL) {
                selection_.Toggle(path);
                selection_.SetAnchor(*hit);
            } else {
                selection_.Clear();
                selection_.Select(path);
                selection_.SetAnchor(*hit);
            }
            reviewState_.SetPrimaryPath(path);
            RequestExifForActive();
            UpdateStatus();
            InvalidateCanvas();
            return;
        }

        if (FilmstripContains(x, y)) {
            if (const auto clickedIndex = HitFilmstrip(x, y)) {
                const auto currentIndex = IndexForPath(reviewState_.SinglePath());
                if (!currentIndex || *clickedIndex != *currentIndex) {
                    const int navigationDelta = currentIndex && *clickedIndex > *currentIndex ? 1 : -1;
                    if (NavigateToSingleIndex(*clickedIndex, navigationDelta, true)) {
                        static_cast<void>(AdvanceReviewNavigationEpoch());
                        if (reviewState_.FaceLockEnabled()) RecenterFaceLockForPath(EditableViewPath());
                        HandleNavigationEvent(navigationDelta);
                        ScheduleVisibleWork();
                        RequestExifForActive();
                        UpdateColorLabelButton();
                        UpdateStatus();
                        const D2D1_SIZE_F canvas = CanvasSizeInDips();
                        PrefetchFilmstrip(CurrentFilmstripLayout(
                            D2D1::RectF(0.0f, 0.0f, canvas.width, canvas.height)));
                        InvalidateCanvas();
                    }
                }
            }
            // The whole filmstrip is an interaction surface; never let clicks
            // fall through to image panning underneath.
            return;
        }

        if (reviewState_.Mode() == ViewMode::Compare) {
            if (const auto slot = HitCompareSlot(x, y)) {
                const bool changed = reviewState_.SetActiveCompareSlot(*slot);
                PrioritizeCompareLoads(reviewState_.ComparePaths()[*slot]);
                if (changed) {
                    if (reviewState_.FaceLockEnabled()) ApplyFaceLockForActive();
                    SchedulePredictivePrefetch(0);
                    RequestExifForActive();
                    UpdateColorLabelButton();
                    UpdateStatus();
                }
            }
        }
        dragging_ = true;
        SetCapture(canvas_);
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::OnCanvasLeftUp(float, float) {
        if (dragging_) {
            dragging_ = false;
            ReleaseCapture();
        }
    }

    void QuickSiftApplicationImpl::OnCanvasDoubleClick(float x, float y) {
        if (reviewState_.Mode() == ViewMode::Thumbnails) {
            const auto hit = HitThumbnail(x, y);
            if (hit) {
                reviewState_.SetPrimaryPath(VisiblePhoto(*hit).path.wstring());
                selection_.Clear();
                selection_.Select(reviewState_.SinglePath());
                SetMode(ViewMode::Single);
                ResetView();
            }
        } else {
            const ViewState state = EditableViewState();
            SetZoomMode(state.mode == ZoomMode::ActualPixels ? ZoomMode::Fit : ZoomMode::ActualPixels);
        }
    }

    void QuickSiftApplicationImpl::OnCanvasMouseMove(float x, float y, WPARAM keys) {
        if (dragging_ && (keys & MK_LBUTTON) && reviewState_.Mode() != ViewMode::Thumbnails) {
            const std::wstring path = EditableViewPath();
            const D2D1_RECT_F area = ViewAreaForPath(path);
            const auto geometry = ViewGeometryForPath(path, area);
            const ViewState state = quicksift::review::transform::PanView(EditableViewState(), geometry,
                { x - lastMouse_.x, y - lastMouse_.y });
            StoreEditableViewState(state);
            const auto interactionEpoch = viewInteractionEpoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
            worker_.SetViewInteractionEpoch(interactionEpoch);
            worker_.DiscardQueuedTiles();
            lastMouse_ = { x, y };
            UpdateZoomModeButton();
            InvalidateCanvas();
        }
    }

    void QuickSiftApplicationImpl::OnCanvasWheel(float x, float y, int delta, UINT keys) {
        if (reviewState_.Mode() == ViewMode::Thumbnails) {
            const float previousScroll = reviewState_.Thumbnails().scrollDip;
            if (keys & MK_CONTROL) {
                const int nextIndex = std::clamp(reviewState_.Thumbnails().sizeIndex +
                    (delta > 0 ? 1 : -1), 0, 4);
                static constexpr int values[] = { 72, 112, 180, 280, 420 };
                PreserveThumbnailAnchorForSizeChange(nextIndex, values[nextIndex]);
                UpdateThumbnailSizeButton();
                // Size change: settle debounce fills the new cell size without a fling dump.
                ArmThumbnailScrollSettle();
            } else {
                const float requested = previousScroll -
                    static_cast<float>(delta) / WHEEL_DELTA * 110.0f;
                reviewState_.SetThumbnailScroll(requested);
                if (reviewState_.Thumbnails().scrollDip != previousScroll) {
                    thumbnailPrefetchPlanner_.ObserveScroll(reviewState_.Thumbnails().scrollDip);
                    ApplyThumbnailScrollLoadBudget(CurrentThumbnailScrollLoadBudget());
                    // Coalesce to one paint/publish per frame — never decode here.
                    RequestThumbnailScrollFrame();
                }
            }
        } else {
            if (reviewState_.Mode() == ViewMode::Compare) {
                if (const auto slot = HitCompareSlot(x, y)) {
                    const bool changed = reviewState_.SetActiveCompareSlot(*slot);
                    PrioritizeCompareLoads(reviewState_.ComparePaths()[*slot]);
                    if (changed) {
                        ApplyFaceLockForActive();
                        RequestExifForActive();
                        UpdateColorLabelButton();
                        UpdateStatus();
                    }
                }
            }
            const float factor = delta > 0 ? 1.2f : (1.0f / 1.2f);
            const std::wstring path = EditableViewPath();
            const D2D1_RECT_F area = ViewAreaForPath(path);
            quicksift::review::transform::ViewPoint zoomPoint{ x, y };
            if (reviewState_.FaceLockEnabled() &&
                (reviewState_.Mode() == ViewMode::Single || reviewState_.Mode() == ViewMode::Compare)) {
                if (const auto faceCenter = PrimaryFaceCenter(path)) {
                    zoomPoint.x = faceCenter->x * (area.right - area.left) + area.left;
                    zoomPoint.y = faceCenter->y * (area.bottom - area.top) + area.top;
                }
            }
            ViewState state = quicksift::review::transform::ZoomViewAt(EditableViewState(),
                ViewGeometryForPath(path, area), zoomPoint, factor);
            StoreEditableViewState(state);
            const auto interactionEpoch = viewInteractionEpoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
            worker_.SetViewInteractionEpoch(interactionEpoch);
            // Zoom uses a dedicated interaction epoch so stale high-resolution work is cancelled. A
            // lower-resolution in-flight decode remains useful as the progressive
            // intermediate frame while the higher-resolution request is queued.
            if (reviewState_.FaceLockEnabled() && !PrimaryFaceCenter(path)) ApplyFaceLockForActive();
            UpdateZoomModeButton();
        }
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::OnKeyDown(UINT key, bool ctrl, bool shift) {
        if (ctrl && key == L'Z') { Undo(); return; }
        if (ctrl && key == L'Y') { Redo(); return; }
        if (ctrl && key == L'5') { ApplyColorLabel(5); return; }
        if (ctrl && key == L'0') { ApplyColorLabel(0); return; }
        if (key >= L'1' && key <= L'5') {
            ApplyRating(static_cast<int>(key - L'0'));
            return;
        }
        if (key == L'0') { ApplyRating(0); return; }
        if (key == L'P') { ApplyPickState(1); return; }
        if (key == L'X') { ApplyPickState(-1); return; }
        if (key == L'U') { ApplyPickState(0); return; }
        if (key >= L'6' && key <= L'9') { ApplyColorLabel(static_cast<int>(key - L'5')); return; }
        if (key == L'F') { SetZoomMode(ZoomMode::Fit); return; }
        if (key == L'Z') { SetZoomMode(ZoomMode::ActualPixels); return; }
        if (key == L'R') { RotateView(90); return; }
        if (key == VK_F7) { ToggleFilmstrip(); return; }
        switch (key) {
        case VK_RETURN:
            if (reviewState_.Mode() == ViewMode::Thumbnails) {
                if (selection_.Size() >= 2) EnterCompareFromSelection();
                else EnterSingleFromSelection();
            }
            break;
        case VK_ESCAPE:
            SetMode(ViewMode::Thumbnails);
            break;
        case VK_LEFT:
            if (reviewState_.Mode() == ViewMode::Compare) NavigateComparePane(key);
            else Navigate(-1, shift);
            break;
        case VK_UP:
            if (reviewState_.Mode() == ViewMode::Compare) NavigateComparePane(key);
            else if (reviewState_.Mode() == ViewMode::Thumbnails) {
                Navigate(-std::max(1, CurrentThumbnailLayout().columns), shift);
            } else Navigate(-1, shift);
            break;
        case VK_RIGHT:
            if (reviewState_.Mode() == ViewMode::Compare) NavigateComparePane(key);
            else Navigate(1, shift);
            break;
        case VK_DOWN:
            if (reviewState_.Mode() == ViewMode::Compare) NavigateComparePane(key);
            else if (reviewState_.Mode() == ViewMode::Thumbnails) {
                Navigate(std::max(1, CurrentThumbnailLayout().columns), shift);
            } else Navigate(1, shift);
            break;
        case VK_SPACE:
            if (reviewState_.Mode() == ViewMode::Thumbnails) EnterSingleFromSelection();
            else SetMode(ViewMode::Thumbnails);
            break;
        case VK_DELETE:
            RequestDeleteOperation();
            break;
        case L'A':
            if (ctrl && reviewState_.Mode() == ViewMode::Thumbnails) {
                selection_.Clear();
                for (size_t visibleIndex = 0; visibleIndex < catalog_.VisibleCount(); ++visibleIndex) {
                    selection_.Select(VisiblePhoto(visibleIndex).path.wstring());
                }
                UpdateStatus();
                InvalidateCanvas();
            }
            break;
        case VK_HOME:
            if (!catalog_.VisibleEmpty()) {
                SelectIndex(0, shift);
                EnsureFocusedThumbnailVisible(0);
                UpdateStatus();
                InvalidateCanvas();
            }
            break;
        case VK_END:
            if (!catalog_.VisibleEmpty()) {
                SelectIndex(catalog_.VisibleCount() - 1, shift);
                EnsureFocusedThumbnailVisible(catalog_.VisibleCount() - 1);
                UpdateStatus();
                InvalidateCanvas();
            }
            break;
        default:
            break;
        }
    }

    void QuickSiftApplicationImpl::NavigateComparePane(UINT key) {
        const auto& paths = reviewState_.ComparePaths();
        if (paths.empty()) return;
        const auto activeSlot = reviewState_.ActiveCompareSlot();
        if (!activeSlot || *activeSlot >= paths.size()) return;

        const D2D1_SIZE_F canvas = CanvasSizeInDips();
        const auto cells = BuildBestCompareLayout(
            D2D1::RectF(0.0f, 0.0f, canvas.width, canvas.height));
        std::optional<std::size_t> nextSlot;
        if (cells.size() == paths.size()) {
            const D2D1_RECT_F& origin = cells[*activeSlot];
            const float originX = (origin.left + origin.right) * 0.5f;
            const float originY = (origin.top + origin.bottom) * 0.5f;
            float bestScore = std::numeric_limits<float>::max();
            for (size_t slot = 0; slot < cells.size(); ++slot) {
                if (slot == *activeSlot) continue;
                const D2D1_RECT_F& candidate = cells[slot];
                const float x = (candidate.left + candidate.right) * 0.5f;
                const float y = (candidate.top + candidate.bottom) * 0.5f;
                const float dx = x - originX;
                const float dy = y - originY;
                float forward = 0.0f;
                float sideways = 0.0f;
                switch (key) {
                case VK_LEFT:  forward = -dx; sideways = std::abs(dy); break;
                case VK_RIGHT: forward = dx;  sideways = std::abs(dy); break;
                case VK_UP:    forward = -dy; sideways = std::abs(dx); break;
                case VK_DOWN:  forward = dy;  sideways = std::abs(dx); break;
                default: return;
                }
                if (forward <= 1.0f) continue;
                const float score = forward + sideways * 1.75f;
                if (score < bestScore) {
                    bestScore = score;
                    nextSlot = slot;
                }
            }
        }

        if (!nextSlot) {
            const int direction = (key == VK_LEFT || key == VK_UP) ? -1 : 1;
            const int count = static_cast<int>(paths.size());
            nextSlot = static_cast<std::size_t>(
                (static_cast<int>(*activeSlot) + direction + count) % count);
        }
        if (!reviewState_.SetActiveCompareSlot(*nextSlot)) return;
        PrioritizeCompareLoads(reviewState_.ComparePaths()[*nextSlot]);
        if (reviewState_.FaceLockEnabled()) ApplyFaceLockForActive();
        RequestExifForActive();
        UpdateColorLabelButton();
        UpdateStatus();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::Navigate(int direction, bool extendSelection) {
        if (catalog_.VisibleEmpty()) return;
        const std::wstring base = reviewState_.ActivePath();
        size_t index = 0;
        if (const auto current = IndexForPath(base)) index = *current;
        int next = static_cast<int>(index) + direction;
        next = std::clamp(next, 0, static_cast<int>(catalog_.VisibleCount()) - 1);
        bool pathChanged = false;

        if (reviewState_.Mode() == ViewMode::Single) {
            pathChanged = NavigateToSingleIndex(static_cast<size_t>(next), direction, false);
        } else if (reviewState_.Mode() == ViewMode::Compare) {
            const auto activeSlot = reviewState_.ActiveCompareSlot();
            if (activeSlot && *activeSlot < reviewState_.ComparePaths().size()) {
                const std::wstring previousPath = reviewState_.ComparePaths()[*activeSlot];
                int candidate = next;
                while (candidate >= 0 && candidate < static_cast<int>(catalog_.VisibleCount())) {
                    const std::wstring candidatePath =
                        VisiblePhoto(static_cast<size_t>(candidate)).path.wstring();
                    if (!reviewState_.IsCompared(candidatePath) || candidatePath == previousPath) {
                        pathChanged = candidatePath != previousPath;
                        if (pathChanged) reviewState_.ReplaceComparePath(previousPath, candidatePath);
                        break;
                    }
                    candidate += direction;
                }
            }
        } else {
            SelectIndex(static_cast<size_t>(next), extendSelection);
            EnsureFocusedThumbnailVisible(static_cast<size_t>(next));
        }

        if (reviewState_.Mode() != ViewMode::Single) {
            static_cast<void>(AdvanceReviewNavigationEpoch());
            if (reviewState_.FaceLockEnabled()) {
                if (pathChanged) RecenterFaceLockForPath(EditableViewPath());
                else ApplyFaceLockForActive();
            }
            HandleNavigationEvent(direction);
        } else if (pathChanged) {
            static_cast<void>(AdvanceReviewNavigationEpoch());
            if (reviewState_.FaceLockEnabled()) RecenterFaceLockForPath(EditableViewPath());
            HandleNavigationEvent(direction);
        }
        SchedulePredictivePrefetch(direction);
        if (reviewState_.Mode() == ViewMode::Single && filmstripVisible_) {
            const D2D1_SIZE_F canvas = CanvasSizeInDips();
            PrefetchFilmstrip(CurrentFilmstripLayout(
                D2D1::RectF(0.0f, 0.0f, canvas.width, canvas.height)));
        }
        ScheduleViewResourceTrim();
        RequestExifForActive();
        UpdateColorLabelButton();
        UpdateStatus();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::SelectIndex(size_t index, bool extend) {
        if (index >= catalog_.VisibleCount()) return;
        if (extend && selection_.Anchor()) {
            const size_t anchor = *selection_.Anchor();
            selection_.Clear();
            const size_t lo = std::min(anchor, index);
            const size_t hi = std::max(anchor, index);
            for (size_t i = lo; i <= hi; ++i) selection_.Select(VisiblePhoto(i).path.wstring());
        } else {
            selection_.Clear();
            selection_.Select(VisiblePhoto(index).path.wstring());
            selection_.SetAnchor(index);
        }
        reviewState_.SetPrimaryPath(VisiblePhoto(index).path.wstring());
        RequestExifForActive();
    }

    void QuickSiftApplicationImpl::EnsureFocusedThumbnailVisible(size_t index) {
        const float previousScroll = reviewState_.Thumbnails().scrollDip;
        const auto layout = CurrentThumbnailLayout();
        reviewState_.SetThumbnailMaximumScroll(layout.maximumScroll);
        reviewState_.SetThumbnailScroll(quicksift::review::thumbnail::ScrollToRevealThumbnail(
            layout, index, reviewState_.Thumbnails().scrollDip));
        if (reviewState_.Thumbnails().scrollDip != previousScroll) {
            thumbnailPrefetchPlanner_.ObserveScroll(reviewState_.Thumbnails().scrollDip);
            ApplyThumbnailScrollLoadBudget(CurrentThumbnailScrollLoadBudget());
            RequestThumbnailScrollFrame();
        }
    }

    void QuickSiftApplicationImpl::PreserveThumbnailAnchorForSizeChange(int newSizeIndex, int newSize) {
        const auto& thumbnails = reviewState_.Thumbnails();
        if (newSizeIndex == thumbnails.sizeIndex && newSize == thumbnails.sizeDip) return;
        std::optional<size_t> anchor;
        if (!reviewState_.ThumbnailFocusPath().empty()) anchor = IndexForPath(reviewState_.ThumbnailFocusPath());
        if (!anchor && !catalog_.VisibleEmpty()) {
            const auto oldLayout = CurrentThumbnailLayout();
            const float probeX = oldLayout.leftInset + oldLayout.cellWidth * 0.5f;
            const float probeY = std::max(0.0f, oldLayout.canvasHeight * 0.5f);
            anchor = quicksift::review::thumbnail::HitTestThumbnail(oldLayout,
                catalog_.VisibleCount(), thumbnails.scrollDip, probeX, probeY);
            if (!anchor) {
                const std::size_t row = static_cast<std::size_t>(std::max(0.0f,
                    std::floor(thumbnails.scrollDip / std::max(1.0f, oldLayout.cellHeight))));
                anchor = std::min(catalog_.VisibleCount() - 1, row *
                    static_cast<std::size_t>(std::max(1, oldLayout.columns)));
            }
        }
        reviewState_.SetThumbnailSize(newSizeIndex, newSize);
        const auto newLayout = CurrentThumbnailLayout();
        reviewState_.SetThumbnailMaximumScroll(newLayout.maximumScroll);
        reviewState_.SetThumbnailScroll(anchor ?
            quicksift::review::thumbnail::ScrollToCenterThumbnail(newLayout, *anchor) : 0.0f);
        thumbnailPrefetchPlanner_.Reset(reviewState_.Thumbnails().scrollDip);
    }

    void QuickSiftApplicationImpl::ResetView() {
        const std::wstring path = EditableViewPath();
        if (reviewState_.FaceLockEnabled()) ResetFaceOffsetForPath(path);
        ViewState state = EditableViewState();
        state.zoom = 1.0f;
        if (!reviewState_.FaceLockEnabled() || !PrimaryFaceCenter(path)) {
            state.centerX = 0.5f;
            state.centerY = 0.5f;
        }
        state.mode = ZoomMode::Fit;
        StoreEditableViewState(state);
        UpdateZoomModeButton();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::SetMode(ViewMode mode) {
        HideFaceWarningTooltip();
        if (mode == ViewMode::Single && reviewState_.SinglePath().empty() && !catalog_.VisibleEmpty())
            reviewState_.SetSinglePath(VisiblePhoto(0).path.wstring());
        if (mode == ViewMode::Compare && reviewState_.ComparePaths().size() < 2) return;
        const ViewMode previousMode = reviewState_.Mode();
        QS_LOG_EVENT(quicksift::diagnostics::Level::Info, L"Navigation", L"view_mode_changed",
            {L"from", std::to_wstring(static_cast<int>(previousMode))},
            {L"to", std::to_wstring(static_cast<int>(mode))},
            {L"generation", std::to_wstring(generation_)},
            {L"single_path_set", reviewState_.SinglePath().empty() ? L"0" : L"1"},
            {L"compare_count", std::to_wstring(reviewState_.ComparePaths().size())});
        if (previousMode == ViewMode::Compare && mode != ViewMode::Compare)
            reviewState_.ClearCompare();
        reviewState_.SetMode(mode);
        if (filmstripToggle_ && mode != ViewMode::Single) {
            ShowWindow(filmstripToggle_, SW_HIDE);
            filmstripVisible_ = false;
        }
        static_cast<void>(AdvanceReviewNavigationEpoch());
        UpdateModeButtons();
        UpdateToggleButtons();
        if (reviewState_.FaceLockEnabled()) ApplyFaceLockForActive();
        SchedulePredictivePrefetch(0);
        // Single/Compare are exact user tasks: discard speculative visual work and
        // make every currently requested view decode compete in the interactive lane.
        worker_.SetInteractiveVisualFocus(mode == ViewMode::Single || mode == ViewMode::Compare);
        if (mode == ViewMode::Compare) PrioritizeCompareLoads(reviewState_.ActiveComparePath());
        else worker_.SetVisualPriorityPaths({});
        ScheduleViewResourceTrim();
        RequestExifForActive();
        UpdateStatus();
        SetFocus(canvas_);
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::EnterSingleFromSelection() {
        if (catalog_.VisibleEmpty()) return;
        if (reviewState_.Mode() == ViewMode::Compare &&
            !reviewState_.ActiveComparePath().empty() &&
            IsPathVisible(reviewState_.ActiveComparePath())) {
            reviewState_.SetSinglePath(reviewState_.ActiveComparePath());
        } else if (!reviewState_.ThumbnailFocusPath().empty() && IsPathVisible(reviewState_.ThumbnailFocusPath())) {
            reviewState_.SetSinglePath(reviewState_.ThumbnailFocusPath());
        } else if (!selection_.Empty()) {
            reviewState_.SetSinglePath(*selection_.First());
        } else {
            reviewState_.SetSinglePath(VisiblePhoto(0).path.wstring());
        }
        selection_.Clear();
        selection_.Select(reviewState_.SinglePath());
        if (reviewState_.Mode() == ViewMode::Compare) {
            SetMode(ViewMode::Single);
            ResetView();
        } else {
            ResetView();
            SetMode(ViewMode::Single);
        }
    }

    void QuickSiftApplicationImpl::EnterCompareFromSelection() {
        std::vector<std::wstring> paths;
        paths.reserve(6);
        for (size_t visibleIndex = 0; visibleIndex < catalog_.VisibleCount(); ++visibleIndex) {
            const PhotoItem& photo = VisiblePhoto(visibleIndex);
            if (!selection_.Contains(photo.path.wstring())) continue;
            paths.push_back(photo.path.wstring());
            if (paths.size() == 6) break;
        }
        if (!reviewState_.EnterCompare(std::move(paths))) {
            ShowToast(L"Select 2 to 6 photos to enter Compare.");
            return;
        }
        SetMode(ViewMode::Compare);
        PrioritizeCompareLoads(reviewState_.ActiveComparePath());
        if (reviewState_.FaceLockEnabled())
            RecenterFaceLockForPath(reviewState_.ActiveComparePath());
    }

    void QuickSiftApplicationImpl::PrioritizeCompareLoads(const std::wstring& focusPath) {
        if (reviewState_.Mode() != ViewMode::Compare || reviewState_.ComparePaths().size() < 2) return;
        std::vector<std::wstring> ordered;
        ordered.reserve(reviewState_.ComparePaths().size());
        const std::wstring focus = focusPath.empty() ? reviewState_.ActiveComparePath() : focusPath;
        if (!focus.empty()) ordered.push_back(focus);
        for (const std::wstring& path : reviewState_.ComparePaths()) {
            if (_wcsicmp(path.c_str(), focus.c_str()) != 0) ordered.push_back(path);
        }
        worker_.SetVisualPriorityPaths(ordered);
        const D2D1_SIZE_F canvas = CanvasSizeInDips();
        const int target = std::max(2048, static_cast<int>(std::max(canvas.width, canvas.height) *
            CanvasPixelScale()));
        const uint64_t epoch = reviewState_.Epochs().navigation;
        const uint64_t interaction = viewInteractionEpoch_.load(std::memory_order_acquire);
        for (const std::wstring& path : ordered) {
            worker_.EnqueueFull(fs::path(path), target, generation_, JobPriority::Interactive,
                CacheClass::Full, epoch, 0, 0, false, false, interaction);
        }
    }

    void QuickSiftApplicationImpl::UpdateModeButtons() {
        if (!controls_.contains(ID_MODE_THUMBS)) return;
        InvalidateRect(controls_[ID_MODE_THUMBS], nullptr, FALSE);
        InvalidateRect(controls_[ID_MODE_SINGLE], nullptr, FALSE);
        InvalidateRect(controls_[ID_MODE_COMPARE], nullptr, FALSE);
    }

} // namespace quicksift::app
