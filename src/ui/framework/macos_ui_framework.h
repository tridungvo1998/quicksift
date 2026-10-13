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

#include "ui/framework/ui_animation.h"
#include "ui/framework/ui_frost.h"
#include "ui/framework/ui_models.h"
#include "ui/ui_design_system.h"

#include <functional>
#include <array>
#include <cstddef>
#include <optional>
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
    // Surface behind the status bar (fills the anti-aliased region's corner slack).
    // CLR_INVALID: palette.windowBottom.
    COLORREF parentSurface = CLR_INVALID;
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

// D2D text input (the shared editor used by the comment field and form dialogs).
// Multiline inputs wrap and scroll vertically (overlay scrollbar, wheel, PgUp/PgDn);
// single-line inputs scroll horizontally and never accept line breaks or tabs.
// The full rounded border is always painted inside the client rect (inset by half
// the stroke) so no edge is clipped at any DPI.
struct TextInputOptions {
    bool multiline = true;
    std::wstring placeholder;
    // Send WM_COMMAND(MAKEWPARAM(id, EN_CHANGE)) to the parent on every user edit
    // and on WM_SETTEXT (not for the initial text).
    bool notifyParent = false;
    std::size_t maxLength = std::size_t{ 1 } << 20;
    // No own fill/border: the input clears with its parent surface colour and is
    // drawn inside chrome painted by its owner (the floating search field).
    bool borderless = false;
};

// ---------------------------------------------------------------------------
// Form dialog: an owned, modal, themed top-level window built from rows. It has
// the document-window chrome (custom title bar, traffic-light close, accent
// strip), one raised content panel, section headers, labelled rows, an inline
// per-field error line, a status line and a primary/secondary footer. Keyboard:
// Tab/Shift+Tab cycle focus (focus rings appear after keyboard navigation),
// Enter activates a focused button / opens a focused dropdown / submits from a
// single-line field, Ctrl+Enter submits from anywhere, Esc cancels. Captions are
// passed already localized. All geometry is DIP-based and DPI-aware.
enum class FormRowKind {
    SectionHeader,   // label = header text
    TextArea,        // multiline scrollable D2D input; heightDip rows tall
    TextField,       // label + single-line D2D input + reserved inline error line
    Dropdown,        // label + popup button (TrackDropdown) showing options[selected]
    Button,          // left-aligned standard button + optional muted note to its right
    Note,            // muted, word-wrapped helper text (heightDip tall)
};

enum class FormEvent {
    Clicked,           // a Button row, the primary button or the cancel button (also Esc/close)
    TextChanged,       // a TextArea/TextField changed (user edit or SetFormText)
    SelectionChanged,  // a Dropdown row picked a different option
    Destroyed,         // the dialog window is gone (sent once, from WM_NCDESTROY)
};

struct FormRow {
    FormRowKind kind = FormRowKind::SectionHeader;
    int id = 0;
    std::wstring label;
    std::wstring text;
    std::wstring placeholder;
    std::wstring note;
    std::vector<std::wstring> options;
    int selected = 0;
    int heightDip = 0;
};

struct FormDialogSpec {
    HWND owner = nullptr;
    HINSTANCE instance = nullptr;
    std::wstring title;
    ThemePalette palette{};
    int widthDip = 600;
    int labelWidthDip = 160;
    std::vector<FormRow> rows;
    int primaryId = IDOK;
    std::wstring primaryText;
    int cancelId = IDCANCEL;
    std::wstring cancelText;
    // Receives (dialog, controlId, event). May call CloseFormDialog/ShowFormDialog.
    std::function<void(HWND, int, FormEvent)> onEvent;
    // Forwarded to TrackDropdown so long dropdowns keep app queues draining.
    std::function<void()> maintenancePump;
    std::function<void(std::wstring_view)> errorSink;
};

struct FolderPickerRequest {
    HWND owner = nullptr;
    std::wstring title;
    std::wstring okLabel;
    std::wstring initialFolder;
};

struct ToastPaintResources {
    ID2D1RenderTarget* target = nullptr;
    IDWriteFactory* writeFactory = nullptr;
    IDWriteTextFormat* textFormat = nullptr;
    D2D1_SIZE_F surfaceSize{};
    ThemePalette palette{};
};

