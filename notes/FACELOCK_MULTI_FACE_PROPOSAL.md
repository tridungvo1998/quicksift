# FaceLock: what to do when several faces are detected

QuickSift 0.9.4.0. **Research and proposal only; no code in the tree was changed.**

Tree: `/workspace/quicksift/QuickSift-0.9.3.1-filmstrip-final-repair` (all paths below are relative to it).

---

## 1. How it works today (end to end)

### 1.1 Detector and where it runs

| Step | Where | What happens |
|---|---|---|
| Gate | `src/ui/face_analysis_controller.cpp:94-103` `EnqueueFaceAnalysis` | This is the only UI path that may enqueue face work (the verifier enforces it: `scripts/verify-source-portable.py:889`). Before scheduling it refreshes the capability and checks per-file backoff (`core/face_detection_policy.*`). |
| Priority | `face_analysis_controller.cpp:86-92` | FaceLock off → `Idle`. FaceLock on in Single/Compare → `Interactive`. Otherwise → `Face`. |
| Job | `src/work/background_work_engine.cpp:1527-1534` `EnqueueFace` | Creates a `JobKind::Face` job with target `max(kFaceDecodeSize=512, target)` (`app/application_support.h:227-228`). Face jobs are cancelled while the Thumbnail viewport is active (CHANGELOG:22, :375). |
| Decode | `background_work_engine.cpp:2638-2654` | Decodes a 512 px preview. It tries the bundled decoders, then WIC (EXIF orientation is applied, `:398-401`), then Shell. `preferEmbeddedPreview=true`, so RAW files use the embedded JPEG. |
| Detect | `src/face_detector.cpp:68-151` `DetectFacesFastBgra` | Each worker thread keeps one WinRT `FaceDetector` (`:31`, `:108`). The BGRA buffer is copied into a `SoftwareBitmap` (`Bgra8`, `:99-104`) and `DetectFacesAsync` is called. Each `FaceBox` is clamped and normalised to 0..1 (`:112-131`), with a cap of 512 faces (`:30`). Results are sorted by `core::FaceAnchorComesBefore` (`:135`). On any exception the result is cleared, the detector is reset, and the job returns `TransientFailure`. |
| Sharpness | `background_work_engine.cpp:751-835` `AnalyzePrimaryFaceSharpnessBgra` (call at `:2662-2690`) | Only `faces.front()` is analysed. The box is padded by 12 % and scored with Laplacian variance plus gradient RMS. If that crop is under 32 px, it re-decodes at 1536 px (`kFaceSharpnessDecodeSize`). |

### 1.2 Stored data

- `NormalizedFaceRect { float x, y, width, height; }` is defined at `src/persistent_cache.h:22-27` and is 16 bytes.
- **The detector provides no confidence, no landmarks and no identity.** WinRT `DetectedFace` only has `FaceBox`.
- `PhotoItem` (`src/core/app_types.h:41-45`) and `WorkResult` (`:143-153`) both carry:
  - `faces` (a vector, kept sorted);
  - `facesScanned`, `faceBlurScanned`, `faceBlurry`;
  - `faceSharpness`, which describes the **primary face only**.
- The face count is simply `faces.size()`; it is not stored separately.
- SQLite storage (`src/persistent_cache.cpp:321-367`, read path `:740-775`, sanitising `:827-840`):
  - `image_records.faces` is a raw BLOB of 16-byte structs, plus the scanned/blur/sharpness columns.
  - Reading validates `bytes % sizeof(NormalizedFaceRect)` and each rect.
  - `kDatabaseSchemaVersion = 1` (`:49`). A version mismatch throws the cache away (`:528-561`). There is also one precedent for an in-place `ALTER TABLE ... ADD COLUMN` (`:619`).
- When a result arrives, `catalog_loading_and_result_integration.cpp:677-693` merges it into the catalog. If it belongs to the active photo, it then calls `ApplyFaceLockForActive(false)`.

