# QuickSift 0.9.4.0 — UI polish pass (macOS / Apple glass)

Tree: `/workspace/quicksift/QuickSift-0.9.3.1-filmstrip-final-repair` (not renamed, not zipped).
Preserved on purpose: view jumps to the face when results arrive, left-pane hover behaviour,
left-pane z-order and `SWP_SHOWWINDOW` rules, region quantization, one `UPDATENOW` per frame,
English-only export format. Line numbers are from the tree after this pass.

## 1. Flicker / repaint

| # | File:line | Symptom | Fix |
|---|---|---|---|
| F1 | `src/ui/app_theme_documents_and_localization.cpp:511-531` (ApplyTheme) | Theme/accent switch flushed child windows several times (visible multi-step repaint) | Invalidate everything first, keep `RedrawActiveFlyoutContents`' ordered compose, then exactly one `RDW_UPDATENOW \| RDW_ALLCHILDREN` (verifier counts it) |
| F2 | `src/ui/document_and_menu_models.cpp:246, 346-370` | Document window painted chrome in `WM_ERASEBKGND` and again unbuffered in `WM_PAINT` (flash on resize) | One double-buffered `PaintDocumentChrome` in `WM_PAINT`, `WM_ERASEBKGND` returns 1 |
| F3 | `src/ui/app_theme_documents_and_localization.cpp:89` | Document chrome painted over the rich edit then the edit repainted (flash) | `WS_CLIPCHILDREN` on the document window |
| F4 | `src/ui/main_window_controls_layout_and_painting.cpp:1594` | Empty Library list: default paint on a stale background, then text painted on top | Own buffered paint (solid fill, panel, centred localized text, 12 DIP inset) when the list is empty |
| F5 | `src/ui/main_window_controls_layout_and_painting.cpp:1404-1408` | Status bar region rebuilt and moved on every layout pass | `MoveWindow` only when moved, region only when resized |
| F6 | `src/ui/main_window_chrome_and_messages.cpp:1049` | Layout / region work while minimized (0×0 client), followed by a full relayout flash on restore | `WM_SIZE` returns early on `SIZE_MINIMIZED` (same in the document window, `document_and_menu_models.cpp:517`) |
| F7 | `src/ui/main_window_controls_layout_and_painting.cpp:210` (OnDpiChanged) | Moving across monitors painted one frame with old-DPI fonts in new geometry | Fonts/resources rebuilt first, badge cache cleared, then `SetWindowPos` |
| F8 | `src/ui/framework/macos_ui_framework.cpp:480-481` | Caret blink timer ran at a fixed rate even when the user disabled blinking | `GetCaretBlinkTime`; no timer at all when `INFINITE` |

## 2. Misshapen elements

| # | File:line | Symptom | Fix |
|---|---|---|---|
| S1 | `main_window_controls_layout_and_painting.cpp:1408`, `framework/macos_ui_framework_gdi.cpp:398-403` | Status bar: region radius 17 vs paint radius 18, buffer started black → dark corner specks | Shared `kStatusRadiusDip`, AA slack region, paint fills `parentSurface` (windowBottom) first |
| S2 | `document_and_menu_models.cpp:270-279` | Square rich-edit corners poked through the radius-14 panel (inflated only 1 px) | Panel radius Small (6) inset 2 DIP (> r·(1−1/√2)); rounding the edit itself rejected (clips native scrollbar) |
| S3 | `main_window_controls_layout_and_painting.cpp:554, 579, 785` | Left pane: region radius and painted radius drifted apart while animating (edge halo) | `LeftPaneBackdropRadiusDip()` (Smoothstep 6→20, quantized to 2 DIP) used by both region and paint |
| S4 | `canvas_rendering_and_overlays.cpp:582, 632` | Square thumbnail placeholders inside rounded cards | `PaintImagePlaceholder` with `ConcentricRadius(kCardRadiusDip, 4)` |
| S5 | `canvas_rendering_and_overlays.cpp:1078-1084` | Badges sized by `chars × 7` → clipped or off-centre text, non-capsule ends | Measured DirectWrite width (cached per text, ≤256 entries), `max(h, ceil(w)+12)`, capsule radius, centred |
| S6 | `framework/macos_ui_framework_d2d.cpp:54-95` | Toast text left-biased and not vertically centred; ad-hoc radius | Glass surface `kToastRadiusDip`, padding 16/12, 24 from the edge, centred layout |
| S7 | `framework/macos_ui_framework_d2d.cpp:371-448` | Alert: soft glow bled outside the card; square-ish buttons | Snapped glass card (elevation 2.5, `kAlertRadiusDip`), no glow, capsule pills with hairlines |
| S8 | `framework/macos_ui_framework_d2d.cpp:115`, `:496+` | Hairlines straddled pixel edges (blurry 2 px greys) on cards, labels, toasts | `SnapToDevicePixels` + strokes inset by half their width |
| S9 | `framework/macos_ui_framework_gdi.cpp:236` | `PaintCenteredMutedText` filled a hard rectangle over rounded parents | No fill; `DT_END_ELLIPSIS` |
| S10 | `framework/macos_ui_framework_gdi.cpp` (menu items), `macos_ui_framework_popup.cpp:129-131, 380-382` | Menu selection was a full-bleed square bar; popup radius one-off | Rounded inset (4 DIP) selection `kMenuSelectionRadiusDip`; popup `kPopupRadiusDip` for both DWM and Win10 region |
| S11 | `framework/macos_ui_framework_gdi.cpp`, `d2d` scrollbars | Scrollbar thumbs with fixed radius looked lozenge-shaped at small sizes | Capsule radius (`CapsuleRadius`) in GDI and D2D |
| S12 | `main_window_controls_layout_and_painting.cpp:1145, 1390`; `canvas_rendering_and_overlays.cpp:456`; gdi `:315` | One-off radii (filmstrip toggle, HUD, labels, compare labels) | `kCompactControlRadiusDip`, `kOverlayPanelRadiusDip`, `kLabelRadiusDip`, `ConcentricRadius(14, 6)` |
| S13 | `framework/macos_ui_framework_gdi.cpp:27, 62, 426` | Mixed icon stroke weights (1, 1.2, 2 px, DPI-unaware) | `StrokePx(kIconStroke=1.5 DIP)` everywhere |
| — | `framework/macos_ui_framework_gdi.cpp:539` | Vertical tab 2 px border | **Kept** on purpose: it covers the aliased region staircase; documented |

