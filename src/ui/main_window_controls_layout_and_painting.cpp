// OWNER: Compiled QuickSiftApplication feature module.
#include "app/quicksift_application_internal.h"
#include "core/pane_scroll_policy.h"
#include "ui/ui_layout_constants.h"

namespace quicksift::app {

namespace {
// Milliseconds since `start` on the steady clock ([Pane] timing).
[[nodiscard]] double PaneMsSince(std::chrono::steady_clock::time_point start) noexcept {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
} // namespace

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
        // No WS_VSCROLL: the classic (unthemed) native bar painted a light strip along
        // the right edge of the dark panel. The framework overlay scrollbar replaces it
        // (PaintLibraryList); wheel/drag scrolling is handled in LibraryListSubclassProc.
        // LBS_NOINTEGRALHEIGHT: the list keeps exactly the height layout gives it
        // (integral-height shrinking left an unpainted band above the action button).
        libraryListSpec.style = WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
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
        RestyleTooltips();
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
        // Floating thumbnail search (framework text input inside canvas-painted chrome).
        if (!CreateSearchField()) return -1;
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
        MarkTitleLayoutDirty();
        // Fonts and DPI-scaled resources FIRST: SetWindowPos below sends WM_SIZE,
        // whose layout pass used to run with the previous DPI's fonts/item heights
        // (one visibly wrong frame, then a second full layout).
        RecreateUiFonts();
        SendMessageW(libraryList_, LB_SETITEMHEIGHT, 0, DipToPx(36));
        if (userCommentEdit_) uiFramework_.SetD2DTextInputPalette(userCommentEdit_, Palette(), currentDpi_);
        UpdateSearchFieldTheme(); // DPI: palette scale + pixel geometry
        if (userCommentLabel_) SendMessageW(userCommentLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFontSemibold_ ? uiFontSemibold_ : uiFont_), FALSE);
        if (languageSelector_) SendMessageW(languageSelector_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFontSemibold_ ? uiFontSemibold_ : uiFont_), FALSE);
        if (renderTarget_) renderTarget_->SetDpi(static_cast<float>(currentDpi_), static_cast<float>(currentDpi_));
        badgeTextWidthCache_.clear();
        if (suggested) {
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
        }
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
        if (palette.highContrast) {
            // High contrast: plain system window colour, no decorative colour blobs.
            uiFramework_.PaintSolidSurface(dc, client, palette.windowBottom);
            return;
        }
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
        const float t = quicksift::ui::animation::Ease(quicksift::ui::animation::Curve::Smoothstep,
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


    void QuickSiftApplicationImpl::LogPaneTiming(std::wstring_view event, FlyoutPanel panel, bool first,
        double totalMs, const quicksift::core::pane_timing::Recorder* segments) {
        namespace timing = quicksift::core::pane_timing;
        const auto level = timing::Decide(first, totalMs) == timing::LogLevel::Info
            ? quicksift::diagnostics::Level::Info : quicksift::diagnostics::Level::Trace;
        if (segments) {
            QS_LOG_EVENT(level, L"Pane", std::wstring(event),
                {L"section", std::to_wstring(static_cast<int>(panel))},
                {L"first", first ? L"1" : L"0"},
                {L"ms", timing::Recorder::FormatMs(totalMs)},
                {L"slowest", std::wstring(segments->Slowest())},
                {L"segments", segments->Format()});
        } else {
            QS_LOG_EVENT(level, L"Pane", std::wstring(event),
                {L"section", std::to_wstring(static_cast<int>(panel))},
                {L"first", first ? L"1" : L"0"},
                {L"ms", timing::Recorder::FormatMs(totalMs)});
        }
    }

    void QuickSiftApplicationImpl::SetActiveFlyout(FlyoutPanel panel) {
        if (activeFlyout_ == panel) return;
        if (activeFlyout_ == FlyoutPanel::Folders && panel != FlyoutPanel::Folders) {
            treeScrollbarHot_ = false;
            treeScrollDragging_ = false;
            if (GetCapture() == libraryList_) ReleaseCapture();
        }
        const bool opening = activeFlyout_ == FlyoutPanel::None && panel != FlyoutPanel::None;
        if (panel != FlyoutPanel::None) lastFlyoutSection_ = panel;
        activeFlyout_ = panel;
        lastFlyoutHover_ = std::chrono::steady_clock::now();
        // Opening: refresh the frost from the canvas under the pane (next canvas frame).
        if (opening) MarkLeftPaneFrostDirty();
        const auto layoutStart = std::chrono::steady_clock::now();
        if (opening) {
            paneOpenRequestedAt_ = layoutStart;
            paneFirstPaintPending_ = true;
        }
        // Tab switch: LayoutControls hides ONLY the previous section's children
        // (HideLeftPaneSectionContentExcept) and moves/shows the new one with
        // SWP_NOREDRAW, then composes the pane once (buffered surfaces, no erase).
        RECT client{};
        GetClientRect(hwnd_, &client);
        LayoutControls(client.right, client.bottom);
        if (panel != FlyoutPanel::None) {
            const double layoutMs = PaneMsSince(layoutStart);
            // Opening only starts the width animation (content is revealed later);
            // a tab switch with the pane open lays out and composes the section now.
            const bool revealedNow = leftPaneContentVisible_;
            const auto index = static_cast<std::size_t>(panel);
            const bool firstReveal = revealedNow && index < paneSectionRevealed_.size() && !paneSectionRevealed_[index];
            if (firstReveal) paneSectionRevealed_[index] = true;
            LogPaneTiming(opening ? L"pane_open" : L"pane_section_switch", panel,
                opening ? !paneEverOpened_ : firstReveal, layoutMs, revealedNow ? &paneLayoutTiming_ : nullptr);
            if (opening) paneEverOpened_ = true;
        }
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

    int QuickSiftApplicationImpl::LeftPaneBackdropRadiusDip() const noexcept {
        const float ease = quicksift::ui::animation::Ease(quicksift::ui::animation::Curve::Smoothstep,
            std::clamp(leftPaneOpenBlend_, 0.0f, 1.0f));
        const float span = static_cast<float>(kLeftPaneOpenRadiusDip - kLeftPaneClosedRadiusDip);
        const int raw = static_cast<int>(std::lround(static_cast<float>(kLeftPaneClosedRadiusDip) + span * ease));
        return std::max(kLeftPaneClosedRadiusDip, (raw / 2) * 2);
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
        // Quantized to 2-DIP steps so SetWindowRgn is not called every ease tick; the
        // painter uses the SAME quantized value so the stroke matches the region.
        const int radiusDip = LeftPaneBackdropRadiusDip();
        // Directly ABOVE the canvas (and the search input on it), below every pane
        // content sibling. The old hWndInsertAfter = canvas_ placed the backdrop BEHIND
        // the canvas (SetWindowPos inserts after = below), so the clip-siblings canvas
        // covered it: the pane looked transparent and every content repaint exposed
        // thumbnails between its children.
        // Geometry/z-order only; paint is a single invalidate (no ShowWindow thrash).
        HWND insertAfter = LeftPaneBackdropInsertAfter();
        const bool wasVisible = IsWindowVisible(flyoutBackdrop_) != FALSE;
        UINT posFlags = SWP_NOACTIVATE | SWP_NOREDRAW;
        if (!insertAfter) posFlags |= SWP_NOZORDER;
        if (!wasVisible) posFlags |= SWP_SHOWWINDOW;
        SetWindowPos(flyoutBackdrop_, insertAfter, pane.left, pane.top, paneW, paneH, posFlags);
        // Quiet region update (bRedraw=false): never SetWindowRgn TRUE mid-animation.
        if (paneW != leftPaneBackdropLastW_ || paneH != leftPaneBackdropLastH_ ||
            radiusDip != leftPaneBackdropLastRadiusDip_) {
            uiFramework_.ApplyRoundedControlRegion(flyoutBackdrop_, paneW, paneH,
                DipToPx(radiusDip), /*redraw=*/false);
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
        ForEachActiveSectionChild(raise);
    }

    void QuickSiftApplicationImpl::ForEachActiveSectionChild(const std::function<void(HWND)>& visit) const {
        // One enumeration of the active section's child windows (stable order:
        // controls by id, then section labels/selector; or the Folders / Info
        // children). Raise, invalidate and synchronous-paint passes share it.
        if (activeFlyout_ == FlyoutPanel::File || activeFlyout_ == FlyoutPanel::Cull ||
            activeFlyout_ == FlyoutPanel::Filter || activeFlyout_ == FlyoutPanel::Settings) {
            for (const auto& [id, control] : controls_) {
                const bool belongs = activeFlyout_ == FlyoutPanel::File ? IsFileControlId(id) :
                    activeFlyout_ == FlyoutPanel::Cull ? IsCullControlId(id) :
                    activeFlyout_ == FlyoutPanel::Filter ? IsFilterControlId(id) : IsSettingsControlId(id);
                if (belongs) visit(control);
            }
            if (activeFlyout_ == FlyoutPanel::Settings) {
                for (HWND label : settingsSectionLabels_) visit(label);
                visit(languageSelector_);
            }
        } else if (activeFlyout_ == FlyoutPanel::Folders) {
            visit(libraryList_);
            if (auto it = controls_.find(ID_REMOVE_LIBRARY); it != controls_.end()) visit(it->second);
        } else if (activeFlyout_ == FlyoutPanel::Info) {
            visit(exif_);
            visit(userCommentLabel_);
            visit(userCommentEdit_);
            if (auto it = controls_.find(ID_SAVE_USER_COMMENT); it != controls_.end()) visit(it->second);
        }
    }

    bool QuickSiftApplicationImpl::LeftPaneWindowBelongsTo(HWND window, int id, FlyoutPanel panel) const {
        if (!window) return false;
        switch (panel) {
        case FlyoutPanel::Folders: return window == libraryList_ || id == ID_REMOVE_LIBRARY;
        case FlyoutPanel::Info:
            return window == exif_ || window == userCommentLabel_ || window == userCommentEdit_ ||
                id == ID_SAVE_USER_COMMENT;
        case FlyoutPanel::File: return id != 0 && IsFileControlId(id);
        case FlyoutPanel::Cull: return id != 0 && IsCullControlId(id);
        case FlyoutPanel::Filter: return id != 0 && IsFilterControlId(id);
        case FlyoutPanel::Settings: {
            if (window == languageSelector_) return true;
            for (HWND label : settingsSectionLabels_) if (label == window) return true;
            return id != 0 && IsSettingsControlId(id);
        }
        default: return false;
        }
    }

    void QuickSiftApplicationImpl::HideLeftPaneSectionContentExcept(FlyoutPanel keep) {
        // All sections share one LeftPaneContentRect, so every child NOT in `keep` is
        // hidden (leaving them WS_VISIBLE stacked sections). Children of `keep` are
        // left alone: the old hide-all + re-show on every LayoutControls/tab switch
        // flashed each child (hide exposes, show repaints) and reset button hover
        // (ClearButtonHot on SWP_HIDEWINDOW), which read as pane flicker.
        // SWP_NOREDRAW: the caller composes the pane once afterwards.
        auto hideQuiet = [&](HWND window, int id) {
            if (!window || !IsWindowVisible(window)) return;
            if (keep != FlyoutPanel::None && LeftPaneWindowBelongsTo(window, id, keep)) return;
            InvalidateUnderHiddenPaneChild(window);
            const auto clipped = std::find(paneClippedChildren_.begin(), paneClippedChildren_.end(), window);
            if (clipped != paneClippedChildren_.end()) paneClippedChildren_.erase(clipped);
            SetWindowPos(window, nullptr, 0, 0, 0, 0,
                SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                SWP_NOACTIVATE | SWP_NOREDRAW);
        };
        for (const auto& [id, control] : controls_) {
            if (!control) continue;
            if (IsFileControlId(id) || IsCullControlId(id) || IsFilterControlId(id) ||
                IsSettingsControlId(id) || id == ID_REMOVE_LIBRARY || id == ID_SAVE_USER_COMMENT) {
                hideQuiet(control, id);
            }
        }
        hideQuiet(filterLabel_, 0); // never part of a pane section layout
        hideQuiet(languageSelector_, 0);
        for (HWND label : settingsSectionLabels_) hideQuiet(label, 0);
        hideQuiet(libraryList_, 0);
        hideQuiet(exif_, 0);
        hideQuiet(userCommentEdit_, 0);
        hideQuiet(userCommentLabel_, 0);
    }

    bool QuickSiftApplicationImpl::IsPaneScrollableChild(HWND window) const {
        if (!window || window == flyoutBackdrop_) return false;
        for (HWND tab : flyoutTabs_) if (tab == window) return false;
        return window == userCommentEdit_ || IsLeftPaneSurface(window);
    }

    void QuickSiftApplicationImpl::InvalidateUnderHiddenPaneChild(HWND window) {
        // SWP_NOREDRAW hides expose nothing: when the child stood outside the pane
        // backdrop (the overflowing language selector over the status bar/canvas),
        // its pixels stayed behind. Repaint whatever lies under its old rect.
        if (!window || !hwnd_) return;
        RECT rc{};
        GetWindowRect(window, &rc);
        MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&rc), 2);
        RECT pane{};
        if (flyoutBackdrop_ && IsWindowVisible(flyoutBackdrop_)) {
            GetWindowRect(flyoutBackdrop_, &pane);
            MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&pane), 2);
        }
        RECT inside{};
        if (IntersectRect(&inside, &rc, &pane) && EqualRect(&inside, &rc)) return; // backdrop recomposes it
        RedrawWindow(hwnd_, &rc, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }

    void QuickSiftApplicationImpl::ApplyPaneScroll(const RECT& viewport) {
        // Called at the end of LayoutControls: every visible child of the active
        // section was just placed at its natural (unscrolled) position.
        paneViewport_ = viewport;
        paneNaturalRects_.clear();
        int contentBottom = viewport.top;
        for (HWND child = GetWindow(hwnd_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            if (!IsWindowVisible(child) || !IsPaneScrollableChild(child)) continue;
            RECT rc{};
            GetWindowRect(child, &rc);
            MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&rc), 2);
            // Anything placed (and possibly painted) outside the viewport at its
            // natural position: repaint whatever lies under that part.
            if (rc.bottom > viewport.bottom || rc.top < viewport.top) InvalidateUnderHiddenPaneChild(child);
            paneNaturalRects_.emplace_back(child, rc);
            contentBottom = std::max(contentBottom, static_cast<int>(rc.bottom));
        }
        const int previousContent = paneContentHeight_;
        paneContentHeight_ = contentBottom - static_cast<int>(viewport.top);
        // Content change (relocalized captions, wrapped sections, resize): keep the
        // offset but re-clamp it; RepositionPaneScroll clamps.
        (void)previousContent;
        RepositionPaneScroll();
    }

    void QuickSiftApplicationImpl::RepositionPaneScroll() {
        namespace pane = quicksift::core::pane;
        const auto index = static_cast<std::size_t>(activeFlyout_);
        if (index >= paneScrollOffset_.size()) return;
        const int viewTop = static_cast<int>(paneViewport_.top);
        const int viewBottom = static_cast<int>(paneViewport_.bottom);
        const int viewH = viewBottom - viewTop;
        int& offset = paneScrollOffset_[index];
        offset = pane::ClampScroll(offset, paneContentHeight_, viewH);
        for (const auto& [child, natural] : paneNaturalRects_) {
            if (!IsWindow(child)) continue;
            const int w = static_cast<int>(natural.right - natural.left);
            const int h = static_cast<int>(natural.bottom - natural.top);
            const int top = static_cast<int>(natural.top) - offset;
            const auto visibility = pane::Classify(top, top + h, viewTop, viewBottom);
            const auto clipped = std::find(paneClippedChildren_.begin(), paneClippedChildren_.end(), child);
            const bool wasClipped = clipped != paneClippedChildren_.end();
            RECT current{};
            GetWindowRect(child, &current);
            MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&current), 2);
            const bool moving = current.top != top || current.left != natural.left;
            if (visibility == pane::Visibility::Hidden) {
                if (IsWindowVisible(child) && GetFocus() != child) {
                    InvalidateUnderHiddenPaneChild(child); // old pixels outside the pane
                    SetWindowPos(child, nullptr, natural.left, top, 0, 0,
                        SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
                }
                continue;
            }
            if (offset != 0 || wasClipped || !IsWindowVisible(child)) {
                if (moving && IsWindowVisible(child)) InvalidateUnderHiddenPaneChild(child);
                SetWindowPos(child, nullptr, natural.left, top, 0, 0,
                    SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
            }
            if (visibility == pane::Visibility::Partial) {
                // Clip to the pane viewport so nothing paints over the status bar,
                // canvas or tab rail.
                const int clipTop = std::max(0, viewTop - top);
                const int clipBottom = std::min(h, viewBottom - top);
                if (HRGN region = CreateRectRgn(0, clipTop, w, clipBottom)) {
                    if (!SetWindowRgn(child, region, FALSE)) DeleteObject(region);
                }
                if (!wasClipped) paneClippedChildren_.push_back(child);
            } else if (wasClipped) {
                paneClippedChildren_.erase(clipped);
                if (child == libraryList_ || child == exif_) {
                    uiFramework_.ApplyRoundedControlRegion(child, w, h, DipToPx(kLeftPaneContentRadiusDip), false);
                } else if (child == languageSelector_) {
                    uiFramework_.ApplyRoundedControlRegion(child, w, h, DipToPx(kButtonRadiusDip), false);
                } else {
                    uiFramework_.ApplyAntialiasedControlRegion(child, w, h, DipToPx(kButtonRadiusDip), false);
                }
            }
        }
    }

    bool QuickSiftApplicationImpl::ScrollPaneBy(int deltaPx) {
        const auto index = static_cast<std::size_t>(activeFlyout_);
        if (index >= paneScrollOffset_.size() || paneNaturalRects_.empty()) return false;
        const int viewH = static_cast<int>(paneViewport_.bottom - paneViewport_.top);
        const int before = paneScrollOffset_[index];
        const int after = quicksift::core::pane::ClampScroll(before + deltaPx, paneContentHeight_, viewH);
        if (after == before) return false;
        paneScrollOffset_[index] = after;
        quicksift::ui::framework::MacOsUiFramework::HideOverflowTip(nullptr);
        RepositionPaneScroll();
        if (flyoutBackdrop_) {
            RedrawWindow(flyoutBackdrop_, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE | RDW_NOCHILDREN);
        }
        RedrawActiveFlyoutContents();
        return true;
    }

    bool QuickSiftApplicationImpl::HandlePaneWheel(WPARAM wParam, LPARAM lParam) {
        if (activeFlyout_ == FlyoutPanel::None || paneNaturalRects_.empty()) return false;
        const int viewH = static_cast<int>(paneViewport_.bottom - paneViewport_.top);
        if (quicksift::core::pane::MaxScroll(paneContentHeight_, viewH) <= 0) return false;
        POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd_, &point);
        RECT client{};
        GetClientRect(hwnd_, &client);
        RECT pane = LeftPaneRect(static_cast<int>(client.right), static_cast<int>(client.bottom));
        if (!PtInRect(&pane, point)) return false;
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        if (lines == WHEEL_PAGESCROLL) {
            ScrollPaneBy(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -viewH : viewH);
            return true;
        }
        ScrollPaneBy(quicksift::core::pane::WheelStep(GET_WHEEL_DELTA_WPARAM(wParam), DipToPx(16),
            static_cast<int>(lines)));
        return true; // consumed even at the ends: the canvas must not scroll under the pane
    }

    void QuickSiftApplicationImpl::EnsurePaneControlVisible(HWND control) {
        const auto index = static_cast<std::size_t>(activeFlyout_);
        if (!control || index >= paneScrollOffset_.size()) return;
        for (const auto& [child, natural] : paneNaturalRects_) {
            if (child != control) continue;
            const int viewH = static_cast<int>(paneViewport_.bottom - paneViewport_.top);
            const int top = static_cast<int>(natural.top - paneViewport_.top);
            const int bottom = static_cast<int>(natural.bottom - paneViewport_.top);
            const int target = quicksift::core::pane::ScrollIntoView(paneScrollOffset_[index], top, bottom,
                paneContentHeight_, viewH, DipToPx(6));
            if (target != paneScrollOffset_[index]) ScrollPaneBy(target - paneScrollOffset_[index]);
            return;
        }
    }

    bool QuickSiftApplicationImpl::PaneScrollbarGeometry(RECT& track, RECT& thumb) const {
        const auto index = static_cast<std::size_t>(activeFlyout_);
        if (index >= paneScrollOffset_.size() || paneNaturalRects_.empty()) return false;
        const int viewTop = static_cast<int>(paneViewport_.top);
        const int viewBottom = static_cast<int>(paneViewport_.bottom);
        const int viewH = viewBottom - viewTop;
        if (quicksift::core::pane::MaxScroll(paneContentHeight_, viewH) <= 0) return false;
        const int barW = DipToPx(4);
        const int x = static_cast<int>(paneViewport_.right) + DipToPx(2);
        track = RECT{ x, viewTop, x + barW, viewBottom };
        const auto t = quicksift::core::pane::ThumbFor(paneScrollOffset_[index], paneContentHeight_, viewH,
            viewTop, viewBottom, DipToPx(24));
        thumb = RECT{ x, t.top, x + barW, t.bottom };
        return t.bottom > t.top;
    }

    void QuickSiftApplicationImpl::EraseLeftPaneBackdropFully() {
        if (!flyoutBackdrop_ || !IsWindowVisible(flyoutBackdrop_)) return;
        // Invalidate only. This used to force RDW_ERASE | RDW_UPDATENOW: a solid
        // panelStrong erase painted and shown, then the frosted paint on top (two
        // visible frames per tab switch). The single buffered compose happens in
        // RedrawActiveFlyoutContents.
        RedrawWindow(flyoutBackdrop_, nullptr, nullptr,
            RDW_INVALIDATE | RDW_NOERASE | RDW_NOCHILDREN);
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
            const auto revealStart = std::chrono::steady_clock::now();
            LayoutControls(width, height);
            if (showContent) {
                const auto index = static_cast<std::size_t>(activeFlyout_);
                const bool first = index < paneSectionRevealed_.size() && !paneSectionRevealed_[index];
                if (first) paneSectionRevealed_[index] = true;
                LogPaneTiming(L"pane_reveal", activeFlyout_, first, PaneMsSince(revealStart), &paneLayoutTiming_);
            }
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
        if (paneFirstPaintPending_ && activeFlyout_ != FlyoutPanel::None) {
            // Open request -> first backdrop paint (the input-to-pixels latency).
            paneFirstPaintPending_ = false;
            const bool first = !paneEverPainted_;
            paneEverPainted_ = true;
            LogPaneTiming(L"pane_first_paint", activeFlyout_, first, PaneMsSince(paneOpenRequestedAt_), nullptr);
        }
        const RECT& rc = draw->rcItem;
        const int width = std::max(0, static_cast<int>(rc.right - rc.left));
        const int height = std::max(0, static_cast<int>(rc.bottom - rc.top));
        if (width <= 0 || height <= 0) return;

        const float openBlend = std::clamp(leftPaneOpenBlend_, 0.0f, 1.0f);
        const float ease = quicksift::ui::animation::Ease(quicksift::ui::animation::Curve::Smoothstep, openBlend);
        const bool raised = ease > 0.15f;
        // Same quantized radius as the HWND region (PlaceLeftPaneBackdrop): the
        // unquantized value used to differ by up to 1 DIP mid-animation, so the AA
        // border was cut by the region staircase at the corners.
        const int radius = LeftPaneBackdropRadiusDip();
        const ThemePalette palette = Palette();

        // Double-buffer: opaque frost fill + grip chrome compose off-screen, then
        // one BitBlt. Prevents mid-paint flicker and any AlphaBlend see-through.
        HDC memDc = CreateCompatibleDC(draw->hDC);
        HBITMAP memBm = memDc ? CreateCompatibleBitmap(draw->hDC, width, height) : nullptr;
        HGDIOBJ oldBm = (memDc && memBm) ? SelectObject(memDc, memBm) : nullptr;
        const bool buffered = memDc && memBm && oldBm;
        HDC target = buffered ? memDc : draw->hDC;
        RECT local = buffered ? RECT{ 0, 0, width, height } : rc;

        if (leftPaneFrostValid_ && LeftPaneFrostEnabled() && uiFramework_.HasPaneFrost() &&
            leftPaneFrostTint_ == Palette().panelStrong) { // never the previous theme's tint
            // Frosted material: the cached blurred canvas snapshot under the pane,
            // tinted with panelStrong (ui_frost), covering the whole pane (tab column,
            // content area and bottom action row), plus hairline + top wash.
            uiFramework_.PaintPaneFrost(target, local, LeftPaneFrostSourceFor(flyoutBackdrop_),
                palette.panelStrong);
            uiFramework_.PaintFrostedPanelChrome(target, local, radius, raised, palette, currentDpi_);
        } else {
            // Fallback (transparency off / high contrast / no snapshot yet): opaque.
            uiFramework_.PaintSolidSurface(target, local, palette.panelStrong);
            uiFramework_.PaintPanel(target, local,
                quicksift::ui::framework::PanelPresentation{ raised, raised /* frost wash */, radius },
                palette, currentDpi_);
        }

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

        // Themed overlay scrollbar for an overflowing pane section (in the inset
        // gutter right of the content viewport).
        if (raised) {
            RECT track{}, thumb{};
            if (PaneScrollbarGeometry(track, thumb)) {
                RECT self{};
                GetWindowRect(flyoutBackdrop_, &self);
                MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&self), 2);
                const int dx = static_cast<int>(local.left - self.left);
                const int dy = static_cast<int>(local.top - self.top);
                OffsetRect(&track, dx, dy);
                OffsetRect(&thumb, dx, dy);
                uiFramework_.PaintOverlayScrollbar(target, thumb, track, false, false, palette, currentDpi_);
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


    // ---- Left-pane frost + buffered pane surfaces ------------------------------------

    HWND QuickSiftApplicationImpl::LeftPaneBackdropInsertAfter() const {
        // Bottom of the stack: canvas, then the search input glued to it. The backdrop
        // goes directly above that floor; SetWindowPos(backdrop, X) puts it just
        // BELOW X, so X is the window currently above the floor.
        HWND floor = (searchInput_ && IsWindowVisible(searchInput_)) ? searchInput_ : canvas_;
        if (!floor) return HWND_BOTTOM;
        HWND above = GetWindow(floor, GW_HWNDPREV);
        if (above == flyoutBackdrop_) return nullptr; // already in its slot
        return above ? above : HWND_TOP;
    }

    bool QuickSiftApplicationImpl::LeftPaneFrostEnabled() const noexcept {
        // Transparency effects off / high contrast: opaque panelStrong (no snapshot).
        return quicksift::ui::CurrentVisualEffects().Glass();
    }

    POINT QuickSiftApplicationImpl::LeftPaneFrostSourceFor(HWND window) const {
        RECT rect{};
        if (!window || !hwnd_ || !GetWindowRect(window, &rect)) return POINT{ 0, 0 };
        MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&rect), 2);
        return POINT{ rect.left - leftPaneFrostOrigin_.x, rect.top - leftPaneFrostOrigin_.y };
    }

    bool QuickSiftApplicationImpl::IsLeftPaneSurface(HWND window) const {
        if (!window) return false;
        if (window == flyoutBackdrop_ || window == libraryList_ || window == exif_ ||
            window == userCommentLabel_ || window == languageSelector_ || window == filterLabel_) return true;
        for (HWND tab : flyoutTabs_) if (tab == window) return true;
        for (HWND label : settingsSectionLabels_) if (label == window) return true;
        const int id = GetDlgCtrlID(window);
        if (id == 0) return false;
        const auto it = controls_.find(id);
        if (it == controls_.end() || it->second != window) return false;
        return IsFileControlId(id) || IsCullControlId(id) || IsFilterControlId(id) ||
            IsSettingsControlId(id) || id == ID_REMOVE_LIBRARY || id == ID_SAVE_USER_COMMENT;
    }

    void QuickSiftApplicationImpl::PaintLeftPaneFrostBehind(HWND window, HDC dc, const RECT& local) const {
        const COLORREF fallback = Palette().panelStrong;
        if (leftPaneFrostValid_ && LeftPaneFrostEnabled() && uiFramework_.HasPaneFrost() &&
            leftPaneFrostTint_ == Palette().panelStrong) { // never the previous theme's tint
            POINT source = LeftPaneFrostSourceFor(window);
            source.x += local.left;
            source.y += local.top;
            uiFramework_.PaintPaneFrost(dc, local, source, fallback);
        } else {
            uiFramework_.PaintSolidSurface(dc, local, fallback);
        }
    }

    void QuickSiftApplicationImpl::MaybeCaptureLeftPaneFrost() {
        if (!swapChainTarget_ || !deviceContext2_ || !canvas_ || !hwnd_ || !flyoutBackdrop_ ||
            activeFlyout_ == FlyoutPanel::None || !IsWindowVisible(flyoutBackdrop_)) return;
        if (!LeftPaneFrostEnabled()) {
            if (leftPaneFrostValid_) {
                uiFramework_.ClearPaneFrost();
                leftPaneFrostValid_ = false;
            }
            leftPaneFrostDirty_ = leftPaneFrostPending_ = false;
            return;
        }
        const ThemePalette palette = Palette();
        if (palette.panelStrong != leftPaneFrostTint_) leftPaneFrostDirty_ = true;
        // Never read back while the grid is in scroll motion / fling (placeholders):
        // the next settled frame refreshes it.
        if (ThumbnailScrollPrefersPlaceholders()) {
            leftPaneFrostPending_ = true;
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!leftPaneFrostDirty_ && now - leftPaneFrostCapturedAt_ <
            std::chrono::milliseconds(quicksift::ui::frost::kRefreshIntervalMs)) {
            leftPaneFrostPending_ = true; // throttled: ServiceLeftPaneFrost re-renders once
            return;
        }
        leftPaneFrostCapturedAt_ = now;
        leftPaneFrostDirty_ = false;
        leftPaneFrostPending_ = false;
        struct FrostTimingScope {
            QuickSiftApplicationImpl& self;
            std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            ~FrostTimingScope() {
                const double ms = PaneMsSince(start);
                const bool first = !self.paneFirstFrostLogged_;
                self.paneFirstFrostLogged_ = true;
                self.LogPaneTiming(L"pane_frost_capture", self.activeFlyout_, first, ms, nullptr);
            }
        } frostTiming{ *this };

        RECT client{};
        GetClientRect(hwnd_, &client);
        const RECT open = LeftPaneOpenRect(client.right, client.bottom);
        RECT canvasRect{};
        GetWindowRect(canvas_, &canvasRect);
        MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&canvasRect), 2);
        const D2D1_SIZE_U pixels = swapChainTarget_->GetPixelSize();
        RECT source{ open.left - canvasRect.left, open.top - canvasRect.top,
            open.right - canvasRect.left, open.bottom - canvasRect.top };
        const RECT bounds{ 0, 0, static_cast<LONG>(pixels.width), static_cast<LONG>(pixels.height) };
        if (!IntersectRect(&source, &source, &bounds)) return;
        const UINT32 width = static_cast<UINT32>(source.right - source.left);
        const UINT32 height = static_cast<UINT32>(source.bottom - source.top);
        if (!leftPaneFrostReadback_ || leftPaneFrostReadbackSize_.width != width ||
            leftPaneFrostReadbackSize_.height != height) {
            leftPaneFrostReadback_.Reset();
            D2D1_BITMAP_PROPERTIES1 properties{};
            properties.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
            properties.dpiX = 96.0f;
            properties.dpiY = 96.0f;
            properties.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
            if (FAILED(deviceContext2_->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
                &properties, &leftPaneFrostReadback_)) || !leftPaneFrostReadback_) return;
            leftPaneFrostReadbackSize_ = D2D1::SizeU(width, height);
        }
        const D2D1_POINT_2U destination = D2D1::Point2U(0, 0);
        const D2D1_RECT_U from = D2D1::RectU(static_cast<UINT32>(source.left), static_cast<UINT32>(source.top),
            static_cast<UINT32>(source.right), static_cast<UINT32>(source.bottom));
        if (FAILED(leftPaneFrostReadback_->CopyFromBitmap(&destination, swapChainTarget_.Get(), &from))) return;
        D2D1_MAPPED_RECT mapped{};
        if (FAILED(leftPaneFrostReadback_->Map(D2D1_MAP_OPTIONS_READ, &mapped)) || !mapped.bits) return;
        const quicksift::ui::frost::Image image = quicksift::ui::frost::Build(mapped.bits,
            static_cast<int>(width), static_cast<int>(height), static_cast<int>(mapped.pitch),
            quicksift::ui::frost::FromColorRef(palette.panelStrong),
            quicksift::ui::frost::TintAlpha(palette.dark));
        leftPaneFrostReadback_->Unmap();
        if (!image.Valid()) return;
        const POINT origin{ canvasRect.left + source.left, canvasRect.top + source.top };
        if (leftPaneFrostValid_ && image.signature == leftPaneFrostSignature_ &&
            origin.x == leftPaneFrostOrigin_.x && origin.y == leftPaneFrostOrigin_.y &&
            palette.panelStrong == leftPaneFrostTint_) return; // nothing visible changed
        uiFramework_.SetPaneFrost(image);
        leftPaneFrostValid_ = uiFramework_.HasPaneFrost();
        leftPaneFrostSignature_ = image.signature;
        leftPaneFrostTint_ = palette.panelStrong;
        leftPaneFrostOrigin_ = origin;
        // Repaint the pane surfaces (each is one buffered blit; no erase, no UPDATENOW
        // from inside the canvas paint).
        auto invalidate = [](HWND window) {
            if (window && IsWindowVisible(window)) RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
        };
        invalidate(flyoutBackdrop_);
        for (HWND tab : flyoutTabs_) invalidate(tab);
        for (const auto& [id, control] : controls_) {
            if (control && IsWindowVisible(control) && IsLeftPaneSurface(control)) invalidate(control);
        }
        for (HWND label : settingsSectionLabels_) invalidate(label);
        invalidate(languageSelector_);
        invalidate(libraryList_);
        invalidate(exif_);
        invalidate(userCommentLabel_);
    }

    void QuickSiftApplicationImpl::ServiceLeftPaneFrost() {
        if (!leftPaneFrostDirty_ && !leftPaneFrostPending_) return;
        if (!hwnd_ || IsIconic(hwnd_) || !swapChainTarget_ || activeFlyout_ == FlyoutPanel::None ||
            !flyoutBackdrop_ || !IsWindowVisible(flyoutBackdrop_) || !LeftPaneFrostEnabled()) return;
        if (ThumbnailScrollPrefersPlaceholders()) return;
        if (!leftPaneFrostDirty_ && std::chrono::steady_clock::now() - leftPaneFrostCapturedAt_ <
            std::chrono::milliseconds(quicksift::ui::frost::kRefreshIntervalMs)) return;
        // One canvas frame; Render reads the pixels under the pane back after EndDraw.
        InvalidateCanvas();
    }

    RECT QuickSiftApplicationImpl::LibraryListThumbRect(HWND list) const {
        RECT client{};
        GetClientRect(list, &client);
        const int count = static_cast<int>(SendMessageW(list, LB_GETCOUNT, 0, 0));
        const int itemHeight = std::max(1, static_cast<int>(SendMessageW(list, LB_GETITEMHEIGHT, 0, 0)));
        const int visible = std::max(1, static_cast<int>(client.bottom - client.top) / itemHeight);
        if (count <= visible) return RECT{};
        const int margin = DipToPx(4);
        const int width = DipToPx(5);
        const int trackTop = client.top + margin;
        const int trackHeight = std::max(1, static_cast<int>(client.bottom - client.top) - 2 * margin);
        const int thumbHeight = std::clamp(MulDiv(trackHeight, visible, count), DipToPx(28), trackHeight);
        const int maxTop = std::max(1, count - visible);
        const int top = std::clamp(static_cast<int>(SendMessageW(list, LB_GETTOPINDEX, 0, 0)), 0, maxTop);
        const int y = trackTop + MulDiv(std::max(0, trackHeight - thumbHeight), top, maxTop);
        return RECT{ client.right - margin - width, y, client.right - margin, y + thumbHeight };
    }

    HBRUSH QuickSiftApplicationImpl::LibraryListBrush() {
        // Any area the list control fills by itself uses the panel colour (the default
        // COLOR_WINDOW brush is white).
        const COLORREF color = Palette().panelStrong;
        if (!libraryListBrush_ || libraryListBrushColor_ != color) {
            if (libraryListBrush_) DeleteObject(libraryListBrush_);
            libraryListBrush_ = CreateSolidBrush(color);
            libraryListBrushColor_ = color;
        }
        return libraryListBrush_;
    }

    void QuickSiftApplicationImpl::PaintLibraryList(HWND hwnd) {
        PAINTSTRUCT paint{};
        HDC target = BeginPaint(hwnd, &paint);
        if (!target) return;
        RECT rect{};
        GetClientRect(hwnd, &rect);
        const ThemePalette palette = Palette();
        // ONE buffered pass: frost behind the rounded corners, the opaque content
        // panel, the owner-drawn rows (rendered into the same buffer through
        // WM_PRINTCLIENT -> WM_DRAWITEM), the overlay scrollbar and the border.
        uiFramework_.PaintBuffered(target, rect, [&](HDC dc, const RECT& local) {
            PaintLeftPaneFrostBehind(hwnd, dc, local);
            uiFramework_.PaintPanel(dc, local,
                quicksift::ui::framework::PanelPresentation{ false, false, kLeftPaneContentRadiusDip },
                palette, currentDpi_);
            if (SendMessageW(hwnd, LB_GETCOUNT, 0, 0) == 0) {
                HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
                RECT textRect = local;
                InflateRect(&textRect, -DipToPx(12), 0);
                uiFramework_.PaintCenteredMutedText(dc, textRect, Tr(L"No item in Library"),
                    palette, currentDpi_, font);
            } else {
                DefSubclassProc(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
                const RECT thumb = LibraryListThumbRect(hwnd);
                if (!IsRectEmpty(&thumb)) {
                    RECT track = thumb;
                    track.top = local.top + DipToPx(4);
                    track.bottom = local.bottom - DipToPx(4);
                    uiFramework_.PaintOverlayScrollbar(dc, thumb, track, treeScrollbarHot_,
                        libraryScrollDragging_, palette, currentDpi_);
                }
            }
            uiFramework_.PaintControlBorder(dc, local, kLeftPaneContentRadiusDip, palette, currentDpi_);
        });
        EndPaint(hwnd, &paint);
    }

    void QuickSiftApplicationImpl::RedrawActiveFlyoutContents() {
        if (activeFlyout_ == FlyoutPanel::None || !flyoutBackdrop_ ||
            !IsWindowVisible(flyoutBackdrop_)) return;

        // Single opaque compose: backdrop under content, then raise + invalidate
        // active section only. One UPDATENOW at the end — never erase parent under
        // visible children mid-pass, never per-child UPDATENOW thrash.
        HWND insertAfter = LeftPaneBackdropInsertAfter();
        UINT zFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW;
        if (!insertAfter) zFlags |= SWP_NOZORDER;
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

        ForEachActiveSectionChild(raiseAndInvalidate);
        for (HWND tab : flyoutTabs_) raiseAndInvalidate(tab);

        // Synchronous compose of pane chrome only — never parent RDW_ALLCHILDREN
        // (that would force a D2D canvas repaint and flash under the pane).
        // [Pane] timing: the synchronous paints are where a first-ever paint
        // (fonts, buffers, regions) would show up; keep the slowest one.
        double slowestPaintMs = 0.0;
        int slowestPaintId = -1;
        auto updateNow = [&](HWND window) {
            if (!window || !IsWindowVisible(window)) return;
            const auto paintStart = std::chrono::steady_clock::now();
            UpdateWindow(window);
            const double ms = PaneMsSince(paintStart);
            if (ms > slowestPaintMs) {
                slowestPaintMs = ms;
                slowestPaintId = GetDlgCtrlID(window);
            }
        };
        updateNow(flyoutBackdrop_);
        ForEachActiveSectionChild(updateNow);
        for (HWND tab : flyoutTabs_) updateNow(tab);
        if (slowestPaintId >= 0) paneLayoutTiming_.Add(L"slowest_paint", slowestPaintMs);
        if (slowestPaintMs >= quicksift::core::pane_timing::kSlowPaneMs) {
            QS_LOG_EVENT(quicksift::diagnostics::Level::Info, L"Pane", L"pane_slow_paint",
                {L"control_id", std::to_wstring(slowestPaintId)},
                {L"ms", quicksift::core::pane_timing::Recorder::FormatMs(slowestPaintMs)});
        }
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


    QuickSiftApplicationImpl::TitleLayoutKey QuickSiftApplicationImpl::CurrentTitleLayoutKey(int width) const {
        TitleLayoutKey key;
        key.width = width;
        key.dpi = currentDpi_;
        key.fullscreen = fullscreen_;
        key.fullscreenTitleVisible = fullscreenTitleVisible_;
        key.singleOrCompare = reviewState_.Mode() == ViewMode::Single || reviewState_.Mode() == ViewMode::Compare;
        key.theme = theme_;
        key.widths = MeasuredTitleButtonWidthsPx(); // cheap: text extents only
        return key;
    }

    void QuickSiftApplicationImpl::LayoutTitleControls(int width, bool force) {
        if (width <= 0) return;
        const TitleLayoutKey key = CurrentTitleLayoutKey(width);
        // Fullscreen always re-applies: its overlay is shown/raised here.
        if (!force && !titleLayoutDirty_ && !key.fullscreen && titleLayoutApplied_ == key) return;
        titleLayoutApplied_ = key;
        titleLayoutDirty_ = false;
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
            uiFramework_.ApplyAntialiasedControlRegion(help->second, helpWidth, titleButtonHeight,
                titlePillRadius);
        }
        if (const auto theme = controls_.find(ID_THEME_TOGGLE); theme != controls_.end()) {
            SetWindowPos(theme->second, HWND_TOP, themeX, firstRowY,
                themeWidth, titleButtonHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            uiFramework_.ApplyAntialiasedControlRegion(theme->second, themeWidth, titleButtonHeight,
                titlePillRadius);
        }

        const bool singleOrCompare = key.singleOrCompare;
        for (const ButtonDefinition& spec : kTitleButtons) {
            const bool show = !((spec.id == ID_SORT || spec.id == ID_THUMB_SIZE) && singleOrCompare) &&
                !((spec.id == ID_ZOOM_MODE || spec.id == ID_ROTATE_LEFT || spec.id == ID_ROTATE_RIGHT) && !singleOrCompare);
            if (const auto it = controls_.find(spec.id); it != controls_.end()) ShowWindow(it->second, show ? SW_SHOW : SW_HIDE);
        }

        const auto& controlWidths = key.widths;
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
                uiFramework_.ApplyAntialiasedControlRegion(it->second, controlWidth, titleButtonHeight,
                    titlePillRadius);
            }
            x += controlWidth + gap;
        }
    }


    void QuickSiftApplicationImpl::SetFilmstripToggleGlyph() {
        // Caption only when it changes: SetWindowTextW on a visible BUTTON repaints
        // it synchronously (WM_SETTEXT), and layout used to do that twice per pass.
        if (!filmstripToggle_) return;
        const wchar_t* glyph = filmstripVisible_ ? L"‹" : L"›";
        wchar_t current[8]{};
        GetWindowTextW(filmstripToggle_, current, static_cast<int>(std::size(current)));
        if (wcscmp(current, glyph) != 0) SetWindowTextW(filmstripToggle_, glyph);
    }

    void QuickSiftApplicationImpl::PlaceFilmstripToggle(int width, int height) {
        // One placement for the Single-view filmstrip toggle (was duplicated in
        // LayoutControls, with the later copy's geometry winning): vertically
        // centred on the canvas right edge, hidden outside Single view.
        if (!filmstripToggle_) return;
        const bool show = reviewState_.Mode() == ViewMode::Single;
        namespace film = quicksift::core::filmstrip;
        const int toggleSize = DipToPx(film::kToggleDip);
        const int margin = DipToPx(film::kToggleMarginDip);
        const RECT canvas = CanvasLayoutRect(width, height);
        const int canvasTop = static_cast<int>(canvas.top);
        const int canvasBottom = static_cast<int>(canvas.bottom);
        // Round 22: left of the open strip (it used to cover part of a cell).
        // The strip is drawn at the canvas right edge; closed keeps the old spot.
        const bool open = show && filmstripVisible_;
        const int toggleX = film::ToggleLeftPx(open ? static_cast<int>(canvas.right) : width,
            toggleSize, margin, DipToPx(static_cast<int>(film::kWidthDip)), open);
        const int toggleY = std::max(canvasTop + margin,
            canvasTop + (canvasBottom - canvasTop - toggleSize) / 2);
        SetWindowPos(filmstripToggle_, HWND_TOP, toggleX, toggleY, toggleSize, toggleSize,
            (show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW) | SWP_NOACTIVATE);
        if (!show) return;
        SetFilmstripToggleGlyph(); // lock-step with ToggleFilmstrip()
        uiFramework_.ApplyRoundedControlRegion(filmstripToggle_, toggleSize, toggleSize, DipToPx(kCompactControlRadiusDip));
    }

    // ---- LayoutControls section helpers (0.9.5.0 round 19: split with no behaviour
    // change). Everything from here to closeFlyoutLayout() in LayoutControls runs
    // before ApplyPaneScroll, so none of it may repaint a pane child synchronously.

    // Show (if hidden) and move a pane child without redrawing, then give it its
    // rounded or antialiased region. Shared by every pane section below.
    namespace lp = quicksift::ui::layout::left_pane;

    void QuickSiftApplicationImpl::PlacePaneChild(HWND child, int x, int y, int width, int height,
        int radius, PaneChildRegion region) {
        UINT f = SWP_NOACTIVATE | SWP_NOREDRAW;
        if (!IsWindowVisible(child)) f |= SWP_SHOWWINDOW;
        SetWindowPos(child, HWND_TOP, x, y, width, height, f);
        if (region == PaneChildRegion::Antialiased)
            uiFramework_.ApplyAntialiasedControlRegion(child, width, height, radius, /*redraw=*/false);
        else
            uiFramework_.ApplyRoundedControlRegion(child, width, height, radius, /*redraw=*/false);
    }

    void QuickSiftApplicationImpl::LayoutCanvasArea(int width, int height) {
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
        // Slack AA region: DrawCanvasCornerUnderlay paints the exact AA corner arc
        // (radius kPanelRadiusDip) in the background colour inside the region.
        if (canvasGeometryChanged) uiFramework_.ApplyAntialiasedControlRegion(canvas_, canvasW, canvasH, DipToPx(kPanelRadiusDip));
        LayoutSearchField(); // keeps the field glued to the canvas (and just above it in z-order)
    }

    void QuickSiftApplicationImpl::LayoutFlyoutTabs(int width, int height, bool showPaneContent) {
        // Left-pane vertical tabs live inside the open pane. Content uses hysteresis
        // (LeftPaneShouldShowContent) so open sessions keep children shown until close.
        const std::array<FlyoutPanel, 6> flyoutPanels{{ FlyoutPanel::Folders, FlyoutPanel::File, FlyoutPanel::Cull, FlyoutPanel::Filter,
            FlyoutPanel::Info, FlyoutPanel::Settings }};
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
    }

    void QuickSiftApplicationImpl::LayoutFoldersSection(const RECT& flyout) {
        const int listW = std::max(0, static_cast<int>(flyout.right - flyout.left));
        const int listH = std::max(0, static_cast<int>(flyout.bottom - flyout.top) - DipToPx(lp::kFolderActionReserveDip));
        const int contentRadius = DipToPx(kLeftPaneContentRadiusDip);
        PlacePaneChild(libraryList_, flyout.left, flyout.top, listW, listH, contentRadius,
            PaneChildRegion::Rounded);
        auto it = controls_.find(ID_REMOVE_LIBRARY);
        if (it != controls_.end()) {
            const int bh = DipToPx(lp::kFolderActionHeightDip);
            const int bw = std::min(listW, DipToPx(lp::kFolderActionMaxWidthDip));
            // Match File/Cull action buttons: rounded HWND so PaintButton chrome
            // cannot leave a sharp rectangular underlay behind the pill fill.
            PlacePaneChild(it->second, flyout.left + (listW - bw) / 2, flyout.bottom - bh, bw, bh,
                DipToPx(kButtonRadiusDip), PaneChildRegion::Antialiased);
        }
    }

    void QuickSiftApplicationImpl::LayoutInfoSection(const RECT& flyout) {
        const int infoW = std::max(0, static_cast<int>(flyout.right - flyout.left));
        const int saveH = DipToPx(lp::kInfoSaveButtonHeightDip);
        const int labelH = DipToPx(lp::kInfoLabelHeightDip);
        const int gap = DipToPx(lp::kInfoGapDip);
        // Keep a small pad so the D2D text-input border stroke is not clipped
        // by the pane edge / rounded region.
        const int edgePad = DipToPx(std::max(lp::kInfoEdgePadMinDip, kTextInputBorderInsetDip + 1));
        const int availableH = std::max(0,
            static_cast<int>(flyout.bottom - flyout.top) - edgePad);
        int commentH = DipToPx(lp::kInfoCommentHeightDip);
        int chromeH = labelH + gap + commentH + gap + saveH;
        int infoH = availableH - chromeH;
        if (infoH < DipToPx(lp::kInfoExifMinHeightDip)) {
            const int deficit = DipToPx(lp::kInfoExifMinHeightDip) - infoH;
            commentH = std::max(DipToPx(lp::kInfoCommentMinHeightDip), commentH - deficit);
            chromeH = labelH + gap + commentH + gap + saveH;
            infoH = std::max(0, availableH - chromeH);
        }
        int y = flyout.top;
        PlacePaneChild(exif_, flyout.left, y, infoW, infoH, DipToPx(kLeftPaneContentRadiusDip),
            PaneChildRegion::Rounded);
        y += infoH + gap;
        PlacePaneChild(userCommentLabel_, flyout.left, y, infoW, labelH, DipToPx(kButtonRadiusDip),
            PaneChildRegion::Antialiased);
        y += labelH + gap;
        PlacePaneChild(userCommentEdit_, flyout.left, y, infoW, commentH, DipToPx(kTextInputRadiusDip),
            PaneChildRegion::Rounded);
        uiFramework_.SetD2DTextInputPalette(userCommentEdit_, Palette(), currentDpi_);
        y += commentH + gap;
        auto save = controls_.find(ID_SAVE_USER_COMMENT);
        if (save != controls_.end()) {
            PlacePaneChild(save->second, flyout.left, y, infoW, saveH, DipToPx(kButtonRadiusDip),
                PaneChildRegion::Antialiased);
        }
    }

    // File / Cull / Filter: flow the section's buttons left to right, wrapping rows.
    void QuickSiftApplicationImpl::LayoutButtonFlowSection(const RECT& flyout, HDC flyoutMeasureDc) {
        const int panelRowHeight = DipToPx(lp::kFlowRowHeightDip);
        const int gapX = DipToPx(kTitleControlGapDip);
        const int gapY = DipToPx(lp::kFlowRowGapDip);
        const int leftEdge = flyout.left;
        const int rightEdge = flyout.right;
        int x = leftEdge;
        int y = flyout.top;
        auto place = [&](const ButtonDefinition& def) {
            auto it = controls_.find(def.id);
            if (it == controls_.end()) return;
            const int widthLimit = std::max(1, rightEdge - leftEdge);
            const int buttonWidth = std::min(widthLimit, FlyoutButtonWidthPx(flyoutMeasureDc, def.id, lp::kFlowButtonFallbackWidthDip));
            if (x != leftEdge && x + buttonWidth > rightEdge) {
                x = leftEdge;
                y += panelRowHeight + gapY;
            }
            PlacePaneChild(it->second, x, y, buttonWidth, panelRowHeight, DipToPx(kButtonRadiusDip),
                PaneChildRegion::Antialiased);
            x += buttonWidth + gapX;
        };
        if (activeFlyout_ == FlyoutPanel::File) for (const auto& def : kFileButtons) place(def);
        else if (activeFlyout_ == FlyoutPanel::Cull) for (const auto& def : kCullButtons) place(def);
        else if (activeFlyout_ == FlyoutPanel::Filter) for (const auto& def : kFilterButtons) place(def);
    }

    void QuickSiftApplicationImpl::LayoutSettingsSection(const RECT& flyout, HDC flyoutMeasureDc) {
        const int panelRowHeight = DipToPx(lp::kSettingsRowHeightDip);
        const int leftEdge = flyout.left;
        const int rightEdge = flyout.right;
        int x = leftEdge;
        int y = flyout.top;
        const int sectionHeight = DipToPx(lp::kSettingsSectionHeaderHeightDip);
        const int sectionControlGap = DipToPx(lp::kSettingsSectionControlGapDip);
        const int rowGap = DipToPx(lp::kSettingsRowGapDip);
        const int selectorHeight = DipToPx(lp::kSettingsSelectorHeightDip);
        const int availableWidth = std::max(1, rightEdge - leftEdge);
        const int columnGap = DipToPx(kTitleControlGapDip);
        const int columnWidth = std::max(1, (availableWidth - columnGap) / 2);

        // Toggles (on/off state) get one full-width row each so long EN/VI captions
        // fit; one-shot actions stay in two columns. Anything still too long shows
        // the ellipsis tooltip (UpdateTruncationTooltip).
        auto placeSection = [&](std::size_t sectionIndex, std::initializer_list<int> ids,
                bool fullWidth, bool withHeader = true) {
            x = leftEdge;
            if (!withHeader) y -= sectionControlGap - rowGap;
            if (withHeader && sectionIndex < settingsSectionLabels_.size()) {
                HWND label = settingsSectionLabels_[sectionIndex];
                if (label) {
                    PlacePaneChild(label, leftEdge, y, availableWidth, sectionHeight,
                        DipToPx(kButtonRadiusDip), PaneChildRegion::Antialiased);
                }
            }
            if (withHeader) y += sectionHeight + sectionControlGap;
            int used = 0;
            int rowY = y;
            for (int id : ids) {
                auto it = controls_.find(id);
                if (it == controls_.end()) continue;
                const int buttonWidth = fullWidth ? availableWidth :
                    std::min(columnWidth, FlyoutButtonWidthPx(flyoutMeasureDc, id, lp::kSettingsButtonFallbackWidthDip));
                if (used != 0 && x + buttonWidth > rightEdge) {
                    x = leftEdge;
                    rowY += panelRowHeight + rowGap;
                }
                PlacePaneChild(it->second, x, rowY, buttonWidth, panelRowHeight,
                    DipToPx(kButtonRadiusDip), PaneChildRegion::Antialiased);
                x += buttonWidth + columnGap;
                used++;
            }
            y = rowY + panelRowHeight + sectionControlGap;
        };

        placeSection(0, { ID_SETTING_BADGE_STARS, ID_SETTING_BADGE_COLOR,
            ID_SETTING_BADGE_PICK, ID_SETTING_BADGE_PAIR }, true);
        placeSection(1, { ID_SETTING_COPY_MOVE_RAW_WITH_JPG, ID_SETTING_RAW_JPEG_PREVIEWS_ONLY }, true);
        placeSection(2, { ID_SETTING_SAFE_JPEG, ID_SETTING_METADATA_RAW,
            ID_SETTING_METADATA_LOSSY, ID_SETTING_METADATA_TIFF }, true);
        placeSection(3, { ID_SETTING_PERFORMANCE_HUD }, true);
        placeSection(3, { ID_SETTING_DIAGNOSTIC_LOG,
            ID_SETTING_CLEAR_MEMORY_CACHE, ID_SETTING_CLEAR_DISK_CACHE,
            ID_SETTING_HELP, ID_SETTING_ABOUT }, false, false);

        if (settingsSectionLabels_.size() > 4 && settingsSectionLabels_[4]) {
            PlacePaneChild(settingsSectionLabels_[4], leftEdge, y, availableWidth, sectionHeight,
                DipToPx(kButtonRadiusDip), PaneChildRegion::Antialiased);
        }
        y += sectionHeight + sectionControlGap;
        if (languageSelector_) {
            const int selectorWidth = std::min(DipToPx(lp::kLanguageSelectorMaxWidthDip), availableWidth);
            // No caption update here: SetWindowTextW on a visible BUTTON paints
            // it synchronously (WM_DRAWITEM) at this natural, unscrolled y,
            // over the status bar; the scroll pass then moves it with
            // SWP_NOREDRAW and those pixels were never invalidated (the
            // "second language selector"). RefreshLocalizedText and
            // ShowLanguageMenu own the caption.
            PlacePaneChild(languageSelector_, leftEdge, y, selectorWidth, selectorHeight,
                DipToPx(kButtonRadiusDip), PaneChildRegion::Rounded);
        }
    }

    void QuickSiftApplicationImpl::LayoutStatusBar(int width, int height) {
        const int outer = DipToPx(kOuterMarginDip);
        const int statusX = outer;
        const int statusY = height - DipToPx(kStatusHeightDip) + DipToPx(quicksift::ui::layout::status_bar::kTopOffsetDip);
        const int statusW = std::max(0, width - outer * 2);
        const int statusH = DipToPx(statusProgressText_.empty() ? quicksift::ui::layout::status_bar::kHeightDip :
            quicksift::ui::layout::status_bar::kHeightWithProgressDip);
        RECT statusBefore{};
        if (GetWindowRect(status_, &statusBefore)) MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&statusBefore), 2);
        const bool statusSizeChanged = statusBefore.right - statusBefore.left != statusW ||
            statusBefore.bottom - statusBefore.top != statusH;
        const bool statusMoved = statusSizeChanged || statusBefore.left != statusX || statusBefore.top != statusY;
        // Only touch the status HWND when its geometry changes: MoveWindow(TRUE) +
        // a redrawing SetWindowRgn on every layout pass repainted it twice per
        // resize step. Region and paint share kStatusRadiusDip (AA slack region;
        // PaintStatus fills the slack with the surface behind the bar).
        if (statusMoved) MoveWindow(status_, statusX, statusY, statusW, statusH, TRUE);
        if (statusSizeChanged)
            uiFramework_.ApplyAntialiasedControlRegion(status_, statusW, statusH, DipToPx(kStatusRadiusDip));
    }

    void QuickSiftApplicationImpl::LayoutControls(int width, int height) {
        if (!canvas_ || width <= 0 || height <= 0) return;

        // [Pane] timing: named segments of this pass (core/pane_timing.h).
        paneLayoutTiming_.Clear();
        auto segmentStart = std::chrono::steady_clock::now();
        auto mark = [&](std::wstring_view name) {
            const auto now = std::chrono::steady_clock::now();
            paneLayoutTiming_.Add(name, std::chrono::duration<double, std::milli>(now - segmentStart).count());
            segmentStart = now;
        };
        LayoutTitleControls(width, /*force=*/false); // skipped when its inputs are unchanged
        mark(L"title");

        // Hide every flyout section child EXCEPT the one being laid out (sections share
        // one content rect, so stale WS_VISIBLE children stacked). The kept section is
        // moved in place below (never hidden and re-shown: that flashed and reset hover).
        HideLeftPaneSectionContentExcept(LeftPaneShouldShowContent() ? activeFlyout_ : FlyoutPanel::None);
        mark(L"hide");

        LayoutCanvasArea(width, height);
        mark(L"canvas");

        const bool showPaneContent = LeftPaneShouldShowContent();
        leftPaneContentVisible_ = showPaneContent;
        LayoutFlyoutTabs(width, height, showPaneContent);


        PlaceFilmstripToggle(width, height); // shown in Single view only

        mark(L"tabs");
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
        if (showPaneContent && activeFlyout_ == FlyoutPanel::Folders && !IsRectEmpty(&flyout))
            LayoutFoldersSection(flyout);
        if (showPaneContent && activeFlyout_ == FlyoutPanel::Info && !IsRectEmpty(&flyout))
            LayoutInfoSection(flyout);

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
            if (activeFlyout_ == FlyoutPanel::Settings) LayoutSettingsSection(flyout, flyoutMeasureDc);
            else LayoutButtonFlowSection(flyout, flyoutMeasureDc);
        }
        auto closeFlyoutLayout = [&] {
            if (flyoutMeasureDc) {
                if (flyoutOldFont) SelectObject(flyoutMeasureDc, flyoutOldFont);
                ReleaseDC(hwnd_, flyoutMeasureDc);
            }
        };
        closeFlyoutLayout();
        mark(L"place");
        if (showPaneContent && !IsRectEmpty(&flyout)) ApplyPaneScroll(flyout);
        else paneNaturalRects_.clear();
        mark(L"scroll");

        if (showPaneContent) RedrawActiveFlyoutContents();
        else if (flyoutBackdrop_ && IsWindowVisible(flyoutBackdrop_)) {
            // Closing / content hidden: one buffered backdrop paint (no erase pass).
            RedrawWindow(flyoutBackdrop_, nullptr, nullptr,
                RDW_INVALIDATE | RDW_NOERASE | RDW_NOCHILDREN | RDW_UPDATENOW);
        }
        mark(L"compose");

        // Again after the pane compose so the toggle stays above raised pane children.
        if (reviewState_.Mode() == ViewMode::Single) PlaceFilmstripToggle(width, height);

        LayoutStatusBar(width, height);
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
        // The catalog record is the authoritative copy once known (search hydration or
        // an earlier visit); otherwise read the database once and publish it to the store.
        std::wstring comment;
        bool available = !path.empty();
        if (!path.empty()) {
            const auto index = catalog_.FindPath(path);
            if (index && catalog_.PhotoAt(*index).userCommentKnown) {
                comment = catalog_.PhotoAt(*index).userComment;
            } else if (auto stored = persistentCache_.TryGetPhotoComment(fs::path(path))) {
                comment = std::move(*stored);
                if (index) catalog_.EditPhoto(*index, [&comment](PhotoItem& photo) {
                    photo.userComment = comment;
                    photo.userCommentKnown = true;
                });
            } else {
                // 0.9.4.0 polish: the cache database is busy (writer, search
                // hydration, startup open). Never block the UI thread on it: keep the
                // field disabled (so an empty field cannot be saved over a real
                // comment) and retry shortly.
                available = false;
                SetTimer(hwnd_, ID_TIMER_USER_COMMENT_RETRY, 120, nullptr);
            }
        }
        SetWindowTextW(userCommentEdit_, comment.c_str());
        EnableWindow(userCommentEdit_, available);
        auto save = controls_.find(ID_SAVE_USER_COMMENT);
        if (save != controls_.end()) EnableWindow(save->second, available);
    }

    void QuickSiftApplicationImpl::SaveUserComment() {
        const std::wstring path = ActiveInfoPath();
        if (path.empty() || !userCommentEdit_) return;
        const int length = GetWindowTextLengthW(userCommentEdit_);
        std::wstring comment(static_cast<size_t>(std::max(0, length)), L'\0');
        if (length > 0) GetWindowTextW(userCommentEdit_, comment.data(), length + 1);
        // Store first (search/filters see it immediately via the metadata epoch), then
        // persist.
        if (const auto index = catalog_.FindPath(path)) {
            catalog_.EditPhoto(*index, [&comment](PhotoItem& photo) {
                photo.userComment = comment;
                photo.userCommentKnown = true;
            });
        }
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
        (void)self;
        if (message == WM_ERASEBKGND) {
            // No erase pass: DrawFlyoutBackdrop paints the complete opaque surface in
            // one buffered blit. Erasing to a solid colour first was a visible frame.
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
        case WM_ERASEBKGND:
            // PaintLibraryList paints the complete surface in one buffered pass.
            return 1;
        case WM_PAINT:
            if (wParam) break; // caller-supplied DC (print paths): default painting
            self->PaintLibraryList(hwnd);
            return 0;
        case WM_MOUSEWHEEL: {
            // Explicit wheel scrolling (the list has no native scrollbar).
            const int count = static_cast<int>(SendMessageW(hwnd, LB_GETCOUNT, 0, 0));
            if (count <= 0) return 0;
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            if (lines == WHEEL_PAGESCROLL || lines == 0) lines = 3;
            const int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            const int top = static_cast<int>(SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0));
            const int target = std::clamp(top - notches * static_cast<int>(lines), 0, std::max(0, count - 1));
            if (target != top) {
                SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
                SendMessageW(hwnd, LB_SETTOPINDEX, static_cast<WPARAM>(target), 0);
                SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const RECT thumb = self->LibraryListThumbRect(hwnd);
            RECT hit = thumb;
            InflateRect(&hit, self->DipToPx(4), 0);
            if (!IsRectEmpty(&thumb) && PtInRect(&hit, point)) {
                self->libraryScrollDragging_ = true;
                self->libraryScrollDragOffset_ = static_cast<int>(point.y - thumb.top);
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }
        case WM_LBUTTONUP:
            if (self->libraryScrollDragging_) {
                self->libraryScrollDragging_ = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
            if (self->libraryScrollDragging_) {
                self->libraryScrollDragging_ = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            if (self->treeScrollbarHot_ && !self->libraryScrollDragging_) {
                self->treeScrollbarHot_ = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        case WM_MOUSEMOVE: {
            const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (self->libraryScrollDragging_) {
                RECT client{};
                GetClientRect(hwnd, &client);
                const RECT thumb = self->LibraryListThumbRect(hwnd);
                const int count = static_cast<int>(SendMessageW(hwnd, LB_GETCOUNT, 0, 0));
                const int itemHeight = std::max(1, static_cast<int>(SendMessageW(hwnd, LB_GETITEMHEIGHT, 0, 0)));
                const int visible = std::max(1, static_cast<int>(client.bottom - client.top) / itemHeight);
                const int margin = self->DipToPx(4);
                const int travel = std::max(1, static_cast<int>(client.bottom - client.top) - 2 * margin -
                    static_cast<int>(thumb.bottom - thumb.top));
                const int desired = std::clamp(static_cast<int>(point.y) - self->libraryScrollDragOffset_ - margin, 0, travel);
                const int maxTop = std::max(0, count - visible);
                const int top = static_cast<int>(std::lround(static_cast<double>(desired) / travel * maxTop));
                if (top != static_cast<int>(SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0))) {
                    SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
                    SendMessageW(hwnd, LB_SETTOPINDEX, static_cast<WPARAM>(top), 0);
                    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            {
                RECT hotRect = self->LibraryListThumbRect(hwnd);
                InflateRect(&hotRect, self->DipToPx(4), 0);
                const bool hot = !IsRectEmpty(&hotRect) && PtInRect(&hotRect, point);
                if (hot != self->treeScrollbarHot_) {
                    self->treeScrollbarHot_ = hot;
                    if (hot) {
                        TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                        TrackMouseEvent(&track);
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
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
        case WM_KEYDOWN:
        case WM_CHAR: {
            // Keyboard navigation scrolls with ScrollWindowEx, which would shift the
            // painted border / overlay thumb with the rows: repaint the whole (buffered)
            // client once instead.
            SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(hwnd, nullptr, FALSE);
            return result;
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
        // Every left-pane surface (tabs, section buttons, labels, info panel) paints
        // in ONE off-screen pass over its slice of the pane frost, then one BitBlt.
        // Owner-draw painters used to draw straight to the screen DC in several
        // layers (corner fill, shadow, fill, sheen, border, text): every hover-fade
        // frame was visible mid-paint, which read as flicker.
        if (!paneItemBuffering_ && draw->CtlType != ODT_LISTBOX && draw->hwndItem &&
            draw->hwndItem != flyoutBackdrop_ && IsLeftPaneSurface(draw->hwndItem)) {
            paneItemBuffering_ = true;
            const HWND item = draw->hwndItem;
            uiFramework_.PaintItemBuffered(*draw,
                [&](HDC dc, const RECT& local) { PaintLeftPaneFrostBehind(item, dc, local); },
                [&](const DRAWITEMSTRUCT& buffered) {
                    DRAWITEMSTRUCT copy = buffered;
                    OnDrawItem(&copy);
                });
            paneItemBuffering_ = false;
            return TRUE;
        }
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
            uiFramework_.PaintSectionTitle(draw->hDC, draw->rcItem, Tr(L"USER COMMENTS"), Palette(),
                currentDpi_, uiFontSemibold_);
            return TRUE;
        }
        for (std::size_t i = 0; i < settingsSectionLabels_.size(); ++i) {
            if (draw->hwndItem == settingsSectionLabels_[i]) {
                const std::wstring title = Tr(
                    i == 0 ? L"THUMBNAILS" :
                    i == 1 ? L"RAW HANDLING" :
                    i == 2 ? L"METADATA HANDLING" :
                    i == 3 ? L"HELP & DIAGNOSTICS" : L"LANGUAGE");
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
        // One registry (ui_command_ids.h) for chevron paint and width reserve.
        return quicksift::ui::command_id::IsPopupMenuButton(id);
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
            (id == ID_SETTING_METADATA_RAW && (metadataDirectMask_ & MetadataDirectRaw) != 0) ||
            (id == ID_SETTING_METADATA_LOSSY && (metadataDirectMask_ & (MetadataDirectJpeg | MetadataDirectPng)) != 0) ||
            (id == ID_SETTING_METADATA_TIFF && (metadataDirectMask_ & MetadataDirectTiff) != 0) ||
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
        // Parent-coloured AA corners (paired with ApplyAntialiasedControlRegion in
        // layout): title-bar buttons sit on the title gradient, every other glass
        // button on the opaque panelStrong flyout surface.
        presentation.parentSurface = static_cast<std::uint32_t>(
            presentation.role == ButtonRole::TitleBar
                ? quicksift::ui::framework::MacOsUiFramework::TitleBarSurfaceColor(Palette())
                : Palette().panelStrong);
        // Pane buttons: the buffer already holds the frost slice behind the button
        // (OnDrawItem), so the AA corners blend into the real backdrop pixels.
        if (paneItemBuffering_ && IsLeftPaneSurface(draw->hwndItem)) presentation.parentSurface.reset();
        const bool truncated = uiFramework_.PaintButton(draw->hDC, draw->rcItem, presentation, Palette(),
            currentDpi_, uiFont_, uiFontSemibold_);
        UpdateTruncationTooltip(draw->hwndItem, truncated, presentation.text);
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
        presentation.parentSurface = Palette().windowBottom;
        uiFramework_.PaintStatus(draw->hDC, draw->rcItem, presentation, Palette(),
            currentDpi_, uiFont_, uiFontSemibold_);
    }



} // namespace quicksift::app
