// OWNER: Thumbnail search box (floating field, Thumbnail view only).
//
// The query language lives in core/search_query (portable, checked by
// scripts/checks/search_query_checks.cpp) and compiles to the same
// core::predicate::Clause list the Filter tab uses. Evaluation reads only the
// authoritative metadata model (CatalogStore PhotoItem / immutable CatalogSnapshot);
// it never touches the disk on the UI thread:
//   - small folders (<= kInlineSearchLimit) are evaluated inline over the store;
//   - larger folders are evaluated on the search worker against a snapshot and the
//     result is discarded unless it is still the latest request (serial).
// Capture time (EXIF DateTimeOriginal, else file-modified time) and user comments are
// hydrated lazily into the store by the same worker, only when a query needs them.
// The visible list is rebuilt through RebuildVisiblePhotos (CullAllowed ANDs the
// Filter tab with the search), so virtualization, scroll settle, the reconcile sweep
// and selection retention behave exactly as for filters.
#include "app/quicksift_application_internal.h"
#include "core/priority_policy.h"
#include "diagnostics/log_categories.h"
#include "core/log_rate_limiter.h"

#include "core/exif_capture_time.h"

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace quicksift::app {
namespace {

using quicksift::ui::framework::FloatingFieldLayout;
using quicksift::ui::framework::FloatingFieldPresentation;
using quicksift::ui::framework::MacOsUiFramework;
using quicksift::ui::framework::TextInputOptions;
namespace pred = quicksift::core::predicate;
namespace sq = quicksift::core::search;
namespace exif = quicksift::core::exif;

constexpr float kSearchFieldTopDip = 10.0f;
constexpr float kSearchFieldHeightDip = 36.0f;
// Right edge clears the thumbnail scrollbar and its 16-DIP hover zone (x >= w - 30).
constexpr float kSearchFieldRightGapDip = 34.0f;
constexpr float kSearchFieldMinWidthDip = 200.0f;
constexpr float kSearchFieldMaxWidthDip = 420.0f;
constexpr float kSearchLaneGapDip = 8.0f;
constexpr std::size_t kInlineSearchLimit = 2000;
constexpr UINT kSearchDebounceMs = 150;
constexpr UINT kSearchRefreshMs = 400;
constexpr std::size_t kHydrateBatch = 32;
constexpr DWORD kCaptureFirstReadBytes = 64u * 1024u;
constexpr std::uint8_t kQueuedCapture = 1;
constexpr std::uint8_t kQueuedComment = 2;
constexpr std::uint8_t kQueuedCamera = 4;

class FileHandle {
public:
    explicit FileHandle(HANDLE handle) noexcept : handle_(handle) {}
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    ~FileHandle() { if (Valid()) CloseHandle(handle_); }
    [[nodiscard]] bool Valid() const noexcept { return handle_ && handle_ != INVALID_HANDLE_VALUE; }
    [[nodiscard]] HANDLE Get() const noexcept { return handle_; }

private:
    HANDLE handle_ = nullptr;
};

pred::LocalDateTime FromSystemTime(const SYSTEMTIME& time) noexcept {
    pred::LocalDateTime result;
    result.year = time.wYear;
    result.month = time.wMonth;
    result.day = time.wDay;
    result.hour = time.wHour;
    result.minute = time.wMinute;
    result.second = time.wSecond;
    return result;
}

// File last-write time in the machine's local time zone (the documented fallback).
std::optional<pred::LocalDateTime> FileModifiedLocalTime(const fs::path& path) noexcept {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return std::nullopt;
    SYSTEMTIME utc{};
    SYSTEMTIME local{};
    if (!FileTimeToSystemTime(&data.ftLastWriteTime, &utc)) return std::nullopt;
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return std::nullopt;
    const pred::LocalDateTime result = FromSystemTime(local);
    if (!result.Valid()) return std::nullopt;
    return result;
}

// Reads the first 64 KB (JPEG APP1 / most RAW IFDs), then up to 512 KB when no
// DateTimeOriginal was found there. Shared read access; never locks the file.
std::optional<exif::CaptureTimeResult> ReadCaptureTime(const fs::path& path) {
    FileHandle file(CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!file.Valid()) return std::nullopt;
    std::vector<std::uint8_t> buffer(kCaptureFirstReadBytes);
    DWORD read = 0;
    if (!ReadFile(file.Get(), buffer.data(), kCaptureFirstReadBytes, &read, nullptr)) return std::nullopt;
    buffer.resize(read);
    std::optional<exif::CaptureTimeResult> result = exif::FindCaptureTime(buffer);
    if ((result && result->tag == exif::CaptureTag::Original) || read < kCaptureFirstReadBytes) return result;
    const std::size_t total = exif::kCaptureTimeReadBytes;
    buffer.resize(total);
    DWORD more = 0;
    if (!ReadFile(file.Get(), buffer.data() + read, static_cast<DWORD>(total - read), &more, nullptr)) return result;
    buffer.resize(static_cast<std::size_t>(read) + more);
    const std::optional<exif::CaptureTimeResult> wider = exif::FindCaptureTime(buffer);
    if (wider && (!result || wider->tag > result->tag)) return wider;
    return result;
}

std::vector<std::uint8_t> EvaluateSnapshot(const quicksift::core::CatalogSnapshot& snapshot,
    const std::vector<pred::Clause>& clauses) {
    std::vector<std::uint8_t> matches(snapshot.Size(), 0);
    std::size_t index = 0;
    for (const auto& chunk : snapshot.chunks) {
        for (const quicksift::core::ImageMetadataRecord& record : *chunk) {
            matches[index++] = pred::MatchesAll(clauses, quicksift::core::FactsOf(record)) ? 1 : 0;
        }
    }
    return matches;
}

std::wstring ReplaceAllTokens(std::wstring text, std::wstring_view token, const std::wstring& value) {
    std::size_t position = 0;
    while ((position = text.find(token, position)) != std::wstring::npos) {
        text.replace(position, token.size(), value);
        position += value.size();
    }
    return text;
}

} // namespace

// ---- Geometry ---------------------------------------------------------------------

bool QuickSiftApplicationImpl::SearchFieldAvailable() const {
    if (!searchInput_ || reviewState_.Mode() != ViewMode::Thumbnails || currentFolder_.empty()) return false;
    const D2D1_RECT_F frame = SearchFieldFrameDip();
    return frame.right > frame.left;
}

D2D1_RECT_F QuickSiftApplicationImpl::SearchFieldFrameDip() const {
    const D2D1_SIZE_F canvas = CanvasSizeInDips();
    // Left: never under the left hover rail / its indicator (kThumbnailRailSafeInsetDip).
    const float available = canvas.width - kThumbnailRailSafeInsetDip - kSearchFieldRightGapDip - 12.0f;
    const float width = std::min({ kSearchFieldMaxWidthDip,
        std::max(kSearchFieldMinWidthDip, canvas.width * 0.34f), available });
    if (width < kSearchFieldMinWidthDip * 0.8f || canvas.height < 160.0f) return D2D1::RectF();
    const float right = canvas.width - kSearchFieldRightGapDip;
    return D2D1::RectF(right - width, kSearchFieldTopDip, right, kSearchFieldTopDip + kSearchFieldHeightDip);
}

float QuickSiftApplicationImpl::ThumbnailSearchLaneDip() const {
    // The lane follows availability (not the fade) so the grid never jumps mid-fade.
    return SearchFieldAvailable() ? kSearchFieldTopDip + kSearchFieldHeightDip + kSearchLaneGapDip : 0.0f;
}

namespace {
// Opacity at which the native input child may sit on the painted glass field.
constexpr float kSearchInputShowOpacity = 0.995f;
} // namespace

bool QuickSiftApplicationImpl::SearchFieldContains(float x, float y) const {
    if (searchFieldOpacity_ < 0.5f || !SearchFieldAvailable()) return false;
    const D2D1_RECT_F frame = SearchFieldFrameDip();
    return x >= frame.left && x <= frame.right && y >= frame.top && y <= frame.bottom;
}

// ---- Creation, layout, theme ----------------------------------------------------------

bool QuickSiftApplicationImpl::CreateSearchField() {
    TextInputOptions options;
    options.multiline = false;
    options.notifyParent = true;
    options.borderless = true;
    options.maxLength = 512;
    options.placeholder = Tr(L"Search names, 8AM, date:12, 5 stars, red, picked\u2026");
    searchInput_ = uiFramework_.CreateTextInput(hwnd_, instance_, ID_THUMBNAIL_SEARCH, options);
    if (!searchInput_) return false;
    ShowWindow(searchInput_, SW_HIDE);
    searchInputShown_ = false;
    UpdateSearchFieldTheme();
    return true;
}

void QuickSiftApplicationImpl::UpdateSearchFieldTheme() {
    if (!searchInput_) return;
    const ThemePalette palette = Palette();
    uiFramework_.SetD2DTextInputPalette(searchInput_, palette, currentDpi_);
    uiFramework_.SetTextInputParentSurface(searchInput_, MacOsUiFramework::FloatingFieldFill(palette));
    uiFramework_.SetTextInputPlaceholder(searchInput_,
        Tr(L"Search names, 8AM, date:12, 5 stars, red, picked\u2026"));
    searchInputPixels_ = {};
    LayoutSearchField();
    InvalidateCanvas();
}

void QuickSiftApplicationImpl::LayoutSearchField() {
    if (!searchInput_ || !canvas_) return;
    // The native input child clears with the field's flat fill at full strength, so
    // it is shown only once the painted glass is fully opaque (and hidden as soon as
    // the field starts to fade out): a half-faded field around an opaque child read
    // as a rectangular block inside the capsule.
    const bool show = SearchFieldAvailable() && !uiFramework_.Alerts().Visible() &&
        searchFieldOpacity_ >= kSearchInputShowOpacity;
    if (!show) {
        if (searchInputShown_) {
            if (GetFocus() == searchInput_) SetFocus(canvas_);
            ShowWindow(searchInput_, SW_HIDE);
            searchInputShown_ = false;
        }
        return;
    }
    const FloatingFieldLayout layout = MacOsUiFramework::LayoutFloatingField(SearchFieldFrameDip(),
        searchActive_, !searchText_.empty());
    RECT canvasRect{};
    GetWindowRect(canvas_, &canvasRect);
    MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&canvasRect), 2);
    const float scale = CanvasDpi() / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    RECT pixels{
        canvasRect.left + static_cast<LONG>(std::lround(layout.input.left * scale)),
        canvasRect.top + static_cast<LONG>(std::lround(layout.input.top * scale)),
        canvasRect.left + static_cast<LONG>(std::lround(layout.input.right * scale)),
        canvasRect.top + static_cast<LONG>(std::lround(layout.input.bottom * scale)) };
    if (pixels.right <= pixels.left || pixels.bottom <= pixels.top) return;
    const bool moved = !EqualRect(&pixels, &searchInputPixels_);
    if (moved || !searchInputShown_) {
        // Directly above the canvas and below every other sibling (left pane, its
        // tabs, the comment input), so an open pane always covers the field.
        UINT flags = SWP_NOACTIVATE | SWP_SHOWWINDOW;
        SetWindowPos(searchInput_, HWND_BOTTOM, pixels.left, pixels.top,
            pixels.right - pixels.left, pixels.bottom - pixels.top, flags);
        SetWindowPos(canvas_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        searchInputPixels_ = pixels;
        searchInputShown_ = true;
    }
}