## 3. Behaviour

| # | File:line | Symptom | Fix |
|---|---|---|---|
| B1 | `framework/macos_ui_framework.cpp:945, 999-1016` | Button stayed "hot" after being hidden, disabled under a modal or losing capture | `ClearButtonHot` (kill timer, props, `TME_CANCEL`) on `SWP_HIDEWINDOW`, `WM_SHOWWINDOW FALSE`, `WM_ENABLE FALSE`; `WM_CAPTURECHANGED` invalidates |
| B2 | `framework/macos_ui_framework_popup.cpp:192, 212, 225` | Popup items highlighted/scrolled from pointer moves outside the menu (capture) | Hover/wheel only when the pointer is inside the client |
| B3 | `main_window_chrome_and_messages.cpp:981-983` | Keyboard focus lost after Alt+Tab | Save focused child on deactivate, restore on activate (if still visible/enabled) |
| B4 | `main_window_chrome_and_messages.cpp:989-992` | Accent / theme / high-contrast / animation / transparency changes ignored until restart | `WM_SETTINGCHANGE`, `WM_THEMECHANGED`, `WM_SYSCOLORCHANGE`, `WM_DWMCOLORIZATIONCOLORCHANGED` → `OnSystemVisualSettingsChanged` (ApplyTheme only when something changed or HC is on) |
| B5 | `document_and_menu_models.cpp:298, 496, 506` | Document window: title didn't dim when inactive; capture loss left the close light pressed; ignored `WM_DPICHANGED` | `WM_NCACTIVATE` invalidates the title strip; `WM_CAPTURECHANGED` shares the cancel path; suggested rect applied on DPI change |
| B6 | `framework/macos_ui_framework_gdi.cpp:88-121` | Press flashed the accent and white text (Windows-ish, jarring) | Subtle press: 1 DIP inset + darken (lighten in dark mode) |
| B7 | `framework/macos_ui_framework_gdi.cpp:127, 259, 554, 588` | Hard-coded white on accent unreadable with light system accents | `accentText` / `MenuSelectedTextColor` roles |
| B8 | `framework/macos_ui_framework.cpp:893, 974` | Hover had a linear step and a timer that could linger | `EaseInOutCubic` over 120 ms; timer only while animating, stops at the end |
| B9 | `main_window_controls_layout_and_painting.cpp` (section titles), `document_and_menu_models.cpp` (export), `src/localization.cpp` | Settings section titles, empty-library text and diagnostic-export messages English-only in Vietnamese | 9 new EN/VI pairs; checker sinks extended (`PaintSectionTitle`, `PaintCenteredMutedText`, `MessageBoxW`) |

## 4. Design-system refinements (centralized, no one-offs)

