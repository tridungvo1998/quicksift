# Thumbnail search + unified authoritative metadata (0.9.4.0)

Tree: `/workspace/quicksift/QuickSift-0.9.3.1-filmstrip-final-repair` (edited in place, not renamed, not zipped).
User docs: CHANGELOG.md (0.9.4.0 → *Thumbnail search box*, *Unified authoritative metadata*), USER_GUIDE.md §4 *Thumbnail search* (syntax table + precedence), ARCHITECTURE.md (*Unified metadata model and Thumbnail search* diagram, core module list), TESTING.md (check programs).

## 1. Change list

### New files
| File | Purpose |
|---|---|
| `src/core/metadata_vocabulary.h` | One canonical table for stars, pick and colour: values, ranges, export tokens, XMP label text, menu/state/filter labels, and EN+VI search keywords (with accented and unaccented aliases). |
| `src/core/photo_predicate.{h,cpp}` | Shared predicate engine (`Clause`, `ClauseMatches`, `MatchesAll`), accent folding, fuzzy name/comment text matching (reuses `batch_match`), and Filter-tab criteria compilation. |
| `src/core/search_query.{h,cpp}` | Query language: tokenizer, prefixes, phrases, fuzzy keyword tiers, precedence, `GroupThousands`. |
| `src/core/exif_capture_time.{h,cpp}` | Bounded EXIF DateTimeOriginal / Digitized / DateTime reader for JPEG APP1, TIFF/RAW IFD0 + Exif sub-IFD, and embedded TIFF blocks. |
| `src/core/image_metadata.h` | Authoritative model contract: `ImageMetadataRecord`, `CatalogSnapshot` (+Ptr), `FactsOf`, `RecordOf`, `SameMetadataFacts`. |
| `src/ui/thumbnail_search_controller.cpp` | Floating field (layout, fade, paint, mouse, keys), debounce, evaluation (inline or worker), fact hydration worker, count text. |
| `scripts/checks/search_query_checks.cpp` | 168 checks. |
| `scripts/checks/metadata_store_checks.cpp` | 23 checks: epoch/snapshot semantics, exhaustive Filter-tab equivalence (224 combinations × 324 records against an independent reference of the old rules), export-token round trip. |

### Modified files
- **Core**
  - `app_types.h`: `PhotoItem` gained `captureTime`, `captureTimeSource`, `userComment` and `userCommentKnown`.
  - `catalog_store.{h,cpp}` and `catalog_store_mutation.cpp`: added `MetadataEpoch()`, `Snapshot()` (cached per epoch) and `BumpMetadataEpoch()`. `Append`, `AppendBatch`, `ReplacePath` and `ClearAndRelease` bump the epoch; `EditPhoto` bumps it only when `!SameMetadataFacts`.
  - `catalog_policy.{h,cpp}`: `IsCullAllowed` → `MatchesFilter(FactsOf(item), FilterCriteriaFor(...))`.
  - `ratings_exchange.cpp`: tokens and validation delegate to the vocabulary; the file format is unchanged.
- **Rendering and framework**
  - `review/thumbnail_layout.{h,cpp}`: `topInset` lane.
  - `ui/framework/macos_ui_framework*`: `TextInputOptions::borderless`, `SelectAllTextInput`, `FloatingFieldLayout`/`Presentation`, `LayoutFloatingField`, `FloatingFieldFill`, `PaintFloatingField`.
- **Persistence and IDs**
  - `persistent_cache.{h,cpp}`: `GetPhotoComments` (batched, chunks of 256).
  - `app/application_support.h`: `WM_APP_SEARCH_WORKER` and `ID_TIMER_SEARCH_DEBOUNCE`.
  - `ui/ui_command_ids.h`: `ID_THUMBNAIL_SEARCH`.
  - `app/quicksift_application_internal.h`: search declarations and state, plus `FilterAllowed`.
- **Hooks**
  - `popup_menus_commands_and_folder_tree.cpp`:
    - `CullAllowed = FilterAllowed && SearchAllows`;
    - `RebuildVisiblePhotos` makes a single pass and computes `searchBaseCount_`;
    - `OnCommand` handles `EN_CHANGE`.
  - `main_window_chrome_and_messages.cpp`:
    - key pre-translation;
    - canvas mouse down, double-click, move and leave;
    - `WM_APP_SEARCH_WORKER`;
    - shutdown (kill the debounce timer, stop the search worker before the cache closes).
  - `main_window_controls_layout_and_painting.cpp`:
    - creates the field;
    - lays it out after the canvas `SetWindowPos`;
    - DPI hook;
    - comments read and write through the store.
  - `quicksift_application.cpp`: animation tick; the destructor stops the worker.
  - `canvas_rendering_and_overlays.cpp`:
    - draws the field before toasts;
    - search empty-state text ("Searching…" / "No photos match the search");
    - the layout lane;
    - `CullStateText` uses the vocabulary.
  - `navigation_input_and_session.cpp`: debounce timer.
  - `catalog_loading_and_result_integration.cpp`: `OnSearchFolderChanged` after `ClearAndRelease`.
  - `app_theme_documents_and_localization.cpp`: theme hook and localized placeholder.