### 1.3 How the lock target is chosen

`src/core/face_policy.cpp:9-33` `FaceAnchorComesBefore` sorts faces by:

1. quantised normalised **area**, largest first;
2. distance from the centre of the frame, nearest first;
3. top edge;
4. left edge.

The comment at `face_detector.cpp:133-134` reads: "The largest face is the default anchor. This is deterministic and avoids expensive subject classification".

`src/ui/view_state_controller.cpp:65-73` `PrimaryFaceCenter(path)` returns the centre of **`faces.front()`**. There is no other selection logic anywhere in the code.

### 1.4 How Single and Compare use it

- **Rendering anchor** (`view_state_controller.cpp:93-104` `ViewForPath`):
  - Applies only if FaceLock is on **and** the view is zoomed beyond Fit.
  - The centre is set to `PrimaryFaceCenter + FaceOffsetForPath`. The offset is the user's pan relative to the face, clamped to ±1 (`review_state_model.cpp:21-25`).
  - At Fit or below, FaceLock has no visible effect.
- **Storing a pan** (`view_state_controller.cpp:114-131`): beyond Fit, it saves `offset = centre − face`. At Fit or below, it resets the offset.
- **Navigation:**
  - Changing path calls `RecenterFaceLockForPath`, which resets the offset and requests analysis if it is missing (`view_state_controller.cpp:139-153`, `navigation_input_and_session.cpp:975-982`, `:1151-1152`).
  - Activating a pane calls `ApplyFaceLockForActive` (`navigation_input_and_session.cpp:109-125`). This keeps the offset; by design a click never recenters (`src/review/README.md:79`).
- **Wheel zoom** (`navigation_input_and_session.cpp:775-792`): the zoom pivot is placed at `faceCenter × area size`.
- **Zoom-mode buttons and Reset** (`navigation_input_and_session.cpp:140-150`, `:1054-1063`): reset the offset and centre on the primary face.
- **Compare + Sync** (`review_state_model.cpp:233-252`, `:255-280`):
  - One `sharedFaceOffset_` is applied **relative to each pane's own `faces.front()`**.
  - With Sync off, each pane's offset lives in its `IndependentPaneState`.
  - Turning FaceLock off materialises every pane's view. If the synced panes had different face anchors, Sync is switched off (`:290-323`, documented at `src/review/README.md:49`).

### 1.5 Settings, UI and cache priority

- There is a single **Face Lock: On/Off** toggle (`ID_FACE_LOCK`):
  - command handler `popup_menus_commands_and_folder_tree.cpp:219-233`;
  - label and tooltip `face_analysis_controller.cpp:39-62`;
  - saved as the `FaceLock` INI value (`navigation_input_and_session.cpp:490, 555`).
- There are no settings for how multiple faces are handled.
- Thumbnails get a red border when `HasFaceFocusWarning` is true (`app_theme_documents_and_localization.cpp:257-259`, tooltip `:263` "the largest detected face …", drawn at `canvas_rendering_and_overlays.cpp:402`).
- While FaceLock is on, bitmaps that have faces are kept in the cache in preference to others (`graphics_resources_and_image_cache.cpp:455-467, 549-552, 563, 780, 865`).
- Face boxes are never drawn on the canvas.

### 1.6 What happens today when more than one face is found

1. Every face is stored, up to 512, in sorted order.
2. **Only `faces[0]` is ever used**: the largest face, with ties broken by distance from the centre. It drives the FaceLock centre, the wheel-zoom pivot, Reset/zoom-mode centring, the sharpness score and the red-border warning.
3. The user cannot see which face is locked, cannot switch to another one, and has no group option.
4. In Compare, each pane locks onto **its own** largest face, even if that is a different person in each pane. The shared Sync offset is then applied around those different anchors.

---

## 2. Gaps, bugs and ambiguities

### Prerequisite issues (to check before any multi-face work)

