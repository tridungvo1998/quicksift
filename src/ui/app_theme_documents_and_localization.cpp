// OWNER: Compiled QuickSiftApplication feature module.
#include "app/quicksift_application_internal.h"

namespace quicksift::app {



    // All visual roles come from one design-system implementation.
    ThemePalette QuickSiftApplicationImpl::Palette() const noexcept {
        return quicksift::ui::GetThemePalette(theme_);
    }


    std::wstring QuickSiftApplicationImpl::Tr(std::wstring_view english) const { return localizer_.Text(english); }


    std::wstring QuickSiftApplicationImpl::ThemeToggleText() const {
        return Tr(theme_ == AppTheme::Light ? L"Light" : L"Dark");
    }


    std::wstring QuickSiftApplicationImpl::FullscreenButtonText() const {
        return Tr(fullscreen_ ? L"Exit fullscreen" : L"Fullscreen");
    }


    ThemePalette QuickSiftApplicationImpl::CurrentDocumentTheme() const noexcept {
        ThemePalette palette = Palette();
        // Read-only document windows use the stronger panel surface so long text
        // remains distinct from the translucent application background.
        palette.panel = palette.panelStrong;
        return palette;
    }


    void QuickSiftApplicationImpl::ApplyDocumentTheme(HWND window, DocumentWindowState* state) {
        if (!window || !state) return;
        state->theme = CurrentDocumentTheme();
        state->normalFont = uiFont_;
        state->semiboldFont = uiFontSemibold_;
        HBRUSH replacementPanelBrush = CreateSolidBrush(state->theme.panel);
        if (replacementPanelBrush) {
            if (state->panelBrush) DeleteObject(state->panelBrush);
            state->panelBrush = replacementPanelBrush;
        } else {
            QS_LOG_WARNING(L"UI", L"Document theme could not allocate a replacement panel brush");
        }
        BOOL dark = state->theme.dark ? TRUE : FALSE;
        DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(window, 35, &state->theme.windowTop, sizeof(state->theme.windowTop));
        DwmSetWindowAttribute(window, 36, &state->theme.text, sizeof(state->theme.text));
        DwmSetWindowAttribute(window, 34, &state->theme.border, sizeof(state->theme.border));
        DWORD rounded = 2;
        DWORD backdrop = 2;
        DwmSetWindowAttribute(window, 33, &rounded, sizeof(rounded));
        DwmSetWindowAttribute(window, 38, &backdrop, sizeof(backdrop));
        if (state->edit) {
            SetWindowTheme(state->edit, state->theme.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
            SendMessageW(state->edit, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(state->theme.panel));
            if (!state->richEdit && state->normalFont) {
                SendMessageW(state->edit, WM_SETFONT,
                    reinterpret_cast<WPARAM>(state->normalFont), TRUE);
            }
        }
        for (HWND button : { state->refresh, state->exportButton, state->close }) {
            if (!button) continue;
            SetWindowTheme(button, L"", L"");
            if (state->semiboldFont) {
                SendMessageW(button, WM_SETFONT,
                    reinterpret_cast<WPARAM>(state->semiboldFont), TRUE);
            }
        }
        InvalidateRect(window, nullptr, TRUE);
    }


    void QuickSiftApplicationImpl::ShowDocumentWindow(const std::wstring& title, const std::wstring& body, DocumentKind kind) {
        const int width = DipToPx(760);
        const int height = DipToPx(680);
        RECT owner{};
        if (!GetWindowRect(hwnd_, &owner)) {
            owner = { 0, 0, width, height };
            QS_LOG_WARNING(L"UI", L"Document window is using fallback placement because the owner rectangle was unavailable");
        }
        const LONG centeredX = owner.left + std::max<LONG>(0L,
            (owner.right - owner.left - static_cast<LONG>(width)) / 2);
        const LONG centeredY = owner.top + std::max<LONG>(0L,
            (owner.bottom - owner.top - static_cast<LONG>(height)) / 2);
        const int x = static_cast<int>(centeredX);
        const int y = static_cast<int>(centeredY);
        HWND document = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT,
            kDocumentClass, title.c_str(), WS_OVERLAPPED | WS_THICKFRAME,
            x, y, width, height, hwnd_, nullptr, instance_, nullptr);
        if (!document) {
            quicksift::diagnostics::WriteLastError(quicksift::diagnostics::Level::Error,
                L"UI", L"Creating a document window");
            return;
        }
        if (!SetPropW(document, kDocumentKindProperty,
            reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(kind)))) {
            quicksift::diagnostics::WriteLastError(quicksift::diagnostics::Level::Error,
                L"UI", L"Attaching document-window type state");
            DestroyWindow(document);
            return;
        }