- **Vocabulary consumers**
  - `settings_filters_and_file_commands.cpp`: Rating, Pick and Label menus plus all three filter menus.
  - `batch_processing_commands.cpp`: dropdown arrays and values.
  - `application_support.cpp`: `LabelName` / `LabelDisplayName`.
  - `embedded_metadata.cpp`: the XMP `LabelName`.
  - `ratings_exchange_commands.cpp`: export snapshot ranges and `PickSign`.
- **Strings:** `localization.cpp` adds 4 EN+VI pairs (placeholder, "{shown} of {total}", "Searching…", "No photos match the search").
- **Build and docs:**
  - `CMakeLists.txt`: new core sources and headers, plus the controller.
  - `scripts/verify-source-portable.py`: the "filters require Known values" guard now follows the contract into `photo_predicate.cpp` (≥3 there, ≥2 in `catalog_policy.cpp` for the rating sort). It also requires `MatchesFilter(FactsOf(item)` in `catalog_policy.cpp`, so a regression away from the shared engine fails verification.

## 2. Query syntax and precedence

These forms are verified by the checks and by a parser probe. See the USER_GUIDE table.

- **Text:** `wedding`, `IMG_12` match the name OR the comment.
  - Names match fuzzily: numbers must match whole digit runs and letter runs of 4+ letters tolerate one typo.
  - Comments match as a substring, or with 1 typo for queries of 4+ letters.
  - Matching is accent-insensitive.
  - `name:` / `file:` / `filename:` and `comment:` / `note:` / `ghichu:` restrict the target. `"quoted"` is always text.
- **Time:** `8AM` (08:00–08:59), `8 am`, `8pm`, `8:30pm`, `20:15`, `20h`, `20h15`; also `time:8AM`, `time:20`, `time:8` (24 h), `time:20:15`, `time:8 pm`, `giờ:`/`gio:`.
- **Date:** `date:12` (day of month), `date:fri` / `date:friday` / `date:thứ sáu` (weekday), `date:12/10[/2026]` (DD/MM), and ISO `2026-10-12`, which also works bare. `ngày:` is accepted as the prefix.
- **Stars:**
  - exact: `5 stars`, `5 sta`, `5 sao`, `5★`, `5*`, `★★★★★`, `rating:5`;
  - comparisons: `>=4 stars`, `4+`, `>3 stars`, `<=2 stars`, `stars:>=4`;
  - no stars: `unrated`.
- **Colour:** `red`, `yelow`, `grn`, `pur`, `đỏ`, `vàng`, `xanh lá`, `no color`, `color:none` / `label:` / `colour:` / `màu:`.
- **Pick:** `picked`, `pick`, `pik`, `đã chọn`; `rejected`, `rej`, `loại`; `unmarked`, `unflagged`, `no pick`; `pick:` / `flag:`.
- **Precedence:**
  1. An explicit prefix wins. An unparseable value is ignored, not guessed.
  2. Quoted text is always text.
  3. Two-token phrases: number + star unit, time + am/pm, two-word keywords.
  4. A bare word is tried in order as a time (it needs a meridiem, minutes or an `h` form), a star form (it needs a unit, comparator, `+` or ★), an ISO date, then a colour/pick keyword. Keywords match exact > prefix > fuzzy (OSA ≤1, ≥4 chars) > abbreviation; they need ≥3 chars bare and ≥2 after a prefix. A best tier that names two different values counts as ambiguous, and the word falls back to text.
  5. Everything else is text.
- **Other rules:**
  - A bare number (`12`) is text.
  - Bare weekday names and DD/MM forms need `date:`.
  - Punctuation-only tokens are ignored.
  - All clauses are ANDed with each other and with the Filter tab.

## 3. Unified design and which paths moved onto it