// Floating field (Thumbnail search box): rounded, themed, shadowed chrome painted on
// a Direct2D surface with a search glyph, a right-aligned result count and a clear
// (x) button. The editable text is a borderless framework text input
// (TextInputOptions::borderless) positioned over FloatingFieldLayout::input whose
// parent surface is FloatingFieldFill(), so the two read as one control.
struct FloatingFieldLayout {
    D2D1_RECT_F frame{};
    D2D1_RECT_F glyph{};
    D2D1_RECT_F input{};
    D2D1_RECT_F count{};
    D2D1_RECT_F clear{};
    float radius = 0.0f;
};

struct FloatingFieldPresentation {
    FloatingFieldLayout layout{};
    float opacity = 1.0f;
    bool focused = false;
    bool clearVisible = false;
    bool clearHot = false;
    bool noResults = false;   // count drawn in the danger tint
    std::wstring_view countText;
};

struct AlertPaintResources {
    ID2D1RenderTarget* target = nullptr;
    IDWriteFactory* writeFactory = nullptr;
    IDWriteTextFormat* titleFormat = nullptr;
    IDWriteTextFormat* messageFormat = nullptr;
    D2D1_SIZE_F surfaceSize{};
    ThemePalette palette{};
};

// Faint image-space outline drawn over a detected face (FaceLock). Opacities are
// deliberately low; the FaceLock target is only marginally stronger.
struct FaceOutlinePresentation {
    D2D1_RECT_F rect{};
    bool target = false;
    bool flagged = false;
    // 0..1 multiplier for the reveal/fade animation (1 = fully shown). Outlines
    // with opacity ~0 are skipped; their hit boxes stay clickable regardless.
    float opacity = 1.0f;
};

// Shared glass material (docs/DESIGN_SYSTEM.md): ambient + key shadow, translucent
// body, top-half vibrancy sheen, 1 px specular top edge and a hairline drawn inside
// the shape. Values come from tokens::Glass(); high contrast / reduced transparency
// render an opaque body with a single crisp shadow. Cheap: no blur, no layers.
struct GlassSurfacePresentation {
    D2D1_RECT_F rect{};
    float radius = 0.0f;
    COLORREF fill = RGB(255, 255, 255);
    COLORREF border = RGB(0, 0, 0);
    float opacity = 1.0f;          // fade multiplier
    float borderWidth = 1.0f;      // DIP; rounded to whole device pixels (>= 1 px)
    float elevation = 1.0f;        // shadow scale (alerts use a deeper shadow)
    bool opaque = false;           // body ignores the material's translucency
    // Vibrancy sheen over the top half. Off for surfaces that host a native child
    // (the search field): the child clears with the flat fill, so a sheen around it
    // read as a rectangular block over the field's upper half.
    bool sheen = true;
};

// Which DWM chrome a top-level window gets (ApplyWindowChrome).
enum class WindowChromeKind {
    Main,        // custom-drawn main window: rounded, no system border, no backdrop
    Fullscreen,  // main window while fullscreen: square corners
    Document,    // owned document/diagnostic windows
    Dialog,      // framework form dialogs
    Popup,       // dropdown menus (returns whether DWM rounds them)
};

struct MediaCardPresentation {
    D2D1_RECT_F rect{};
    float radius = 14.0f;
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
    ~MacOsUiFramework();
    MacOsUiFramework(const MacOsUiFramework&) = delete;
    MacOsUiFramework& operator=(const MacOsUiFramework&) = delete;

    static constexpr wchar_t PopupWindowClassName[] = L"QuickSift.Framework.PopupMenu";

    static bool InitializeNativeControls() noexcept;
    // Central DWM chrome (dark mode, caption/text/border colours, corner preference,
    // system backdrop), gated by CurrentVisualEffects(): Mica only for Document /
    // Dialog windows while glass is enabled; system colours in high contrast.
    // Returns true when DWM accepted the corner preference (Windows 11).
    static bool ApplyWindowChrome(HWND window, WindowChromeKind kind,
        const ThemePalette& palette) noexcept;
    bool RegisterPopupWindowClass(HINSTANCE instance) noexcept;

