// OWNER: Reusable framework form dialog (owned modal window with document chrome,
// section headers, labelled rows, D2D text inputs, dropdowns and a footer) and the
// shared folder picker. Feature code describes rows; it never creates HWNDs itself.
#include "ui/framework/macos_ui_framework.h"
#include "ui/ui_layout_metrics.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

namespace quicksift::ui::framework {
namespace {

using Microsoft::WRL::ComPtr;

inline constexpr wchar_t kFormDialogClass[] = L"QuickSift.Framework.FormDialog";
inline constexpr UINT_PTR kFormChildSubclassId = 0x51464F52; // 'QFOR'
inline constexpr UINT kFormDestroyMessage = WM_APP + 0x3F0;

// DIP metrics (one place; keep in sync with the document windows' chrome).
inline constexpr int kOuterMarginDip = 14;
inline constexpr int kPanelPaddingDip = 18;
inline constexpr int kPanelRadiusDip = 16;
inline constexpr int kSectionHeightDip = 30;
inline constexpr int kSectionGapAboveDip = 10;
inline constexpr int kRowGapDip = 10;
inline constexpr int kControlHeightDip = 34;
inline constexpr int kErrorLineDip = 18;
inline constexpr int kStatusLineDip = 20;
inline constexpr int kFooterButtonHeightDip = 36;
inline constexpr int kFooterButtonMinWidthDip = 112;
inline constexpr int kRowButtonMinWidthDip = 150;
inline constexpr int kDropdownMaxWidthDip = 300;
inline constexpr int kDefaultTextAreaDip = 140;
inline constexpr int kDefaultNoteDip = 36;
inline constexpr int kLabelGapDip = 10;

int Px(int dip, UINT dpi) noexcept {
    return MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

struct FormControl {
    FormRow row;
    HWND window = nullptr;
    RECT rowRect{};
    RECT labelRect{};
    RECT controlRect{};
    RECT errorRect{};
    RECT noteRect{};
    std::wstring error;
};

struct FormRuntime {
    MacOsUiFramework* framework = nullptr;
    FormDialogSpec spec;
    std::vector<FormControl> controls;
    HWND dialog = nullptr;
    HWND primary = nullptr;
    HWND cancel = nullptr;
    HFONT font = nullptr;
    HFONT semibold = nullptr;
    HFONT titleFont = nullptr;
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    RECT panelRect{};
    RECT statusRect{};
    std::wstring status;
    bool statusError = false;
    bool busy = false;
    bool keyboardCues = false;
    bool closeHot = false;
    bool closePressed = false;
    bool trackingMouse = false;
    bool ownerDisabled = false;
    bool closing = false;
    bool inDropdown = false;
    HWND lastFocus = nullptr;
};

FormRuntime* RuntimeOf(HWND dialog) noexcept {
    if (!dialog || !IsWindow(dialog)) return nullptr;
    wchar_t className[64]{};
    if (!GetClassNameW(dialog, className, static_cast<int>(std::size(className)))) return nullptr;
    if (wcscmp(className, kFormDialogClass) != 0) return nullptr;
    return reinterpret_cast<FormRuntime*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
}

FormControl* FindControl(FormRuntime& runtime, int id) noexcept {
    for (FormControl& control : runtime.controls)
        if (control.row.id == id && control.row.kind != FormRowKind::SectionHeader) return &control;
    return nullptr;
}

FormControl* FindControl(FormRuntime& runtime, HWND window) noexcept {
    if (!window) return nullptr;
    for (FormControl& control : runtime.controls)
        if (control.window == window) return &control;
    return nullptr;
}

std::wstring WindowText(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) return {};
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(std::max(0, copied)));
    return text;
}

void RecreateFonts(FormRuntime& runtime) {
    for (HFONT* font : { &runtime.font, &runtime.semibold, &runtime.titleFont }) {
        if (*font) DeleteObject(*font);
        *font = nullptr;
    }
    const auto make = [&](int points, int weight, const wchar_t* face) {
        return CreateFontW(-MulDiv(points, static_cast<int>(runtime.dpi), 72), 0, 0, 0, weight,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
    };
    // Same faces/sizes as the main window (RecreateUiFonts).
    runtime.font = make(tokens::kTypeBodyPt, FW_NORMAL, UiFontFamily(FontRole::Text));
    runtime.semibold = make(tokens::kTypeCaptionPt, FW_SEMIBOLD, UiFontFamily(FontRole::Display));
    runtime.titleFont = make(tokens::kTypeHeadlinePt, FW_SEMIBOLD, UiFontFamily(FontRole::Display));
}

int MeasureText(HWND window, HFONT font, std::wstring_view text) {
    HDC dc = GetDC(window);
    if (!dc) return static_cast<int>(text.size()) * 8;
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    SIZE extent{};
    GetTextExtentPoint32W(dc, text.data(), static_cast<int>(text.size()), &extent);
    if (old) SelectObject(dc, old);
    ReleaseDC(window, dc);
    return static_cast<int>(extent.cx);
}

RECT CloseLightRect(HWND dialog, UINT dpi, bool hitArea) {
    RECT client{};
    GetClientRect(dialog, &client);
    const int titleHeight = Px(layout::kTitleBarHeightDip, dpi);
    const int diameter = Px(12, dpi);
    const int rightMargin = Px(15, dpi);
    RECT rect{ client.right - rightMargin - diameter, (titleHeight - diameter) / 2,
        client.right - rightMargin, (titleHeight - diameter) / 2 + diameter };
    if (hitArea) InflateRect(&rect, Px(4, dpi), Px(5, dpi));
    return rect;
}

bool IsFocusable(HWND window) noexcept {
    return window && IsWindowVisible(window) && IsWindowEnabled(window);
}

std::vector<HWND> FocusOrder(const FormRuntime& runtime) {
    std::vector<HWND> order;
    for (const FormControl& control : runtime.controls)
        if (IsFocusable(control.window)) order.push_back(control.window);
    if (IsFocusable(runtime.cancel)) order.push_back(runtime.cancel);
    if (IsFocusable(runtime.primary)) order.push_back(runtime.primary);
    return order;
}

void InvalidateButtons(const FormRuntime& runtime) {
    for (const FormControl& control : runtime.controls)
        if (control.window && (control.row.kind == FormRowKind::Button ||
            control.row.kind == FormRowKind::Dropdown)) InvalidateRect(control.window, nullptr, FALSE);
    if (runtime.primary) InvalidateRect(runtime.primary, nullptr, FALSE);
    if (runtime.cancel) InvalidateRect(runtime.cancel, nullptr, FALSE);
}

void FocusControl(FormRuntime& runtime, HWND window) {
    if (!window) return;
    runtime.lastFocus = window;
    SetFocus(window);
}

void MoveFocus(FormRuntime& runtime, bool backwards) {
    const std::vector<HWND> order = FocusOrder(runtime);
    if (order.empty()) return;
    const HWND current = GetFocus();
    auto it = std::find(order.begin(), order.end(), current);
    std::size_t index = 0;
    if (it == order.end()) {
        index = backwards ? order.size() - 1 : 0;
    } else {
        const std::size_t position = static_cast<std::size_t>(it - order.begin());
        index = backwards ? (position + order.size() - 1) % order.size()
                          : (position + 1) % order.size();
    }
    if (!runtime.keyboardCues) {
        runtime.keyboardCues = true;
        InvalidateButtons(runtime);
    }
    FocusControl(runtime, order[index]);
}

void Fire(FormRuntime& runtime, int id, FormEvent event) {
    if (runtime.closing) return;
    if (runtime.spec.onEvent) {
        // Copy: the handler may close the dialog (destruction is deferred, but the
        // callback object must stay alive for the duration of this call).
        const auto handler = runtime.spec.onEvent;
        handler(runtime.dialog, id, event);
    } else if (event == FormEvent::Clicked &&
        (id == runtime.spec.cancelId || id == runtime.spec.primaryId) && runtime.framework) {
        runtime.framework->CloseFormDialog(runtime.dialog);
    }
}

void Cancel(FormRuntime& runtime) {
    // Esc / close light / Alt+F4 are always available, even while busy, so the
    // feature can cancel its own work.
    Fire(runtime, runtime.spec.cancelId, FormEvent::Clicked);
}

void Submit(FormRuntime& runtime) {
    if (runtime.busy || !IsFocusable(runtime.primary)) return;
    Fire(runtime, runtime.spec.primaryId, FormEvent::Clicked);
}

void OpenDropdown(FormRuntime& runtime, FormControl& control) {
    if (!runtime.framework || control.row.options.empty() || runtime.inDropdown) return;
    DropdownRequest request{};
    request.owner = runtime.dialog;
    request.anchor = control.window;
    request.instance = runtime.spec.instance;
    request.palette = runtime.spec.palette;
    request.font = runtime.font;
    request.dpi = runtime.dpi;
    request.maintenancePump = runtime.spec.maintenancePump;
    request.errorSink = runtime.spec.errorSink;
    for (std::size_t index = 0; index < control.row.options.size(); ++index) {
        MenuItem item{};
        item.text = control.row.options[index];
        item.command = static_cast<int>(index) + 1;
        item.checked = static_cast<int>(index) == control.row.selected;
        request.items.push_back(std::move(item));
    }
    const HWND dialog = runtime.dialog;
    const HWND anchor = control.window;
    const int id = control.row.id;
    runtime.inDropdown = true;
    const int result = runtime.framework->TrackDropdown(std::move(request));
    // The dialog may have been closed while the popup pumped messages.
    FormRuntime* live = RuntimeOf(dialog);
    if (!live) return;
    live->inDropdown = false;
    if (IsWindow(anchor)) SetFocus(anchor);
    FormControl* again = FindControl(*live, id);
    if (!again || result <= 0) return;
    const int selected = result - 1;
    if (selected == again->row.selected ||
        selected >= static_cast<int>(again->row.options.size())) return;
    again->row.selected = selected;
    SetWindowTextW(again->window, again->row.options[static_cast<std::size_t>(selected)].c_str());
    InvalidateRect(again->window, nullptr, FALSE);
    Fire(*live, id, FormEvent::SelectionChanged);
}

LRESULT CALLBACK FormChildSubclassProc(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR subclassId, DWORD_PTR reference) {
    FormRuntime* runtime = RuntimeOf(reinterpret_cast<HWND>(reference));
    if (!runtime) {
        if (message == WM_NCDESTROY) RemoveWindowSubclass(window, FormChildSubclassProc, subclassId);
        return DefSubclassProc(window, message, wParam, lParam);
    }
    const FormControl* control = FindControl(*runtime, window);
    const bool multiline = control && control->row.kind == FormRowKind::TextArea;
    const bool textInput = MacOsUiFramework::IsTextInput(window);
    const bool isButton = !textInput;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    switch (message) {
    case WM_SETFOCUS:
        runtime->lastFocus = window;
        break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        if (runtime->keyboardCues) {
            runtime->keyboardCues = false;
            InvalidateButtons(*runtime);
        }
        break;
    case WM_KEYDOWN:
        switch (wParam) {
        case VK_TAB:
            MoveFocus(*runtime, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            return 0;
        case VK_ESCAPE:
            Cancel(*runtime);
            return 0;
        case VK_RETURN:
            if (ctrl) { Submit(*runtime); return 0; }
            if (multiline) break; // newline
            if (isButton) {
                if (control && control->row.kind == FormRowKind::Dropdown) {
                    OpenDropdown(*runtime, *const_cast<FormControl*>(control));
                } else if (IsWindowEnabled(window)) {
                    SendMessageW(window, BM_CLICK, 0, 0);
                }
                return 0;
            }
            Submit(*runtime);
            return 0;
        case VK_DOWN:
        case VK_UP:
        case VK_F4:
            if (control && control->row.kind == FormRowKind::Dropdown) {
                OpenDropdown(*runtime, *const_cast<FormControl*>(control));
                return 0;
            }
            break;
        default:
            break;
        }
        break;
    case WM_CHAR:
        if (wParam == L'\t' || wParam == 27) return 0;
        if (wParam == L'\r' && (!multiline || ctrl)) return 0;
        if (wParam == 10 /* Ctrl+Enter */) return 0;
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, FormChildSubclassProc, subclassId);
        break;
    default:
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

int LayoutForm(FormRuntime& runtime, int clientWidth, bool move) {
    const UINT dpi = runtime.dpi;
    const int titleHeight = Px(layout::kTitleBarHeightDip, dpi);
    const int margin = Px(kOuterMarginDip, dpi);
    const int padding = Px(kPanelPaddingDip, dpi);
    const int panelLeft = margin;
    const int panelRight = clientWidth - margin;
    const int panelTop = titleHeight + Px(16, dpi);
    const int x0 = panelLeft + padding;
    const int x1 = std::max(x0 + Px(80, dpi), panelRight - padding);
    const int labelWidth = Px(runtime.spec.labelWidthDip, dpi);
    const int controlLeft = std::min(x1 - Px(60, dpi), x0 + labelWidth + Px(kLabelGapDip, dpi));
    const int controlHeight = Px(kControlHeightDip, dpi);
    const int gap = Px(kRowGapDip, dpi);
    const int radius = Px(layout::kButtonRadiusDip, dpi);
    int y = panelTop + padding;
    bool first = true;
    const auto place = [&](HWND window, const RECT& rect, bool roundButton) {
        if (!window || !move) return;
        const int width = std::max(1, static_cast<int>(rect.right - rect.left));
        const int height = std::max(1, static_cast<int>(rect.bottom - rect.top));
        SetWindowPos(window, nullptr, rect.left, rect.top, width, height,
            SWP_NOZORDER | SWP_NOACTIVATE);
        if (roundButton && runtime.framework)
            runtime.framework->ApplyAntialiasedControlRegion(window, width, height, radius);
    };
    for (FormControl& control : runtime.controls) {
        control.labelRect = control.controlRect = control.errorRect = control.noteRect = RECT{};
        switch (control.row.kind) {
        case FormRowKind::SectionHeader: {
            if (!first) y += Px(kSectionGapAboveDip, dpi);
            control.rowRect = { x0, y, x1, y + Px(kSectionHeightDip, dpi) };
            y = control.rowRect.bottom + gap;
            break;
        }
        case FormRowKind::TextArea: {
            const int height = Px(control.row.heightDip > 0 ? control.row.heightDip : kDefaultTextAreaDip, dpi);
            control.controlRect = { x0, y, x1, y + height };
            control.rowRect = control.controlRect;
            place(control.window, control.controlRect, false);
            y = control.rowRect.bottom + gap;
            break;
        }
        case FormRowKind::TextField: {
            control.labelRect = { x0, y, controlLeft - Px(kLabelGapDip, dpi), y + controlHeight };
            control.controlRect = { controlLeft, y, x1, y + controlHeight };
            control.errorRect = { controlLeft + Px(4, dpi), y + controlHeight + Px(2, dpi),
                x1, y + controlHeight + Px(2 + kErrorLineDip, dpi) };
            control.rowRect = { x0, y, x1, control.errorRect.bottom };
            place(control.window, control.controlRect, false);
            y = control.rowRect.bottom + Px(4, dpi);
            break;
        }
        case FormRowKind::Dropdown: {
            control.labelRect = { x0, y, controlLeft - Px(kLabelGapDip, dpi), y + controlHeight };
            control.controlRect = { controlLeft, y,
                std::min(x1, controlLeft + Px(kDropdownMaxWidthDip, dpi)), y + controlHeight };
            control.rowRect = { x0, y, x1, y + controlHeight };
            place(control.window, control.controlRect, true);
            y = control.rowRect.bottom + gap;
            break;
        }
        case FormRowKind::Button: {
            const int textWidth = MeasureText(runtime.dialog, runtime.semibold, control.row.label);
            const int width = std::min(x1 - x0, std::max(Px(kRowButtonMinWidthDip, dpi),
                textWidth + Px(layout::kButtonTextPaddingDip * 2 + 12, dpi)));
            control.controlRect = { x0, y, x0 + width, y + controlHeight };
            control.noteRect = { control.controlRect.right + Px(12, dpi), y, x1, y + controlHeight };
            control.rowRect = { x0, y, x1, y + controlHeight };
            place(control.window, control.controlRect, true);
            y = control.rowRect.bottom + gap;
            break;
        }
        case FormRowKind::Note: {
            const int height = Px(control.row.heightDip > 0 ? control.row.heightDip : kDefaultNoteDip, dpi);
            control.noteRect = { x0 + Px(2, dpi), y, x1, y + height };
            control.rowRect = control.noteRect;
            y = control.rowRect.bottom + gap;
            break;
        }
        }
        first = false;
    }
    runtime.statusRect = { x0 + Px(2, dpi), y, x1, y + Px(kStatusLineDip, dpi) };
    y = runtime.statusRect.bottom + Px(8, dpi);
    const int footerHeight = Px(kFooterButtonHeightDip, dpi);
    const auto footerWidth = [&](HWND button) {
        if (!button) return 0;
        return std::max(Px(kFooterButtonMinWidthDip, dpi),
            MeasureText(runtime.dialog, runtime.semibold, WindowText(button)) +
                Px(layout::kButtonTextPaddingDip * 2 + 16, dpi));
    };
    const int primaryWidth = footerWidth(runtime.primary);
    const int cancelWidth = footerWidth(runtime.cancel);
    RECT primaryRect{ x1 - primaryWidth, y, x1, y + footerHeight };
    RECT cancelRect{ primaryRect.left - Px(12, dpi) - cancelWidth, y,
        primaryRect.left - Px(12, dpi), y + footerHeight };
    place(runtime.primary, primaryRect, true);
    place(runtime.cancel, cancelRect, true);
    y += footerHeight;
    runtime.panelRect = { panelLeft, panelTop, panelRight, y + padding };
    return runtime.panelRect.bottom + margin;
}

void ApplyDwmChrome(HWND dialog, const ThemePalette& palette) {
    MacOsUiFramework::ApplyWindowChrome(dialog, WindowChromeKind::Dialog, palette);
}

void ApplyChildTheme(FormRuntime& runtime) {
    for (FormControl& control : runtime.controls) {
        if (!control.window) continue;
        if (MacOsUiFramework::IsTextInput(control.window) && runtime.framework) {
            runtime.framework->SetD2DTextInputPalette(control.window, runtime.spec.palette, runtime.dpi);
            runtime.framework->SetTextInputParentSurface(control.window, runtime.spec.palette.panelStrong);
            runtime.framework->SetTextInputError(control.window, !control.error.empty());
        } else {
            SendMessageW(control.window, WM_SETFONT, reinterpret_cast<WPARAM>(runtime.semibold), FALSE);
        }
        InvalidateRect(control.window, nullptr, FALSE);
    }
    for (HWND button : { runtime.primary, runtime.cancel }) {
        if (!button) continue;
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(runtime.semibold), FALSE);
        InvalidateRect(button, nullptr, FALSE);
    }
}

void Relayout(FormRuntime& runtime, const RECT* suggested) {
    RECT window{};
    GetWindowRect(runtime.dialog, &window);
    const int width = Px(runtime.spec.widthDip, runtime.dpi);
    const int height = LayoutForm(runtime, width, false);
    const int left = suggested ? suggested->left : window.left;
    const int top = suggested ? suggested->top : window.top;
    SetWindowPos(runtime.dialog, nullptr, left, top, width, height,
        SWP_NOZORDER | SWP_NOACTIVATE);
    RECT client{};
    GetClientRect(runtime.dialog, &client);
    LayoutForm(runtime, client.right > 0 ? client.right : width, true);
    InvalidateRect(runtime.dialog, nullptr, FALSE);
}

void PaintForm(FormRuntime& runtime, HDC target) {
    if (!runtime.framework) return;
    RECT client{};
    GetClientRect(runtime.dialog, &client);
    const int width = std::max(1, static_cast<int>(client.right));
    const int height = std::max(1, static_cast<int>(client.bottom));
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = dc ? CreateCompatibleBitmap(target, width, height) : nullptr;
    HDC paint = target;
    HGDIOBJ oldBitmap = nullptr;
    if (dc && bitmap) {
        oldBitmap = SelectObject(dc, bitmap);
        paint = dc;
    }
    const ThemePalette& palette = runtime.spec.palette;
    const UINT dpi = runtime.dpi;
    MacOsUiFramework& framework = *runtime.framework;
    framework.PaintWindowBackground(paint, client, palette);
    const int titleHeight = Px(layout::kTitleBarHeightDip, dpi);
    TitleBarPresentation title{};
    title.title = WindowText(runtime.dialog);
    title.titleTextRect = { Px(16, dpi), 0, client.right - Px(80, dpi), titleHeight };
    title.titleFont = runtime.semibold;
    title.active = GetActiveWindow() == runtime.dialog;
    title.trafficLights[0].rect = CloseLightRect(runtime.dialog, dpi, false);
    title.trafficLights[0].color = RGB(255, 95, 87);
    title.trafficLights[0].hot = runtime.closeHot;
    title.trafficLights[0].pressed = runtime.closePressed;
    framework.PaintTitleBar(paint, client, titleHeight, title, palette, dpi);
    RECT accent = client;
    accent.left += Px(22, dpi);
    accent.right -= Px(22, dpi);
    accent.top = titleHeight + Px(7, dpi);
    accent.bottom = titleHeight + Px(10, dpi);
    framework.PaintAccentStrip(paint, accent, palette);
    framework.PaintPanel(paint, runtime.panelRect, PanelPresentation{ false, false, kPanelRadiusDip },
        palette, dpi);

    SetBkMode(paint, TRANSPARENT);
    const auto drawText = [&](const std::wstring& text, RECT rect, COLORREF color, HFONT font, UINT format) {
        if (text.empty() || IsRectEmpty(&rect)) return;
        HGDIOBJ old = font ? SelectObject(paint, font) : nullptr;
        SetTextColor(paint, color);
        DrawTextW(paint, text.c_str(), static_cast<int>(text.size()), &rect, format | DT_NOPREFIX);
        if (old) SelectObject(paint, old);
    };
    const UINT singleLine = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
    for (const FormControl& control : runtime.controls) {
        switch (control.row.kind) {
        case FormRowKind::SectionHeader:
            framework.PaintSectionTitle(paint, control.rowRect, control.row.label, palette, dpi,
                runtime.semibold);
            break;
        case FormRowKind::TextField:
        case FormRowKind::Dropdown:
            drawText(control.row.label, control.labelRect,
                runtime.busy ? palette.muted : palette.text, runtime.font, singleLine);
            if (!control.error.empty())
                drawText(control.error, control.errorRect, palette.dangerBorder, runtime.font, singleLine);
            break;
        case FormRowKind::Button:
            drawText(control.row.note, control.noteRect, palette.muted, runtime.font, singleLine);
            break;
        case FormRowKind::Note:
            drawText(control.row.note.empty() ? control.row.label : control.row.note,
                control.noteRect, palette.muted, runtime.font, DT_LEFT | DT_TOP | DT_WORDBREAK);
            break;
        case FormRowKind::TextArea:
            break;
        }
    }
    drawText(runtime.status, runtime.statusRect,
        runtime.statusError ? palette.dangerBorder : palette.muted, runtime.font, singleLine);

    if (paint != target) {
        BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBitmap);
    }
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
}

void PaintFormButton(FormRuntime& runtime, const DRAWITEMSTRUCT& draw) {
    if (!runtime.framework) return;
    ButtonPresentation presentation{};
    presentation.text = WindowText(draw.hwndItem);
    presentation.role = ButtonRole::Standard;
    presentation.state.hot = runtime.framework->IsButtonHot(draw.hwndItem);
    presentation.state.pressed = (draw.itemState & ODS_SELECTED) != 0;
    presentation.state.disabled = (draw.itemState & ODS_DISABLED) != 0;
    presentation.state.focused = runtime.keyboardCues && (draw.itemState & ODS_FOCUS) != 0;
    presentation.state.selected = draw.hwndItem == runtime.primary && !presentation.state.disabled;
    const FormControl* control = FindControl(runtime, draw.hwndItem);
    presentation.state.popup = control && control->row.kind == FormRowKind::Dropdown;
    presentation.hoverBlend = runtime.framework->ButtonHoverBlend(draw.hwndItem);
    presentation.pressBlend = presentation.state.pressed ? 1.0f : 0.0f;
    presentation.parentSurface = static_cast<std::uint32_t>(runtime.spec.palette.panelStrong);
    runtime.framework->PaintButton(draw.hDC, draw.rcItem, presentation, runtime.spec.palette,
        runtime.dpi, runtime.font, runtime.semibold);
}

void SetOwnerEnabled(FormRuntime& runtime, bool enabled) {
    const HWND owner = runtime.spec.owner;
    if (!owner || !IsWindow(owner)) return;
    if (enabled && runtime.ownerDisabled) {
        EnableWindow(owner, TRUE);
        runtime.ownerDisabled = false;
    } else if (!enabled && !runtime.ownerDisabled) {
        EnableWindow(owner, FALSE);
        runtime.ownerDisabled = true;
    }
}

LRESULT HandleFormMessage(HWND dialog, FormRuntime& runtime, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;
    case WM_NCPAINT:
        return 0;
    case WM_NCACTIVATE:
        InvalidateRect(dialog, nullptr, FALSE);
        return TRUE;
    case WM_NCHITTEST: {
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(dialog, &point);
        const RECT close = CloseLightRect(dialog, runtime.dpi, true);
        if (PtInRect(&close, point)) return HTCLIENT;
        if (point.y >= 0 && point.y < Px(layout::kTitleBarHeightDip, runtime.dpi)) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(dialog, &paint);
        if (dc) PaintForm(runtime, dc);
        EndPaint(dialog, &paint);
        return 0;
    }
    case WM_DRAWITEM: {
        const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (draw && draw->CtlType == ODT_BUTTON) {
            PaintFormButton(runtime, *draw);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        const HWND source = reinterpret_cast<HWND>(lParam);
        if (code == EN_CHANGE && source) {
            Fire(runtime, id, FormEvent::TextChanged);
            return 0;
        }
        if (code == BN_CLICKED && source) {
            if (FormControl* control = FindControl(runtime, source)) {
                if (control->row.kind == FormRowKind::Dropdown) {
                    OpenDropdown(runtime, *control);
                    return 0;
                }
            }
            if (source == runtime.primary) Submit(runtime);
            else if (source == runtime.cancel) Cancel(runtime);
            else Fire(runtime, id, FormEvent::Clicked);
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
        // Focus is on the dialog itself (e.g. every control disabled while busy).
        if (wParam == VK_ESCAPE) { Cancel(runtime); return 0; }
        if (wParam == VK_TAB) { MoveFocus(runtime, (GetKeyState(VK_SHIFT) & 0x8000) != 0); return 0; }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            const HWND focus = GetFocus();
            if (focus && IsChild(dialog, focus)) runtime.lastFocus = focus;
        } else {
            HWND target = IsFocusable(runtime.lastFocus) ? runtime.lastFocus : nullptr;
            if (!target) {
                const std::vector<HWND> order = FocusOrder(runtime);
                if (!order.empty()) target = order.front();
            }
            SetFocus(target ? target : dialog);
        }
        InvalidateRect(dialog, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const RECT close = CloseLightRect(dialog, runtime.dpi, true);
        const bool hot = PtInRect(&close, point) != FALSE;
        if (hot != runtime.closeHot) {
            runtime.closeHot = hot;
            InvalidateRect(dialog, nullptr, FALSE);
        }
        if (!runtime.trackingMouse) {
            TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, dialog, 0 };
            runtime.trackingMouse = TrackMouseEvent(&track) != FALSE;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        runtime.trackingMouse = false;
        if (runtime.closeHot) {
            runtime.closeHot = false;
            if (!runtime.closePressed) InvalidateRect(dialog, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        if (runtime.keyboardCues) {
            runtime.keyboardCues = false;
            InvalidateButtons(runtime);
        }
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const RECT close = CloseLightRect(dialog, runtime.dpi, true);
        if (PtInRect(&close, point)) {
            runtime.closePressed = true;
            runtime.closeHot = true;
            SetCapture(dialog);
            InvalidateRect(dialog, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (runtime.closePressed) {
            runtime.closePressed = false;
            ReleaseCapture();
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const RECT close = CloseLightRect(dialog, runtime.dpi, true);
            const bool activate = PtInRect(&close, point) != FALSE;
            runtime.closeHot = activate;
            InvalidateRect(dialog, nullptr, FALSE);
            if (activate) Cancel(runtime);
            return 0;
        }
        break;
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        if (runtime.closePressed && (message == WM_CANCELMODE ||
            reinterpret_cast<HWND>(lParam) != dialog)) {
            runtime.closePressed = false;
            runtime.closeHot = false;
            if (GetCapture() == dialog) ReleaseCapture();
            InvalidateRect(dialog, nullptr, FALSE);
        }
        break;
    case WM_CLOSE:
        Cancel(runtime);
        return 0;
    case WM_DPICHANGED: {
        runtime.dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, HIWORD(wParam));
        RecreateFonts(runtime);
        ApplyChildTheme(runtime);
        Relayout(runtime, reinterpret_cast<const RECT*>(lParam));
        return 0;
    }
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        InvalidateRect(dialog, nullptr, FALSE);
        break;
    case kFormDestroyMessage:
        DestroyWindow(dialog);
        return 0;
    case WM_DESTROY:
        SetOwnerEnabled(runtime, true);
        break;
    default:
        break;
    }
    return DefWindowProcW(dialog, message, wParam, lParam);
}

LRESULT CALLBACK FormDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(dialog, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(create ? create->lpCreateParams : nullptr));
        if (auto* runtime = static_cast<FormRuntime*>(create ? create->lpCreateParams : nullptr))
            runtime->dialog = dialog;
        return DefWindowProcW(dialog, message, wParam, lParam);
    }
    auto* runtime = reinterpret_cast<FormRuntime*>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(dialog, GWLP_USERDATA, 0);
        if (runtime) {
            std::unique_ptr<FormRuntime> owned(runtime);
            SetOwnerEnabled(*owned, true);
            const auto handler = std::move(owned->spec.onEvent);
            for (HFONT font : { owned->font, owned->semibold, owned->titleFont })
                if (font) DeleteObject(font);
            owned.reset();
            if (handler) {
                try { handler(dialog, 0, FormEvent::Destroyed); } catch (...) {}
            }
        }
        return DefWindowProcW(dialog, message, wParam, lParam);
    }
    if (!runtime) return DefWindowProcW(dialog, message, wParam, lParam);
    try {
        return HandleFormMessage(dialog, *runtime, message, wParam, lParam);
    } catch (...) {
        if (runtime->spec.errorSink) {
            try { runtime->spec.errorSink(L"Unhandled exception in a framework form dialog"); } catch (...) {}
        }
        return DefWindowProcW(dialog, message, wParam, lParam);
    }
}

bool EnsureFormClass(HINSTANCE instance) noexcept {
    WNDCLASSEXW existing{ sizeof(WNDCLASSEXW) };
    if (GetClassInfoExW(instance, kFormDialogClass, &existing)) return true;
    WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = FormDialogProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kFormDialogClass;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

} // namespace

HWND MacOsUiFramework::CreateFormDialog(FormDialogSpec spec) {
    if (!spec.instance || !EnsureFormClass(spec.instance)) {
        if (spec.errorSink) spec.errorSink(L"Registering the framework form-dialog class");
        return nullptr;
    }
    auto runtime = std::make_unique<FormRuntime>();
    runtime->framework = this;
    runtime->dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI,
        spec.owner ? GetWindowDpi(spec.owner) : USER_DEFAULT_SCREEN_DPI);
    runtime->spec = std::move(spec);
    for (FormRow& row : runtime->spec.rows) {
        FormControl control{};
        if (row.kind == FormRowKind::Dropdown)
            row.selected = std::clamp(row.selected, 0, std::max(0, static_cast<int>(row.options.size()) - 1));
        control.row = std::move(row);
        runtime->controls.push_back(std::move(control));
    }
    runtime->spec.rows.clear();
    RecreateFonts(*runtime);

    const FormDialogSpec& s = runtime->spec;
    const int width = Px(s.widthDip, runtime->dpi);
    RECT owner{ 0, 0, width, Px(600, runtime->dpi) };
    if (s.owner) GetWindowRect(s.owner, &owner);
    FormRuntime* raw = runtime.get();
    HWND dialog = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, kFormDialogClass,
        s.title.c_str(), WS_OVERLAPPED | WS_THICKFRAME | WS_CLIPCHILDREN,
        owner.left, owner.top, width, Px(400, runtime->dpi), s.owner, nullptr, s.instance, raw);
    if (!dialog) {
        if (s.errorSink) s.errorSink(L"Creating a framework form dialog");
        for (HFONT font : { raw->font, raw->semibold, raw->titleFont }) if (font) DeleteObject(font);
        return nullptr;
    }
    runtime.release(); // owned by the window from here (deleted in WM_NCDESTROY)
    raw->dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetWindowDpi(dialog));
    RecreateFonts(*raw);
    ApplyDwmChrome(dialog, raw->spec.palette);

    const HINSTANCE instance = raw->spec.instance;
    bool failed = false;
    for (FormControl& control : raw->controls) {
        switch (control.row.kind) {
        case FormRowKind::TextArea:
        case FormRowKind::TextField: {
            TextInputOptions options{};
            options.multiline = control.row.kind == FormRowKind::TextArea;
            options.placeholder = control.row.placeholder;
            options.notifyParent = true;
            control.window = CreateTextInput(dialog, instance, control.row.id, options, control.row.text);
            break;
        }
        case FormRowKind::Dropdown: {
            const std::wstring caption = control.row.options.empty() ? std::wstring{}
                : control.row.options[static_cast<std::size_t>(control.row.selected)];
            control.window = CreateButton({ dialog, instance, control.row.id, caption });
            break;
        }
        case FormRowKind::Button:
            control.window = CreateButton({ dialog, instance, control.row.id, control.row.label });
            break;
        case FormRowKind::SectionHeader:
        case FormRowKind::Note:
            break;
        }
        const bool needsWindow = control.row.kind != FormRowKind::SectionHeader &&
            control.row.kind != FormRowKind::Note;
        if (needsWindow && !control.window) failed = true;
    }
    if (!raw->spec.cancelText.empty())
        raw->cancel = CreateButton({ dialog, instance, raw->spec.cancelId, raw->spec.cancelText });
    if (!raw->spec.primaryText.empty())
        raw->primary = CreateButton({ dialog, instance, raw->spec.primaryId, raw->spec.primaryText });
    if (failed || (!raw->spec.cancelText.empty() && !raw->cancel) ||
        (!raw->spec.primaryText.empty() && !raw->primary)) {
        if (raw->spec.errorSink) raw->spec.errorSink(L"Creating framework form-dialog controls");
        raw->spec.onEvent = nullptr;
        DestroyWindow(dialog);
        return nullptr;
    }
    const auto subclass = [&](HWND child) {
        if (child) SetWindowSubclass(child, FormChildSubclassProc, kFormChildSubclassId,
            reinterpret_cast<DWORD_PTR>(dialog));
    };
    for (FormControl& control : raw->controls) subclass(control.window);
    subclass(raw->primary);
    subclass(raw->cancel);
    ApplyChildTheme(*raw);

    // Size to content and centre over the owner.
    const int height = LayoutForm(*raw, width, false);
    const int x = owner.left + std::max<int>(0, static_cast<int>(owner.right - owner.left) - width) / 2;
    const int y = owner.top + std::max<int>(0, static_cast<int>(owner.bottom - owner.top) - height) / 3;
    SetWindowPos(dialog, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    RECT client{};
    GetClientRect(dialog, &client);
    LayoutForm(*raw, client.right > 0 ? client.right : width, true);

    const std::vector<HWND> order = FocusOrder(*raw);
    raw->lastFocus = order.empty() ? nullptr : order.front();
    SetOwnerEnabled(*raw, false);
    ShowWindow(dialog, SW_SHOWNORMAL);
    UpdateWindow(dialog);
    SetForegroundWindow(dialog);
    if (raw->lastFocus) SetFocus(raw->lastFocus);
    return dialog;
}

std::wstring MacOsUiFramework::FormText(HWND dialog, int id) {
    FormRuntime* runtime = RuntimeOf(dialog);
    if (!runtime) return {};
    const FormControl* control = FindControl(*runtime, id);
    if (!control || !control->window) return {};
    return WindowText(control->window);
}

void MacOsUiFramework::SetFormText(HWND dialog, int id, std::wstring_view text) const {
    FormRuntime* runtime = RuntimeOf(dialog);
    if (!runtime) return;
    const FormControl* control = FindControl(*runtime, id);
    if (!control || !control->window) return;
    const std::wstring value(text);
    SetWindowTextW(control->window, value.c_str());
}

int MacOsUiFramework::FormSelection(HWND dialog, int id) noexcept {
    FormRuntime* runtime = RuntimeOf(dialog);
    if (!runtime) return 0;
    const FormControl* control = FindControl(*runtime, id);
    return control ? control->row.selected : 0;
}

void MacOsUiFramework::SetFormSelection(HWND dialog, int id, int index) const {
    FormRuntime* runtime = RuntimeOf(dialog);
    if (!runtime) return;
    FormControl* control = FindControl(*runtime, id);
    if (!control || control->row.kind != FormRowKind::Dropdown || control->row.options.empty()) return;
    control->row.selected = std::clamp(index, 0, static_cast<int>(control->row.options.size()) - 1);
    if (control->window) {
        SetWindowTextW(control->window,
            control->row.options[static_cast<std::size_t>(control->row.selected)].c_str());
        InvalidateRect(control->window, nullptr, FALSE);
    }
}

void MacOsUiFramework::SetFormFieldError(HWND dialog, int id, std::wstring_view message) const {
    FormRuntime* runtime = RuntimeOf(dialog);
    if (!runtime) return;
    FormControl* control = FindControl(*runtime, id);
    if (!control || control->error == message) return;
    control->error.assign(message);
    if (control->window && IsTextInput(control->window))
        SetTextInputError(control->window, !control->error.empty());
    if (!IsRectEmpty(&control->errorRect)) InvalidateRect(dialog, &control->errorRect, FALSE);
}

void MacOsUiFramework::SetFormNote(HWND dialog, int id, std::wstring_view note) const {
    FormRuntime* runtime = RuntimeOf(dialog);
    if (!runtime) return;
    FormControl* control = FindControl(*runtime, id);
    if (!control || control->row.note == note) return;
    control->row.note.assign(note);
    if (!IsRectEmpty(&control->noteRect)) InvalidateRect(dialog, &control->noteRect, FALSE);
}

void MacOsUiFramework::SetFormStatus(HWND dialog, std::wstring_view text, bool error) const {
    FormRuntime* runtime = RuntimeOf(dialog);
    if (!runtime || (runtime->status == text && runtime->statusError == error)) return;
    runtime->status.assign(text);
    runtime->statusError = error;
    InvalidateRect(dialog, &runtime->statusRect, FALSE);
}

void MacOsUiFramework::SetFormBusy(HWND dialog, bool busy) const {
    FormRuntime* runtime = RuntimeOf(dialog);
    if (!runtime || runtime->busy == busy) return;
    runtime->busy = busy;
    const HWND focus = GetFocus();
    for (const FormControl& control : runtime->controls)
        if (control.window) EnableWindow(control.window, busy ? FALSE : TRUE);
    if (runtime->primary) EnableWindow(runtime->primary, busy ? FALSE : TRUE);
    if (busy && focus && !IsWindowEnabled(focus) && runtime->cancel) SetFocus(runtime->cancel);
    if (!busy && runtime->lastFocus && IsFocusable(runtime->lastFocus) &&
        GetActiveWindow() == dialog) SetFocus(runtime->lastFocus);
    SetCursor(LoadCursorW(nullptr, busy ? IDC_APPSTARTING : IDC_ARROW));
    InvalidateRect(dialog, nullptr, FALSE);
}

void MacOsUiFramework::SetFormPalette(HWND dialog, const ThemePalette& palette) const {
    FormRuntime* runtime = RuntimeOf(dialog);
    if (!runtime) return;
    runtime->spec.palette = palette;
    ApplyDwmChrome(dialog, palette);
    ApplyChildTheme(*runtime);
    InvalidateRect(dialog, nullptr, FALSE);
}

void MacOsUiFramework::SetFormTitle(HWND dialog, std::wstring_view title) const {
    if (!RuntimeOf(dialog)) return;
    const std::wstring value(title);
    SetWindowTextW(dialog, value.c_str());
    InvalidateRect(dialog, nullptr, FALSE);
}

void MacOsUiFramework::ShowFormDialog(HWND dialog, bool visible) const {
    FormRuntime* runtime = RuntimeOf(dialog);
    if (!runtime || runtime->closing) return;
    if (visible) {
        SetOwnerEnabled(*runtime, false);
        ShowWindow(dialog, SW_SHOWNORMAL);
        SetForegroundWindow(dialog);
        HWND target = IsFocusable(runtime->lastFocus) ? runtime->lastFocus : nullptr;
        if (!target) {
            const std::vector<HWND> order = FocusOrder(*runtime);
            if (!order.empty()) target = order.front();
        }
        if (target) SetFocus(target);
    } else {
        const HWND focus = GetFocus();
        if (focus && IsChild(dialog, focus)) runtime->lastFocus = focus;
        SetOwnerEnabled(*runtime, true);
        ShowWindow(dialog, SW_HIDE);
        if (runtime->spec.owner && IsWindow(runtime->spec.owner)) SetActiveWindow(runtime->spec.owner);
    }
}

void MacOsUiFramework::FocusFormControl(HWND dialog, int id) const {
    FormRuntime* runtime = RuntimeOf(dialog);
    if (!runtime) return;
    const FormControl* control = FindControl(*runtime, id);
    HWND target = control ? control->window : nullptr;
    if (id == runtime->spec.primaryId) target = runtime->primary;
    if (id == runtime->spec.cancelId) target = runtime->cancel;
    if (IsFocusable(target)) FocusControl(*runtime, target);
}

void MacOsUiFramework::CloseFormDialog(HWND dialog) const {
    FormRuntime* runtime = RuntimeOf(dialog);
    if (!runtime || runtime->closing) return;
    runtime->closing = true;
    SetOwnerEnabled(*runtime, true);
    ShowWindow(dialog, SW_HIDE);
    if (runtime->spec.owner && IsWindow(runtime->spec.owner)) SetActiveWindow(runtime->spec.owner);
    // Deferred so callers may close from inside onEvent / a child's notification.
    if (!PostMessageW(dialog, kFormDestroyMessage, 0, 0)) DestroyWindow(dialog);
}

bool MacOsUiFramework::IsFormDialog(HWND window) noexcept {
    return RuntimeOf(window) != nullptr;
}

std::optional<std::wstring> MacOsUiFramework::PickFolder(const FolderPickerRequest& request) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog)))) return std::nullopt;
    DWORD options = 0;
    if (FAILED(dialog->GetOptions(&options))) options = 0;
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
        FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    if (!request.title.empty()) dialog->SetTitle(request.title.c_str());
    if (!request.okLabel.empty()) dialog->SetOkButtonLabel(request.okLabel.c_str());
    if (!request.initialFolder.empty()) {
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(request.initialFolder.c_str(), nullptr,
            IID_PPV_ARGS(&folder)))) dialog->SetFolder(folder.Get());
    }
    if (FAILED(dialog->Show(request.owner))) return std::nullopt;
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item)) || !item) return std::nullopt;
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) return std::nullopt;
    std::wstring result(path);
    CoTaskMemFree(path);
    return result;
}

} // namespace quicksift::ui::framework
