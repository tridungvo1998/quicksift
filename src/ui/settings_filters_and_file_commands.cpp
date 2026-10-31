// OWNER: Compiled QuickSiftApplication feature module.
#include "app/quicksift_application_internal.h"
#include "core/exif_capture_time.h"
#include "core/priority_policy.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace quicksift::app {
namespace {

namespace vocab = quicksift::core::vocabulary;

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

// CODE GUIDE: See CODE_GUIDE.md -> "How to add a feature".
// OWNER: Settings/filter UI and high-level file command orchestration; decisions belong to their policy owners.

// User commands: metadata/filter/settings menus, button captions, delete
// confirmation, and high-level copy/move/delete command orchestration.

void QuickSiftApplicationImpl::ShowRatingMenu() {
        const int active = ActiveRatingForUi();
        std::vector<MenuItem> items = {
            // Labels come from the canonical vocabulary (core/metadata_vocabulary.h).
            MakeMenuItem(std::wstring(vocab::StarLabel(0)), ID_RATE_CLEAR, active == 0),
            MakeMenuItem(std::wstring(vocab::StarLabel(1)), ID_RATE_1, active == 1),
            MakeMenuItem(std::wstring(vocab::StarLabel(2)), ID_RATE_2, active == 2),
            MakeMenuItem(std::wstring(vocab::StarLabel(3)), ID_RATE_3, active == 3),
            MakeMenuItem(std::wstring(vocab::StarLabel(4)), ID_RATE_4, active == 4),
            MakeMenuItem(std::wstring(vocab::StarLabel(5)), ID_RATE_5, active == 5),
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
        MakeMenuItem(std::wstring(vocab::Pick(vocab::kPickPicked).command), ID_PICK_STATE_PICK, active == 1),
        MakeMenuItem(std::wstring(vocab::Pick(vocab::kPickRejected).command), ID_PICK_STATE_REJECT, active == -1),
        MakeMenuItem(std::wstring(vocab::Pick(vocab::kPickNone).command), ID_PICK_STATE_UNMARK, active == 0),
    };
    const int command = TrackDropdownMenu(controls_[ID_PICK_STATE_MENU], std::move(items));
    switch (command) {
    case ID_PICK_STATE_PICK: ApplyPickState(1); break;
    case ID_PICK_STATE_REJECT: ApplyPickState(-1); break;
    case ID_PICK_STATE_UNMARK: ApplyPickState(0); break;
    default: break;
    }
}

// Settings -> "Mark picks with a label" (core/xmp_interop_policy.h PickCompanionLabel).
void QuickSiftApplicationImpl::ShowPickLabelMappingMenu() {
    const auto control = controls_.find(ID_SETTING_PICK_LABEL);
    if (control == controls_.end()) return;
    std::vector<MenuItem> items;
    items.reserve(vocab::kColors.size());
    for (const auto& entry : vocab::kColors) {
        items.push_back(MakeMenuItem(std::wstring(entry.label), entry.value + 1, pickLabelMapping_ == entry.value));
    }
    const int command = TrackDropdownMenu(control->second, std::move(items));
    if (command <= 0 || command > static_cast<int>(vocab::kColors.size())) return;
    pickLabelMapping_ = command - 1;
    SaveSession();
    UpdatePickLabelMappingButton();
    ShowToast(pickLabelMapping_ == 0 ? L"Picks no longer add a colour label." :
        L"New picks also get this colour label, so Lightroom and Capture One can see them.");
}

void QuickSiftApplicationImpl::UpdatePickLabelMappingButton() {
    const auto control = controls_.find(ID_SETTING_PICK_LABEL);
    if (control == controls_.end() || !control->second) return;
    const std::wstring caption = FillToken(Tr(L"Mark picks with a label: {color}"), L"{color}",
        Tr(vocab::Color(pickLabelMapping_).label));
    wchar_t current[160]{};
    GetWindowTextW(control->second, current, static_cast<int>(std::size(current)));
    if (caption != current) SetWindowTextW(control->second, caption.c_str());
    InvalidateRect(control->second, nullptr, FALSE);
}

void QuickSiftApplicationImpl::ShowColorLabelMenu() {
        const int active = ActiveColorLabelForUi();
        std::vector<MenuItem> items = {
            MakeMenuItem(std::wstring(vocab::Color(vocab::kColorNone).label), ID_LABEL_NONE, active == 0),
            MakeMenuItem(std::wstring(vocab::Color(vocab::kColorRed).label), ID_LABEL_RED, active == 1),
            MakeMenuItem(std::wstring(vocab::Color(vocab::kColorYellow).label), ID_LABEL_YELLOW, active == 2),
            MakeMenuItem(std::wstring(vocab::Color(vocab::kColorGreen).label), ID_LABEL_GREEN, active == 3),
            MakeMenuItem(std::wstring(vocab::Color(vocab::kColorBlue).label), ID_LABEL_BLUE, active == 4),
            MakeMenuItem(std::wstring(vocab::Color(vocab::kColorPurple).label), ID_LABEL_PURPLE, active == 5),
        };
        const int command = TrackDropdownMenu(controls_[ID_COLOR_LABEL], std::move(items));
        if (command >= ID_LABEL_NONE && command <= ID_LABEL_PURPLE) ApplyColorLabel(command - ID_LABEL_NONE);
    }

