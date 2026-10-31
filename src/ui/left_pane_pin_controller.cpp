// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: The command-pane Pin button (0.9.6.0). Pinned, the left pane stays open
// instead of closing on hover-out, and CanvasLayoutRect gives it its own column so
// it never covers the photos. The state persists (session key LeftPanePinned).
// The button is a framework button (theme, hover, focus and selected state come
// from DrawGlassButton) placed under the section tabs.
#include "app/quicksift_application_internal.h"
#include "ui/ui_layout_constants.h"

namespace quicksift::app {

void QuickSiftApplicationImpl::CreateLeftPanePinButton() {
    if (!CreateMainWindowButton(ID_LEFT_PANE_PIN, L"Pin pane")) return;
    HWND pin = controls_[ID_LEFT_PANE_PIN];
    ShowWindow(pin, SW_HIDE);
    leftPanePinTooltipText_ = Tr(L"Keep the command pane open and give it its own space");
    quicksift::ui::framework::TooltipControlSpec spec{};
    spec.owner = hwnd_;
    spec.target = pin;
    spec.instance = instance_;
    spec.toolId = static_cast<UINT_PTR>(ID_LEFT_PANE_PIN);
    spec.text = &leftPanePinTooltipText_;
    leftPanePinTooltip_ = uiFramework_.CreateTooltip(spec);
    if (!leftPanePinTooltip_) {
        quicksift::diagnostics::WriteLastError(quicksift::diagnostics::Level::Warning,
            L"UI", L"Creating the command-pane pin tooltip");
    }
    RestyleTooltips();
    RefreshLeftPanePinButton();
}

void QuickSiftApplicationImpl::RefreshLeftPanePinButton() {
    const auto control = controls_.find(ID_LEFT_PANE_PIN);
    if (control == controls_.end() || !control->second) return;
    const std::wstring caption = Tr(leftPanePinned_ ? L"Unpin pane" : L"Pin pane");
    wchar_t current[64]{};
    GetWindowTextW(control->second, current, static_cast<int>(std::size(current)));
    if (caption != current) SetWindowTextW(control->second, caption.c_str());
    leftPanePinTooltipText_ = Tr(leftPanePinned_ ?
        L"Let the command pane close again when the pointer leaves it" :
        L"Keep the command pane open and give it its own space");
    if (leftPanePinTooltip_) {
        TOOLINFOW tool{};
        tool.cbSize = sizeof(TOOLINFOW);
        tool.hwnd = control->second;
        tool.uId = static_cast<UINT_PTR>(ID_LEFT_PANE_PIN);
        tool.lpszText = leftPanePinTooltipText_.data();
        SendMessageW(leftPanePinTooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    InvalidateRect(control->second, nullptr, FALSE);
}

void QuickSiftApplicationImpl::LayoutLeftPanePinButton(int width, int height, bool showPaneContent) {
    const auto control = controls_.find(ID_LEFT_PANE_PIN);
    if (control == controls_.end() || !control->second) return;
    namespace pin = quicksift::ui::layout::left_pane_pin;
    const RECT pane = LeftPaneOpenRect(width, height);
    const int inset = DipToPx(pin::kInsetDip);
    const int buttonW = DipToPx(kLeftPaneTabColumnWidthDip);
    const int buttonH = DipToPx(pin::kButtonHeightDip);
    const RECT lastTab = FlyoutTabRect(FlyoutPanel::Settings, width, height);
    const int y = pane.bottom - inset - buttonH;
    // Hidden when the window is too short to fit it below the section tabs.
    const bool fits = !IsRectEmpty(&pane) && !IsRectEmpty(&lastTab) &&
        y >= lastTab.bottom + DipToPx(kLeftPaneTabGapDip);
    if (showPaneContent && fits) {
        const int x = pane.left + inset;
        UINT flags = SWP_NOACTIVATE | SWP_NOREDRAW;
        if (!IsWindowVisible(control->second)) flags |= SWP_SHOWWINDOW;
        SetWindowPos(control->second, HWND_TOP, x, y, buttonW, buttonH, flags);
        if (buttonW != leftPanePinLastW_ || buttonH != leftPanePinLastH_) {
            uiFramework_.ApplyAntialiasedControlRegion(control->second, buttonW, buttonH, DipToPx(kButtonRadiusDip));
            leftPanePinLastW_ = buttonW;
            leftPanePinLastH_ = buttonH;
        }
        RedrawWindow(control->second, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    } else if (IsWindowVisible(control->second)) {
        SetWindowPos(control->second, nullptr, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE |
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    }
}

void QuickSiftApplicationImpl::ToggleLeftPanePinned() {
    leftPanePinned_ = !leftPanePinned_;
    if (leftPanePinned_ && activeFlyout_ == FlyoutPanel::None && !fullscreen_) SetActiveFlyout(lastFlyoutSection_);
    lastFlyoutHover_ = std::chrono::steady_clock::now();
    RefreshLeftPanePinButton();
    SaveSession();
    RECT client{};
    GetClientRect(hwnd_, &client);
    LayoutControls(client.right, client.bottom); // the canvas gains or gives up the pane's column
    MarkLeftPaneFrostDirty();
    InvalidateCanvas();
    ShowToast(leftPanePinned_ ? L"Command pane pinned open." : L"Command pane unpinned; it opens on hover.");
}

void QuickSiftApplicationImpl::ApplyStartupPreferences() {
    RefreshLeftPanePinButton();
    UpdatePickLabelMappingButton();
    if (leftPanePinned_ && activeFlyout_ == FlyoutPanel::None && !fullscreen_) {
        SetActiveFlyout(lastFlyoutSection_ == FlyoutPanel::None ? FlyoutPanel::Folders : lastFlyoutSection_);
        RECT client{};
        GetClientRect(hwnd_, &client);
        LayoutControls(client.right, client.bottom);
    }
    ShowFirstRunTipIfNeeded();
    if (rawPreviewDefaultNoticePending_) {
        rawPreviewDefaultNoticePending_ = false;
        ShowToast(L"RAW photos now open with their embedded JPG previews for speed. Zooming in loads the full RAW. Change this in Settings → RAW handling.");
        SaveSession(); // records SettingsVersion so the notice shows once
    }
}

} // namespace quicksift::app
