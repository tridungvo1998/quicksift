// OWNER: Themed document and popup runtime implementation.
#include "app/application_support.h"

namespace quicksift::app {
// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Shared help/about/diagnostics/popup models and subclass callbacks.

// Shared UI models and the themed help/about/diagnostics document window.
// This fragment is included before QuickSiftApplication because native subclass callbacks need
// stable storage types without exposing QuickSiftApplication internals.





std::wstring NormalizeDocumentNewlines(std::wstring_view text) {
    std::wstring normalized;
    normalized.reserve(text.size() + text.size() / 16);
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t value = text[i];
        if (value == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
            normalized += L"\r\n";
        } else if (value == L'\n') {
            normalized += L"\r\n";
        } else {
            normalized.push_back(value);
        }
    }
    return normalized;
}

std::vector<std::wstring> DocumentLines(std::wstring_view text) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring_view::npos) end = text.size();
        std::wstring line(text.substr(start, end - start));
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == text.size()) break;
        start = end + 1;
    }
    return lines;
}

bool IsDocumentRule(std::wstring_view line) {
    if (line.size() < 8) return false;
    return std::all_of(line.begin(), line.end(), [](wchar_t value) {
        return value == L'=' || std::iswspace(value) != 0;
    });
}

bool IsDocumentHeading(std::wstring_view line) {
    bool hasLetter = false;
    for (wchar_t value : line) {
        if (!std::iswalpha(value)) continue;
        hasLetter = true;
        if (std::iswlower(value)) return false;
    }
    return hasLetter && line.size() <= 80;
}

void AppendRtfEscaped(std::string& output, std::wstring_view text) {
    for (wchar_t value : text) {
        if (value == L'\\' || value == L'{' || value == L'}') {
            output.push_back('\\');
            output.push_back(static_cast<char>(value));
        } else if (value == L'\t') {
            output += "\\tab ";
        } else if (value >= 0x20 && value <= 0x7e) {
            output.push_back(static_cast<char>(value));
        } else {
            const auto unit = static_cast<std::uint16_t>(value);
            const auto signedUnit = static_cast<std::int16_t>(unit);
            output += "\\u";
            output += std::to_string(static_cast<int>(signedUnit));
            output.push_back('?');
        }
    }
}

std::string BuildDocumentRtf(std::wstring_view text, bool helpDocument, const ThemePalette& theme) {
    auto appendColor = [](std::string& output, COLORREF color) {
        output += "\\red" + std::to_string(GetRValue(color));
        output += "\\green" + std::to_string(GetGValue(color));
        output += "\\blue" + std::to_string(GetBValue(color)) + ";";
    };
    std::string rtf = "{\\rtf1\\ansi\\ansicpg1252\\deff0"
        "{\\fonttbl{\\f0\\fnil Segoe UI;}}{\\colortbl;";
    appendColor(rtf, theme.text);
    appendColor(rtf, theme.muted);
    appendColor(rtf, theme.accent);
    rtf += "}\\viewkind4\\uc1\\pard\\f0\\cf1\\fs21\\sl276\\slmult1\\sa90 ";
    const auto lines = DocumentLines(text);
    bool firstContent = true;
    bool previousBlank = false;
    for (const std::wstring& original : lines) {
        std::wstring_view line = original;
        while (!line.empty() && std::iswspace(line.front())) line.remove_prefix(1);
        while (!line.empty() && std::iswspace(line.back())) line.remove_suffix(1);
        if (IsDocumentRule(line)) continue;
        if (line.empty()) {
            if (!previousBlank) rtf += "\\pard\\sa80\\par ";
            previousBlank = true;
            continue;
        }
        previousBlank = false;
        const bool bullet = line.front() == L'•';
        if (bullet) {
            line.remove_prefix(1);
            while (!line.empty() && std::iswspace(line.front())) line.remove_prefix(1);
            rtf += "\\pard\\li420\\fi-220\\tx420\\sa65\\fs21 \\u8226?\\tab ";
            AppendRtfEscaped(rtf, line);
            rtf += "\\par ";
            firstContent = false;
            continue;
        }
        const bool technical = line == L"TECHNICAL DOCUMENTATION" || line == L"TÀI LIỆU KỸ THUẬT";
        const bool heading = helpDocument && IsDocumentHeading(line);
        if (firstContent) {
            rtf += "\\pard\\keepn\\sb0\\sa180\\cf3\\b\\fs32 ";
            AppendRtfEscaped(rtf, line);
            rtf += "\\cf1\\b0\\fs21\\par ";
        } else if (technical) {
            rtf += "\\pard\\keepn\\sb360\\sa150\\cf3\\b\\fs30 ";
            AppendRtfEscaped(rtf, line);
            rtf += "\\cf1\\b0\\fs21\\par ";
        } else if (heading) {
            rtf += "\\pard\\keepn\\sb220\\sa80\\cf3\\b\\fs24 ";
            AppendRtfEscaped(rtf, line);
            rtf += "\\cf1\\b0\\fs21\\par ";
        } else {
            rtf += "\\pard\\sa90\\cf1\\fs21 ";
            AppendRtfEscaped(rtf, line);
            rtf += "\\par ";
        }
        firstContent = false;
    }
    rtf += "}";
    return rtf;
}

