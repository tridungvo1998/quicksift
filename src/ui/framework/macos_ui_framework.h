// OWNER: Central macOS-inspired native control creation, painting, popups, toasts, and alerts.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <gdiplus.h>

#include "ui/framework/ui_models.h"
#include "ui/ui_design_system.h"

#include <functional>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace quicksift::ui::framework {

struct NativeControlSpec {
    DWORD extendedStyle = 0;
    const wchar_t* className = L"STATIC";
    std::wstring text;
    DWORD style = WS_CHILD | WS_VISIBLE;
    HWND parent = nullptr;
    int id = 0;
    HINSTANCE instance = nullptr;
    void* creationParameter = nullptr;
};

struct ButtonControlSpec {
    HWND parent = nullptr;
    HINSTANCE instance = nullptr;
    int id = 0;
    std::wstring text;
    SUBCLASSPROC subclassProcedure = nullptr;
    UINT_PTR subclassId = 0;
    DWORD_PTR subclassReference = 0;
    DWORD extendedStyle = 0;
    DWORD additionalStyle = 0;
};

struct TooltipControlSpec {
    HWND owner = nullptr;
    HWND target = nullptr;
    HINSTANCE instance = nullptr;
    UINT_PTR toolId = 0;
    std::wstring* text = nullptr;
    int maximumWidthDip = 390;
    int initialDelayMs = 500;
    int reshowDelayMs = 100;
    int autoPopDelayMs = 7000;
};

struct PanelPresentation {
    bool raised = false;
    bool translucent = false;
    int radiusDip = 16;
};

struct LabelPresentation {
    std::wstring text;
    bool semibold = false;
    bool muted = false;
    UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
    int horizontalPaddingDip = 10;
};

struct InfoPanelPresentation {
    std::wstring title;
    std::wstring body;
};

struct StatusPresentation {
    std::wstring leftText;
    std::wstring rightText;
    std::wstring secondaryText;
};

struct TrafficLightPresentation {
    RECT rect{};
    COLORREF color = RGB(255, 95, 87);
    bool hot = false;
    bool pressed = false;
};

struct TitleBarPresentation {
    std::wstring title;
    RECT titleTextRect{};
    HFONT titleFont = nullptr;
    bool active = true;
    std::array<TrafficLightPresentation, 3> trafficLights{};
};

struct DropdownRequest {
    HWND owner = nullptr;
    HWND anchor = nullptr;
    HINSTANCE instance = nullptr;
    ThemePalette palette{};
    HFONT font = nullptr;
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    std::vector<MenuItem> items;
    // Called from the popup's private message loop so long-lived menus cannot
    // starve bounded background-completion queues owned by the application.
    std::function<void()> maintenancePump;
    std::function<void(std::wstring_view)> errorSink;
};

struct ToastPaintResources {
    ID2D1RenderTarget* target = nullptr;
    IDWriteFactory* writeFactory = nullptr;
    IDWriteTextFormat* textFormat = nullptr;
    D2D1_SIZE_F surfaceSize{};
    ThemePalette palette{};
};

struct AlertPaintResources {
    ID2D1RenderTarget* target = nullptr;
    IDWriteFactory* writeFactory = nullptr;
    IDWriteTextFormat* titleFormat = nullptr;
    IDWriteTextFormat* messageFormat = nullptr;
    D2D1_SIZE_F surfaceSize{};
    ThemePalette palette{};
};

struct MediaCardPresentation {
    D2D1_RECT_F rect{};
    float radius = 16.0f;
    bool paintSurface = true;
    bool selected = false;
    bool focused = false;
    bool warning = false;
};

struct MediaCardBrushes {
    ID2D1Brush* panel = nullptr;
    ID2D1Brush* border = nullptr;
    ID2D1Brush* accent = nullptr;
    ID2D1Brush* danger = nullptr;
};

class MacOsUiFramework {
public:
    MacOsUiFramework() = default;
    MacOsUiFramework(const MacOsUiFramework&) = delete;
    MacOsUiFramework& operator=(const MacOsUiFramework&) = delete;

    static constexpr wchar_t PopupWindowClassName[] = L"QuickSift.Framework.PopupMenu";

    static bool InitializeNativeControls() noexcept;
    bool RegisterPopupWindowClass(HINSTANCE instance) noexcept;

    HWND CreateControl(const NativeControlSpec& spec) const noexcept;
    HWND CreateButton(const ButtonControlSpec& spec) const noexcept;
    HWND CreateOwnerDrawStatic(HWND parent, HINSTANCE instance, int id,
        std::wstring text, DWORD extendedStyle = 0, DWORD additionalStyle = 0) const noexcept;
    HWND CreateD2DTextInput(HWND parent, HINSTANCE instance, int id,
        std::wstring text = L"") const noexcept;
    void SetD2DTextInputPalette(HWND window, const ThemePalette& palette, UINT dpi) const noexcept;
    HWND CreateTooltip(const TooltipControlSpec& spec) const noexcept;

    bool IsButtonHot(HWND button) const noexcept;
    // 0..1 hover fade driven by the shared button subclass timer.
    [[nodiscard]] float ButtonHoverBlend(HWND button) const noexcept;
    // bRedraw=false: SetWindowRgn without forcing an immediate redraw (animation ticks).
    void ApplyRoundedControlRegion(HWND window, int width, int height, int radius,
        bool redraw = true) const noexcept;