        auto* state = new (std::nothrow) DocumentWindowState();
        if (!state) {
            QS_LOG_ERROR(L"UI", L"Allocating document-window state failed");
            DestroyWindow(document);
            return;
        }
        state->framework = &uiFramework_;
        state->normalFont = uiFont_;
        state->semiboldFont = uiFontSemibold_;
        if (!SetWindowSubclass(document, DocumentWindowSubclassProc, kDocumentWindowSubclassId,
            reinterpret_cast<DWORD_PTR>(state))) {
            quicksift::diagnostics::WriteLastError(quicksift::diagnostics::Level::Error,
                L"UI", L"Attaching document-window behavior");
            delete state;
            DestroyWindow(document);
            return;
        }

        state->richEdit = EnsureRichEditLoaded();
        state->diagnosticLog = kind == DocumentKind::DiagnosticLog;
        const wchar_t* documentControlClass = state->richEdit ? MSFTEDIT_CLASS : L"EDIT";
        quicksift::ui::framework::NativeControlSpec editSpec{};
        editSpec.className = documentControlClass;
        editSpec.style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | ES_LEFT |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL;
        editSpec.parent = document;
        editSpec.id = ID_DOCUMENT_TEXT;
        editSpec.instance = instance_;
        state->edit = uiFramework_.CreateControl(editSpec);
        if (state->diagnosticLog) {
            state->refresh = uiFramework_.CreateButton({ document, instance_,
                ID_DOCUMENT_REFRESH, Tr(L"Refresh") });
            state->exportButton = uiFramework_.CreateButton({ document, instance_,
                ID_DOCUMENT_EXPORT, Tr(L"Export…") });
            state->verboseLogging = uiFramework_.CreateControl({
                0, L"BUTTON", Tr(L"Verbose logging"),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | BS_AUTOCHECKBOX,
                document, ID_DOCUMENT_VERBOSE, instance_, nullptr});
            if (state->verboseLogging) {
                SendMessageW(state->verboseLogging, BM_SETCHECK,
                    quicksift::diagnostics::VerboseLogging() ? BST_CHECKED : BST_UNCHECKED, 0);
                if (state->normalFont) SendMessageW(state->verboseLogging, WM_SETFONT,
                    reinterpret_cast<WPARAM>(state->normalFont), TRUE);
                SetWindowTheme(state->verboseLogging, state->theme.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
            }
        }
        state->close = uiFramework_.CreateButton({ document, instance_, IDOK, Tr(L"Close") });
        if (state->close) {
            // This button is owner-drawn. Do not let the generic BUTTON class
            // reserve/paint a keyboard-focus cue on top of its custom rounded outline.
            SetWindowLongPtrW(state->close, GWL_STYLE,
                GetWindowLongPtrW(state->close, GWL_STYLE) &
                    ~static_cast<LONG_PTR>(WS_TABSTOP));
        }
        if (!state->edit || !state->close ||
            (state->diagnosticLog && (!state->refresh || !state->exportButton || !state->verboseLogging))) {
            QS_LOG_ERROR(L"UI", L"One or more required document-window controls could not be created");
            DestroyWindow(document); // The subclass owns and releases state.
            return;
        }
        if (!SetPropW(document, kDocumentStateProperty, state)) {
            quicksift::diagnostics::WriteLastError(quicksift::diagnostics::Level::Error,
                L"UI", L"Publishing document-window state");
            DestroyWindow(document); // The subclass owns and releases state.
            return;
        }

        ApplyDocumentTheme(document, state);
        if (uiFont_) {
            if (!state->richEdit)
                SendMessageW(state->edit, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
            if (state->refresh) SendMessageW(state->refresh, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
            if (state->exportButton) SendMessageW(state->exportButton, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
            if (state->verboseLogging) SendMessageW(state->verboseLogging, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
            SendMessageW(state->close, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        }
        SetDocumentControlText(state->edit, body, kind == DocumentKind::Help, state->richEdit, state->theme);
        RECT client{};
        if (GetClientRect(document, &client)) {
            SendMessageW(document, WM_SIZE, 0, MAKELPARAM(client.right, client.bottom));
        }
        ShowWindow(document, SW_SHOWNORMAL);
        UpdateWindow(document);
    }


    void QuickSiftApplicationImpl::ShowHelp() {
        ShowDocumentWindow(Tr(L"QuickSift Help"), localizer_.HelpDocument(), DocumentKind::Help);
    }


    void QuickSiftApplicationImpl::ShowAbout() {
        ShowDocumentWindow(Tr(L"About QuickSift"), localizer_.AboutDocument(), DocumentKind::About);
    }


    void QuickSiftApplicationImpl::ShowDiagnosticLog() {
        ShowDocumentWindow(Tr(L"QuickSift Diagnostic Log"),
            quicksift::diagnostics::SnapshotText(), DocumentKind::DiagnosticLog);
    }


    void QuickSiftApplicationImpl::RefreshOpenDocumentWindows() {
        struct RefreshContext { QuickSiftApplicationImpl* app; } context{ this };
        EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM parameter) noexcept -> BOOL {
            try {
                auto* context = reinterpret_cast<RefreshContext*>(parameter);
                if (!context || !context->app ||
                    GetWindow(window, GW_OWNER) != context->app->hwnd_) return TRUE;
                const auto kindValue = reinterpret_cast<ULONG_PTR>(
                    GetPropW(window, kDocumentKindProperty));
                if (kindValue == 0) return TRUE;
                const auto kind = static_cast<DocumentKind>(kindValue);
                const bool help = kind == DocumentKind::Help;
                const bool diagnosticLog = kind == DocumentKind::DiagnosticLog;
                const std::wstring title = help ? context->app->Tr(L"QuickSift Help") :
                    (diagnosticLog ? context->app->Tr(L"QuickSift Diagnostic Log") :
                        context->app->Tr(L"About QuickSift"));
                const std::wstring body = help ? context->app->localizer_.HelpDocument() :
                    (diagnosticLog ? quicksift::diagnostics::SnapshotText() :
                        context->app->localizer_.AboutDocument());
                SetWindowTextW(window, title.c_str());
                auto* state = reinterpret_cast<DocumentWindowState*>(
                    GetPropW(window, kDocumentStateProperty));
                if (state) context->app->ApplyDocumentTheme(window, state);
                if (HWND edit = GetDlgItem(window, ID_DOCUMENT_TEXT)) {
                    // The control class is the reliable source when refreshing an already-open window.
                    wchar_t className[32]{};
                    GetClassNameW(edit, className, static_cast<int>(std::size(className)));
                    const bool richEdit = lstrcmpiW(className, MSFTEDIT_CLASS) == 0;
                    SetDocumentControlText(edit, body, help, richEdit,
                        state ? state->theme : context->app->CurrentDocumentTheme());
                }
                if (HWND refresh = GetDlgItem(window, ID_DOCUMENT_REFRESH))
                    SetWindowTextW(refresh, context->app->Tr(L"Refresh").c_str());
                if (HWND exportButton = GetDlgItem(window, ID_DOCUMENT_EXPORT))
                    SetWindowTextW(exportButton, context->app->Tr(L"Export…").c_str());
                if (HWND close = GetDlgItem(window, IDOK))
                    SetWindowTextW(close, context->app->Tr(L"Close").c_str());
                return TRUE;
            } catch (...) {
                quicksift::diagnostics::Write(quicksift::diagnostics::Level::Error,
                    L"RefreshOpenDocumentWindows",
                    L"Stopped refreshing document windows after an unexpected exception");
                return FALSE;
            }
        }, reinterpret_cast<LPARAM>(&context));
    }


    bool QuickSiftApplicationImpl::HasFaceFocusWarning(const PhotoItem& photo) noexcept {
        return photo.facesScanned && !photo.faces.empty() && photo.faceBlurScanned && photo.faceBlurry;
    }


    std::wstring QuickSiftApplicationImpl::FaceWarningTooltipText() const {
        return Tr(L"Red border: the largest detected face appears quite blurry or out of focus. Medium sensitivity flags only clear focus misses; treat this as a warning, not a definitive focus test.");
    }


    void QuickSiftApplicationImpl::RefreshFaceWarningTooltipText() {
        faceWarningTooltipText_ = FaceWarningTooltipText();
        if (!faceWarningTooltip_ || !canvas_) return;
        TOOLINFOW tool{ sizeof(TOOLINFOW) };
        tool.hwnd = canvas_;
        tool.uId = kFaceWarningTooltipId;
        tool.lpszText = faceWarningTooltipText_.data();
        SendMessageW(faceWarningTooltip_, TTM_UPDATETIPTEXTW, 0,
            reinterpret_cast<LPARAM>(&tool));
    }


    void QuickSiftApplicationImpl::CreateFaceWarningTooltip() {
        if (!canvas_ || faceWarningTooltip_) return;
        faceWarningTooltipText_ = FaceWarningTooltipText();
        quicksift::ui::framework::TooltipControlSpec spec{};
        spec.owner = hwnd_;
        spec.target = canvas_;
        spec.instance = instance_;
        spec.toolId = kFaceWarningTooltipId;
        spec.text = &faceWarningTooltipText_;
        faceWarningTooltip_ = uiFramework_.CreateTooltip(spec);
        if (!faceWarningTooltip_) {
            quicksift::diagnostics::WriteLastError(quicksift::diagnostics::Level::Warning,
                L"UI", L"Creating the face-focus warning tooltip");
            return;
        }
    }


    void QuickSiftApplicationImpl::HideFaceWarningTooltip() {
        if (!faceWarningTooltip_) return;
        if (faceWarningTooltipRect_.left == 0 && faceWarningTooltipRect_.top == 0 &&
            faceWarningTooltipRect_.right == 0 && faceWarningTooltipRect_.bottom == 0) return;
        faceWarningTooltipRect_ = RECT{};
        TOOLINFOW tool{ sizeof(TOOLINFOW) };
        tool.hwnd = canvas_;
        tool.uId = kFaceWarningTooltipId;
        tool.rect = faceWarningTooltipRect_;
        SendMessageW(faceWarningTooltip_, TTM_NEWTOOLRECTW, 0,
            reinterpret_cast<LPARAM>(&tool));
        SendMessageW(faceWarningTooltip_, TTM_POP, 0, 0);
    }


    void QuickSiftApplicationImpl::UpdateFaceWarningTooltip(float x, float y) {
        if (!faceWarningTooltip_ || reviewState_.Mode() != ViewMode::Thumbnails || uiFramework_.Alerts().Visible()) {
            HideFaceWarningTooltip();
            return;
        }

        const auto hovered = HitThumbnail(x, y);
        if (!hovered || *hovered >= catalog_.VisibleCount() ||
            !HasFaceFocusWarning(VisiblePhoto(*hovered))) {
            HideFaceWarningTooltip();
            return;
        }

        const D2D1_RECT_F hoveredRect = ThumbnailRectForIndex(*hovered);
        const float scale = CanvasPixelScale();
        RECT next{
            static_cast<LONG>(std::floor(hoveredRect.left * scale)),
            static_cast<LONG>(std::floor(hoveredRect.top * scale)),
            static_cast<LONG>(std::ceil(hoveredRect.right * scale)),
            static_cast<LONG>(std::ceil(hoveredRect.bottom * scale))
        };
        if (EqualRect(&next, &faceWarningTooltipRect_)) return;
        faceWarningTooltipRect_ = next;
        TOOLINFOW tool{ sizeof(TOOLINFOW) };
        tool.hwnd = canvas_;
        tool.uId = kFaceWarningTooltipId;
        tool.rect = faceWarningTooltipRect_;
        SendMessageW(faceWarningTooltip_, TTM_NEWTOOLRECTW, 0,
            reinterpret_cast<LPARAM>(&tool));
    }


    void QuickSiftApplicationImpl::RefreshLocalizedText() {
        if (hwnd_) SetWindowTextW(hwnd_, localizer_.WindowTitle().c_str());
        for (const auto& [id, key] : controlTextKeys_) {
            const auto found = controls_.find(id);
            if (found != controls_.end()) SetWindowTextW(found->second, Tr(key).c_str());
        }
        if (filterLabel_) SetWindowTextW(filterLabel_, Tr(L"Filter by:").c_str());
        if (exif_ && ActiveInfoPath().empty())
            SetWindowTextW(exif_, Tr(L"Select a photo to inspect its EXIF information.").c_str());
        if (status_ && currentFolder_.empty())
            SetWindowTextW(status_, Tr(L"Choose a folder to begin.").c_str());
        UpdateFormatFilterButton();
        UpdateDateFilterButton();
        UpdateSortButton();
        UpdateRatingButton();
        UpdateThumbnailSizeButton();
        UpdateToggleButtons();
        UpdateLanguageSelectorButton();
        UpdateHistoryButtons();
        RefreshFaceWarningTooltipText();
        RefreshFaceLockAvailabilityUi();

        // Localization can change the measured size of vertical flyout tabs.
        // Re-run the geometry pass after captions are replaced so the rail
        // grows/shrinks with the actual localized labels instead of retaining
        // the dimensions from the previous language.
        RECT client{};
        if (hwnd_ && GetClientRect(hwnd_, &client)) {
            LayoutControls(client.right - client.left, client.bottom - client.top);
            // LayoutControls changes the child-window rectangles; force the
            // four owner-drawn tabs to repaint immediately with the new geometry.
            for (HWND tab : flyoutTabs_) {
                if (!tab) continue;
                InvalidateRect(tab, nullptr, FALSE);
                UpdateWindow(tab);
            }
            if (activeFlyout_ != FlyoutPanel::None) {
                RedrawActiveFlyoutContents();
            }
        }
        InvalidateTitleBar();
        InvalidateCanvas();
        if (status_) InvalidateRect(status_, nullptr, FALSE);
        if (exif_) InvalidateRect(exif_, nullptr, FALSE);
        for (HWND tab : flyoutTabs_) if (tab) InvalidateRect(tab, nullptr, FALSE);
        RedrawActiveFlyoutContents();
        RefreshOpenDocumentWindows();
        RequestExifForActive();
        UpdateStatus();
    }


    D2D1_COLOR_F QuickSiftApplicationImpl::ToD2D(COLORREF color, float alpha ) noexcept {
        return quicksift::ui::ToDirect2DColor(color, alpha);
    }


    void QuickSiftApplicationImpl::InitializeColorBlobs() {
        std::random_device entropy;
        std::mt19937 random(entropy() ^ static_cast<unsigned int>(GetTickCount64()));
        std::uniform_real_distribution<float> position(0.08f, 0.92f);
        std::uniform_real_distribution<float> radius(0.20f, 0.42f);
        const auto& accentColors = quicksift::ui::BackgroundAccentColors();
        backgroundBlobs_.clear();
        for (COLORREF color : accentColors) {
            backgroundBlobs_.push_back({ position(random), position(random), radius(random), color });
        }
    }


    void QuickSiftApplicationImpl::ApplyTheme() {
        const ThemePalette palette = Palette();
        BOOL dark = theme_ == AppTheme::Dark ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));
        // Do not let Windows 11 add its own colored frame around the custom
        // chrome. 0xFFFFFFFE is DWMWA_COLOR_NONE on supported systems and is
        // safely ignored by older Windows versions.
        const COLORREF noSystemBorder = static_cast<COLORREF>(0xFFFFFFFEu);
        DwmSetWindowAttribute(hwnd_, 34, &noSystemBorder, sizeof(noSystemBorder));

        if (userCommentEdit_) uiFramework_.SetD2DTextInputPalette(userCommentEdit_, palette, currentDpi_);
        // Theme changes are presentation-only. Do NOT discard the decoded bitmap/WIC
        // caches here: doing so turns a light/dark toggle into a full thumbnail reload.
        // Recreate only the theme-dependent brushes/background and repaint the existing
        // pixel cache with the new palette.
        textBrush_.Reset();
        panelBrush_.Reset();
        accentBrush_.Reset();
        dangerBrush_.Reset();
        mutedBrush_.Reset();
        backgroundBrush_.Reset();
        overlayBrush_.Reset();
        glassBorderBrush_.Reset();
        glassHighlightBrush_.Reset();
        backgroundPixels_.clear();
        backgroundCacheWidth_ = 0;
        backgroundCacheHeight_ = 0;
        InvalidateRect(hwnd_, nullptr, TRUE);
        UpdateWindow(hwnd_);
        if (titleOverlay_ && IsWindowVisible(titleOverlay_)) {
            RedrawWindow(titleOverlay_, nullptr, nullptr,
                RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
        }
        InvalidateCanvas();
        for (const auto& [id, control] : controls_) {
            (void)id;
            if (IsWindowVisible(control)) RedrawWindow(control, nullptr, nullptr,
                RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
        }
        if (exif_ && IsWindowVisible(exif_)) RedrawWindow(exif_, nullptr, nullptr,
            RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
        if (status_) RedrawWindow(status_, nullptr, nullptr,
            RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
        RedrawActiveFlyoutContents();
        for (HWND tab : flyoutTabs_) {
            if (tab) RedrawWindow(tab, nullptr, nullptr,
                RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
        }
        RefreshOpenDocumentWindows();
    }

} // namespace quicksift::app
