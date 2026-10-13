# FaceLock multi-face: implementation notes (QuickSift 0.9.4.0)

Tree: `QuickSift-0.9.3.1-filmstrip-final-repair` (0.9.4.0 with all prior changes). Not zipped, not renamed.
Background proposal: `FACELOCK_MULTI_FACE_PROPOSAL.md`. Where the two differ, the user's decisions below win.

## 1. Behaviour

| Area | Behaviour |
|---|---|
| Default target | One face: that face. Several faces: the face whose centre is closest to the image centre. Distance uses pixel-proportional units (x scaled by width/height). Ties: larger area, then top, then left. Keys are quantized at 1e-6, so the order is deterministic. |
| Small-face eligibility | A face whose linear size is under 20% of the largest face is kept (outlined, clickable, quality-checked) but ordered after every larger face. A tiny background face at the centre therefore never becomes the default target. |
| Noise filter | Applied at detection. A box is dropped only when its short edge is under BOTH 8 px in the 512 px detection bitmap AND 1.5% of the image's short edge. |
| Outlines | Shown only while FaceLock is on, in Single and Compare. Very faint rounded outline: a white stroke at about 26% opacity over a dark under-stroke at 16%. The target gets 48%/28% and a slightly thicker stroke. A flagged face is tinted faint red. Outlines are drawn in image space under the bitmap's own transform, so they follow zoom, pan and rotation. |
| Click to frame | Press and release within 4 DIP inside the face box, inflated by 12% (minimum 6 DIP). The click makes that face the target and frames it at 70% of the pane along the limiting axis, centred. Zoom is limited to the 32x-Fit maximum, and Fit is used if the face is already larger than 70% at Fit. Dragging still pans. A double-click right after a face click is ignored, so it does not undo the framing with Fit/100%. |
| Compare / Sync | The click applies to the clicked pane. **Sync on:** the shared zoom follows the clicked pane's framing, and the shared face offset resets to 0, so every other pane centres on its OWN target face at the same physical magnification. **Sync off:** only the clicked pane's independent view changes. The other panes keep their own targets either way. |
| Persistence | The selection is stored per image in the cache DB as a normalized box (never an index), keyed by path plus fingerprint. Writes go off the UI thread through the existing deduplicating record-cache queue. On load it is matched to the re-detected faces: best IoU >= 0.30, else the nearest centre within 0.6 stored-face sizes with size ratio <= 2. Without a match, the default target is used. A fingerprint change (file edited elsewhere) drops it. A selection made in the session wins over the persisted one. |
| Red border | Shown when ANY face is out of focus, motion-blurred, or has closed eyes. Tooltip and status reasons read like "Face 2: eyes closed; Face 1: motion blur", with faces numbered left to right, then top to bottom. |
| View jump on result arrival | Unchanged (intended). |
| Wheel pivot fix | With FaceLock, the wheel now pivots on the target face's true on-screen position (`SourceToScreen`, clamped to the pane). The old `faceCenter * area` pivot was wrong under letterboxing, pan or rotation. This is a low-risk bug fix. |

## 2. Design choices to report

- **Gray8: implemented, with fallback.**
  - On the worker, `DetectFacesFastBgra` converts the existing BGRA buffer to Gray8 luma, using the same weights as the quality analyzer. It does this only if `FaceDetector::IsBitmapPixelFormatSupported(Gray8)`.
  - It then creates a Gray8 `SoftwareBitmap` and runs detection.
  - Any exception in that branch sets a process-wide `gGray8InputRejected` flag and falls back to the original Bgra8 path, in this call and in every later one.
  - Cost: one w*h byte buffer (about 256 KB at 512 px) and one linear pass.
- **Outlines only while FaceLock is on.** This keeps the plain viewer clean. To flip it, change the first `FaceLockEnabled()` guard in `DrawFaceOutlines` and `HitFaceOutline`.
- **Hit testing reads current state, never paint-time rectangles.** `HitFaceOutline` resolves the pane under the pointer, then `ViewForPath`/`ResolveView`, then `SourceToScreen` of each face's corners. This follows the review subsystem rule "no retained paint-time hit rectangles". The renderer and the hit test use the same placement math.
- **Thresholds** live in `core::FaceQualityThresholds` (`src/core/face_quality_policy.h`). The FaceLock geometry constants live in `src/core/face_policy.h`.