    HWND CreateControl(const NativeControlSpec& spec) const noexcept;
    HWND CreateButton(const ButtonControlSpec& spec) const noexcept;
    HWND CreateOwnerDrawStatic(HWND parent, HINSTANCE instance, int id,
        std::wstring text, DWORD extendedStyle = 0, DWORD additionalStyle = 0) const noexcept;
    HWND CreateD2DTextInput(HWND parent, HINSTANCE instance, int id,
        std::wstring text = L"") const noexcept;
    HWND CreateTextInput(HWND parent, HINSTANCE instance, int id,
        const TextInputOptions& options, std::wstring text = L"") const noexcept;
    [[nodiscard]] static bool IsTextInput(HWND window) noexcept;
    void SetD2DTextInputPalette(HWND window, const ThemePalette& palette, UINT dpi) const noexcept;
    // Colour painted behind the rounded corners (the surface the input sits on).
    void SetTextInputParentSurface(HWND window, COLORREF color) const noexcept;
    // Danger-coloured border while a field holds an invalid value.
    void SetTextInputError(HWND window, bool error) const noexcept;
    void SetTextInputPlaceholder(HWND window, std::wstring_view placeholder) const noexcept;
    // Selects the whole content (Ctrl+F on an already-filled search field).
    void SelectAllTextInput(HWND window) const noexcept;

    // Form dialog (macos_ui_framework_form.cpp). Returns the shown dialog; the
    // owner is disabled while the dialog is visible.
    HWND CreateFormDialog(FormDialogSpec spec);
    [[nodiscard]] static std::wstring FormText(HWND dialog, int id);
    void SetFormText(HWND dialog, int id, std::wstring_view text) const;
    [[nodiscard]] static int FormSelection(HWND dialog, int id) noexcept;
    void SetFormSelection(HWND dialog, int id, int index) const;
    // Empty message clears the inline error (TextField rows).
    void SetFormFieldError(HWND dialog, int id, std::wstring_view message) const;
    void SetFormNote(HWND dialog, int id, std::wstring_view note) const;
    void SetFormStatus(HWND dialog, std::wstring_view text, bool error) const;
    // Busy: every control except the cancel button is disabled.
    void SetFormBusy(HWND dialog, bool busy) const;
    void SetFormPalette(HWND dialog, const ThemePalette& palette) const;
    void SetFormTitle(HWND dialog, std::wstring_view title) const;
    // Hide (owner re-enabled) or re-show (owner disabled, focus restored).
    void ShowFormDialog(HWND dialog, bool visible) const;
    void FocusFormControl(HWND dialog, int id) const;
    // Hides immediately and destroys asynchronously (safe from inside onEvent).
    void CloseFormDialog(HWND dialog) const;
    [[nodiscard]] static bool IsFormDialog(HWND window) noexcept;
    // IFileOpenDialog in FOS_PICKFOLDERS mode; nullopt when cancelled/failed.
    [[nodiscard]] static std::optional<std::wstring> PickFolder(const FolderPickerRequest& request);
    HWND CreateTooltip(const TooltipControlSpec& spec) const noexcept;

    bool IsButtonHot(HWND button) const noexcept;
    // 0..1 hover fade (in AND out) on the central animator's button channel.
    [[nodiscard]] float ButtonHoverBlend(HWND button) const noexcept;
    // Hover channel target for a framework button; animate=false snaps (hidden,
    // disabled or destroyed buttons never animate).
    void SetButtonHover(HWND button, bool hot, bool animate) const;
    // Region for owner-drawn controls that paint their own parent-coloured corners
    // (ButtonPresentation::parentSurface): the region radius is a few device pixels
    // smaller than the painted radius so the whole AA fringe stays inside the
    // region instead of being cut into a 1-bit staircase.
    void ApplyAntialiasedControlRegion(HWND window, int width, int height, int radius,
        bool redraw = true) const noexcept;
    // Mid-height colour of the title bar gradient painted by PaintTitleBar (the
    // parent surface for title-bar buttons).
    static COLORREF TitleBarSurfaceColor(const ThemePalette& palette) noexcept;
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

