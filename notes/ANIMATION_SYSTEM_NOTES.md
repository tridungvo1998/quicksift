# QuickSift 0.9.4.0 — Centralized animation system notes

Tree: `/workspace/quicksift/QuickSift-0.9.3.1-filmstrip-final-repair` (not renamed, not zipped).
Spec: `docs/DESIGN_SYSTEM.md` → "Animation system". Code: `src/ui/framework/ui_animation.{h,cpp}`.

## Inventory of transient UI

| Element | Before | Now | Preset |
|---|---|---|---|
| Dropdown menus (sort, filter, ratings, overflow…; `TrackDropdown`) | popped instantly | reveal on open, fade on close (constant layered alpha, click-through while fading) | `DropDownReveal(up/down)` + Fade-out 120 |
| Toasts | instant show, ad-hoc fade, `ID_TIMER_TOAST` | slides up 8 DIP + fades; exit lands as it expires; queue works | `SlideFade(Up, 8)` (180 in / 250 out) |
| Glass alerts (confirm, 2/3-button, ratings chooser, info) | instant | veil + card pop in; fades out with the last content | `PopScale` (0.96→1, 180 / 120) |
| Search field + match count | ad-hoc `StepToward` | fade (no slide: native edit HWND is fixed) | `Fade` |
| Filmstrip | ad-hoc `StepToward` | fade | `Fade(180, 180)` |
| Left pane open/close | ad-hoc progress + Smoothstep | same maths on Animator channel | `Toggle(180)` |
| Sidebar tab highlight | ad-hoc progress | Animator channel | `Toggle(120)` |
| Button hover | per-button `SetTimer`, fade-in only | Animator, fades in **and** out | `Toggle(120)` + Smoothstep in paint |
| Thumbnail placeholder → image | hard cut | crossfade only when a placeholder was painted; never during scroll/fling; ≤48 concurrent | `Crossfade` |
| Face outlines | private Smoothstep | central curve; instant hide under reduced motion | 250 ms Smoothstep (policy unchanged) |

## Intentionally static

| Element | Why |
|---|---|
| Tooltips | comctl32 already fades them per system setting |
| Native `TrackPopupMenu` (context menus) | system menu animation; left alone per request |
| Document, Batch form, missing-files/unmatched windows | DWM open/close transition already; a layered fade would kill Mica + DWM corners |
| Status bar text | updates constantly; a fade reads as lag |
| Red border, badges, selection highlight | truthful state, must be instant (Finder-like); per-card tracking cost |
| Single ↔ Compare swap, face jump | would need to keep the previous bitmap; the jump to the face on results is INTENDED |
| EXIF/info panels, left-pane section switch | child HWNDs: fading risks flicker and z-order/stacking regressions |
| Performance HUD | debug overlay |
| Caret blink, dropdown maintenance pump | not animations |

## Animator design

- `Animator` registry: `ElementId` (Space in top 8 bits: Chrome, FlyoutTab, Button, Window, Thumbnail, Popup) → entry {value, from, target, start, duration, curve, surface, window, completion, sequence, transient}.
- Presets map one presence scalar to opacity/offset/scale/reveal via `Resolve()`; durations are motion tokens (static_asserted).
- `AnimateTo`: starts from current value; idempotent for same target; proportional duration on reversal; reversal drops old completion; disabled/suspended → instant + completion now; always marks the surface dirty.
- `Tick(now)`: update all values, then run completions ordered by (due time, start seq); completions may chain; transient entries are forgotten on landing; returns surface mask, `DirtyWindows()` deduplicated.
- Driver: existing `ID_TIMER_ANIMATION`. Activation hook → 16 ms instantly; `UpdateAnimationCadence` → 33 ms when idle. No new timers. Dropdown modal loop has its own local Animator pumped by `MsgWaitForMultipleObjectsEx(16 ms)`.
- Reduced motion (`SPI_GETCLIENTAREAANIMATION`) → disabled (instant end states, dropdown not layered). Minimized/hidden → suspended.
- Safety: buttons `Remove` on `WM_NCDESTROY`; fading windows are `WS_EX_TRANSPARENT|WS_EX_NOACTIVATE` and destroyed on completion; `ShutdownAnimations` on main `WM_DESTROY`; destructor never runs completions.
- Cost: no per-frame regions (pane quantization untouched), no blur, no layout in paint, alert opacity layer bounded to the card and only while < 1, one UPDATENOW rule untouched, one invalidation per surface per frame.

## Verification (Linux)

- `cmake … -DQS_WARNINGS_AS_ERRORS=ON` build: clean.
- All 12 check programs pass, incl. new `animation_checks` (78 checks: easing ends/monotonic/bounds, token durations, reversal continuity + proportional duration, toggle no-jump, reduced-motion and suspended paths, completion ordering, reversal drops completion, chaining, batching, transient forgetting, dirty-window dedupe, activation hook, FinishAll, destructor safety).
- `scripts/verify-source-portable.py` passes incl. new `verify_animation_contracts`; `check-localization.py` and `--self-test` pass.
- MinGW-w64 compile of every changed Win32 file: OK (only the known D2DERR_RECREATE_TARGET sign-compare note).

## Windows eyeball list

1. Win11: dropdown keeps rounded corners while layered and fading (kill switch `kLayeredPopupTransitions` in `macos_ui_framework_popup.cpp`).
2. Dropdown drop shadow (CS_DROPSHADOW) during fade — does it lag/pop?
3. Focus returns to the owner after a dropdown closes; clicks pass through the fading menu.
4. Selecting an item mid-reveal fades out from the current alpha (no flash to opaque).
5. Alert pop scale looks crisp (text not blurry at 0.96); buttons not clickable during fade-out.
6. Toast queue: back-to-back toasts; toast under an alert fades away.
7. Button hover fades out on mouse leave; no stuck highlight after hide.
8. Thumbnail crossfade: no cost/flicker while scrolling or flinging the grid.
9. Reduced motion (Settings → Accessibility → Animation effects off): everything instant, dropdown not layered.
10. Minimize during an animation, restore: final state, no stale frames.
11. Idle: timer back to 33 ms (CPU at rest).
12. Left pane open/close and hover look identical to before; no left-pane flicker or stacking change.
