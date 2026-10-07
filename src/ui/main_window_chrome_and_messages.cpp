// OWNER: Compiled QuickSiftApplication feature module.
#include "app/quicksift_application_internal.h"

namespace quicksift::app {

// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Main-window creation/message routing/title bar/fullscreen/DPI and checked UI timers.

// Main-window shell: class registration, creation, custom title bar, fullscreen,
// tree scrollbar, Win32 message routing, DPI conversion, and interaction timing.

    bool QuickSiftApplicationImpl::StartUiTimer(UINT_PTR timerId, UINT intervalMilliseconds, std::wstring_view purpose) {
        if (hwnd_ && SetTimer(hwnd_, timerId, intervalMilliseconds, nullptr)) return true;
        QS_LOG_WARNING(L"UI", L"Could not start the " + std::wstring(purpose) + L" timer");
        return false;
    }


    bool QuickSiftApplicationImpl::HandleGlobalKey(const MSG& message) {
        if (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN) return false;
        if (message.hwnd != hwnd_ && !IsChild(hwnd_, message.hwnd)) return false;
        const UINT key = static_cast<UINT>(message.wParam);
        if (uiFramework_.Alerts().Visible()) {
            if (key == VK_ESCAPE) DismissGlassAlert();
            else if (key == VK_RETURN || key == VK_SPACE) ConfirmGlassAlert();
            return true;
        }
        if (key == VK_F11) {
            ToggleFullscreen();
            return true;
        }
        const bool ctrl = GetKeyState(VK_CONTROL) < 0;
        const bool shift = GetKeyState(VK_SHIFT) < 0;
        const bool globalNavigation = key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN ||
            key == VK_HOME || key == VK_END || key == VK_ESCAPE || key == VK_DELETE;
        const bool globalShortcut = ctrl && (key == L'Z' || key == L'Y');
        const bool globalTag = (key >= L'0' && key <= L'9') || key == L'P' || key == L'X' || key == L'U';
        if (!globalNavigation && !globalShortcut && !globalTag) return false;
        MarkInteraction();
        OnKeyDown(key, ctrl, shift);
        return true;
    }