**P0-a: The pixel format may break detection entirely.**

- `face_detector.cpp:99-104` passes a **Bgra8** `SoftwareBitmap` to `DetectFacesAsync`.
- Microsoft's documentation says: "In the current version, the FaceDetector class only supports images in Gray8 or Nv12". It recommends calling `SoftwareBitmap::Convert(..., Gray8)` or checking `IsBitmapPixelFormatSupported`.
- If Bgra8 is rejected, the call throws. The `catch` turns that into `TransientFailure` on every photo, so FaceLock would quietly never find a face. The backoff policy and the "Face analysis paused" status would hide this.
- Nothing in the tree converts the format; `rg Gray8|Nv12|Convert` finds no matches.
- **Verify on a real device** first: either the warning log ("Face analysis failed … transient failure count") or a debugger.
- If it is confirmed, the fix is a single line: `SoftwareBitmap::Convert(bitmap, BitmapPixelFormat::Gray8)`, or better, build Gray8 directly from the BGRA luminance and skip the copy.

**P0-b: Late results can jump the view.**

- When a face result arrives after the user has already zoomed or panned (beyond Fit), `ApplyFaceLockForActive(false)` → `InvalidateCanvas` makes `ViewForPath` re-anchor to `face + (stale or zero offset)`.
- The image jumps in the middle of an inspection.

**P0-c: The wheel-zoom pivot math is wrong in some layouts.**

- `navigation_input_and_session.cpp:780-781` maps normalised source coordinates to the screen linearly across the pane.
- That ignores letterboxing, the current pan and user rotation. The pivot is then off-face, and `StoreEditableViewState` turns the error into a non-zero face offset.
- It works well enough at Fit when the image fills the pane, and drifts otherwise.

### Multi-face gaps

| # | Gap | Effect |
|---|---|---|
| G1 | "Largest wins" is a poor primary rule | Picks a large but cut-off face at the edge, a blurry foreground shoulder, or a bystander over the centred, in-focus subject. |
| G2 | Unstable choice when faces are nearly equal | Quantised area ordering means two similar faces can swap when detection runs at a different resolution, on another machine, or after the cache is rebuilt. The target can flip between sessions or between Compare panes. |
| G3 | No cross-pane identity in Compare | In a burst comparison, pane A can lock on person 1 and pane B on person 2. Sync then pans around different people, which defeats the purpose of comparing. |
| G4 | No size or confidence filter | No confidence is available, and there is no minimum size or plausibility check. Tiny background faces, posters, statues and patterns can become the target when they are the only "face". |
| G5 | Faces cut at the frame edge | These are not penalised. A clipped face at the border is a poor lock target and makes for a poor sharpness crop. |
| G6 | Group shots | No union or "fit all faces" mode. Locking one person in a group photo is rarely what the user wants. |
| G7 | No user control | No way to cycle, click to choose or reset. No visual feedback about which face is locked. |
| G8 | The blur warning only checks `faces[0]` | Its meaning depends on the primary rule. In a group shot, other blurry significant faces are invisible. The tooltip wording ("largest") would have to change along with the rule. |
| G9 | Detection resolution | Detection runs at 512 px on the long edge. Faces smaller than about 5–8 % of the long edge are likely missed, because the WinRT default `MinDetectableFaceSize` is unknown (not documented and not set in our code). This is a real limitation for group shots. |
| G10 | RAW embedded-preview geometry | Face boxes come from the embedded JPEG (`preferEmbeddedPreview=true`). Some cameras crop or scale that JPEG slightly differently from the rendered RAW, which gives small anchor offsets. |
| G11 | Mixed results in Compare | Panes that have faces anchor on a face, while panes without faces use the shared plain centre. With Sync on, the panes are effectively unsynchronised. This is acceptable, but it is not documented. |
| G12 | No tests | The CHANGELOG mentions "portable regression coverage", but the CTest suite was retired (`CMakeLists.txt:125-127`). Only the source-token verifier protects face logic. |