bool QuickSiftApplicationImpl::SearchFieldAnimating() const noexcept {
    return searchInput_ && uiFramework_.Motion().IsAnimating(quicksift::ui::animation::element::kSearchField);
}

void QuickSiftApplicationImpl::SyncSearchFieldTransition(std::chrono::steady_clock::time_point now) {
    if (!searchInput_) return;
    namespace anim = quicksift::ui::animation;
    anim::Animator& motion = uiFramework_.Motion();
    if (!motion.Contains(anim::element::kSearchField))
        motion.Snap(anim::element::kSearchField, searchFieldOpacity_, anim::surface::kCanvas);
    // Fade only (no slide): the native text input child sits at a fixed position
    // inside the glass field, so moving the painted field would detach the two.
    const bool target = SearchFieldAvailable() && !uiFramework_.Alerts().Visible();
    motion.SetShown(anim::element::kSearchField, target, MacOsUiFramework::kSearchFieldTransition,
        anim::surface::kCanvas, now);
}

void QuickSiftApplicationImpl::ApplySearchFieldTransition() {
    if (!searchInput_) return;
    searchFieldOpacity_ = uiFramework_.Motion().Appear(quicksift::ui::animation::element::kSearchField,
        MacOsUiFramework::kSearchFieldTransition, searchFieldOpacity_).opacity;
}

