# Pane + scroll hardening notes

Tree: `QuickSift-0.9.3.1-filmstrip-final-repair`

## 1) Left pane flicker — root cause

**Symptom:** Pane chrome opened, but tabs/buttons were invisible until the pointer hovered a control; the empty pane flickered during open/close.

**Root causes (confirmed in source):**

1. **Z-order bury.** All pane controls are *sibling* HWNDs of `flyoutBackdrop_`. `LayoutLeftPaneAnimationFrame` called `SetWindowPos(..., HWND_TOP)` on the backdrop every animation tick after content reveal. With `WS_CLIPSIBLINGS`, that put the opaque owner-draw backdrop above tabs/buttons, so they clipped away until something (hover invalidate / `SetActiveFlyout` → `LayoutControls`) raised them again.
2. **Hover-only repaint.** `ButtonSubclassProc` invalidates on `WM_MOUSEMOVE`, which matched “only visible on hover” once Z-order was wrong.
3. **Per-frame region churn.** `ApplyRoundedControlRegion` → `SetWindowRgn(..., TRUE)` every tick forced a full backdrop redraw, amplifying flash.
4. **Reveal gating.** Content waited for `kLeftPaneContentRevealBlend` (0.72) and could flip show/hide around the threshold; combined with (1) this looked like an empty open pane.

**Fixes:**

| Change | Intent |
|---|---|
| `PlaceLeftPaneBackdrop` inserts above `canvas_`, never `HWND_TOP` | Backdrop under content siblings |
| `RaiseLeftPaneContentAboveBackdrop` | Keep tabs/section controls above backdrop without ShowWindow |
| Skip `SetWindowRgn` when W/H/radius unchanged | Stop region-forced flash |
| `LeftPaneShouldShowContent` hysteresis (reveal 0.28 / hide 0.18) | Show once open enough; stay shown for the session |
| Full `LayoutControls` only on visibility flip | No discrete show/hide each animation frame |
| `RedrawActiveFlyoutContents` paints backdrop under children, then raises children | Consistent composite paint |

## 2) Scroll/drag lag — virtualized pattern

**Symptom:** Lag still appeared on heavy flings or multiple successive drags/flings; prior work only tuned thresholds.

**Prior gaps:**

- Wheel/keyboard still called `ScheduleVisibleWork` on every event (ApplyThumbnailViewport thrash even when throttled).
- Coast/paint path could enqueue via `GetOrRequestBitmap` while the schedule path also enqueued.
- Settle via generic idle timer could dump Interactive work between successive flings.
- In-flight off-viewport decodes kept a grace period during fling, so jumps left a backlog running.

**Correct photo-grid pattern now:**

```
input (wheel/drag/key)
  → update scroll offset + ObserveScroll
  → RequestThumbnailScrollFrame()   // cancels pending settle
        │
        ▼  (≤1× / 16 ms)
ProcessThumbnailScrollFrame()
  → InvalidateCanvas (placeholders / cache only)
  → Fling:   ApplyThumbnailViewport only if visible rows jumped (cancel stale; enqueue 0)
  → Coast:   size-capped enqueue once this frame
  → Settled: ArmThumbnailScrollSettle (~140 ms)
        │
        ▼
ID_TIMER_SCROLL_SETTLE
  → Clear motion admission → full visible enqueue + predictive
```

Size-aware caps (`ConcurrentThumbnailDecodeCap` / `ComputeScrollLoadBudget`) remain.

**Key code:**

- `src/review/thumbnail_prefetch_policy.h` — frame interval + settle debounce contract
- `src/ui/viewport_prefetch_controller.cpp` — frame pump, publish/enqueue split, settle arm
- `src/ui/graphics_resources_and_image_cache.cpp` — paint never enqueues during motion
- `src/work/background_work_engine.cpp` — fling cancels in-flight off-viewport immediately
- Timers: `ID_TIMER_SCROLL_FRAME`, `ID_TIMER_SCROLL_SETTLE` (+ existing drag render timer shares the frame processor)

## Files touched

- `src/ui/ui_layout_metrics.h`
- `src/ui/main_window_controls_layout_and_painting.cpp`
- `src/app/quicksift_application_internal.h`
- `src/app/application_support.h`
- `src/review/thumbnail_prefetch_policy.h` / `.cpp`
- `src/ui/viewport_prefetch_controller.cpp`
- `src/ui/navigation_input_and_session.cpp`
- `src/ui/main_window_chrome_and_messages.cpp`
- `src/ui/graphics_resources_and_image_cache.cpp`
- `src/work/background_work_engine.cpp`
- `CHANGELOG.md`
- `/workspace/quicksift/PANE_SCROLL_HARDENING_NOTES.md`

Preserved: external-change reconcile, filmstrip, left hover pane features, wrapping toolbars, size-aware decode caps.

## Residual risks

- Linux box cannot run the Win32/D2D GUI; verification is source review + `verify-source-portable.py` only.
- Manual Windows checks: open left pane and leave the pointer still (content must stay visible, no flicker); hover tabs/buttons; open/close ease; ~1000-photo Thumbnail View Tiny→Huge; rapid successive scrollbar flings and wheel bursts; settle fill-in after the last fling; no decode backlog after stopping.
- Content still appears after a low reveal blend (not a true per-control fade); acceptable vs opacity-per-child cost.
- Very sparse cache after a long fling still shows placeholders until settle (~140 ms + decode) — intentional.
- Coast still allows 1–N size-capped decodes; pathological remote/spinning storage may still stutter unless storage-profile caps are lowered further.
- `ID_TIMER_DRAG_RENDER` and `ID_TIMER_SCROLL_FRAME` both drive the same processor; only one should be armed at a time (guarded), but a future change that arms both could double-process a frame.

## Verifier

`python3 scripts/verify-source-portable.py --source-dir .` — passed.