---

## 3. Proposed design

### 3.1 Principles

- **Keep the invariant that `faces[0]` is the automatic primary face.**
  - Ranking moves to a better pure scoring function in `core/face_policy`, still run **on the worker** inside `DetectFacesFastBgra`.
  - `PrimaryFaceCenter`, the sharpness analysis, the blur warning and the cache priority then keep working unchanged.
  - `FaceAnchorComesBefore` stays as the final tie-breaker, which also keeps the verifier happy.
- **User choice and Compare matching are resolved on the UI thread at view-resolution time.**
  - They are pure functions over at most a few dozen rects, which is negligible cost.
- **Store selection as a normalised rectangle, never as an index.**
  - It is then re-resolved by IoU, so it survives re-detection and reordering.

### 3.2 Candidate filtering (replaces the missing confidence)

All rules are pure functions in `core/face_policy`, with constants in one place:

- **Valid face:** the face's short edge is at least `0.025 × image short edge` and at least about 12 px in the detection bitmap. Smaller boxes are dropped at detection time.
- **Plausible face:** the box aspect ratio is between 0.6 and 1.6. WinRT boxes are nearly square, so anything outside that range is very likely a false positive.
- **Significant face:** area is at least `0.20 × largest valid face area`. Only significant faces can be the automatic primary, join group mode, or be matched across panes.
  - Non-significant faces are still stored and can be selected by cycling or clicking.
  - This keeps a tiny background bystander from being chosen automatically, while still letting the user pick it.
- **Clipped face:** the box comes within 0.5 % of any image edge. Clipped faces stay valid but are penalised in scoring.
- **Optional, phase 4:** a sharpness factor once per-face sharpness exists. False positives, such as textures, are usually low-contrast or smooth.

### 3.3 Deterministic primary-face score (worker-side)

```
size       = sqrt(area / largestSignificantArea)                    // 0..1, dominant term
centrality = 0.65 + 0.35 * (1 - min(1, dist(faceCentre, (0.5,0.45)) / 0.65))
                                                                     // 0.65..1; aim slightly above centre (rule-of-thirds headroom)
edge       = clipped ? 0.70 : 1.0
sharp      = 1.0   (phase 4: 0.80 + 0.20 * normalisedSharpness among the top 3)
af         = 1.0   (phase 4: 1.5 if the EXIF AF point/SubjectArea lies inside the padded box)
score      = size * centrality * edge * sharp * af
```

- The score is quantised to 1e-4 and compared. Equal scores fall back to `FaceAnchorComesBefore`, which keeps the result deterministic.
- **Hysteresis against flip-flopping (G2):** if the top two scores are within 8 %, prefer the larger face, which matches the old rule. A near-tie therefore resolves exactly as it does today, and results are stable across decode sizes.
- **Worked examples:**
  - A centred face beats a typical edge face (about 0.5 from the aim point) up to about 1.9× its area.
  - A clipped edge face needs about 3.8× the area of a centred face to win.
  - A face of a quarter the area (size term 0.5) never beats a large centred face.
- **Cost:** O(n log n) with n ≤ 512, typically n ≤ 10, run once per detection on a worker. This is negligible next to the decode.
- **Cached records** were sorted with the old rule. Re-rank them when they are read in `PersistentCache::LookupRecord` (worker thread, pure, about 1 µs). No schema change is needed for phase 1.

### 3.4 Lock-target resolution (UI thread, per pane)

`ResolveFaceAnchor(path, paneContext)` replaces `PrimaryFaceCenter` and resolves in this order:

