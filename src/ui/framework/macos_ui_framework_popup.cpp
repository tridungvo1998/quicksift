// OWNER: Central macOS-inspired dropdown host, input routing, placement, scrolling, and painting.
#include "ui/framework/macos_ui_framework.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <limits>
#include <utility>
namespace quicksift::ui::framework {
namespace {
inline constexpr UINT_PTR kPopupMaintenanceTimerId = 0x5153504Du; inline constexpr UINT kPopupMaintenanceIntervalMs = 50u;
HFONT SafeFont(HFONT requested) noexcept {
    return requested ? requested : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}
bool SelectFontObject(HDC dc, HFONT font, HFONT& previous) noexcept {
    previous = static_cast<HFONT>(SelectObject(dc, SafeFont(font)));
    return previous && previous != HGDI_ERROR;
}
int SaturatingAdd(int first, int second) noexcept {
    const long long sum = static_cast<long long>(first) + second;
    return static_cast<int>(std::clamp(sum,
        static_cast<long long>(std::numeric_limits<int>::min()),
        static_cast<long long>(std::numeric_limits<int>::max())));
}
void ReportDropdownError(const std::function<void(std::wstring_view)>& sink,
    std::wstring_view operation) noexcept {
    if (!sink) return;
    try {
        sink(operation);
    } catch (...) {
        // Diagnostics must never interrupt popup cleanup or leave capture held.
    }
}
} // namespace
struct MacOsUiFramework::PopupRuntime {
    MacOsUiFramework* framework = nullptr;
    PopupMenuModel model;
    PopupMenuMetrics metrics{};
    ThemePalette palette{};
    HFONT font = nullptr;
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    int result = 0;
    int contentHeight = 0;
    int viewportHeight = 0;
    int scrollOffset = 0;
    bool done = false, maintenanceFailed = false;
    std::function<void()> maintenancePump;
    std::function<void(std::wstring_view)> errorSink;
    explicit PopupRuntime(std::vector<MenuItem> items) : model(std::move(items)) {}
};
namespace {
template <typename Runtime>
int MaximumScroll(const Runtime& runtime) noexcept {
    return std::max(0, runtime.contentHeight - runtime.viewportHeight);
}
template <typename Runtime>
void ClampPopupScroll(Runtime& runtime) noexcept {
    runtime.scrollOffset = std::clamp(runtime.scrollOffset, 0, MaximumScroll(runtime));
}
template <typename Runtime>
void EnsureHoveredVisible(Runtime& runtime) noexcept {
    const int index = runtime.model.HoveredIndex();
    if (index < 0) return;
    const int top = runtime.model.ItemTop(index, runtime.metrics);
    const int bottom = SaturatingAdd(top, runtime.model.ItemHeight(index, runtime.metrics));
    const int inset = std::max(0, runtime.metrics.padding);
    const int visibleTop = SaturatingAdd(runtime.scrollOffset, inset);
    const int visibleBottom = std::max(visibleTop,
        SaturatingAdd(runtime.scrollOffset, runtime.viewportHeight - inset));
    if (top < visibleTop) runtime.scrollOffset = std::max(0, top - inset);
    else if (bottom > visibleBottom) {
        runtime.scrollOffset = bottom - runtime.viewportHeight + inset;
    }
    ClampPopupScroll(runtime);
}
template <typename Runtime>
int LastSelectableIndex(const Runtime& runtime) noexcept {
    for (int index = static_cast<int>(runtime.model.Items().size()) - 1; index >= 0; --index) {
        if (!runtime.model.Items()[static_cast<std::size_t>(index)].separator) return index;
    }
    return -1;
}
} // namespace
bool MacOsUiFramework::RegisterPopupWindowClass(HINSTANCE instance) noexcept {
    if (!instance) return false;
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (GetClassInfoExW(instance, PopupWindowClassName, &existing)) return true;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = PopupWindowProc;
    windowClass.lpszClassName = PopupWindowClassName;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.style = CS_DROPSHADOW;
    return RegisterClassExW(&windowClass) != 0;
}
void MacOsUiFramework::PaintPopup(HWND window, PopupRuntime& runtime) const {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    const int width = std::max(1, static_cast<int>(client.right - client.left));
    const int height = std::max(1, static_cast<int>(client.bottom - client.top));
    HDC bufferDc = CreateCompatibleDC(dc);
    HBITMAP bitmap = bufferDc ? CreateCompatibleBitmap(dc, width, height) : nullptr;
    if (!bufferDc || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (bufferDc) DeleteDC(bufferDc);
        EndPaint(window, &paint);
        return;
    }
    HGDIOBJ oldBitmap = SelectObject(bufferDc, bitmap);
    if (!oldBitmap || oldBitmap == HGDI_ERROR) {
        DeleteObject(bitmap);
        DeleteDC(bufferDc);
        EndPaint(window, &paint);
        return;
    }
    FillSolidRect(bufferDc, client, runtime.palette.menu);
    {
        Gdiplus::Graphics graphics(bufferDc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        FillRoundedRectGp(graphics, client, static_cast<float>(ScaleDip(11, runtime.dpi)),
            ToGpColor(runtime.palette.menu));
        DrawRoundedRectGp(graphics, client, static_cast<float>(ScaleDip(11, runtime.dpi)),
            ToGpColor(BlendColor(runtime.palette.border, runtime.palette.menu, 0.12f)), 1.0f);
    }
    for (int index = 0; index < static_cast<int>(runtime.model.Items().size()); ++index) {
        const int top = runtime.model.ItemTop(index, runtime.metrics) - runtime.scrollOffset;
        const int itemHeight = runtime.model.ItemHeight(index, runtime.metrics);
        if (top >= height || top + itemHeight <= 0) continue;
        RECT itemRect{ runtime.metrics.padding, top,
            width - runtime.metrics.padding, top + itemHeight };
        PaintMenuItem(bufferDc, itemRect, runtime.model.Items()[static_cast<std::size_t>(index)],
            runtime.model.HoveredIndex() == index, runtime.palette, runtime.dpi, runtime.font);
    }
    if (runtime.contentHeight > runtime.viewportHeight) {
        const int inset = ScaleDip(5, runtime.dpi);
        RECT track{ width - ScaleDip(7, runtime.dpi), inset,
            width - ScaleDip(3, runtime.dpi), height - inset };
        const int trackHeight = std::max(1, static_cast<int>(track.bottom - track.top));
        const int minimumThumb = ScaleDip(22, runtime.dpi);
        const int thumbHeight = std::clamp(
            static_cast<int>((static_cast<long long>(trackHeight) * runtime.viewportHeight) /
                std::max(1, runtime.contentHeight)),
            std::min(minimumThumb, trackHeight), trackHeight);
        const int travel = std::max(0, trackHeight - thumbHeight);
        const int maximum = MaximumScroll(runtime);
        const int thumbTop = track.top + (maximum > 0
            ? static_cast<int>((static_cast<long long>(travel) * runtime.scrollOffset) / maximum)
            : 0);
        RECT thumb{ track.left, thumbTop, track.right, thumbTop + thumbHeight };
        PaintOverlayScrollbar(bufferDc, thumb, track, false, false,
            runtime.palette, runtime.dpi);
    }
    BitBlt(dc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY);
    SelectObject(bufferDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(bufferDc);
    EndPaint(window, &paint);
}
LRESULT MacOsUiFramework::HandlePopupMessage(HWND window, PopupRuntime& runtime,
    UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT:
        PaintPopup(window, runtime);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (wParam == kPopupMaintenanceTimerId && runtime.maintenancePump &&
            !runtime.maintenanceFailed) try { runtime.maintenancePump(); }
            catch (...) {
                runtime.maintenanceFailed = true;
                ReportDropdownError(runtime.errorSink, L"Pumping background completions while a dropdown was open");
            }
        return 0;
    case WM_MOUSEMOVE: {
        const int contentY = SaturatingAdd(GET_Y_LPARAM(lParam), runtime.scrollOffset);
        const int next = runtime.model.HitTestY(contentY, runtime.metrics);
        if (next != runtime.model.HoveredIndex()) {
            runtime.model.SetHoveredIndex(next);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        const int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        if (notches != 0 && runtime.contentHeight > runtime.viewportHeight) {
            const long long desired = static_cast<long long>(runtime.scrollOffset) -
                static_cast<long long>(notches) * runtime.metrics.itemHeight * 3;
            runtime.scrollOffset = static_cast<int>(std::clamp(desired,
                static_cast<long long>(std::numeric_limits<int>::min()),
                static_cast<long long>(std::numeric_limits<int>::max())));
            ClampPopupScroll(runtime);
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (ScreenToClient(window, &point)) {
                const int contentY = SaturatingAdd(point.y, runtime.scrollOffset);
                runtime.model.SetHoveredIndex(runtime.model.HitTestY(contentY, runtime.metrics));
            }
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT client{};
        GetClientRect(window, &client);
        const int contentY = SaturatingAdd(point.y, runtime.scrollOffset);
        const int item = PtInRect(&client, point)
            ? runtime.model.HitTestY(contentY, runtime.metrics) : -1;
        runtime.model.SetHoveredIndex(item);
        runtime.result = runtime.model.HoveredCommand();
        runtime.done = true;
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            runtime.done = true;
            return 0;
        }
        if (wParam == VK_RETURN) {
            runtime.result = runtime.model.HoveredCommand();
            runtime.done = true;
            return 0;
        }
        if (wParam == VK_UP || wParam == VK_DOWN) {
            runtime.model.MoveHover(wParam == VK_UP ? -1 : 1);
            EnsureHoveredVisible(runtime);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_HOME || wParam == VK_END) {
            const int index = wParam == VK_HOME
                ? runtime.model.FirstSelectableIndex() : LastSelectableIndex(runtime);
            runtime.model.SetHoveredIndex(index);
            EnsureHoveredVisible(runtime);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_PRIOR || wParam == VK_NEXT) {
            const int direction = wParam == VK_PRIOR ? -1 : 1;
            const int steps = std::max(1,
                runtime.viewportHeight / std::max(1, runtime.metrics.itemHeight) - 1);
            for (int step = 0; step < steps; ++step) runtime.model.MoveHover(direction);
            EnsureHoveredVisible(runtime);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) runtime.done = true;
        return 0;
    case WM_CAPTURECHANGED:
        runtime.done = true;
        return 0;
    case WM_NCHITTEST:
        return HTCLIENT;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
LRESULT CALLBACK MacOsUiFramework::PopupWindowProc(HWND window, UINT message,
    WPARAM wParam, LPARAM lParam) {
    PopupRuntime* runtime = reinterpret_cast<PopupRuntime*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        runtime = static_cast<PopupRuntime*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(runtime));
    }
    if (!runtime || !runtime->framework) {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    try {
        return runtime->framework->HandlePopupMessage(window, *runtime, message, wParam, lParam);
    } catch (...) {
        runtime->done = true;
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
int MacOsUiFramework::TrackDropdown(DropdownRequest request) {
    if (!request.owner || !request.anchor || !request.instance || request.items.empty()) return 0;
    if (!RegisterPopupWindowClass(request.instance)) {
        ReportDropdownError(request.errorSink,
            L"Registering the centralized popup-menu window class");
        return 0;
    }
    PopupRuntime runtime(std::move(request.items));
    runtime.framework = this;
    runtime.palette = request.palette;
    runtime.font = request.font;
    runtime.dpi = request.dpi ? request.dpi : USER_DEFAULT_SCREEN_DPI;
    runtime.maintenancePump = std::move(request.maintenancePump);
    runtime.errorSink = request.errorSink;
    runtime.metrics.itemHeight = ScaleDip(32, runtime.dpi);
    runtime.metrics.separatorHeight = ScaleDip(9, runtime.dpi);
    runtime.metrics.padding = ScaleDip(6, runtime.dpi);
    runtime.contentHeight = runtime.model.TotalHeight(runtime.metrics);
    HDC measureDc = GetDC(request.owner);
    int width = ScaleDip(176, runtime.dpi);
    if (measureDc) {
        HFONT oldFont = nullptr;
        if (SelectFontObject(measureDc, request.font, oldFont)) {
            for (const MenuItem& item : runtime.model.Items()) {
                if (item.separator) continue;
                SIZE extent{};
                if (GetTextExtentPoint32W(measureDc, item.text.c_str(),
                    static_cast<int>(item.text.size()), &extent)) {
                    width = std::max(width, static_cast<int>(extent.cx) + ScaleDip(62, runtime.dpi));
                }
            }
            SelectObject(measureDc, oldFont);
        }
        ReleaseDC(request.owner, measureDc);
    }
    width = std::min(width, ScaleDip(320, runtime.dpi));
    RECT anchorRect{};
    if (!GetWindowRect(request.anchor, &anchorRect)) {
        ReportDropdownError(request.errorSink, L"Reading a dropdown anchor rectangle");
        return 0;
    }
    RECT workArea{};
    MONITORINFO monitor{ sizeof(MONITORINFO) };
    const HMONITOR nearest = MonitorFromRect(&anchorRect, MONITOR_DEFAULTTONEAREST);
    if (nearest && GetMonitorInfoW(nearest, &monitor)) {
        workArea = monitor.rcWork;
    } else if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
        workArea.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        workArea.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        workArea.right = workArea.left + std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
        workArea.bottom = workArea.top + std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
    }
    const int screenInset = ScaleDip(8, runtime.dpi);
    const int gap = ScaleDip(5, runtime.dpi);
    const int availableWidth = std::max(1,
        static_cast<int>(workArea.right - workArea.left) - screenInset * 2);
    const int availableHeight = std::max(1,
        static_cast<int>(workArea.bottom - workArea.top) - screenInset * 2);
    width = std::min(width, availableWidth);
    runtime.viewportHeight = std::clamp(runtime.contentHeight, 1, availableHeight);
    const int height = runtime.viewportHeight;
    int left = anchorRect.left;
    int top = anchorRect.bottom + gap;
    const int below = workArea.bottom - screenInset - top;
    const int above = anchorRect.top - gap - (workArea.top + screenInset);
    if (height > below && above > below) top = anchorRect.top - height - gap;
    left = std::clamp(left, static_cast<int>(workArea.left) + screenInset,
        std::max(static_cast<int>(workArea.left) + screenInset, static_cast<int>(workArea.right) - screenInset - width));
    top = std::clamp(top, static_cast<int>(workArea.top) + screenInset,
        std::max(static_cast<int>(workArea.top) + screenInset, static_cast<int>(workArea.bottom) - screenInset - height));
    HWND menu = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        PopupWindowClassName, L"", WS_POPUP, left, top, width, height,
        request.owner, nullptr, request.instance, &runtime);
    if (!menu) {
        ReportDropdownError(request.errorSink, L"Creating a centralized dropdown window");
        return 0;
    }
    ApplyRoundedRegion(menu, width, height, ScaleDip(11, runtime.dpi));
    const int corner = 2;
    DwmSetWindowAttribute(menu, 33, &corner, sizeof(corner));
    if (runtime.maintenancePump && SetTimer(menu, kPopupMaintenanceTimerId,
        kPopupMaintenanceIntervalMs, nullptr) == 0)
        ReportDropdownError(request.errorSink, L"Starting dropdown background-maintenance timer");
    ShowWindow(menu, SW_SHOWNORMAL);
    UpdateWindow(menu);
    SetCapture(menu);
    SetFocus(menu);
    MSG message{};
    bool sawQuit = false;
    int quitCode = 0;
    while (!runtime.done) {
        // Keep input isolated to the popup. Its timer drains background results
        // without dispatching owner commands or allowing arbitrary reentrancy.
        const BOOL status = GetMessageW(&message, menu, 0, 0);
        if (status <= 0) {
            if (status == 0) {
                sawQuit = true;
                quitCode = static_cast<int>(message.wParam);
            } else {
                ReportDropdownError(request.errorSink,
                    L"Reading messages while a centralized dropdown was open");
            }
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (GetCapture() == menu) ReleaseCapture();
    KillTimer(menu, kPopupMaintenanceTimerId);
    DestroyWindow(menu);
    if (sawQuit) PostQuitMessage(quitCode);
    return runtime.result;
}
} // namespace quicksift::ui::framework