## 3. Per-face quality (worker only)

Implemented in `core::AnalyzeFaceQualityBgra`, which is pure and noexcept.

**Crop and resample.** The face box is cropped and box/bilinear-resampled to S = clamp(cropShortEdge, 64, 256) px of luma plus chroma.

**Skip rules.** A face is not judged (and so never flagged) when:
- **TooSmall:** the crop's short edge is under 64 px.
- **LowContrast:** robust contrast (p95-p5) is under 28.

**Defocus.** Detail is computed per 8x8 block from the Laplacian: `sqrt(p90^2 - min(p10^2, 0.5*p90^2)) / contrast * 100`. This is noise-floor-corrected and normalized by face size and contrast. **Blurry** if detail < **6.0**. The old primary-face test was equivalent to about 4.6, so this is more sensitive.
- Calibration: sharp real faces 13.9-40.8; Gaussian sigma 1 px blur 4.9-20.8; sigma 2 px blur 2-9.4.

**Motion blur.** Directional second-derivative energies are measured at 0/90/45/135 degrees, and anisotropy = 1 - min/max. **MotionBlur** if anisotropy >= **0.80** AND detail < **16**.
- Calibration: natural faces 0.46-0.70; synthetic 9-17 px motion 0.78-0.98.
- A structure-tensor coherence measure was tried and dropped because it did not separate the cases.

**Eyes closed.** Windows FaceDetector provides no landmarks, so this is a heuristic. It runs only when the crop is >= 96 px.
- The skin reference is the median luma/chroma of the central lower face.
- The check is skipped for monochrome images or very dark skin references (luma < 45 or chroma < 10).
- Each eye window (band 0.24-0.54 of the face height; x 0.14-0.47 / 0.53-0.86) counts as evaluable only if its dark coverage is between 1% and 40%.
- A window is "closed" if the sclera fraction is under **0.0005**. Sclera pixels are bright (>= 0.9 x skin luma) and low-chroma (<= 0.55 x skin chroma).
- **EyesClosed** requires BOTH eyes to be evaluable and closed.
- Calibration on 10 real portraits: 0/6 false positives on open eyes; 2/4 closed-eye recall. It deliberately favours missing a case over a false red border.

**Re-decode for small faces.**
- Faces that are TooSmall in the detection or quality preview are re-judged once on a larger decode. The size is max(1536, the edge needed for a 256 px crop), capped at 2048.
- The preview buffer is released before that decode.
- A failed re-decode leaves `faceBlurScanned` false, so the outcome is TransientFailure and the existing retry/backoff policy applies.

**Aggregation.** `faceBlurry` = any `FaceQualityFlagged` face; `faceSharpness` = the default (faces[0]) face's score; `faceQualities` runs parallel to `faces`.

**Expected false positives:**
- sunglasses or dark glasses (no sclera visible);
- profile and three-quarter faces (eye windows misplaced);
- strong squints and laughing eyes;
- very warm or dim tungsten light;
- heavy eye makeup;
- low-resolution or very compressed faces (blur).

Faces under 64 px are skipped entirely.

**Expected misses:** closed eyes with bright eyelids or highlights, small faces, and monochrome images (the eye check is skipped).

## 4. Persistence and schema v2

**New columns in `image_records`:**
- `face_quality BLOB`: `FaceQualityRecord[]`, 16 bytes each (sharpness, anisotropy, eyeOpenness, flags), parallel to `faces`.
- `face_quality_version INTEGER NOT NULL DEFAULT 0`.
- `face_selection BLOB`: one `NormalizedFaceRect`, 16 bytes.