1. **Group mode** (global toggle) with at least 2 significant faces → centre of the padded union box (§3.7).
2. **Explicit user choice** for this path (session map) → the face with best IoU ≥ 0.4 against the stored rect. If no face matches, fall through.
3. **Compare propagation** (Compare + Sync, and the active pane has a locked face) → the best match in this pane (§3.6). If there is no match above threshold, fall through.
4. **Automatic primary:** `faces[0]`.
5. **No faces, or faces not scanned yet** → `nullopt`, which gives today's ordinary non-FaceLock behaviour (§3.10).

The return value is `{centre, rect, faceIndex, source}`, where `source` is one of Group, User, Matched, Auto or None. The renderer uses `source` for the indicator.

### 3.5 User cycling and click-to-lock

These keys are unbound today; I checked `navigation_input_and_session.cpp` `OnKeyDown`.

| Input | Action |
|---|---|
| `]` / `[` | Move to the next or previous face **spatially**: left to right by centre x, then top to bottom. This is more predictable than score order ("the next person to the right") and stays deterministic. |
| `\` | Clear the user choice for this photo and return to Auto. |
| `G` | Toggle group mode, which is a global preference. |
| Click (mouse up, less than 4 px of movement) on a face box while boxes are showing, or **Alt+click** at any time | Lock the clicked face. Dragging still pans, so existing panning is unaffected. |

**Avoid Tab:** Win32 may use it for focus traversal, and it conflicts with dialog conventions.

Each choice resets the face offset for that path and recentres, using the same semantics as `RecenterFaceLockForPath`. This is an explicit action, consistent with README rule 5 ("click never implicitly recenters"), because selecting a face *is* an explicit recentre.

If the image is still at Fit, choosing a face does not zoom it, and FaceLock still only acts beyond Fit. Optionally, `Z` or 100 % then centres on the chosen face, which already happens through `SetZoomMode`.

### 3.6 Face indicators (subtle)

Drawn in `canvas_rendering_and_overlays.cpp` with the existing Direct2D brushes. Rects are converted from normalised to screen coordinates through the existing `ResolveView` / `InverseRotate` path, so rotation and pan are handled correctly.

- **When shown:**
  - FaceLock is on and the photo has 2 or more valid faces, and
  - one of these applies: the pointer is moving over the canvas (fade out after 1.2 s idle), or the last 1.2 s after navigation, cycling or a mode change.
  - A setting `Face boxes: Auto / Always / Never` (default Auto) controls this.
- **Style:**
  - Locked face: 1.5 px accent outline with rounded corners, matching the app's rounded-corner language.
  - Other significant faces: 1 px white at 35 % opacity.
  - Non-significant faces: only while cycling, at 20 % opacity.
  - Group mode: one dashed union box.
- **Small "2 / 5" chip** next to the zoom indicator: "Face 2 of 5 · Auto/Manual/Group".
- **Cost:** a handful of `DrawRoundedRectangle` calls per frame, with no new bitmaps and no work while hidden.

### 3.7 Group-shot mode

- **Anchor:** the union of all *significant*, non-clipped faces (clipped faces are added if they are the only ones), padded by 20 % horizontally, 35 % at the top and 25 % at the bottom to include hair and chin.
- **Behaviour:** FaceLock centres on the union centre.
- **"Fit faces" zoom:** an extra zoom target, for example `Shift+F` or the zoom-mode dropdown, that zooms so the union box fills about 85 % of the pane. That makes "check everyone's eyes" a single keystroke.
- **Default: off (opt-in with `G`).**
  - Optional auto-suggestion: when there are at least 3 significant faces, `max/min area ≤ 2.5` and `score[0] < 1.35 × score[1]` (no dominant subject), show a one-time hint chip: "Group detected – press G to frame all faces".
  - I do not recommend switching modes automatically: deterministic and unsurprising behaviour matters more in fast culling.
- **Blur warning in group mode:** report the share of significant faces that are blurry (phase 4, needs per-face sharpness).

### 3.8 Cross-pane matching in Compare

This applies when Sync is on (or always, for propagating the selection), and the active pane resolves to face *F* with normalised centre *c* and size *s* (√area).

For each other pane, evaluate every significant face *f* and keep the one with the lowest cost:

```
cost = |c_f − c| / max(s, s_f)  +  0.75 * |ln(s_f / s)|
```

- Accept the match only if `cost < 1.2`; otherwise fall back to that pane's own Auto primary.
- Skip matching (use Auto) when the aspect ratios differ by more than 12 %, for example portrait against landscape, or crops where normalised positions are not comparable.
- **This suits bursts and series well:** same framing, same people in the same places. It deliberately does no identity recognition (no embeddings, no privacy concerns, no models).
- Choosing a face in the active pane (§3.5) propagates through matching to the synced panes. With Sync off, it affects only the active pane.
- It fixes G3. Combined with the shared offset, synced panes now pan around *the same person*.
- **Cost:** with at most 6 panes × about 10 faces, the work is trivial on the UI thread. Optionally, memoise per `(activeRect, paneEpoch)`.

### 3.9 Remembered choice

- **Recommended: session-only.**
  - A `std::unordered_map<std::wstring, FaceSelection>` lives in `ReviewStateModel`, next to the existing per-pane state.
  - It is cleared in `ResetForFolder` (`review_state_model.cpp:327`), which follows the existing rule that folder changes clear image-specific state.
  - It persists across navigation within the folder, so going back to a photo restores the person you chose.
- **Option: DB persistence** (phase 4).
  - Add a `face_choice BLOB` column holding a version byte, a mode byte and one `NormalizedFaceRect`, using the existing `ALTER TABLE ... ADD COLUMN` precedent (`persistent_cache.cpp:619`) so the cache is not discarded.
  - The fingerprint (size, mtime, sidecar) already invalidates it when the file changes.
  - Tradeoffs: more upsert churn (choices are rare, so this is minor) and new state in a "disposable" cache. I only recommend it if users say they re-cull the same folder over several sessions.

### 3.10 Zero-face and failure fallback

- **No faces (scan complete):** behave exactly as with FaceLock off. Wheel zoom pivots at the cursor and Reset centres the image. On navigation, briefly show "No face found" in the face chip, with no toast and no jump.
- **Not scanned yet (pending):**
  - Behave as with no faces.
  - When the result arrives, **do not jump if the user interacted** after navigation (fixes P0-b): set `offset = currentCentre − newAnchor`, so the next pan or zoom is face-relative but the current frame stays where it is.
  - If the user has not interacted, anchor straight away as today.
- **Transient failure, exhausted retries or unavailable:** handled like no faces. The existing status text already explains the state (`popup_menus_commands_and_folder_tree.cpp:486-492`).
- **Optional later:** use the EXIF AF point as the anchor when there are no faces. Only worth doing if AF metadata gets parsed (§5).

---

## 4. Data model changes

| Item | Change | Phase |
|---|---|---|
| `NormalizedFaceRect` | **Unchanged (16 bytes).** Do *not* add fields to it. The blob reader only checks `bytes % sizeof`, so a larger struct could misread old blobs; for example, 48 bytes is 3×16 *and* 2×24. | — |
| Face order | `faces` is sorted by the new score, so `faces[0]` is the auto primary. Cached rows are re-ranked on read; no schema bump. | 1 |
| `core/face_policy` | New: `FaceRankingParams`, `ScoreFace`, `RankFaces`, `IsValidFace`, `IsSignificantFace`, `IsClippedFace`, `SignificantUnion`, `MatchFace(source, candidates, aspect)`, `ResolveSelection(rect, faces)`. `FaceAnchorComesBefore` is kept as the tie-breaker. | 1–3 |
| `review::FaceSelection` | `{ enum Mode { Auto, Manual }; NormalizedFaceRect rect; }`, with a per-path session map in `ReviewStateModel` and a `groupMode_` preference flag. Transitions: cleared on folder change; choosing a face resets that path's face offset. | 2–3 |
| `WorkResult` / `PhotoItem` | Phase 4 only: `std::vector<float> faceSharpnessTopK` (top 3 significant faces). Blur flags stay primary-based unless group mode is used. | 4 |
| SQLite | Phases 1–3: none. Phase 4: optional `face_detail BLOB` (versioned: top-K sharpness, AF point) and `face_choice BLOB` through `ALTER TABLE ADD COLUMN`. | 4 |
| INI | `FaceBoxes` (0 Never, 1 Auto, 2 Always) and `FaceGroupMode` (0/1), saved next to `FaceLock` (`navigation_input_and_session.cpp:490, 555`). | 2–3 |

---

## 5. Performance

- **No new UI-thread image work.** Ranking, filtering and per-face sharpness all run on the worker inside the existing Face job.
- The UI does only O(faces) arithmetic in `ResolveFaceAnchor` plus at most a few dozen rect draws.
- Phase 1 adds about 1 µs of sorting per photo.
- Phase 4's top-3 sharpness reuses the already decoded 512 px (or 1536 px) preview: about 0.5–2 ms per extra face on a worker, and only for photos with 2 or more significant faces.
- Doing P0-a with a direct Gray8 conversion **reduces** cost: a quarter of the SoftwareBitmap bytes, and the 4× face-job memory reservation (`background_work_engine.cpp:1748-1753`) could be cut.
- **Detection at higher resolution (G9)** is the only expensive option. A second pass at 1024 px costs about 4× the pixels, and maybe 10–40 ms on a worker depending on the machine (**unmeasured**). Suggested gate: only when FaceLock is on and the first pass found 2 or more faces, or the image is wider than 3:2. Leave it out of the default until it has been profiled.
- Matching and indicators do not affect decode scheduling, eviction or the epochs. `PathHasDetectedFace` stays as it is.

---

## 6. Files that would change

| File | Change |
|---|---|
| `src/face_detector.cpp` | P0-a Gray8 conversion and an `IsBitmapPixelFormatSupported` check; optionally set `MinDetectableFaceSize`; call `RankFaces` instead of the bare sort. |
| `src/core/face_policy.h/.cpp` | Scoring, filters, union, matching and selection resolution (pure and portable). |
| `src/persistent_cache.cpp` | Re-rank faces on read (phase 1); optional new columns (phase 4). |
| `src/work/background_work_engine.cpp` | Generalise `AnalyzePrimaryFaceSharpnessBgra(face)`; top-K sharpness (phase 4). |
| `src/core/app_types.h` | Phase 4 fields only. |
| `src/review/review_state_model.h/.cpp`, `src/review/README.md` | `FaceSelection` map, group flag, transitions and invariants. |
| `src/ui/view_state_controller.cpp`, `src/app/quicksift_application_internal.h` | `ResolveFaceAnchor` replaces `PrimaryFaceCenter`; Compare matching; P0-b late-result offset adoption. |
| `src/ui/navigation_input_and_session.cpp` | `[` `]` `\` `G` `Shift+F`; correct face-to-screen pivot through `ResolveView` (P0-c); INI keys. |
| `src/ui/main_window_chrome_and_messages.cpp` | Click (no drag) / Alt+click hit test on face boxes. |
| `src/ui/canvas_rendering_and_overlays.cpp` | Face-box overlay and the "n / N" chip. |
| `src/ui/popup_menus_commands_and_folder_tree.cpp`, `src/ui/ui_command_ids.h`, `ui_control_catalog.h` | Face Lock submenu (Auto / Next / Previous / Group / Show boxes) and settings rows. |
| `src/ui/localization.cpp`, `app_theme_documents_and_localization.cpp:263` | New EN/VI strings. The tooltip changes from "largest detected face" to "primary detected face". |
| `scripts/verify-source-portable.py` | New guard tokens (e.g. `RankFaces`, `ResolveFaceAnchor`, Gray8 conversion). Keep the `FaceAnchorComesBefore` guard. |
| `USER_GUIDE.md:28,32`, `ARCHITECTURE.md:35,128-134`, `CHANGELOG.md`, `README.md:10` | Documentation. |

---

## 7. Phased plan

**Phase 0: Correctness prerequisites (small, separate PR)**

1. Confirm the Bgra8 question (P0-a) on a Windows 10/11 machine. If confirmed, convert to Gray8 and log `GetSupportedBitmapPixelFormats` once at capability query.
2. P0-b: no jump on late results after user interaction.
3. P0-c: wheel-zoom pivot through the real view transform.

**Phase 1: Deterministic primary choice (worker-side, invisible apart from better picks)**

1. Filters (valid, plausible, significant, clipped) and the score with hysteresis, in `core/face_policy`.
2. `faces[0]` becomes the new auto primary; cached rows are re-ranked on read; the blur warning follows automatically.
3. Update the documentation and tooltip wording.

**Phase 2: User control and visibility**

1. Session `FaceSelection` map, `[` `]` `\`, click / Alt+click to lock.
2. Face-box indicators, the "n / N · Auto/Manual" chip and the `FaceBoxes` setting.

**Phase 3: Multi-subject workflows**

1. Compare cross-pane matching, with propagation of the choice under Sync.
2. Group mode (`G`), the "Fit faces" zoom and the optional group hint.

**Phase 4: Optional enhancements (only if wanted)**

1. Top-K per-face sharpness feeding the score, and a blur summary for groups.
2. EXIF AF point / SubjectArea boost (needs a new metadata parser).
3. DB-persisted choice.
4. Higher-resolution second detection pass for small group faces (profile first).

---

## 8. Recommended defaults

| Setting | Default |
|---|---|
| Primary rule | Size × centrality × edge penalty, with 8 % hysteresis towards the larger face |
| Minimum face | 2.5 % of the image short edge; significant = at least 20 % of the largest area |
| Cycling | `[` / `]` spatial order; `\` returns to Auto; click (no drag) or Alt+click |
| Indicators | Auto (on pointer movement / after a change, fade after 1.2 s; only when there are 2 or more faces) |
| Group mode | Off; toggle with `G`; optional one-time hint |
| Compare | Match the active pane's face by position and size when Sync is on; otherwise fall back to Auto |
| Memory | Session only (cleared on folder change) |
| Zero faces | Plain non-FaceLock behaviour; no jump when results arrive late |

---

## 9. Uncertainties to resolve

1. **Bgra8 support in WinRT `FaceDetector`** (P0-a). The documentation says Gray8/Nv12 only, and the current code passes Bgra8. It needs on-device confirmation. If detection really does fail today, the user may never have seen FaceLock work with multiple faces at all.
2. **Default `MinDetectableFaceSize` / `MaxDetectableFaceSize`.** Not documented; read them at runtime and log them once. Together with the 512 px detection size, they set the real floor for group shots.
3. **No confidence score from WinRT.** The filters in §3.2 are heuristics. A future detector, such as a Windows ML / ONNX face model, could add confidence and landmarks (eyes, for eye-focused anchoring). The data model in §4 leaves room for that without breaking the 16-byte blob.
4. **AF point metadata.** EXIF `SubjectArea` (0x9214) is standard but rarely filled in. Maker-note AF data (Canon AFInfo2, Nikon AFInfo2, Sony FocusLocation, Fuji FocusPixel) varies by vendor. No AF parsing exists in the tree today.
5. **Score constants.** The constants in §3.3 are reasoned starting points. They should be tuned on a small labelled set: portraits, couples, groups, bystanders, clipped faces.
6. **RAW embedded-preview geometry** (G10). Measure on a few camera models whether embedded-JPEG boxes line up with the rendered RAW.
7. **Spatial vs score order for cycling.** I recommend spatial order. Score order is the alternative if users want "next most likely subject".