struct RtfInput {
    const std::string* text = nullptr;
    size_t offset = 0;
};

DWORD CALLBACK StreamDocumentRtf(DWORD_PTR cookie, LPBYTE buffer, LONG requested, LONG* written) noexcept {
    if (!written) return ERROR_INVALID_PARAMETER;
    *written = 0;
    auto* input = reinterpret_cast<RtfInput*>(cookie);
    if (!buffer || !input || !input->text || requested <= 0 || input->offset >= input->text->size()) return 0;
    const size_t count = std::min<size_t>(static_cast<size_t>(requested), input->text->size() - input->offset);
    std::memcpy(buffer, input->text->data() + input->offset, count);
    input->offset += count;
    *written = static_cast<LONG>(count);
    return 0;
}

void SetDocumentControlText(HWND edit, std::wstring_view text, bool helpDocument, bool richEdit,
    const ThemePalette& theme) {
    if (!edit) return;
    SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
    if (richEdit) {
        const std::string rtf = BuildDocumentRtf(text, helpDocument, theme);
        RtfInput input{ &rtf, 0 };
        EDITSTREAM stream{};
        stream.dwCookie = reinterpret_cast<DWORD_PTR>(&input);
        stream.pfnCallback = StreamDocumentRtf;
        SendMessageW(edit, EM_SETREADONLY, FALSE, 0);
        SetWindowTextW(edit, L"");
        SendMessageW(edit, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&stream));
        SendMessageW(edit, EM_SETREADONLY, TRUE, 0);
        SendMessageW(edit, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(theme.panel));
        SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(14, 14));
        SendMessageW(edit, EM_SETSEL, 0, 0);
        SendMessageW(edit, EM_SCROLLCARET, 0, 0);
    } else {
        const std::wstring normalized = NormalizeDocumentNewlines(text);
        SetWindowTextW(edit, normalized.c_str());
        SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(12, 12));
        SendMessageW(edit, EM_SETSEL, 0, 0);
    }
    SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(edit, nullptr, TRUE);
}

bool EnsureRichEditLoaded() {
    static HMODULE module = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return module != nullptr;
}

LRESULT CALLBACK DocumentWindowSubclassProc(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR subclassId, DWORD_PTR reference);

