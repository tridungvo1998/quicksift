// Overflow-tooltip hover harness (Windows or Wine). Drives the REAL framework
// button + ButtonSubclassProc hover tracking + overflow tooltip with synthetic
// input and writes every decision to harness.log:
//   PAINT truncated=1 -> marked-truncated -> hover-enter -> armed -> timer-fired ->
//   shown (window rect, visible=1) -> hover-leave -> hidden.
// Build (clang-cl or cl, from the project root):
//   cl /std:c++20 /EHsc /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Isrc scripts\harness\overflow_tooltip_harness.cpp
//      src\ui\framework\*.cpp src\ui\ui_design_system.cpp user32.lib gdi32.lib comctl32.lib
//      uxtheme.lib dwmapi.lib d2d1.lib dwrite.lib windowscodecs.lib ole32.lib gdiplus.lib
//      shell32.lib advapi32.lib msimg32.lib shcore.lib oleaut32.lib uuid.lib
// (link with a Common-Controls v6 manifest, as the app does).
#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <string>
#include "ui/framework/macos_ui_framework.h"
#include "ui/ui_design_system.h"
#include "core/app_types.h"
using namespace quicksift::ui::framework;
static FILE* gLog;
static MacOsUiFramework* gFw;
static HWND gButton, gMain, gTip;
static std::wstring gStored;
static void Sink(const wchar_t* m) noexcept { fwprintf(gLog, L"TRACE %ls\n", m); fflush(gLog); }
static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DRAWITEM) {
        auto* d = reinterpret_cast<DRAWITEMSTRUCT*>(l);
        wchar_t t[128]{}; GetWindowTextW(d->hwndItem, t, 128);
        ButtonPresentation p{}; p.text = t; p.hoverBlend = gFw->ButtonHoverBlend(d->hwndItem);
        auto pal = quicksift::ui::GetThemePalette(quicksift::core::AppTheme::Dark);
        HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        bool tr = gFw->PaintButton(d->hDC, d->rcItem, p, pal, 96, f, f);
        fwprintf(gLog, L"PAINT truncated=%d\n", tr ? 1 : 0); fflush(gLog);
        if (tr && gStored.empty()) {
            MacOsUiFramework::SetOverflowTraceSink(Sink);
            gTip = MacOsUiFramework::CreateOverflowTooltip(gMain, GetModuleHandleW(nullptr));
            MacOsUiFramework::SetOverflowTooltip(gTip); MacOsUiFramework::StyleTooltip(gTip, pal, 96, f);
            MacOsUiFramework::SetOverflowTooltip(gTip);
            gStored = t;
            MacOsUiFramework::SetButtonOverflowText(d->hwndItem, gStored.c_str());
        }
        return TRUE;
    }
    if (m == WM_TIMER && w == 77) {
        static int step = 0; ++step;
        RECT r; GetWindowRect(gButton, &r);
        if (step == 1) { SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
            INPUT in{}; in.type = INPUT_MOUSE; in.mi.dwFlags = MOUSEEVENTF_MOVE; in.mi.dx = 1; SendInput(1, &in, sizeof in);
            fwprintf(gLog, L"STEP cursor on button\n"); }
        if (step == 60) { fwprintf(gLog, L"STEP tooltip visible=%d\n", gTip && IsWindowVisible(gTip)); 
            if (gTip) { RECT t; GetWindowRect(gTip, &t); fwprintf(gLog, L"STEP tip rect %ld,%ld,%ld,%ld\n", t.left,t.top,t.right,t.bottom);} 
            SetCursorPos(5, 5); fwprintf(gLog, L"STEP cursor away\n"); }
        if (step == 64) { fwprintf(gLog, L"STEP tooltip visible=%d\n", gTip && IsWindowVisible(gTip)); PostQuitMessage(0); }
        fflush(gLog);
        return 0;
    }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    gLog = _wfopen(L"harness.log", L"w");
    MacOsUiFramework::InitializeNativeControls();
    MacOsUiFramework fw; gFw = &fw;
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = inst; wc.lpszClassName = L"H"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    RegisterClassW(&wc);
    gMain = CreateWindowExW(0, L"H", L"h", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE, 100, 100, 500, 300, nullptr, nullptr, inst, nullptr);
    ButtonControlSpec s{}; s.parent = gMain; s.instance = inst; s.id = 1001; s.text = L"Show diagnostic log window please";
    gButton = fw.CreateButton(s);
    SetWindowPos(gButton, nullptr, 40, 40, 120, 32, SWP_NOZORDER);
    UpdateWindow(gMain);
    SetTimer(gMain, 77, 150, nullptr);
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    fclose(gLog); return 0;
}
