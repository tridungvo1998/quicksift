// OWNER: Central native control construction and shared component interaction behavior.
#include "ui/framework/macos_ui_framework.h"
#include "ui/ui_layout_metrics.h"

#include <dwmapi.h>
#include <uxtheme.h>
#include <wrl/client.h>
#include <dwrite.h>
#include <d2d1helper.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <new>
#include <utility>

namespace quicksift::ui::framework {
namespace {
// DWM window attributes by value: older SDK / MinGW headers lack the names.
inline constexpr DWORD kDwmUseImmersiveDarkMode = 20;  // DWMWA_USE_IMMERSIVE_DARK_MODE
inline constexpr DWORD kDwmWindowCornerPreference = 33; // DWMWA_WINDOW_CORNER_PREFERENCE
inline constexpr DWORD kDwmBorderColor = 34;           // DWMWA_BORDER_COLOR
inline constexpr DWORD kDwmCaptionColor = 35;          // DWMWA_CAPTION_COLOR
inline constexpr DWORD kDwmTextColor = 36;             // DWMWA_TEXT_COLOR
inline constexpr DWORD kDwmSystemBackdropType = 38;    // DWMWA_SYSTEMBACKDROP_TYPE
inline constexpr DWORD kDwmCornerDoNotRound = 1;       // DWMWCP_DONOTROUND
inline constexpr DWORD kDwmCornerRound = 2;            // DWMWCP_ROUND
inline constexpr DWORD kDwmBackdropNone = 1;           // DWMSBT_NONE
inline constexpr DWORD kDwmBackdropMica = 2;           // DWMSBT_MAINWINDOW
inline constexpr COLORREF kDwmColorDefault = 0xFFFFFFFFu;
inline constexpr COLORREF kDwmColorNone = 0xFFFFFFFEu;

inline constexpr wchar_t kButtonHotProperty[] = L"QuickSift.Framework.ButtonHot";
inline constexpr UINT_PTR kButtonSubclassId = 0x51535549u;
inline constexpr UINT_PTR kOwnerDrawStaticSubclassId = 0x51535354u;

inline constexpr wchar_t kD2DTextInputClass[] = L"QuickSift.Framework.D2DTextInput";
inline constexpr wchar_t kD2DTextInputStateProperty[] = L"QuickSift.D2DTextInputState";
inline constexpr UINT_PTR kD2DTextInputTimer = 0x5154494Eu;
inline constexpr UINT_PTR kD2DTextInputSubclassId = 0x5154494Eu;

// Shared framework text input (single- or multi-line). One implementation serves
// the user-comment field, form-dialog text areas and labelled single-line fields.
// Internal line break is '\r' (what Enter inserts); CRLF/LF are normalized on
// WM_SETTEXT and paste, and copied text uses CRLF.
struct D2DTextInputState {
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout; // cached for (revision, width)
    std::uint64_t layoutRevision = ~0ull;
    float layoutWidth = -1.0f;
    std::uint64_t revision = 0;
    std::wstring text;
    std::wstring placeholder;
    std::size_t caret = 0;
    std::size_t selectionAnchor = 0;
    std::size_t maxLength = 1u << 20;
    float scrollY = 0.0f;
    float scrollX = 0.0f;
    bool multiline = true;
    bool notifyParent = false;
    bool borderless = false;
    bool error = false;
    bool focused = false;
    bool caretVisible = true;
    bool selecting = false;
    bool thumbDragging = false;
    float thumbGrabOffset = 0.0f;
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    COLORREF panel = RGB(35, 38, 45);
    COLORREF border = RGB(78, 84, 96);
    COLORREF accent = RGB(70, 145, 255);
    COLORREF textColor = RGB(235, 238, 244);
    COLORREF muted = RGB(150, 156, 168);
    COLORREF danger = RGB(255, 99, 99);
    COLORREF selection = RGB(70, 145, 255);
    bool hasParentSurface = false;
    COLORREF parentSurface = RGB(35, 38, 45);
};

COLORREF BlendEditorColor(COLORREF base, COLORREF over, BYTE alpha) noexcept {
    const int a = alpha;
    return RGB((GetRValue(base) * (255 - a) + GetRValue(over) * a) / 255,
        (GetGValue(base) * (255 - a) + GetGValue(over) * a) / 255,
        (GetBValue(base) * (255 - a) + GetBValue(over) * a) / 255);
}

D2D1_COLOR_F D2DColor(COLORREF c, float alpha = 1.0f) noexcept {
    return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f,
        GetBValue(c) / 255.0f, alpha);
}

float InputScale(const D2DTextInputState& s) noexcept {
    return static_cast<float>(s.dpi ? s.dpi : USER_DEFAULT_SCREEN_DPI) / USER_DEFAULT_SCREEN_DPI;
}

// Content box in client pixels: left/top/right/bottom padding around the text.
D2D1_RECT_F InputContentRect(HWND hwnd, const D2DTextInputState& s) noexcept {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const float scale = InputScale(s);
    const float left = 10.0f * scale;
    const float right = static_cast<float>(rc.right) - (s.multiline ? 14.0f : 10.0f) * scale;
    const float top = s.multiline ? 8.0f * scale : 0.0f;
    const float bottom = static_cast<float>(rc.bottom) - (s.multiline ? 8.0f * scale : 0.0f);
    return D2D1::RectF(left, top, std::max(left + 1.0f, right), std::max(top + 1.0f, bottom));
}

void EnsureD2DTextInputResources(HWND hwnd, D2DTextInputState& state) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    if (!state.d2dFactory) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, state.d2dFactory.GetAddressOf());
    if (!state.writeFactory) DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(state.writeFactory.GetAddressOf()));
    if (!state.target && state.d2dFactory) {
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
        const D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
            hwnd, D2D1::SizeU(static_cast<UINT32>(rc.right), static_cast<UINT32>(rc.bottom)));
        state.d2dFactory->CreateHwndRenderTarget(props, hwndProps, &state.target);
    }
    if (state.target) {
        D2D1_SIZE_U desired = D2D1::SizeU(static_cast<UINT32>(rc.right), static_cast<UINT32>(rc.bottom));
        if (state.target->GetPixelSize().width != desired.width || state.target->GetPixelSize().height != desired.height)
            state.target->Resize(desired);
    }
    if (!state.format && state.writeFactory) {
        state.writeFactory->CreateTextFormat(UiFontFamily(FontRole::Text), nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            12.0f * static_cast<float>(state.dpi) / USER_DEFAULT_SCREEN_DPI, L"en-us", &state.format);
        if (state.format) {
            state.format->SetWordWrapping(state.multiline ? DWRITE_WORD_WRAPPING_WRAP :
                DWRITE_WORD_WRAPPING_NO_WRAP);
            state.format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        state.layout.Reset();
    }
}