- **Authority.** `CatalogStore`'s `PhotoItem` is the one model. It holds rating, pick and colour together with their Knowledge (written by the metadata reader and the transaction pipeline), plus capture time and comment (written by hydration and the comment field). All writes are UI-thread `EditPhoto` / `Append` / `ReplacePath`. The epoch bumps only on fact changes.
- **Thread-safe snapshot API.** `CatalogStore::Snapshot()` returns an immutable `shared_ptr<const CatalogSnapshot>` tagged with the epoch and cached until the next bump. The search worker evaluates against it. Batch and Export keep their existing UI-thread catalog value snapshots (taken before the worker starts), so they already read the store.
- **Paths moved onto the model:**
  - **Filter tab:** `IsCullAllowed` runs on the shared predicate engine. Equivalence with the old rules is proved exhaustively.
  - **Search:** same engine and same facts.
  - **Ratings Export:** reads the store, using it whenever a field is Known/Unsupported. Files are re-read only for Unknown fields, which was already the case and is now documented in place. Token ranges and `PickSign` come from the vocabulary.
  - **Ratings Import and Batch:** still `SubmitRatingsImport` → metadata transaction service, which verifies, applies CAS and then updates the store. Search and filters refresh from the epoch.
  - **Info tab comments:** read from the store when known; saving updates the store first, then the DB.
  - **Status-bar cull text, menus, filter menus, Batch dropdowns, XMP label writers and export tokens:** all use `metadata_vocabulary.h`.
  - **Sorting:** unchanged (it already reads the store, and the rating sort keeps its Known rule).
- **Not changed:** the export file format; reconcile/CAS; history and undo; transaction verification; "view jumps to the face when results arrive"; FaceLock; Batch semantics.

## 4. Verification (Oct 9 2026, UTC+7)

- `python3 scripts/verify-source-portable.py --source-dir .`: passed. This is with the updated guard described above.
- Portable build, `cmake -DQS_WARNINGS_AS_ERRORS=ON`: built from a clean build directory with no warnings.
- Check programs, built with `g++ -std=c++20 -O1 -Wall -Wextra -Wpedantic -Wshadow -Werror`:

  | Check program | Result |
  |---|---|
  | ratings_exchange | 70 checks, 0 failures |
  | face_interaction | 58 checks, 0 failures |
  | face_policy | 49/49 |
  | batch_match | 62 checks, 0 failures |
  | search_query | 168 checks, 0 failures |
  | metadata_store (new) | 23 checks, 0 failures |

- MinGW-w64 `-fsyntax-only -Wall -Wextra -Wshadow` was run on all src files. Every changed or new file is clean.
  - The pre-existing failures in `work/background_work_engine.cpp` come from WIC declarations missing from the MinGW headers. That file is untouched.
  - `persistent_cache.cpp` was checked with a declaration-only `sqlite3.h` shim in `/tmp/mgw-inc`.
- Not run: the MSVC build or the Windows app. There is no Windows host on the box.

## 5. Windows risks / things to check on a real build

1. **Sibling HWND over the flip-model swap chain.** The search input is a child window that sits directly above the canvas. This is the same arrangement the left pane and comment field already use. Check that it never flickers or is overdrawn during fast thumbnail scrolling and DWM composition, especially with the left pane animating, and when resizing or changing DPI. The field hides instantly when leaving Thumbnail view and while a glass alert is shown.
2. **Capture time coverage.** Capture time comes from bounded reads: 64 KB first, then up to 512 KB.
   - JPEG, DNG, NEF, ARW and CR2 keep EXIF early in the file.
   - HEIC/HEIF and CR3 may store EXIF beyond 512 KB, or in boxes the scan does not reach. Those files then fall back to the file-modified time. That may be wrong after copying, which usually preserves mtime, and is wrong after editing.
   - The tag fallback order is DateTimeOriginal, then DateTimeDigitized, then DateTime, then file-modified.
3. **Time zone.** EXIF times are used as written (camera local time). The modified-time fallback is converted with the current zone rules (`SystemTimeToTzSpecificLocalTime`), so DST edges may differ by an hour from Explorer.
4. **Ctrl+F** now focuses search in Thumbnail view. In Single and Compare views it still means Fit, because the `F` shortcut ignores Ctrl.
5. **Global keys while typing.** Keys in the search field never reach global shortcuts, including Ctrl+Z/Y. This differs from the comment field, where Ctrl+Z/Y stay global. The framework input has no text undo of its own.
6. **Search persists outside Thumbnail view.** Opening a result in Single view keeps the result set, so arrows move through the results, but the field is not visible there. The count and query come back on returning.
7. **Large folders while scanning.** With search active, every scan batch bumps the epoch. Re-evaluation is throttled to 400 ms, but each one builds a snapshot on the UI thread (O(n) copies of name and comment strings). It is fine at about 10⁴ photos; profile at 10⁵+. Rows not yet in the result bitset are evaluated directly in `CullAllowed`.
8. **Hydration I/O.** The first time/date query in a folder reads up to 64–512 KB per file in the background. On slow HDDs or network shares the counter shows "…" for a while. Reads use shared access and never lock files.
9. **Selection.** Selection follows the visible results, as with filters. The "selection limited" toast is intentionally not shown for search.
10. **Not regenerated:** `SOURCE_MANIFEST.sha256` and `codebase_bundle.txt` (earlier change sets did not update them either). Regenerate them in the Windows packaging step if they are release inputs.