void QuickSiftApplicationImpl::UpdateSearchFieldAnimation() {
    if (!searchInput_) return;
    const bool target = SearchFieldAvailable() && !uiFramework_.Alerts().Visible();
    const bool wasShown = searchInputShown_;
    // Hide the input at once when leaving Thumbnail view; show it once the chrome is
    // mostly faded in. LayoutSearchField does both and keeps the geometry current.
    if (!target || searchFieldOpacity_ >= kSearchInputShowOpacity || wasShown) LayoutSearchField();
    const bool focused = searchInputShown_ && GetFocus() == searchInput_;
    if (focused != searchFocusPainted_) {
        searchFocusPainted_ = focused;
        InvalidateCanvas();
    }
    // P3 demand: a focused box or an applied query pauses P5 work (face, analysis)
    // and lets search reads run ahead of background metadata. Lock-free publish.
    // Only while Thumbnail view shows the box: Single/Compare face work (FaceLock,
    // view jump to the face) is never paused by a query left applied in the grid.
    const bool demandActive = searchActive_ && SearchFieldAvailable();
    if (demandActive != searchDemandPublishedActive_ || focused != searchDemandPublishedFocused_) {
        searchDemandPublishedActive_ = demandActive;
        searchDemandPublishedFocused_ = focused;
        quicksift::core::priority::Demand().SetSearch(demandActive, focused);
        quicksift::core::priority::Demand().Touch();
        QS_LOG_EVENT(quicksift::diagnostics::Level::Trace, quicksift::diagnostics::category::kScheduler,
            L"search_demand", { L"active", demandActive ? L"1" : L"0" }, { L"focused", focused ? L"1" : L"0" });
    }
    // Lost-wake backstop: the worker stored results but its PostMessage failed.
    if (searchUnconsumed_.load(std::memory_order_acquire) &&
        !searchMessagePosted_.load(std::memory_order_acquire)) OnSearchWorkerMessage();
    if (!searchActive_) return;
    // Hydrate facts for photos that arrived since the last check (scan in progress).
    if (searchHydrateCheckedCount_ != catalog_.PhotoCount() || searchHydrateGeneration_ != generation_)
        EnsureSearchFactsHydrated();
    // Any change to the authoritative store (transactions, reconcile, hydration, new
    // scan batches) re-evaluates the applied query through the same debounce.
    if (!searchDebounceArmed_ && searchInFlightSerial_ == 0 && catalog_.MetadataEpoch() != searchMatchesEpoch_) {
        // Longer than the typing debounce: scans and hydration bump the epoch often and
        // each re-evaluation snapshots the store.
        searchDebounceArmed_ = StartUiTimer(ID_TIMER_SEARCH_DEBOUNCE, kSearchRefreshMs, L"search refresh");
    }
    if (searchHydrateDone_.load(std::memory_order_acquire) < searchHydrateTotal_.load(std::memory_order_acquire))
        InvalidateCanvas(); // progress ellipsis in the count
}

// ---- Painting and pointer ------------------------------------------------------------------

std::wstring QuickSiftApplicationImpl::SearchCountText() const {
    if (!searchActive_) return {};
    const wchar_t separator = localizer_.CurrentLanguage() == quicksift::Language::Vietnamese ? L'.' : L',';
    std::wstring text = Tr(L"{shown} of {total}");
    text = ReplaceAllTokens(std::move(text), L"{shown}", sq::GroupThousands(catalog_.VisibleCount(), separator));
    text = ReplaceAllTokens(std::move(text), L"{total}", sq::GroupThousands(searchBaseCount_, separator));
    if (searchHydrateDone_.load(std::memory_order_acquire) < searchHydrateTotal_.load(std::memory_order_acquire) ||
        searchInFlightSerial_ != 0) {
        text += L" \u2026";
    }
    return text;
}

