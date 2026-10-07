// OWNER: Central native control construction and shared component interaction behavior.
#include "ui/framework/macos_ui_framework.h"
#include "ui/ui_layout_metrics.h"

#include <uxtheme.h>
#include <wrl/client.h>
#include <dwrite.h>
#include <d2d1helper.h>

#include <algorithm>
#include <string>
#include <new>
#include <utility>

namespace quicksift::ui::framework {
namespace {
inline constexpr wchar_t kButtonHotProperty[] = L"QuickSift.Framework.ButtonHot";
inline constexpr wchar_t kButtonHoverTickProperty[] = L"QuickSift.Framework.ButtonHoverTick";
inline constexpr UINT_PTR kButtonSubclassId = 0x51535549u;
inline constexpr UINT_PTR kButtonHoverTimerId = 0x51534856u;
inline constexpr UINT_PTR kOwnerDrawStaticSubclassId = 0x51535354u;

inline constexpr wchar_t kD2DTextInputClass[] = L"QuickSift.Framework.D2DTextInput";
inline constexpr UINT_PTR kD2DTextInputTimer = 0x5154494Eu;

struct D2DTextInputState {
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    std::wstring text;
    std::size_t caret = 0;
    std::size_t selectionAnchor = 0;
    int scrollY = 0;
    bool focused = false;
    bool caretVisible = true;
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    COLORREF panel = RGB(35, 38, 45);
    COLORREF border = RGB(78, 84, 96);
    COLORREF accent = RGB(70, 145, 255);
    COLORREF textColor = RGB(235, 238, 244);
    COLORREF selection = RGB(70, 145, 255);
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
                D2D1_ALPHA_MODE_IGNORE));
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
        state.writeFactory->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            12.0f * static_cast<float>(state.dpi) / USER_DEFAULT_SCREEN_DPI, L"en-us", &state.format);
        if (state.format) {
            state.format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            state.format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
    }
}

std::pair<std::size_t,std::size_t> SelectionRange(const D2DTextInputState& s) noexcept {
    return std::minmax(s.caret, s.selectionAnchor);
}

void ClampTextInputScroll(HWND hwnd, D2DTextInputState& s) {
    EnsureD2DTextInputResources(hwnd, s);
    if (!s.format || !s.writeFactory) return;
    RECT rc{}; GetClientRect(hwnd,&rc);
    const float width = static_cast<float>(std::max<LONG>(1, rc.right - 24));
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    s.writeFactory->CreateTextLayout(s.text.c_str(), static_cast<UINT32>(s.text.size()),
        s.format.Get(), width, 100000.0f, &layout);
    if (!layout) return;
    DWRITE_TEXT_METRICS m{}; layout->GetMetrics(&m);
    const int viewH = std::max<LONG>(1, rc.bottom - 20);
    const int maxScroll = std::max(0, static_cast<int>(m.height) - viewH);
    s.scrollY = std::clamp(s.scrollY, 0, maxScroll);
}