- `src/ui/ui_design_tokens.h` (new, CMake-registered): radii 6/10/14/20, capsule, concentric radii; spacing 4/8/12/16/24; strokes (hairline 1, icon 1.5, focus 2 + gap 1); type 9/10/11 pt with Segoe UI Variable Text/Display → Segoe UI; motion 120/180/250 ms, 16 ms frame, 33 ms idle; `EaseOutCubic`, `EaseInOutCubic`, `StepToward`; `GlassMaterial`; `VisualEffects`.
- `ui_layout_metrics.h`: every component radius maps onto a token (status, card, document panel, toast, alert, popup, menu selection, compact control, label, overlay panel, pane closed/open).
- `ui_design_system.cpp:169-201`: `ResolveThemePalette`, `ApplyAccentToPalette` (system accent, contrast-corrected), `HighContrastPalette`, `RefreshVisualEffects`, `UiFontFamily`, `StrokePx`.
- `framework/macos_ui_framework.cpp:654`: `ApplyWindowChrome` — sole owner of DWM attributes (verifier-enforced). Main: rounded, no DWM border, no Mica (opaque canvas). Fullscreen: unrounded. Document/Dialog: palette caption/border, Mica only while glass is allowed. Popup: small-round, blended border.
- `framework/macos_ui_framework_d2d.cpp:115, 180`: `PaintGlassSurface` and `PaintFocusRing`; `MeasureSingleLineText`, `PaintImagePlaceholder`.
- Focus rings: outside the shape in D2D surfaces with room; inside the bounds for child-window controls (accent edge, or `accentText` hairline when selected) → never clipped.
- Motion: filmstrip/pane Standard (180 ms), tabs/search/hover Fast (120 ms), toast in 120 ms ease-out, out 250 ms. Pane keeps symmetric Smoothstep (mid-flight reversal).
- Adaptive cadence (`src/app/quicksift_application.cpp:241`): same timer ID at 16 ms only while chrome animates, 33 ms otherwise. No new timers; no blur.
- Docs: `docs/DESIGN_SYSTEM.md` (new), CHANGELOG "UI polish", pointers in ARCHITECTURE.md, CODE_GUIDE.md, src/ui/README.md.

## 5. Accessibility

- High contrast (`SPI_GETHIGHCONTRAST`): `GetSysColor` palette, glass/shadows/sheen off, background blobs solid (`main_window_controls_layout_and_painting.cpp:243`), danger brush uses `dangerBorder` (`graphics_resources_and_image_cache.cpp:161`), DWM colours reset, focus ring opacity 1.0.
- `SPI_GETCLIENTAREAANIMATION` off: `StepToward`, hover blend and toast fade snap to their end state.
- `EnableTransparency` = 0: glass opaque (no sheen/highlight/ambient shadow), no Mica.
- All three refresh live.

## 6. Verification (Linux box)

- `python3 scripts/verify-source-portable.py --source-dir .` — passed (includes new `verify_ui_polish_contracts`).
- `scripts/check-localization.py` — 415 exact keys, no findings; `--self-test` passed.
- Portable `-Werror` CMake build — clean.
- All 11 check programs pass, including new `design_tokens_checks` (50 checks).
- MinGW `-fsyntax-only -Wall -Wextra -Wshadow` on every changed Windows file — clean except the known D2DERR sign-compare noise. Fixed along the way: two `-Wcast-function-type` warnings in `ui_design_system.cpp` (`GetProcAddress` via `void*`) and a `-Wshadow` in `RefreshOpenDocumentWindows`. Pre-existing, untouched warnings remain in `main.cpp`, `runtime_support.cpp` (cast-function-type) and `settings_filters_and_file_commands.cpp:787` (parentheses).
- Not possible here: an MSVC build and running the app.

## 7. Windows eyeball list

1. Focus rings: Tab through buttons/tabs/search/alert pills — ring visible, not clipped, inside child controls.
2. Status bar corners in light and dark — no dark specks, smooth AA.
3. Left pane open/close — radius 6→20 with no halo or edge mismatch mid-animation; hover behaviour unchanged.
4. Help/About/Diagnostic log — panel corners around the rich edit, no flash on resize, title dims when inactive.
5. Toasts and alerts — glass look, centred text, capsule buttons, no glow outside the card.
6. Badges — capsule, centred, long labels not clipped (EN and VI).
7. Menus/popups — rounded inset selection, readable selected text, no hover from outside; Windows 10 popup region.
8. Toggle High contrast, Animation effects, Transparency effects live — the UI follows without restart.
9. Change the system accent (light, dark, very light accents like yellow) — buttons/tabs readable.
10. Windows 10 (no Segoe UI Variable) — fallback fonts, no clipping.
11. Drag a document window between mixed-DPI monitors — geometry follows; fonts are still the main window's (not rescaled), check legibility.
12. Alt+Tab away and back — focus returns to the same control.
13. Minimize/restore — no relayout flash.
14. Idle CPU — the animation timer drops back to 33 ms once nothing animates.
15. Theme switch — single clean repaint, no child flash.