void QuickSiftApplicationImpl::DrawSearchFieldOverlay() {
    if (!renderTarget_ || !searchInput_ || searchFieldOpacity_ <= 0.001f ||
        reviewState_.Mode() != ViewMode::Thumbnails) return;
    const D2D1_RECT_F frame = SearchFieldFrameDip();
    if (frame.right <= frame.left) return;
    const std::wstring count = SearchCountText();
    FloatingFieldPresentation presentation;
    presentation.layout = MacOsUiFramework::LayoutFloatingField(frame, searchActive_, !searchText_.empty());
    presentation.opacity = searchFieldOpacity_;
    presentation.focused = searchFocusPainted_;
    presentation.clearVisible = !searchText_.empty();
    presentation.clearHot = searchClearHot_;
    presentation.noResults = searchActive_ && catalog_.VisibleEmpty() && searchInFlightSerial_ == 0;
    presentation.countText = count;
    uiFramework_.PaintFloatingField(renderTarget_.Get(), presentation, Palette(), smallText_.Get());
}

bool QuickSiftApplicationImpl::HandleSearchFieldMouseDown(float x, float y) {
    if (!SearchFieldContains(x, y)) return false;
    const FloatingFieldLayout layout = MacOsUiFramework::LayoutFloatingField(SearchFieldFrameDip(),
        searchActive_, !searchText_.empty());
    const bool onClear = !searchText_.empty() && x >= layout.clear.left && x <= layout.clear.right &&
        y >= layout.clear.top && y <= layout.clear.bottom;
    if (onClear) SetWindowTextW(searchInput_, L""); // EN_CHANGE -> applies the empty query now
    SetFocus(searchInput_);
    InvalidateCanvas();
    return true;
}

void QuickSiftApplicationImpl::UpdateSearchFieldHover(float x, float y) {
    bool hot = false;
    if (!searchText_.empty() && SearchFieldContains(x, y)) {
        const FloatingFieldLayout layout = MacOsUiFramework::LayoutFloatingField(SearchFieldFrameDip(),
            searchActive_, true);
        hot = x >= layout.clear.left && x <= layout.clear.right && y >= layout.clear.top && y <= layout.clear.bottom;
    }
    if (hot != searchClearHot_) {
        searchClearHot_ = hot;
        InvalidateCanvas();
    }
}

void QuickSiftApplicationImpl::ClearSearchFieldHover() {
    if (!searchClearHot_) return;
    searchClearHot_ = false;
    InvalidateCanvas();
}

// ---- Keyboard ---------------------------------------------------------------------------------

bool QuickSiftApplicationImpl::HandleSearchKey(const MSG& message) {
    if (message.message != WM_KEYDOWN || !searchInput_) return false;
    const UINT key = static_cast<UINT>(message.wParam);
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    if (message.hwnd == searchInput_) {
        if (key == VK_ESCAPE) {
            // First Esc clears the query, the second leaves the field.
            if (!searchText_.empty()) SetWindowTextW(searchInput_, L"");
            else SetFocus(canvas_);
            InvalidateCanvas();
            return true;
        }
        if (key == VK_RETURN || key == VK_DOWN || key == VK_TAB) {
            if (searchDebounceArmed_ && searchText_ != searchAppliedText_) ApplySearchText();
            SetFocus(canvas_);
            InvalidateCanvas();
            return true;
        }
    }
    // Ctrl+F (no Shift) focuses the field in Thumbnail view. Elsewhere Ctrl+F keeps
    // its previous meaning (the F zoom-to-fit shortcut ignores Ctrl).
    if (ctrl && !shift && key == L'F' && SearchFieldAvailable()) {
        FocusSearchField();
        return true;
    }
    return false;
}

void QuickSiftApplicationImpl::FocusSearchField() {
    if (!searchInput_) return;
    // Fully shown at once (the input child only appears over an opaque field).
    searchFieldOpacity_ = 1.0f;
    uiFramework_.Motion().Snap(quicksift::ui::animation::element::kSearchField, searchFieldOpacity_,
        quicksift::ui::animation::surface::kCanvas);
    LayoutSearchField();
    if (!searchInputShown_) return;
    SetFocus(searchInput_);
    uiFramework_.SelectAllTextInput(searchInput_);
    InvalidateCanvas();
}

// ---- Query lifecycle ---------------------------------------------------------------------------

void QuickSiftApplicationImpl::OnSearchTextChanged() {
    if (!searchInput_) return;
    const int length = GetWindowTextLengthW(searchInput_);
    std::wstring text(static_cast<std::size_t>(std::max(0, length)), L'\0');
    if (length > 0) GetWindowTextW(searchInput_, text.data(), length + 1);
    if (text == searchText_) return;
    const bool clearChanged = text.empty() != searchText_.empty();
    searchText_ = std::move(text);
    if (clearChanged) LayoutSearchField(); // the clear button narrows the input
    if (searchText_.empty()) {
        ApplySearchText(); // clearing is immediate
    } else {
        KillTimer(hwnd_, ID_TIMER_SEARCH_DEBOUNCE);
        searchDebounceArmed_ = StartUiTimer(ID_TIMER_SEARCH_DEBOUNCE, kSearchDebounceMs, L"search debounce");
        if (!searchDebounceArmed_) ApplySearchText();
    }
    InvalidateCanvas();
}

void QuickSiftApplicationImpl::OnSearchDebounceTimer() {
    KillTimer(hwnd_, ID_TIMER_SEARCH_DEBOUNCE);
    searchDebounceArmed_ = false;
    if (searchText_ != searchAppliedText_) ApplySearchText();
    else if (searchActive_) RequestSearchEvaluation(false);
}