IDWriteTextLayout* InputLayout(HWND hwnd, D2DTextInputState& s) {
    EnsureD2DTextInputResources(hwnd, s);
    if (!s.format || !s.writeFactory) return nullptr;
    const D2D1_RECT_F content = InputContentRect(hwnd, s);
    const float width = s.multiline ? std::max(1.0f, content.right - content.left) : 100000.0f;
    if (s.layout && s.layoutRevision == s.revision && s.layoutWidth == width) return s.layout.Get();
    s.layout.Reset();
    s.writeFactory->CreateTextLayout(s.text.c_str(), static_cast<UINT32>(s.text.size()),
        s.format.Get(), width, 100000.0f, &s.layout);
    s.layoutRevision = s.revision;
    s.layoutWidth = width;
    return s.layout.Get();
}

float InputContentHeight(HWND hwnd, D2DTextInputState& s) {
    IDWriteTextLayout* layout = InputLayout(hwnd, s);
    if (!layout) return 0.0f;
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return metrics.height;
}

// Text origin in client pixels (accounts for scroll and single-line centring).
D2D1_POINT_2F InputTextOrigin(HWND hwnd, D2DTextInputState& s) {
    const D2D1_RECT_F content = InputContentRect(hwnd, s);
    if (s.multiline) return D2D1::Point2F(content.left, content.top - s.scrollY);
    const float lineHeight = InputContentHeight(hwnd, s);
    const float top = content.top + std::max(0.0f, (content.bottom - content.top - lineHeight) * 0.5f);
    return D2D1::Point2F(content.left - s.scrollX, top);
}

std::pair<std::size_t,std::size_t> SelectionRange(const D2DTextInputState& s) noexcept {
    return std::minmax(s.caret, s.selectionAnchor);
}

void ClampTextInputScroll(HWND hwnd, D2DTextInputState& s) {
    const D2D1_RECT_F content = InputContentRect(hwnd, s);
    if (s.multiline) {
        const float viewH = std::max(1.0f, content.bottom - content.top);
        const float maxScroll = std::max(0.0f, InputContentHeight(hwnd, s) - viewH);
        s.scrollY = std::clamp(s.scrollY, 0.0f, maxScroll);
        s.scrollX = 0.0f;
    } else {
        IDWriteTextLayout* layout = InputLayout(hwnd, s);
        DWRITE_TEXT_METRICS metrics{};
        if (layout) layout->GetMetrics(&metrics);
        const float viewW = std::max(1.0f, content.right - content.left);
        s.scrollX = std::clamp(s.scrollX, 0.0f, std::max(0.0f, metrics.widthIncludingTrailingWhitespace - viewW + 2.0f));
        s.scrollY = 0.0f;
    }
}

bool CaretMetrics(HWND hwnd, D2DTextInputState& s, std::size_t position, float& x, float& y, float& height) {
    IDWriteTextLayout* layout = InputLayout(hwnd, s);
    if (!layout) return false;
    DWRITE_HIT_TEST_METRICS metrics{};
    if (FAILED(layout->HitTestTextPosition(static_cast<UINT32>(std::min(position, s.text.size())),
        FALSE, &x, &y, &metrics))) return false;
    height = metrics.height;
    return true;
}

void EnsureCaretVisible(HWND hwnd, D2DTextInputState& s) {
    float x = 0.0f, y = 0.0f, height = 0.0f;
    if (!CaretMetrics(hwnd, s, s.caret, x, y, height)) return;
    const D2D1_RECT_F content = InputContentRect(hwnd, s);
    if (s.multiline) {
        const float viewH = std::max(1.0f, content.bottom - content.top);
        if (y < s.scrollY) s.scrollY = y;
        if (y + height > s.scrollY + viewH) s.scrollY = y + height - viewH;
    } else {
        const float viewW = std::max(1.0f, content.right - content.left);
        if (x < s.scrollX) s.scrollX = x;
        if (x + 2.0f > s.scrollX + viewW) s.scrollX = x + 2.0f - viewW;
    }
    ClampTextInputScroll(hwnd, s);
}

std::size_t HitTextPosition(HWND hwnd, D2DTextInputState& s, float clientX, float clientY) {
    IDWriteTextLayout* layout = InputLayout(hwnd, s);
    if (!layout) return s.text.size();
    const D2D1_POINT_2F origin = InputTextOrigin(hwnd, s);
    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    if (FAILED(layout->HitTestPoint(clientX - origin.x, clientY - origin.y, &trailing, &inside, &metrics)))
        return s.text.size();
    std::size_t position = metrics.textPosition + (trailing ? metrics.length : 0u);
    return std::min(position, s.text.size());
}

// Multi-line overflow scrollbar geometry (client pixels). Returns false when the
// content fits (no scrollbar).
bool InputScrollbar(HWND hwnd, D2DTextInputState& s, D2D1_RECT_F& track, D2D1_RECT_F& thumb) {
    if (!s.multiline) return false;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const D2D1_RECT_F content = InputContentRect(hwnd, s);
    const float viewH = std::max(1.0f, content.bottom - content.top);
    const float contentH = InputContentHeight(hwnd, s);
    if (contentH <= viewH + 0.5f) return false;
    const float scale = InputScale(s);
    track = D2D1::RectF(static_cast<float>(rc.right) - 9.0f * scale, 6.0f * scale,
        static_cast<float>(rc.right) - 4.0f * scale, static_cast<float>(rc.bottom) - 6.0f * scale);
    const float trackH = std::max(1.0f, track.bottom - track.top);
    const float thumbH = std::clamp(trackH * viewH / contentH, std::min(trackH, 24.0f * scale), trackH);
    const float maxScroll = std::max(1.0f, contentH - viewH);
    const float top = track.top + (trackH - thumbH) * std::clamp(s.scrollY / maxScroll, 0.0f, 1.0f);
    thumb = D2D1::RectF(track.left, top, track.right, top + thumbH);
    return true;
}

void NotifyTextChanged(HWND hwnd, D2DTextInputState& s) {
    ++s.revision;
    if (!s.notifyParent) return;
    const HWND parent = GetParent(hwnd);
    if (parent) SendMessageW(parent, WM_COMMAND,
        MAKEWPARAM(static_cast<UINT>(GetDlgCtrlID(hwnd)), EN_CHANGE), reinterpret_cast<LPARAM>(hwnd));
}

std::wstring NormalizeInputText(std::wstring_view value, bool multiline) {
    std::wstring normalized;
    normalized.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        wchar_t ch = value[index];
        if (ch == L'\r' && index + 1 < value.size() && value[index + 1] == L'\n') ++index;
        if (ch == L'\n') ch = L'\r';
        if (ch == L'\r') {
            normalized.push_back(multiline ? L'\r' : L' ');
            continue;
        }
        if (ch == L'\t') {
            normalized.push_back(multiline ? L'\t' : L' ');
            continue;
        }
        if (ch < 32 || ch == 127) continue;
        normalized.push_back(ch);
    }
    return normalized;
}