LRESULT CALLBACK DocumentWindowSubclassProcImpl(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR, DWORD_PTR reference) {
    auto* state = reinterpret_cast<DocumentWindowState*>(reference);
    switch (message) {
    case WM_NCCALCSIZE:
        if (wParam) return 0;
        return DefSubclassProc(window, message, wParam, lParam);
    case WM_NCPAINT:
        return 0;
    case WM_NCACTIVATE:
        return TRUE;
    case WM_NCHITTEST:
        if (state) {
            POINT screenPoint{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT windowRect{};
            GetWindowRect(window, &windowRect);
            const UINT dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, quicksift::ui::GetWindowDpi(window));
            const auto px = [dpi](int dip) { return std::max(1, MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI)); };
            const int border = px(7);
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
            if (bottom) return HTBOTTOM;
            POINT clientPoint = screenPoint;
            ScreenToClient(window, &clientPoint);
            const int titleHeight = px(quicksift::ui::layout::kTitleBarHeightDip);
            const int diameter = px(12);
            const int rightMargin = px(15);
            RECT client{};
            GetClientRect(window, &client);
            RECT closeRect{ client.right - rightMargin - diameter, (titleHeight - diameter) / 2,
                client.right - rightMargin, (titleHeight - diameter) / 2 + diameter };
            InflateRect(&closeRect, px(4), px(5));
            if (PtInRect(&closeRect, clientPoint)) return HTCLIENT;
            if (clientPoint.y >= 0 && clientPoint.y < titleHeight) return HTCAPTION;
        }
        break;
    case WM_PAINT:
        if (state && state->framework) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            state->framework->PaintWindowBackground(dc, client, state->theme);
            const UINT dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, quicksift::ui::GetWindowDpi(window));
            const auto px = [dpi](int dip) { return std::max(1, MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI)); };
            const int titleHeight = px(quicksift::ui::layout::kTitleBarHeightDip);
            quicksift::ui::framework::TitleBarPresentation title{};
            wchar_t titleText[256]{};
            GetWindowTextW(window, titleText, static_cast<int>(std::size(titleText)));
            title.title = titleText;
            title.titleTextRect = { px(16), 0, client.right - px(80), titleHeight };
            title.titleFont = state->semiboldFont ? state->semiboldFont : state->normalFont;
            title.active = GetActiveWindow() == window;
            const int diameter = px(12);
            const int rightMargin = px(15);
            const int closeTop = (titleHeight - diameter) / 2;
            title.trafficLights[0].rect = { client.right - rightMargin - diameter, closeTop,
                client.right - rightMargin, closeTop + diameter };
            title.trafficLights[0].color = RGB(255, 95, 87);
            title.trafficLights[0].hot = state->titleCloseHot;
            title.trafficLights[0].pressed = state->titleClosePressed;
            state->framework->PaintTitleBar(dc, client, titleHeight, title, state->theme, dpi);
            EndPaint(window, &paint);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        if (state && state->framework) {
            HDC dc = reinterpret_cast<HDC>(wParam);
            RECT client{};
            GetClientRect(window, &client);
            state->framework->PaintWindowBackground(dc, client, state->theme);
            const UINT dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, quicksift::ui::GetWindowDpi(window));
            const auto px = [dpi](int dip) { return std::max(1, MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI)); };
            const int titleHeight = px(quicksift::ui::layout::kTitleBarHeightDip);
            quicksift::ui::framework::TitleBarPresentation title{};
            wchar_t titleText[256]{};
            GetWindowTextW(window, titleText, static_cast<int>(std::size(titleText)));
            title.title = titleText;
            title.titleTextRect = { px(16), 0, client.right - px(80), titleHeight };
            title.titleFont = state->semiboldFont ? state->semiboldFont : state->normalFont;
            title.active = GetActiveWindow() == window;
            const int diameter = px(12);
            const int rightMargin = px(15);
            const int closeTop = (titleHeight - diameter) / 2;
            title.trafficLights[0].rect = { client.right - rightMargin - diameter, closeTop,
                client.right - rightMargin, closeTop + diameter };
            title.trafficLights[0].color = RGB(255, 95, 87);
            title.trafficLights[0].hot = state->titleCloseHot;
            title.trafficLights[0].pressed = state->titleClosePressed;
            state->framework->PaintTitleBar(dc, client, titleHeight, title, state->theme, dpi);
            if (state->edit) {
                RECT panel{};
                GetWindowRect(state->edit, &panel);
                MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&panel), 2);
                InflateRect(&panel, 1, 1);
                state->framework->PaintPanel(dc, panel,
                    quicksift::ui::framework::PanelPresentation{ false, false, 14 },
                    state->theme, quicksift::ui::GetWindowDpi(window));
            }
            RECT accent = client;
            accent.left += px(22);
            accent.right -= px(22);
            accent.top = titleHeight + px(7);
            accent.bottom = titleHeight + px(10);
            state->framework->PaintAccentStrip(dc, accent, state->theme);
            return 1;
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        if (state) {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, state->theme.text);
            SetBkColor(dc, state->theme.panel);
            return reinterpret_cast<LRESULT>(state->panelBrush);
        }
        break;
    case WM_DRAWITEM:
        if (state && state->framework) {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (draw && (draw->hwndItem == state->close || draw->hwndItem == state->refresh ||
                draw->hwndItem == state->exportButton)) {
                wchar_t caption[128]{};
                GetWindowTextW(draw->hwndItem, caption, static_cast<int>(std::size(caption)));
                quicksift::ui::framework::ButtonPresentation presentation{};
                presentation.text = caption;
                presentation.role = quicksift::ui::framework::ButtonRole::Standard;
                presentation.state.hot = state->framework->IsButtonHot(draw->hwndItem);
                presentation.state.pressed = (draw->itemState & ODS_SELECTED) != 0;
                presentation.state.disabled = (draw->itemState & ODS_DISABLED) != 0;
                presentation.state.focused = (draw->itemState & ODS_FOCUS) != 0;
                presentation.hoverBlend = state->framework->ButtonHoverBlend(draw->hwndItem);
                presentation.pressBlend = presentation.state.pressed ? 1.0f : 0.0f;
                state->framework->PaintButton(draw->hDC, draw->rcItem, presentation,
                    state->theme, quicksift::ui::GetWindowDpi(window),
                    state->normalFont, state->semiboldFont);
                return TRUE;
            }
        }
        break;
    case WM_MOUSEMOVE:
        if (state) {
            const UINT dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, quicksift::ui::GetWindowDpi(window));
            const auto px = [dpi](int dip) { return std::max(1, MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI)); };
            RECT client{};
            GetClientRect(window, &client);
            const int titleHeight = px(quicksift::ui::layout::kTitleBarHeightDip);
            const int diameter = px(12);
            const int rightMargin = px(15);
            RECT closeRect{ client.right - rightMargin - diameter, (titleHeight - diameter) / 2,
                client.right - rightMargin, (titleHeight - diameter) / 2 + diameter };
            InflateRect(&closeRect, px(4), px(5));
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const bool hot = PtInRect(&closeRect, point) != FALSE;
            if (hot != state->titleCloseHot) {
                state->titleCloseHot = hot;
                InvalidateRect(window, nullptr, FALSE);
            }
            if (!GetPropW(window, L"QuickSift.DocumentTrackingMouse")) {
                SetPropW(window, L"QuickSift.DocumentTrackingMouse", reinterpret_cast<HANDLE>(1));
                TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0 };
                TrackMouseEvent(&track);
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (state) {
            state->titleCloseHot = false;
            if (!state->titleClosePressed) InvalidateRect(window, nullptr, FALSE);
        }
        RemovePropW(window, L"QuickSift.DocumentTrackingMouse");
        return 0;
    case WM_LBUTTONDOWN:
        if (state) {
            const UINT dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, quicksift::ui::GetWindowDpi(window));
            const auto px = [dpi](int dip) { return std::max(1, MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI)); };
            RECT client{};
            GetClientRect(window, &client);
            const int titleHeight = px(quicksift::ui::layout::kTitleBarHeightDip);
            const int diameter = px(12);
            const int rightMargin = px(15);
            RECT closeRect{ client.right - rightMargin - diameter, (titleHeight - diameter) / 2,
                client.right - rightMargin, (titleHeight - diameter) / 2 + diameter };
            InflateRect(&closeRect, px(4), px(5));
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (PtInRect(&closeRect, point)) {
                state->titleClosePressed = true;
                state->titleCloseHot = true;
                SetCapture(window);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
        }
        break;
    case WM_LBUTTONUP:
        if (state && state->titleClosePressed) {
            state->titleClosePressed = false;
            ReleaseCapture();
            const UINT dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, quicksift::ui::GetWindowDpi(window));
            const auto px = [dpi](int dip) { return std::max(1, MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI)); };
            RECT client{};
            GetClientRect(window, &client);
            const int titleHeight = px(quicksift::ui::layout::kTitleBarHeightDip);
            const int diameter = px(12);
            const int rightMargin = px(15);
            RECT closeRect{ client.right - rightMargin - diameter, (titleHeight - diameter) / 2,
                client.right - rightMargin, (titleHeight - diameter) / 2 + diameter };
            InflateRect(&closeRect, px(4), px(5));
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const bool activate = PtInRect(&closeRect, point) != FALSE;
            state->titleCloseHot = activate;
            InvalidateRect(window, nullptr, FALSE);
            if (activate) DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CANCELMODE:
        if (state && state->titleClosePressed) {
            state->titleClosePressed = false;
            state->titleCloseHot = false;
            if (GetCapture() == window) ReleaseCapture();
            InvalidateRect(window, nullptr, FALSE);
        }
        break;
    case WM_SIZE:
        if (state) {
            const int width = LOWORD(lParam);
            const int height = HIWORD(lParam);
            const UINT dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, quicksift::ui::GetWindowDpi(window));
            const auto px = [dpi](int dip) { return std::max(1, MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI)); };
            // Keep the action row close to the window edges. Use a slightly smaller
            // bottom inset than the horizontal inset so the Close button sits visually
            // balanced against the right edge without leaving a large strip below it.
            const int horizontalMargin = px(8);
            const int bottomMargin = px(6);
            const int topMargin = px(quicksift::ui::layout::kTitleBarHeightDip + 18);
            const int buttonGap = px(12);
            const int buttonWidth = px(112);
            const int buttonHeight = px(38);
            const int margin = horizontalMargin;
            if (state->edit) MoveWindow(state->edit, margin, topMargin,
                std::max(1, width - margin * 2),
                std::max(1, height - topMargin - bottomMargin - buttonGap - buttonHeight), TRUE);
            const int buttonY = std::max(topMargin, height - bottomMargin - buttonHeight);
            auto positionButton = [&](HWND button, int x) {
                if (!button) return;
                MoveWindow(button, x, buttonY, buttonWidth, buttonHeight, TRUE);
                if (state->framework) {
                    state->framework->ApplyRoundedControlRegion(button, buttonWidth,
                        buttonHeight, px(9));
                }
            };
            const int closeX = std::max(margin, width - margin - buttonWidth);
            positionButton(state->close, closeX);
            if (state->exportButton) positionButton(state->exportButton,
                std::max(margin, closeX - buttonGap - buttonWidth));
            if (state->refresh) positionButton(state->refresh,
                std::max(margin, closeX - (buttonGap + buttonWidth) * 2));
            if (state->verboseLogging) {
                MoveWindow(state->verboseLogging, margin, buttonY, buttonWidth + px(20), buttonHeight, TRUE);
            }
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(window);
            return 0;
        }
        if (state && state->diagnosticLog && LOWORD(wParam) == ID_DOCUMENT_EXPORT) {
            std::filesystem::path exported;
            if (quicksift::diagnostics::ExportSnapshotInteractive(window, &exported)) {
                QS_LOG_INFO(L"Diagnostics", L"Diagnostic snapshot exported to " + exported.wstring());
                MessageBoxW(window, (L"Diagnostic log exported to:\r\n\r\n" + exported.wstring()).c_str(),
                    L"QuickSift", MB_OK | MB_ICONINFORMATION);
            } else if (!exported.empty() || CommDlgExtendedError() != 0) {
                QS_LOG_WARNING(L"Diagnostics", L"Diagnostic snapshot export failed");
                MessageBoxW(window, L"QuickSift could not export the diagnostic log to that location.",
                    L"QuickSift", MB_OK | MB_ICONWARNING);
            }
            return 0;
        }
        if (state && state->diagnosticLog && LOWORD(wParam) == ID_DOCUMENT_VERBOSE) {
            const bool enabled = SendMessageW(state->verboseLogging, BM_GETCHECK, 0, 0) == BST_CHECKED;
            quicksift::diagnostics::SetVerboseLogging(enabled);
            return 0;
        }
        if (state && state->diagnosticLog && LOWORD(wParam) == ID_DOCUMENT_REFRESH) {
            SetDocumentControlText(state->edit, quicksift::diagnostics::SnapshotText(), false,
                state->richEdit, state->theme);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        RemovePropW(window, L"QuickSift.DocumentTrackingMouse");
        RemovePropW(window, kDocumentKindProperty);
        RemovePropW(window, kDocumentStateProperty);
        RemoveWindowSubclass(window, DocumentWindowSubclassProc, kDocumentWindowSubclassId);
        if (state && state->panelBrush) DeleteObject(state->panelBrush);
        delete state;
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}


LRESULT CALLBACK DocumentWindowSubclassProc(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR subclassId, DWORD_PTR reference) {
    try {
        return DocumentWindowSubclassProcImpl(window, message, wParam, lParam,
            subclassId, reference);
    } catch (...) {
        quicksift::diagnostics::Write(quicksift::diagnostics::Level::Critical,
            L"DocumentWindowSubclassProc",
            L"Unhandled C++ exception escaped a Windows UI callback");
        if (message == WM_NCDESTROY) {
            auto* state = reinterpret_cast<DocumentWindowState*>(reference);
            RemovePropW(window, kDocumentKindProperty);
            RemovePropW(window, kDocumentStateProperty);
            RemoveWindowSubclass(window, DocumentWindowSubclassProc,
                kDocumentWindowSubclassId);
            if (state && state->panelBrush) DeleteObject(state->panelBrush);
            delete state;
        } else if (message == WM_CLOSE && IsWindow(window)) {
            DestroyWindow(window);
        } else if (IsWindow(window)) {
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
        return 0;
    }
}


} // namespace quicksift::app
