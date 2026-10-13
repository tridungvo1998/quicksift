# QuickSift design system (0.9.4.0)

The UI takes its cues from macOS and Apple's glass materials: quiet surfaces, concentric
corners, capsule controls, hairline borders, soft elevation and short, decelerating motion.
All values are tokens; feature code does not invent radii, durations, alphas or fonts.

## Owners

| Concern | Owner |
|---|---|
| Raw tokens (radii, spacing, strokes, type, motion, glass, effects policy) | `src/ui/ui_design_tokens.h` (constexpr, portable, unit-tested by `scripts/checks/design_tokens_checks.cpp`) |
| Component metrics (button radius, pane radius, toast radius, ...) | `src/ui/ui_layout_metrics.h`: every radius maps onto a token |
| Palette, accent, high contrast, visual-effects state, font family | `src/ui/ui_design_system.{h,cpp}` |
| Painting (glass, focus rings, pills, cards, menus, status) | `src/ui/framework/macos_ui_framework*.cpp` |
| Motion (tweening, presets, transitions, frame driver) | `src/ui/framework/ui_animation.{h,cpp}` (portable `Animator`, unit-tested by `scripts/checks/animation_checks.cpp`), driven by `MacOsUiFramework::TickAnimations` |
| Window chrome (DWM attributes, corners, Mica) | `MacOsUiFramework::ApplyWindowChrome` (the only `DwmSetWindowAttribute` caller; enforced by the verifier) |

## Tokens

- **Radii (DIP):** Small 6, Medium 10, Large 14, XLarge 20, plus `CapsuleRadius(h) = h/2`.
  - Small: menu selection, labels, document panel, closed left pane.
  - Medium: buttons, inputs, tabs, popups, compact controls, overlay HUD, pane content panels.
  - Large: grid and compare cards, toasts.
  - XLarge: panels, open left pane, status bar, alerts.
  - Capsule: pill buttons in alerts, search field, scrollbars, badges.
  - Nested surfaces use `ConcentricRadius(outer, inset)` (minimum 2), e.g. placeholders inside cards and compare labels.
- **Spacing (DIP):** 4 / 8 / 12 / 16 / 24 (`kSpace1..kSpace5`).
- **Strokes (DIP):** hairline 1, icon 1.5, focus ring 2 with a 1 DIP gap, selection 2 (3 when focused). GDI paths convert with `StrokePx(dip, dpi)`.
- **Type:** caption 9 pt, body 10 pt, headline 11 pt. The family resolves once at run time:
  `Segoe UI Variable Text/Display` when installed, otherwise `Segoe UI` (Windows 10).
  GDI+ cannot open the Variable named instances and always uses `kFamilyFallback`.
- **Motion:** fast 120 ms (hover, tab highlight, search field, dropdowns, crossfades, toast/alert exit),
  standard 180 ms (filmstrip, left pane, toast enter, alert enter), slow 250 ms (toast exit, face outline fade).
  Every animation runs on the central Animator (see "Animation system").

## Animation system

One system for every transient element: `quicksift::ui::animation` (`src/ui/framework/ui_animation.h`).

- **Registry:** `Animator` holds one entry per element id (`MakeId(Space, index)`: chrome singletons,
  sidebar tab slots, button HWNDs, top-level window HWNDs, thumbnail path hashes). Each entry tweens
  one scalar, normally the element's *presence* (0 hidden, 1 shown).
- **Presets** (`animation::presets`) map presence onto visual properties, with durations and curves
  from the motion tokens:

  | Preset | In | Out | Properties |
  |---|---|---|---|
  | `Fade` | 120 ms ease-out | 120 ms ease-in-out | opacity |
  | `SlideFade(dir, d)` | 180 ms spring-lite | 250 ms ease-in-out | opacity + `d` DIP travel |
  | `DropDownReveal(up)` | 120 ms ease-out | 120 ms | opacity + 4 DIP travel + clip reveal 85→100 % |
  | `PopScale` | 180 ms spring-lite | 120 ms | opacity + scale 0.96→1 |
  | `Crossfade` | 120 ms ease-in-out | 120 ms | opacity (small surfaces only) |
  | `Toggle(ms)` | linear | linear | reversible channel; the consumer applies its own symmetric curve |

  The framework names the preset per component (`kToastTransition`, `kAlertTransition`,
  `kSearchFieldTransition`, `kFilmstripTransition`, `kThumbnailTransition`, `kButtonHoverTransition`).
- **Curves:** `Linear`, `EaseOutCubic`, `EaseInOutCubic`, `Smoothstep`, `SpringLite` (critically damped
  response normalised to land exactly at 1, monotonic, no overshoot). All map 0→0 and 1→1 exactly.
- **Interruptible and reversible:** every trip starts from the current value, so reversing mid-flight
  never jumps; trips are proportional (a half-way reversal takes half the duration). Reversal drops
  the pending completion, so a cancelled hide never destroys anything.
- **Completions** run after all values of the frame are final, ordered by scheduled finish time then
  start order; they may start new animations (chains). Destroying the Animator never runs them.
- **Driver:** the existing `ID_TIMER_ANIMATION` timer only. `UpdateAnimations` sets targets, calls
  `MacOsUiFramework::TickAnimations` (which also syncs toast/alert presence, repaints per-window targets
  and applies layered-window alphas) and invalidates each app surface (canvas, pane frame, tabs) at most
  once. The activation hook switches the same timer to 16 ms the moment any animation starts; it drops
  back to the 33 ms idle poll when nothing is active. The one exception is `TrackDropdown`, whose nested
  modal loop cannot see that timer: it owns a local Animator and pumps reveal frames from its own loop
  with `MsgWaitForMultipleObjectsEx` (no timer).