void ReplaceSelection(HWND hwnd, D2DTextInputState& s, std::wstring_view insert) {
    const auto [a, b] = SelectionRange(s);
    if (a == b && insert.empty()) return;
    const std::size_t room = s.maxLength > s.text.size() - (b - a) ? s.maxLength - (s.text.size() - (b - a)) : 0u;
    const std::wstring_view kept = insert.substr(0, std::min(insert.size(), room));
    s.text.replace(a, b - a, kept);
    s.caret = a + kept.size();
    s.selectionAnchor = s.caret;
    NotifyTextChanged(hwnd, s);
}

void CopySelection(HWND hwnd, const D2DTextInputState& s) {
    const auto [a, b] = SelectionRange(s);
    if (a == b) return;
    std::wstring copy;
    copy.reserve((b - a) + 16);
    for (std::size_t index = a; index < b; ++index) {
        if (s.text[index] == L'\r') copy += L"\r\n";
        else copy.push_back(s.text[index]);
    }
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    const SIZE_T bytes = (copy.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* locked = GlobalLock(memory)) {
            memcpy(locked, copy.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

void PasteClipboard(HWND hwnd, D2DTextInputState& s) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(hwnd)) return;
    std::wstring pasted;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* locked = static_cast<const wchar_t*>(GlobalLock(data))) {
            const SIZE_T capacity = GlobalSize(data) / sizeof(wchar_t);
            std::size_t length = 0;
            while (length < capacity && locked[length] != L'\0') ++length;
            pasted.assign(locked, length);
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    ReplaceSelection(hwnd, s, NormalizeInputText(pasted, s.multiline));
}

bool IsWordChar(wchar_t ch) noexcept {
    return !(ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L',' || ch == L';' || ch == L'"');
}

void PaintD2DTextInput(HWND hwnd, D2DTextInputState& s) {
    EnsureD2DTextInputResources(hwnd,s);
    if (!s.target || !s.format || !s.writeFactory) return;
    RECT rc{}; GetClientRect(hwnd,&rc);
    ClampTextInputScroll(hwnd,s);
    ID2D1HwndRenderTarget* rt = s.target.Get();
    // Keep the HWND target size in sync; a stale smaller target clips the right/bottom border.
    const UINT clientW = static_cast<UINT>(std::max(0L, rc.right - rc.left));
    const UINT clientH = static_cast<UINT>(std::max(0L, rc.bottom - rc.top));
    if (rt->GetPixelSize().width != clientW || rt->GetPixelSize().height != clientH) {
        rt->Resize(D2D1::SizeU(std::max(1u, clientW), std::max(1u, clientH)));
    }
    const bool enabled = IsWindowEnabled(hwnd) != FALSE;
    rt->BeginDraw();
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    rt->Clear(D2DColor(s.hasParentSurface ? s.parentSurface : s.panel));
    const COLORREF borderColor = s.error ? s.danger : (s.focused ? s.accent : s.border);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush, textBrush, mutedBrush, selBrush, caretBrush, fieldBrush, barBrush;
    rt->CreateSolidColorBrush(D2DColor(borderColor), &borderBrush);
    rt->CreateSolidColorBrush(D2DColor(enabled ? s.textColor : s.muted), &textBrush);
    rt->CreateSolidColorBrush(D2DColor(s.muted), &mutedBrush);
    rt->CreateSolidColorBrush(D2DColor(s.selection,0.32f), &selBrush);
    rt->CreateSolidColorBrush(D2DColor(s.accent), &caretBrush);
    rt->CreateSolidColorBrush(D2DColor(s.panel), &fieldBrush);
    rt->CreateSolidColorBrush(D2DColor(s.muted, s.thumbDragging ? 0.75f : 0.45f), &barBrush);
    // The full rounded border stays inside the client area at every DPI: the stroke
    // is centred on a rect inset by half its width plus the shared inset, so the
    // right/bottom edges are never clipped by the HWND render target.
    using quicksift::ui::layout::kTextInputBorderInsetDip;
    using quicksift::ui::layout::kTextInputRadiusDip;
    const float scale = InputScale(s);
    const float strokeWidth = (s.focused || s.error) ? std::max(1.5f, 1.5f * scale) : std::max(1.0f, scale);
    const float inset = std::max(1.0f, kTextInputBorderInsetDip * scale) + strokeWidth * 0.5f - 0.5f;
    const float radius = kTextInputRadiusDip * scale;
    const D2D1_ROUNDED_RECT box = D2D1::RoundedRect(
        D2D1::RectF(inset, inset,
            static_cast<float>(rc.right) - inset,
            static_cast<float>(rc.bottom) - inset),
        radius, radius);
    if (!s.borderless) {
        if (fieldBrush) rt->FillRoundedRectangle(box, fieldBrush.Get());
        if (borderBrush) rt->DrawRoundedRectangle(box, borderBrush.Get(), strokeWidth);
    }
    const D2D1_RECT_F content = InputContentRect(hwnd, s);
    rt->PushAxisAlignedClip(D2D1::RectF(content.left - 2.0f * scale, std::max(inset + 1.0f, content.top - 4.0f * scale),
        content.right + 2.0f * scale, std::min(static_cast<float>(rc.bottom) - inset - 1.0f, content.bottom + 4.0f * scale)),
        D2D1_ANTIALIAS_MODE_ALIASED);
    IDWriteTextLayout* layout = InputLayout(hwnd, s);
    const D2D1_POINT_2F origin = InputTextOrigin(hwnd, s);
    if (layout) {
        const auto [a,b]=SelectionRange(s);
        if (a!=b && selBrush) {
            UINT32 needed = 0;
            layout->HitTestTextRange(static_cast<UINT32>(a), static_cast<UINT32>(b-a), origin.x, origin.y,
                nullptr, 0, &needed);
            if (needed > 0) {
                std::vector<DWRITE_HIT_TEST_METRICS> metrics(needed);
                UINT32 actual = 0;
                if (SUCCEEDED(layout->HitTestTextRange(static_cast<UINT32>(a),static_cast<UINT32>(b-a),
                    origin.x, origin.y, metrics.data(), needed, &actual))) {
                    for (UINT32 i=0;i<actual;++i) {
                        const auto& m=metrics[i];
                        if (m.top + m.height < content.top - 8.0f || m.top > content.bottom + 8.0f) continue;
                        rt->FillRectangle(D2D1::RectF(m.left,m.top,m.left+std::max(m.width, 3.0f * scale),m.top+m.height),selBrush.Get());
                    }
                }
            }
        }
        if (s.text.empty() && !s.placeholder.empty() && mutedBrush) {
            rt->DrawTextW(s.placeholder.c_str(), static_cast<UINT32>(s.placeholder.size()), s.format.Get(),
                D2D1::RectF(origin.x, origin.y, content.right, origin.y + 1000.0f), mutedBrush.Get(),
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        rt->DrawTextLayout(origin,layout,textBrush.Get(),D2D1_DRAW_TEXT_OPTIONS_NONE);
        if (s.focused && s.caretVisible && caretBrush && enabled) {
            float x=0.0f, y=0.0f, height=0.0f;
            if (CaretMetrics(hwnd, s, s.caret, x, y, height)) {
                x += origin.x; y += origin.y;
                rt->DrawLine(D2D1::Point2F(x,y),D2D1::Point2F(x,y+height),caretBrush.Get(),std::max(1.5f, 1.5f * scale));
            }
        }
    }
    rt->PopAxisAlignedClip();
    D2D1_RECT_F track{}, thumb{};
    if (barBrush && InputScrollbar(hwnd, s, track, thumb)) {
        const float r = (thumb.right - thumb.left) * 0.5f;
        rt->FillRoundedRectangle(D2D1::RoundedRect(thumb, r, r), barBrush.Get());
    }
    if (rt->EndDraw() == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
        s.target.Reset();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void MoveCaretVertically(HWND hwnd, D2DTextInputState& s, bool down) {
    float x = 0.0f, y = 0.0f, height = 0.0f;
    if (!CaretMetrics(hwnd, s, s.caret, x, y, height)) return;
    const D2D1_POINT_2F origin = InputTextOrigin(hwnd, s);
    const float targetY = down ? y + height * 1.5f : y - height * 0.5f;
    if (targetY < 0.0f) { s.caret = 0; return; }
    if (targetY > InputContentHeight(hwnd, s)) { s.caret = s.text.size(); return; }
    s.caret = HitTextPosition(hwnd, s, origin.x + x, origin.y + targetY);
}

void MoveCaretToLineEdge(HWND hwnd, D2DTextInputState& s, bool end) {
    if (!s.multiline) { s.caret = end ? s.text.size() : 0; return; }
    float x = 0.0f, y = 0.0f, height = 0.0f;
    if (!CaretMetrics(hwnd, s, s.caret, x, y, height)) return;
    const D2D1_POINT_2F origin = InputTextOrigin(hwnd, s);
    s.caret = HitTextPosition(hwnd, s, end ? origin.x + 100000.0f : origin.x - 1000.0f, origin.y + y + height * 0.5f);
    // Never land after the line's own break (that is the next line's start).
    if (end && s.caret > 0 && s.caret <= s.text.size() && s.text[s.caret - 1] == L'\r') --s.caret;
}

LRESULT CALLBACK D2DTextInputProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR referenceData) {
    (void)subclassId;
    (void)referenceData;
    auto* s=reinterpret_cast<D2DTextInputState*>(GetPropW(hwnd,kD2DTextInputStateProperty));
    if (!s) return DefSubclassProc(hwnd,message,wParam,lParam);
    const auto changed = [&]() {
        s->caretVisible = true;
        EnsureCaretVisible(hwnd, *s);
        InvalidateRect(hwnd, nullptr, FALSE);
    };
    switch(message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps{}; BeginPaint(hwnd,&ps); PaintD2DTextInput(hwnd,*s); EndPaint(hwnd,&ps); return 0; }
    case WM_SIZE: if (s->target) s->target->Resize(D2D1::SizeU(LOWORD(lParam),HIWORD(lParam))); s->layout.Reset(); ClampTextInputScroll(hwnd,*s); InvalidateRect(hwnd,nullptr,FALSE); return 0;
    case WM_GETDLGCODE: return DLGC_WANTCHARS | DLGC_WANTARROWS;
    case WM_SETFOCUS: {
        s->focused=true; s->caretVisible=true;
        // System caret blink rate; INFINITE ("no blink" accessibility setting) means a
        // steady caret and no timer at all.
        const UINT blink = GetCaretBlinkTime();
        if (blink != INFINITE && blink != 0) SetTimer(hwnd,kD2DTextInputTimer,blink,nullptr);
        else KillTimer(hwnd,kD2DTextInputTimer);
        InvalidateRect(hwnd,nullptr,FALSE); return 0;
    }
    case WM_KILLFOCUS: s->focused=false; s->selecting=false; KillTimer(hwnd,kD2DTextInputTimer); InvalidateRect(hwnd,nullptr,FALSE); return 0;
    case WM_ENABLE: InvalidateRect(hwnd,nullptr,FALSE); return 0;
    case WM_TIMER: if(wParam==kD2DTextInputTimer){s->caretVisible=!s->caretVisible;InvalidateRect(hwnd,nullptr,FALSE);} return 0;
    case WM_SETFONT: s->format.Reset(); return DefSubclassProc(hwnd,message,wParam,lParam);
    case WM_SETTEXT: {
        const wchar_t* v=reinterpret_cast<const wchar_t*>(lParam);
        s->text = NormalizeInputText(v ? std::wstring_view(v) : std::wstring_view(), s->multiline);
        if (s->text.size() > s->maxLength) s->text.resize(s->maxLength);
        s->caret=s->selectionAnchor=s->text.size(); s->scrollY=0.0f; s->scrollX=0.0f;
        NotifyTextChanged(hwnd, *s);
        EnsureCaretVisible(hwnd, *s);
        InvalidateRect(hwnd,nullptr,FALSE); return TRUE; }
    case WM_GETTEXTLENGTH: return static_cast<LRESULT>(s->text.size());
    case WM_GETTEXT: { const int cap=static_cast<int>(wParam); if(!lParam||cap<=0)return 0; const int n=std::min<int>(cap-1,static_cast<int>(s->text.size())); wcsncpy_s(reinterpret_cast<wchar_t*>(lParam),static_cast<rsize_t>(cap),s->text.c_str(),static_cast<rsize_t>(n)); return n; }
    case WM_CHAR: {
        const wchar_t ch=static_cast<wchar_t>(wParam);
        if (ch==8) {
            // Backspace is handled here only (WM_KEYDOWN must not delete a second time).
            auto [a,b]=SelectionRange(*s);
            if (a==b && s->caret>0) s->selectionAnchor = s->caret - 1;
            ReplaceSelection(hwnd, *s, {});
        } else if (ch==13 || ch==10) {
            if (s->multiline) ReplaceSelection(hwnd, *s, L"\r");
            else return 0; // single-line: Enter belongs to the owner (default action)
        } else if (ch==9 && s->multiline) {
            return 0; // Tab moves focus (owner); never inserted from the keyboard
        } else if (ch>=32 && ch!=127) {
            const wchar_t one[2]{ ch, L'\0' };
            ReplaceSelection(hwnd, *s, std::wstring_view(one, 1));
        } else {
            return 0;
        }
        changed(); return 0; }
    case WM_KEYDOWN: {
        const bool ctrl=GetKeyState(VK_CONTROL)<0, shift=GetKeyState(VK_SHIFT)<0;
        bool moved = false;
        switch (wParam) {
        case 'A': if (ctrl) { s->selectionAnchor=0; s->caret=s->text.size(); InvalidateRect(hwnd,nullptr,FALSE); return 0; } return 0;
        case 'C': if (ctrl) CopySelection(hwnd, *s); return 0;
        case 'X': if (ctrl) { CopySelection(hwnd, *s); ReplaceSelection(hwnd, *s, {}); changed(); } return 0;
        case 'V': if (ctrl) { PasteClipboard(hwnd, *s); changed(); } return 0;
        case VK_INSERT:
            if (ctrl) CopySelection(hwnd, *s);
            else if (shift) { PasteClipboard(hwnd, *s); changed(); }
            return 0;
        case VK_DELETE: {
            if (shift) CopySelection(hwnd, *s);
            auto [a,b]=SelectionRange(*s);
            if (a==b && s->caret<s->text.size()) s->selectionAnchor = s->caret + 1;
            ReplaceSelection(hwnd, *s, {});
            changed(); return 0; }
        case VK_LEFT: {
            auto [a,b]=SelectionRange(*s);
            if (!shift && a!=b) s->caret=a; else if (s->caret>0) --s->caret;
            moved = true; break; }
        case VK_RIGHT: {
            auto [a,b]=SelectionRange(*s);
            if (!shift && a!=b) s->caret=b; else if (s->caret<s->text.size()) ++s->caret;
            moved = true; break; }
        case VK_HOME: if (ctrl) s->caret=0; else MoveCaretToLineEdge(hwnd,*s,false); moved = true; break;
        case VK_END: if (ctrl) s->caret=s->text.size(); else MoveCaretToLineEdge(hwnd,*s,true); moved = true; break;
        case VK_UP: case VK_DOWN:
            if (!s->multiline) return 0;
            MoveCaretVertically(hwnd,*s,wParam==VK_DOWN); moved = true; break;
        case VK_PRIOR: case VK_NEXT:
            if (!s->multiline) return 0;
            for (int line = 0; line < 8; ++line) MoveCaretVertically(hwnd,*s,wParam==VK_NEXT);
            moved = true; break;
        default:
            return DefSubclassProc(hwnd,message,wParam,lParam);
        }
        if (moved && !shift) s->selectionAnchor=s->caret;
        changed(); return 0; }
    case WM_MOUSEWHEEL:
        if (s->multiline) {
            s->scrollY -= static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA * 48.0f * InputScale(*s);
            ClampTextInputScroll(hwnd,*s); InvalidateRect(hwnd,nullptr,FALSE);
            return 0;
        }
        break;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        const float x = static_cast<float>(GET_X_LPARAM(lParam));
        const float y = static_cast<float>(GET_Y_LPARAM(lParam));
        D2D1_RECT_F track{}, thumb{};
        if (InputScrollbar(hwnd, *s, track, thumb) && x >= track.left - 4.0f * InputScale(*s)) {
            if (y >= thumb.top && y <= thumb.bottom) {
                s->thumbGrabOffset = y - thumb.top;
            } else {
                s->thumbGrabOffset = (thumb.bottom - thumb.top) * 0.5f;
            }
            s->thumbDragging = true;
            SetCapture(hwnd);
        } else {
            s->caret = HitTextPosition(hwnd, *s, x, y);
            if ((wParam & MK_SHIFT) == 0) s->selectionAnchor = s->caret;
            s->selecting = true;
            SetCapture(hwnd);
        }
        // Continue in the move handler so a click on the track jumps the thumb.
    }
        [[fallthrough]];
    case WM_MOUSEMOVE: {
        if (GetCapture() != hwnd) return 0;
        const float x = static_cast<float>(GET_X_LPARAM(lParam));
        const float y = static_cast<float>(GET_Y_LPARAM(lParam));
        if (s->thumbDragging) {
            D2D1_RECT_F track{}, thumb{};
            if (InputScrollbar(hwnd, *s, track, thumb)) {
                const D2D1_RECT_F content = InputContentRect(hwnd, *s);
                const float viewH = std::max(1.0f, content.bottom - content.top);
                const float maxScroll = std::max(0.0f, InputContentHeight(hwnd, *s) - viewH);
                const float span = std::max(1.0f, (track.bottom - track.top) - (thumb.bottom - thumb.top));
                s->scrollY = std::clamp((y - s->thumbGrabOffset - track.top) / span, 0.0f, 1.0f) * maxScroll;
                ClampTextInputScroll(hwnd, *s);
            }
            InvalidateRect(hwnd,nullptr,FALSE);
        } else if (s->selecting) {
            s->caret = HitTextPosition(hwnd, *s, x, y);
            changed();
        }
        return 0; }
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (s->thumbDragging || s->selecting) {
            s->thumbDragging = false;
            s->selecting = false;
            if (message == WM_LBUTTONUP && GetCapture() == hwnd) ReleaseCapture();
            InvalidateRect(hwnd,nullptr,FALSE);
        }
        return 0;
    case WM_LBUTTONDBLCLK: {
        // Select the entry (word) under the caret.
        s->caret = HitTextPosition(hwnd, *s, static_cast<float>(GET_X_LPARAM(lParam)),
            static_cast<float>(GET_Y_LPARAM(lParam)));
        std::size_t start = s->caret;
        std::size_t end = s->caret;
        while (start > 0 && IsWordChar(s->text[start - 1])) --start;
        while (end < s->text.size() && IsWordChar(s->text[end])) ++end;
        s->selectionAnchor = start;
        s->caret = end;
        changed();
        return 0; }
    case WM_NCDESTROY: KillTimer(hwnd,kD2DTextInputTimer); RemovePropW(hwnd,kD2DTextInputStateProperty); delete s; RemoveWindowSubclass(hwnd,D2DTextInputProc,kD2DTextInputSubclassId); break;
    default: break;
    }
    return DefSubclassProc(hwnd,message,wParam,lParam);
}

bool EnsureD2DTextInputClass(HINSTANCE instance) {
    static ATOM atom=0;
    if(atom) return true;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style=CS_DBLCLKS;
    wc.hInstance=instance; wc.lpfnWndProc=DefWindowProcW; wc.lpszClassName=kD2DTextInputClass;
    wc.hCursor=LoadCursorW(nullptr,IDC_IBEAM); wc.hbrBackground=nullptr;
    atom=RegisterClassExW(&wc);
    return atom!=0 || GetLastError()==ERROR_CLASS_ALREADY_EXISTS;
}
}

bool MacOsUiFramework::InitializeNativeControls() noexcept {
    // BUTTON and STATIC are intrinsic User32 classes and do not need
    // ICC_STANDARD_CLASSES. QuickSift only needs the tree-view family here;
    // that flag also registers the tooltip class used by the framework.
    INITCOMMONCONTROLSEX controls{ sizeof(INITCOMMONCONTROLSEX), ICC_TREEVIEW_CLASSES };
    return InitCommonControlsEx(&controls) != FALSE;
}

bool MacOsUiFramework::ApplyWindowChrome(HWND window, WindowChromeKind kind,
    const ThemePalette& palette) noexcept {
    if (!window) return false;
    const tokens::VisualEffects& effects = CurrentVisualEffects();
    const BOOL dark = palette.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(window, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));
    const DWORD corner = kind == WindowChromeKind::Fullscreen ? kDwmCornerDoNotRound : kDwmCornerRound;
    const bool rounded = SUCCEEDED(DwmSetWindowAttribute(window, kDwmWindowCornerPreference,
        &corner, sizeof(corner)));
    // High contrast: leave every frame colour to the system theme.
    COLORREF border = kDwmColorDefault;
    COLORREF caption = kDwmColorDefault;
    COLORREF text = kDwmColorDefault;
    if (!effects.highContrast) {
        switch (kind) {
        case WindowChromeKind::Main:
        case WindowChromeKind::Fullscreen:
            // The custom chrome draws its own edge; no coloured system frame.
            border = kDwmColorNone;
            break;
        case WindowChromeKind::Document:
        case WindowChromeKind::Dialog:
            border = palette.border;
            caption = palette.windowTop;
            text = palette.text;
            break;
        case WindowChromeKind::Popup:
            border = BlendColor(palette.border, palette.menu, 0.12f);
            break;
        }
    }
    DwmSetWindowAttribute(window, kDwmBorderColor, &border, sizeof(border));
    if (kind == WindowChromeKind::Document || kind == WindowChromeKind::Dialog) {
        DwmSetWindowAttribute(window, kDwmCaptionColor, &caption, sizeof(caption));
        DwmSetWindowAttribute(window, kDwmTextColor, &text, sizeof(text));
    }
    // Mica only where the client does not fight it: the main window paints an opaque
    // D2D canvas + GDI chrome every frame (Mica would be hidden yet still composed),
    // so it stays DWMSBT_NONE. Document/form windows get Mica while glass is
    // enabled; it shows through the frame and during live resize instead of a
    // white/black flash. Popups never get a backdrop (their owner draws them).
    if (kind != WindowChromeKind::Popup) {
        const DWORD backdrop = (kind == WindowChromeKind::Document || kind == WindowChromeKind::Dialog) &&
            effects.Glass() ? kDwmBackdropMica : kDwmBackdropNone;
        DwmSetWindowAttribute(window, kDwmSystemBackdropType, &backdrop, sizeof(backdrop));
    }
    return rounded;
}