    void QuickSiftApplicationImpl::ShowRatingFilterMenu() {
        const int active = static_cast<int>(ratingFilter_);
        // Filter labels from the canonical vocabulary, indexed by RatingFilter.
        std::vector<MenuItem> items;
        for (int index = 0; index <= ID_RATING_FILTER_4PLUS - ID_RATING_FILTER_ALL; ++index) {
            items.push_back(MakeMenuItem(std::wstring(vocab::kRatingFilterLabels[static_cast<std::size_t>(index)]),
                ID_RATING_FILTER_ALL + index, active == index));
        }
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
            MakeMenuItem(std::wstring(vocab::kPickFilterAllLabel), ID_PICK_FILTER_ALL, active == 0),
            MakeMenuItem(std::wstring(vocab::Pick(vocab::kPickPicked).filterLabel), ID_PICK_FILTER_PICKS, active == 1),
            MakeMenuItem(std::wstring(vocab::Pick(vocab::kPickRejected).filterLabel), ID_PICK_FILTER_REJECTS, active == 2),
            MakeMenuItem(std::wstring(vocab::Pick(vocab::kPickNone).filterLabel), ID_PICK_FILTER_UNMARKED, active == 3),
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
            MakeMenuItem(std::wstring(vocab::kColorFilterAllLabel), ID_LABEL_FILTER_ALL, active == 0),
        };
        for (const vocab::ColorEntry& color : vocab::kColors) {
            items.push_back(MakeMenuItem(std::wstring(color.filterLabel),
                ID_LABEL_FILTER_NONE + color.value, active == color.value + 1));
        }
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
            hudTextWidthDip_ = 0.0f;
            hudPanelRect_ = {};
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
            viewRetryState_.clear();
            // Single/Compare must reload immediately; otherwise the pane stays
            // blank until the user leaves and re-enters the view.
            if (reviewState_.Mode() == ViewMode::Single || reviewState_.Mode() == ViewMode::Compare) {
                const std::wstring path = reviewState_.Mode() == ViewMode::Single
                    ? reviewState_.SinglePath() : reviewState_.ActivePath();
                if (!path.empty()) {
                    // Force a fresh request on the next paint and also enqueue now.
                    RequestViewDecode(fs::path(path), JobKind::DecodeFull, 2048,
                        CacheClass::Full, JobPriority::Interactive, false);
                }
            }
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
            SaveSession(); // the Settings entry did not persist the theme before 0.9.5.0
            break;
        case ID_SETTING_DIRECT_JPEG: toggledMetadataFlag = MetadataDirectJpeg; break;
        case ID_SETTING_METADATA_RAW: toggledMetadataFlag = MetadataDirectRaw; break;
        case ID_SETTING_METADATA_TIFF: toggledMetadataFlag = MetadataDirectTiff; break;
        case ID_SETTING_METADATA_LOSSY: {
            // JPG and PNG share one switch: normalise a mixed mask before toggling.
            const std::uint32_t lossy = MetadataDirectJpeg | MetadataDirectPng;
            const bool wasOn = (metadataDirectMask_ & lossy) != 0;
            metadataDirectMask_ = (metadataDirectMask_ & ~lossy) | (wasOn ? lossy : std::uint32_t{ 0 });
            toggledMetadataFlag = lossy;
            break;
        }
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
        case ID_SETTING_AUTO_ADVANCE:
            autoAdvance_ = !autoAdvance_;
            SaveSession();
            ShowToast(autoAdvance_ ? L"Auto-advance on: rating, pick or reject moves to the next photo." :
                L"Auto-advance off. Turn Caps Lock on to use it temporarily.");
            if (controls_.contains(ID_SETTING_AUTO_ADVANCE))
                InvalidateRect(controls_[ID_SETTING_AUTO_ADVANCE], nullptr, FALSE);
            InvalidateCanvas();
            return;
        case ID_SETTING_SHORTCUTS: ToggleShortcutSheet(); return;
        case ID_SETTING_SHOW_TIP_AGAIN:
            firstRunTipDismissed_ = false;
            SaveSession();
            ShowFirstRunTipIfNeeded();
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

