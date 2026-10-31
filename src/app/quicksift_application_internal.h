// OWNER: Private application composition root and feature-module declaration surface.
#pragma once
#include "core/blocking_wait.h"
#include "core/hud_snapshot.h"
#include "core/pane_timing.h"
#include "core/worker_lifetime.h"
#include "core/view_load_policy.h"
#include "app/application_support.h"
#include "core/hydrated_facts.h"
#include "core/metadata_queue_order.h"
#include "core/selection_store.h"
#include "core/face_detection_policy.h"
#include "core/face_policy.h"
#include "core/face_quality_policy.h"
#include "review/thumbnail_layout.h"
#include "review/review_state_model.h"
#include "review/review_async_policy.h"
#include "review/face_interaction_policy.h"
#include "core/ratings_exchange.h"
#include "core/batch_match_policy.h"
#include "core/search_query.h"
#include "core/image_metadata.h"
#include "core/metadata_edit_queue.h"
#include "core/user_error_catalog.h"
#include "work/background_work_engine.h"
#include "work/folder_scanner.h"
#include "transactions/transaction_coordinator.h"
#include "app/state/chrome_state.h"
#include "app/state/pane_state.h"
#include "app/state/viewer_state.h"
#include "app/state/browse_state.h"
#include "app/state/metadata_state.h"
#include "app/state/ratings_exchange_state.h"
#include "app/state/batch_state.h"
#include "app/state/search_state.h"
#include "app/state/hud_state.h"
#include "app/state/session_log_browser_state.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace quicksift::app {

// Invariant-locale upper-case fold mirroring NTFS case-insensitive name matching
// (non-ASCII letters included). Shared by Ratings Import and Batch Processing.
// Thread-safe (used on worker threads).
std::wstring WindowsNameFold(std::wstring_view name);