void PaintD2DTextInput(HWND hwnd, D2DTextInputState& s) {
    EnsureD2DTextInputResources(hwnd,s);
    if (!s.target || !s.format || !s.writeFactory) return;
    RECT rc{}; GetClientRect(hwnd,&rc);
    ClampTextInputScroll(hwnd,s);
    ID2D1HwndRenderTarget* rt = s.target.Get();
    // Keep the HWND target size in sync; a stale smaller target clips the right/bottom border.
    const UINT targetW = rt->GetPixelSize().width;
    const UINT targetH = rt->GetPixelSize().height;
    const UINT clientW = static_cast<UINT>(std::max(0L, rc.right - rc.left));
    const UINT clientH = static_cast<UINT>(std::max(0L, rc.bottom - rc.top));
    if (targetW != clientW || targetH != clientH) {
        rt->Resize(D2D1::SizeU(std::max(1u, clientW), std::max(1u, clientH)));
    }
    rt->BeginDraw();
    rt->Clear(D2DColor(s.panel));
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush, textBrush, selBrush, caretBrush, fieldBrush;
    rt->CreateSolidColorBrush(D2DColor(s.focused ? s.accent : s.border), &borderBrush);
    rt->CreateSolidColorBrush(D2DColor(s.textColor), &textBrush);
    rt->CreateSolidColorBrush(D2DColor(s.selection,0.32f), &selBrush);
    rt->CreateSolidColorBrush(D2DColor(s.accent), &caretBrush);
    rt->CreateSolidColorBrush(D2DColor(s.panel), &fieldBrush);
    // Inset the stroke so right/bottom edges are not clipped by the HWND render target.
    using quicksift::ui::layout::kTextInputBorderInsetDip;
    using quicksift::ui::layout::kTextInputRadiusDip;
    const UINT dpi = s.dpi ? s.dpi : USER_DEFAULT_SCREEN_DPI;
    const auto scaleDip = [dpi](int dip) {
        return MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    };
    const float inset = static_cast<float>(std::max(1, scaleDip(kTextInputBorderInsetDip)));
    const float radius = static_cast<float>(scaleDip(kTextInputRadiusDip));
    const D2D1_ROUNDED_RECT box = D2D1::RoundedRect(
        D2D1::RectF(inset, inset,
            static_cast<float>(rc.right) - inset,
            static_cast<float>(rc.bottom) - inset),
        radius, radius);
    if (fieldBrush) rt->FillRoundedRectangle(box, fieldBrush.Get());
    if (borderBrush) rt->DrawRoundedRectangle(box, borderBrush.Get(), 1.0f);
    const float left=10.0f, top=8.0f-s.scrollY, right=static_cast<float>(rc.right)-10.0f;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    s.writeFactory->CreateTextLayout(s.text.c_str(), static_cast<UINT32>(s.text.size()), s.format.Get(),
        std::max(1.0f,right-left),100000.0f,&layout);
    if (layout) {
        const auto [a,b]=SelectionRange(s);
        if (a!=b && selBrush) {
            UINT32 actual=0; DWRITE_HIT_TEST_METRICS metrics[128]{};
            if (b-a < 128 && SUCCEEDED(layout->HitTestTextRange(static_cast<UINT32>(a),static_cast<UINT32>(b-a),left,top,metrics,128,&actual))) {
                for (UINT32 i=0;i<actual;++i) {
                    const auto& m=metrics[i];
                    rt->FillRectangle(D2D1::RectF(m.left,m.top,m.left+m.width,m.top+m.height),selBrush.Get());
                }
            }
        }
        rt->DrawTextLayout(D2D1::Point2F(left,top),layout.Get(),textBrush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
        if (s.focused && s.caretVisible && caretBrush) {
            float x=left,y=top; DWRITE_HIT_TEST_METRICS cm{};
            if (SUCCEEDED(layout->HitTestTextPosition(static_cast<UINT32>(s.caret),FALSE,&x,&y,&cm))) {
                x += left; y += top; rt->DrawLine(D2D1::Point2F(x,y),D2D1::Point2F(x,y+cm.height),caretBrush.Get(),1.5f);
            }
        }
    }
    rt->EndDraw();
}

LRESULT CALLBACK D2DTextInputProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR subclassId, DWORD_PTR referenceData) {
    (void)subclassId;
    (void)referenceData;
    auto* s=reinterpret_cast<D2DTextInputState*>(GetPropW(hwnd,L"QuickSift.D2DTextInputState"));
    if (!s) return DefSubclassProc(hwnd,message,wParam,lParam);
    switch(message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps{}; BeginPaint(hwnd,&ps); PaintD2DTextInput(hwnd,*s); EndPaint(hwnd,&ps); return 0; }
    case WM_SIZE: if (s->target) s->target->Resize(D2D1::SizeU(LOWORD(lParam),HIWORD(lParam))); InvalidateRect(hwnd,nullptr,FALSE); return 0;
    case WM_SETFOCUS: s->focused=true; s->caretVisible=true; SetTimer(hwnd,kD2DTextInputTimer,500,nullptr); InvalidateRect(hwnd,nullptr,FALSE); return 0;
    case WM_KILLFOCUS: s->focused=false; KillTimer(hwnd,kD2DTextInputTimer); InvalidateRect(hwnd,nullptr,FALSE); return 0;
    case WM_TIMER: if(wParam==kD2DTextInputTimer){s->caretVisible=!s->caretVisible;InvalidateRect(hwnd,nullptr,FALSE);} return 0;
    case WM_SETFONT: s->format.Reset(); return DefSubclassProc(hwnd,message,wParam,lParam);
    case WM_SETTEXT: { const wchar_t* v=reinterpret_cast<const wchar_t*>(lParam); s->text=v?v:L""; s->caret=s->selectionAnchor=s->text.size(); s->scrollY=0; InvalidateRect(hwnd,nullptr,FALSE); return TRUE; }
    case WM_GETTEXTLENGTH: return static_cast<LRESULT>(s->text.size());
    case WM_GETTEXT: { const int cap=static_cast<int>(wParam); if(!lParam||cap<=0)return 0; const int n=std::min<int>(cap-1,static_cast<int>(s->text.size())); wcsncpy_s(reinterpret_cast<wchar_t*>(lParam),cap,s->text.c_str(),n); return n; }
    case WM_CHAR: {
        wchar_t ch=static_cast<wchar_t>(wParam);
        if(ch==8){auto [a,b]=SelectionRange(*s); if(a!=b)s->text.erase(a,b-a); else if(s->caret>0){s->text.erase(s->caret-1,1);--s->caret;} s->selectionAnchor=s->caret;}
        else if(ch==13 || ch==10 || ch>=32){auto [a,b]=SelectionRange(*s); if(a!=b)s->text.erase(a,b-a); s->text.insert(s->caret,1,ch); ++s->caret; s->selectionAnchor=s->caret;}
        ClampTextInputScroll(hwnd,*s); InvalidateRect(hwnd,nullptr,FALSE); return 0; }
    case WM_KEYDOWN: {
        const bool ctrl=GetKeyState(VK_CONTROL)<0, shift=GetKeyState(VK_SHIFT)<0; const auto old=s->caret;
        if(ctrl && wParam=='A'){s->selectionAnchor=0;s->caret=s->text.size();}
        else if(wParam==VK_LEFT){ if(s->caret>0)--s->caret; }
        else if(wParam==VK_RIGHT){ if(s->caret<s->text.size())++s->caret; }
        else if(wParam==VK_HOME){s->caret=0;}
        else if(wParam==VK_END){s->caret=s->text.size();}
        else if(wParam==VK_UP || wParam==VK_DOWN){ s->scrollY += (wParam==VK_DOWN?24:-24); }
        else if(wParam==VK_BACK){
            auto [a,b]=SelectionRange(*s);
            if(a!=b) {
                s->text.erase(a,b-a);
                s->caret=a;
            } else if(s->caret>0) {
                s->text.erase(s->caret-1,1);
                --s->caret;
            }
            s->selectionAnchor=s->caret;
        }
        else if(wParam==VK_DELETE){auto [a,b]=SelectionRange(*s); if(a!=b){s->text.erase(a,b-a); s->caret=a;} else if(s->caret<s->text.size())s->text.erase(s->caret,1); s->selectionAnchor=s->caret;}
        if(!shift && wParam!=VK_UP && wParam!=VK_DOWN && !(ctrl&&wParam=='A')) s->selectionAnchor=s->caret;
        if(shift && (wParam==VK_LEFT||wParam==VK_RIGHT||wParam==VK_HOME||wParam==VK_END)) { if(old==s->selectionAnchor) s->selectionAnchor=old; }
        ClampTextInputScroll(hwnd,*s); InvalidateRect(hwnd,nullptr,FALSE); return 0; }
    case WM_MOUSEWHEEL: s->scrollY -= GET_WHEEL_DELTA_WPARAM(wParam)/4; ClampTextInputScroll(hwnd,*s); InvalidateRect(hwnd,nullptr,FALSE); return 0;
    case WM_LBUTTONDOWN: SetFocus(hwnd); s->caret=s->text.size(); s->selectionAnchor=s->caret; return 0;
    case WM_NCDESTROY: KillTimer(hwnd,kD2DTextInputTimer); RemovePropW(hwnd,L"QuickSift.D2DTextInputState"); delete s; RemoveWindowSubclass(hwnd,D2DTextInputProc,0x5154494Eu); break;
    default: break;
    }
    return DefSubclassProc(hwnd,message,wParam,lParam);
}