void QuickSiftApplicationImpl::ApplySearchText() {
    KillTimer(hwnd_, ID_TIMER_SEARCH_DEBOUNCE);
    searchDebounceArmed_ = false;
    searchAppliedText_ = searchText_;
    sq::SearchQuery parsed = sq::ParseSearchQuery(searchText_);
    if (parsed.Empty()) {
        if (searchActive_) ClearSearch();
        searchQuery_ = std::move(parsed);
        LayoutSearchField();
        InvalidateCanvas();
        return;
    }
    const bool changed = !searchActive_ || parsed.clauses != searchQuery_.clauses;
    if (!searchActive_) {
        // Remember the view so clearing the search restores it.
        searchSavedScroll_ = reviewState_.Thumbnails().scrollDip;
        searchSavedFocus_ = reviewState_.ThumbnailFocusPath();
    }
    const bool layoutChanged = !searchActive_;
    searchQuery_ = std::move(parsed);
    searchActive_ = true;
    if (layoutChanged) LayoutSearchField(); // the count slot appears
    if (changed) {
        // Privacy: the query text is never logged, only its shape.
        static quicksift::core::LogRateLimiter limiter(6, std::chrono::seconds(10));
        std::uint64_t suppressed = 0;
        if (limiter.Allow(&suppressed)) {
            QS_LOG_EVENT(quicksift::diagnostics::Level::Info, quicksift::diagnostics::category::kSearch,
                L"query_applied",
                { L"clauses", std::to_wstring(searchQuery_.clauses.size()) },
                { L"chars", std::to_wstring(searchText_.size()) },
                { L"photos", std::to_wstring(catalog_.PhotoCount()) },
                { L"mode", catalog_.PhotoCount() <= kInlineSearchLimit ? L"inline" : L"worker" },
                { L"suppressed", std::to_wstring(suppressed) });
        }
    }
    EnsureSearchFactsHydrated();
    if (changed || catalog_.MetadataEpoch() != searchMatchesEpoch_) RequestSearchEvaluation(changed);
    InvalidateCanvas();
}

void QuickSiftApplicationImpl::ClearSearch() {
    searchActive_ = false;
    searchQuery_ = {};
    std::vector<std::uint8_t>().swap(searchMatches_);
    searchMatchesEpoch_ = 0;
    ++searchSerial_; // drops any evaluation still in flight
    searchInFlightSerial_ = 0;
    searchResetScrollPending_ = false;
    {
        std::lock_guard lock(searchMutex_);
        searchEvalJob_.reset();
        searchHydrateQueue_.clear(); // stop reading files nobody is searching for
    }
    // Facts already hydrated stay in the store; unprocessed items may be queued again.
    std::vector<std::uint8_t>().swap(searchHydrateQueued_);
    searchHydrateCheckedCount_ = 0;
    searchHydrateNeedCapture_ = false;
    searchHydrateNeedComments_ = false;
    searchHydrateDone_.store(0, std::memory_order_release);
    searchHydrateTotal_.store(0, std::memory_order_release);

    RebuildVisiblePhotos();
    if (searchSavedScroll_) {
        // Restore the pre-search view. When the user moved focus to a result, keep that
        // photo on screen instead (restored scroll, then reveal the focused card).
        const auto layout = CurrentThumbnailLayout();
        reviewState_.SetThumbnailMaximumScroll(layout.maximumScroll);
        float scroll = std::clamp(*searchSavedScroll_, 0.0f, layout.maximumScroll);
        const std::wstring& focus = reviewState_.ThumbnailFocusPath();
        if (!focus.empty() && focus != searchSavedFocus_) {
            if (const auto index = IndexForPath(focus))
                scroll = quicksift::review::thumbnail::ScrollToRevealThumbnail(layout, *index, scroll);
        }
        reviewState_.SetThumbnailScroll(scroll);
        thumbnailPrefetchPlanner_.Reset(reviewState_.Thumbnails().scrollDip);
    }
    searchSavedScroll_.reset();
    searchSavedFocus_.clear();
    LayoutSearchField(); // the count slot disappears
    ArmThumbnailReconcile(120);
    InvalidateCanvas();
}

void QuickSiftApplicationImpl::RequestSearchEvaluation(bool resetScroll) {
    if (!searchActive_) return;
    searchResetScrollPending_ = searchResetScrollPending_ || resetScroll;
    const std::uint64_t serial = ++searchSerial_;
    if (catalog_.PhotoCount() <= kInlineSearchLimit) {
        // Cheap pass over the in-memory store (no disk I/O).
        std::vector<std::uint8_t> matches(catalog_.PhotoCount(), 0);
        for (std::size_t index = 0; index < catalog_.PhotoCount(); ++index) {
            matches[index] = pred::MatchesAll(searchQuery_.clauses,
                quicksift::core::FactsOf(catalog_.PhotoAt(index))) ? 1 : 0;
        }
        ApplySearchResult(serial, catalog_.MetadataEpoch(), std::move(matches));
        return;
    }
    if (!StartSearchWorker()) {
        ApplySearchResult(serial, catalog_.MetadataEpoch(),
            EvaluateSnapshot(*catalog_.Snapshot(), searchQuery_.clauses));
        return;
    }
    SearchEvalJob job;
    job.serial = serial;
    job.snapshot = catalog_.Snapshot();
    job.clauses = searchQuery_.clauses;
    {
        std::lock_guard lock(searchMutex_);
        searchEvalJob_ = std::move(job); // replaces an older pending request
    }
    searchInFlightSerial_ = serial;
    searchCv_.notify_one();
}

void QuickSiftApplicationImpl::ApplySearchResult(std::uint64_t serial, std::uint64_t epoch,
    std::vector<std::uint8_t> matches) {
    if (serial != searchSerial_ || !searchActive_) return; // stale
    searchInFlightSerial_ = 0;
    searchMatches_ = std::move(matches);
    searchMatchesEpoch_ = epoch;
    RebuildVisiblePhotos();
    if (searchResetScrollPending_) {
        // A new query starts at the top; re-evaluations of the same query keep the view.
        searchResetScrollPending_ = false;
        reviewState_.SetThumbnailScroll(0.0f);
        thumbnailPrefetchPlanner_.Reset(0.0f);
    }
    ArmThumbnailReconcile(120);
    InvalidateCanvas();
}