    // ---- Left-pane frost material + flicker-free owner draw -------------------------
    // The frost image (ui_frost.h: blurred canvas snapshot under the open pane, tinted
    // with panelStrong) is kept as ONE cached DIB. Every pane surface (backdrop, tabs,
    // buttons, labels, list) blits its own slice of it, so they agree pixel-for-pixel
    // and no surface ever falls back to a parent erase. `source` is the top-left of
    // `dest` in frost-image coordinates; pixels outside the image use `fallback`.
    void SetPaneFrost(const frost::Image& image);
    void ClearPaneFrost() noexcept;
    [[nodiscard]] bool HasPaneFrost() const noexcept { return paneFrostBitmap_ != nullptr; }
    void PaintPaneFrost(HDC dc, const RECT& dest, POINT source, COLORREF fallback) const;
    // Pane chrome over the frost: hairline rounded border + optional top wash.
    // Paints no base fill (the frost already is the material).
    void PaintFrostedPanelChrome(HDC dc, const RECT& rect, int radiusDip, bool wash,
        const ThemePalette& palette, UINT dpi) const;
    // One off-screen pass for an owner-drawn item: `background` fills the whole item,
    // `paint` draws it with a DRAWITEMSTRUCT retargeted to the buffer, then one BitBlt.
    // Falls back to painting directly when the buffer cannot be created.
    void PaintItemBuffered(const DRAWITEMSTRUCT& draw,
        const std::function<void(HDC, const RECT&)>& background,
        const std::function<void(const DRAWITEMSTRUCT&)>& paint) const;
    // Same for a whole window client (WM_PAINT paths such as the library list).
    void PaintBuffered(HDC target, const RECT& rect,
        const std::function<void(HDC, const RECT&)>& paint) const;
    void PaintOverlayScrollbar(ID2D1RenderTarget* target, const D2D1_RECT_F& thumb,
        const D2D1_RECT_F& track, bool hot, bool dragging,
        const ThemePalette& palette) const;

    int TrackDropdown(DropdownRequest request);

    // ---- Central animation system (ui_animation.h, docs/DESIGN_SYSTEM.md) ----------
    // Transition presets shared by every caller (no per-feature durations/curves).
    static constexpr animation::Preset kToastTransition =
        animation::presets::SlideFade(animation::Direction::Up, 8.0f);
    static constexpr animation::Preset kAlertTransition = animation::presets::PopScale();
    static constexpr animation::Preset kSearchFieldTransition = animation::presets::Fade();
    static constexpr animation::Preset kFilmstripTransition =
        animation::presets::Fade(tokens::kMotionStandardMs, tokens::kMotionStandardMs);
    static constexpr animation::Preset kThumbnailTransition = animation::presets::Crossfade();
    static constexpr animation::Preset kButtonHoverTransition =
        animation::presets::Toggle(tokens::kMotionFastMs);
    animation::Animator& Motion() noexcept { return animator_; }
    const animation::Animator& Motion() const noexcept { return animator_; }
    // One frame of the shared UI animation timer: refreshes the accessibility state,
    // syncs toast/alert presence with their presenters, advances every animation,
    // repaints per-window targets (buttons) and layered-window alphas, and returns
    // the app-owned surfaces to invalidate (each at most once).
    animation::SurfaceMask TickAnimations(animation::Animator::TimePoint now, bool suspended);
    // Toast hold/queue and alert visibility -> presence targets (idempotent; also
    // called at the start of a canvas frame so a new overlay animates from frame 1).
    void SyncOverlayTransitions(animation::Animator::TimePoint now);
    // Fades a layered top-level window out (input-transparent while fading) and
    // destroys it on completion; destroys at once when it cannot animate.
    // `fromOpacity` is the window's current constant alpha (0..1), so a window closed
    // mid-reveal fades out from where it is instead of popping back to opaque.
    void FadeOutAndDestroyWindow(HWND window, float fromOpacity = 1.0f);
    // WM_DESTROY of the owner: destroys fading windows, drops pending completions.
    void ShutdownAnimations() noexcept;

    ToastPresenter& Toasts() noexcept { return toasts_; }
    const ToastPresenter& Toasts() const noexcept { return toasts_; }
    AlertPresenter& Alerts() noexcept { return alerts_; }
    const AlertPresenter& Alerts() const noexcept { return alerts_; }