int MacOsUiFramework::ScaleDip(int dip, UINT dpi) noexcept {
    const UINT safeDpi = dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
    return MulDiv(dip, static_cast<int>(safeDpi), USER_DEFAULT_SCREEN_DPI);
}

HWND MacOsUiFramework::CreateControl(const NativeControlSpec& spec) const noexcept {
    if (!spec.parent || !spec.instance || !spec.className) return nullptr;
    return CreateWindowExW(spec.extendedStyle, spec.className, spec.text.c_str(), spec.style,
        0, 0, 10, 10, spec.parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(spec.id)), spec.instance,
        spec.creationParameter);
}

HWND MacOsUiFramework::CreateButton(const ButtonControlSpec& spec) const noexcept {
    NativeControlSpec control{};
    control.extendedStyle = spec.extendedStyle;
    control.className = L"BUTTON";
    control.text = spec.text;
    control.style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
        BS_OWNERDRAW | spec.additionalStyle;
    control.parent = spec.parent;
    control.id = spec.id;
    control.instance = spec.instance;
    HWND window = CreateControl(control);
    if (!window) return nullptr;
    SetWindowTheme(window, L"", L"");
    if (!SetWindowSubclass(window, ButtonSubclassProc, kButtonSubclassId,
        reinterpret_cast<DWORD_PTR>(this))) {
        DestroyWindow(window);
        return nullptr;
    }
    if (spec.subclassProcedure && !SetWindowSubclass(window, spec.subclassProcedure,
        spec.subclassId, spec.subclassReference)) {
        DestroyWindow(window);
        return nullptr;
    }
    return window;
}