    void QuickSiftApplicationImpl::ShowFormatsMenu() {
        std::set<std::wstring> exts;
        for (std::size_t i = 0; i < catalog_.PhotoCount(); ++i) {
            std::wstring ext = catalog_.PhotoAt(i).extension;
            for (wchar_t& ch : ext) ch = static_cast<wchar_t>(std::towlower(ch));
            if (!ext.empty() && ext.front() == L'.') ext.erase(ext.begin());
            if (!ext.empty()) exts.insert(std::move(ext));
        }
        std::vector<MenuItem> items;
        items.push_back(MakeMenuItem(L"All", 1, formatExtFilter_.empty()));
        int id = 2;
        for (const auto& ext : exts) {
            std::wstring label = ext;
            for (wchar_t& ch : label) ch = static_cast<wchar_t>(std::towupper(ch));
            items.push_back(MakeMenuItem(label, id, formatExtFilter_ == ext));
            ++id;
        }
        const int command = TrackDropdownMenu(controls_[ID_FORMATS], std::move(items));
        if (command <= 0) return;
        if (command == 1) formatExtFilter_.clear();
        else {
            auto it = exts.begin();
            std::advance(it, command - 2);
            formatExtFilter_ = *it;
        }
        std::wstring label = formatExtFilter_.empty() ? L"Format: All" : L"Format: " + formatExtFilter_;
        for (wchar_t& ch : label) if (ch >= L'a' && ch <= L'z') ch = static_cast<wchar_t>(ch - L'a' + L'A');
        SetWindowTextW(controls_[ID_FORMATS], label.c_str());
        RebuildVisiblePhotos();
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
        HydrateMetadataForFilters();
        const std::vector<std::wstring> dates = catalog_.FilterValues().Dates(); // incremental, no folder scan
        std::vector<MenuItem> items;
        items.push_back(MakeMenuItem(L"All", 1, dateExactFilter_.empty()));
        int id = 2;
        for (const auto& d : dates) {
            items.push_back(MakeMenuItem(d, id, dateExactFilter_ == d));
            ++id;
        }
        const int command = TrackDropdownMenu(controls_[ID_DATE_FILTER], std::move(items));
        if (command <= 0) return;
        if (command == 1) dateExactFilter_.clear();
        else {
            auto it = dates.begin();
            std::advance(it, command - 2);
            dateExactFilter_ = *it;
        }
        std::wstring dateLabel = dateExactFilter_.empty() ? std::wstring(L"Date: All")
            : L"Date: " + dateExactFilter_;
        SetWindowTextW(controls_[ID_DATE_FILTER], dateLabel.c_str());
        RebuildVisiblePhotos();
        InvalidateCanvas();
    }

