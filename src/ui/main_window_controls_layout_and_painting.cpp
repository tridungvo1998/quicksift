// OWNER: Compiled QuickSiftApplication feature module.
#include "app/quicksift_application_internal.h"

namespace quicksift::app {

// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Required child controls, layout, left hover pane, fonts, and owner-drawn control painting.

// Main-window controls: child creation, DPI/font lifecycle, left-pane layout,
// owner-drawn controls, title/status/info surfaces, and themed painting.

    bool QuickSiftApplicationImpl::CreateMainWindowButton(int id, std::wstring_view captionKey) {
        controlTextKeys_[id] = std::wstring(captionKey);
        const std::wstring localizedCaption = Tr(captionKey);
        quicksift::ui::framework::ButtonControlSpec spec{};
        spec.parent = hwnd_;
        spec.instance = instance_;
        spec.id = id;
        spec.text = localizedCaption;
        HWND control = uiFramework_.CreateButton(spec);
        if (!control) {
            quicksift::diagnostics::WriteLastError(quicksift::diagnostics::Level::Error,
                L"UI", L"Creating a main-window button");
            return false;
        }
        controls_[id] = control;
        return true;
    }


    LRESULT QuickSiftApplicationImpl::OnCreate() {
        currentDpi_ = quicksift::ui::GetWindowDpi(hwnd_);
        if (!currentDpi_) currentDpi_ = USER_DEFAULT_SCREEN_DPI;
        if (!uiFramework_.InitializeNativeControls()) {
            quicksift::diagnostics::WriteLastError(quicksift::diagnostics::Level::Error,
                L"UI", L"Initializing Windows common controls");
            return -1;
        }
        EnsureGpuBudgetDevice();

        const auto createButtons = [this](const auto& definitions) {
            for (const ButtonDefinition& button : definitions) {
                if (!CreateMainWindowButton(button.id, button.captionKey)) return false;
            }
            return true;
        };
        if (!createButtons(kTitleButtons) || !createButtons(kFileButtons) || !createButtons(kCullButtons) ||
            !createButtons(kFilterButtons) || !createButtons(kSettingsButtons)) return -1;
        // CreateButton uses WS_VISIBLE; hide flyout-section buttons until LayoutControls
        // shows only the active section (avoids all sections painting at 0,0 before WM_SIZE).
        for (const ButtonDefinition& button : kFileButtons) ShowWindow(controls_[button.id], SW_HIDE);
        for (const ButtonDefinition& button : kCullButtons) ShowWindow(controls_[button.id], SW_HIDE);
        for (const ButtonDefinition& button : kFilterButtons) ShowWindow(controls_[button.id], SW_HIDE);
        for (const ButtonDefinition& button : kSettingsButtons) ShowWindow(controls_[button.id], SW_HIDE);
        if (!CreateMainWindowButton(ID_HELP, L"")) return -1;
        if (!CreateMainWindowButton(ID_THEME_TOGGLE, L"")) return -1;

        if (!CreateMainWindowButton(ID_FILMSTRIP, L"›")) return -1;
        filmstripToggle_ = controls_[ID_FILMSTRIP];
        if (filmstripToggle_) ShowWindow(filmstripToggle_, SW_HIDE);

        filterLabel_ = uiFramework_.CreateOwnerDrawStatic(hwnd_, instance_,
            ID_FILTER_LABEL, Tr(L"Filter by:"));
        if (filterLabel_) ShowWindow(filterLabel_, SW_HIDE);

        quicksift::ui::framework::NativeControlSpec libraryListSpec{};
        // No WS_EX_CLIENTEDGE: sharp system edge is replaced by PaintPanel stroke +
        // ApplyRoundedControlRegion so the library content panel matches kLeftPaneContentRadiusDip.
        libraryListSpec.extendedStyle = 0;
        libraryListSpec.className = L"LISTBOX";
        libraryListSpec.style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY |
            LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_CLIPSIBLINGS;
        libraryListSpec.parent = hwnd_;
        libraryListSpec.id = ID_LIBRARY_LIST;
        libraryListSpec.instance = instance_;
        libraryList_ = uiFramework_.CreateControl(libraryListSpec);
        if (!libraryList_) {
            QS_LOG_ERROR(L"UI", L"Creating the library list control");
            return -1;
        }
        ShowWindow(libraryList_, SW_HIDE);
        SetWindowTheme(libraryList_, L"", L"");
        SendMessageW(libraryList_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        SendMessageW(libraryList_, LB_SETITEMHEIGHT, 0, DipToPx(36));
        if (!SetWindowSubclass(libraryList_, LibraryListSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this))) return -1;
        quicksift::ui::framework::TooltipControlSpec libraryTooltipSpec{};
        libraryTooltipSpec.owner = hwnd_;
        libraryTooltipSpec.target = libraryList_;
        libraryTooltipSpec.instance = instance_;
        libraryTooltipSpec.toolId = 1;
        libraryTooltipSpec.text = &libraryTooltipText_;
        libraryTooltip_ = uiFramework_.CreateTooltip(libraryTooltipSpec);
        if (!CreateMainWindowButton(ID_REMOVE_LIBRARY, L"Remove from library")) return -1;
        ShowWindow(controls_[ID_REMOVE_LIBRARY], SW_HIDE);
        EnableWindow(controls_[ID_REMOVE_LIBRARY], FALSE);

        userCommentEdit_ = uiFramework_.CreateD2DTextInput(hwnd_, instance_, ID_USER_COMMENT_EDIT, L"");
        if (!userCommentEdit_) return -1;
        uiFramework_.SetD2DTextInputPalette(userCommentEdit_, Palette(), currentDpi_);
        userCommentLabel_ = uiFramework_.CreateOwnerDrawStatic(hwnd_, instance_, 0, std::wstring(L"User comments"));
        if (!userCommentLabel_) return -1;
        SendMessageW(userCommentLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFontSemibold_ ? uiFontSemibold_ : uiFont_), TRUE);
        if (!CreateMainWindowButton(ID_SAVE_USER_COMMENT, L"Save")) return -1;
        ShowWindow(userCommentEdit_, SW_HIDE);
        ShowWindow(userCommentLabel_, SW_HIDE);
        ShowWindow(controls_[ID_SAVE_USER_COMMENT], SW_HIDE);

        const std::array<std::pair<int, std::wstring_view>, 5> settingsSections{{
            { ID_SETTING_SECTION_THUMBNAILS, L"Thumbnails" },
            { ID_SETTING_SECTION_RAW, L"RAW handling" },
            { ID_SETTING_SECTION_METADATA, L"Metadata handling" },
            { ID_SETTING_SECTION_DIAGNOSTICS, L"Help & Diagnostics" },
            { ID_SETTING_SECTION_LANGUAGE, L"Language" }
        }};
        for (std::size_t i = 0; i < settingsSections.size(); ++i) {
            settingsSectionLabels_[i] = uiFramework_.CreateOwnerDrawStatic(
                hwnd_, instance_, settingsSections[i].first, std::wstring(settingsSections[i].second));
            if (!settingsSectionLabels_[i]) {
                QS_LOG_ERROR(L"UI", L"Creating a settings section label");
                return -1;
            }
            SendMessageW(settingsSectionLabels_[i], WM_SETFONT,
                reinterpret_cast<WPARAM>(uiFontSemibold_ ? uiFontSemibold_ : uiFont_), TRUE);
            ShowWindow(settingsSectionLabels_[i], SW_HIDE);
        }

        languageSelector_ = controls_.contains(ID_SETTING_LANGUAGE_SELECTOR)
            ? controls_.at(ID_SETTING_LANGUAGE_SELECTOR) : nullptr;
        UpdateLanguageSelectorButton();
        if (languageSelector_) ShowWindow(languageSelector_, SW_HIDE);

        quicksift::ui::framework::NativeControlSpec canvasSpec{};
        canvasSpec.className = kCanvasClass;
        canvasSpec.style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS;
        canvasSpec.parent = hwnd_;
        canvasSpec.id = ID_CANVAS;
        canvasSpec.instance = instance_;
        canvasSpec.creationParameter = this;
        canvas_ = uiFramework_.CreateControl(canvasSpec);

        quicksift::ui::framework::NativeControlSpec titleOverlaySpec{};
        titleOverlaySpec.className = L"STATIC";
        titleOverlaySpec.style = WS_CHILD | WS_CLIPSIBLINGS | SS_OWNERDRAW | SS_NOTIFY;
        titleOverlaySpec.parent = hwnd_;
        titleOverlaySpec.id = ID_FULLSCREEN_TITLE_OVERLAY;
        titleOverlaySpec.instance = instance_;
        titleOverlay_ = uiFramework_.CreateControl(titleOverlaySpec);
        if (titleOverlay_ && !SetWindowSubclass(titleOverlay_, TitleOverlaySubclassProc, kTitleOverlaySubclassId,
            reinterpret_cast<DWORD_PTR>(this))) {
            QS_LOG_ERROR(L"UI", L"Could not attach the fullscreen-title subclass");
            DestroyWindow(titleOverlay_);
            titleOverlay_ = nullptr;
        }
        if (titleOverlay_) ShowWindow(titleOverlay_, SW_HIDE);
        else QS_LOG_WARNING(L"UI", L"The optional fullscreen title overlay is unavailable");

        quicksift::ui::framework::NativeControlSpec flyoutBackdropSpec{};
        flyoutBackdropSpec.extendedStyle = WS_EX_NOACTIVATE;
        flyoutBackdropSpec.className = L"STATIC";
        flyoutBackdropSpec.style = WS_CHILD | WS_CLIPSIBLINGS | SS_OWNERDRAW;
        flyoutBackdropSpec.parent = hwnd_;
        flyoutBackdropSpec.id = ID_FLYOUT_BACKDROP;
        flyoutBackdropSpec.instance = instance_;
        flyoutBackdrop_ = uiFramework_.CreateControl(flyoutBackdropSpec);
        if (flyoutBackdrop_) SetWindowSubclass(flyoutBackdrop_, FlyoutBackdropSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

        static_assert(kFlyoutTabIds.size() == std::tuple_size_v<decltype(flyoutTabs_)>);
        for (size_t index = 0; index < flyoutTabs_.size(); ++index) {
            flyoutTabs_[index] = uiFramework_.CreateOwnerDrawStatic(hwnd_, instance_,
                kFlyoutTabIds[index], L"", WS_EX_NOACTIVATE);
            if (flyoutTabs_[index]) ShowWindow(flyoutTabs_[index], SW_HIDE);
        }

        quicksift::ui::framework::NativeControlSpec exifSpec{};
        exifSpec.className = L"STATIC";
        exifSpec.text = Tr(L"Select a photo to inspect its EXIF information.");
        exifSpec.style = WS_CHILD | WS_CLIPSIBLINGS | SS_OWNERDRAW;
        exifSpec.parent = hwnd_;
        exifSpec.id = ID_EXIF_PANEL;
        exifSpec.instance = instance_;
        exif_ = uiFramework_.CreateControl(exifSpec);

        status_ = uiFramework_.CreateOwnerDrawStatic(hwnd_, instance_, ID_STATUS_TEXT,
            Tr(L"Choose a folder to begin."));

        const bool requiredWindowsCreated = filterLabel_ && canvas_ && flyoutBackdrop_ && exif_ && status_ &&
            std::all_of(flyoutTabs_.begin(), flyoutTabs_.end(), [](HWND tab) { return tab != nullptr; });
        if (!requiredWindowsCreated) {
            QS_LOG_ERROR(L"UI", L"One or more required main-window controls could not be created");
            return -1;
        }
        CreateFaceWarningTooltip();
        RefreshFaceDetectorCapability(false);
        CreateFaceLockAvailabilityTooltip();

        RecreateUiFonts();
        ConfigureDwmGlass();
        RefreshLocalizedText();
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateModeButtons();
        UpdateHistoryButtons();
        StartUiTimer(ID_TIMER_AUTOHIDE, 60, L"auto-hide");
        return 0;
    }