// The plain state lives in cohesive structs (app/state/*.h) inherited privately:
// member names and every use are unchanged; services, window/GDI/COM handles and
// image caches stay here because their construction/destruction order matters.
class QuickSiftApplicationImpl :
    private state::ChromeState,
    private state::PaneState,
    private state::ViewerState,
    private state::BrowseState,
    private state::MetadataState,
    private state::RatingsExchangeState,
    private state::BatchState,
    private state::SearchState,
    private state::HudState,
    private state::SessionLogBrowserState {
public:
    QuickSiftApplicationImpl();
    ~QuickSiftApplicationImpl();
    QuickSiftApplicationImpl(const QuickSiftApplicationImpl&) = delete;
    QuickSiftApplicationImpl& operator=(const QuickSiftApplicationImpl&) = delete;
    int Run(HINSTANCE instance, int showCommand);
    void StartDeferredStartup();
    void StartDiagnosticsCleanup();
    // Teardown (WM_DESTROY for normal close and Force quit, and the destructor):
    // revokes workerLifetime_, signals every app-owned worker and joins them all.
    // Idempotent; never throws.
    void StopAllWorkersForTeardown() noexcept;
    // The only way an app-owned worker thread may wake the UI thread.
    static bool PostFromWorker(const quicksift::core::worker_lifetime::Token& token, HWND window,
        UINT message, WPARAM wParam = 0, LPARAM lParam = 0) noexcept;
    void HandleDeferredStartupReady(bool success);

private:

    // All visual roles come from one design-system implementation.
    ThemePalette Palette() const noexcept;

    std::wstring Tr(std::wstring_view english) const;

    std::wstring ThemeToggleText() const;

    std::wstring FullscreenButtonText() const;

    ThemePalette CurrentDocumentTheme() const noexcept;

    void ApplyDocumentTheme(HWND window, DocumentWindowState* state);

    // Returns the new owned document window (nullptr on failure).
    // titleKey: English key re-translated on language change (MissingFiles windows;
    // empty = "Missing files").
    HWND ShowDocumentWindow(const std::wstring& title, const std::wstring& body, DocumentKind kind,
        std::wstring_view titleKey = {});

    void ShowHelp();

    void ShowAbout();

    void ShowDiagnosticLog();

    void RefreshOpenDocumentWindows();

    static bool HasFaceFocusWarning(const PhotoItem& photo) noexcept;

    std::wstring FaceWarningTooltipText() const;

    // Per-photo red-border reason, e.g. "Face 2: eyes closed; Face 1: motion blur".
    std::wstring FaceWarningReasonText(const PhotoItem& photo) const;

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
    // HandleMainMessage helpers (round 19 split; no behaviour change).
    LRESULT OnMainActivate(WPARAM wParam, LPARAM lParam);
    LRESULT OnTitleMouseMove(LPARAM lParam);
    LRESULT OnTitleLeftButtonDown(WPARAM wParam, LPARAM lParam);
    LRESULT OnTitleLeftButtonUp(WPARAM wParam, LPARAM lParam);
    LRESULT OnGetMinMaxInfo(MINMAXINFO* info);
    LRESULT OnCtlColorListBox(WPARAM wParam, LPARAM lParam);
    LRESULT OnMainCloseRequested();
    LRESULT OnQueryEndSession();
    LRESULT OnEndSession(WPARAM wParam);
    void OnMainWindowDestroyed();
    void RequestBackgroundStopForShutdown();
    void KillMainTimersForShutdown();
    void KillInteractionTimersForShutdown();
    void StopServicesForShutdown();

    LRESULT HandleCanvasMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    // HandleCanvasMessage helpers (round 19 split; no behaviour change).
    LRESULT OnCanvasLeftButtonDownMessage(WPARAM wParam, LPARAM lParam);
    void BeginThumbnailScrollbarDrag(float pointY);
    LRESULT OnCanvasLeftButtonUpMessage(LPARAM lParam);
    void EndThumbnailScrollbarDrag();
    bool CancelCanvasPressOnCaptureLoss(LPARAM lParam);
    LRESULT OnCanvasMouseMoveMessage(WPARAM wParam, LPARAM lParam);
    void DragThumbnailScrollbar(float pointY);
    bool SetCanvasCursor(LPARAM lParam);

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
    // One quantized (2-DIP steps) radius for the backdrop region AND its painter.
    int LeftPaneBackdropRadiusDip() const noexcept;
    // WM_SETTINGCHANGE / WM_THEMECHANGED / WM_SYSCOLORCHANGE / DWM colorization:
    // re-reads accessibility + accent (RefreshVisualEffects) and re-themes once.
    void OnSystemVisualSettingsChanged();

    // Raise every currently-visible left-pane child above the backdrop without
    // ShowWindow churn (show/hide only happens in LayoutControls on open/close).
    void RaiseLeftPaneContentAboveBackdrop();
    // Visits the active section's child windows in a stable order (shared by the
    // raise, invalidate and synchronous-paint passes).
    void ForEachActiveSectionChild(const std::function<void(HWND)>& visit) const;

    // Opaque-erase + paint the flyout backdrop immediately so a tab switch cannot
    // leave prior section pixels stacked in the shared content rect.
    void EraseLeftPaneBackdropFully();

    [[nodiscard]] bool LeftPaneShouldShowContent() const noexcept;

    void UpdateAutoHideFromCursor();

    FlyoutPanel PanelForFlyoutTabWindow(HWND window) const;

    void DrawFlyoutTab(DRAWITEMSTRUCT* draw, FlyoutPanel panel);

    void DrawFlyoutBackdrop(DRAWITEMSTRUCT* draw);
    // ---- Left-pane frost + flicker-free pane painting (0.9.4.0 bug-fix round) ----
    // Backdrop z-slot: directly above the canvas (and the search input that sits on
    // it), below every pane content sibling. Returns the hWndInsertAfter to use, or
    // nullptr when the backdrop already is in that slot.
    HWND LeftPaneBackdropInsertAfter() const;
    bool LeftPaneFrostEnabled() const noexcept;
    // Called by Render after EndDraw: reads the canvas pixels under the open pane
    // back (only when dirty / after the refresh interval, never during scroll motion)
    // and rebuilds the frost; repaints the pane only when the frost changed.
    void MaybeCaptureLeftPaneFrost();
    void MarkLeftPaneFrostDirty() noexcept { leftPaneFrostDirty_ = true; }
    // Pending capture that was throttled: re-render the canvas once (timer tick).
    void ServiceLeftPaneFrost();
    // Frost-image coordinates of a pane window's top-left.
    POINT LeftPaneFrostSourceFor(HWND window) const;
    bool IsLeftPaneSurface(HWND window) const;
    void PaintLeftPaneFrostBehind(HWND window, HDC dc, const RECT& local) const;
    bool LeftPaneWindowBelongsTo(HWND window, int id, FlyoutPanel panel) const;
    void HideLeftPaneSectionContentExcept(FlyoutPanel keep);
    // Scrollable left pane (every tab): natural (unscrolled) child rects captured
    // after layout, shifted by the per-tab offset, clipped/hidden to the viewport.
    void ApplyPaneScroll(const RECT& viewport);
    void RepositionPaneScroll();
    bool ScrollPaneBy(int deltaPx);
    bool HandlePaneWheel(WPARAM wParam, LPARAM lParam);
    void EnsurePaneControlVisible(HWND control);
    bool PaneScrollbarGeometry(RECT& track, RECT& thumb) const;
    bool IsPaneScrollableChild(HWND window) const;
    void InvalidateUnderHiddenPaneChild(HWND window);
    void PaintLibraryList(HWND hwnd);
    RECT LibraryListThumbRect(HWND list) const;
    HBRUSH LibraryListBrush();

    void RedrawActiveFlyoutContents();

    void PaintMainWindow();

    void RaiseFullscreenTitleOverlay();

    // Title-bar layout (Help/Theme pills and the kTitleButtons rows). `force`
    // false is the per-LayoutControls call: it is skipped when nothing that
    // affects the result changed (TitleLayoutKey) and the layout is not marked
    // dirty. Direct callers (fullscreen reveal, caption changes) always force.
    void LayoutTitleControls(int width, bool force = true);
    // Everything LayoutTitleControls reads: width, DPI, fullscreen state, view
    // mode family, theme and the measured caption widths (language, captions).
    struct TitleLayoutKey {
        int width = 0;
        UINT dpi = 0;
        bool fullscreen = false;
        bool fullscreenTitleVisible = false;
        bool singleOrCompare = false;
        AppTheme theme = AppTheme::Dark;
        std::array<int, kTitleButtons.size()> widths{};
        bool operator==(const TitleLayoutKey&) const = default;
    };
    TitleLayoutKey CurrentTitleLayoutKey(int width) const;
    void MarkTitleLayoutDirty() noexcept { titleLayoutDirty_ = true; }
    void PlaceFilmstripToggle(int width, int height);
    void SetFilmstripToggleGlyph();

    void LayoutControls(int width, int height);
    // LayoutControls helpers (round 19 split; no behaviour change).
    enum class PaneChildRegion { Rounded, Antialiased };
    void PlacePaneChild(HWND child, int x, int y, int width, int height, int radius,
        PaneChildRegion region);
    void LayoutCanvasArea(int width, int height);
    void LayoutFlyoutTabs(int width, int height, bool showPaneContent);
    void LayoutFoldersSection(const RECT& flyout);
    void LayoutInfoSection(const RECT& flyout);
    void LayoutButtonFlowSection(const RECT& flyout, HDC flyoutMeasureDc);
    void LayoutSettingsSection(const RECT& flyout, HDC flyoutMeasureDc);
    void LayoutStatusBar(int width, int height);

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
    // Same dropdown for another owner window (a document window); item text is shown
    // as given (no Tr()), for data such as file labels.
    int TrackDropdownMenuVerbatim(HWND owner, HWND anchor, std::vector<MenuItem> items);
    // Shared nested-loop pump for framework dropdowns (main window and form dialogs).
    std::function<void()> DropdownMaintenancePump();

    void OnCommand(int id, int notifyCode, HWND source);
    // OnCommand helpers (round 19 split; no behaviour change).
    bool HandleCommandBeforeAlertGuard(int id, int notifyCode);
    void OnLibraryListSelectionChanged();
    void RemoveSelectedLibraryFolder();
    bool DispatchNavigationCommand(int id);
    void OnCompareModeCommand();
    bool DispatchCullingCommand(int id);
    bool DispatchFilterMenuCommand(int id);
    bool DispatchViewerCommand(int id);
    void ToggleSyncCompareViewCommand();
    void ToggleFaceLockCommand();
    void DispatchApplicationCommand(int id);
    void ToggleThemeCommand();
    void RefreshUserComment();
    void SaveUserComment();

    LRESULT OnNotify(NMHDR* header);

    void ChooseFolder();

    void RefreshLibraryList();

    // Filter tab (format, date, rating/pick/label) AND the applied thumbnail search.
    bool CullAllowed(const PhotoItem& item) const;
    // Filter tab only (the search box's "of N" total).
    bool FilterAllowed(const PhotoItem& item) const;

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
    // Schedules the next completion slice without starving input/paint (0.9.4.0).
    void ScheduleBackgroundCompletionContinuation();
    // Posts at most one coalesced wake for queued completions (0.9.4.0).
    void RequestBackgroundCompletionWake();
    void DrainPostedWorkerMessages();

    void CompleteMetadataJob();

    void OnMetadataJobAborted(uint64_t generation);

    int CalculateAdaptiveTimeoutMs(const std::wstring& path, int attempt) const;

    void OnWorkReady(const std::shared_ptr<WorkResult>& result);
    // OnWorkReady helpers (round 19 split; no behaviour change).
    void ApplyMetadataWorkResult(const std::shared_ptr<WorkResult>& result, const std::wstring& path);
    void ApplyExifWorkResult(const std::shared_ptr<WorkResult>& result, const std::wstring& path);
    void ApplyFaceWorkResult(const std::shared_ptr<WorkResult>& result, const std::wstring& path);
    void HandleUnusableDecodeResult(const std::shared_ptr<WorkResult>& result, const std::wstring& path);
    void ClearDecodeFailureStateOnSuccess(const std::shared_ptr<WorkResult>& result, const std::wstring& path);
    bool DecodedResultIsObsolete(const std::shared_ptr<WorkResult>& result, const std::wstring& path);
    void RefreshCatalogGeometryFromDecode(const std::shared_ptr<WorkResult>& result, const std::wstring& path);
    void AdmitDecodedPixels(const std::shared_ptr<WorkResult>& result, const std::wstring& path);
    void RejectUndisplayableDecode(const std::shared_ptr<WorkResult>& result, const std::wstring& path,
        const wchar_t* reason, HRESULT hr);
    void EnqueueFaceAnalysisAfterDecode(const std::shared_ptr<WorkResult>& result, const std::wstring& path);
    size_t ReclaimableBytesForDecode(const std::shared_ptr<WorkResult>& result, const std::wstring& path,
        const std::optional<CacheKey>& incomingBitmapKey);
    void StoreDecodedTile(const std::shared_ptr<WorkResult>& result, const std::wstring& path,
        const ComPtr<ID2D1Bitmap>& bitmap);
    void StoreDecodedBitmap(const std::shared_ptr<WorkResult>& result, const std::wstring& path,
        CacheKey key, const ComPtr<ID2D1Bitmap>& bitmap);
// CODE GUIDE: See CODE_GUIDE.md -> "Scrolling thumbnails".
// OWNER: Graphics devices and bitmap-cache admission/eviction; UI thread owns device resources.

// Graphics/cache controller: Direct2D/Direct3D resources, bitmap admission and
// eviction, GPU pressure handling, WIC image sources, and view-resource retention.

    bool CreateSwapChainTarget();

    bool EnsureModernDeviceResources();

    void ResizeCanvasTarget(UINT width, UINT height);

    void ResetModernDeviceResources();

    void EnsureDeviceResources();

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
    // EvictBitmapCache helpers (round 19 split; no behaviour change).
    struct CacheEvictionBudget {
        bool forceProtected = false;
        std::optional<CacheClass> incomingClass;
        size_t incomingBytes = 0;
        size_t adjustedHardLimit = 0;
        std::chrono::steady_clock::time_point now{};
        std::chrono::milliseconds recentGrace{ 0 };
        bool IsRecent(const std::chrono::steady_clock::time_point& lastUseTime) const;
    };
    size_t CacheLimitForClass(CacheClass cacheClass) const;
    size_t CacheBytesForClass(CacheClass cacheClass) const;
    size_t EvictionLimitFor(const CacheEvictionBudget& budget, CacheClass cacheClass) const;
    void EvictBitmapCacheByCandidates(const CacheEvictionBudget& budget);
    bool EvictOldestBitmap(const CacheEvictionBudget& budget, std::optional<CacheClass> onlyClass);
    void EvictBitmapCacheByLruScan(const CacheEvictionBudget& budget);

    void TrimCachesForMemoryPressure(bool forced = false);

    void OnResourcePressure(quicksift::ResourcePressureEvent event);

    int BucketFor(int desired) const;
    int MaximumViewDecodeEdge() const;
    void LogSingleViewStall(const PhotoItem& photo, const BitmapEntry* bitmap, int displayDesired);

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

    ComPtr<ID2D1Bitmap> GetOrCreateShellFallback(const fs::path& path, int targetSize);

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

    // Pre-localized variants (no Tr lookup) for dynamic text (counts, names).
    // token: optional PendingGlassAction recorded on the notice so ResolveGlassAlert
    // can react to its dismissal (notices never dispatch an action).
    void ShowGlassAlertText(const std::wstring& title, const std::wstring& message,
        GlassAlertKind kind, PendingGlassAction token = PendingGlassAction::None);
    // Confirmation with an optional third, left-aligned button. Pre-localized text.
    void ShowGlassChoice(const std::wstring& title, const std::wstring& message,
        const std::wstring& primaryText, PendingGlassAction primaryAction,
        const std::wstring& secondaryText, GlassAlertKind kind,
        const std::wstring& tertiaryText = {},
        PendingGlassAction tertiaryAction = PendingGlassAction::None,
        bool tertiaryKeepsOpen = false);
    void DispatchGlassAction(PendingGlassAction action);
    void ActivateGlassAlertTertiary();
    // In-canvas alert keyboard: Esc dismisses, Enter/Space activate the focused
    // (default: primary) button, Tab/Shift+Tab and Left/Right move the focus ring.
    void HandleGlassAlertKey(UINT key, bool shift);
    // AA rounded canvas corners painted in the parent's background colour (the
    // canvas HWND region is a few pixels larger; see ApplyAntialiasedControlRegion).
    void DrawCanvasCornerUnderlay();
    // Face outline reveal/fade opacity (0..1) for a pane; records the pane as
    // displayed this frame.
    float FaceOutlineOpacityForPath(const std::wstring& path);

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

    // Round 23: one badge painter for the grid and the filmstrip (same capsule
    // language). The filmstrip omits the colour label and format badges and fades
    // the badges with the strip.
    struct ThumbnailBadgeStyle {
        bool includeColorLabel = true;
        bool includeFormat = true;
        float opacity = 1.0f;
    };
    void DrawThumbnailBadges(const PhotoItem& photo, D2D1_RECT_F area, const ThumbnailBadgeStyle& style);
    void DrawThumbnailBadges(const PhotoItem& photo, D2D1_RECT_F area) { DrawThumbnailBadges(photo, area, ThumbnailBadgeStyle{}); }
    void RefreshBadgeCaptions();

    void DrawBitmapFit(BitmapEntry* bitmap, D2D1_RECT_F area,
        const quicksift::review::transform::ResolvedView& view, bool clip, float opacity = 1.0f);
    // Placeholder -> image crossfade bookkeeping (central Animator; skipped in motion).
    void NoteThumbnailPlaceholder(const std::wstring& path);
    float ThumbnailRevealOpacity(const std::wstring& path);

    // Faint image-space outlines on every detected face (FaceLock on).
    void DrawFaceOutlines(const PhotoItem& photo, D2D1_RECT_F area,
        const quicksift::review::transform::ResolvedView& view);

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

    // FaceLock target index: matched persisted/user selection, otherwise faces[0].
    std::optional<std::size_t> TargetFaceIndexForPath(const std::wstring& path) const;

    // Hit-tests face outlines from CURRENT layout/view state (never paint-time
    // rectangles): pane under the pointer -> resolved view -> inflated face boxes.
    std::optional<FaceOutlineHit> HitFaceOutline(float x, float y) const;

    // Makes the face the FaceLock target for its photo (session + persisted) and
    // frames it at kFaceClickFillFraction of the pane.
    void SelectFaceAndFrame(const std::wstring& path, std::size_t faceIndex);

    // Click-to-zoom toggle (face_interaction::FaceZoomToggle). Key: the pane path,
    // or one shared key in Compare with Sync on (all panes share one view).
    std::wstring FaceZoomKeyForPath(const std::wstring& path) const;
    bool PathDisplayedInReview(const std::wstring& path) const;
    // Current visible view + pane rect of a displayed pane (nullopt otherwise).
    std::optional<quicksift::review::face_interaction::PaneViewSnapshot>
        PaneSnapshotForPath(const std::wstring& path) const;
    // Captures the toggle inputs at mouse-down (before Compare slot activation can
    // move views) for a press on a face outline.
    void CaptureFaceTogglePress(const FaceOutlineHit& hit);
    // A confirmed click (not a drag) on a face outline: lock-zoom or revert.
    void HandleFaceOutlineClick(const FaceOutlineHit& hit);
    void ClearFaceZoomToggle() noexcept;
    // Cancels an in-progress canvas press (Escape / capture loss). Returns true
    // when a press was active.
    bool CancelCanvasPress();
    quicksift::review::face_interaction::DragThreshold CanvasDragThreshold() const;
    // Right-click on a photo with FaceLock on: pin/unpin face outlines.
    bool ToggleFaceOutlinesPinned(float x, float y);

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
    FilmstripLayout CurrentFilmstripLayout(const D2D1_RECT_F& bounds) const;
    D2D1_RECT_F SingleImageArea(const D2D1_RECT_F& bounds) const;
    float FilmstripReservedDip(const D2D1_RECT_F& bounds) const;
    bool FilmstripContains(float x, float y) const;
    std::optional<size_t> HitFilmstrip(float x, float y) const;
    bool NavigateToSingleIndex(size_t index, int direction, bool resetView);
    void PrefetchFilmstrip(const FilmstripLayout& layout);
    void RenderFilmstrip(const D2D1_RECT_F& bounds);
    void ToggleFilmstrip();
    void UpdateAnimations();
    // 16 ms while the central Animator has work, the 33 ms idle poll otherwise.
    void UpdateAnimationCadence();
    void SyncFilmstripMotion(std::chrono::steady_clock::time_point now);
    void PublishFilmstripDecodeWindow(quicksift::core::filmstrip::Range range);

// OWNER: Import / Export Ratings (src/ui/ratings_exchange_commands.cpp). Format,
// parser and reconciliation live in core/ratings_exchange (portable, checked).
    void ShowRatingsExchangeChooser();
    void BeginRatingsExport();
    void BeginRatingsImport();
    // UI-thread completion of the ratings worker (WM_APP_RATINGS_WORKER_DONE).
    void OnRatingsWorkerCompleted();
    // Import confirmation chain: count mismatch -> missing files -> final confirm.
    void ContinueRatingsImport(PendingGlassAction step);
    void ShowRatingsMissingFilesWindow();
    void CloseRatingsMissingFilesWindow();
    void ApplyRatingsImport();
    void CancelRatingsImport();
    void StopRatingsWorker() noexcept;
    bool RatingsWorkerBusy() const noexcept;
    std::wstring RatingsStatusLine() const;
    // Builds and submits the import through the existing metadata transaction
    // pipeline (metadata_history_and_file_safety.cpp). Returns the token (0 = none).
    // Fields left empty are not changed (Batch Processing "No change").
    struct RatingsImportItem {
        fs::path path;
        std::optional<int> starRating;
        std::optional<int> pickState;
        std::optional<int> colorLabel;
    };
    std::uint64_t SubmitRatingsImport(const std::vector<RatingsImportItem>& items,
        RatingsImportCounts& counts, std::wstring_view label = L"Ratings import");
    // Called by FinishMetadataTransaction for the import token. Returns true when
    // the completion was handled (summary toast/alert shown).
    bool FinishRatingsImportTransaction(
        const quicksift::transactions::MetadataTransactionResult& result, bool changed);

// OWNER: Batch Processing (src/ui/batch_processing_commands.cpp). Name-list parsing,
// extension validation and matching live in core/batch_match_policy (portable,
// checked by scripts/checks/batch_match_checks.cpp). The dialog is the reusable
// framework form dialog; the apply step reuses SubmitRatingsImport.
    void OpenBatchProcessingDialog();
    void OnBatchFormEvent(HWND dialog, int id, quicksift::ui::framework::FormEvent event);
    void CaptureBatchSettings();
    bool ValidateBatchExtensions();
    void BeginBatchFolderLoad();
    void BeginBatchPreflight();
    void OnBatchWorkerCompleted();
    void ApplyBatchProcessing();
    void ReturnToBatchDialog();
    void CloseBatchDialog();
    void CancelBatchPending();
    void ShowBatchUnmatchedWindow();
    void CloseBatchUnmatchedWindow();
    void CheckBatchGlassWatch();
    void StopBatchWorker() noexcept;
    bool BatchWorkerBusy() const noexcept;

    // ---- Thumbnail search (thumbnail_search_controller.cpp) ----
    // Floating field shown only in Thumbnail view; queries compile to the shared
    // predicate clauses and AND with the Filter tab inside CullAllowed().
    bool CreateSearchField();
    bool SearchFieldAvailable() const;
    D2D1_RECT_F SearchFieldFrameDip() const;
    float ThumbnailSearchLaneDip() const;
    void LayoutSearchField();
    void UpdateSearchFieldTheme();
    // Search field fade on the central Animator (target sync before the tick, value
    // read-back after it), then the per-tick layout/focus/demand upkeep.
    void SyncSearchFieldTransition(std::chrono::steady_clock::time_point now);
    void ApplySearchFieldTransition();
    void UpdateSearchFieldAnimation();
    void DrawSearchFieldOverlay();
    bool HandleSearchFieldMouseDown(float x, float y);
    bool SearchFieldContains(float x, float y) const;
    void UpdateSearchFieldHover(float x, float y);
    void ClearSearchFieldHover();
    bool HandleSearchKey(const MSG& message);
    void FocusSearchField();
    void OnSearchTextChanged();
    void OnSearchDebounceTimer();
    void ApplySearchText();
    void ClearSearch();
    void RequestSearchEvaluation(bool resetScroll);
    void ApplySearchResult(std::uint64_t serial, std::uint64_t epoch, std::vector<std::uint8_t> matches);
    void OnSearchWorkerMessage();
    void EnsureSearchFactsHydrated();
    void ApplySearchFactUpdates();
    // Queues capture/comment/camera reads on the background search worker (P3,
    // below decode and user work). Shared by search and the filter menus.
    void QueueFactHydration(bool needCapture, bool needComments, bool needCamera);
    void OnSearchFolderChanged();
    void StopSearchWorker() noexcept;

    // Previous-session log browser (session_log_browser_controller.cpp). The
    // diagnostic log window's "Previous sessions…" button lists the Logs folder on
    // a worker, shows a dropdown, and opens the chosen log read-only.
    void BrowseSessionLogs(HWND documentWindow, HWND anchor);
    bool StartSessionLogWorker(bool listing, HWND requester, HWND anchor, std::wstring name);
    void OnSessionLogWorkerDone();
    void ShowSessionLogMenu(SessionLogWorkerResult& result);
    void ShowSessionLogDocument(const SessionLogWorkerResult& result);
    void StopSessionLogWorker() noexcept;
    bool StartSearchWorker();
    void SearchWorkerLoop(HWND window, quicksift::core::worker_lifetime::Token token) noexcept;
    bool SearchAllows(const PhotoItem& item) const;
    std::wstring SearchCountText() const;
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
    // 0.9.4.0 thumbnail reliability: re-enqueue visible (+1 row margin) cells that
    // have no cached bitmap and no pending/in-flight job; arms the backstop timer
    // while anything visible is still missing.
    void ReconcileVisibleThumbnails(const wchar_t* reason);
    void ArmThumbnailReconcile(UINT delayMs);
    void StopThumbnailReconcile();
    // Retry policy (3 attempts, escalating worker timeouts, short backoff).
    bool ThumbnailDecodeAdmissible(const PhotoItem& photo, const std::wstring& key,
        std::chrono::steady_clock::time_point now, int& retryAttempt, bool& forceWic);
    void RecordThumbnailDecodeFailure(const WorkResult& result);
    void RecordViewDecodeFailure(const WorkResult& result);
    bool ViewDecodeAdmissible(const fs::path& path, int& retryAttempt);
    bool ViewDecodeTerminallyFailed(const fs::path& path);
    void ArmDecodeRetryTimer();
    void RetryFailedImageLoads();
    // Single/Compare decode routing: the dedicated view lane in view modes, the
    // shared pool otherwise. Never decodes on the UI thread.
    void RequestViewDecode(const fs::path& path, JobKind kind, int target,
        CacheClass cacheClass, JobPriority priority, bool prefetch = false);
    void PublishViewPrefetchNeighbours(int direction);
    bool ViewBitmapSettled(const BitmapEntry* bitmap, const PhotoItem& photo, int displayDesired) const;
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
    // Blocking-wait dialog (ui/blocking_wait_controller.cpp).
    std::vector<quicksift::core::blocking_wait::Blocker> CurrentBlockers(bool includeUnsavedEdits);
    bool ShowBlockingWaitIfNeeded(quicksift::core::blocking_wait::Command command);
    void UpdateBlockingWaitDialog();
    void OnBlockingWaitPrimary();
    void LogBlockingWait(const std::vector<quicksift::core::blocking_wait::Blocker>& blockers, bool first);
    std::optional<quicksift::core::blocking_wait::Command> blockingWaitCommand_;
    std::chrono::steady_clock::time_point blockingWaitStarted_{};
    std::chrono::steady_clock::time_point blockingWaitLastLog_{};
    std::unordered_map<int, std::chrono::steady_clock::time_point> blockerFirstSeen_;
    // Performance HUD: values refreshed at most once a second (core/hud_snapshot.h).
    std::chrono::steady_clock::time_point hudCountsAt_{};
    std::size_t hudPendingRating_ = 0;
    std::size_t hudPendingPick_ = 0;
    std::uint64_t hudRamFreeMiB_ = 0;
    std::wstring hudBlockingReason_;
    // HUD placement (DIPs; < 0 = default top-right), cached text layout and drag state.
    float hudX_ = -1.0f;
    float hudY_ = -1.0f;
    float hudTextWidthDip_ = 0.0f;
    quicksift::core::hud::PanelRect hudPanelRect_{};
    std::wstring hudLayoutText_;
    ComPtr<IDWriteTextLayout> hudLayout_;
    bool hudDragging_ = false;
    bool hudHandleHot_ = false;
    D2D1_POINT_2F hudDragOffset_{};
    bool HudHandleHit(float x, float y) const noexcept;
    // Reads only Theme + Language from the session file; called before window creation.
    void LoadEarlyPresentationSettings();
    bool earlyPresentationLoaded_ = false;

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

    // Pure (no UI): cumulative free-space check, run on the transaction thread.
    static std::optional<quicksift::transactions::MetadataStorageShortfall>
        MetadataStorageShortfallFor(const std::vector<MetadataSnapshot>& targets);
    void ShowMetadataStorageShortfall(
        const quicksift::transactions::MetadataStorageShortfall& shortfall,
        const std::wstring& operation);
    static quicksift::transactions::MetadataChange::FileStamps MetadataFileStampsFor(
        const fs::path& path);
    static void ApplyMetadataFileStamps(PhotoItem& item,
        const quicksift::transactions::MetadataChange::FileStamps& stamps) noexcept;

    // ---- Metadata write lane (metadata_write_lane_controller.cpp) ----
    // Interactive edits typed while a write runs are coalesced here (last value wins
    // per file and field) and flushed on the urgent lane / after the running plan.
    void QueueMetadataEdit(const std::vector<fs::path>& targets, MetadataFieldId field,
        int value, const std::wstring& label);
    void FlushPendingMetadataEdits();
    void EnsureMetadataWriteLaneTimer();
    void PollMetadataWriteLane();
    void RetryInUseMetadataChanges();
    void SkipInUseMetadataChanges();
    // Localized message for a user-facing error; {file} is replaced by `file`.
    std::wstring UserErrorText(quicksift::core::UserErrorCode code,
        const std::wstring& file = {}) const;
    std::wstring UserErrorTitle(quicksift::core::UserErrorCode code) const;
    void ShowToastText(const std::wstring& localizedText);
    static std::wstring MetadataFieldLabel(MetadataFieldId field);

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

    void ShowLanguageMenu();
    void UpdateLanguageSelectorButton();
    void ApplySettingCommand(int command);

    void ShowPickStateMenu();

    void UpdateFormatFilterButton();

    void ShowFormatsMenu();

    void UpdateDateFilterButton();

    void ShowDateFilterMenu();
    void ShowIsoFilterMenu();
    void ShowApertureFilterMenu();
    void ShowFocalFilterMenu();
    void ShowShutterFilterMenu();
    void ShowCameraFilterMenu();
    void ShowHourFilterMenu();
    // Pull indexed camera settings and capture time from the cache into the catalog
    // so filter dropdowns show the values present in the folder.
    void HydrateMetadataForFilters();

    void UpdateSortButton();

    void ShowSortMenu();

    void UpdateThumbnailSizeButton();

    void ShowThumbnailSizeMenu();

    void RequestDeleteOperation();

    void FileOperation(bool move, bool remove);
    // FileOperation helpers (round 19 split; no behaviour change).
    struct PlannedFileTransfer {
        fs::path source;
        fs::path destination;
        fs::path sourceSidecar;
        fs::path destinationSidecar;
        bool sidecarPresent = false;
    };
    struct SidecarOwnerIndex {
        bool scanned = false;
        bool succeeded = false;
        std::unordered_map<std::wstring, std::vector<std::wstring>> ownersBySidecar;
    };
    bool AddRawCompanionsForSelection(std::vector<fs::path>& active, bool& missingRawCompanion);
    std::optional<fs::path> ChooseFileOperationDestination(bool move, bool remove);
    bool MediaSidecarHasExternalOwner(std::unordered_map<std::wstring, SidecarOwnerIndex>& directories,
        const std::unordered_set<std::wstring>& operationSourceKeys,
        const fs::path& source, const fs::path& sidecar, bool& scanSucceeded) const;
    bool PlanFileTransfers(const std::vector<fs::path>& active, const fs::path& destinationFolder,
        bool move, bool remove, std::vector<PlannedFileTransfer>& plan);
    bool CheckFileOperationStorage(const std::vector<PlannedFileTransfer>& plan,
        const fs::path& destinationFolder, bool move, bool remove);
    quicksift::transactions::FileTransactionPlan BuildFileTransactionPlan(
        std::vector<PlannedFileTransfer>& plan, bool move, bool remove, bool missingRawCompanion);

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
    // One shared tooltip for every button whose caption is drawn with an ellipsis.
    HWND truncationTooltip_ = nullptr;
    std::unordered_map<HWND, std::wstring> truncatedButtonText_;
    void UpdateTruncationTooltip(HWND button, bool truncated, const std::wstring& fullText);
    void RestyleTooltips();
    HWND languageSelector_ = nullptr;
    std::array<HWND, 5> settingsSectionLabels_{};
    std::wstring libraryTooltipText_;
    // Compatibility placeholder for legacy tree-scroll message helpers; no TreeView is created.
    HWND tree_ = nullptr;
    HWND exif_ = nullptr;
    HWND userCommentEdit_ = nullptr;
    HWND userCommentLabel_ = nullptr;
    HWND status_ = nullptr;
    HWND filterLabel_ = nullptr;
    HWND flyoutBackdrop_ = nullptr;
    std::array<HWND, 6> flyoutTabs_{};
    HWND filmstripToggle_ = nullptr;
    std::unordered_map<int, HWND> controls_;
    UINT currentDpi_ = USER_DEFAULT_SCREEN_DPI;
    HFONT uiFont_ = nullptr;
    HFONT uiFontSemibold_ = nullptr;
    HFONT uiTitleFont_ = nullptr;

    ComPtr<ID2D1Bitmap1> leftPaneFrostReadback_;
    D2D1_SIZE_U leftPaneFrostReadbackSize_{};
    HBRUSH libraryListBrush_ = nullptr;
    COLORREF libraryListBrushColor_ = CLR_INVALID;
    // Child that owned keyboard focus when the main window was deactivated; restored
    // on WA_ACTIVE / WA_CLICKACTIVE (DefWindowProc would focus the frame itself).
    HWND lastFocusedChild_ = nullptr;
    // Color-label badge brushes (index 1..5), owned like the other device brushes.
    std::array<ComPtr<ID2D1SolidColorBrush>, 6> badgeColorBrushes_{};
    void LogPaneTiming(std::wstring_view event, FlyoutPanel panel, bool first, double totalMs,
        const quicksift::core::pane_timing::Recorder* segments);

    quicksift::core::CatalogStore catalog_;
    quicksift::core::SelectionStore selection_;

    quicksift::review::ReviewStateModel reviewState_;

    quicksift::review::prefetch::PrefetchPlanner thumbnailPrefetchPlanner_;
    quicksift::core::FaceAnalysisRetryPolicy faceAnalysisRetryPolicy_;
    // Last applied title-bar layout inputs; see LayoutTitleControls.
    std::optional<TitleLayoutKey> titleLayoutApplied_;
    bool titleLayoutDirty_ = true;
    MacOsUiFramework uiFramework_;

    // ---- Import / Export Ratings state ----

    // ---- Batch Processing ----
    // Runs work on the single batch worker thread (folder load or preflight).
    bool StartBatchWorker(bool folderLoad, std::function<void(BatchWorkerResult&)> work);

    // ---- Thumbnail search state ----
    quicksift::core::metadata_order::VisibleWindow MetadataPriorityWindow() const;
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
    // Shutdown token for app-owned workers (ratings, batch, search, deferred
    // startup): each captures a Token and posts through PostFromWorker.
    quicksift::core::worker_lifetime::Gate workerLifetime_;
    bool workersStoppedForTeardown_ = false;
    std::atomic<bool> deferredStartupCancel_{false};
    std::thread deferredStartupThread_;
    // One-shot, background-priority deletion of diagnostic files older than 7 days
    // (diagnostics::CleanupOldDiagnosticFiles). Holds only a token, never this.
    std::thread diagnosticsCleanupThread_;
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
    ComPtr<ID2D1SolidColorBrush> faceOutlineBrush_;
    ComPtr<ID2D1SolidColorBrush> faceOutlineShadowBrush_;
    // Round 22: filmstrip brushes, created once per device and recoloured per frame
    // (were three CreateSolidColorBrush calls per frame). Reset in DiscardDeviceResources.
    // Round 23: blurry-photo outline (theme red, dashed). Brush is theme-dependent
    // (reset in ApplyTheme); both are released in DiscardDeviceResources.
    ComPtr<ID2D1SolidColorBrush> blurOutlineBrush_;
    ComPtr<ID2D1StrokeStyle> blurOutlineStrokeStyle_;
    ComPtr<ID2D1SolidColorBrush> filmstripBackgroundBrush_;
    ComPtr<ID2D1SolidColorBrush> filmstripActiveBrush_;
    ComPtr<ID2D1SolidColorBrush> filmstripInactiveBrush_;
    ComPtr<IDWriteTextFormat> smallText_;
    ComPtr<IDWriteTextFormat> mediumText_;
    ComPtr<IDWriteTextFormat> largeText_;

    std::unordered_map<CacheKey, BitmapEntry, CacheKeyHash> bitmapCache_;
    std::unordered_map<std::wstring, std::set<int>> pathToBitmapSizes_;
    std::unordered_map<std::wstring, ComPtr<ID2D1Bitmap>> shellFallbackCache_;
    std::chrono::steady_clock::time_point shellFallbackLastUse_{};
    std::unordered_map<size_t, std::uint8_t> cacheFrequency_;
    size_t cacheFrequencySamples_ = 0;
    std::unordered_set<std::wstring> protectedThumbnailPaths_;
    std::unordered_set<std::wstring> protectedViewPaths_;
    std::unordered_map<std::wstring, CacheKey> displayedViewKeys_;
    std::unordered_map<TileKey, TileEntry, TileKeyHash> tileCache_;
    std::unordered_map<std::wstring, std::wstring> exifCache_;
    quicksift::core::AppState currentAppState_{};
    std::atomic<std::uint64_t> viewInteractionEpoch_{1};
    // Round 21 (t29u): ID2D1RenderTarget::GetMaximumBitmapSize (0 until known).
    std::uint32_t deviceMaxBitmapEdge_ = 0;
    // Round 21 (t29u): Single View load-stall watchdog (see LogSingleViewStall).
    quicksift::core::view_load::StallWatch singleViewStallWatch_;
    std::chrono::steady_clock::time_point lastInteractionTimestamp_{ std::chrono::steady_clock::now() };
    std::unordered_map<std::wstring, std::wstring> pathKeyCache_;
    size_t exifCacheLimit_ = 128;
    std::unordered_map<std::wstring, std::chrono::steady_clock::time_point> failedDecodes_;
    // Terminal thumbnail failures (failed placeholder shown). Keyed by NormalizedPathKey.
    std::unordered_set<std::wstring> failedThumbnailPaths_;
    // 0.9.4.0 per-path retry state (NormalizedPathKey). failedAttempts counts worker
    // attempts that produced no pixels; nextRetryAt is the backoff deadline; terminal
    // entries remember the file stamp so a changed file resets automatically.
    struct DecodeRetryState {
        int failedAttempts = 0;
        quicksift::core::DecodeFailureKind lastFailure = quicksift::core::DecodeFailureKind::None;
        std::chrono::steady_clock::time_point nextRetryAt{};
        bool terminal = false;
        std::uint64_t fileSize = 0;
        std::int64_t modifiedStamp = 0;
    };
    static constexpr int kMaxDecodeAttempts = 3;
    std::unordered_map<std::wstring, DecodeRetryState> thumbnailRetryState_;
    std::unordered_map<std::wstring, DecodeRetryState> viewRetryState_;
    bool thumbnailReconcileTimerActive_ = false;
    std::wstring lastViewPrefetchSignature_;
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