HWND MacOsUiFramework::CreateOwnerDrawStatic(HWND parent, HINSTANCE instance, int id,
    std::wstring text, DWORD extendedStyle, DWORD additionalStyle) const noexcept {
    NativeControlSpec spec{};
    spec.extendedStyle = extendedStyle;
    spec.className = L"STATIC";
    spec.text = std::move(text);
    spec.style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_OWNERDRAW | additionalStyle;
    spec.parent = parent;
    spec.id = id;
    spec.instance = instance;
    HWND window = CreateControl(spec);
    if (!window) return nullptr;
    if (!SetWindowSubclass(window, OwnerDrawStaticSubclassProc,
        kOwnerDrawStaticSubclassId, 0)) {
        DestroyWindow(window);
        return nullptr;
    }
    return window;
}

HWND MacOsUiFramework::CreateD2DTextInput(HWND parent, HINSTANCE instance, int id, std::wstring text) const noexcept {
    return CreateTextInput(parent, instance, id, TextInputOptions{}, std::move(text));
}

HWND MacOsUiFramework::CreateTextInput(HWND parent, HINSTANCE instance, int id,
    const TextInputOptions& options, std::wstring text) const noexcept {
    if (!EnsureD2DTextInputClass(instance)) return nullptr;
    HWND window = CreateWindowExW(0, kD2DTextInputClass, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
        0,0,10,10,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
    if (!window) return nullptr;
    auto* state = new (std::nothrow) D2DTextInputState();
    if (!state) { DestroyWindow(window); return nullptr; }
    try {
        state->multiline = options.multiline;
        state->borderless = options.borderless;
        state->notifyParent = false; // no notification for the initial text
        state->maxLength = std::max<std::size_t>(1, options.maxLength);
        state->placeholder = options.placeholder;
        state->text = NormalizeInputText(text, state->multiline);
        if (state->text.size() > state->maxLength) state->text.resize(state->maxLength);
        state->caret = state->selectionAnchor = state->text.size();
    } catch (...) {
        delete state;
        DestroyWindow(window);
        return nullptr;
    }
    SetPropW(window, kD2DTextInputStateProperty, reinterpret_cast<HANDLE>(state));
    if (!SetWindowSubclass(window,D2DTextInputProc,kD2DTextInputSubclassId,0)) {
        RemovePropW(window, kD2DTextInputStateProperty);
        delete state;
        DestroyWindow(window);
        return nullptr;
    }
    state->notifyParent = options.notifyParent;
    return window;
}

namespace {
D2DTextInputState* TextInputState(HWND window) noexcept {
    return window ? reinterpret_cast<D2DTextInputState*>(GetPropW(window, kD2DTextInputStateProperty)) : nullptr;
}
} // namespace

bool MacOsUiFramework::IsTextInput(HWND window) noexcept {
    return TextInputState(window) != nullptr;
}

void MacOsUiFramework::SetD2DTextInputPalette(HWND window, const ThemePalette& palette, UINT dpi) const noexcept {
    auto* state = TextInputState(window);
    if (!state) return;
    state->dpi = dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
    state->panel = palette.panelStrong;
    state->border = palette.border;
    state->accent = palette.accent;
    state->textColor = palette.text;
    state->muted = palette.muted;
    state->danger = palette.dangerBorder;
    state->selection = palette.accent;
    state->format.Reset();
    state->layout.Reset();
    InvalidateRect(window,nullptr,FALSE);
}

void MacOsUiFramework::SetTextInputParentSurface(HWND window, COLORREF surface) const noexcept {
    auto* state = TextInputState(window);
    if (!state) return;
    state->hasParentSurface = true;
    state->parentSurface = surface;
    InvalidateRect(window, nullptr, FALSE);
}

void MacOsUiFramework::SelectAllTextInput(HWND window) const noexcept {
    auto* state = TextInputState(window);
    if (!state) return;
    state->selectionAnchor = 0;
    state->caret = state->text.size();
    InvalidateRect(window, nullptr, FALSE);
}

void MacOsUiFramework::SetTextInputError(HWND window, bool error) const noexcept {
    auto* state = TextInputState(window);
    if (!state || state->error == error) return;
    state->error = error;
    InvalidateRect(window, nullptr, FALSE);
}

void MacOsUiFramework::SetTextInputPlaceholder(HWND window, std::wstring_view placeholder) const noexcept {
    auto* state = TextInputState(window);
    if (!state) return;
    try {
        state->placeholder.assign(placeholder);
    } catch (...) {
        return;
    }
    InvalidateRect(window, nullptr, FALSE);
}

HWND MacOsUiFramework::CreateTooltip(const TooltipControlSpec& spec) const noexcept {
    if (!spec.owner || !spec.target || !spec.instance || !spec.text || spec.toolId == 0) {
        return nullptr;
    }
    HWND tooltip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        spec.owner, nullptr, spec.instance, nullptr);
    if (!tooltip) return nullptr;
    SetWindowPos(tooltip, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    const UINT dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetWindowDpi(spec.target));
    SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, ScaleDip(spec.maximumWidthDip, dpi));
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, spec.initialDelayMs);
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_RESHOW, spec.reshowDelayMs);
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, spec.autoPopDelayMs);

    TOOLINFOW tool{ sizeof(TOOLINFOW) };
    tool.uFlags = TTF_SUBCLASS;
    tool.hwnd = spec.target;
    tool.uId = spec.toolId;
    tool.rect = RECT{};
    tool.lpszText = spec.text->data();
    if (!SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool))) {
        DestroyWindow(tooltip);
        return nullptr;
    }
    return tooltip;
}

