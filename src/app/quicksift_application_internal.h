// OWNER: Private application composition root and feature-module declaration surface.
#pragma once
#include "app/application_support.h"
#include "core/selection_store.h"
#include "core/face_detection_policy.h"
#include "review/thumbnail_layout.h"
#include "review/review_state_model.h"
#include "review/review_async_policy.h"
#include "work/background_work_engine.h"
#include "work/folder_scanner.h"
#include "transactions/transaction_coordinator.h"

#include <atomic>
#include <thread>

namespace quicksift::app {

class QuickSiftApplicationImpl {
public:
    QuickSiftApplicationImpl();
    ~QuickSiftApplicationImpl();
    QuickSiftApplicationImpl(const QuickSiftApplicationImpl&) = delete;
    QuickSiftApplicationImpl& operator=(const QuickSiftApplicationImpl&) = delete;
    int Run(HINSTANCE instance, int showCommand);
    void StartDeferredStartup();
    void HandleDeferredStartupReady(bool success);

private:

    // All visual roles come from one design-system implementation.
    ThemePalette Palette() const noexcept;

    std::wstring Tr(std::wstring_view english) const;

    std::wstring ThemeToggleText() const;

    std::wstring FullscreenButtonText() const;

    ThemePalette CurrentDocumentTheme() const noexcept;

    void ApplyDocumentTheme(HWND window, DocumentWindowState* state);

    void ShowDocumentWindow(const std::wstring& title, const std::wstring& body, DocumentKind kind);

    void ShowHelp();

    void ShowAbout();

    void ShowDiagnosticLog();

    void RefreshOpenDocumentWindows();

    static bool HasFaceFocusWarning(const PhotoItem& photo) noexcept;

    std::wstring FaceWarningTooltipText() const;

    void RefreshFaceWarningTooltipText();

    void CreateFaceWarningTooltip();

    void HideFaceWarningTooltip();

    void UpdateFaceWarningTooltip(float x, float y);
    std::wstring FaceLockAvailabilityTooltipText() const;
    void CreateFaceLockAvailabilityTooltip();
    void RefreshFaceLockAvailabilityUi();
    void RefreshFaceDetectorCapability(bool force = false);

    void RefreshLocalizedText();

    static D2D1_COLOR_F ToD2D(COLORREF color, float alpha = 1.0f) noexcept;

    void InitializeColorBlobs();

    void ApplyTheme();
// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Main-window creation/message routing/title bar/fullscreen/DPI and checked UI timers.

// Main-window shell: class registration, creation, custom title bar, fullscreen,
// tree scrollbar, Win32 message routing, DPI conversion, and interaction timing.

    bool StartUiTimer(UINT_PTR timerId, UINT intervalMilliseconds, std::wstring_view purpose);

    bool HandleGlobalKey(const MSG& message);

    bool RegisterClasses();

    bool PrepareSavedWindowPlacement();

    bool CreateMainWindow(int showCommand);

    std::array<int, kTitleButtons.size()> MeasuredTitleButtonWidthsPx() const;

    int TitleBarRowCountForWidth(int width) const;

    int NominalTitleBarHeightPxForWidth(int width) const;

    int TitleBarHeightPxForWidth(int width) const;

    int CurrentTitleBarHeightPx() const;

    RECT TitleBarRect() const;

    RECT TrafficLightRect(TrafficLight light) const;

    TrafficLight HitTrafficLight(POINT point) const;

    bool IsBrowseControlId(int id) const noexcept;

    bool IsPointOverTitleControl(POINT clientPoint) const;

    void InvalidateTitleBar();

    void UpdateTrafficHover(POINT point);

    void PerformTrafficAction(TrafficLight light);

    void ToggleMaximizeRestore();

    void RelayoutWindowChrome();

    void SetFullscreenTitleVisible(bool visible);

    void EnterFullscreen();

    void ExitFullscreen();

    void ToggleFullscreen();

    void UpdateFullscreenTitleFromCursor();

    void ShowWindowSystemMenu(POINT screenPoint);

    int ResizeBorderThicknessPx() const;

    LRESULT HitTestMainWindow(LPARAM lParam);

    void DrawMacTitleBar(HDC dc, const RECT& client);

    VisibleTreeMetrics MeasureVisibleTree(HWND tree) const;

    HTREEITEM VisibleTreeItemAt(HWND tree, int requestedIndex) const;

    RECT TreeScrollbarThumbRect(HWND tree, const VisibleTreeMetrics* knownMetrics = nullptr) const;

    void DrawTreeOverlayScrollbar(HWND tree);

    void DragTreeScrollbar(int mouseY);

    static void ReportUiCallbackException(HWND hwnd, UINT message, std::wstring_view callbackName) noexcept;

    static LRESULT CALLBACK TitleOverlaySubclassProcImpl(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData);

    static LRESULT CALLBACK TreeSubclassProcImpl(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData);