bool QuickSiftApplicationImpl::SearchAllows(const PhotoItem& item) const {
    if (!searchActive_) return true;
    const auto photos = catalog_.Photos();
    const std::less<const PhotoItem*> before{};
    if (!photos.empty() && !before(&item, photos.data()) && before(&item, photos.data() + photos.size())) {
        const std::size_t index = static_cast<std::size_t>(&item - photos.data());
        if (index < searchMatches_.size()) return searchMatches_[index] != 0;
    }
    // Not covered by the last evaluation (new scan batch / prospective record):
    // evaluate the same clauses directly against the in-memory record.
    return pred::MatchesAll(searchQuery_.clauses, quicksift::core::FactsOf(item));
}

void QuickSiftApplicationImpl::OnSearchFolderChanged() {
    std::vector<std::uint8_t>().swap(searchMatches_);
    searchMatchesEpoch_ = 0;
    ++searchSerial_;
    searchInFlightSerial_ = 0;
    std::vector<std::uint8_t>().swap(searchHydrateQueued_);
    searchHydrateCheckedCount_ = 0;
    searchHydrateNeedCapture_ = false;
    searchHydrateNeedComments_ = false;
    searchHydrateGeneration_ = generation_;
    searchHydrateDone_.store(0, std::memory_order_release);
    searchHydrateTotal_.store(0, std::memory_order_release);
    searchSavedScroll_.reset();
    searchSavedFocus_.clear();
    std::lock_guard lock(searchMutex_);
    searchEvalJob_.reset();
    searchHydrateQueue_.clear();
    searchFactUpdates_.clear();
    searchEvalResult_.reset();
    searchWorkerGeneration_ = generation_;
}

// ---- Fact hydration (capture time, comments) ----------------------------------------------------

void QuickSiftApplicationImpl::EnsureSearchFactsHydrated() {
    if (!searchActive_) return;
    const bool needCapture = pred::NeedsCaptureTime(searchQuery_.clauses);
    const bool needComments = pred::NeedsComments(searchQuery_.clauses);
    const bool needCamera = pred::NeedsCameraSettings(searchQuery_.clauses);
    if (searchHydrateGeneration_ != generation_) {
        std::vector<std::uint8_t>().swap(searchHydrateQueued_);
        searchHydrateCheckedCount_ = 0;
        searchHydrateNeedCapture_ = false;
        searchHydrateNeedComments_ = false;
        searchHydrateNeedCamera_ = false;
        searchHydrateGeneration_ = generation_;
    }
    if (!needCapture && !needComments && !needCamera) {
        searchHydrateCheckedCount_ = catalog_.PhotoCount();
        return;
    }
    const bool newNeeds = (needCapture && !searchHydrateNeedCapture_) ||
        (needComments && !searchHydrateNeedComments_) ||
        (needCamera && !searchHydrateNeedCamera_);
    const std::size_t count = catalog_.PhotoCount();
    if (!newNeeds && searchHydrateCheckedCount_ == count) return;
    const std::size_t start = newNeeds ? 0 : std::min(searchHydrateCheckedCount_, count);
    searchHydrateNeedCapture_ = searchHydrateNeedCapture_ || needCapture;
    searchHydrateNeedComments_ = searchHydrateNeedComments_ || needComments;
    searchHydrateNeedCamera_ = searchHydrateNeedCamera_ || needCamera;
    searchHydrateQueued_.resize(count, 0);
    std::vector<SearchHydrateItem> items;
    for (std::size_t index = start; index < count; ++index) {
        const PhotoItem& photo = catalog_.PhotoAt(index);
        std::uint8_t& queued = searchHydrateQueued_[index];
        SearchHydrateItem item;
        item.index = index;
        item.capture = searchHydrateNeedCapture_ && (queued & kQueuedCapture) == 0 &&
            photo.captureTimeSource == pred::CaptureTimeSource::Unknown;
        item.comment = searchHydrateNeedComments_ && (queued & kQueuedComment) == 0 && !photo.userCommentKnown;
        item.camera = searchHydrateNeedCamera_ && (queued & kQueuedCamera) == 0 && !photo.cameraSettingsKnown;
        if (!item.capture && !item.comment && !item.camera) continue;
        if (item.capture) queued |= kQueuedCapture;
        if (item.comment) queued |= kQueuedComment;
        if (item.camera) queued |= kQueuedCamera;
        item.path = photo.path;
        items.push_back(std::move(item));
    }
    searchHydrateCheckedCount_ = count;
    if (items.empty()) return;
    // Visible (filter-passing) photos first so the current grid resolves soonest.
    std::stable_partition(items.begin(), items.end(), [this](const SearchHydrateItem& item) {
        return catalog_.VisiblePosition(item.index) != quicksift::core::CatalogStore::InvalidVisiblePosition ||
            FilterAllowed(catalog_.PhotoAt(item.index));
    });
    if (!StartSearchWorker()) return;
    searchHydrateTotal_.fetch_add(items.size(), std::memory_order_acq_rel);
    {
        std::lock_guard lock(searchMutex_);
        if (searchWorkerGeneration_ != generation_) {
            searchHydrateQueue_.clear();
            searchWorkerGeneration_ = generation_;
        }
        for (SearchHydrateItem& item : items) searchHydrateQueue_.push_back(std::move(item));
    }
    searchCv_.notify_one();
}