    bool QuickSiftApplicationImpl::RegisterClasses() {
        WNDCLASSEXW mainClass{ sizeof(WNDCLASSEXW) };
        mainClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        mainClass.lpfnWndProc = MainWndProc;
        mainClass.hInstance = instance_;
        mainClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        mainClass.hIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_QS),
            IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
        mainClass.hIconSm = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_QS),
            IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
        mainClass.hbrBackground = nullptr;
        mainClass.lpszClassName = kMainClass;
        if (!RegisterClassExW(&mainClass)) return false;

        WNDCLASSEXW canvasClass{ sizeof(WNDCLASSEXW) };
        canvasClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        canvasClass.lpfnWndProc = CanvasWndProc;
        canvasClass.hInstance = instance_;
        canvasClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        canvasClass.hbrBackground = nullptr;
        canvasClass.lpszClassName = kCanvasClass;
        if (!RegisterClassExW(&canvasClass)) return false;

        WNDCLASSEXW documentClass{ sizeof(WNDCLASSEXW) };
        documentClass.style = CS_HREDRAW | CS_VREDRAW;
        documentClass.lpfnWndProc = DefWindowProcW;
        documentClass.hInstance = instance_;
        documentClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        documentClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        documentClass.lpszClassName = kDocumentClass;
        if (!RegisterClassExW(&documentClass)) return false;

        return uiFramework_.RegisterPopupWindowClass(instance_);
    }


    bool QuickSiftApplicationImpl::PrepareSavedWindowPlacement() {
        if (!hasSavedPlacement_) return false;
        RECT normal = savedPlacement_.rcNormalPosition;
        HMONITOR monitor = MonitorFromRect(&normal, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{ sizeof(MONITORINFO) };
        if (!monitor || !GetMonitorInfoW(monitor, &info)) {
            QS_LOG_WARNING(L"UI", L"Saved window placement could not resolve a monitor; using the default position");
            hasSavedPlacement_ = false;
            return false;
        }
        const RECT work = info.rcWork;
        const auto boundedExtent = [](LONG lower, LONG upper) {
            const std::int64_t extent = static_cast<std::int64_t>(upper) -
                static_cast<std::int64_t>(lower);
            return static_cast<int>(std::clamp<std::int64_t>(extent, 1, INT_MAX));
        };
        const int workWidth = boundedExtent(work.left, work.right);
        const int workHeight = boundedExtent(work.top, work.bottom);
        const int requestedWidth = boundedExtent(normal.left, normal.right);
        const int requestedHeight = boundedExtent(normal.top, normal.bottom);
        int width = std::clamp(requestedWidth, std::min(640, workWidth), workWidth);
        int height = std::clamp(requestedHeight, std::min(480, workHeight), workHeight);
        int left = std::clamp(static_cast<int>(normal.left), static_cast<int>(work.left),
            static_cast<int>(work.right) - width);
        int top = std::clamp(static_cast<int>(normal.top), static_cast<int>(work.top),
            static_cast<int>(work.bottom) - height);
        savedPlacement_.length = sizeof(WINDOWPLACEMENT);
        savedPlacement_.flags = 0;
        savedPlacement_.rcNormalPosition = { left, top, left + width, top + height };
        savedPlacement_.showCmd = savedPlacement_.showCmd == SW_SHOWMAXIMIZED ?
            SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        return true;
    }


    bool QuickSiftApplicationImpl::CreateMainWindow(int showCommand) {
        POINT origin{};
        if (!GetCursorPos(&origin)) origin = { 0, 0 };
        HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{ sizeof(MONITORINFO) };
        if (!GetMonitorInfoW(monitor, &info)) {
            info.rcWork = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        }
        const RECT work = info.rcWork;
        const std::int64_t rawWorkWidth = static_cast<std::int64_t>(work.right) - work.left;
        const std::int64_t rawWorkHeight = static_cast<std::int64_t>(work.bottom) - work.top;
        const int workWidth = static_cast<int>(std::clamp<std::int64_t>(rawWorkWidth, 1, INT_MAX));
        const int workHeight = static_cast<int>(std::clamp<std::int64_t>(rawWorkHeight, 1, INT_MAX));
        const int width = std::max(1, static_cast<int>(std::lround(workWidth * 0.90)));
        const int height = std::max(1, static_cast<int>(std::lround(workHeight * 0.90)));
        const int left = work.left + (workWidth - width) / 2;
        const int top = work.top + (workHeight - height) / 2;

        // Use a real native Win32 window, but own the entire non-client area.
        // WS_POPUP prevents Windows/DWM from resurrecting a standard caption,
        // while the sizing/system styles preserve snapping, Alt+Space, taskbar
        // behavior, minimize/maximize, and resizable edges.
        const DWORD windowStyle = WS_POPUP | WS_THICKFRAME | WS_SYSMENU |
            WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, kMainClass, kWindowTitle,
            windowStyle,
            left, top, width, height,
            nullptr, nullptr, instance_, this);
        if (!hwnd_) return false;
        PrepareSavedWindowPlacement();
        if (hasSavedPlacement_ && !SetWindowPlacement(hwnd_, &savedPlacement_)) {
            QS_LOG_WARNING(L"UI", L"Windows rejected the saved window placement; using the default position");
            hasSavedPlacement_ = false;
        }
        const int resolvedShow = hasSavedPlacement_ ? static_cast<int>(savedPlacement_.showCmd) :
            (showCommand == SW_SHOWDEFAULT ? SW_SHOWNORMAL : showCommand);
        ShowWindow(hwnd_, resolvedShow);
        UpdateWindow(hwnd_);
        return true;
    }


    std::array<int, kTitleButtons.size()> QuickSiftApplicationImpl::MeasuredTitleButtonWidthsPx() const {
        std::array<int, kTitleButtons.size()> widths{};
        HDC measureDc = hwnd_ ? GetDC(hwnd_) : nullptr;
        HFONT oldFont = nullptr;
        if (measureDc) {
            oldFont = static_cast<HFONT>(SelectObject(measureDc,
                uiFontSemibold_ ? uiFontSemibold_ :
                static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT))));
        }

        for (size_t index = 0; index < kTitleButtons.size(); ++index) {
            const ButtonDefinition& spec = kTitleButtons[index];
            int measuredWidth = DipToPx(spec.minimumWidthDip);
            if (measureDc) {
                const auto control = controls_.find(spec.id);
                if (control != controls_.end()) {
                    wchar_t caption[160]{};
                    const int length = GetWindowTextW(control->second, caption,
                        static_cast<int>(std::size(caption)));
                    SIZE extent{};
                    if (length > 0 && GetTextExtentPoint32W(measureDc, caption, length, &extent)) {
                        // Match PaintButton title padding + popup chevron reserve.
                        const int sidePadding = DipToPx(kTitleButtonTextPaddingDip) * 2;
                        const int popupAllowance = ButtonHasPopupMenu(spec.id)
                            ? DipToPx(kButtonPopupChevronDip) : 0;
                        measuredWidth = std::max(measuredWidth,
                            static_cast<int>(extent.cx) + sidePadding + popupAllowance);
                    }
                }
            }
            widths[index] = measuredWidth;
        }

        if (measureDc) {
            if (oldFont) SelectObject(measureDc, oldFont);
            ReleaseDC(hwnd_, measureDc);
        }
        return widths;
    }


    int QuickSiftApplicationImpl::TitleBarRowCountForWidth(int width) const {
        if (width <= 0) return 1;
        const int outer = DipToPx(kOuterMarginDip);
        const int gap = DipToPx(kTitleControlGapDip);
        const int themeWidth = DipToPx(92);
        const auto controlWidths = MeasuredTitleButtonWidthsPx();
        int row = 0;
        int rowStart = DipToPx(158);
        int rowLimit = std::max(rowStart, width - outer - themeWidth - gap);
        int x = rowStart;
        const bool singleOrCompare = reviewState_.Mode() == ViewMode::Single || reviewState_.Mode() == ViewMode::Compare;
        for (size_t index = 0; index < kTitleButtons.size(); ++index) {
            const auto& spec = kTitleButtons[index];
            if ((spec.id == ID_SORT || spec.id == ID_THUMB_SIZE) && singleOrCompare) continue;
            if ((spec.id == ID_ZOOM_MODE || spec.id == ID_ROTATE_LEFT || spec.id == ID_ROTATE_RIGHT) && !singleOrCompare) continue;
            const int controlWidth = controlWidths[index];
            if (x > rowStart && x + controlWidth > rowLimit) {
                ++row;
                rowStart = outer;
                rowLimit = std::max(rowStart, width - outer);
                x = rowStart;
            }
            x += controlWidth + gap;
        }
        return row + 1;
    }


    int QuickSiftApplicationImpl::NominalTitleBarHeightPxForWidth(int width) const {
        const int rows = TitleBarRowCountForWidth(width);
        return DipToPx(kTitleBarHeightDip) +
            std::max(0, rows - 1) * DipToPx(kTitleBarExtraRowHeightDip) +
            (rows > 1 ? DipToPx(kTitleBarBottomPaddingDip) : 0);
    }


    int QuickSiftApplicationImpl::TitleBarHeightPxForWidth(int width) const {
        // Fullscreen chrome is a sibling overlay. It never contributes to the
        // content layout, whether revealed or retracted.
        if (fullscreen_) return 0;
        return NominalTitleBarHeightPxForWidth(width);
    }


    int QuickSiftApplicationImpl::CurrentTitleBarHeightPx() const {
        RECT client{};
        if (hwnd_) GetClientRect(hwnd_, &client);
        return TitleBarHeightPxForWidth(client.right - client.left);
    }


    RECT QuickSiftApplicationImpl::TitleBarRect() const {
        RECT client{};
        if (hwnd_) GetClientRect(hwnd_, &client);
        const LONG titleBottom = static_cast<LONG>(
            TitleBarHeightPxForWidth(client.right - client.left));
        if (client.bottom > titleBottom) client.bottom = titleBottom;
        return client;
    }


    RECT QuickSiftApplicationImpl::TrafficLightRect(TrafficLight light) const {
        const int diameter = DipToPx(12);
        const int gap = DipToPx(8);
        const int firstLeft = DipToPx(15);
        const int top = (DipToPx(kTitleBarHeightDip) - diameter) / 2;
        int index = 0;
        if (light == TrafficLight::Minimize) index = 1;
        else if (light == TrafficLight::Zoom) index = 2;
        else if (light != TrafficLight::Close) return {};
        const int left = firstLeft + index * (diameter + gap);
        return { left, top, left + diameter, top + diameter };
    }


    TrafficLight QuickSiftApplicationImpl::HitTrafficLight(POINT point) const {
        if (fullscreen_ && !fullscreenTitleVisible_) return TrafficLight::None;
        for (TrafficLight light : { TrafficLight::Close, TrafficLight::Minimize, TrafficLight::Zoom }) {
            RECT rect = TrafficLightRect(light);
            InflateRect(&rect, DipToPx(4), DipToPx(5));
            if (PtInRect(&rect, point)) return light;
        }
        return TrafficLight::None;
    }


    bool QuickSiftApplicationImpl::IsBrowseControlId(int id) const noexcept {
        return id == ID_HELP || id == ID_THEME_TOGGLE || ContainsControlId(kTitleButtons, id);
    }


    bool QuickSiftApplicationImpl::IsPointOverTitleControl(POINT clientPoint) const {
        for (const auto& [id, control] : controls_) {
            if (!IsBrowseControlId(id) || !IsWindowVisible(control)) continue;
            RECT rect{};
            GetWindowRect(control, &rect);
            MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&rect), 2);
            if (PtInRect(&rect, clientPoint)) return true;
        }
        return false;
    }


    void QuickSiftApplicationImpl::InvalidateTitleBar() {
        if (fullscreen_ && titleOverlay_) {
            InvalidateRect(titleOverlay_, nullptr, FALSE);
            return;
        }
        const RECT rect = TitleBarRect();
        InvalidateRect(hwnd_, &rect, FALSE);
    }


    void QuickSiftApplicationImpl::UpdateTrafficHover(POINT point) {
        const TrafficLight next = HitTrafficLight(point);
        if (next == hoveredTrafficLight_) return;
        hoveredTrafficLight_ = next;
        InvalidateTitleBar();
    }


    void QuickSiftApplicationImpl::PerformTrafficAction(TrafficLight light) {
        switch (light) {
        case TrafficLight::Close:
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            break;
        case TrafficLight::Minimize:
            ShowWindow(hwnd_, SW_MINIMIZE);
            break;
        case TrafficLight::Zoom:
            ToggleMaximizeRestore();
            break;
        default:
            break;
        }
    }


    void QuickSiftApplicationImpl::ToggleMaximizeRestore() {
        if (fullscreen_) {
            ToggleFullscreen();
            return;
        }
        ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
    }


    void QuickSiftApplicationImpl::RelayoutWindowChrome() {
        if (!hwnd_ || !canvas_) return;
        RECT client{};
        if (!GetClientRect(hwnd_, &client)) return;
        LayoutControls(client.right - client.left, client.bottom - client.top);
        InvalidateRect(hwnd_, nullptr, TRUE);
        InvalidateCanvas();
    }


    void QuickSiftApplicationImpl::SetFullscreenTitleVisible(bool visible) {
        if (!fullscreen_ || fullscreenTitleVisible_ == visible) return;
        fullscreenTitleVisible_ = visible;
        fullscreenEdgeHoverSince_ = {};
        fullscreenLeaveSince_ = {};
        // Mouse-leave tracking belongs to a specific HWND. Reset it whenever
        // ownership switches between the main window and the overlay host.
        trackingTitleMouse_ = false;
        if (!visible) {
            hoveredTrafficLight_ = TrafficLight::None;
            pressedTrafficLight_ = TrafficLight::None;
            trackingTitleMouse_ = false;
            HWND focused = GetFocus();
            if (focused && IsChild(hwnd_, focused) && IsBrowseControlId(GetDlgCtrlID(focused)) && canvas_) {
                SetFocus(canvas_);
            }
            HWND capture = GetCapture();
            if (capture == hwnd_ || (capture && IsChild(hwnd_, capture))) ReleaseCapture();
        }

        // Only the overlay chrome changes here. The canvas and flyout geometry
        // deliberately remain untouched so revealing fullscreen controls cannot
        // resize, re-decode, or jump the current image.
        RECT client{};
        if (GetClientRect(hwnd_, &client)) {
            LayoutTitleControls(client.right - client.left);
            RaiseFullscreenTitleOverlay();
        }
        InvalidateTitleBar();
    }


    void QuickSiftApplicationImpl::EnterFullscreen() {
        if (fullscreen_ || !hwnd_) return;

        WINDOWPLACEMENT placement{ sizeof(WINDOWPLACEMENT) };
        if (!GetWindowPlacement(hwnd_, &placement)) return;
        preFullscreenPlacement_ = placement;
        preFullscreenStyle_ = GetWindowLongPtrW(hwnd_, GWL_STYLE);
        preFullscreenExStyle_ = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);

        MONITORINFO monitorInfo{ sizeof(MONITORINFO) };
        if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitorInfo)) return;

        if (IsZoomed(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
        fullscreen_ = true;
        fullscreenTitleVisible_ = false;
        fullscreenEdgeHoverSince_ = {};
        fullscreenLeaveSince_ = {};
        trackingTitleMouse_ = false;
        hoveredTrafficLight_ = TrafficLight::None;
        pressedTrafficLight_ = TrafficLight::None;
        // Route through SetActiveFlyout so section children are SW_HIDE'd consistently.
        if (activeFlyout_ != FlyoutPanel::None) SetActiveFlyout(FlyoutPanel::None);

        const LONG_PTR fullscreenStyle = preFullscreenStyle_ &
            ~static_cast<LONG_PTR>(WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        SetWindowLongPtrW(hwnd_, GWL_STYLE, fullscreenStyle);
        const int noRoundCorners = 1; // DWMWCP_DONOTROUND on Windows 11.
        DwmSetWindowAttribute(hwnd_, 33, &noRoundCorners, sizeof(noRoundCorners));

        const RECT& monitor = monitorInfo.rcMonitor;
        SetWindowPos(hwnd_, HWND_TOP, monitor.left, monitor.top,
            monitor.right - monitor.left, monitor.bottom - monitor.top,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
        UpdateFullscreenButton();
        RelayoutWindowChrome();
        if (canvas_) SetFocus(canvas_);
    }


    void QuickSiftApplicationImpl::ExitFullscreen() {
        if (!fullscreen_ || !hwnd_) return;

        fullscreen_ = false;
        fullscreenTitleVisible_ = true;
        fullscreenEdgeHoverSince_ = {};
        fullscreenLeaveSince_ = {};
        trackingTitleMouse_ = false;
        hoveredTrafficLight_ = TrafficLight::None;
        pressedTrafficLight_ = TrafficLight::None;
        SetWindowLongPtrW(hwnd_, GWL_STYLE, preFullscreenStyle_);
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, preFullscreenExStyle_);
        const int roundedCorners = 2; // DWMWCP_ROUND on Windows 11.
        DwmSetWindowAttribute(hwnd_, 33, &roundedCorners, sizeof(roundedCorners));

        preFullscreenPlacement_.length = sizeof(WINDOWPLACEMENT);
        SetWindowPlacement(hwnd_, &preFullscreenPlacement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateFullscreenButton();
        RelayoutWindowChrome();
        if (canvas_) SetFocus(canvas_);
    }


    void QuickSiftApplicationImpl::ToggleFullscreen() {
        if (fullscreen_) ExitFullscreen();
        else EnterFullscreen();
    }


    void QuickSiftApplicationImpl::UpdateFullscreenTitleFromCursor() {
        if (!fullscreen_ || !hwnd_ || !IsWindowVisible(hwnd_) || IsIconic(hwnd_) ||
            (!windowActive_ && !menuOpen_)) return;

        POINT screenPoint{};
        if (!GetCursorPos(&screenPoint)) return;
        RECT windowRect{};
        if (!GetWindowRect(hwnd_, &windowRect)) return;
        const auto now = std::chrono::steady_clock::now();
        const int revealDepth = std::max(4, DipToPx(kFullscreenEdgeRevealDip));
        const bool overTopEdge = screenPoint.x >= windowRect.left && screenPoint.x < windowRect.right &&
            screenPoint.y >= windowRect.top && screenPoint.y <= windowRect.top + revealDepth;

        if (!fullscreenTitleVisible_) {
            fullscreenLeaveSince_ = {};
            if (!overTopEdge) {
                fullscreenEdgeHoverSince_ = {};
                return;
            }
            if (fullscreenEdgeHoverSince_ == std::chrono::steady_clock::time_point{}) {
                fullscreenEdgeHoverSince_ = now;
                return;
            }
            if (now - fullscreenEdgeHoverSince_ >= std::chrono::milliseconds(110)) {
                SetFullscreenTitleVisible(true);
            }
            return;
        }

        fullscreenEdgeHoverSince_ = {};
        POINT clientPoint = screenPoint;
        ScreenToClient(hwnd_, &clientPoint);
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int keepAliveBottom = NominalTitleBarHeightPxForWidth(client.right - client.left) +
            DipToPx(kFullscreenKeepAliveDip);
        const bool overTitleArea = clientPoint.x >= 0 && clientPoint.x < client.right &&
            clientPoint.y >= 0 && clientPoint.y <= keepAliveBottom;
        const HWND capture = GetCapture();
        const bool appOwnsCapture = capture == hwnd_ || (capture && IsChild(hwnd_, capture));
        if (overTopEdge || overTitleArea || menuOpen_ || appOwnsCapture) {
            fullscreenLeaveSince_ = {};
            return;
        }
        if (fullscreenLeaveSince_ == std::chrono::steady_clock::time_point{}) {
            fullscreenLeaveSince_ = now;
            return;
        }
        if (now - fullscreenLeaveSince_ >= std::chrono::milliseconds(420)) {
            SetFullscreenTitleVisible(false);
        }
    }


    void QuickSiftApplicationImpl::ShowWindowSystemMenu(POINT screenPoint) {
        HMENU menu = GetSystemMenu(hwnd_, FALSE);
        if (!menu) return;
        EnableMenuItem(menu, SC_RESTORE, MF_BYCOMMAND | (IsZoomed(hwnd_) ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, SC_MAXIMIZE, MF_BYCOMMAND | (IsZoomed(hwnd_) ? MF_GRAYED : MF_ENABLED));
        const quicksift::core::ScopedBooleanFlag menuGuard(menuOpen_);
        fullscreenLeaveSince_ = {};
        const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
            screenPoint.x, screenPoint.y, 0, hwnd_, nullptr);
        fullscreenLeaveSince_ = {};
        if (command) PostMessageW(hwnd_, WM_SYSCOMMAND, static_cast<WPARAM>(command), 0);
    }


    int QuickSiftApplicationImpl::ResizeBorderThicknessPx() const {
        return std::max(DipToPx(7), 4);
    }


    LRESULT QuickSiftApplicationImpl::HitTestMainWindow(LPARAM lParam) {
        POINT screenPoint{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT windowRect{};
        GetWindowRect(hwnd_, &windowRect);

        if (!fullscreen_ && !IsZoomed(hwnd_)) {
            const int border = ResizeBorderThicknessPx();
            const bool left = screenPoint.x < windowRect.left + border;
            const bool right = screenPoint.x >= windowRect.right - border;
            const bool top = screenPoint.y < windowRect.top + border;
            const bool bottom = screenPoint.y >= windowRect.bottom - border;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }

        POINT clientPoint = screenPoint;
        ScreenToClient(hwnd_, &clientPoint);
        if (HitTrafficLight(clientPoint) != TrafficLight::None || IsPointOverTitleControl(clientPoint)) return HTCLIENT;
        if (clientPoint.y >= 0 && clientPoint.y < CurrentTitleBarHeightPx()) return HTCAPTION;
        return HTCLIENT;
    }


    void QuickSiftApplicationImpl::DrawMacTitleBar(HDC dc, const RECT& client) {
        const int width = std::max(1, static_cast<int>(client.right - client.left));
        quicksift::ui::framework::TitleBarPresentation presentation{};
        presentation.title = L"QuickSift";
        presentation.titleTextRect = { DipToPx(82), 0, DipToPx(151),
            DipToPx(kTitleBarHeightDip) };
        presentation.titleFont = uiTitleFont_;
        presentation.active = windowActive_;
        const std::array<TrafficLight, 3> lights{ TrafficLight::Close,
            TrafficLight::Minimize, TrafficLight::Zoom };
        const std::array<COLORREF, 3> colors{ RGB(255, 95, 87),
            RGB(255, 189, 46), RGB(40, 200, 64) };
        for (std::size_t index = 0; index < lights.size(); ++index) {
            presentation.trafficLights[index].rect = TrafficLightRect(lights[index]);
            presentation.trafficLights[index].color = colors[index];
            presentation.trafficLights[index].hot = hoveredTrafficLight_ == lights[index];
            presentation.trafficLights[index].pressed = pressedTrafficLight_ == lights[index];
        }
        uiFramework_.PaintTitleBar(dc, client,
            NominalTitleBarHeightPxForWidth(width), presentation, Palette(), currentDpi_);
    }


    VisibleTreeMetrics QuickSiftApplicationImpl::MeasureVisibleTree(HWND tree) const {
        VisibleTreeMetrics metrics;
        const HTREEITEM firstVisible = TreeView_GetFirstVisible(tree);
        for (HTREEITEM item = TreeView_GetRoot(tree); item; item = TreeView_GetNextVisible(tree, item)) {
            if (item == firstVisible) metrics.firstIndex = metrics.count;
            ++metrics.count;
        }
        return metrics;
    }


    HTREEITEM QuickSiftApplicationImpl::VisibleTreeItemAt(HWND tree, int requestedIndex) const {
        if (requestedIndex < 0) return nullptr;
        int index = 0;
        for (HTREEITEM item = TreeView_GetRoot(tree); item; item = TreeView_GetNextVisible(tree, item), ++index) {
            if (index == requestedIndex) return item;
        }
        return nullptr;
    }


    RECT QuickSiftApplicationImpl::TreeScrollbarThumbRect(HWND tree, const VisibleTreeMetrics* knownMetrics ) const {
        RECT client{};
        GetClientRect(tree, &client);
        const int margin = DipToPx(4);
        const int width = DipToPx(5);
        RECT empty{};
        const VisibleTreeMetrics measured = knownMetrics ? *knownMetrics : MeasureVisibleTree(tree);
        if (measured.count <= 0) return empty;
        const int itemHeight = std::max(1, static_cast<int>(SendMessageW(tree, TVM_GETITEMHEIGHT, 0, 0)));
        const int visibleSlots = std::max(1, static_cast<int>(client.bottom - client.top) / itemHeight);
        if (measured.count <= visibleSlots) return empty;
        const int trackTop = client.top + margin;
        const int trackBottom = client.bottom - margin;
        const int trackHeight = std::max(1, trackBottom - trackTop);
        const int thumbHeight = std::clamp(
            MulDiv(trackHeight, visibleSlots, measured.count), DipToPx(28), trackHeight);
        const int maxFirst = std::max(1, measured.count - visibleSlots);
        const int travel = std::max(0, trackHeight - thumbHeight);
        const int top = trackTop + MulDiv(travel, std::min(measured.firstIndex, maxFirst), maxFirst);
        return { client.right - margin - width, top, client.right - margin, top + thumbHeight };
    }


    void QuickSiftApplicationImpl::DrawTreeOverlayScrollbar(HWND tree) {
        RECT thumb = TreeScrollbarThumbRect(tree);
        if (IsRectEmpty(&thumb)) return;
        HDC dc = GetDC(tree);
        if (!dc) return;
        RECT track = thumb;
        RECT client{};
        GetClientRect(tree, &client);
        track.top = DipToPx(4);
        track.bottom = client.bottom - DipToPx(4);
        uiFramework_.PaintOverlayScrollbar(dc, thumb, track, treeScrollbarHot_,
            treeScrollDragging_, Palette(), currentDpi_);
        ReleaseDC(tree, dc);
    }


    void QuickSiftApplicationImpl::DragTreeScrollbar(int mouseY) {
        if (!tree_) return;
        const VisibleTreeMetrics metrics = MeasureVisibleTree(tree_);
        if (metrics.count <= 0) return;
        RECT client{};
        GetClientRect(tree_, &client);
        const int itemHeight = std::max(1, static_cast<int>(SendMessageW(tree_, TVM_GETITEMHEIGHT, 0, 0)));
        const int visibleSlots = std::max(1, static_cast<int>(client.bottom - client.top) / itemHeight);
        if (metrics.count <= visibleSlots) return;
        RECT thumb = TreeScrollbarThumbRect(tree_, &metrics);
        const int trackTop = DipToPx(4);
        const int trackBottom = client.bottom - DipToPx(4);
        const int travel = std::max(1, static_cast<int>(trackBottom - trackTop - (thumb.bottom - thumb.top)));
        const int desiredTop = std::clamp(mouseY - treeScrollDragOffset_, trackTop, trackTop + travel);
        const float ratio = static_cast<float>(desiredTop - trackTop) / static_cast<float>(travel);
        const int maxFirst = std::max(0, metrics.count - visibleSlots);
        const int index = std::clamp(static_cast<int>(std::lround(ratio * maxFirst)), 0, maxFirst);
        if (const HTREEITEM target = VisibleTreeItemAt(tree_, index)) {
            SendMessageW(tree_, TVM_SELECTITEM, TVGN_FIRSTVISIBLE, reinterpret_cast<LPARAM>(target));
        }
        InvalidateRect(tree_, nullptr, FALSE);
    }


    void QuickSiftApplicationImpl::ReportUiCallbackException(HWND hwnd, UINT message, std::wstring_view callbackName) noexcept {
        quicksift::diagnostics::Write(quicksift::diagnostics::Level::Critical, callbackName,
            L"Unhandled C++ exception escaped a Windows UI callback");
        if (message == WM_NCCREATE || message == WM_CLOSE ||
            message == WM_DESTROY || message == WM_NCDESTROY) return;
        HWND root = GetAncestor(hwnd, GA_ROOT);
        if (!root) root = hwnd;
        if (root && IsWindow(root)) PostMessageW(root, WM_CLOSE, 0, 0);
    }


    LRESULT CALLBACK QuickSiftApplicationImpl::TitleOverlaySubclassProcImpl(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData) {
        (void)subclassId;
        QuickSiftApplicationImpl* app = reinterpret_cast<QuickSiftApplicationImpl*>(referenceData);
        if (!app) return DefSubclassProc(hwnd, message, wParam, lParam);

        switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_NCHITTEST:
            return HTCLIENT;
        case WM_MOUSEMOVE: {
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            app->UpdateTrafficHover(point);
            if (!app->trackingTitleMouse_) {
                TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                app->trackingTitleMouse_ = TrackMouseEvent(&track) != FALSE;
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            app->trackingTitleMouse_ = false;
            if (app->pressedTrafficLight_ == TrafficLight::None) {
                app->hoveredTrafficLight_ = TrafficLight::None;
                app->InvalidateTitleBar();
            }
            return 0;
        case WM_LBUTTONDOWN: {
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const TrafficLight light = app->HitTrafficLight(point);
            if (light != TrafficLight::None) {
                app->pressedTrafficLight_ = light;
                app->hoveredTrafficLight_ = light;
                SetCapture(hwnd);
                app->InvalidateTitleBar();
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (app->pressedTrafficLight_ != TrafficLight::None) {
                POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                const TrafficLight pressed = app->pressedTrafficLight_;
                const TrafficLight releasedOver = app->HitTrafficLight(point);
                app->pressedTrafficLight_ = TrafficLight::None;
                if (GetCapture() == hwnd) ReleaseCapture();
                app->InvalidateTitleBar();
                if (pressed == releasedOver) app->PerformTrafficAction(pressed);
            }
            return 0;
        case WM_LBUTTONDBLCLK:
            app->ToggleFullscreen();
            return 0;
        case WM_RBUTTONUP: {
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &point);
            app->ShowWindowSystemMenu(point);
            return 0;
        }
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
            app->pressedTrafficLight_ = TrafficLight::None;
            app->InvalidateTitleBar();
            return 0;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, TitleOverlaySubclassProc, kTitleOverlaySubclassId);
            break;
        default:
            break;
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }


    LRESULT CALLBACK QuickSiftApplicationImpl::TreeSubclassProcImpl(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData) {
        (void)subclassId;
        QuickSiftApplicationImpl* app = reinterpret_cast<QuickSiftApplicationImpl*>(referenceData);
        if (!app) return DefSubclassProc(hwnd, message, wParam, lParam);
        switch (message) {
        case WM_MOUSEMOVE: {
            app->treeScrollbarHot_ = true;
            if (!app->trackingTreeMouse_) {
                TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                app->trackingTreeMouse_ = TrackMouseEvent(&track) != FALSE;
            }
            if (app->treeScrollDragging_) {
                app->DragTreeScrollbar(GET_Y_LPARAM(lParam));
                return 0;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        case WM_MOUSELEAVE:
            app->trackingTreeMouse_ = false;
            if (!app->treeScrollDragging_) app->treeScrollbarHot_ = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_LBUTTONDOWN: {
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT thumb = app->TreeScrollbarThumbRect(hwnd);
            if (!IsRectEmpty(&thumb) && PtInRect(&thumb, point)) {
                app->treeScrollDragging_ = true;
                app->treeScrollDragOffset_ = point.y - thumb.top;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }
        case WM_LBUTTONUP:
            if (app->treeScrollDragging_) {
                app->treeScrollDragging_ = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
            app->treeScrollDragging_ = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_STYLECHANGING:
            if (wParam == static_cast<WPARAM>(GWL_STYLE)) {
                auto* styles = reinterpret_cast<STYLESTRUCT*>(lParam);
                styles->styleNew &= ~static_cast<DWORD>(WS_VSCROLL);
            }
            break;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const VisibleTreeMetrics metrics = app->MeasureVisibleTree(hwnd);
            if (metrics.count > 0) {
                UINT wheelLines = 3;
                SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &wheelLines, 0);
                if (wheelLines == WHEEL_PAGESCROLL) {
                    RECT client{};
                    GetClientRect(hwnd, &client);
                    const int itemHeight = std::max(1,
                        static_cast<int>(SendMessageW(hwnd, TVM_GETITEMHEIGHT, 0, 0)));
                    wheelLines = static_cast<UINT>(std::max(1,
                        static_cast<int>(client.bottom - client.top) / itemHeight));
                }

                const int notches = std::max(1, std::abs(delta) / WHEEL_DELTA);
                const int direction = delta > 0 ? -1 : 1;
                const int stepCount = static_cast<int>(wheelLines) * notches;
                const int targetIndex = std::clamp(
                    metrics.firstIndex + direction * stepCount, 0, metrics.count - 1);
                if (const HTREEITEM target = app->VisibleTreeItemAt(hwnd, targetIndex)) {
                    SendMessageW(hwnd, TVM_SELECTITEM, TVGN_FIRSTVISIBLE,
                        reinterpret_cast<LPARAM>(target));
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_PAINT: {
            ShowScrollBar(hwnd, SB_VERT, FALSE);
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            ShowScrollBar(hwnd, SB_VERT, FALSE);
            app->DrawTreeOverlayScrollbar(hwnd);
            return result;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, TreeSubclassProc, kFolderTreeSubclassId);
            break;
        default:
            break;
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }


LRESULT CALLBACK QuickSiftApplicationImpl::MainWndProcImpl(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        QuickSiftApplicationImpl* app = reinterpret_cast<QuickSiftApplicationImpl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCT*>(lParam);
            app = static_cast<QuickSiftApplicationImpl*>(create->lpCreateParams);
            app->hwnd_ = hwnd;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        return app ? app->HandleMainMessage(message, wParam, lParam) : DefWindowProc(hwnd, message, wParam, lParam);
    }


    LRESULT CALLBACK QuickSiftApplicationImpl::CanvasWndProcImpl(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        QuickSiftApplicationImpl* app = reinterpret_cast<QuickSiftApplicationImpl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCT*>(lParam);
            app = static_cast<QuickSiftApplicationImpl*>(create->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        return app ? app->HandleCanvasMessage(hwnd, message, wParam, lParam) : DefWindowProc(hwnd, message, wParam, lParam);
    }


    LRESULT CALLBACK QuickSiftApplicationImpl::TitleOverlaySubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData) {
        try {
            return TitleOverlaySubclassProcImpl(hwnd, message, wParam, lParam, subclassId, referenceData);
        } catch (...) {
            ReportUiCallbackException(hwnd, message, L"TitleOverlaySubclassProc");
            if (message == WM_NCDESTROY) {
                RemoveWindowSubclass(hwnd, TitleOverlaySubclassProc, kTitleOverlaySubclassId);
            }
            return 0;
        }
    }


    LRESULT CALLBACK QuickSiftApplicationImpl::TreeSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR subclassId, DWORD_PTR referenceData) {
        try {
            return TreeSubclassProcImpl(hwnd, message, wParam, lParam, subclassId, referenceData);
        } catch (...) {
            ReportUiCallbackException(hwnd, message, L"TreeSubclassProc");
            if (message == WM_NCDESTROY) {
                RemoveWindowSubclass(hwnd, TreeSubclassProc, kFolderTreeSubclassId);
            }
            return 0;
        }
    }


LRESULT CALLBACK QuickSiftApplicationImpl::MainWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        try {
            return MainWndProcImpl(hwnd, message, wParam, lParam);
        } catch (...) {
            ReportUiCallbackException(hwnd, message, L"MainWndProc");
            if (message == WM_NCCREATE) return FALSE;
            if (message == WM_CREATE) return -1;
            if (message == WM_CLOSE && IsWindow(hwnd)) DestroyWindow(hwnd);
            if (message == WM_DESTROY) PostQuitMessage(1);
            return 0;
        }
    }


    LRESULT CALLBACK QuickSiftApplicationImpl::CanvasWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        try {
            return CanvasWndProcImpl(hwnd, message, wParam, lParam);
        } catch (...) {
            ReportUiCallbackException(hwnd, message, L"CanvasWndProc");
            return message == WM_NCCREATE ? FALSE : (message == WM_CREATE ? -1 : 0);
        }
    }



    LRESULT QuickSiftApplicationImpl::HandleMainMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            return OnCreate();
        case WM_PAINT:
            PaintMainWindow();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_NCCALCSIZE:
            // Make the full window rectangle client area. This is the key
            // difference between a real custom title bar and merely hiding
            // WS_CAPTION: Windows has no native caption left to repaint.
            if (wParam) return 0;
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        case WM_NCPAINT:
            return 0;
        case WM_NCACTIVATE:
            // Suppress the legacy active/inactive frame flash. The custom
            // title bar handles its own activation appearance in WM_ACTIVATE.
            return TRUE;
        case WM_NCHITTEST:
            return HitTestMainWindow(lParam);
        case WM_NCLBUTTONDBLCLK:
            if (wParam == HTCAPTION) {
                ToggleMaximizeRestore();
                return 0;
            }
            return DefWindowProc(hwnd_, message, wParam, lParam);
        case WM_NCRBUTTONUP:
            if (wParam == HTCAPTION) {
                POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ShowWindowSystemMenu(point);
                return 0;
            }
            return DefWindowProc(hwnd_, message, wParam, lParam);
        case WM_ACTIVATE:
            windowActive_ = LOWORD(wParam) != WA_INACTIVE || menuOpen_;
            if (fullscreen_ && LOWORD(wParam) == WA_INACTIVE && !menuOpen_) {
                SetFullscreenTitleVisible(false);
            }
            InvalidateTitleBar();
            return DefWindowProc(hwnd_, message, wParam, lParam);
        case WM_MOUSEMOVE: {
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            UpdateTrafficHover(point);
            if (!trackingTitleMouse_) {
                TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd_, 0 };
                trackingTitleMouse_ = TrackMouseEvent(&track) != FALSE;
            }
            return 0;
        }
        case WM_NCMOUSEMOVE:
            if (hoveredTrafficLight_ != TrafficLight::None) {
                hoveredTrafficLight_ = TrafficLight::None;
                InvalidateTitleBar();
            }
            return DefWindowProc(hwnd_, message, wParam, lParam);
        case WM_MOUSELEAVE:
            trackingTitleMouse_ = false;
            if (pressedTrafficLight_ == TrafficLight::None) {
                hoveredTrafficLight_ = TrafficLight::None;
                InvalidateTitleBar();
            }
            return 0;
        case WM_LBUTTONDOWN: {
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const TrafficLight light = HitTrafficLight(point);
            if (light != TrafficLight::None) {
                pressedTrafficLight_ = light;
                hoveredTrafficLight_ = light;
                SetCapture(hwnd_);
                InvalidateTitleBar();
                return 0;
            }
            return DefWindowProc(hwnd_, message, wParam, lParam);
        }
        case WM_LBUTTONUP:
            if (pressedTrafficLight_ != TrafficLight::None) {
                POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                const TrafficLight pressed = pressedTrafficLight_;
                const TrafficLight releasedOver = HitTrafficLight(point);
                pressedTrafficLight_ = TrafficLight::None;
                if (GetCapture() == hwnd_) ReleaseCapture();
                InvalidateTitleBar();
                if (pressed == releasedOver) PerformTrafficAction(pressed);
                return 0;
            }
            return DefWindowProc(hwnd_, message, wParam, lParam);
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
            pressedTrafficLight_ = TrafficLight::None;
            InvalidateTitleBar();
            return 0;
        case WM_SIZE:
            LayoutControls(LOWORD(lParam), HIWORD(lParam));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED:
            OnDpiChanged(HIWORD(wParam), reinterpret_cast<RECT*>(lParam));
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            if (!info) return 0;

            MONITORINFO monitorInfo{ sizeof(MONITORINFO) };
            const HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
            if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) {
                monitorInfo.rcMonitor = { 0, 0,
                    std::max(1, GetSystemMetrics(SM_CXSCREEN)),
                    std::max(1, GetSystemMetrics(SM_CYSCREEN)) };
                monitorInfo.rcWork = monitorInfo.rcMonitor;
                SystemParametersInfoW(SPI_GETWORKAREA, 0, &monitorInfo.rcWork, 0);
                QS_LOG_WARNING(L"UI", L"Window sizing is using primary-screen geometry because monitor information was unavailable");
            }

            const LONG workWidth = std::max<LONG>(1, monitorInfo.rcWork.right - monitorInfo.rcWork.left);
            const LONG workHeight = std::max<LONG>(1, monitorInfo.rcWork.bottom - monitorInfo.rcWork.top);
            info->ptMaxPosition.x = monitorInfo.rcWork.left - monitorInfo.rcMonitor.left;
            info->ptMaxPosition.y = monitorInfo.rcWork.top - monitorInfo.rcMonitor.top;
            info->ptMaxSize.x = workWidth;
            info->ptMaxSize.y = workHeight;
            info->ptMinTrackSize.x = std::min<LONG>(static_cast<LONG>(DipToPx(1120)), workWidth);
            info->ptMinTrackSize.y = std::min<LONG>(static_cast<LONG>(DipToPx(520)), workHeight);
            return 0;
        }
        case WM_DRAWITEM:
            return OnDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        case WM_COMMAND:
            OnCommand(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
            return 0;
        case WM_NOTIFY:
            return OnNotify(reinterpret_cast<NMHDR*>(lParam));
        case WM_APP_BACKGROUND_COMPLETION:
            // A scrollbar drag owns the UI thread. Background completion delivery
            // is intentionally deferred until the gesture ends; otherwise scan
            // batches / metadata / decode completions can steal input latency even
            // when decoding itself is entirely off-thread.
            if (!ThumbnailScrollPrefersPlaceholders()) DrainBackgroundCompletions();
            return 0;
        case WM_APP_FILE_TRANSACTION_COMPLETION:
            DrainFileTransactionResults();
            return 0;
        case WM_APP_METADATA_TRANSACTION_COMPLETION:
            DrainMetadataTransactionResults();
            return 0;
        case WM_APP_RESOURCE_PRESSURE:
            OnResourcePressure(static_cast<quicksift::ResourcePressureEvent>(wParam));
            return 0;
        case WM_APP_STARTUP_READY:
            HandleDeferredStartupReady(wParam != 0);
            return 0;
        case WM_TIMER:
            OnTimer(static_cast<UINT_PTR>(wParam));
            return 0;
        case WM_CLOSE:
            if (transactions_.Busy()) {
                if (closingAfterTransactionCancellation_) {
                    ShowToast(L"Cancelling file operations at a safe boundary before closing…");
                    return 0;
                }
                ShowGlassConfirmation(
                    L"File operations are still running",
                    L"QuickSift is still changing files. Closing anyway will cancel pending operations at the next safe file boundary. Already completed and verified files will remain complete; QuickSift will not stop in the middle of an atomic media group.",
                    L"Close anyway", PendingGlassAction::CloseWithPendingOperations,
                    GlassAlertKind::Warning, L"Wait");
                return 0;
            }
            SaveSession();
            DestroyWindow(hwnd_);
            return 0;
        case WM_QUERYENDSESSION:
            SaveSession();
            // A shutdown query is not a normal close request. Ask active work to
            // stop at its next safe boundary, but do not auto-close QuickSift if
            // this shutdown attempt is vetoed or cancelled by another process.
            transactions_.CancelAll();
            if (transactions_.Busy()) {
                // An Exiv2 write cannot be interrupted safely once it has entered
                // the library. Veto this shutdown attempt and let the user retry
                // after the serialized transaction reaches its safe boundary.
                const wchar_t* reason =
                    L"QuickSift is finishing a verified media transaction.";
                if (ShutdownBlockReasonCreate(hwnd_, reason) != FALSE) {
                    shutdownBlocked_ = true;
                }
                return FALSE;
            }
            return TRUE;
        case WM_ENDSESSION:
            if (shutdownBlocked_) {
                ShutdownBlockReasonDestroy(hwnd_);
                shutdownBlocked_ = false;
            }
            if (wParam != FALSE) {
                SaveSession();
                DestroyWindow(hwnd_);
            } else {
                // Another application vetoed shutdown. Re-open the coordinator
                // so QuickSift does not remain permanently read-only afterward.
                transactions_.CancelCloseRequest();
                closingAfterTransactionCancellation_ = false;
            }
            return 0;
        case WM_DESTROY: {
            if (shutdownBlocked_) {
                ShutdownBlockReasonDestroy(hwnd_);
                shutdownBlocked_ = false;
            }
            const auto shutdownStartedAt = std::chrono::steady_clock::now();
            QS_LOG_INFO(L"Lifecycle", L"Shutdown sequence started after main window destruction");
            KillTimer(hwnd_, ID_TIMER_IDLE_QUALITY);
            KillTimer(hwnd_, ID_TIMER_CACHE_FLUSH);
            KillTimer(hwnd_, ID_TIMER_AUTOHIDE);
            KillTimer(hwnd_, ID_TIMER_TOAST);
            KillTimer(hwnd_, ID_TIMER_METADATA_UI);
            KillTimer(hwnd_, ID_TIMER_VIEW_RESOURCE_TRIM);
            KillTimer(hwnd_, ID_TIMER_BACKGROUND_ANALYSIS);
            KillTimer(hwnd_, ID_TIMER_FOLDER_SELECTION);
            KillTimer(hwnd_, ID_TIMER_FILE_PROGRESS);
            KillTimer(hwnd_, ID_TIMER_PERFORMANCE_HUD);
            KillTimer(hwnd_, ID_TIMER_THUMBNAIL_RETRY_COOLDOWN);
            KillTimer(hwnd_, ID_TIMER_ANIMATION);
            KillTimer(hwnd_, ID_TIMER_DRAG_RENDER);
            KillTimer(hwnd_, ID_TIMER_SCROLL_FRAME);
            KillTimer(hwnd_, ID_TIMER_SCROLL_SETTLE);
            KillTimer(hwnd_, ID_TIMER_THROTTLED_SCHEDULE);

            auto logShutdownStage = [&](std::wstring_view stage, const auto startedAt) {
                const double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - startedAt).count();
                QS_LOG_EVENT(quicksift::diagnostics::Level::Info, L"Lifecycle", L"shutdown_stage",
                    {L"stage", std::wstring(stage)}, {L"ms", std::to_wstring(ms)});
            };

            auto stage = std::chrono::steady_clock::now();
            resourcePressureMonitor_.Stop();
            logShutdownStage(L"resource_pressure_stop", stage);

            stage = std::chrono::steady_clock::now();
            transactions_.Stop();
            logShutdownStage(L"transactions_stop", stage);

            stage = std::chrono::steady_clock::now();
            scanner_.Cancel();
            scanner_.SetNotify(nullptr);
            logShutdownStage(L"scanner_cancel", stage);

            stage = std::chrono::steady_clock::now();
            worker_.SetNotify(nullptr);
            worker_.Stop();
            logShutdownStage(L"worker_stop", stage);

            stage = std::chrono::steady_clock::now();
            DrainPostedWorkerMessages();
            logShutdownStage(L"drain_completions", stage);

            stage = std::chrono::steady_clock::now();
            // Persistent cache is disposable. Release SQLite/WAL handles without a
            // forced checkpoint; committed WAL frames are recovered on the next open.
            persistentCache_.CloseForShutdown();
            logShutdownStage(L"cache_close", stage);

            const double totalMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - shutdownStartedAt).count();
            QS_LOG_EVENT(quicksift::diagnostics::Level::Info, L"Lifecycle", L"shutdown_sequence_complete",
                {L"total_ms", std::to_wstring(totalMs)});

            if (faceWarningTooltip_) {
                DestroyWindow(faceWarningTooltip_);
                faceWarningTooltip_ = nullptr;
            }
            if (faceLockAvailabilityTooltip_) {
                DestroyWindow(faceLockAvailabilityTooltip_);
                faceLockAvailabilityTooltip_ = nullptr;
            }
            DestroyUiFonts();
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProc(hwnd_, message, wParam, lParam);
        }
    }


    LRESULT QuickSiftApplicationImpl::HandleCanvasMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            canvas_ = hwnd;
            return 0;
        case WM_PAINT:
            Render();
            return 0;
        case WM_SIZE:
            ResizeCanvasTarget(std::max<UINT>(1, LOWORD(lParam)),
                std::max<UINT>(1, HIWORD(lParam)));
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN: {
            HideFaceWarningTooltip();
            MarkInteraction();
            SetFocus(canvas_);
            const auto point = CanvasPixelsToDips(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (HandleGlassAlertClick(point.x, point.y)) return 0;
            if (reviewState_.Mode() == ViewMode::Thumbnails && thumbnailScrollbarHot_) {
                thumbnailScrollDragging_ = true;
                thumbnailScrollMotionActive_ = true;
                thumbnailDragLoadMode_ = 0;
                thumbnailScrollConcurrentCap_ = 0;
                thumbnailDragVelocityPixelsPerSecond_ = 0.0f;
                worker_.SetThumbnailDragLoadMode(0);
                worker_.SetThumbnailScrollConcurrentCap(0);
                worker_.SetThumbnailDragActive(true);
                thumbnailViewportNeedsUpdate_ = false;
                const D2D1_RECT_F thumb = ThumbnailScrollbarThumbRect();
                thumbnailScrollDragOffset_ = point.y - thumb.top;
                SetCapture(canvas_);
                InvalidateCanvas();
                return 0;
            }
            OnCanvasLeftDown(point.x, point.y, wParam);
            return 0;
        }
        case WM_LBUTTONUP: {
            if (thumbnailScrollDragging_) {
                thumbnailScrollDragging_ = false;
                if (GetCapture() == canvas_) ReleaseCapture();
                if (thumbnailDragRenderTimerActive_) {
                    KillTimer(hwnd_, ID_TIMER_DRAG_RENDER);
                    thumbnailDragRenderTimerActive_ = false;
                }
                thumbnailViewportNeedsUpdate_ = false;
                // Settle-debounce fills the viewport; do not dump Interactive work on release.
                ArmThumbnailScrollSettle();
                ScheduleViewResourceTrim();
                if (completionQueue_.Size() != 0 && hwnd_ && IsWindow(hwnd_))
                    PostMessageW(hwnd_, WM_APP_BACKGROUND_COMPLETION, 0, 0);
                InvalidateCanvas();
                return 0;
            }
            if (uiFramework_.Alerts().Visible()) return 0;
            const auto point = CanvasPixelsToDips(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            OnCanvasLeftUp(point.x, point.y);
            return 0;
        }
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE: {
            if (thumbnailScrollDragging_) {
                thumbnailScrollDragging_ = false;
                if (thumbnailDragRenderTimerActive_) {
                    KillTimer(hwnd_, ID_TIMER_DRAG_RENDER);
                    thumbnailDragRenderTimerActive_ = false;
                }
                thumbnailViewportNeedsUpdate_ = false;
                ArmThumbnailScrollSettle();
                InvalidateCanvas();
                return 0;
            }
            break;
        }
        case WM_LBUTTONDBLCLK: {
            if (uiFramework_.Alerts().Visible()) return 0;
            const auto point = CanvasPixelsToDips(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            OnCanvasDoubleClick(point.x, point.y);
            return 0;
        }
        case WM_MOUSEMOVE: {
            const auto point = CanvasPixelsToDips(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (!trackingCanvasMouse_) {
                TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, canvas_, 0 };
                trackingCanvasMouse_ = TrackMouseEvent(&track) != FALSE;
            }
            if (uiFramework_.Alerts().Visible()) {
                HideFaceWarningTooltip();
                UpdateGlassAlertHover(point.x, point.y);
                return 0;
            }
            if (reviewState_.Mode() == ViewMode::Thumbnails) {
                if (thumbnailScrollDragging_) {
                    const auto layout = CurrentThumbnailLayout();
                    const float trackTop = 6.0f;
                    const float trackBottom = layout.canvasHeight - 6.0f;
                    const float trackHeight = std::max(1.0f, trackBottom - trackTop);
                    const float thumbHeight = std::clamp(
                        (layout.canvasHeight / std::max(1.0f, layout.contentHeight)) * trackHeight,
                        32.0f, trackHeight);
                    const float travel = std::max(1.0f, trackHeight - thumbHeight);
                    const float desiredTop = std::clamp(
                        point.y - thumbnailScrollDragOffset_, trackTop, trackTop + travel);
                    const float ratio = (desiredTop - trackTop) / travel;
                    const float previousScroll = reviewState_.Thumbnails().scrollDip;
                    const auto now = std::chrono::steady_clock::now();
                    reviewState_.SetThumbnailScroll(ratio * layout.maximumScroll);
                    if (reviewState_.Thumbnails().scrollDip != previousScroll) {
                        thumbnailPrefetchPlanner_.ObserveScroll(reviewState_.Thumbnails().scrollDip, now);
                        ApplyThumbnailScrollLoadBudget(CurrentThumbnailScrollLoadBudget());
                        // One virtualized frame (paint + optional publish) — never enqueue
                        // from WM_MOUSEMOVE. Successive flings cancel settle via Request.
                        RequestThumbnailScrollFrame();
                        if (!thumbnailDragRenderTimerActive_) {
                            thumbnailDragRenderTimerActive_ = StartUiTimer(ID_TIMER_DRAG_RENDER,
                                static_cast<UINT>(quicksift::review::prefetch::ScrollFrameIntervalMilliseconds()),
                                L"drag render throttle");
                        }
                    }
                    return 0;
                }
                const D2D1_RECT_F thumb = ThumbnailScrollbarThumbRect();
                const bool hit = point.x >= thumb.left - 16.0f && point.x <= thumb.right + 16.0f &&
                    point.y >= thumb.top && point.y <= thumb.bottom;
                if (hit != thumbnailScrollbarHot_) {
                    thumbnailScrollbarHot_ = hit;
                    InvalidateCanvas();
                }
            }
            UpdateFaceWarningTooltip(point.x, point.y);
            if ((wParam & MK_LBUTTON) != 0) MarkInteraction();
            OnCanvasMouseMove(point.x, point.y, wParam);
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingCanvasMouse_ = false;
            HideFaceWarningTooltip();
            thumbnailScrollbarHot_ = false;
            InvalidateCanvas();
            return 0;
        case WM_MOUSEWHEEL: {
            HideFaceWarningTooltip();
            if (uiFramework_.Alerts().Visible()) return 0;
            MarkInteraction();
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(canvas_, &point);
            const auto dipPoint = CanvasPixelsToDips(point.x, point.y);
            OnCanvasWheel(dipPoint.x, dipPoint.y, GET_WHEEL_DELTA_WPARAM(wParam), GET_KEYSTATE_WPARAM(wParam));
            return 0;
        }
        case WM_KEYDOWN:
            if (uiFramework_.Alerts().Visible()) {
                if (wParam == VK_ESCAPE) DismissGlassAlert();
                else if (wParam == VK_RETURN || wParam == VK_SPACE) ConfirmGlassAlert();
                return 0;
            }
            MarkInteraction();
            OnKeyDown(static_cast<UINT>(wParam), GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_SHIFT) < 0);
            return 0;
        case WM_SETCURSOR:
            if (uiFramework_.Alerts().Visible() && (uiFramework_.Alerts().PrimaryHot() || uiFramework_.Alerts().SecondaryHot())) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
            if (dragging_) {
                SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
                return TRUE;
            }
            break;
        }
        return DefWindowProc(hwnd, message, wParam, lParam);
    }


    int QuickSiftApplicationImpl::DipToPx(int dip) const {
        return MulDiv(dip, static_cast<int>(currentDpi_), USER_DEFAULT_SCREEN_DPI);
    }


    float QuickSiftApplicationImpl::CanvasDpi() const {
        if (!canvas_) return static_cast<float>(currentDpi_);
        const UINT dpi = quicksift::ui::GetWindowDpi(canvas_);
        return static_cast<float>(dpi ? dpi : currentDpi_);
    }


    float QuickSiftApplicationImpl::CanvasPixelScale() const {
        return CanvasDpi() / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    }


    D2D1_POINT_2F QuickSiftApplicationImpl::CanvasPixelsToDips(int x, int y) const {
        const float scale = static_cast<float>(USER_DEFAULT_SCREEN_DPI) / CanvasDpi();
        return D2D1::Point2F(x * scale, y * scale);
    }


    D2D1_SIZE_F QuickSiftApplicationImpl::CanvasSizeInDips() const {
        RECT rect{};
        if (canvas_) GetClientRect(canvas_, &rect);
        const float scale = static_cast<float>(USER_DEFAULT_SCREEN_DPI) / CanvasDpi();
        return D2D1::SizeF((rect.right - rect.left) * scale, (rect.bottom - rect.top) * scale);
    }


    void QuickSiftApplicationImpl::MarkInteraction() {
        lastInteraction_ = std::chrono::steady_clock::now();
        worker_.MarkInteractiveActivity();
        StartUiTimer(ID_TIMER_IDLE_QUALITY, 220, L"idle-quality");
    }


    bool QuickSiftApplicationImpl::IsActivelyInteracting() const {
        return std::chrono::steady_clock::now() - lastInteraction_ < std::chrono::milliseconds(180);
    }


    void QuickSiftApplicationImpl::ApplyUiFontsToControls(HFONT normal, HFONT semibold) {
        const HFONT stock = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        normal = normal ? normal : stock;
        semibold = semibold ? semibold : stock;
        for (const auto& [id, control] : controls_) {
            (void)id;
            if (control && IsWindow(control)) {
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(semibold), TRUE);
            }
        }
        if (tree_ && IsWindow(tree_)) SendMessageW(tree_, WM_SETFONT, reinterpret_cast<WPARAM>(normal), TRUE);
        if (exif_ && IsWindow(exif_)) SendMessageW(exif_, WM_SETFONT, reinterpret_cast<WPARAM>(normal), TRUE);
        if (status_ && IsWindow(status_)) SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(normal), TRUE);
        if (filterLabel_ && IsWindow(filterLabel_)) {
            SendMessageW(filterLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(semibold), TRUE);
        }
    }


    void QuickSiftApplicationImpl::DestroyUiFonts() {
        // A GDI font cannot be deleted while a child control still has it selected.
        // Detach every custom font first; otherwise DeleteObject silently fails and
        // leaks one GDI handle on each DPI/font recreation.
        ApplyUiFontsToControls(nullptr, nullptr);
        if (uiFont_) DeleteObject(uiFont_);
        if (uiFontSemibold_) DeleteObject(uiFontSemibold_);
        if (uiTitleFont_) DeleteObject(uiTitleFont_);
        uiFont_ = nullptr;
        uiFontSemibold_ = nullptr;
        uiTitleFont_ = nullptr;
    }


    void QuickSiftApplicationImpl::RecreateUiFonts() {
        const int normalHeight = -MulDiv(10, static_cast<int>(currentDpi_), 72);
        const int compactHeight = -MulDiv(9, static_cast<int>(currentDpi_), 72);
        const int titleHeight = -MulDiv(11, static_cast<int>(currentDpi_), 72);
        HFONT newNormal = CreateFontW(normalHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
        HFONT newSemibold = CreateFontW(compactHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
        HFONT newTitle = CreateFontW(titleHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");

        const HFONT oldNormal = uiFont_;
        const HFONT oldSemibold = uiFontSemibold_;
        const HFONT oldTitle = uiTitleFont_;
        // Preserve a valid existing font when GDI is temporarily exhausted.
        // Falling back to stock controls is safe, but discarding a working font
        // merely because its replacement failed creates avoidable UI degradation.
        uiFont_ = newNormal ? newNormal : oldNormal;
        uiFontSemibold_ = newSemibold ? newSemibold : oldSemibold;
        uiTitleFont_ = newTitle ? newTitle : oldTitle;
        ApplyUiFontsToControls(uiFont_, uiFontSemibold_);

        if (newNormal && oldNormal) DeleteObject(oldNormal);
        if (newSemibold && oldSemibold) DeleteObject(oldSemibold);
        if (newTitle && oldTitle) DeleteObject(oldTitle);
    }


    void QuickSiftApplicationImpl::ConfigureDwmGlass() {
        const int rounded = 2; // DWMWCP_ROUND on Windows 11; ignored on Windows 10.
        DwmSetWindowAttribute(hwnd_, 33, &rounded, sizeof(rounded));
        const int backdrop = 1; // No transparent system backdrop: custom blobs are cheaper and deterministic.
        DwmSetWindowAttribute(hwnd_, 38, &backdrop, sizeof(backdrop));
        ApplyTheme();
    }

} // namespace quicksift::app