bool EnsureD2DTextInputClass(HINSTANCE instance) {
    static ATOM atom=0;
    if(atom) return true;
    WNDCLASSEXW wc{sizeof(wc)};
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
    if (!SetWindowSubclass(window, ButtonSubclassProc, kButtonSubclassId, 0)) {
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
    if (!EnsureD2DTextInputClass(instance)) return nullptr;
    HWND window = CreateWindowExW(0, kD2DTextInputClass, text.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
        0,0,10,10,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
    if (!window) return nullptr;
    auto* state = new (std::nothrow) D2DTextInputState();
    if (!state) { DestroyWindow(window); return nullptr; }
    state->text = std::move(text);
    SetPropW(window, L"QuickSift.D2DTextInputState", reinterpret_cast<HANDLE>(state));
    if (!SetWindowSubclass(window,D2DTextInputProc,0x5154494Eu,0)) {
        RemovePropW(window, L"QuickSift.D2DTextInputState");
        delete state;
        DestroyWindow(window);
        return nullptr;
    }
    return window;
}

void MacOsUiFramework::SetD2DTextInputPalette(HWND window, const ThemePalette& palette, UINT dpi) const noexcept {
    auto* state = window ? reinterpret_cast<D2DTextInputState*>(GetPropW(window,L"QuickSift.D2DTextInputState")) : nullptr;
    if (!state) return;
    state->dpi = dpi ? dpi : USER_DEFAULT_SCREEN_DPI;
    state->panel = palette.panelStrong;
    state->border = palette.border;
    state->accent = palette.accent;
    state->textColor = palette.text;
    state->selection = palette.accent;
    state->format.Reset();
    InvalidateRect(window,nullptr,FALSE);
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
    if (!button || !GetPropW(button, kButtonHotProperty)) return 0.0f;
    const HANDLE tickHandle = GetPropW(button, kButtonHoverTickProperty);
    if (!tickHandle) return 1.0f;
    const DWORD started = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(tickHandle));
    const DWORD elapsed = GetTickCount() - started;
    return Smoothstep01(static_cast<float>(elapsed) / 110.0f);
}

void MacOsUiFramework::ApplyRoundedControlRegion(HWND window, int width, int height,
    int radius, bool redraw) const noexcept {
    ApplyRoundedRegion(window, width, height, radius, redraw);
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

LRESULT CALLBACK MacOsUiFramework::ButtonSubclassProc(HWND window, UINT message,
    WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR referenceData) {
    (void)subclassId;
    (void)referenceData;
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
        if (!GetPropW(window, kButtonHotProperty)) {
            SetPropW(window, kButtonHotProperty, reinterpret_cast<HANDLE>(1));
            SetPropW(window, kButtonHoverTickProperty,
                reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(GetTickCount())));
            TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0 };
            TrackMouseEvent(&track);
            SetTimer(window, kButtonHoverTimerId, 16, nullptr);
            InvalidateRect(window, nullptr, FALSE);
        }
        break;
    case WM_TIMER:
        if (wParam == kButtonHoverTimerId) {
            InvalidateRect(window, nullptr, FALSE);
            const HANDLE tickHandle = GetPropW(window, kButtonHoverTickProperty);
            if (!GetPropW(window, kButtonHotProperty) || !tickHandle ||
                GetTickCount() - static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(tickHandle)) >= 120) {
                KillTimer(window, kButtonHoverTimerId);
            }
            return 0;
        }
        break;
    case WM_MOUSELEAVE:
        KillTimer(window, kButtonHoverTimerId);
        RemovePropW(window, kButtonHotProperty);
        RemovePropW(window, kButtonHoverTickProperty);
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        // Owner-draw ODS_SELECTED updates on press/release; force a crisp repaint.
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_NCDESTROY:
        KillTimer(window, kButtonHoverTimerId);
        RemovePropW(window, kButtonHotProperty);
        RemovePropW(window, kButtonHoverTickProperty);
        RemoveWindowSubclass(window, ButtonSubclassProc, kButtonSubclassId);
        break;
    default:
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

} // namespace quicksift::ui::framework