void QuickSiftApplicationImpl::ApplySearchFactUpdates() {
    std::vector<SearchFactUpdate> updates;
    std::uint64_t generation = 0;
    {
        std::lock_guard lock(searchMutex_);
        updates.swap(searchFactUpdates_);
        generation = searchFactGeneration_;
    }
    if (updates.empty() || generation != generation_) return;
    for (const SearchFactUpdate& update : updates) {
        std::size_t index = update.index;
        if (index >= catalog_.PhotoCount() || catalog_.PhotoAt(index).path != update.path) {
            const auto found = catalog_.FindPath(update.path.wstring());
            if (!found) continue;
            index = *found;
        }
        // The store is the single authority: fill only facts it does not know yet
        // (a comment saved by the user meanwhile wins over the hydrated one).
        catalog_.EditPhoto(index, [&update](PhotoItem& photo) {
            if (update.hasCapture && photo.captureTimeSource == pred::CaptureTimeSource::Unknown) {
                photo.captureTime = update.capture;
                photo.captureTimeSource = update.source;
            }
            if (update.hasComment && !photo.userCommentKnown) {
                photo.userComment = update.comment;
                photo.userCommentKnown = true;
            }
            if (update.hasCamera && !photo.cameraSettingsKnown) {
                photo.iso = update.iso;
                photo.aperture = update.aperture;
                photo.focalLength = update.focalLength;
                photo.shutterSpeed = update.shutterSpeed;
                photo.camera = update.camera;
                photo.lens = update.lens;
                photo.cameraSettingsKnown = update.cameraKnown;
            }
        });
    }
}

void QuickSiftApplicationImpl::OnSearchWorkerMessage() {
    searchMessagePosted_.store(false, std::memory_order_release);
    searchUnconsumed_.store(false, std::memory_order_release);
    ApplySearchFactUpdates();
    std::optional<SearchEvalResult> result;
    {
        std::lock_guard lock(searchMutex_);
        result.swap(searchEvalResult_);
    }
    if (result) ApplySearchResult(result->serial, result->epoch, std::move(result->matches));
    InvalidateCanvas();
}

// ---- Worker --------------------------------------------------------------------------------------

bool QuickSiftApplicationImpl::StartSearchWorker() {
    if (searchWorker_.joinable()) return true;
    {
        std::lock_guard lock(searchMutex_);
        searchWorkerStop_ = false;
        searchWorkerGeneration_ = generation_;
    }
    try {
        searchWorker_ = std::thread([this]() noexcept { SearchWorkerLoop(); });
    } catch (...) {
        return false;
    }
    return true;
}

void QuickSiftApplicationImpl::StopSearchWorker() noexcept {
    try {
        {
            std::lock_guard lock(searchMutex_);
            searchWorkerStop_ = true;
            searchEvalJob_.reset();
            searchHydrateQueue_.clear();
        }
        searchCv_.notify_all();
        if (searchWorker_.joinable()) searchWorker_.join();
    } catch (...) {
    }
}