**Versioning.** `kDatabaseSchemaVersion = 2`.
- A v1 database is migrated in place by `PersistentCache::MigrateSchema(1)`: `BEGIN IMMEDIATE`, the column-existence-checked `ALTER TABLE ... ADD COLUMN` statements, `PRAGMA user_version=2`, `COMMIT`.
- Any other mismatch, or a failed migration, rebuilds the disposable cache as before.

**Lazy re-analysis.** A row whose `face_quality_version` differs from `kFaceQualityAnalysisVersion` (2), or whose blob does not line up with `faces`, is read back with `faceBlurScanned=false` and `faceBlurry=false`. The existing scheduling (`!faceBlurScanned && faces non-empty`) then re-analyzes it when the photo is next visible or active. Migrated v1 rows have version 0.

**Upsert rules:**
- Quality follows the existing face rule: it is kept while the fingerprint is unchanged and the write carries no face scan.
- The selection uses a bound "selection known" flag (`?26`). Without knowledge, the stored selection is kept while the fingerprint is unchanged. Any fingerprint change drops it.

**Validation.** Blob sizes must be multiples of 16 and must match the face count. Values must be finite, flags must fall within `KnownMask`, and the selection must be a valid normalized rect. The upsert also sanitizes records before writing.

**SQL harness.** The real upsert, migration and lookup SQL were extracted from the C++ source and run against SQLite in Python, covering:
- v1 → migrated (qv 0);
- quality write;
- selection write;
- metadata-only write (everything preserved);
- fingerprint change (faces, quality and selection dropped);
- fresh v2.

## 5. Files changed

**New:**
- `src/core/face_quality_policy.{h,cpp}`
- `scripts/checks/face_policy_checks.cpp`

**Core and policy:**
- `src/core/face_policy.{h,cpp}`: rewritten.
- `src/core/app_types.h`: PhotoItem/WorkResult gain `faceQualities` and the `faceSelection` fields.
- `src/persistent_cache.{h,cpp}`: `FaceQualityRecord`/`FaceQualityFlag`, record fields, schema v2, migration, bind/lookup/validation.
- `src/review/view_transform_policy.{h,cpp}`: `SourceToScreen`, `FrameSourceRect`.

**Detection and worker:**
- `src/face_detector.cpp`: Gray8 with Bgra8 fallback, noise filter, `RankFacesForAnchor`.
- `src/work/background_work_engine.cpp`: per-face quality with the bounded larger re-decode, re-ranking of cached faces, selection and qualities in the result. `FaceSharpnessResult sharpness` / `result->faceBlurScanned = sharpness.scanned` are kept.

**UI:**
- `src/ui/view_state_controller.cpp`: `TargetFaceIndexForPath`, `PrimaryFaceCenter` (now the target face), `HitFaceOutline`, `SelectFaceAndFrame`.
- `src/ui/canvas_rendering_and_overlays.cpp`: `DrawFaceOutlines` in Single and Compare.
- `src/ui/framework/macos_ui_framework.h`, `macos_ui_framework_d2d.cpp`: `FaceOutlinePresentation` and `PaintFaceOutline` (central renderer rule).
- `src/ui/graphics_resources_and_image_cache.cpp`: outline brushes (optional; released with device resources).
- `src/ui/navigation_input_and_session.cpp`: pending face click on press, travel cancel, select-and-frame on release, double-click suppression, wheel pivot via `SourceToScreen`.
- `src/ui/main_window_chrome_and_messages.cpp`: capture loss cancels a pending face click.
- `src/ui/catalog_loading_and_result_integration.cpp`: merges qualities and the selection (the session wins).
- `src/ui/metadata_history_and_file_safety.cpp`: `PersistPhotoRecord` writes qualities (with version) and the selection when known.
- `src/ui/app_theme_documents_and_localization.cpp`: `FaceWarningReasonText`, a per-hover tooltip text update, and the new red-border text.
- `src/ui/popup_menus_commands_and_folder_tree.cpp`: status reason for the active photo.
- `src/ui/face_analysis_controller.cpp`, `src/localization.cpp`: new EN/VI strings and in-app help (no more "largest face").
- `src/app/quicksift_application_internal.h`: declarations and members (`pendingFaceClick_`, `lastFaceClickTick_`, outline brushes). None of the forbidden legacy names are used.