    static LRESULT CALLBACK MainWndProcImpl(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    static LRESULT CALLBACK CanvasWndProcImpl(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    static LRESULT CALLBACK TitleOverlaySubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData);

    static LRESULT CALLBACK TreeSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData);
    static LRESULT CALLBACK FlyoutBackdropSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData);
    static LRESULT CALLBACK LibraryListSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData);

    static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    static LRESULT CALLBACK CanvasWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT HandleMainMessage(UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT HandleCanvasMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    int DipToPx(int dip) const;

    float CanvasDpi() const;

    float CanvasPixelScale() const;

    D2D1_POINT_2F CanvasPixelsToDips(int x, int y) const;

    D2D1_SIZE_F CanvasSizeInDips() const;

    void MarkInteraction();

    bool IsActivelyInteracting() const;

    void ApplyUiFontsToControls(HFONT normal, HFONT semibold);

    void DestroyUiFonts();

    void RecreateUiFonts();

    void ConfigureDwmGlass();
// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Required child controls, layout, left hover pane, fonts, and owner-drawn control painting.

// Main-window controls: child creation, DPI/font lifecycle, left-pane layout,
// owner-drawn controls, title/status/info surfaces, and themed painting.

    bool CreateMainWindowButton(int id, std::wstring_view captionKey);

    LRESULT OnCreate();

    void OnDpiChanged(UINT newDpi, const RECT* suggested);

    void DrawBackgroundBlobs(HDC dc, const RECT& client);

    bool IsCullControlId(int id) const noexcept;

    bool IsFileControlId(int id) const noexcept;

    bool IsSettingsControlId(int id) const noexcept;

    bool IsFilterControlId(int id) const noexcept;

    int FlyoutTabHeightPx(FlyoutPanel panel) const;

    // Deterministic left-pane width for a given client width (scales with window).
    int LeftPaneWidthPx(int clientWidth) const;

    RECT LeftPaneHitStripRect(int clientHeight) const;

    RECT LeftPaneRect(int width, int height) const;

    // Full open-pane geometry (ignores open blend). Used for tab/content layout
    // once the reveal threshold is crossed so children are not cramped mid-animation.
    RECT LeftPaneOpenRect(int width, int height) const;

    RECT LeftPaneContentRect(int width, int height) const;

    RECT FlyoutTabRect(FlyoutPanel panel, int clientWidth, int clientHeight) const;

    FlyoutPanel FlyoutTabAtPoint(POINT point, int clientWidth, int clientHeight) const;

    // Content rectangle for the selected section inside the open left pane.
    RECT FlyoutRect(FlyoutPanel panel, int width, int height) const;

    RECT CanvasLayoutRect(int width, int height) const;

    std::wstring FlyoutLabel(FlyoutPanel panel) const;

    void SetActiveFlyout(FlyoutPanel panel);

    // Reposition the left-pane backdrop for the current open blend (width animation).
    // Avoids a full LayoutControls pass on every animation tick.
    void LayoutLeftPaneAnimationFrame();

    // Keep backdrop under pane children (siblings). Raising the backdrop to HWND_TOP
    // every animation frame was burying tabs/buttons until hover invalidated them.
    void PlaceLeftPaneBackdrop(const RECT& pane, bool show);

    // Raise every currently-visible left-pane child above the backdrop without
    // ShowWindow churn (show/hide only happens in LayoutControls on open/close).
    void RaiseLeftPaneContentAboveBackdrop();

    // SW_HIDE every flyout section child (all tabs' content). LayoutControls then
    // shows only the active section when LeftPaneShouldShowContent() is true.
    void HideAllLeftPaneSectionContent();

    // Opaque-erase + paint the flyout backdrop immediately so a tab switch cannot
    // leave prior section pixels stacked in the shared content rect.
    void EraseLeftPaneBackdropFully();

    [[nodiscard]] bool LeftPaneShouldShowContent() const noexcept;

    void UpdateAutoHideFromCursor();

    FlyoutPanel PanelForFlyoutTabWindow(HWND window) const;

    void DrawFlyoutTab(DRAWITEMSTRUCT* draw, FlyoutPanel panel);

    void DrawFlyoutBackdrop(DRAWITEMSTRUCT* draw);

    void RedrawActiveFlyoutContents();

    void DrawActiveFlyoutPanel(HDC dc, const RECT& client);

    void PaintMainWindow();

    void RaiseFullscreenTitleOverlay();

    void LayoutTitleControls(int width);

    void LayoutControls(int width, int height);

    bool IsModeButtonActive(int id) const;

    std::wstring ActiveInfoPath() const;

    void RequestExifForActive();

    int ActiveRatingForUi() const;

    int ActivePickStateForUi() const;

    LRESULT OnDrawItem(DRAWITEMSTRUCT* draw);

    bool ButtonHasPopupMenu(int id) const;
    int FlyoutButtonWidthPx(HDC measureDc, int id, int fallbackWidthDip) const;

    void DrawGlassButton(DRAWITEMSTRUCT* draw);

    void DrawFilterLabel(DRAWITEMSTRUCT* draw);

    void DrawExifPanel(DRAWITEMSTRUCT* draw);

    void DrawGlassStatus(DRAWITEMSTRUCT* draw);

// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Popup menus, command dispatch, folder tree, visible-catalog rebuild, and status text.

// UI command layer: custom popup menus, command dispatch, notifications, folder
// tree population, visible-catalog rebuilding, status text, and invalidation.

    int TrackDropdownMenu(HWND anchor, std::vector<MenuItem> items);

    void OnCommand(int id, int notifyCode, HWND source);
    void RefreshUserComment();
    void SaveUserComment();

    LRESULT OnNotify(NMHDR* header);

    void ChooseFolder();

    void RefreshLibraryList();

    bool CullAllowed(const PhotoItem& item) const;

    std::optional<size_t> CatalogIndexForPath(std::wstring_view path) const;

    const PhotoItem& VisiblePhoto(size_t visibleIndex) const;

    bool IsPathVisible(const std::wstring& path) const;

    void RebuildVisiblePhotos();

    std::wstring SingleStatusDetail() const;

    std::wstring FileOperationStatusLine() const;

    void UpdateStatus();

    void InvalidateCanvas();
// CODE GUIDE: See CODE_GUIDE.md -> "Opening a folder".
// OWNER: Catalog-load lifecycle and stale-safe worker-result admission on the UI thread.

// Catalog controller: folder loading, scanner batches, result integration,
// metadata queueing, ordering, and visible-position validation.

    void LoadFolder(const fs::path& folder);

    void PumpMetadataQueue();

    void OnScanBatch(const std::shared_ptr<ScanBatch>& batch);

    void OnScanComplete(uint64_t generation,
        quicksift::work::ScanCompletionStatus status, std::wstring detail,
        std::uint32_t nativeError);

    bool PhotoSortLess(const PhotoItem& a, const PhotoItem& b) const;

    void RestorePhotoOrderState(const std::wstring& current, const std::wstring& focus);

    void SortPhotos();

#ifndef NDEBUG
    void ValidateVisibleCatalog() const;

#endif

    void DrainBackgroundCompletions();
    void DrainPostedWorkerMessages();

    void CompleteMetadataJob();

    void OnMetadataJobAborted(uint64_t generation);

    int CalculateAdaptiveTimeoutMs(const std::wstring& path, int attempt) const;

    void OnWorkReady(const std::shared_ptr<WorkResult>& result);
// CODE GUIDE: See CODE_GUIDE.md -> "Scrolling thumbnails".
// OWNER: Graphics devices and bitmap-cache admission/eviction; UI thread owns device resources.

// Graphics/cache controller: Direct2D/Direct3D resources, bitmap admission and
// eviction, GPU pressure handling, WIC image sources, and view-resource retention.

    bool CreateSwapChainTarget();

    bool EnsureModernDeviceResources();

    void ResizeCanvasTarget(UINT width, UINT height);

    void ResetModernDeviceResources();

    void EnsureDeviceResources();

    void ClearWicImageSources();

    void DiscardDeviceResources();

    void EnsureGpuBudgetDevice();

    void UpdateGpuBudget();

    void OfferGraphicsResources(bool release);

    void ConfigureCacheBudgets();

    void AddCacheBytes(CacheClass cacheClass, size_t bytes);

    void RemoveCacheBytes(CacheClass cacheClass, size_t bytes);
    void UnindexBitmapSize(const CacheKey& key);

    size_t TotalCacheBytes() const;

    size_t FrequencyHash(const CacheKey& key) const;

    std::uint8_t CacheFrequency(const CacheKey& key) const;

    void RecordCacheFrequency(const CacheKey& key);

    void TouchBitmapEntry(const CacheKey& key, BitmapEntry& entry);

    bool ShouldAdmitBitmap(const CacheKey& key, const WorkResult& result, size_t additionalBytes);

    void PruneSupersededViewBitmaps(const std::wstring& path);

    std::chrono::milliseconds ViewResourceRetention() const;

    std::chrono::milliseconds RecentViewGracePeriod() const;

    size_t RetainedWicImageSourceLimit() const;

    bool PathHasDetectedFace(const std::wstring& path) const;

    bool MemoryPressureActive() const;

    std::chrono::milliseconds ViewResourceRetentionForPath(
        const std::wstring& path, bool memoryPressure) const;

    std::unordered_set<std::wstring> ViewResourceKeepSet() const;

    void TrimInactiveViewResources(bool force = false);

    void ScheduleViewResourceTrim();

    void ClearBitmapCache();

    bool IsProtectedBitmap(const CacheKey& key) const;

    void EvictBitmapCache(bool forceProtected = false,
        std::optional<CacheClass> incomingClass = std::nullopt, size_t incomingBytes = 0);

    void TrimCachesForMemoryPressure(bool forced = false);

    void OnResourcePressure(quicksift::ResourcePressureEvent event);

    int BucketFor(int desired) const;

    int IntermediateViewDecodeTarget(int baseDesired, int finalDesired) const;

    int ThumbnailDecodeTarget() const;

    static int BitmapMaximumEdge(const BitmapEntry* bitmap);

    static bool BitmapSatisfiesDisplay(const BitmapEntry* bitmap, int desiredEdge,
        bool requireFullQuality);

    quicksift::review::thumbnail::ThumbnailLayoutMetrics CurrentThumbnailLayout() const;

    D2D1_RECT_F ThumbnailRectForIndex(size_t index) const;

    void PreserveThumbnailAnchorForSizeChange(int newSizeIndex, int newSize);

    BitmapEntry* FindBitmap(const fs::path& path, int desired, CacheClass preferredClass);
    BitmapEntry* FindBitmapNoTouch(const fs::path& path, int desired, CacheClass preferredClass) const;

    BitmapEntry* FindBestViewBitmap(const fs::path& path, int desiredEdge,
        CacheClass preferredClass);

    bool SupportsModernWicImageSource(const fs::path& path) const;

    WicImageSourceEntry* GetOrCreateWicImageSource(const fs::path& path);
    ComPtr<ID2D1Bitmap> GetOrCreateShellFallback(const fs::path& path, int targetSize);

    bool DrawWicImageSourceFit(WicImageSourceEntry* entry, D2D1_RECT_F area,
        const quicksift::review::transform::ResolvedView& view, bool clip = true);

    BitmapEntry* GetOrRequestBitmap(const fs::path& path, int desired, JobPriority priority, bool requestFull,
        CacheClass cacheClass = CacheClass::Preview, uint64_t viewportEpoch = 0, bool ignoreThumbnailViewport = false);
    BitmapEntry* GetOrRequestIntermediateViewBitmap(const fs::path& path, int desired,
        JobPriority priority);
// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Canvas drawing and hit testing only; policy belongs in core/navigation modules.

// Canvas renderer: thumbnails, single/compare views, overlays, alerts, badges,
// hit testing, and per-photo view-state calculations.

    void Render();

    void ShowToast(const std::wstring& messageKey);

    void DrawToastOverlay();

    void ShowGlassAlert(const std::wstring& title, const std::wstring& message,
        GlassAlertKind kind = GlassAlertKind::Warning);

    void ShowGlassConfirmation(const std::wstring& title, const std::wstring& message,
        const std::wstring& confirmText, PendingGlassAction action,
        GlassAlertKind kind = GlassAlertKind::Warning,
        const std::wstring& secondaryText = L"Cancel");

    void ResolveGlassAlert(bool accepted);

    void DismissGlassAlert();

    void ConfirmGlassAlert();

    bool HandleGlassAlertClick(float x, float y);

    void UpdateGlassAlertHover(float x, float y);

    void DrawGlassAlertOverlay();

    void DrawCanvasBackground();
    void DrawPerformanceHudOverlay();
    D2D1_RECT_F ThumbnailScrollbarThumbRect() const;
    void DrawThumbnailScrollbarOverlay();
    void UpdatePerformanceHudTimer();
    void LogPerformanceSummary();

    void DrawCenteredText(const std::wstring& text, D2D1_RECT_F rect, IDWriteTextFormat* format, ID2D1Brush* brush);

    void RenderThumbnails(const D2D1_RECT_F& bounds);

    std::wstring Stars(int rating) const;
    std::wstring RatingStateText(const PhotoItem& photo) const;

    std::wstring CullStateText(const PhotoItem& photo) const;

    void RenderSingle(const D2D1_RECT_F& bounds);

    float AspectForPath(const std::wstring& path) const;

    std::vector<D2D1_RECT_F> BuildBestCompareLayout(const D2D1_RECT_F& bounds) const;

    void RenderCompare(const D2D1_RECT_F& bounds);

    std::wstring FormatBadgeText(const PhotoItem& photo) const;

    void DrawThumbnailBadges(const PhotoItem& photo, D2D1_RECT_F area);

    void DrawBitmapFit(BitmapEntry* bitmap, D2D1_RECT_F area,
        const quicksift::review::transform::ResolvedView& view, bool clip);

    void RenderVisibleTiles(const PhotoItem& photo, const D2D1_RECT_F& area,
        const quicksift::review::transform::ResolvedView& view, const BitmapEntry* baseBitmap);

    std::optional<size_t> IndexForPath(const std::wstring& path) const;

    std::optional<size_t> HitThumbnail(float x, float y) const;

    std::optional<size_t> HitCompareSlot(float x, float y) const;


    std::wstring EditableViewPath() const;

    ViewState BaseViewForPath(const std::wstring& path) const;

    D2D1_RECT_F ViewAreaForPath(const std::wstring& path) const;

    quicksift::review::transform::ViewGeometry ViewGeometryForPath(const std::wstring& path,
        D2D1_RECT_F area, int fallbackWidth = 0, int fallbackHeight = 0) const;

    quicksift::review::transform::ResolvedView ResolveViewForPath(const std::wstring& path,
        D2D1_RECT_F area, const ViewState& state, int fallbackWidth = 0,
        int fallbackHeight = 0) const;

    std::optional<D2D1_POINT_2F> PrimaryFaceCenter(const std::wstring& path) const;

    D2D1_POINT_2F FaceOffsetForPath(const std::wstring& path) const;

    void SetFaceOffsetForPath(const std::wstring& path, D2D1_POINT_2F offset);

    void ResetFaceOffsetForPath(const std::wstring& path);

    bool IsBeyondFitZoom(const std::wstring& path, D2D1_RECT_F area,
        const ViewState& state) const;

    ViewState ViewForPath(const std::wstring& path, D2D1_RECT_F area) const;

    ViewState ViewForPath(const std::wstring& path) const;

    ViewState EditableViewState() const;

    void StoreEditableViewState(const ViewState& visibleState);

    void RecenterFaceLockForPath(const std::wstring& path, bool requestIfMissing = true);

    void DisableFaceLockPreservingViews();

    std::uint64_t AdvanceReviewNavigationEpoch();
    std::uint64_t PublishThumbnailViewport(std::uint64_t signature,
        const std::vector<std::wstring>& desiredPaths);
    struct FilmstripLayout {
        float width = 130.0f;
        float thumbSize = 100.0f;
        float padding = 10.0f;
        float top = 16.0f;
        int maxThumbs = 0;
        size_t firstIndex = 0;
        size_t lastIndex = 0; // exclusive
    };
    FilmstripLayout CurrentFilmstripLayout(const D2D1_RECT_F& bounds) const;
    bool FilmstripContains(float x, float y) const;
    std::optional<size_t> HitFilmstrip(float x, float y) const;
    bool NavigateToSingleIndex(size_t index, int direction, bool resetView);
    void PrefetchFilmstrip(const FilmstripLayout& layout);
    void RenderFilmstrip(const D2D1_RECT_F& bounds);
    void ToggleFilmstrip();
    void UpdateAnimations();
// CODE GUIDE: See CODE_GUIDE.md -> "Scrolling thumbnails".
// OWNER: Translate viewport plans into ordered decode requests; do not duplicate prediction math.

// Viewport prefetch controller: converts current view, scroll direction, device
// pressure, and storage profile into ordered visible/predictive work requests.

    AdaptivePrefetchPolicy CurrentPrefetchPolicy();

    void UpdateAppState();
    void RefreshPerformancePolicy(bool forceUpdate = false);
    void ApplyDynamicCacheBudgets(size_t targetBudget);
    void ReconcileEvictedThumbnails();
    void DispatchIdlePrefetch();
    void HandleNavigationEvent(int stepDirection);

    void ScheduleVisibleWork();

    // Virtualized thumbnail scroll: coalesce input to one frame; publish/cancel on
    // visible-row jumps; settle-debounce before decode enqueue.
    void RequestThumbnailScrollFrame();
    void ProcessThumbnailScrollFrame();
    void ArmThumbnailScrollSettle();
    void CancelThumbnailScrollSettle();
    void PublishThumbnailViewportForCurrentPlan(bool enqueueDecodes);

    void SchedulePredictivePrefetch(int direction);
    // Velocity + thumbnail-size scroll-load policy for Thumbnail View.
    [[nodiscard]] quicksift::review::prefetch::ScrollLoadBudget CurrentThumbnailScrollLoadBudget() const noexcept;
    void ApplyThumbnailScrollLoadBudget(const quicksift::review::prefetch::ScrollLoadBudget& budget);
    void ClearThumbnailScrollMotionAdmission();
    [[nodiscard]] bool ThumbnailScrollPrefersPlaceholders() const noexcept;
// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Navigation, zoom/pan/rotation, selection, modes, and session persistence.

// Navigation/session controller: face analysis scheduling, zoom/pan/rotation, input,
// selection, mode changes, timers, and persistent user session state.

    static std::uint64_t FileTimeValue(const FILETIME& time);

    bool CpuHasBackgroundHeadroom();

    bool PathHasResidentPixels(const std::wstring& path) const;

    void ScheduleBackgroundFaceAnalysis();
    bool EnqueueFaceAnalysis(const fs::path& path, uint64_t epoch, JobPriority priority,
        int targetSize = kFaceDecodeSize, bool explicitRequest = false);
    JobPriority FaceAnalysisPriority() const noexcept;
    void HandleFaceAnalysisOutcome(const WorkResult& result);

    const PhotoItem* PhotoForPath(const std::wstring& path) const;

    void ApplyFaceLockForActive(bool requestIfMissing = true);

    std::pair<int, int> ActiveSourceSize() const;

    void SetZoomMode(ZoomMode mode);

    void RotateView(int degrees);

    int ActiveColorLabelForUi() const;

    void UpdateColorLabelButton();
    void UpdateRatingButton();

    void UpdateRatingFilterButton();

    void UpdatePickFilterButton();

    void UpdateColorLabelFilterButton();

    void UpdateZoomModeButton();

    void UpdateFullscreenButton();

    void RelayoutTitleControlsAfterCaptionChange();

    void UpdateToggleButtons();

    void FlushMetadataUi();

    void OnTimer(UINT_PTR timer);

    void LoadSession();

    void SaveSession();

    void RestoreSessionAfterCreate();

    void OnCanvasLeftDown(float x, float y, WPARAM keys);

    void OnCanvasLeftUp(float, float);

    void OnCanvasDoubleClick(float x, float y);

    void OnCanvasMouseMove(float x, float y, WPARAM keys);

    void OnCanvasWheel(float x, float y, int delta, UINT keys);

    void OnKeyDown(UINT key, bool ctrl, bool shift);

    void NavigateComparePane(UINT key);

    void Navigate(int direction, bool extendSelection);

    void SelectIndex(size_t index, bool extend);

    void EnsureFocusedThumbnailVisible(size_t index);

    void ResetView();

    void SetMode(ViewMode mode);

    void EnterSingleFromSelection();

    void EnterCompareFromSelection();
    void PrioritizeCompareLoads(const std::wstring& focusPath = {});

    void UpdateModeButtons();
// CODE GUIDE: See CODE_GUIDE.md -> "Writing metadata".
// OWNER: Metadata/file transactions and history; preserve preflight, flush, verification, commit, and rollback.

// Metadata/history safety: snapshots, storage preflight, transactional metadata
// updates, verified copy/move primitives, and bounded undo/redo history.

std::vector<fs::path> ActivePaths() const;

    std::optional<MetadataSnapshot> SnapshotForPath(const fs::path& path) const;

    void UpdateHistoryButtons();

    void FinishUnrecordedAction(bool changed);

    void PushHistory(HistoryEntry entry);

    bool PreflightMetadataStorage(const std::vector<MetadataSnapshot>& targets,
        const std::wstring& operation);

    static quicksift::transactions::MetadataWriteOutcome WriteMetadataPatchFile(
        const fs::path& path, const MetadataPatch& patch, MetadataStorageKind storage,
        bool jpeg, bool safeJpegWrites);

    enum class MetadataEditField { Rating, Pick, ColorLabel };

    void ConfigureMetadataTransactions();

    void SubmitMetadataEdit(MetadataEditField field, int value);

    void DrainMetadataTransactionResults();

    void FinishMetadataTransaction(
        quicksift::transactions::MetadataTransactionResult result);

    void RefreshMetadataFileState(PhotoItem& item, const fs::path& path);

    static std::wstring NormalizedPathKey(const fs::path& path);

    static bool PathFingerprint(const fs::path& path,
        quicksift::transactions::FileIdentity& identity);

    static bool PathEntryExists(const fs::path& path, bool* isSymlink = nullptr);

    bool HistoryDestinationUnchanged(const FileHistoryItem& item) const;

    bool HistoryRedoSourceUnchanged(const FileHistoryItem& item) const;

    [[nodiscard]] bool HistoryEntryHasExternalIdentityDivergence(
        const HistoryEntry& entry, bool undoReplay) const;

    void RefreshAfterHistory();

    // When identityDiverged is non-null, it is set true only for external
    // content/identity mismatches (another app changed the file), not for
    // temporary blockers such as low storage.
    bool PreflightUndoFiles(const HistoryEntry& entry, std::wstring& detail,
        bool* identityDiverged = nullptr) const;

    bool PreflightRedoFiles(const HistoryEntry& entry, std::wstring& detail,
        bool* identityDiverged = nullptr) const;

    void OfferExternalChangeHistoryReconcile(HistoryDirection direction,
        const std::wstring& detail);

    void RelinquishGuardedHistory(HistoryDirection direction);

    void OfferExternalChangeMetadataReconcile(const std::wstring& detail,
        std::vector<fs::path> conflictPaths,
        std::optional<HistoryDirection> relinquishHistory);

    void RefreshCatalogFromDiskAfterExternalMetadataConflict();

    static HistoryEntry FileHistorySubset(const HistoryEntry& source,
        const std::vector<bool>& completed, bool takeCompleted);

    static HistoryEntry MetadataHistorySubset(const HistoryEntry& source,
        const std::vector<bool>& completed, bool takeCompleted);

    void Undo();

    void Redo();

    void ApplyRating(int rating);

    void PersistPhotoRecord(const PhotoItem& photo, const std::wstring& exifText = {});

    void UpdatePhotoMetadataEverywhere(const fs::path& path, const std::function<void(PhotoItem&)>& update);

    void ApplyPickState(int state);

    void ApplyColorLabel(int label);
// CODE GUIDE: See CODE_GUIDE.md -> "How to add a feature".
// OWNER: Settings/filter UI and high-level file command orchestration; decisions belong to their policy owners.

// User commands: metadata/filter/settings menus, button captions, delete
// confirmation, and high-level copy/move/delete command orchestration.

void ShowColorLabelMenu();

    void ShowRatingMenu();

    void ShowRatingFilterMenu();

    void ShowPickFilterMenu();

    void ShowColorLabelFilterMenu();

    void ShowZoomModeMenu();

    void ShowSettingsMenu();
    void ShowLanguageMenu();
    void UpdateLanguageSelectorButton();
    void ApplySettingCommand(int command);

    void ShowPickStateMenu();

    void UpdateFormatFilterButton();

    void ShowFormatsMenu();

    void UpdateDateFilterButton();

    void ShowDateFilterMenu();

    void UpdateSortButton();

    void ShowSortMenu();

    void UpdateThumbnailSizeButton();

    void ShowThumbnailSizeMenu();

    void RequestDeleteOperation();

    void FileOperation(bool move, bool remove);

    void DrainFileTransactionResults();

    void FinishFileTransaction(quicksift::transactions::FileTransactionResult result);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND canvas_ = nullptr;
    HWND titleOverlay_ = nullptr;
    HWND faceWarningTooltip_ = nullptr;
    HWND faceLockAvailabilityTooltip_ = nullptr;
    HWND libraryList_ = nullptr;
    HWND libraryTooltip_ = nullptr;
    HWND languageSelector_ = nullptr;
    std::array<HWND, 5> settingsSectionLabels_{};
    std::wstring libraryTooltipText_;
    // Compatibility placeholder for legacy tree-scroll message helpers; no TreeView is created.
    HWND tree_ = nullptr;
    HWND exif_ = nullptr;
    HWND userCommentEdit_ = nullptr;
    HWND userCommentLabel_ = nullptr;
    HWND status_ = nullptr;
    std::wstring statusLeftText_;
    std::wstring statusRightText_;
    std::wstring statusProgressText_;
    int statusRatingState_ = -2;
    int statusPickState_ = -2;
    int undoEnabledState_ = -1;
    int redoEnabledState_ = -1;
    std::wstring faceWarningTooltipText_;
    std::wstring faceLockAvailabilityTooltipText_;
    RECT faceWarningTooltipRect_{};
    HWND filterLabel_ = nullptr;
    HWND flyoutBackdrop_ = nullptr;
    std::array<HWND, 6> flyoutTabs_{};
    HWND filmstripToggle_ = nullptr;
    std::unordered_map<int, HWND> controls_;
    UINT currentDpi_ = USER_DEFAULT_SCREEN_DPI;
    HFONT uiFont_ = nullptr;
    HFONT uiFontSemibold_ = nullptr;
    HFONT uiTitleFont_ = nullptr;
    TrafficLight hoveredTrafficLight_ = TrafficLight::None;
    TrafficLight pressedTrafficLight_ = TrafficLight::None;
    bool trackingTitleMouse_ = false;
    bool trackingCanvasMouse_ = false;
    bool windowActive_ = true;
    bool fullscreen_ = false;
    bool shutdownBlocked_ = false;
    bool closingAfterTransactionCancellation_ = false;
    bool fullscreenTitleVisible_ = true;
    LONG_PTR preFullscreenStyle_ = 0;
    LONG_PTR preFullscreenExStyle_ = 0;
    WINDOWPLACEMENT preFullscreenPlacement_{ sizeof(WINDOWPLACEMENT) };
    std::chrono::steady_clock::time_point fullscreenEdgeHoverSince_{};

    bool filmstripVisible_ = false;
    float filmstripOpacity_ = 0.0f;
    mutable FilmstripLayout filmstripLayoutCache_{};
    mutable bool filmstripLayoutCacheValid_ = false;
    mutable float filmstripLayoutCacheHeight_ = -1.0f;
    mutable size_t filmstripLayoutCacheVisibleCount_ = 0;
    mutable size_t filmstripLayoutCacheCurrentIndex_ = std::numeric_limits<size_t>::max();
    mutable std::uint64_t filmstripLayoutCacheGeneration_ = std::numeric_limits<std::uint64_t>::max();
    std::array<float, 6> flyoutTabActiveBlend_{};
    float leftPaneOpenBlend_ = 0.0f;
    bool leftPaneContentVisible_ = false;
    // Skip SetWindowRgn/redraw when animated backdrop geometry is unchanged.
    int leftPaneBackdropLastW_ = -1;
    int leftPaneBackdropLastH_ = -1;
    int leftPaneBackdropLastRadiusDip_ = -1;
    std::chrono::steady_clock::time_point lastAnimationTick_{};
    std::chrono::steady_clock::time_point fullscreenLeaveSince_{};
    FlyoutPanel activeFlyout_ = FlyoutPanel::None;
    // Remembered section restored when the left hit-strip reopens the pane.
    FlyoutPanel lastFlyoutSection_ = FlyoutPanel::Folders;
    bool menuOpen_ = false;
    std::chrono::steady_clock::time_point lastFlyoutHover_ = std::chrono::steady_clock::now();
    bool treeScrollbarHot_ = false;
    bool trackingTreeMouse_ = false;
    bool treeScrollDragging_ = false;
    int treeScrollDragOffset_ = 0;
    bool thumbnailScrollbarHot_ = false;
    bool thumbnailScrollDragging_ = false;
    // True while scrollbar drag OR wheel/keyboard fling/coast keeps decode admission throttled.
    bool thumbnailScrollMotionActive_ = false;
    bool thumbnailViewportNeedsUpdate_ = false;
    bool thumbnailDragRenderTimerActive_ = false;
    // Shared frame pump for scrollbar drag AND wheel/keyboard flings: one layout/paint
    // (and at most one viewport publish) per frame while motion is active.
    bool thumbnailScrollFrameTimerActive_ = false;
    bool thumbnailScrollFramePending_ = false;
    bool thumbnailScrollSettlePending_ = false;
    int thumbnailScrollLastPublishedFirstRow_ = std::numeric_limits<int>::min();
    int thumbnailScrollLastPublishedLastRow_ = std::numeric_limits<int>::min();
    int thumbnailDragLoadMode_ = 0;
    unsigned thumbnailScrollConcurrentCap_ = 0;
    float thumbnailDragVelocityPixelsPerSecond_ = 0.0f;
    std::chrono::steady_clock::time_point lastThrottledSchedule_{};
    bool throttledSchedulePending_ = false;
    float thumbnailScrollDragOffset_ = 0.0f;

    fs::path rootFolder_;
    fs::path currentFolder_;
    fs::path pendingTreeFolder_;
    fs::path restoredFolder_;
    fs::path sessionPath_;
    quicksift::core::CatalogStore catalog_;
    quicksift::core::SelectionStore selection_;
    std::wstring restoredCurrentPath_;

    quicksift::review::ReviewStateModel reviewState_;

    std::vector<std::wstring> libraryPaths_;
    std::unordered_map<FormatGroup, bool> formats_;

    ViewMode restoredMode_ = ViewMode::Thumbnails;
    SortMode sortMode_ = SortMode::NameAsc;
    DateFilter dateFilter_ = DateFilter::Any;
    RatingFilter ratingFilter_ = RatingFilter::All;
    PickFilter pickFilter_ = PickFilter::All;
    ColorLabelFilter colorLabelFilter_ = ColorLabelFilter::All;
    quicksift::review::prefetch::PrefetchPlanner thumbnailPrefetchPlanner_;
    quicksift::core::FaceAnalysisRetryPolicy faceAnalysisRetryPolicy_;
    bool badgeStars_ = true;
    bool badgeColor_ = true;
    bool badgePick_ = true;
    bool badgePair_ = false;
    bool copyMoveRawWithJpg_ = false;
    bool loadOnlyRawJpegPreviews_ = false;
    AppTheme theme_ = AppTheme::Dark;
    std::uint32_t metadataDirectMask_ = kDefaultMetadataDirectMask;
    bool safeJpegMetadataWrites_ = true;
    bool performanceHudVisible_ = false;
    MacOsUiFramework uiFramework_;
    bool dragging_ = false;
    bool scanComplete_ = false;
    bool sessionRestored_ = false;
    D2D1_POINT_2F mouseDown_{};
    D2D1_POINT_2F lastMouse_{};
    std::vector<ColorBlob> backgroundBlobs_;
    std::vector<std::uint32_t> backgroundPixels_;
    int backgroundCacheWidth_ = 0;
    int backgroundCacheHeight_ = 0;
    int backgroundSurfaceWidth_ = 0;
    int backgroundSurfaceHeight_ = 0;
    AppTheme backgroundCacheTheme_ = AppTheme::Dark;
    quicksift::core::HistoryStore historyStore_;
    size_t historyMemoryLimit_ = 96ull * kMebibyte;
    bool replayingHistory_ = false;
    std::vector<fs::path> pendingExternalMetadataReconcilePaths_;
    std::optional<HistoryDirection> pendingExternalMetadataReconcileHistory_;
    uint64_t generation_ = 0;
    size_t pendingRatings_ = 0;
    size_t metadataJobsInFlight_ = 0;
    size_t metadataQueueLimit_ = 24;
    std::deque<size_t> pendingMetadataIndices_;
    bool metadataUiDirty_ = false;
    std::chrono::steady_clock::time_point lastInteraction_ = std::chrono::steady_clock::now() - std::chrono::seconds(5);
    size_t backgroundFaceCursor_ = 0;
    size_t backgroundFaceAttemptsSinceWork_ = 0;
    std::chrono::steady_clock::time_point backgroundFacePauseUntil_{};
    std::chrono::steady_clock::time_point lastPerformanceTelemetryLog_{};
    std::chrono::steady_clock::time_point lastPerformanceCpuSample_{};
    bool performanceCpuSampleInitialized_ = false;
    bool performanceCpuLoadValid_ = false;
    unsigned performanceCpuLoadPercent_ = 0;
    std::uint64_t previousPerformanceCpuIdle_ = 0;
    std::uint64_t previousPerformanceCpuKernel_ = 0;
    std::uint64_t previousPerformanceCpuUser_ = 0;
    bool cpuSampleValid_ = false;
    std::uint64_t previousCpuIdle_ = 0;
    std::uint64_t previousCpuKernel_ = 0;
    std::uint64_t previousCpuUser_ = 0;
    WINDOWPLACEMENT savedPlacement_{ sizeof(WINDOWPLACEMENT) };
    bool hasSavedPlacement_ = false;

    quicksift::Localizer localizer_;
    std::unordered_map<int, std::wstring> controlTextKeys_;
    SystemProfile systemProfile_;
    quicksift::core::FixedPerformancePolicy performanceGovernor_;
    quicksift::core::PerformancePolicy performancePolicy_;
    quicksift::PersistentCache persistentCache_;
    quicksift::ResourcePressureMonitor resourcePressureMonitor_;
    quicksift::StorageProfile storageProfile_;
    std::atomic<bool> deferredStartupCancel_{false};
    std::thread deferredStartupThread_;
    bool deferredStartupReady_ = false;
    bool deferredStartupFailed_ = false;
    quicksift::work::BackgroundCompletionQueue completionQueue_;
    quicksift::transactions::TransactionCoordinator transactions_;
    Worker worker_;
    FolderScanner scanner_;

    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID3D11DeviceContext> d3dContext_;
    ComPtr<IDXGIDevice3> dxgiDevice3_;
    ComPtr<IDXGIAdapter3> dxgiAdapter3_;
    ComPtr<ID2D1Factory> d2dFactory_;
    ComPtr<ID2D1Factory1> d2dFactory1_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext2> deviceContext2_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID2D1Bitmap1> swapChainTarget_;
    ComPtr<IDWriteFactory> dwriteFactory_;
    ComPtr<ID2D1RenderTarget> renderTarget_;
    ComPtr<ID2D1HwndRenderTarget> hwndRenderTarget_;
    ComPtr<IWICImagingFactory> uiWicFactory_;
    ComPtr<ID2D1SolidColorBrush> textBrush_;
    ComPtr<ID2D1SolidColorBrush> panelBrush_;
    ComPtr<ID2D1SolidColorBrush> accentBrush_;
    ComPtr<ID2D1SolidColorBrush> dangerBrush_;
    ComPtr<ID2D1SolidColorBrush> mutedBrush_;
    ComPtr<ID2D1SolidColorBrush> backgroundBrush_;
    ComPtr<ID2D1SolidColorBrush> overlayBrush_;
    ComPtr<ID2D1SolidColorBrush> glassBorderBrush_;
    ComPtr<ID2D1SolidColorBrush> glassHighlightBrush_;
    ComPtr<IDWriteTextFormat> smallText_;
    ComPtr<IDWriteTextFormat> mediumText_;
    ComPtr<IDWriteTextFormat> largeText_;

    std::unordered_map<CacheKey, BitmapEntry, CacheKeyHash> bitmapCache_;
    std::unordered_map<std::wstring, std::set<int>> pathToBitmapSizes_;
    std::unordered_map<std::wstring, ComPtr<ID2D1Bitmap>> shellFallbackCache_;
    std::chrono::steady_clock::time_point shellFallbackLastUse_{};
    std::unordered_map<std::wstring, WicImageSourceEntry> wicImageSources_;
    std::unordered_map<size_t, std::uint8_t> cacheFrequency_;
    size_t cacheFrequencySamples_ = 0;
    std::unordered_set<std::wstring> protectedThumbnailPaths_;
    std::unordered_set<std::wstring> protectedViewPaths_;
    std::unordered_map<std::wstring, CacheKey> displayedViewKeys_;
    std::unordered_map<TileKey, TileEntry, TileKeyHash> tileCache_;
    std::unordered_map<std::wstring, std::wstring> exifCache_;
    quicksift::core::AppState currentAppState_{};
    std::atomic<std::uint64_t> viewInteractionEpoch_{1};
    std::chrono::steady_clock::time_point lastInteractionTimestamp_{ std::chrono::steady_clock::now() };
    std::unordered_map<std::wstring, std::wstring> pathKeyCache_;
    size_t exifCacheLimit_ = 128;
    std::unordered_map<std::wstring, std::chrono::steady_clock::time_point> failedDecodes_;
    std::unordered_set<std::wstring> failedThumbnailPaths_;
    std::unordered_map<std::wstring, int> thumbnailRetryAttempts_;
    std::unordered_map<std::wstring, std::chrono::steady_clock::time_point> thumbnailRetryCooldown_;
    std::unordered_map<std::wstring, std::uint64_t> thumbnailFailureRetryViewportEpochs_;
    std::uint64_t lastScheduledThumbnailViewportEpoch_ = 0;
    static constexpr std::chrono::milliseconds kDecodeRetryBackoff{250};
    bool HasRecentDecodeFailure(const std::wstring& key) noexcept;
    void RememberTransientDecodeFailure(const std::wstring& key) noexcept;
    size_t failedDecodeLimit_ = 4096;
    size_t thumbnailCacheBytes_ = 0;
    size_t previewCacheBytes_ = 0;
    size_t fullCacheBytes_ = 0;
    size_t tileCacheBytes_ = 0;
    size_t thumbnailCacheLimit_ = 64ull * 1024ull * 1024ull;
    size_t previewCacheLimit_ = 192ull * 1024ull * 1024ull;
    size_t fullCacheLimit_ = 128ull * 1024ull * 1024ull;
    size_t tileCacheLimit_ = 192ull * 1024ull * 1024ull;
    size_t memoryHardCacheLimit_ = kFallbackBitmapCacheLimit;
    size_t hardCacheLimit_ = kFallbackBitmapCacheLimit;
    uint64_t useCounter_ = 0;
};

} // namespace quicksift::app