void QuickSiftApplicationImpl::SearchWorkerLoop() noexcept {
    const HWND window = hwnd_;
    const auto post = [this, window]() noexcept {
        searchUnconsumed_.store(true, std::memory_order_release);
        if (!searchMessagePosted_.exchange(true, std::memory_order_acq_rel) && window) {
            // 0.9.4.0 polish: a failed post (queue full / window closing) used to
            // leave the flag set forever, so no later batch could wake the UI and
            // search stalled. Reset it; the animation tick also polls.
            if (!PostMessageW(window, WM_APP_SEARCH_WORKER, 0, 0))
                searchMessagePosted_.store(false, std::memory_order_release);
        }
    };
    namespace prio = quicksift::core::priority;
    for (;;) {
        std::optional<SearchEvalJob> evaluation;
        std::vector<SearchHydrateItem> batch;
        std::uint64_t generation = 0;
        try {
            std::unique_lock lock(searchMutex_);
            searchCv_.wait(lock, [this] {
                return searchWorkerStop_ || searchEvalJob_.has_value() || !searchHydrateQueue_.empty();
            });
            if (searchWorkerStop_) return;
            if (searchEvalJob_) {
                evaluation = std::move(searchEvalJob_);
                searchEvalJob_.reset();
            } else {
                // P3 yields to P1 writes and P2 view decodes. While Single/Compare
                // is still loading the requested image, do not start indexing
                // file reads. No timeout: the view load is short and must win.
                while (prio::ShouldYield(prio::Tier::SearchRead, prio::Demand().Snapshot())) {
                    searchCv_.wait_for(lock, std::chrono::milliseconds(16), [this] {
                        return searchWorkerStop_ || searchEvalJob_.has_value();
                    });
                    if (searchWorkerStop_ || searchEvalJob_) break;
                }
                if (searchWorkerStop_) return;
                if (searchEvalJob_) continue; // evaluate first, hydrate afterwards
                generation = searchWorkerGeneration_;
                const std::size_t batchLimit = std::min(kHydrateBatch,
                    prio::SearchHydrateBatch(prio::Demand().Snapshot()));
                while (!searchHydrateQueue_.empty() && batch.size() < batchLimit) {
                    batch.push_back(std::move(searchHydrateQueue_.front()));
                    searchHydrateQueue_.pop_front();
                }
            }
        } catch (...) {
            return;
        }

        if (evaluation) {
            // Evaluation pre-empts hydration: it runs between hydration batches.
            SearchEvalResult result;
            result.serial = evaluation->serial;
            result.epoch = evaluation->snapshot ? evaluation->snapshot->epoch : 0;
            try {
                if (evaluation->snapshot) result.matches = EvaluateSnapshot(*evaluation->snapshot, evaluation->clauses);
            } catch (...) {
                result.matches.clear(); // UI falls back to direct per-record evaluation
                try {
                    QS_LOG_EVENT(quicksift::diagnostics::Level::Warning, quicksift::diagnostics::category::kSearch,
                        L"evaluate_failed", { L"error_code", L"worker.exception" },
                        { L"fallback", L"per_record" });
                } catch (...) {}
            }
            try {
                std::lock_guard lock(searchMutex_);
                searchEvalResult_ = std::move(result);
            } catch (...) {
            }
            post();
            continue;
        }

        std::vector<SearchFactUpdate> updates;
        std::vector<SearchHydrateItem> deferred;
        try {
            // A file inside a metadata write (exclusive path gate) is not opened
            // now: the write must not see our handle. Re-queued at the back.
            for (auto it = batch.begin(); it != batch.end();) {
                if (it->capture && worker_.IsPathBlockedForDecoder(it->path)) {
                    deferred.push_back(std::move(*it));
                    it = batch.erase(it);
                } else {
                    ++it;
                }
            }
            std::vector<fs::path> commentPaths;
            for (const SearchHydrateItem& item : batch) if (item.comment) commentPaths.push_back(item.path);
            std::vector<std::wstring> comments;
            if (!commentPaths.empty()) comments = persistentCache_.GetPhotoComments(commentPaths);
            std::size_t commentIndex = 0;
            updates.reserve(batch.size());
            for (const SearchHydrateItem& item : batch) {
                SearchFactUpdate update;
                update.index = item.index;
                update.path = item.path;
                if (item.capture) {
                    if (const auto exifTime = ReadCaptureTime(item.path)) {
                        update.hasCapture = true;
                        update.capture = exifTime->time;
                        update.source = pred::CaptureTimeSource::Exif;
                    } else if (const auto modified = FileModifiedLocalTime(item.path)) {
                        update.hasCapture = true;
                        update.capture = *modified;
                        update.source = pred::CaptureTimeSource::FileModified;
                    }
                }
                if (item.camera) {
                    // Prefer the stamp-independent camera index (no file re-read).
                    bool fromCache = false;
                    if (auto cached = persistentCache_.LookupCameraSettings(item.path)) {
                        update.hasCamera = true;
                        update.iso = cached->iso;
                        update.aperture = cached->aperture;
                        update.focalLength = cached->focalLength;
                        update.shutterSpeed = cached->shutterSpeed;
                        update.camera = std::move(cached->camera);
                        update.lens = std::move(cached->lens);
                        update.cameraKnown = true;
                        fromCache = true;
                    }
                    if (!fromCache) {
                        // Same bounded read used for capture time.
                        FileHandle file(CreateFileW(item.path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
                        if (file.Valid()) {
                            std::vector<std::uint8_t> buffer(exif::kCaptureTimeReadBytes);
                            DWORD read = 0;
                            if (ReadFile(file.Get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) && read > 0) {
                                buffer.resize(read);
                                const auto settings = exif::FindCameraSettings(buffer);
                                update.hasCamera = true;
                                update.iso = settings.iso;
                                update.aperture = settings.aperture;
                                update.focalLength = settings.focalLength;
                                update.shutterSpeed = settings.shutterSpeed;
                                update.camera = settings.camera;
                                update.lens = settings.lens;
                                update.cameraKnown = settings.known;
                                // Persist only camera columns; never overwrite rating/faces.
                                CachedImageRecord rec;
                                rec.iso = settings.iso;
                                rec.aperture = settings.aperture;
                                rec.focalLength = settings.focalLength;
                                rec.shutterSpeed = settings.shutterSpeed;
                                rec.camera = settings.camera;
                                rec.lens = settings.lens;
                                rec.cameraSettingsKnown = settings.known;
                                persistentCache_.UpdateCameraSettings(item.path, rec);
                            }
                        }
                    }
                }
                if (item.comment) {
                    update.hasComment = true;
                    if (commentIndex < comments.size()) update.comment = std::move(comments[commentIndex]);
                    ++commentIndex;
                }
                updates.push_back(std::move(update));
            }
        } catch (...) {
            static quicksift::core::LogRateLimiter limiter(3, std::chrono::seconds(30));
            std::uint64_t suppressed = 0;
            if (limiter.Allow(&suppressed)) {
                try { // the worker entry point is noexcept: logging must not throw out
                    QS_LOG_EVENT(quicksift::diagnostics::Level::Warning, quicksift::diagnostics::category::kSearch,
                        L"hydrate_failed", { L"error_code", L"search.read_failed" },
                        { L"batch", std::to_wstring(batch.size()) },
                        { L"suppressed", std::to_wstring(suppressed) });
                } catch (...) {}
            }
        }
        try {
            std::lock_guard lock(searchMutex_);
            if (generation == searchWorkerGeneration_ && !updates.empty()) {
                if (searchFactGeneration_ != generation) searchFactUpdates_.clear();
                searchFactGeneration_ = generation;
                for (SearchFactUpdate& update : updates) searchFactUpdates_.push_back(std::move(update));
            }
            if (generation == searchWorkerGeneration_)
                for (SearchHydrateItem& item : deferred) searchHydrateQueue_.push_back(std::move(item));
        } catch (...) {
        }
        searchHydrateDone_.fetch_add(batch.size(), std::memory_order_acq_rel);
        if (!deferred.empty() && batch.empty()) {
            // Only gated files left: back off instead of spinning on them.
            std::unique_lock lock(searchMutex_);
            searchCv_.wait_for(lock, std::chrono::milliseconds(50), [this] {
                return searchWorkerStop_ || searchEvalJob_.has_value();
            });
        }
        post();
    }
}

} // namespace quicksift::app