    // One device pixel in the target's DIP space, and a rect snapped to whole pixels.
    [[nodiscard]] static float DevicePixelDip(ID2D1RenderTarget* target) noexcept;
    [[nodiscard]] static D2D1_RECT_F SnapToDevicePixels(ID2D1RenderTarget* target,
        const D2D1_RECT_F& rect) noexcept;
    // Returns the pixel-snapped body rect.
    D2D1_RECT_F PaintGlassSurface(ID2D1RenderTarget* target,
        const GlassSurfacePresentation& presentation, const ThemePalette& palette) const;
    // macOS focus ring around `shape` (outside it by kFocusRingGap; concentric radius).
    void PaintFocusRing(ID2D1RenderTarget* target, const D2D1_RECT_F& shape,
        float radius, const ThemePalette& palette, float opacity = 1.0f) const;
    [[nodiscard]] static float MeasureSingleLineText(IDWriteFactory* factory,
        IDWriteTextFormat* format, std::wstring_view text) noexcept;

    void PaintToast(const ToastPaintResources& resources,
        ToastPresenter::TimePoint now = ToastPresenter::Clock::now()) const;
    void PaintAlert(const AlertPaintResources& resources);
    // Floating field geometry in surface DIPs (count slot and clear button optional).
    [[nodiscard]] static FloatingFieldLayout LayoutFloatingField(const D2D1_RECT_F& frame,
        bool showCount, bool showClear) noexcept;
    // Opaque fill of the floating field (also the text input's parent surface).
    [[nodiscard]] static COLORREF FloatingFieldFill(const ThemePalette& palette) noexcept;
    void PaintFloatingField(ID2D1RenderTarget* target,
        const FloatingFieldPresentation& presentation, const ThemePalette& palette,
        IDWriteTextFormat* countFormat) const;
    void PaintMediaCard(ID2D1RenderTarget* target,
        const MediaCardPresentation& presentation,
        const MediaCardBrushes& brushes) const;
    // Cheap rounded image-well placeholder (one solid fill, no I/O) used while a
    // thumbnail is not yet decoded or the grid is in motion.
    void PaintImagePlaceholder(ID2D1RenderTarget* target, const D2D1_RECT_F& rect,
        float radius, ID2D1Brush* fill) const;
    // Anti-aliased rounded corners for an HWND whose 1-bit window region is a few
    // pixels larger than the visual radius (ApplyAntialiasedControlRegion): paints
    // the four "square minus quarter circle" corner patches in the parent's colour
    // (top-left, top-right, bottom-right, bottom-left) so the visible edge is an AA
    // arc. One stroked rounded rect per corner under an axis-aligned clip; cheap.
    void PaintRoundedCornerUnderlay(ID2D1RenderTarget* target, D2D1_SIZE_F size,
        float radius, const std::array<D2D1_COLOR_F, 4>& colors,
        ID2D1SolidColorBrush* brush) const;
    void PaintFaceOutline(ID2D1RenderTarget* target,
        const FaceOutlinePresentation& presentation,
        ID2D1SolidColorBrush* stroke, ID2D1SolidColorBrush* shadow) const;
    void PaintOverlayLabel(ID2D1RenderTarget* target, const D2D1_RECT_F& rect,
        float radius, std::wstring_view text, IDWriteTextFormat* format,
        ID2D1Brush* fill, ID2D1Brush* textBrush,
        ID2D1Brush* border = nullptr, float borderWidth = 1.0f, bool centered = false) const;
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
    // mutable: const component factories (CreateButton) register hover channels.
    mutable animation::Animator animator_{};
    std::vector<HWND> fadingWindows_;
    HDC paneFrostDc_ = nullptr;     // memory DC holding the frost DIB
    HBITMAP paneFrostBitmap_ = nullptr;
    HGDIOBJ paneFrostPrevious_ = nullptr;
    int paneFrostWidth_ = 0;
    int paneFrostHeight_ = 0;
    std::wstring toastGhostText_;   // painted while a cleared toast fades out
    AlertRequest alertGhost_{};     // painted while a resolved alert fades out
};

} // namespace quicksift::ui::framework