- **Reduced motion and minimized:** `SPI_GETCLIENTAREAANIMATION` off → `SetEnabled(false)`: every value
  jumps to its target and completions run immediately; the dropdown is not even layered. Minimized or
  hidden main window → `SetSuspended(true)`: in-flight work finishes on the next tick.
- **Safe on destroy:** buttons remove their entry in `WM_NCDESTROY`; window fades are input-transparent
  and destroyed on completion; `ShutdownAnimations` (main `WM_DESTROY`) destroys fading windows and drops
  completions.
- **Cheap by construction:** one scalar per element, no per-frame regions (the pane keeps its 2-DIP
  radius quantization), no blur, no layout in paint (paint only reads values; the canvas frame only sets
  overlay targets), opacity layers bounded to the alert card and only while it moves, thumbnail
  crossfades only for cells that showed a placeholder, never during scroll motion/fling, at most 48 at once.
- **Top-level windows:** dropdowns fade with constant layered alpha (`LWA_ALPHA`, not per-pixel, so DWM
  keeps rounded corners; kill switch `kLayeredPopupTransitions`). Document, form and list windows keep
  DWM's own open/close transition: making them layered would disable Mica and the DWM corners.
  `AnimateWindow` is not used (it blocks and needs `WM_PRINTCLIENT` paths the custom painters do not have).
- **Rules (verifier-enforced):** feature code (`src/ui/*.cpp`, `src/app/*.cpp`) never calls
  `StepToward`/`EaseToward`/`Smoothstep01`/`EaseOutCubic`/`EaseInOutCubic`, `AnimateWindow` or
  `SetLayeredWindowAttributes`, and never starts an animation/fade/hover/toast timer; the framework's only
  timers are the caret blink and the dropdown maintenance pump.

## Colour

- `ResolveThemePalette` is the only palette entry point. Built-in light/dark palettes receive the
  system accent (DWM `AccentColor`):
  - light: the accent is darkened until white text reaches 4.5:1;
  - dark: the accent is lightened until it reaches 3:1 against `panelStrong`, and `accentText`
    picks the better of black or white.
- Semantic roles: `accent`, `accentBorder`, `accentText`, `menuSelected`, `menuSelectedText`,
  `danger*`, `panel*`, `text*`. Selected text never hard-codes white.

## Glass material

One recipe (`tokens::Glass(dark, effects)`, painted by `PaintGlassSurface`) for toasts, alerts and the
floating search field; buttons reuse its sheen/edge alphas:

1. Two-layer ambient shadow plus a tight key shadow offset downwards.
2. Body fill at 0.94–0.97 opacity.
3. Top-half sheen under a clip.
4. 1 px top specular highlight under a clip.
5. Hairline border inset by half its width (never straddles the device-pixel edge).

Geometry is snapped to device pixels first (`SnapToDevicePixels`). There is no per-frame blur: the
"frost" is solid colour and alpha only.

## Focus

- macOS-style ring: accent, 2 DIP, 1 DIP gap, 0.8 opacity (1.0 in high contrast).
- D2D surfaces with room (alerts, search field) draw it outside the shape with `PaintFocusRing`.
- Child-window controls cannot paint outside their own bounds, so their ring is drawn **inside**:
  an accent ring at the edge, or an `accentText` hairline inset by 2 px when the control is selected.
  A ring is never clipped.
- The main window remembers the last focused child on deactivation and restores it on activation.

## Shape rules

- A window region (`ApplyAntialiasedControlRegion`) and the paint for the same surface must use the same
  radius token; the region has AA slack and the paint fills the parent surface first so corner pixels
  never show black or a stale colour.
- Square native children (rich edit, scrollbars) sit inside a Small-radius panel inset 2 DIP, so square
  corners cannot poke through a large radius.
- Strokes are inset by half their width.
- The left pane radius animates 6 → 20 DIP and is quantized to 2-DIP steps, so the region and the paint
  always agree and the region is rebuilt at most a few times per transition.

## Window chrome

| Kind | Treatment |
|---|---|
| Main | Rounded corners, no DWM border (the app paints its own), opaque canvas, no Mica (the canvas is opaque, so Mica would only cost composition) |
| Fullscreen | `DWMWCP_DONOTROUND`, no border |
| Document / Dialog | Caption, text and border colours from the palette; Mica backdrop only while glass is allowed |
| Popup | Small-round corners, blended border, no backdrop; Windows 10 uses a region with `kPopupRadiusDip` |
| High contrast | All DWM colours reset to default |

## Accessibility

`CurrentVisualEffects()` caches three switches, refreshed on `WM_SETTINGCHANGE`, `WM_THEMECHANGED`,
`WM_SYSCOLORCHANGE` and `WM_DWMCOLORIZATIONCOLORCHANGED`. A full `ApplyTheme` runs only when something
changed (or high contrast is on).

| Setting | Effect |
|---|---|
| `SPI_GETHIGHCONTRAST` | System colour palette from `GetSysColor`, glass and shadows off, background blobs solid, default DWM colours |
| `SPI_GETCLIENTAREAANIMATION` off | The central Animator is disabled: every transition (pane, tabs, hover, toast, alert, dropdown, crossfades, face outlines) jumps to its end state |
| `Personalize\EnableTransparency` = 0 | Glass becomes opaque: no sheen, no highlight, one crisp shadow, no Mica |

## Repaint rules (unchanged invariants)

- `ApplyTheme` invalidates everything first, lets `RedrawActiveFlyoutContents` keep its ordered compose,
  then flushes with exactly one `RDW_UPDATENOW` (verifier-enforced).
- Left-pane z-order, `SWP_SHOWWINDOW` rules, region quantization and one `UPDATENOW` per frame are
  unchanged.