    void QuickSiftApplicationImpl::OnDpiChanged(UINT newDpi, const RECT* suggested) {
        currentDpi_ = newDpi ? newDpi : USER_DEFAULT_SCREEN_DPI;
        if (suggested) {
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
        }
        RecreateUiFonts();
        SendMessageW(libraryList_, LB_SETITEMHEIGHT, 0, DipToPx(36));
        if (userCommentEdit_) uiFramework_.SetD2DTextInputPalette(userCommentEdit_, Palette(), currentDpi_);
        if (userCommentLabel_) SendMessageW(userCommentLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFontSemibold_ ? uiFontSemibold_ : uiFont_), TRUE);
        if (languageSelector_) SendMessageW(languageSelector_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFontSemibold_ ? uiFontSemibold_ : uiFont_), TRUE);
        if (renderTarget_) renderTarget_->SetDpi(static_cast<float>(currentDpi_), static_cast<float>(currentDpi_));
        if (swapChain_) {
            RECT canvasRect{};
            GetClientRect(canvas_, &canvasRect);
            ResizeCanvasTarget(std::max(1L, canvasRect.right), std::max(1L, canvasRect.bottom));
        }
        RECT client{};
        GetClientRect(hwnd_, &client);
        LayoutControls(client.right, client.bottom);
        InvalidateRect(hwnd_, nullptr, TRUE);
        InvalidateCanvas();
    }


    void QuickSiftApplicationImpl::DrawBackgroundBlobs(HDC dc, const RECT& client) {
        const ThemePalette palette = Palette();
        const auto safeExtent = [](LONG low, LONG high) {
            const std::int64_t extent = static_cast<std::int64_t>(high) - static_cast<std::int64_t>(low);
            return static_cast<int>(std::clamp<std::int64_t>(extent, 1, std::numeric_limits<int>::max()));
        };
        const int width = safeExtent(client.left, client.right);
        const int height = safeExtent(client.top, client.bottom);
        const int downsample = theme_ == AppTheme::Dark ? 16 : 12;
        const int surfaceWidth = std::max(48, 1 + (width - 1) / downsample);
        const int surfaceHeight = std::max(32, 1 + (height - 1) / downsample);
        const size_t surfaceWidthSize = static_cast<size_t>(surfaceWidth);
        const size_t surfaceHeightSize = static_cast<size_t>(surfaceHeight);
        if (surfaceWidthSize > std::numeric_limits<size_t>::max() / surfaceHeightSize) {
            uiFramework_.PaintSolidSurface(dc, client, palette.windowBottom);
            return;
        }
        const size_t requiredPixels = surfaceWidthSize * surfaceHeightSize;

        if (backgroundPixels_.empty() || backgroundCacheWidth_ != width || backgroundCacheHeight_ != height ||
            backgroundSurfaceWidth_ != surfaceWidth || backgroundSurfaceHeight_ != surfaceHeight ||
            backgroundCacheTheme_ != theme_) {
            backgroundCacheWidth_ = width;
            backgroundCacheHeight_ = height;
            backgroundSurfaceWidth_ = surfaceWidth;
            backgroundSurfaceHeight_ = surfaceHeight;
            backgroundCacheTheme_ = theme_;
            const size_t excessiveCapacity = requiredPixels > std::numeric_limits<size_t>::max() / 4 ?
                std::numeric_limits<size_t>::max() : std::max<size_t>(4096, requiredPixels * 4);
            if (backgroundPixels_.capacity() > excessiveCapacity) {
                std::vector<std::uint32_t>().swap(backgroundPixels_);
            }
            try {
                backgroundPixels_.assign(requiredPixels, 0);
            } catch (...) {
                std::vector<std::uint32_t>().swap(backgroundPixels_);
                uiFramework_.PaintSolidSurface(dc, client, palette.windowBottom);
                QS_LOG_WARNING(L"UI", L"Background surface allocation failed; using a solid fallback");
                return;
            }

            auto channel = [](COLORREF color, int shift) -> float {
                return static_cast<float>((color >> shift) & 0xFF);
            };
            const float maxDimension = static_cast<float>(std::max(surfaceWidth, surfaceHeight));
            const float blobStrength = theme_ == AppTheme::Dark ? 0.115f : 0.13f;
            for (int y = 0; y < surfaceHeight; ++y) {
                const float ny = (static_cast<float>(y) + 0.5f) / surfaceHeight;
                const COLORREF base = BlendColor(palette.windowTop, palette.windowBottom, ny);
                for (int x = 0; x < surfaceWidth; ++x) {
                    float red = channel(base, 0);
                    float green = channel(base, 8);
                    float blue = channel(base, 16);
                    for (const ColorBlob& blob : backgroundBlobs_) {
                        const float cx = blob.x * surfaceWidth;
                        const float cy = blob.y * surfaceHeight;
                        const float radius = std::max(1.0f, blob.radius * maxDimension * (theme_ == AppTheme::Dark ? 2.90f : 2.15f));
                        const float dx = (static_cast<float>(x) + 0.5f - cx) / radius;
                        const float dy = (static_cast<float>(y) + 0.5f - cy) / radius;
                        const float amount = blobStrength * std::exp(-2.15f * (dx * dx + dy * dy));
                        red += (static_cast<float>(GetRValue(blob.color)) - red) * amount;
                        green += (static_cast<float>(GetGValue(blob.color)) - green) * amount;
                        blue += (static_cast<float>(GetBValue(blob.color)) - blue) * amount;
                    }
                    const auto clampByte = [](float value) -> std::uint32_t {
                        return static_cast<std::uint32_t>(std::clamp(std::lround(value), 0l, 255l));
                    };
                    backgroundPixels_[static_cast<size_t>(y) * surfaceWidth + x] =
                        clampByte(blue) | (clampByte(green) << 8) | (clampByte(red) << 16);
                }
            }
        }

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = backgroundSurfaceWidth_;
        info.bmiHeader.biHeight = -backgroundSurfaceHeight_;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        const int oldMode = SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        StretchDIBits(dc, client.left, client.top, width, height,
            0, 0, backgroundSurfaceWidth_, backgroundSurfaceHeight_, backgroundPixels_.data(),
            &info, DIB_RGB_COLORS, SRCCOPY);
        SetStretchBltMode(dc, oldMode);
    }


    bool QuickSiftApplicationImpl::IsCullControlId(int id) const noexcept {
        return ContainsControlId(kCullButtons, id);
    }

    bool QuickSiftApplicationImpl::IsFileControlId(int id) const noexcept {
        return ContainsControlId(kFileButtons, id);
    }

    bool QuickSiftApplicationImpl::IsSettingsControlId(int id) const noexcept {
        return ContainsControlId(kSettingsButtons, id);
    }

    bool QuickSiftApplicationImpl::IsFilterControlId(int id) const noexcept {
        return ContainsControlId(kFilterButtons, id);
    }



    int QuickSiftApplicationImpl::FlyoutTabHeightPx(FlyoutPanel panel) const {
        (void)panel;
        // Sidebar tabs use horizontal labels, so height is a fixed layout metric
        // rather than a rotated text extent.
        return DipToPx(kLeftPaneTabHeightDip);
    }

    int QuickSiftApplicationImpl::LeftPaneWidthPx(int clientWidth) const {
        // Deterministic for a given client size: fraction of width, clamped.
        const int outer = DipToPx(kOuterMarginDip);
        const int minW = DipToPx(kLeftPaneMinWidthDip);
        const int maxW = DipToPx(kLeftPaneMaxWidthDip);
        const int fromFraction = static_cast<int>(std::lround(
            static_cast<double>(clientWidth) * static_cast<double>(kLeftPaneWidthFraction)));
        // Leave room for a usable canvas; never exceed available content width.
        const int available = std::max(0, clientWidth - outer * 2 - DipToPx(160));
        const int upper = std::min(maxW, available);
        const int lower = std::min(minW, upper);
        return std::clamp(fromFraction, lower, upper);
    }

    RECT QuickSiftApplicationImpl::LeftPaneHitStripRect(int clientHeight) const {
        RECT client{};
        if (hwnd_) GetClientRect(hwnd_, &client);
        const int outer = DipToPx(kOuterMarginDip);
        const int contentTop = TitleBarHeightPxForWidth(client.right - client.left) + outer;
        const int bottom = clientHeight - DipToPx(kStatusHeightDip) - outer;
        if (bottom <= contentTop) return {};
        return { 0, contentTop, DipToPx(kLeftPaneHitStripDip), bottom };
    }

    RECT QuickSiftApplicationImpl::LeftPaneRect(int width, int height) const {
        const int outer = DipToPx(kOuterMarginDip);
        const int top = TitleBarHeightPxForWidth(width) + outer;
        const int bottom = height - DipToPx(kStatusHeightDip) - outer;
        if (bottom <= top) return {};
        const int fullWidth = LeftPaneWidthPx(width);
        const int stripWidth = DipToPx(kLeftPaneHitStripDip);
        const float t = quicksift::ui::framework::Smoothstep01(
            std::clamp(leftPaneOpenBlend_, 0.0f, 1.0f));
        // Lerp hit-strip (x=0, narrow) → open pane (x=outer, full width) so open/close
        // is a real width animation instead of a discrete show/hide snap.
        const int left = static_cast<int>(std::lround(static_cast<double>(outer) * static_cast<double>(t)));
        const int paneWidth = stripWidth + static_cast<int>(std::lround(
            static_cast<double>(fullWidth - stripWidth) * static_cast<double>(t)));
        return { left, top, left + std::max(stripWidth, paneWidth), bottom };
    }

    RECT QuickSiftApplicationImpl::LeftPaneOpenRect(int width, int height) const {
        const int outer = DipToPx(kOuterMarginDip);
        const int top = TitleBarHeightPxForWidth(width) + outer;
        const int bottom = height - DipToPx(kStatusHeightDip) - outer;
        if (bottom <= top) return {};
        const int paneWidth = LeftPaneWidthPx(width);
        return { outer, top, outer + paneWidth, bottom };
    }

    RECT QuickSiftApplicationImpl::LeftPaneContentRect(int width, int height) const {
        RECT pane = LeftPaneOpenRect(width, height);
        if (IsRectEmpty(&pane)) return {};
        const int inset = DipToPx(10);
        const int tabColumn = DipToPx(kLeftPaneTabColumnWidthDip);
        const int gap = DipToPx(8);
        pane.left += inset + tabColumn + gap;
        pane.top += inset;
        pane.right -= inset;
        pane.bottom -= inset;
        if (pane.right < pane.left) pane.right = pane.left;
        if (pane.bottom < pane.top) pane.bottom = pane.top;
        return pane;
    }

    RECT QuickSiftApplicationImpl::FlyoutTabRect(FlyoutPanel panel, int clientWidth, int clientHeight) const {
        if (activeFlyout_ == FlyoutPanel::None) return {};
        RECT pane = LeftPaneOpenRect(clientWidth, clientHeight);
        if (IsRectEmpty(&pane)) return {};
        const int inset = DipToPx(10);
        const int gap = DipToPx(kLeftPaneTabGapDip);
        const int tabHeight = FlyoutTabHeightPx(panel);
        const int tabColumn = DipToPx(kLeftPaneTabColumnWidthDip);
        const std::array<FlyoutPanel, 6> panels{{ FlyoutPanel::Folders, FlyoutPanel::File, FlyoutPanel::Cull,
            FlyoutPanel::Filter, FlyoutPanel::Info, FlyoutPanel::Settings }};
        auto it = std::find(panels.begin(), panels.end(), panel);
        if (it == panels.end()) return {};
        const int index = static_cast<int>(std::distance(panels.begin(), it));
        int top = pane.top + inset;
        for (int i = 0; i < index; ++i) top += tabHeight + gap;
        const int bottomLimit = pane.bottom - inset;
        if (top >= bottomLimit) return {};
        return { pane.left + inset, top,
            pane.left + inset + tabColumn, std::min(top + tabHeight, bottomLimit) };
    }


    FlyoutPanel QuickSiftApplicationImpl::FlyoutTabAtPoint(POINT point, int clientWidth, int clientHeight) const {
        if (activeFlyout_ == FlyoutPanel::None) return FlyoutPanel::None;
        for (FlyoutPanel panel : { FlyoutPanel::Folders, FlyoutPanel::File, FlyoutPanel::Cull, FlyoutPanel::Filter,
            FlyoutPanel::Info, FlyoutPanel::Settings }) {
            RECT rect = FlyoutTabRect(panel, clientWidth, clientHeight);
            if (!IsRectEmpty(&rect) && PtInRect(&rect, point)) return panel;
        }
        return FlyoutPanel::None;
    }


    int QuickSiftApplicationImpl::FlyoutButtonWidthPx(HDC measureDc, int id, int fallbackWidthDip) const {
        // Rotate controls are glyph-only buttons. Their localized accessibility
        // captions must never inflate their visual width.
        if (id == ID_ROTATE_LEFT || id == ID_ROTATE_RIGHT) {
            return DipToPx(44);
        }

        int controlWidth = DipToPx(fallbackWidthDip);
        if (!measureDc) return controlWidth;

        wchar_t caption[160]{};
        const auto it = controls_.find(id);
        if (it == controls_.end()) return controlWidth;
        const int length = GetWindowTextW(it->second, caption, static_cast<int>(std::size(caption)));
        SIZE extent{};
        if (length > 0 && GetTextExtentPoint32W(measureDc, caption, length, &extent)) {
            // Match PaintButton horizontal padding + popup chevron reserve.
            const int sidePadding = DipToPx(kButtonTextPaddingDip) * 2;
            const int popupAllowance = ButtonHasPopupMenu(id) ? DipToPx(kButtonPopupChevronDip) : 0;
            controlWidth = std::max(controlWidth,
                static_cast<int>(extent.cx) + sidePadding + popupAllowance);
        }
        return controlWidth;
    }


    RECT QuickSiftApplicationImpl::FlyoutRect(FlyoutPanel panel, int width, int height) const {
        if (panel == FlyoutPanel::None || activeFlyout_ == FlyoutPanel::None) return {};
        // All sections share one left-pane content rectangle so layout is
        // deterministic for a given window size (buttons wrap inside it).
        return LeftPaneContentRect(width, height);
    }

    RECT QuickSiftApplicationImpl::CanvasLayoutRect(int width, int height) const {
        const int outer = DipToPx(kOuterMarginDip);
        const int titleHeight = TitleBarHeightPxForWidth(width);
        const int topMargin = fullscreen_ ? 0 : outer;
        const int contentBottom = height - DipToPx(kStatusHeightDip) - outer;
        // The image canvas always owns the full content area. The left hover pane
        // is a sibling layered above it, so revealing the pane never resizes,
        // reflows, or jumps the current image.
        RECT rect{ outer, titleHeight + topMargin, width - outer, contentBottom };
        if (rect.right < rect.left) rect.right = rect.left;
        if (rect.bottom < rect.top) rect.bottom = rect.top;
        return rect;
    }


    std::wstring QuickSiftApplicationImpl::FlyoutLabel(FlyoutPanel panel) const {
        switch (panel) {
        case FlyoutPanel::File: return Tr(L"FILE");
        case FlyoutPanel::Folders: return Tr(L"LIBRARY");
        case FlyoutPanel::Info: return Tr(L"INFO");
        case FlyoutPanel::Cull: return Tr(L"CULL & RATE");
        case FlyoutPanel::Filter: return Tr(L"FILTER");
        case FlyoutPanel::Settings: return Tr(L"SETTINGS");
        default: return {};
        }
    }


    void QuickSiftApplicationImpl::SetActiveFlyout(FlyoutPanel panel) {
        if (activeFlyout_ == panel) return;
        if (activeFlyout_ == FlyoutPanel::Folders && panel != FlyoutPanel::Folders) {
            treeScrollbarHot_ = false;
            treeScrollDragging_ = false;
            if (GetCapture() == libraryList_) ReleaseCapture();
        }
        const bool switchingSection = activeFlyout_ != FlyoutPanel::None
            && panel != FlyoutPanel::None;
        if (panel != FlyoutPanel::None) lastFlyoutSection_ = panel;
        activeFlyout_ = panel;
        lastFlyoutHover_ = std::chrono::steady_clock::now();
        // Tab switch: hide every prior section quietly. LayoutControls opaque-erases
        // once then shows the new section (single compose — no mid-frame erase thrash).
        if (switchingSection) {
            HideAllLeftPaneSectionContent();
        }
        RECT client{};
        GetClientRect(hwnd_, &client);
        LayoutControls(client.right, client.bottom);
        // Tabs are invalidated inside RedrawActiveFlyoutContents / LayoutControls.
        // Do not parent-erase or UPDATENOW tabs again here (that flashed the canvas).
    }



    bool QuickSiftApplicationImpl::LeftPaneShouldShowContent() const noexcept {
        if (activeFlyout_ == FlyoutPanel::None) return false;
        const float blend = std::clamp(leftPaneOpenBlend_, 0.0f, 1.0f);
        // Hysteresis: once revealed for an open session, keep children shown until the
        // pane is closing past the hide blend. Avoids per-frame ShowWindow flicker.
        if (leftPaneContentVisible_) return blend >= kLeftPaneContentHideBlend;
        return blend >= kLeftPaneContentRevealBlend;
    }

    void QuickSiftApplicationImpl::PlaceLeftPaneBackdrop(const RECT& pane, bool show) {
        if (!flyoutBackdrop_) return;
        if (!show || IsRectEmpty(&pane)) {
            if (IsWindowVisible(flyoutBackdrop_)) {
                SetWindowPos(flyoutBackdrop_, nullptr, 0, 0, 0, 0,
                    SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                    SWP_NOACTIVATE | SWP_NOREDRAW);
            }
            leftPaneBackdropLastW_ = -1;
            leftPaneBackdropLastH_ = -1;
            leftPaneBackdropLastRadiusDip_ = -1;
            return;
        }
        const int paneW = std::max(0, static_cast<int>(pane.right - pane.left));
        const int paneH = std::max(0, static_cast<int>(pane.bottom - pane.top));
        const float ease = quicksift::ui::framework::Smoothstep01(
            std::clamp(leftPaneOpenBlend_, 0.0f, 1.0f));
        // Quantize radius to 2-DIP steps so SetWindowRgn is not called every ease tick.
        const int radiusDip = (std::max(6, static_cast<int>(std::lround(6.0f + 10.0f * ease))) / 2) * 2;
        // Insert above the canvas, never HWND_TOP — content siblings must stay above.
        // Geometry/z-order only; paint is a single invalidate (no ShowWindow thrash).
        HWND insertAfter = canvas_ ? canvas_ : HWND_BOTTOM;
        const bool wasVisible = IsWindowVisible(flyoutBackdrop_) != FALSE;
        UINT posFlags = SWP_NOACTIVATE | SWP_NOREDRAW;
        if (!wasVisible) posFlags |= SWP_SHOWWINDOW;
        SetWindowPos(flyoutBackdrop_, insertAfter, pane.left, pane.top, paneW, paneH, posFlags);
        // Quiet region update (bRedraw=false): never SetWindowRgn TRUE mid-animation.
        if (paneW != leftPaneBackdropLastW_ || paneH != leftPaneBackdropLastH_ ||
            radiusDip != leftPaneBackdropLastRadiusDip_) {
            uiFramework_.ApplyRoundedControlRegion(flyoutBackdrop_, paneW, paneH,
                DipToPx(std::max(6, radiusDip)), /*redraw=*/false);
            leftPaneBackdropLastW_ = paneW;
            leftPaneBackdropLastH_ = paneH;
            leftPaneBackdropLastRadiusDip_ = radiusDip;
        }
        // One invalidate per place; DrawFlyoutBackdrop double-buffers opaque frost.
        // No RDW_UPDATENOW / RDW_ERASE here — caller composes once per frame.
        RedrawWindow(flyoutBackdrop_, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }

    void QuickSiftApplicationImpl::RaiseLeftPaneContentAboveBackdrop() {
        if (!hwnd_ || activeFlyout_ == FlyoutPanel::None || !leftPaneContentVisible_) return;
        auto raise = [&](HWND window) {
            if (!window || !IsWindowVisible(window)) return;
            // Z-order only — never SWP_SHOWWINDOW here. Showing is LayoutControls' job;
            // SWP_SHOWWINDOW would resurrect inactive section children left WS_VISIBLE.
            SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
        };
        for (HWND tab : flyoutTabs_) raise(tab);
        if (activeFlyout_ == FlyoutPanel::File || activeFlyout_ == FlyoutPanel::Cull ||
            activeFlyout_ == FlyoutPanel::Filter || activeFlyout_ == FlyoutPanel::Settings) {
            for (const auto& [id, control] : controls_) {
                const bool belongs = activeFlyout_ == FlyoutPanel::File ? IsFileControlId(id) :
                    activeFlyout_ == FlyoutPanel::Cull ? IsCullControlId(id) :
                    activeFlyout_ == FlyoutPanel::Filter ? IsFilterControlId(id) : IsSettingsControlId(id);
                if (belongs) raise(control);
            }
            if (activeFlyout_ == FlyoutPanel::Settings) {
                for (HWND label : settingsSectionLabels_) raise(label);
                raise(languageSelector_);
            }
        } else if (activeFlyout_ == FlyoutPanel::Folders) {
            raise(libraryList_);
            if (auto it = controls_.find(ID_REMOVE_LIBRARY); it != controls_.end()) raise(it->second);
        } else if (activeFlyout_ == FlyoutPanel::Info) {
            raise(exif_);
            raise(userCommentLabel_);
            raise(userCommentEdit_);
            if (auto it = controls_.find(ID_SAVE_USER_COMMENT); it != controls_.end()) raise(it->second);
        }
    }

    void QuickSiftApplicationImpl::HideAllLeftPaneSectionContent() {
        // Unconditionally hide every flyout section child. All sections share one
        // LeftPaneContentRect; leaving prior sections WS_VISIBLE stacks them.
        // SWP_NOREDRAW: batch hides without mid-frame ShowWindow paint thrash; the
        // caller opaque-erases the backdrop before showing the next section.
        auto hideQuiet = [](HWND window) {
            if (!window || !IsWindowVisible(window)) return;
            SetWindowPos(window, nullptr, 0, 0, 0, 0,
                SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                SWP_NOACTIVATE | SWP_NOREDRAW);
        };
        for (const auto& [id, control] : controls_) {
            if (!control) continue;
            if (IsFileControlId(id) || IsCullControlId(id) || IsFilterControlId(id) ||
                IsSettingsControlId(id) || id == ID_REMOVE_LIBRARY || id == ID_SAVE_USER_COMMENT) {
                hideQuiet(control);
            }
        }
        hideQuiet(filterLabel_);
        hideQuiet(languageSelector_);
        for (HWND label : settingsSectionLabels_) hideQuiet(label);
        hideQuiet(libraryList_);
        hideQuiet(exif_);
        hideQuiet(userCommentEdit_);
        hideQuiet(userCommentLabel_);
    }

    void QuickSiftApplicationImpl::EraseLeftPaneBackdropFully() {
        if (!flyoutBackdrop_ || !IsWindowVisible(flyoutBackdrop_)) return;
        // Force WM_ERASEBKGND (opaque panelStrong) + DrawFlyoutBackdrop now so the
        // shared content rect is clean before the next section's children appear.
        RedrawWindow(flyoutBackdrop_, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }

    void QuickSiftApplicationImpl::LayoutLeftPaneAnimationFrame() {
        if (!hwnd_ || !flyoutBackdrop_) return;
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int width = client.right;
        const int height = client.bottom;
        if (width <= 0 || height <= 0) return;

        RECT pane = LeftPaneRect(width, height);
        // Move/size/quiet-region the backdrop only — no mid-animation ShowWindow,
        // no SetWindowRgn TRUE, no parent erase under children.
        PlaceLeftPaneBackdrop(pane, !IsRectEmpty(&pane));

        const bool showContent = LeftPaneShouldShowContent();
        // Full child layout only when content visibility flips — children are shown
        // once at reveal, then stay shown until hide (moved/raised, not re-shown).
        if (showContent != leftPaneContentVisible_) {
            leftPaneContentVisible_ = showContent;
            LayoutControls(width, height);
            return;
        }
        if (leftPaneContentVisible_) {
            // Z-order only; invalidate once without UPDATENOW (compose with next paint).
            RaiseLeftPaneContentAboveBackdrop();
            for (HWND tab : flyoutTabs_) {
                if (tab && IsWindowVisible(tab))
                    RedrawWindow(tab, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
            }
        }
    }

    void QuickSiftApplicationImpl::UpdateAutoHideFromCursor() {
        if (!hwnd_ || !IsWindowVisible(hwnd_) || IsIconic(hwnd_) || menuOpen_) return;
        POINT point{};
        if (!GetCursorPos(&point)) return;
        ScreenToClient(hwnd_, &point);
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int clientW = client.right;
        const int clientH = client.bottom;

        const RECT hitStrip = LeftPaneHitStripRect(clientH);
        const bool overHitStrip = !IsRectEmpty(&hitStrip) && PtInRect(&hitStrip, point);

        if (activeFlyout_ == FlyoutPanel::None) {
            bool shouldOpen = overHitStrip;
            if (!shouldOpen && leftPaneOpenBlend_ > 0.05f) {
                // While the close animation is still visible, hovering the
                // shrinking pane cancels the close instead of requiring the strip.
                RECT animPane = LeftPaneRect(clientW, clientH);
                InflateRect(&animPane, DipToPx(4), DipToPx(4));
                shouldOpen = !IsRectEmpty(&animPane) && PtInRect(&animPane, point);
            }
            if (shouldOpen) {
                const FlyoutPanel restore = lastFlyoutSection_ == FlyoutPanel::None
                    ? FlyoutPanel::Folders : lastFlyoutSection_;
                SetActiveFlyout(restore);
                lastFlyoutHover_ = std::chrono::steady_clock::now();
            }
            return;
        }

        const FlyoutPanel tab = FlyoutTabAtPoint(point, clientW, clientH);
        if (tab != FlyoutPanel::None) {
            SetActiveFlyout(tab);
            lastFlyoutHover_ = std::chrono::steady_clock::now();
            return;
        }

        RECT pane = LeftPaneRect(clientW, clientH);
        InflateRect(&pane, DipToPx(6), DipToPx(6));
        if (overHitStrip || (!IsRectEmpty(&pane) && PtInRect(&pane, point))) {
            lastFlyoutHover_ = std::chrono::steady_clock::now();
            return;
        }
        if (std::chrono::steady_clock::now() - lastFlyoutHover_
            > std::chrono::milliseconds(kLeftPaneHideGraceMs)) {
            SetActiveFlyout(FlyoutPanel::None);
        }
    }


    FlyoutPanel QuickSiftApplicationImpl::PanelForFlyoutTabWindow(HWND window) const {
        const std::array<FlyoutPanel, 6> panels{{ FlyoutPanel::Folders, FlyoutPanel::File, FlyoutPanel::Cull, FlyoutPanel::Filter,
            FlyoutPanel::Info, FlyoutPanel::Settings }};
        for (size_t i = 0; i < panels.size(); ++i) if (window == flyoutTabs_[i]) return panels[i];
        return FlyoutPanel::None;
    }


    void QuickSiftApplicationImpl::DrawFlyoutTab(DRAWITEMSTRUCT* draw, FlyoutPanel panel) {
        if (!draw || panel == FlyoutPanel::None) return;
        const std::array<FlyoutPanel, 6> panels{{ FlyoutPanel::Folders, FlyoutPanel::File, FlyoutPanel::Cull,
            FlyoutPanel::Filter, FlyoutPanel::Info, FlyoutPanel::Settings }};
        float blend = activeFlyout_ == panel ? 1.0f : 0.0f;
        for (size_t i = 0; i < panels.size(); ++i) {
            if (panels[i] == panel) { blend = flyoutTabActiveBlend_[i]; break; }
        }
        uiFramework_.PaintSidebarTab(draw->hDC, draw->rcItem, FlyoutLabel(panel),
            activeFlyout_ == panel, Palette(), currentDpi_, blend);
    }


    void QuickSiftApplicationImpl::DrawFlyoutBackdrop(DRAWITEMSTRUCT* draw) {
        if (!draw || !draw->hDC) return;
        const RECT& rc = draw->rcItem;
        const int width = std::max(0, static_cast<int>(rc.right - rc.left));
        const int height = std::max(0, static_cast<int>(rc.bottom - rc.top));
        if (width <= 0 || height <= 0) return;

        const float openBlend = std::clamp(leftPaneOpenBlend_, 0.0f, 1.0f);
        const float ease = quicksift::ui::framework::Smoothstep01(openBlend);
        const bool raised = ease > 0.15f;
        const int radius = static_cast<int>(std::lround(6.0f + 10.0f * ease));
        const ThemePalette palette = Palette();

        // Double-buffer: opaque frost fill + grip chrome compose off-screen, then
        // one BitBlt. Prevents mid-paint flicker and any AlphaBlend see-through.
        HDC memDc = CreateCompatibleDC(draw->hDC);
        HBITMAP memBm = memDc ? CreateCompatibleBitmap(draw->hDC, width, height) : nullptr;
        HGDIOBJ oldBm = (memDc && memBm) ? SelectObject(memDc, memBm) : nullptr;
        const bool buffered = memDc && memBm && oldBm;
        HDC target = buffered ? memDc : draw->hDC;
        RECT local = buffered ? RECT{ 0, 0, width, height } : rc;

        // Opaque underlay first (rectangular) so rounded GDI+ AA edges cannot leave
        // holes that show the thumbnail canvas through the pane.
        uiFramework_.PaintSolidSurface(target, local, palette.panelStrong);
        // Frosted panel: opaque panelStrong base + highlight wash (see PaintPanel).
        uiFramework_.PaintPanel(target, local,
            quicksift::ui::framework::PanelPresentation{ raised, raised /* frost wash */, radius },
            palette, currentDpi_);

        // Closed / nearly-closed hit strip: small visible affordance so the rail
        // reads as interactable without relying on hover alone.
        if (ease < 0.35f) {
            const int stripW = std::max(1, width);
            const int accentW = std::max(2, DipToPx(3));
            const int gripH = DipToPx(28);
            const int midY = (local.top + local.bottom) / 2;
            RECT accent{ local.left, local.top,
                local.left + std::min(accentW, stripW), local.bottom };
            const COLORREF accentColor = BlendColor(palette.accent, palette.panelStrong,
                0.35f + 0.40f * (1.0f - ease));
            quicksift::ui::FillSolidRect(target, accent, accentColor);
            const int cx = local.left + stripW / 2;
            const int dot = std::max(2, DipToPx(2));
            for (int i = -1; i <= 1; ++i) {
                RECT bead{ cx - dot / 2, midY + i * DipToPx(7) - gripH / 6,
                    cx - dot / 2 + dot, midY + i * DipToPx(7) - gripH / 6 + dot };
                quicksift::ui::FillSolidRect(target, bead,
                    BlendColor(palette.muted, palette.accent, 0.55f));
            }
        }

        if (buffered) {
            BitBlt(draw->hDC, rc.left, rc.top, width, height, memDc, 0, 0, SRCCOPY);
            SelectObject(memDc, oldBm);
            DeleteObject(memBm);
            DeleteDC(memDc);
        } else {
            if (oldBm) SelectObject(memDc, oldBm);
            if (memBm) DeleteObject(memBm);
            if (memDc) DeleteDC(memDc);
        }
    }


    void QuickSiftApplicationImpl::RedrawActiveFlyoutContents() {
        if (activeFlyout_ == FlyoutPanel::None || !flyoutBackdrop_ ||
            !IsWindowVisible(flyoutBackdrop_)) return;

        // Single opaque compose: backdrop under content, then raise + invalidate
        // active section only. One UPDATENOW at the end — never erase parent under
        // visible children mid-pass, never per-child UPDATENOW thrash.
        HWND insertAfter = canvas_ ? canvas_ : HWND_BOTTOM;
        UINT zFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW;
        if (!IsWindowVisible(flyoutBackdrop_)) zFlags |= SWP_SHOWWINDOW;
        SetWindowPos(flyoutBackdrop_, insertAfter, 0, 0, 0, 0, zFlags);
        // NOERASE: DrawFlyoutBackdrop is fully opaque. LayoutControls already called
        // EraseLeftPaneBackdropFully before showing children when a clear was needed.
        RedrawWindow(flyoutBackdrop_, nullptr, nullptr,
            RDW_INVALIDATE | RDW_NOERASE | RDW_NOCHILDREN);

        auto raiseAndInvalidate = [&](HWND window) {
            if (!window || !IsWindowVisible(window)) return;
            SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
        };

        if (activeFlyout_ == FlyoutPanel::File || activeFlyout_ == FlyoutPanel::Cull ||
            activeFlyout_ == FlyoutPanel::Filter || activeFlyout_ == FlyoutPanel::Settings) {
            for (const auto& [id, control] : controls_) {
                const bool belongs = activeFlyout_ == FlyoutPanel::File ? IsFileControlId(id) :
                    activeFlyout_ == FlyoutPanel::Cull ? IsCullControlId(id) :
                    activeFlyout_ == FlyoutPanel::Filter ? IsFilterControlId(id) : IsSettingsControlId(id);
                if (belongs) raiseAndInvalidate(control);
            }
            if (activeFlyout_ == FlyoutPanel::Settings) {
                for (HWND label : settingsSectionLabels_) raiseAndInvalidate(label);
                raiseAndInvalidate(languageSelector_);
            }
        } else if (activeFlyout_ == FlyoutPanel::Folders) {
            raiseAndInvalidate(libraryList_);
            if (auto it = controls_.find(ID_REMOVE_LIBRARY); it != controls_.end()) raiseAndInvalidate(it->second);
        } else if (activeFlyout_ == FlyoutPanel::Info) {
            raiseAndInvalidate(exif_);
            raiseAndInvalidate(userCommentLabel_);
            raiseAndInvalidate(userCommentEdit_);
            if (auto it = controls_.find(ID_SAVE_USER_COMMENT); it != controls_.end()) raiseAndInvalidate(it->second);
        }
        for (HWND tab : flyoutTabs_) raiseAndInvalidate(tab);

        // Synchronous compose of pane chrome only — never parent RDW_ALLCHILDREN
        // (that would force a D2D canvas repaint and flash under the pane).
        auto updateNow = [](HWND window) {
            if (window && IsWindowVisible(window)) UpdateWindow(window);
        };
        updateNow(flyoutBackdrop_);
        if (activeFlyout_ == FlyoutPanel::File || activeFlyout_ == FlyoutPanel::Cull ||
            activeFlyout_ == FlyoutPanel::Filter || activeFlyout_ == FlyoutPanel::Settings) {
            for (const auto& [id, control] : controls_) {
                const bool belongs = activeFlyout_ == FlyoutPanel::File ? IsFileControlId(id) :
                    activeFlyout_ == FlyoutPanel::Cull ? IsCullControlId(id) :
                    activeFlyout_ == FlyoutPanel::Filter ? IsFilterControlId(id) : IsSettingsControlId(id);
                if (belongs) updateNow(control);
            }
            if (activeFlyout_ == FlyoutPanel::Settings) {
                for (HWND label : settingsSectionLabels_) updateNow(label);
                updateNow(languageSelector_);
            }
        } else if (activeFlyout_ == FlyoutPanel::Folders) {
            updateNow(libraryList_);
            if (auto it = controls_.find(ID_REMOVE_LIBRARY); it != controls_.end()) updateNow(it->second);
        } else if (activeFlyout_ == FlyoutPanel::Info) {
            updateNow(exif_);
            updateNow(userCommentLabel_);
            updateNow(userCommentEdit_);
            if (auto it = controls_.find(ID_SAVE_USER_COMMENT); it != controls_.end()) updateNow(it->second);
        }
        for (HWND tab : flyoutTabs_) updateNow(tab);
    }


    void QuickSiftApplicationImpl::DrawActiveFlyoutPanel(HDC dc, const RECT& client) {
        if (activeFlyout_ == FlyoutPanel::None) return;
        const RECT panel = LeftPaneRect(client.right, client.bottom);
        if (IsRectEmpty(&panel)) return;
        uiFramework_.PaintPanel(dc, panel,
            quicksift::ui::framework::PanelPresentation{ true, true, 16 },
            Palette(), currentDpi_);
    }


    void QuickSiftApplicationImpl::PaintMainWindow() {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd_, &paint);
        RECT client{};
        GetClientRect(hwnd_, &client);
        if (client.right <= 0 || client.bottom <= 0) {
            EndPaint(hwnd_, &paint);
            return;
        }

        const int titleHeight = TitleBarHeightPxForWidth(client.right - client.left);
        if (titleHeight > 0 && paint.rcPaint.bottom <= titleHeight) {
            DrawMacTitleBar(dc, client);
            EndPaint(hwnd_, &paint);
            return;
        }

        const ThemePalette palette = Palette();
        DrawBackgroundBlobs(dc, client);
        if (titleHeight > 0) DrawMacTitleBar(dc, client);

        const int radius = DipToPx(kPanelRadiusDip);
        RECT canvasRect = CanvasLayoutRect(client.right, client.bottom);
        if (theme_ == AppTheme::Light) {
            RECT canvasShadow{ canvasRect.left, canvasRect.top + DipToPx(1), canvasRect.right, canvasRect.bottom + DipToPx(1) };
            const COLORREF shadow = BlendColor(palette.windowBottom, RGB(0, 0, 0), 0.038f);
            uiFramework_.PaintRoundedShadow(dc, canvasShadow, radius, shadow);
        }

        EndPaint(hwnd_, &paint);
    }


    void QuickSiftApplicationImpl::RaiseFullscreenTitleOverlay() {
        if (!fullscreen_ || !fullscreenTitleVisible_ || !titleOverlay_) return;
        SetWindowPos(titleOverlay_, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        for (const ButtonDefinition& spec : kTitleButtons) {
            if (const auto it = controls_.find(spec.id); it != controls_.end() && IsWindowVisible(it->second)) {
                SetWindowPos(it->second, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
        }
        for (int id : { ID_HELP, ID_THEME_TOGGLE }) {
            if (const auto it = controls_.find(id); it != controls_.end() && IsWindowVisible(it->second)) {
                SetWindowPos(it->second, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
        }
    }


    void QuickSiftApplicationImpl::LayoutTitleControls(int width) {
        if (width <= 0) return;
        const int outer = DipToPx(kOuterMarginDip);
        const int gap = DipToPx(kTitleControlGapDip);
        const int titleButtonHeight = DipToPx(kTitleButtonHeightDip);
        const int firstRowHeight = DipToPx(kTitleBarHeightDip);
        const int extraRowHeight = DipToPx(kTitleBarExtraRowHeightDip);
        const bool showTitleControls = !fullscreen_ || fullscreenTitleVisible_;
        const int overlayHeight = NominalTitleBarHeightPxForWidth(width);

        if (!showTitleControls) {
            for (int id : { ID_HELP, ID_THEME_TOGGLE }) {
                if (const auto it = controls_.find(id); it != controls_.end()) {
                    ShowWindow(it->second, SW_HIDE);
                }
            }
            for (const ButtonDefinition& spec : kTitleButtons) {
                if (const auto it = controls_.find(spec.id); it != controls_.end()) {
                    ShowWindow(it->second, SW_HIDE);
                }
            }
            // Hide the surface last so there is never a frame where detached
            // buttons are left hovering over the photograph without their bar.
            if (titleOverlay_) ShowWindow(titleOverlay_, SW_HIDE);
            return;
        }

        // In fullscreen the surface is shown before its controls, then the
        // whole group is raised in one pass. The canvas remains at a fixed
        // rectangle beneath it throughout the operation.
        if (titleOverlay_) {
            if (fullscreen_) {
                SetWindowPos(titleOverlay_, HWND_TOP, 0, 0, width, overlayHeight,
                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
                InvalidateRect(titleOverlay_, nullptr, FALSE);
            } else {
                ShowWindow(titleOverlay_, SW_HIDE);
            }
        }

        const int themeWidth = DipToPx(44);
        const int helpWidth = DipToPx(44);
        const int firstRowY = (firstRowHeight - titleButtonHeight) / 2;
        const int themeX = width - outer - themeWidth;
        const int helpX = themeX - gap - helpWidth;
        const int titlePillRadius = std::max(1, titleButtonHeight / 2);
        if (const auto help = controls_.find(ID_HELP); help != controls_.end()) {
            SetWindowPos(help->second, HWND_TOP, helpX, firstRowY,
                helpWidth, titleButtonHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            uiFramework_.ApplyRoundedControlRegion(help->second, helpWidth, titleButtonHeight,
                titlePillRadius);
        }
        if (const auto theme = controls_.find(ID_THEME_TOGGLE); theme != controls_.end()) {
            SetWindowPos(theme->second, HWND_TOP, themeX, firstRowY,
                themeWidth, titleButtonHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            uiFramework_.ApplyRoundedControlRegion(theme->second, themeWidth, titleButtonHeight,
                titlePillRadius);
        }

        const bool singleOrCompare = reviewState_.Mode() == ViewMode::Single || reviewState_.Mode() == ViewMode::Compare;
        for (const ButtonDefinition& spec : kTitleButtons) {
            const bool show = !((spec.id == ID_SORT || spec.id == ID_THUMB_SIZE) && singleOrCompare) &&
                !((spec.id == ID_ZOOM_MODE || spec.id == ID_ROTATE_LEFT || spec.id == ID_ROTATE_RIGHT) && !singleOrCompare);
            if (const auto it = controls_.find(spec.id); it != controls_.end()) ShowWindow(it->second, show ? SW_SHOW : SW_HIDE);
        }

        const auto controlWidths = MeasuredTitleButtonWidthsPx();
        int row = 0;
        int rowStart = DipToPx(158);
        int rowLimit = std::max(rowStart, helpX - gap);
        int x = rowStart;
        for (size_t index = 0; index < kTitleButtons.size(); ++index) {
            const ButtonDefinition& spec = kTitleButtons[index];
            if ((spec.id == ID_SORT || spec.id == ID_THUMB_SIZE) && singleOrCompare) continue;
            if ((spec.id == ID_ZOOM_MODE || spec.id == ID_ROTATE_LEFT || spec.id == ID_ROTATE_RIGHT) && !singleOrCompare) continue;
            const int controlWidth = controlWidths[index];
            if (x > rowStart && x + controlWidth > rowLimit) {
                ++row;
                rowStart = outer;
                rowLimit = std::max(rowStart, width - outer);
                x = rowStart;
            }
            const int rowTop = row == 0 ? 0 : firstRowHeight + (row - 1) * extraRowHeight;
            const int rowHeight = row == 0 ? firstRowHeight : extraRowHeight;
            const int titleY = rowTop + (rowHeight - titleButtonHeight) / 2;
            if (const auto it = controls_.find(spec.id); it != controls_.end()) {
                SetWindowPos(it->second, HWND_TOP, x, titleY, controlWidth, titleButtonHeight,
                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
                uiFramework_.ApplyRoundedControlRegion(it->second, controlWidth, titleButtonHeight,
                    titlePillRadius);
            }
            x += controlWidth + gap;
        }
    }


    void QuickSiftApplicationImpl::LayoutControls(int width, int height) {
        if (!canvas_ || width <= 0 || height <= 0) return;

        const int outer = DipToPx(kOuterMarginDip);
        LayoutTitleControls(width);

        // Hide EVERY flyout section first. Prior logic left the active section's
        // children WS_VISIBLE when showPaneContent was false (reveal hysteresis),
        // and all sections share one content rect — so File/Settings/Info/Library
        // children stacked on top of each other after tab switches.
        HideAllLeftPaneSectionContent();

        RECT canvasRect = CanvasLayoutRect(width, height);
        const int canvasW = std::max(0, static_cast<int>(canvasRect.right - canvasRect.left));
        const int canvasH = std::max(0, static_cast<int>(canvasRect.bottom - canvasRect.top));
        RECT currentCanvas{};
        GetWindowRect(canvas_, &currentCanvas);
        MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&currentCanvas), 2);
        const bool canvasGeometryChanged = currentCanvas.left != canvasRect.left ||
            currentCanvas.top != canvasRect.top || currentCanvas.right - currentCanvas.left != canvasW ||
            currentCanvas.bottom - currentCanvas.top != canvasH;
        UINT canvasFlags = SWP_SHOWWINDOW | SWP_NOACTIVATE;
        if (!canvasGeometryChanged) canvasFlags |= SWP_NOMOVE | SWP_NOSIZE;
        SetWindowPos(canvas_, HWND_BOTTOM, canvasRect.left, canvasRect.top, canvasW, canvasH, canvasFlags);
        if (canvasGeometryChanged) uiFramework_.ApplyRoundedControlRegion(canvas_, canvasW, canvasH, DipToPx(20));

        // Left-pane vertical tabs live inside the open pane. Content uses hysteresis
        // (LeftPaneShouldShowContent) so open sessions keep children shown until close.
        const std::array<FlyoutPanel, 6> flyoutPanels{{ FlyoutPanel::Folders, FlyoutPanel::File, FlyoutPanel::Cull, FlyoutPanel::Filter,
            FlyoutPanel::Info, FlyoutPanel::Settings }};
        const bool showPaneContent = LeftPaneShouldShowContent();
        leftPaneContentVisible_ = showPaneContent;
        for (size_t i = 0; i < flyoutTabs_.size(); ++i) {
            const FlyoutPanel panel = flyoutPanels[i];
            RECT tab = FlyoutTabRect(panel, width, height);
            const int tabW = std::max(0, static_cast<int>(tab.right - tab.left));
            const int tabH = std::max(0, static_cast<int>(tab.bottom - tab.top));
            if (showPaneContent && !IsRectEmpty(&tab)) {
                // Show once (or move if already shown); never bare ShowWindow mid-layout.
                UINT tabFlags = SWP_NOACTIVATE | SWP_NOREDRAW;
                if (!IsWindowVisible(flyoutTabs_[i])) tabFlags |= SWP_SHOWWINDOW;
                SetWindowPos(flyoutTabs_[i], HWND_TOP, tab.left, tab.top, tabW, tabH, tabFlags);
                uiFramework_.ApplyRoundedControlRegion(flyoutTabs_[i], tabW, tabH,
                    DipToPx(kSidebarTabRadiusDip), /*redraw=*/false);
            } else if (IsWindowVisible(flyoutTabs_[i])) {
                SetWindowPos(flyoutTabs_[i], nullptr, 0, 0, 0, 0,
                    SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                    SWP_NOACTIVATE | SWP_NOREDRAW);
            }
        }


        if (filmstripToggle_) {
            const bool showFilmstripToggle = reviewState_.Mode() == ViewMode::Single;
            const int toggleSize = DipToPx(36);
            const int margin = DipToPx(6);
            const int titleHeight = TitleBarHeightPxForWidth(width);
            const int canvasTop = std::max(titleHeight, static_cast<int>(CanvasLayoutRect(width, height).top));
            const int canvasBottom = CanvasLayoutRect(width, height).bottom;
            const int toggleX = std::max(0, width - toggleSize - margin);
            const int toggleY = std::max(canvasTop + margin,
                canvasTop + (canvasBottom - canvasTop - toggleSize) / 2);
            SetWindowPos(filmstripToggle_, HWND_TOP, toggleX, toggleY, toggleSize, toggleSize,
                (showFilmstripToggle ? SWP_SHOWWINDOW : SWP_HIDEWINDOW) | SWP_NOACTIVATE);
            if (showFilmstripToggle) {
                // Keep glyphs in lock-step with ToggleFilmstrip().
                SetWindowTextW(filmstripToggle_, filmstripVisible_ ? L"‹" : L"›");
                uiFramework_.ApplyRoundedControlRegion(filmstripToggle_, toggleSize, toggleSize, DipToPx(12));
            }
        }

        RECT flyout = showPaneContent ? FlyoutRect(activeFlyout_, width, height) : RECT{};
        // Animated backdrop sits above the canvas and below pane content siblings.
        if (flyoutBackdrop_) {
            PlaceLeftPaneBackdrop(LeftPaneRect(width, height), true);
        }
        // HideAll already ran; one opaque backdrop erase BEFORE children become
        // visible so the shared content rect cannot keep prior section pixels.
        // RedrawActiveFlyoutContents then does a single raise+compose (no second
        // erase thrash beyond the batched UPDATENOW at the end of that helper).
        if (showPaneContent) EraseLeftPaneBackdropFully();
        // Content rect already includes inset; place children flush to it.
        if (showPaneContent && activeFlyout_ == FlyoutPanel::Folders && !IsRectEmpty(&flyout)) {
            const int listW = std::max(0, static_cast<int>(flyout.right - flyout.left));
            const int listH = std::max(0, static_cast<int>(flyout.bottom - flyout.top) - DipToPx(50));
            const int contentRadius = DipToPx(kLeftPaneContentRadiusDip);
            {
                UINT lf = SWP_NOACTIVATE | SWP_NOREDRAW;
                if (!IsWindowVisible(libraryList_)) lf |= SWP_SHOWWINDOW;
                SetWindowPos(libraryList_, HWND_TOP, flyout.left, flyout.top, listW, listH, lf);
                uiFramework_.ApplyRoundedControlRegion(libraryList_, listW, listH, contentRadius,
                    /*redraw=*/false);
            }
            auto it = controls_.find(ID_REMOVE_LIBRARY);
            if (it != controls_.end()) {
                const int bh = DipToPx(32);
                const int bw = std::min(listW, DipToPx(160));
                {
                    UINT bf = SWP_NOACTIVATE | SWP_NOREDRAW;
                    if (!IsWindowVisible(it->second)) bf |= SWP_SHOWWINDOW;
                    SetWindowPos(it->second, HWND_TOP, flyout.left + (listW - bw) / 2,
                        flyout.bottom - bh, bw, bh, bf);
                    // Match File/Cull action buttons: rounded HWND so PaintButton chrome
                    // cannot leave a sharp rectangular underlay behind the pill fill.
                    uiFramework_.ApplyRoundedControlRegion(it->second, bw, bh,
                        DipToPx(kButtonRadiusDip), /*redraw=*/false);
                }
            }
        }
        if (showPaneContent && activeFlyout_ == FlyoutPanel::Info && !IsRectEmpty(&flyout)) {
            const int infoW = std::max(0, static_cast<int>(flyout.right - flyout.left));
            const int saveH = DipToPx(32);
            const int labelH = DipToPx(22);
            const int gap = DipToPx(8);
            // Keep a small pad so the D2D text-input border stroke is not clipped
            // by the pane edge / rounded region.
            const int edgePad = DipToPx(std::max(2, kTextInputBorderInsetDip + 1));
            const int availableH = std::max(0,
                static_cast<int>(flyout.bottom - flyout.top) - edgePad);
            int commentH = DipToPx(110);
            int chromeH = labelH + gap + commentH + gap + saveH;
            int infoH = availableH - chromeH;
            if (infoH < DipToPx(48)) {
                const int deficit = DipToPx(48) - infoH;
                commentH = std::max(DipToPx(56), commentH - deficit);
                chromeH = labelH + gap + commentH + gap + saveH;
                infoH = std::max(0, availableH - chromeH);
            }
            int y = flyout.top;
            {
                UINT f = SWP_NOACTIVATE | SWP_NOREDRAW;
                if (!IsWindowVisible(exif_)) f |= SWP_SHOWWINDOW;
                SetWindowPos(exif_, HWND_TOP, flyout.left, y, infoW, infoH, f);
                uiFramework_.ApplyRoundedControlRegion(exif_, infoW, infoH,
                    DipToPx(kLeftPaneContentRadiusDip), /*redraw=*/false);
            }
            y += infoH + gap;
            {
                UINT f = SWP_NOACTIVATE | SWP_NOREDRAW;
                if (!IsWindowVisible(userCommentLabel_)) f |= SWP_SHOWWINDOW;
                SetWindowPos(userCommentLabel_, HWND_TOP, flyout.left, y, infoW, labelH, f);
                uiFramework_.ApplyRoundedControlRegion(userCommentLabel_, infoW, labelH,
                    DipToPx(kButtonRadiusDip), /*redraw=*/false);
            }
            y += labelH + gap;
            {
                UINT f = SWP_NOACTIVATE | SWP_NOREDRAW;
                if (!IsWindowVisible(userCommentEdit_)) f |= SWP_SHOWWINDOW;
                SetWindowPos(userCommentEdit_, HWND_TOP, flyout.left, y, infoW, commentH, f);
                uiFramework_.ApplyRoundedControlRegion(userCommentEdit_, infoW, commentH,
                    DipToPx(kTextInputRadiusDip), /*redraw=*/false);
            }
            uiFramework_.SetD2DTextInputPalette(userCommentEdit_, Palette(), currentDpi_);
            y += commentH + gap;
            auto save = controls_.find(ID_SAVE_USER_COMMENT);
            if (save != controls_.end()) {
                {
                    UINT f = SWP_NOACTIVATE | SWP_NOREDRAW;
                    if (!IsWindowVisible(save->second)) f |= SWP_SHOWWINDOW;
                    SetWindowPos(save->second, HWND_TOP, flyout.left, y, infoW, saveH, f);
                    uiFramework_.ApplyRoundedControlRegion(save->second, infoW, saveH,
                        DipToPx(kButtonRadiusDip), /*redraw=*/false);
                }
            }
        }

        HDC flyoutMeasureDc = GetDC(hwnd_);
        HFONT flyoutOldFont = nullptr;
        if (flyoutMeasureDc) {
            flyoutOldFont = static_cast<HFONT>(SelectObject(flyoutMeasureDc,
                uiFontSemibold_ ? uiFontSemibold_ :
                static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT))));
        }

        if (showPaneContent && !IsRectEmpty(&flyout) &&
            (activeFlyout_ == FlyoutPanel::File || activeFlyout_ == FlyoutPanel::Cull ||
             activeFlyout_ == FlyoutPanel::Filter || activeFlyout_ == FlyoutPanel::Settings)) {
            const int panelRowHeight = DipToPx(activeFlyout_ == FlyoutPanel::Settings ? 34 : 30);
            const int gapX = DipToPx(kTitleControlGapDip);
            const int gapY = DipToPx(6);
            const int leftEdge = flyout.left;
            const int rightEdge = flyout.right;
            int x = leftEdge;
            int y = flyout.top;
            auto place = [&](const ButtonDefinition& def) {
                auto it = controls_.find(def.id);
                if (it == controls_.end()) return;
                const int widthLimit = std::max(1, rightEdge - leftEdge);
                const int buttonWidth = std::min(widthLimit, FlyoutButtonWidthPx(flyoutMeasureDc, def.id, 96));
                if (x != leftEdge && x + buttonWidth > rightEdge) {
                    x = leftEdge;
                    y += panelRowHeight + gapY;
                }
                {
                    UINT f = SWP_NOACTIVATE | SWP_NOREDRAW;
                    if (!IsWindowVisible(it->second)) f |= SWP_SHOWWINDOW;
                    SetWindowPos(it->second, HWND_TOP, x, y, buttonWidth, panelRowHeight, f);
                    uiFramework_.ApplyRoundedControlRegion(it->second, buttonWidth, panelRowHeight,
                        DipToPx(kButtonRadiusDip), /*redraw=*/false);
                }
                x += buttonWidth + gapX;
            };
            if (activeFlyout_ == FlyoutPanel::File) for (const auto& def : kFileButtons) place(def);
            else if (activeFlyout_ == FlyoutPanel::Cull) for (const auto& def : kCullButtons) place(def);
            else if (activeFlyout_ == FlyoutPanel::Filter) for (const auto& def : kFilterButtons) place(def);
            else {
                const int sectionHeight = DipToPx(22);
                const int sectionControlGap = DipToPx(5);
                const int rowGap = DipToPx(6);
                const int selectorHeight = DipToPx(34);
                const int availableWidth = std::max(1, rightEdge - leftEdge);
                const int columnGap = DipToPx(kTitleControlGapDip);
                const int columnWidth = std::max(1, (availableWidth - columnGap) / 2);

                auto placeSection = [&](std::size_t sectionIndex, std::initializer_list<int> ids) {
                    x = leftEdge;
                    if (sectionIndex < settingsSectionLabels_.size()) {
                        HWND label = settingsSectionLabels_[sectionIndex];
                        if (label) {
                            {
                                UINT f = SWP_NOACTIVATE | SWP_NOREDRAW;
                                if (!IsWindowVisible(label)) f |= SWP_SHOWWINDOW;
                                SetWindowPos(label, HWND_TOP, leftEdge, y, availableWidth, sectionHeight, f);
                                uiFramework_.ApplyRoundedControlRegion(label, availableWidth, sectionHeight,
                                    DipToPx(kButtonRadiusDip), /*redraw=*/false);
                            }
                        }
                    }
                    y += sectionHeight + sectionControlGap;
                    int used = 0;
                    int rowY = y;
                    for (int id : ids) {
                        auto it = controls_.find(id);
                        if (it == controls_.end()) continue;
                        const int measured = FlyoutButtonWidthPx(flyoutMeasureDc, id, 110);
                        const int buttonWidth = std::min(columnWidth, measured);
                        if (used != 0 && x + buttonWidth > rightEdge) {
                            x = leftEdge;
                            rowY += panelRowHeight + rowGap;
                        }
                        {
                            UINT f = SWP_NOACTIVATE | SWP_NOREDRAW;
                            if (!IsWindowVisible(it->second)) f |= SWP_SHOWWINDOW;
                            SetWindowPos(it->second, HWND_TOP, x, rowY, buttonWidth, panelRowHeight, f);
                            uiFramework_.ApplyRoundedControlRegion(it->second, buttonWidth, panelRowHeight,
                                DipToPx(kButtonRadiusDip), /*redraw=*/false);
                        }
                        x += buttonWidth + columnGap;
                        used++;
                    }
                    y = rowY + panelRowHeight + sectionControlGap;
                };

                placeSection(0, { ID_SETTING_BADGE_STARS, ID_SETTING_BADGE_COLOR,
                    ID_SETTING_BADGE_PICK, ID_SETTING_BADGE_PAIR });
                placeSection(1, { ID_SETTING_COPY_MOVE_RAW_WITH_JPG, ID_SETTING_RAW_JPEG_PREVIEWS_ONLY });
                placeSection(2, { ID_SETTING_SAFE_JPEG, ID_SETTING_METADATA_RAW,
                    ID_SETTING_METADATA_LOSSY, ID_SETTING_METADATA_TIFF });
                placeSection(3, { ID_SETTING_PERFORMANCE_HUD, ID_SETTING_DIAGNOSTIC_LOG,
                    ID_SETTING_CLEAR_MEMORY_CACHE, ID_SETTING_CLEAR_DISK_CACHE,
                    ID_SETTING_HELP, ID_SETTING_ABOUT });

                if (settingsSectionLabels_.size() > 4 && settingsSectionLabels_[4]) {
                    {
                        UINT f = SWP_NOACTIVATE | SWP_NOREDRAW;
                        if (!IsWindowVisible(settingsSectionLabels_[4])) f |= SWP_SHOWWINDOW;
                        SetWindowPos(settingsSectionLabels_[4], HWND_TOP, leftEdge, y, availableWidth, sectionHeight, f);
                        uiFramework_.ApplyRoundedControlRegion(settingsSectionLabels_[4], availableWidth, sectionHeight,
                            DipToPx(kButtonRadiusDip), /*redraw=*/false);
                    }
                }
                y += sectionHeight + sectionControlGap;
                if (languageSelector_) {
                    const int selectorWidth = std::min(DipToPx(210), availableWidth);
                    {
                        UINT f = SWP_NOACTIVATE | SWP_NOREDRAW;
                        if (!IsWindowVisible(languageSelector_)) f |= SWP_SHOWWINDOW;
                        SetWindowPos(languageSelector_, HWND_TOP, leftEdge, y, selectorWidth, selectorHeight, f);
                        UpdateLanguageSelectorButton();
                        uiFramework_.ApplyRoundedControlRegion(languageSelector_, selectorWidth, selectorHeight,
                            DipToPx(kButtonRadiusDip), /*redraw=*/false);
                    }
                }
            }
        }
        auto closeFlyoutLayout = [&] {
            if (flyoutMeasureDc) {
                if (flyoutOldFont) SelectObject(flyoutMeasureDc, flyoutOldFont);
                ReleaseDC(hwnd_, flyoutMeasureDc);
            }
        };
        closeFlyoutLayout();

        if (showPaneContent) RedrawActiveFlyoutContents();
        else if (flyoutBackdrop_ && IsWindowVisible(flyoutBackdrop_)) {
            // Closing / content hidden: one opaque backdrop paint (no parent erase).
            RedrawWindow(flyoutBackdrop_, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_NOCHILDREN | RDW_UPDATENOW);
        }

        if (filmstripToggle_ && reviewState_.Mode() == ViewMode::Single) {
            const int toggleSize = DipToPx(36);
            const int margin = DipToPx(6);
            const RECT canvas = CanvasLayoutRect(width, height);
            const int canvasTop = static_cast<int>(canvas.top);
            const int canvasBottom = static_cast<int>(canvas.bottom);
            const int toggleX = std::max(0, width - toggleSize - margin);
            const int toggleY = std::max(canvasTop + margin,
                canvasTop + (canvasBottom - canvasTop - toggleSize) / 2);
            SetWindowPos(filmstripToggle_, HWND_TOP, toggleX, toggleY, toggleSize, toggleSize,
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
            SetWindowTextW(filmstripToggle_, filmstripVisible_ ? L"‹" : L"›");
            uiFramework_.ApplyRoundedControlRegion(filmstripToggle_, toggleSize, toggleSize, DipToPx(12));
        }

        const int statusX = outer;
        const int statusY = height - DipToPx(kStatusHeightDip) + DipToPx(2);
        const int statusW = std::max(0, width - outer * 2);
        const int statusH = DipToPx(statusProgressText_.empty() ? 34 : 44);
        MoveWindow(status_, statusX, statusY, statusW, statusH, TRUE);
        uiFramework_.ApplyRoundedControlRegion(status_, statusW, statusH, DipToPx(17));
        RaiseFullscreenTitleOverlay();
    }


    bool QuickSiftApplicationImpl::IsModeButtonActive(int id) const {
        return (id == ID_MODE_THUMBS && reviewState_.Mode() == ViewMode::Thumbnails) ||
            (id == ID_MODE_SINGLE && reviewState_.Mode() == ViewMode::Single) ||
            (id == ID_MODE_COMPARE && reviewState_.Mode() == ViewMode::Compare);
    }


    void QuickSiftApplicationImpl::RefreshUserComment() {
        if (!userCommentEdit_) return;
        const std::wstring path = ActiveInfoPath();
        const std::wstring comment = path.empty() ? L"" : persistentCache_.GetPhotoComment(fs::path(path));
        SetWindowTextW(userCommentEdit_, comment.c_str());
        EnableWindow(userCommentEdit_, !path.empty());
        auto save = controls_.find(ID_SAVE_USER_COMMENT);
        if (save != controls_.end()) EnableWindow(save->second, !path.empty());
    }

    void QuickSiftApplicationImpl::SaveUserComment() {
        const std::wstring path = ActiveInfoPath();
        if (path.empty() || !userCommentEdit_) return;
        const int length = GetWindowTextLengthW(userCommentEdit_);
        std::wstring comment(static_cast<size_t>(std::max(0, length)), L'\0');
        if (length > 0) GetWindowTextW(userCommentEdit_, comment.data(), length + 1);
        persistentCache_.SetPhotoComment(fs::path(path), std::move(comment));
        ShowToast(L"User comment saved.");
    }

    std::wstring QuickSiftApplicationImpl::ActiveInfoPath() const {
        if (!reviewState_.ActivePath().empty()) return reviewState_.ActivePath();
        if (selection_.Size() == 1) return *selection_.First();
        return {};
    }


    void QuickSiftApplicationImpl::RequestExifForActive() {
        if (!exif_) return;
        RefreshUserComment();
        const std::wstring path = ActiveInfoPath();
        if (path.empty()) {
            SetWindowTextW(exif_, Tr(L"Select a photo to inspect its EXIF information.").c_str());
            InvalidateRect(exif_, nullptr, FALSE);
            return;
        }
        const auto cached = exifCache_.find(path);
        if (cached != exifCache_.end()) {
            SetWindowTextW(exif_, Tr(cached->second).c_str());
        } else {
            const fs::path file(path);
            const std::wstring loading = file.filename().wstring() + L"\r\n\r\n" + Tr(L"Reading EXIF metadata…");
            SetWindowTextW(exif_, loading.c_str());
            worker_.EnqueueExif(file, generation_);
        }
        InvalidateRect(exif_, nullptr, FALSE);
    }


    int QuickSiftApplicationImpl::ActiveRatingForUi() const {
        if (reviewState_.Mode() == ViewMode::Thumbnails && selection_.Size() > 1) {
            std::optional<MetadataFieldValue> common;
            for (size_t index = 0; index < catalog_.VisibleCount(); ++index) {
                const PhotoItem& photo = VisiblePhoto(index);
                if (!selection_.Contains(photo.path.wstring())) continue;
                const MetadataFieldValue field{ photo.ratingKnowledge, photo.rating, photo.rawRating };
                if (!common) common = field;
                else if (!MetadataFieldEquivalent(*common, field)) return -2;
            }
            return common && common->IsKnown() ? common->value : -1;
        }
        const PhotoItem* photo = PhotoForPath(EditableViewPath());
        return photo && photo->ratingKnowledge == MetadataKnowledge::Known ? photo->rating : -1;
    }


    int QuickSiftApplicationImpl::ActivePickStateForUi() const {
        if (reviewState_.Mode() == ViewMode::Thumbnails && selection_.Size() > 1) {
            std::optional<MetadataFieldValue> common;
            for (size_t index = 0; index < catalog_.VisibleCount(); ++index) {
                const PhotoItem& photo = VisiblePhoto(index);
                if (!selection_.Contains(photo.path.wstring())) continue;
                const MetadataFieldValue field{ photo.pickStateKnowledge, photo.pickState, photo.rawPickState };
                if (!common) common = field;
                else if (!MetadataFieldEquivalent(*common, field)) return 2;
            }
            return common && common->IsKnown() ? common->value : 3;
        }
        const PhotoItem* photo = PhotoForPath(EditableViewPath());
        return photo && photo->pickStateKnowledge == MetadataKnowledge::Known ? photo->pickState : 3;
    }


    LRESULT CALLBACK QuickSiftApplicationImpl::FlyoutBackdropSubclassProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR referenceData) {
        auto* self = reinterpret_cast<QuickSiftApplicationImpl*>(referenceData);
        if (message == WM_ERASEBKGND) {
            // Opaque fill so RDW_ERASE / tab-switch clears cannot leave holes that
            // show the thumbnail canvas through the pane.
            if (self) {
                HDC dc = reinterpret_cast<HDC>(wParam);
                RECT rect{};
                GetClientRect(hwnd, &rect);
                self->uiFramework_.PaintSolidSurface(dc, rect, self->Palette().panelStrong);
            }
            return 1;
        }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, FlyoutBackdropSubclassProc, subclassId);
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    LRESULT CALLBACK QuickSiftApplicationImpl::LibraryListSubclassProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR referenceData) {
        auto* self = reinterpret_cast<QuickSiftApplicationImpl*>(referenceData);
        if (!self) return DefSubclassProc(hwnd, message, wParam, lParam);
        switch (message) {
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            RECT rect{};
            GetClientRect(hwnd, &rect);
            // Opaque rounded panel base; ApplyRoundedControlRegion clips the HWND.
            self->uiFramework_.PaintPanel(dc, rect,
                quicksift::ui::framework::PanelPresentation{
                    false, false, kLeftPaneContentRadiusDip },
                self->Palette(), self->currentDpi_);
            return 1;
        }
        case WM_PAINT: {
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            HDC dc = GetDC(hwnd);
            if (dc) {
                RECT rect{};
                GetClientRect(hwnd, &rect);
                const bool empty = SendMessageW(hwnd, LB_GETCOUNT, 0, 0) == 0;
                if (empty) {
                    // Refill so empty-state text sits on a clean panel (DefSubclassProc
                    // may have left a blank client without our frosted fill).
                    self->uiFramework_.PaintPanel(dc, rect,
                        quicksift::ui::framework::PanelPresentation{
                            false, false, kLeftPaneContentRadiusDip },
                        self->Palette(), self->currentDpi_);
                    HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
                    self->uiFramework_.PaintCenteredMutedText(dc, rect, L"No item in Library",
                        self->Palette(), self->currentDpi_, font);
                } else {
                    // Stroke only — do not cover owner-drawn list items.
                    self->uiFramework_.PaintControlBorder(dc, rect, kLeftPaneContentRadiusDip,
                        self->Palette(), self->currentDpi_);
                }
                ReleaseDC(hwnd, dc);
            }
            return result;
        }
        case WM_MOUSEMOVE: {
            const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(point.x, point.y));
            const int index = LOWORD(hit);
            const bool outside = HIWORD(hit) != 0;
            std::wstring tip;
            if (!outside && index >= 0 && index < static_cast<int>(self->libraryPaths_.size())) {
                tip = self->libraryPaths_[index];
            }
            if (tip != self->libraryTooltipText_) {
                self->libraryTooltipText_ = std::move(tip);
                if (self->libraryTooltip_) {
                    TOOLINFOW tool{ sizeof(TOOLINFOW) };
                    tool.uFlags = TTF_SUBCLASS;
                    tool.hwnd = hwnd;
                    tool.uId = 1;
                    tool.lpszText = self->libraryTooltipText_.empty()
                        ? const_cast<wchar_t*>(L"") : self->libraryTooltipText_.data();
                    SendMessageW(self->libraryTooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
                }
            }
            break;
        }
        case WM_VSCROLL:
        case WM_HSCROLL:
            if (LOWORD(wParam) == SB_THUMBTRACK) {
                SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
                const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
                SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
                InvalidateRect(hwnd, nullptr, FALSE);
                return result;
            }
            break;
        case WM_NCDESTROY:
            if (self->libraryTooltip_) {
                DestroyWindow(self->libraryTooltip_);
                self->libraryTooltip_ = nullptr;
            }
            self->libraryTooltipText_.clear();
            RemoveWindowSubclass(hwnd, LibraryListSubclassProc, subclassId);
            break;
        default: break;
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }


    LRESULT QuickSiftApplicationImpl::OnDrawItem(DRAWITEMSTRUCT* draw) {
        if (!draw) return FALSE;
        if (draw->CtlID == ID_LIBRARY_LIST) {
            const int index = static_cast<int>(draw->itemID);
            if (index >= 0 && index < static_cast<int>(libraryPaths_.size())) {
                const std::wstring& path = libraryPaths_[index];
                const bool isCurrent = (_wcsicmp(path.c_str(), currentFolder_.wstring().c_str()) == 0);
                MenuItem item{};
                item.text = fs::path(path).filename().wstring();
                item.command = ID_LIBRARY_LIST;
                uiFramework_.PaintMenuItem(draw->hDC, draw->rcItem, item, isCurrent,
                    Palette(), static_cast<UINT>(std::lround(CanvasDpi())), uiFont_);
            }
            return TRUE;
        }
        if (draw->hwndItem == userCommentLabel_) {
            uiFramework_.PaintSectionTitle(draw->hDC, draw->rcItem, L"USER COMMENTS", Palette(),
                currentDpi_, uiFontSemibold_);
            return TRUE;
        }
        for (std::size_t i = 0; i < settingsSectionLabels_.size(); ++i) {
            if (draw->hwndItem == settingsSectionLabels_[i]) {
                const std::wstring title =
                    i == 0 ? L"THUMBNAILS" :
                    i == 1 ? L"RAW HANDLING" :
                    i == 2 ? L"METADATA HANDLING" :
                    i == 3 ? L"HELP & DIAGNOSTICS" : L"LANGUAGE";
                uiFramework_.PaintSectionTitle(draw->hDC, draw->rcItem, title, Palette(),
                    currentDpi_, uiFontSemibold_);
                return TRUE;
            }
        }
        if (draw->hwndItem == titleOverlay_) {
            DrawMacTitleBar(draw->hDC, draw->rcItem);
            return TRUE;
        }
        const FlyoutPanel flyoutTab = PanelForFlyoutTabWindow(draw->hwndItem);
        if (flyoutTab != FlyoutPanel::None) {
            DrawFlyoutTab(draw, flyoutTab);
            return TRUE;
        }
        if (draw->hwndItem == flyoutBackdrop_) {
            DrawFlyoutBackdrop(draw);
            return TRUE;
        }
        if (draw->hwndItem == exif_) {
            DrawExifPanel(draw);
            return TRUE;
        }
        if (draw->hwndItem == status_) {
            DrawGlassStatus(draw);
            return TRUE;
        }
        if (draw->CtlID == ID_FILTER_LABEL || draw->hwndItem == filterLabel_) {
            DrawFilterLabel(draw);
            return TRUE;
        }
        if (draw->CtlType == ODT_BUTTON) {
            DrawGlassButton(draw);
            return TRUE;
        }
        return FALSE;
    }


    bool QuickSiftApplicationImpl::ButtonHasPopupMenu(int id) const {
        switch (id) {
        case ID_FORMATS:
        case ID_DATE_FILTER:
        case ID_SORT:
        case ID_THUMB_SIZE:
        case ID_STAR_FILTER:
        case ID_PICK_FILTER:
        case ID_COLOR_LABEL:
        case ID_ZOOM_MODE:
        case ID_RATING_MENU:
        case ID_PICK_STATE_MENU:
        case ID_SETTING_LANGUAGE_SELECTOR:
            return true;
        default:
            return false;
        }
    }


    void QuickSiftApplicationImpl::DrawGlassButton(DRAWITEMSTRUCT* draw) {
        if (!draw) return;
        const int id = GetDlgCtrlID(draw->hwndItem);
        const int rating = ActiveRatingForUi();
        const int pickState = ActivePickStateForUi();
        const bool selected = IsModeButtonActive(id) ||
            (id == ID_RATING_MENU && rating >= 1 && rating <= 5) ||
            (id == ID_PICK_STATE_MENU && (pickState == 1 || pickState == -1 || pickState == 0)) ||
            (id == ID_SETTING_BADGE_STARS && badgeStars_) ||
            (id == ID_SETTING_BADGE_COLOR && badgeColor_) ||
            (id == ID_SETTING_BADGE_PICK && badgePick_) ||
            (id == ID_SETTING_BADGE_PAIR && badgePair_) ||
            (id == ID_SETTING_COPY_MOVE_RAW_WITH_JPG && copyMoveRawWithJpg_) ||
            (id == ID_SETTING_RAW_JPEG_PREVIEWS_ONLY && loadOnlyRawJpegPreviews_) ||
            (id == ID_SETTING_PERFORMANCE_HUD && performanceHudVisible_) ||
            (id == ID_SETTING_THEME && theme_ == AppTheme::Dark) ||
            (id == ID_SETTING_LANGUAGE_ENGLISH && localizer_.CurrentLanguage() == quicksift::Language::English) ||
            (id == ID_SETTING_LANGUAGE_VIETNAMESE && localizer_.CurrentLanguage() == quicksift::Language::Vietnamese) ||
            (id == ID_SETTING_SAFE_JPEG && safeJpegMetadataWrites_) ||
            (id == ID_SETTING_DIRECT_JPEG && (metadataDirectMask_ & MetadataDirectJpeg) != 0) ||
            (id == ID_SETTING_DIRECT_PNG && (metadataDirectMask_ & MetadataDirectPng) != 0) ||
            (id == ID_SETTING_DIRECT_TIFF && (metadataDirectMask_ & MetadataDirectTiff) != 0) ||
            (id == ID_SETTING_DIRECT_RAW && (metadataDirectMask_ & MetadataDirectRaw) != 0) ||
            (id == ID_COLOR_LABEL && ActiveColorLabelForUi() > 0) ||
            (id == ID_STAR_FILTER && ratingFilter_ != RatingFilter::All) ||
            (id == ID_PICK_FILTER && pickFilter_ != PickFilter::All) ||
            (id == ID_LABEL_FILTER && colorLabelFilter_ != ColorLabelFilter::All) ||
            (id == ID_DATE_FILTER && dateFilter_ != DateFilter::Any) ||
            (id == ID_FORMATS && std::any_of(formats_.begin(), formats_.end(),
                [](const auto& item) { return !item.second; })) ||
            (id == ID_SYNC_VIEW && reviewState_.SyncCompareView()) ||
            (id == ID_FACE_LOCK && reviewState_.FaceLockEnabled()) ||
            (id == ID_FULLSCREEN && fullscreen_);

        wchar_t text[128]{};
        GetWindowTextW(draw->hwndItem, text, static_cast<int>(std::size(text)));
        ButtonPresentation presentation{};
        presentation.text = text;
        presentation.role = (id == ID_THEME_TOGGLE || id == ID_HELP) ? ButtonRole::TitleBar :
            (id == ID_DELETE ? ButtonRole::Danger :
            (IsBrowseControlId(id) ? ButtonRole::TitleBar : ButtonRole::Standard));
        if (id == ID_THEME_TOGGLE) presentation.glyph = ButtonGlyph::Theme;
        else if (id == ID_HELP) presentation.glyph = ButtonGlyph::Help;
        else if (id == ID_PREVIOUS) presentation.glyph = ButtonGlyph::LeftArrow;
        else if (id == ID_NEXT) presentation.glyph = ButtonGlyph::RightArrow;
        else if (id == ID_ROTATE_LEFT) presentation.glyph = ButtonGlyph::RotateLeft;
        else if (id == ID_ROTATE_RIGHT) presentation.glyph = ButtonGlyph::RotateRight;
        presentation.state.hot = uiFramework_.IsButtonHot(draw->hwndItem);
        presentation.state.pressed = (draw->itemState & ODS_SELECTED) != 0;
        presentation.state.disabled = (draw->itemState & ODS_DISABLED) != 0;
        presentation.state.focused = (draw->itemState & ODS_FOCUS) != 0;
        presentation.state.selected = selected;
        presentation.state.popup = ButtonHasPopupMenu(id);
        presentation.hoverBlend = uiFramework_.ButtonHoverBlend(draw->hwndItem);
        presentation.pressBlend = presentation.state.pressed ? 1.0f : 0.0f;
        uiFramework_.PaintButton(draw->hDC, draw->rcItem, presentation, Palette(),
            currentDpi_, uiFont_, uiFontSemibold_);
    }


    void QuickSiftApplicationImpl::DrawFilterLabel(DRAWITEMSTRUCT* draw) {
        if (!draw) return;
        quicksift::ui::framework::LabelPresentation presentation{};
        presentation.text = Tr(L"Filter by:");
        presentation.semibold = true;
        presentation.format = DT_LEFT | DT_VCENTER | DT_SINGLELINE;
        uiFramework_.PaintLabel(draw->hDC, draw->rcItem, presentation, Palette(),
            currentDpi_, uiFont_, uiFontSemibold_);
    }


    void QuickSiftApplicationImpl::DrawExifPanel(DRAWITEMSTRUCT* draw) {
        if (!draw) return;
        wchar_t body[4096]{};
        GetWindowTextW(exif_, body, static_cast<int>(std::size(body)));
        quicksift::ui::framework::InfoPanelPresentation presentation{};
        presentation.title = Tr(L"PHOTO DETAILS");
        presentation.body = body;
        uiFramework_.PaintInfoPanel(draw->hDC, draw->rcItem, presentation, Palette(),
            currentDpi_, uiFont_, uiFontSemibold_);
    }


    void QuickSiftApplicationImpl::DrawGlassStatus(DRAWITEMSTRUCT* draw) {
        if (!draw) return;
        wchar_t left[1024]{};
        GetWindowTextW(status_, left, static_cast<int>(std::size(left)));
        quicksift::ui::framework::StatusPresentation presentation{};
        presentation.leftText = left;
        presentation.rightText = statusRightText_;
        presentation.secondaryText = statusProgressText_;
        uiFramework_.PaintStatus(draw->hDC, draw->rcItem, presentation, Palette(),
            currentDpi_, uiFont_, uiFontSemibold_);
    }



} // namespace quicksift::app