    void PaintButton(HDC dc, const RECT& rect, const ButtonPresentation& presentation,
        const ThemePalette& palette, UINT dpi, HFONT normalFont, HFONT semiboldFont) const;
    void PaintPanel(HDC dc, const RECT& rect, const PanelPresentation& presentation,
        const ThemePalette& palette, UINT dpi) const;
    // Stroke-only rounded border for content HWNDs that already have an opaque fill
    // (e.g. library list items). Matches PaintPanel border treatment.
    void PaintControlBorder(HDC dc, const RECT& rect, int radiusDip,
        const ThemePalette& palette, UINT dpi) const;
    void PaintLabel(HDC dc, const RECT& rect, const LabelPresentation& presentation,
        const ThemePalette& palette, UINT dpi, HFONT normalFont, HFONT semiboldFont) const;
    void PaintSectionTitle(HDC dc, const RECT& rect, std::wstring_view text,
        const ThemePalette& palette, UINT dpi, HFONT semiboldFont) const;
    void PaintInfoPanel(HDC dc, const RECT& rect, const InfoPanelPresentation& presentation,
        const ThemePalette& palette, UINT dpi, HFONT normalFont, HFONT semiboldFont) const;
    void PaintStatus(HDC dc, const RECT& rect, const StatusPresentation& presentation,
        const ThemePalette& palette, UINT dpi, HFONT normalFont, HFONT semiboldFont) const;
    // Returns the horizontal text extent used by the rotated vertical-tab renderer.
    // Callers use this value to size the tab's vertical geometry before painting.
    int MeasureVerticalTabTextExtent(std::wstring_view text, UINT dpi) const;

    void PaintVerticalTab(HDC dc, const RECT& rect, std::wstring_view text, bool active,
        const ThemePalette& palette, UINT dpi, float activeBlend = 1.0f) const;
    // Horizontal-label tab for a vertical sidebar column inside the left pane.
    void PaintSidebarTab(HDC dc, const RECT& rect, std::wstring_view text, bool active,
        const ThemePalette& palette, UINT dpi, float activeBlend = 1.0f) const;
    void PaintMenuItem(HDC dc, const RECT& rect, const MenuItem& item, bool selected,
        const ThemePalette& palette, UINT dpi, HFONT font) const;
    void PaintCenteredMutedText(HDC dc, const RECT& rect, std::wstring_view text,
        const ThemePalette& palette, UINT dpi, HFONT font) const;
    void PaintWindowBackground(HDC dc, const RECT& rect, const ThemePalette& palette) const;
    void PaintAccentStrip(HDC dc, const RECT& rect, const ThemePalette& palette) const;
    void PaintSolidSurface(HDC dc, const RECT& rect, COLORREF color) const;
    void PaintRoundedShadow(HDC dc, const RECT& rect, int radius,
        COLORREF color) const;
    void PaintTitleBar(HDC dc, const RECT& client, int height,
        const TitleBarPresentation& presentation, const ThemePalette& palette,
        UINT dpi) const;
    void PaintOverlayScrollbar(HDC dc, const RECT& thumb, const RECT& track,
        bool hot, bool dragging, const ThemePalette& palette, UINT dpi) const;
    void PaintOverlayScrollbar(ID2D1RenderTarget* target, const D2D1_RECT_F& thumb,
        const D2D1_RECT_F& track, bool hot, bool dragging,
        const ThemePalette& palette) const;

    int TrackDropdown(DropdownRequest request);

    ToastPresenter& Toasts() noexcept { return toasts_; }
    const ToastPresenter& Toasts() const noexcept { return toasts_; }
    AlertPresenter& Alerts() noexcept { return alerts_; }
    const AlertPresenter& Alerts() const noexcept { return alerts_; }

    void PaintToast(const ToastPaintResources& resources,
        ToastPresenter::TimePoint now = ToastPresenter::Clock::now()) const;
    void PaintAlert(const AlertPaintResources& resources);
    void PaintMediaCard(ID2D1RenderTarget* target,
        const MediaCardPresentation& presentation,
        const MediaCardBrushes& brushes) const;
    void PaintOverlayLabel(ID2D1RenderTarget* target, const D2D1_RECT_F& rect,
        float radius, std::wstring_view text, IDWriteTextFormat* format,
        ID2D1Brush* fill, ID2D1Brush* textBrush,
        ID2D1Brush* border = nullptr, float borderWidth = 1.0f) const;
    void PaintSingleLineText(ID2D1RenderTarget* target, std::wstring_view text,
        IDWriteTextFormat* format, const D2D1_RECT_F& rect,
        ID2D1Brush* brush) const;
    void PaintText(ID2D1RenderTarget* target, std::wstring_view text,
        IDWriteTextFormat* format, const D2D1_RECT_F& rect, ID2D1Brush* brush,
        D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_NONE) const;
    void PaintCenteredText(ID2D1RenderTarget* target, std::wstring_view text,
        IDWriteTextFormat* format, const D2D1_RECT_F& rect, ID2D1Brush* brush) const;
    static void PaintChevron(HDC dc, const RECT& rect, COLORREF color, UINT dpi);

private:
    struct PopupRuntime;

    static LRESULT CALLBACK ButtonSubclassProc(HWND window, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR referenceData);
    static LRESULT CALLBACK OwnerDrawStaticSubclassProc(HWND window, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR referenceData);
    static LRESULT CALLBACK PopupWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandlePopupMessage(HWND window, PopupRuntime& runtime,
        UINT message, WPARAM wParam, LPARAM lParam);
    void PaintPopup(HWND window, PopupRuntime& runtime) const;

    static int ScaleDip(int dip, UINT dpi) noexcept;
    static void ApplyRoundedRegion(HWND window, int width, int height, int radius, bool redraw = true) noexcept;
    static void PaintThemeGlyph(HDC dc, const RECT& rect, COLORREF color, UINT dpi, bool lightTheme);
    ToastPresenter toasts_{};
    AlertPresenter alerts_{};
};

} // namespace quicksift::ui::framework