bool MacOsUiFramework::IsButtonHot(HWND button) const noexcept {
    return button && GetPropW(button, kButtonHotProperty) != nullptr;
}

float MacOsUiFramework::ButtonHoverBlend(HWND button) const noexcept {
    if (!button) return 0.0f;
    // Linear channel; PaintButton applies the symmetric Smoothstep, so a hover that
    // reverses mid-fade continues from the same colour (no jump).
    return animator_.Value(animation::MakeId(animation::Space::Button,
        reinterpret_cast<std::uintptr_t>(button)), IsButtonHot(button) ? 1.0f : 0.0f);
}

void MacOsUiFramework::SetButtonHover(HWND button, bool hot, bool animate) const {
    if (!button) return;
    const auto window = reinterpret_cast<std::uintptr_t>(button);
    const animation::ElementId id = animation::MakeId(animation::Space::Button, window);
    if (!animate) {
        animator_.Snap(id, hot ? 1.0f : 0.0f, animation::surface::kWindow, window);
        return;
    }
    animator_.SetEnabled(CurrentVisualEffects().Animate());
    animation::Animator::Options options{};
    options.durationMs = kButtonHoverTransition.inMs;
    options.curve = kButtonHoverTransition.inCurve;
    options.surface = animation::surface::kWindow;
    options.window = window;
    animator_.AnimateTo(id, hot ? 1.0f : 0.0f, options, animation::Animator::Clock::now(), {}, 0.0f);
}