**Build and verification:**
- `CMakeLists.txt`: core module added.
- `scripts/verify-source-portable.py`, `scripts/verify-source.ps1`: the "current-only schema" guard becomes a "versioned migration" guard. `ALTER TABLE image_records` is allowed only inside `MigrateSchema`, and `kDatabaseSchemaVersion = 2` plus the new columns are required. New multi-face, quality and worker guards were added, and `face_quality_policy.cpp` is a required module.

**Docs:**
- `CHANGELOG.md` (0.9.4.0), `USER_GUIDE.md`, `ARCHITECTURE.md`, `src/review/README.md`, `TESTING.md`, `PRE_RELEASE_CHECKLIST.md`.

## 6. Verification (Linux box)

- `python3 scripts/verify-source-portable.py --source-dir .`: **passed**.
- Portable CMake build (`QuickSiftCore`, `-Wall -Wextra -Wpedantic -Werror`): **passed**.
- `scripts/checks/face_policy_checks.cpp`: **49/49 passed**. It also compiles warning-free with `-Wshadow -Wconversion`.
- MinGW-w64 `-fsyntax-only -Wall -Wextra -Wshadow` on every changed Windows translation unit: no new diagnostics.
  - The only diagnostics are pre-existing: the `WINDOWPLACEMENT` initializer, HRESULT sign-compare, a `context` shadow, and `background_work_engine.cpp` WIC symbols that MinGW headers lack, all before line 700. There are no errors in the edited worker regions.
  - The C++/WinRT branch of `face_detector.cpp` (Gray8) could not be compiled here because no WinRT headers are available, so it needs the MSVC build.
- SQL harness for the migration, upsert and lookup: all scenarios behaved as designed.

## 7. Risks to test on Windows

1. **Gray8 path (MSVC/WinRT compile and runtime).** Confirm detection results match the old Bgra8 path on a few multi-face images. Watch the log for silent fallback; the flag is process-wide.
2. **Box geometry vs eye bands.** WinRT FaceBox framing (forehead/chin margins) may shift the eye band. Check a few frontal portraits: false "eyes closed" warnings mean the band constants need to move.
3. **Memory.** A 512 px detection job whose faces are tiny may re-decode at up to 2048 px (~17 MB). That exceeds its 8 MB face reservation; the old 1536 re-decode already did. Quality-only jobs (1536 target, ~38 MB reservation) are covered.
4. **Migration on real user DBs.** Open an existing 0.9.x cache: expect the log line "Persistent cache schema migrated from v1 to v2". There should be no rebuild and ratings should stay cached. Also try a DB held by a second instance (busy → it falls back to a rebuild, as with any schema mismatch).
5. **Lazy re-analysis after upgrade.** Every photo with faces gets one quality-only job (1536 decode) when it becomes visible or active. Watch thumbnail-scroll smoothness on large folders right after the upgrade.
6. **Outline alignment** at 90/180/270 rotation, letterboxed panes, Compare panes of different sizes, high DPI, and while tiles refine.
7. **Click vs pan.** A 4 DIP threshold on touchpads and pens. A drag that starts on a face must pan; a slight jitter still counts as a click.
8. **Double-click.** A double-click on a face frames it, and a double-click on empty image still toggles Fit/100%.
9. **Sync.** Click a face in pane B with Sync on: B is framed and the other panes keep their own faces at the same physical zoom. With Sync off, only B changes.
10. **Thresholds.** Blur (6.0) is more sensitive than before, so expect more red borders on soft but acceptable portraits. Motion (0.80 / 16) and eyes (0.0005) are conservative. Tune them only in `FaceQualityThresholds`, and bump `kFaceQualityAnalysisVersion` after changing semantics so cached verdicts are re-analyzed.
11. **Persisted-selection lifetime.** It matches face boxes: an in-session edit by another app does not clear it until faces are re-detected. Cache eviction (180 days / count cap) also drops it.