    // ---- Camera-settings filters (options from indexed values in the folder) ----

    void QuickSiftApplicationImpl::HydrateMetadataForFilters() {
        // No file or database I/O on the UI thread: take whatever the background
        // worker has already read, then queue the rest (capture time + camera
        // settings) on it at P3 priority (yields to decodes and user work, and is
        // cancelled by the generation bump on folder change). Menus show values
        // known now; reopening shows more as hydration completes, and active
        // filters are re-applied as facts arrive (ApplySearchFactUpdates).
        ApplySearchFactUpdates();
        QueueFactHydration(true, false, true);
    }

    void QuickSiftApplicationImpl::ShowIsoFilterMenu() {
        HydrateMetadataForFilters();
        const std::vector<int> values = catalog_.FilterValues().Iso();
        std::vector<MenuItem> items;
        items.push_back(MakeMenuItem(L"All", 1, isoFilter_ < 0));
        int id = 2;
        for (int v : values) {
            items.push_back(MakeMenuItem(L"ISO " + std::to_wstring(v), id, isoFilter_ == v));
            ++id;
        }
        const int command = TrackDropdownMenu(controls_[ID_ISO_FILTER], std::move(items));
        if (command <= 0) return;
        if (command == 1) isoFilter_ = -1;
        else {
            auto it = values.begin();
            std::advance(it, command - 2);
            isoFilter_ = *it;
        }
        std::wstring isoLabel = isoFilter_ < 0 ? std::wstring(L"ISO: All")
            : L"ISO: " + std::to_wstring(isoFilter_);
        SetWindowTextW(controls_[ID_ISO_FILTER], isoLabel.c_str());
        RebuildVisiblePhotos();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::ShowApertureFilterMenu() {
        HydrateMetadataForFilters();
        const std::vector<int> cents = catalog_.FilterValues().ApertureCents(); // f-number * 100
        std::vector<MenuItem> items;
        items.push_back(MakeMenuItem(L"All", 1, apertureFilter_ <= 0.0));
        int id = 2;
        for (int c : cents) {
            const int whole = c / 100;
            const int frac = c % 100;
            std::wstring text = L"f/" + std::to_wstring(whole) + L"." + std::to_wstring(frac / 10);
            items.push_back(MakeMenuItem(text, id, std::abs(apertureFilter_ - c / 100.0) < 0.05));
            ++id;
        }
        const int command = TrackDropdownMenu(controls_[ID_APERTURE_FILTER], std::move(items));
        if (command <= 0) return;
        if (command == 1) apertureFilter_ = 0.0;
        else {
            auto it = cents.begin();
            std::advance(it, command - 2);
            apertureFilter_ = *it / 100.0;
        }
        std::wstring label;
        if (apertureFilter_ <= 0.0) label = L"Aperture: All";
        else label = L"Aperture: f/" + std::to_wstring(static_cast<int>(apertureFilter_ * 10) / 10.0);
        SetWindowTextW(controls_[ID_APERTURE_FILTER], label.c_str());
        RebuildVisiblePhotos();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::ShowFocalFilterMenu() {
        HydrateMetadataForFilters();
        const std::vector<int> mms = catalog_.FilterValues().FocalMm();
        std::vector<MenuItem> items;
        items.push_back(MakeMenuItem(L"All", 1, focalFilter_ <= 0.0));
        int id = 2;
        for (int mm : mms) {
            items.push_back(MakeMenuItem(std::to_wstring(mm) + L"mm", id, std::abs(focalFilter_ - mm) < 0.5));
            ++id;
        }
        const int command = TrackDropdownMenu(controls_[ID_FOCAL_FILTER], std::move(items));
        if (command <= 0) return;
        if (command == 1) focalFilter_ = 0.0;
        else {
            auto it = mms.begin();
            std::advance(it, command - 2);
            focalFilter_ = static_cast<double>(*it);
        }
        std::wstring focalLabel = focalFilter_ <= 0.0 ? std::wstring(L"Focal: All")
            : L"Focal: " + std::to_wstring(static_cast<int>(focalFilter_)) + L"mm";
        SetWindowTextW(controls_[ID_FOCAL_FILTER], focalLabel.c_str());
        RebuildVisiblePhotos();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::ShowShutterFilterMenu() {
        HydrateMetadataForFilters();
        const std::vector<int> denoms = catalog_.FilterValues().ShutterKeys(); // 160 = 1/160; negative = seconds
        std::vector<MenuItem> items;
        items.push_back(MakeMenuItem(L"All", 1, shutterFilter_ <= 0.0));
        int id = 2;
        for (int d : denoms) {
            std::wstring label = d < 0 ? std::to_wstring(-d) + L"s" : L"1/" + std::to_wstring(d);
            const double seconds = d < 0 ? static_cast<double>(-d) : 1.0 / d;
            items.push_back(MakeMenuItem(label, id, std::abs(shutterFilter_ - seconds) < seconds * 0.02));
            ++id;
        }
        const int command = TrackDropdownMenu(controls_[ID_SHUTTER_FILTER], std::move(items));
        if (command <= 0) return;
        if (command == 1) shutterFilter_ = 0.0;
        else {
            auto it = denoms.begin();
            std::advance(it, command - 2);
            shutterFilter_ = *it < 0 ? static_cast<double>(-*it) : 1.0 / *it;
        }
        std::wstring label = L"Shutter: All";
        if (shutterFilter_ > 0.0) {
            if (shutterFilter_ >= 1.0) label = L"Shutter: " + std::to_wstring(static_cast<int>(std::lround(shutterFilter_))) + L"s";
            else label = L"Shutter: 1/" + std::to_wstring(static_cast<int>(std::lround(1.0 / shutterFilter_)));
        }
        SetWindowTextW(controls_[ID_SHUTTER_FILTER], label.c_str());
        RebuildVisiblePhotos();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::ShowCameraFilterMenu() {
        HydrateMetadataForFilters();
        const std::vector<std::wstring> cams = catalog_.FilterValues().Cameras();
        std::vector<MenuItem> items;
        items.push_back(MakeMenuItem(L"All", 1, cameraFilter_.empty()));
        int id = 2;
        for (const auto& c : cams) {
            items.push_back(MakeMenuItem(c, id, cameraFilter_ == c));
            ++id;
        }
        const int command = TrackDropdownMenu(controls_[ID_CAMERA_FILTER], std::move(items));
        if (command <= 0) return;
        if (command == 1) cameraFilter_.clear();
        else {
            auto it = cams.begin();
            std::advance(it, command - 2);
            cameraFilter_ = *it;
        }
        std::wstring camLabel = cameraFilter_.empty() ? std::wstring(L"Camera: All")
            : L"Camera: " + cameraFilter_;
        SetWindowTextW(controls_[ID_CAMERA_FILTER], camLabel.c_str());
        RebuildVisiblePhotos();
        InvalidateCanvas();
    }

    void QuickSiftApplicationImpl::ShowHourFilterMenu() {
        HydrateMetadataForFilters();
        const std::vector<int> hours = catalog_.FilterValues().Hours();
        std::vector<MenuItem> items;
        items.push_back(MakeMenuItem(L"All", 1, hourFilter_ < 0));
        int id = 2;
        for (int h : hours) {
            std::wstring text = (h < 10 ? L"0" : L"") + std::to_wstring(h) + L":00";
            items.push_back(MakeMenuItem(text, id, hourFilter_ == h));
            ++id;
        }
        const int command = TrackDropdownMenu(controls_[ID_HOUR_FILTER], std::move(items));
        if (command <= 0) return;
        if (command == 1) hourFilter_ = -1;
        else {
            auto it = hours.begin();
            std::advance(it, command - 2);
            hourFilter_ = *it;
        }
        std::wstring label;
        if (hourFilter_ < 0) label = L"Hour: All";
        else {
            std::wstring pad = hourFilter_ < 10 ? std::wstring(L"0") : std::wstring(L"");
            label = L"Hour: " + pad + std::to_wstring(hourFilter_) + L":00";
        }
        SetWindowTextW(controls_[ID_HOUR_FILTER], label.c_str());
        RebuildVisiblePhotos();
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
        // 0.9.6.0: table-driven (localization.cpp) instead of inline EN/VI text.
        const wchar_t* key = active.size() == 1 ?
            L"QuickSift will move {count} selected photo and matching XMP sidecars into:\n\n{folder}\n\nThis does not use the Recycle Bin. You can Undo while the protected file-operation history remains available." :
            L"QuickSift will move {count} selected photos and matching XMP sidecars into:\n\n{folder}\n\nThis does not use the Recycle Bin. You can Undo while the protected file-operation history remains available.";
        std::wstring message = FillToken(Tr(key), L"{count}", std::to_wstring(active.size()));
        message = FillToken(std::move(message), L"{folder}", destination.wstring());
        ShowGlassConfirmation(L"Move selected photos to Deleted?", message,
            L"Move to Deleted", PendingGlassAction::DeleteSelection, GlassAlertKind::Warning);
    }

    // Copy / Move / Delete-to-folder (0.9.5.0 round 19: split into the RAW pairing,
    // destination, preflight, storage and transaction-building stages with no
    // behaviour change). Every stage that stops the operation shows its own alert
    // and returns false / nullopt before any file is touched.
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
        if (copyMoveRawWithJpg_ && !remove &&
            !AddRawCompanionsForSelection(active, missingRawCompanion)) return;

        const std::optional<fs::path> destination = ChooseFileOperationDestination(move, remove);
        if (!destination) return;
        const fs::path destinationFolder = *destination;

        std::vector<PlannedFileTransfer> plan;
        if (!PlanFileTransfers(active, destinationFolder, move, remove, plan)) return;

        // The plan owns the paths from here. Release duplicate selection and
        // collision-check storage before potentially long filesystem work.
        active.clear();
        active.shrink_to_fit();

        if (!CheckFileOperationStorage(plan, destinationFolder, move, remove)) return;

        std::error_code createError;
        fs::create_directories(destinationFolder, createError);
        if (createError) {
            ShowGlassAlert(remove ? L"Deleted folder unavailable" : L"Destination unavailable",
                L"QuickSift could not create or access the destination folder. No files were changed.",
                GlassAlertKind::Error);
            return;
        }

        quicksift::transactions::FileTransactionPlan transaction =
            BuildFileTransactionPlan(plan, move, remove, missingRawCompanion);

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

    // "Copy/move RAW with JPG": append each selected JPEG's same-stem RAW siblings.
    // Returns false (after an alert) when a source folder could not be inspected.
    bool QuickSiftApplicationImpl::AddRawCompanionsForSelection(std::vector<fs::path>& active,
        bool& missingRawCompanion) {
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
            return false;
        }
        return true;
    }

    // Deleted subfolder (remove) or a picked folder; nullopt when cancelled or when
    // the destination is not a usable normal folder (an alert is shown).
    std::optional<fs::path> QuickSiftApplicationImpl::ChooseFileOperationDestination(bool move, bool remove) {
        fs::path destinationFolder;
        if (remove) {
            destinationFolder = currentFolder_ / L"Deleted";
        } else {
            const std::wstring destinationTitle = Tr(move ? L"Move selected photos to" : L"Copy selected photos to");
            const auto destination = PickFolder(hwnd_, destinationTitle.c_str());
            if (!destination) return std::nullopt;
            destinationFolder = *destination;
        }

        std::error_code destinationStatusError;
        const fs::file_status destinationStatus = fs::symlink_status(destinationFolder, destinationStatusError);
        if (!destinationStatusError && fs::exists(destinationStatus) &&
            (fs::is_symlink(destinationStatus) || !fs::is_directory(destinationStatus))) {
            ShowGlassAlert(remove ? L"Deleted folder conflict" : L"Destination conflict",
                L"The destination path is not a normal folder. QuickSift stopped before changing any files.",
                GlassAlertKind::Warning);
            return std::nullopt;
        }
        if (destinationStatusError && destinationStatusError != std::errc::no_such_file_or_directory) {
            ShowGlassAlert(remove ? L"Deleted folder unavailable" : L"Destination unavailable",
                L"QuickSift could not safely inspect the destination folder. No files were changed.",
                GlassAlertKind::Error);
            return std::nullopt;
        }
        return destinationFolder;
    }

    // Index the parent folder's media by sidecar path (once per folder) and report
    // whether the sidecar is also owned by media outside this operation.
    bool QuickSiftApplicationImpl::MediaSidecarHasExternalOwner(
        std::unordered_map<std::wstring, SidecarOwnerIndex>& directories,
        const std::unordered_set<std::wstring>& operationSourceKeys,
        const fs::path& source, const fs::path& sidecar, bool& scanSucceeded) const {
        const fs::path parent = source.parent_path();
        SidecarOwnerIndex& index = directories[NormalizedPathKey(parent)];
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
    }

    // Preflight the whole batch (no overwrite, no automatic rename, no partial
    // selection). Returns false after the conflict alert.
    bool QuickSiftApplicationImpl::PlanFileTransfers(const std::vector<fs::path>& active,
        const fs::path& destinationFolder, bool move, bool remove,
        std::vector<PlannedFileTransfer>& plan) {
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
        std::wstring conflictReason; // localization key (literals below are marked loc-key)
        std::wstring conflictFile;
        int conflictCount = 0;

        auto recordConflict = [&](const fs::path& path, const wchar_t* reason) {
            ++conflictCount;
            if (conflictReason.empty()) {
                conflictReason = reason;
                conflictFile = path.filename().wstring();
            }
        };

        std::unordered_map<std::wstring, SidecarOwnerIndex> rawSidecarDirectories;
        rawSidecarDirectories.reserve(4);
        auto mediaSidecarHasExternalOwner = [&](const fs::path& source,
            const fs::path& sidecar, bool& scanSucceeded) {
            return MediaSidecarHasExternalOwner(rawSidecarDirectories, operationSourceKeys,
                source, sidecar, scanSucceeded);
        };

        // Preflight the complete batch before creating folders or touching any
        // image. A single collision stops the whole operation: no overwrite,
        // no automatic “(2)” name, and no partially processed selection.
        for (const fs::path& source : active) {
            std::error_code ec;
            const fs::file_status sourceStatus = fs::symlink_status(source, ec);
            if (ec || fs::is_symlink(sourceStatus) || !fs::is_regular_file(sourceStatus)) {
                recordConflict(source, fs::is_symlink(sourceStatus) ?
                    L"Symbolic-link image paths are not moved or deleted for safety." : // loc-key
                    L"The source file is missing or inaccessible."); // loc-key
                continue;
            }

            PlannedFileTransfer transfer;
            transfer.source = source;
            transfer.destination = destinationFolder / source.filename();
            transfer.sourceSidecar = SidecarPathFor(source);
            transfer.destinationSidecar = SidecarPathFor(transfer.destination);

            bool transferValid = true;
            ec.clear();
            const fs::file_status sidecarStatus = fs::symlink_status(transfer.sourceSidecar, ec);
            if (ec && ec != std::errc::no_such_file_or_directory) {
                recordConflict(transfer.sourceSidecar,
                    L"QuickSift could not safely inspect the matching XMP sidecar."); // loc-key
                transferValid = false;
            } else if (!ec && fs::exists(sidecarStatus)) {
                if (fs::is_symlink(sidecarStatus)) {
                    recordConflict(transfer.sourceSidecar,
                        L"The matching XMP is a symbolic link and was left untouched for safety."); // loc-key
                    transferValid = false;
                } else if (!fs::is_regular_file(sidecarStatus)) {
                    recordConflict(transfer.sourceSidecar,
                        L"The matching XMP path is not a normal file."); // loc-key
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
                        L"QuickSift could not finish checking whether this XMP is shared."); // loc-key
                    continue;
                }
                if (sharedSidecar) {
                    recordConflict(transfer.sourceSidecar,
                        L"This XMP sidecar is shared by another media file, so moving it would detach metadata from that file."); // loc-key
                    continue;
                }
            }

            if (NormalizedPathKey(source) == NormalizedPathKey(transfer.destination)) {
                recordConflict(transfer.destination, L"The destination is the same as the source."); // loc-key
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
            // 0.9.6.0: every piece is a table key; the file name stays verbatim.
            std::wstring message = Tr(remove ? L"Delete-to-folder was stopped before any files were changed." :
                (move ? L"Move was stopped before any files were changed." :
                    L"Copy was stopped before any files were changed."));
            if (!conflictReason.empty()) message += L"\n\n" + Tr(conflictReason) + L"\n\n" + conflictFile;
            if (conflictCount > 1) {
                message += L"\n\n" + FillToken(Tr(L"Plus {count} other conflict(s)."), L"{count}",
                    std::to_wstring(conflictCount - 1));
            }
            message += L"\n\n" + Tr(L"QuickSift never overwrites or automatically renames files.");
            ShowGlassAlertText(Tr(L"File conflict — nothing changed"), message, GlassAlertKind::Warning);
            return false;
        }
        return true;
    }

    // Verified copies need room for the complete destination bytes plus a reserve.
    // Returns false after the storage alert.
    bool QuickSiftApplicationImpl::CheckFileOperationStorage(const std::vector<PlannedFileTransfer>& plan,
        const fs::path& destinationFolder, bool move, bool remove) {
        // Verified copies need room for the complete destination bytes plus a
        // reserve for filesystem metadata and other applications. Same-volume
        // renames do not duplicate file data, while cross-volume moves do.
        std::uint64_t requiredDestinationBytes = 0;
        bool storageSizeKnown = true;
        const auto destinationVolume = VolumeRootForPath(destinationFolder);
        std::unordered_set<std::wstring> sizedSidecars;
        sizedSidecars.reserve(plan.size() + 1);
        for (const PlannedFileTransfer& transfer : plan) {
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
            return false;
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
                return false;
            }
        }
        return true;
    }

    quicksift::transactions::FileTransactionPlan QuickSiftApplicationImpl::BuildFileTransactionPlan(
        std::vector<PlannedFileTransfer>& plan, bool move, bool remove, bool missingRawCompanion) {
        const std::wstring historyLabel = remove ? L"Delete to subfolder" :
            (move ? L"Move files" : L"Copy files");
        std::vector<std::pair<std::size_t, std::size_t>> historyPathLengths;
        historyPathLengths.reserve(plan.size() * 2);
        std::unordered_set<std::wstring> historySidecars;
        historySidecars.reserve(plan.size() + 1);
        for (const PlannedFileTransfer& transfer : plan) {
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
        for (PlannedFileTransfer& transfer : plan) {
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
        return transaction;
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