void MacOsUiFramework::ApplyRoundedControlRegion(HWND window, int width, int height,
    int radius, bool redraw) const noexcept {
    ApplyRoundedRegion(window, width, height, radius, redraw);
}

void MacOsUiFramework::ApplyAntialiasedControlRegion(HWND window, int width, int height,
    int radius, bool redraw) const noexcept {
    // A 1-bit region boundary moves outward by ~0.41 px per pixel of radius
    // reduction at 45 degrees; 3 device pixels keep the AA fringe (about one pixel
    // wide at any DPI) fully inside. The painter fills the extra corner pixels
    // with the parent surface colour, so they are indistinguishable from it.
    const int slack = std::min(std::max(0, radius), 3);
    ApplyRoundedRegion(window, width, height, std::max(0, radius - slack), redraw);
}

LRESULT CALLBACK MacOsUiFramework::OwnerDrawStaticSubclassProc(HWND window, UINT message,
    WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR referenceData) {
    (void)wParam;
    (void)lParam;
    (void)subclassId;
    (void)referenceData;
    switch (message) {
    case WM_ERASEBKGND:
        // Every framework static paints its complete opaque surface through
        // WM_DRAWITEM. Suppressing the default erase removes the white/background
        // flash between decode-driven invalidation and the owner draw callback.
        return 1;
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, OwnerDrawStaticSubclassProc,
            kOwnerDrawStaticSubclassId);
        break;
    default:
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

namespace {
// Drops hover state (and its fade timer). Used when the pointer leaves AND when
// the button stops being hoverable without a WM_MOUSELEAVE: hidden with its pane
// (SetWindowPos SWP_HIDEWINDOW), disabled under a modal, or capture taken away by
// a popup. Previously the hot fill stuck and reappeared when the pane reopened.
void ClearButtonHot(HWND window, const MacOsUiFramework* framework) noexcept {
    const bool wasHot = GetPropW(window, kButtonHotProperty) != nullptr;
    RemovePropW(window, kButtonHotProperty);
    if (framework) {
        try { framework->SetButtonHover(window, false, /*animate=*/false); } catch (...) {}
    }
    if (wasHot) {
        TRACKMOUSEEVENT cancel{ sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_CANCEL, window, 0 };
        TrackMouseEvent(&cancel);
    }
}
} // namespace

LRESULT CALLBACK MacOsUiFramework::ButtonSubclassProc(HWND window, UINT message,
    WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR referenceData) {
    (void)subclassId;
    // The owning framework (CreateButton passes `this`); hover frames come from its
    // central animator, driven by the shared UI animation timer (no per-button timer).
    const auto* framework = reinterpret_cast<const MacOsUiFramework*>(referenceData);
    const auto hover = [&](bool hot, bool animate) {
        if (!framework) return;
        try { framework->SetButtonHover(window, hot, animate); } catch (...) {}
    };
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
        if (!GetPropW(window, kButtonHotProperty)) {
            SetPropW(window, kButtonHotProperty, reinterpret_cast<HANDLE>(1));
            TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0 };
            TrackMouseEvent(&track);
            hover(true, true);
            InvalidateRect(window, nullptr, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        RemovePropW(window, kButtonHotProperty);
        hover(false, true); // macOS-style: the hover fades out too
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_WINDOWPOSCHANGED: {
        const auto* position = reinterpret_cast<const WINDOWPOS*>(lParam);
        if (position && (position->flags & SWP_HIDEWINDOW)) ClearButtonHot(window, framework);
        break;
    }
    case WM_SHOWWINDOW:
        if (!wParam) ClearButtonHot(window, framework);
        break;
    case WM_CAPTURECHANGED:
        // Press ended without a WM_LBUTTONUP here (popup/modal took capture):
        // repaint so ODS_SELECTED / pressed chrome does not linger.
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        // Owner-draw ODS_SELECTED updates on press/release; force a crisp repaint.
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_ENABLE:
        if (!wParam) ClearButtonHot(window, framework);
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_NCDESTROY:
        RemovePropW(window, kButtonHotProperty);
        if (framework) {
            // Safe on destroy: the entry (and any frame request for this HWND) goes away.
            framework->animator_.Remove(animation::MakeId(animation::Space::Button,
                reinterpret_cast<std::uintptr_t>(window)));
        }
        RemoveWindowSubclass(window, ButtonSubclassProc, kButtonSubclassId);
        break;
    default:
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

// ---- Central animation system: frame driver and window transitions ----------------------

void MacOsUiFramework::SyncOverlayTransitions(animation::Animator::TimePoint now) {
    using namespace animation;
    // Toast: hold/queue stays in ToastPresenter; presence follows it. The exit
    // transition starts exactly kToastTransition.outMs before the toast ends, so it
    // lands as the presenter advances to the next queued toast (which then enters).
    toasts_.Tick(now);
    const bool toastShown = toasts_.Visible() &&
        toasts_.Remaining(now) > std::chrono::milliseconds(kToastTransition.outMs);
    if (toasts_.Visible() && toastGhostText_ != toasts_.CurrentText()) toastGhostText_ = toasts_.CurrentText();
    animator_.SetShown(element::kToast, toastShown, kToastTransition, surface::kCanvas, now);
    // Alert: the request is kept as a ghost so the dismissal can fade/scale out
    // after the presenter cleared it (the ghost is never hit-testable).
    if (alerts_.Visible()) {
        const AlertRequest& live = alerts_.Request();
        if (live.title != alertGhost_.title || live.message != alertGhost_.message ||
            live.primaryText != alertGhost_.primaryText || live.secondaryText != alertGhost_.secondaryText ||
            live.tertiaryText != alertGhost_.tertiaryText || live.mode != alertGhost_.mode ||
            live.kind != alertGhost_.kind || live.destructive != alertGhost_.destructive) {
            alertGhost_ = live;
        }
    }
    animator_.SetShown(element::kAlert, alerts_.Visible(), kAlertTransition, surface::kCanvas, now);
}

animation::SurfaceMask MacOsUiFramework::TickAnimations(animation::Animator::TimePoint now,
    bool suspended) {
    animator_.SetEnabled(CurrentVisualEffects().Animate());
    animator_.SetSuspended(suspended);
    SyncOverlayTransitions(now);
    const animation::Animator::TickResult result = animator_.Tick(now);
    // Per-window targets: layered fades update their alpha (no repaint); buttons get
    // one invalidate per frame each. Destroyed windows (completion ran) are skipped.
    for (const std::uintptr_t value : animator_.DirtyWindows()) {
        HWND window = reinterpret_cast<HWND>(value);
        if (!window || !IsWindow(window)) continue;
        if (std::find(fadingWindows_.begin(), fadingWindows_.end(), window) != fadingWindows_.end()) {
            const float alpha = animator_.Value(animation::MakeId(animation::Space::Window, value), 0.0f);
            SetLayeredWindowAttributes(window, 0,
                static_cast<BYTE>(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)), LWA_ALPHA);
        } else {
            InvalidateRect(window, nullptr, FALSE);
        }
    }
    return result.dirty;
}

void MacOsUiFramework::FadeOutAndDestroyWindow(HWND window, float fromOpacity) {
    if (!window || !IsWindow(window)) return;
    const LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    animator_.SetEnabled(CurrentVisualEffects().Animate());
    if (!(exStyle & WS_EX_LAYERED) || !animator_.Enabled() || animator_.Suspended() ||
        !IsWindowVisible(window)) {
        DestroyWindow(window);
        return;
    }
    // Click-through and non-activating while it fades: input reaches whatever is
    // underneath at once, exactly as if the window were already gone.
    SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    const auto value = reinterpret_cast<std::uintptr_t>(window);
    const animation::ElementId id = animation::MakeId(animation::Space::Window, value);
    fadingWindows_.push_back(window);
    animator_.Snap(id, std::clamp(fromOpacity, 0.0f, 1.0f), animation::surface::kWindow, value);
    animator_.Hide(id, animation::presets::Fade(), animation::surface::kWindow,
        animation::Animator::Clock::now(), [this, window] {
            fadingWindows_.erase(std::remove(fadingWindows_.begin(), fadingWindows_.end(), window),
                fadingWindows_.end());
            if (IsWindow(window)) DestroyWindow(window);
        }, value, /*transient=*/true);
}

void MacOsUiFramework::ShutdownAnimations() noexcept {
    std::vector<HWND> windows;
    windows.swap(fadingWindows_);
    for (HWND window : windows) {
        if (window && IsWindow(window)) DestroyWindow(window);
    }
    try {
        animator_.FinishAll(/*runCompletions=*/false);
        animator_.RemoveSpace(animation::Space::Window);
    } catch (...) {
    }
}

} // namespace quicksift::ui::framework
