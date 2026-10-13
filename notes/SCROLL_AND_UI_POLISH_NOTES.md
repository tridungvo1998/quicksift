# Scroll smoothness and UI polish notes

Tree: `QuickSift-0.9.3.1-filmstrip-final-repair`

## Root cause (Thumbnail View lag)

Fast scrollbar drag and wheel fling were still able to flood the decode pipeline:

1. **Wheel/keyboard scroll** always called `ScheduleVisibleWork()` with full Interactive enqueue for every newly visible cell. With ~1000 photos (especially Large/Huge cells) this started many decodes that were cancelled after a short grace, while completions and `ApplyThumbnailViewport` queue rebuilds competed with paints.
2. **Scrollbar drag** already had velocity tiers, but `ScheduleVisibleWork` returned early during drag and paint was cache-only, so moderate/slow drag modes never actually scheduled a rolling window. On release, a sudden full schedule dump could stutter.
3. **Size-independent concurrency**: 420 DIP cells cost far more decode work than 72 DIP cells but shared the same admission caps during motion.

Symptom: viewport jumped far while the UI waited on decode/cache work (up to ~10s on slow PCs).

## Scroll policy changes

Portable policy in `thumbnail_prefetch_policy`:

- `ScrollMotionPhase` = Settled / Coasting / Fling
- `ComputeScrollLoadBudget(velocity, thumbnailSizeDip, visibleEstimate)`
- `ConcurrentThumbnailDecodeCap(sizeDip)` — Huge→1 … Tiny→6
- `PrefetchPlanner::EffectiveVelocityPixelsPerSecond` — quiet window ~160ms so fling mode ends promptly after input stops

Wiring:

| Phase | UI | Decode |
|---|---|---|
| **Fling** (≥~950 px/s) | Cheap paint / placeholders; Present without vsync; skip heavy completion drain for thumbnails | loadMode 0, enqueue 0, publish viewport to cancel stale jobs |
| **Coasting** | Normal paint; skip shell-fallback creation while motion active | size-capped concurrent + enqueue; no predictive |
| **Settled** | Full paint | Clear scroll-motion admission; full visible then predictive |

Sources of motion: scrollbar drag **and** wheel/keyboard (shared `thumbnailScrollMotionActive_` + worker drag/load APIs + `SetThumbnailScrollConcurrentCap`).

Settle path: idle-quality timer and Settled budget clear admission, then `ScheduleVisibleWork` / `SchedulePredictivePrefetch` restore full priority.

Fling publish is coalesced (~32ms wheel throttle; drag uses 16ms render timer) so hundreds of `WM_MOUSEMOVE` events cannot thrash `ApplyThumbnailViewport`.

## UI polish

Centralized in `macos_ui_framework*` / `ui_models`:

- `EaseToward` / `Smoothstep01` motion helpers
- `PaintButton`: richer border + inner edge, soft top highlight, press inset scale, dual focus ring
- Button subclass: short hover fade timer (`ButtonHoverBlend`); press/focus invalidate
- Left pane backdrop open blend (`leftPaneOpenBlend_`) eases raised chrome instead of a hard pop
- Existing filmstrip opacity + sidebar tab blends now use `EaseToward`

No heavy per-frame bitmap effects.

## Files touched

- `src/review/thumbnail_prefetch_policy.h/.cpp`
- `src/work/background_work_engine.h`
- `src/work/background_performance_controller.cpp`
- `src/work/background_work_engine.cpp`
- `src/ui/viewport_prefetch_controller.cpp`
- `src/ui/navigation_input_and_session.cpp`
- `src/ui/main_window_chrome_and_messages.cpp`
- `src/ui/graphics_resources_and_image_cache.cpp`
- `src/ui/canvas_rendering_and_overlays.cpp`
- `src/ui/performance_policy_controller.cpp`
- `src/ui/framework/ui_models.h`
- `src/ui/framework/macos_ui_framework.h/.cpp`
- `src/ui/framework/macos_ui_framework_gdi.cpp`
- `src/ui/main_window_controls_layout_and_painting.cpp`
- `src/ui/document_and_menu_models.cpp`
- `src/app/quicksift_application_internal.h`
- `src/app/quicksift_application.cpp`
- `scripts/verify-source-portable.py`
- `CHANGELOG.md`, `USER_GUIDE.md`, `CODE_GUIDE.md`

Preserved: external-change history/metadata reconcile, left hover pane + vertical tabs, wrapping toolbars, filmstrip fixes.

## Residual risks

- Linux cannot run the Win32/D2D GUI; verified by source review + `verify-source-portable.py` only. Manual Windows check: ~1000-photo Thumbnail View, Tiny→Huge sizes, scrollbar fling vs slow drag, fast wheel, settle fill-in, button hover/press/focus, left-pane open ease.
- Very sparse cache after a long fling still shows placeholders until settle fills the viewport (intentional smoothness tradeoff).
- Coasting still allows 1–N decodes; pathological storage (remote/spinning) may need lower caps via existing storage profile paths.
- Hover fade uses a short per-button timer (16ms × ~110ms); leave is still instantaneous.
