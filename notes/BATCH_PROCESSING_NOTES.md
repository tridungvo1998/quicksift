# Batch Processing — implementation notes (0.9.4.0)

Tree: `/workspace/quicksift/QuickSift-0.9.3.1-filmstrip-final-repair` (in place, not zipped, nothing renamed).

## User-visible behaviour

The File tab has a new **Batch Processing** button. It opens a modal form with:

1. **File name list**
   - a multiline scrollable text area;
   - **Load names from folder**.
2. **Do what?**
   - **Star rating**: No change / 0 ★ (Unrated) / 1–5 ★.
   - **Pick state**: No change / Pick / Reject / Unmark.
   - **Color tag**: No change / None / Red / Yellow / Green / Blue / Purple.
3. **Additional settings**
   - **Extensions**: a single-line field with an inline error line.
   - **Matching method**: Partial matching (default) / Complete matching.
4. **Execute** (primary, accent) and **Cancel**.

## Exact matching rules (core/batch_match_policy)

- **Separators**: space, tab, `,`, `;`, CR, LF. A token that starts with `"` runs to the next `"` and may contain separators; the quotes are removed. An unterminated quote is dropped.
- **Entry cleanup**:
  - trim;
  - remove a directory prefix (up to the last `\` or `/`);
  - remove ONE trailing extension if it is a supported image extension (`IMG_1.JPG` → `IMG_1`; `IMG_1.xmp` stays as typed).
- **Dedupe**: one fold key per entry. Windows uses `WindowsNameFold` (invariant `LCMapStringEx` upper-casing, the same fold Ratings Import uses); the portable default is an ASCII fold. The first occurrence is kept, for typed and folder-loaded names alike.
- **Complete matching**: fold(file stem) == fold(entry).
- **Partial matching**:
  1. Tokenize the folded entry and stem into digit runs (ASCII 0–9, leading zeros removed, `000` → `0`) and letter runs (non-delimiter, non-digit characters; delimiters are ASCII non-alphanumerics; non-ASCII counts as a letter).
  2. Every entry digit run must equal a WHOLE stem digit run, in order, with each stem run used at most once (numerals first: `123` matches `IMG_0123` and `DSC123`, not `IMG_1234` or `IMG_51230`).
  3. Every entry letter run must occur in some stem letter run: as an exact case-insensitive substring if shorter than 4 characters, otherwise within Damerau-Levenshtein (OSA) distance ≤ 1 of some substring.
  4. Letter-only entries therefore use fuzzy substring matching. An entry made only of punctuation matches nothing.
- **Counting**: each file is counted once, however many entries match it. Results keep catalog order. Per-extension counts are sorted by count descending, then by name.
- **Unmatched entries**: entries that matched no in-scope file. They are reported in the confirmation (count, plus **See unmatched names**).
- **Known limitation**: an unquoted name containing spaces is split into several entries. Quote it (`"Wedding 012"`). Names loaded from a folder that contain separators are quoted automatically.
- **Performance**: Complete matching uses a hash map. Partial matching narrows candidates by the entry's rarest digit run. The worker polls the cancel flag every 256 steps.

## Design decisions

- **Empty Extensions** = every supported type (the 42 extensions in `AllSupportedImageExtensions()`: jpg/jpeg/jpe, png, avif/heic/heif, tif/tiff, bmp/dib/gif, webp + 29 RAW). The confirmation then adds "Extensions: all supported image types (the Extensions field is empty)." Tokens may be `jpg`, `.JPG` or `*.jpg`. Unknown or unsupported tokens show an inline red error under the field, give it a danger border, and block Execute. A check asserts the registry and `IsFormatAllowed` agree in both directions.
- **Scope** = `catalog_`: every photo of the open folder.
  - It is non-recursive, like the scanner.
  - View filters, format toggles and the selection are ignored, the same scope as Ratings Export.
  - It is then narrowed to the target extensions.
  - Execute requires a finished folder scan.
- **Folder load**:
  - `IFileOpenDialog` with `FOS_PICKFOLDERS` (framework `PickFolder`), starting in the open folder.
  - Listing runs on the batch worker: non-recursive; directories and hidden/system files skipped; supported image types only; sorted case-insensitively.
  - The stems are appended to the existing text (one per line), skipping keys already present. The dialog is busy (inputs disabled, Cancel live) while loading.
  - A note next to the button reports "Added N names (M already listed)".
  - Lists over 1 Mi characters (the text-input cap) are refused with a status message.
- **Unmatched entries** are reported in the confirmation with **See unmatched names**. That button reuses the missing-files list window (`DocumentKind::MissingFiles` with a new `titleKey`, so a language change keeps the "Unmatched names" title). It does not close the confirmation (`tertiaryKeepsOpen`).
- **Zero matches** shows an information notice, not a confirmation; dismissing it returns to the dialog.
- **Return to dialog**:
  - While a glass alert is shown, the dialog is hidden and its owner re-enabled, because glass alerts are in-canvas.
  - Declining the confirmation or dismissing the notice re-shows the dialog with all inputs intact.
  - `ID_TIMER_BATCH_GLASS_WATCH` restores the dialog if another alert replaces the batch alert.
  - If the language changed meanwhile, the dialog is rebuilt with the same inputs.
- **Apply**:
  - Goes through `SubmitRatingsImport` (the Ratings Import path), with `RatingsImportItem` fields now `std::optional<int>` (empty = No change) and a label parameter.
  - Pipeline: optimistic catalog edit, storage preflight, serialized and verified metadata transaction with conflict checks, restore on failure.
  - Files already holding the value are skipped. Files removed since the preflight count as skipped.
  - Progress shows as "Batch processing: d / t files" in the status bar, then a summary toast "Batch processing complete: A applied, S skipped, F failed." Failures and cancellation show an alert.
  - `recordHistory=false`: not in Undo, and Redo is cleared.
- **Refusal**: Batch Processing will not open or execute while `transactions_.Busy()`, the ratings worker is running, or a ratings or batch apply is in flight.
- **Session memory**: all inputs (names, dropdowns, extensions, method) are remembered until exit. They are captured on Cancel, on Execute and on apply.

## New reusable framework elements (src/ui/framework)

- `TextInputOptions` + `CreateTextInput` (`CreateD2DTextInput` is a thin wrapper, so the comment field is unchanged in use). Features:
  - multiline (wrap, vertical scroll, overlay scrollbar with thumb drag, wheel, PgUp/PgDn) or single-line (no wrap, horizontal scroll, vertically centred);
  - placeholder;
  - error border (`SetTextInputError`) and parent-surface corners (`SetTextInputParentSurface`);
  - selection (mouse drag, Shift+click, double-click word, keyboard) and clipboard (Ctrl+A/C/X/V, Shift/Ctrl+Insert, Shift+Delete);
  - `EN_CHANGE` notifications to the parent;
  - `IsTextInput(HWND)`.
- Full rounded border: the D2D target uses 96 DPI, so pixels map 1:1. The stroke is centred on a rect inset by half its width plus `kTextInputBorderInsetDip`, so no edge is clipped at any DPI. There is no GDI erase (`WM_ERASEBKGND` is a no-op), so the input does not flicker.
- `CreateFormDialog(FormDialogSpec)` with `FormRow` kinds SectionHeader / TextArea / TextField / Dropdown / Button (+ note) / Note, plus:
  - accessors `FormText`, `SetFormText`, `FormSelection`, `SetFormSelection`, `SetFormFieldError`, `SetFormNote`, `SetFormStatus`, `SetFormBusy`, `SetFormPalette`, `SetFormTitle`, `ShowFormDialog`, `FocusFormControl`, `CloseFormDialog` (deferred destroy), `IsFormDialog`;
  - reused painters: `PaintWindowBackground`, `PaintTitleBar` (traffic-light close), `PaintAccentStrip`, `PaintPanel`, `PaintSectionTitle`, `PaintButton` (`state.popup` chevron for dropdowns, `state.selected` for primary, `state.focused` ring only after keyboard navigation, `parentSurface` = panelStrong) and `ApplyAntialiasedControlRegion`;
  - reused controls: `CreateButton` and `TrackDropdown` (with the app's maintenance pump, now shared via `DropdownMaintenancePump()`);
  - double-buffered paint, `WM_DPICHANGED` (fonts, inputs and layout rebuilt), and the same fonts as the main window.
- `PickFolder(FolderPickerRequest)`: title, OK label and initial folder. The existing `app::PickFolder` is left untouched for its current callers.

## Fixes made along the way

- **Text input Backspace** deleted two characters (handled in both `WM_KEYDOWN` and `WM_CHAR`). Fixed.
- **Text input selection highlight** was limited to 128 hit-test runs. Fixed.
- **`HandleGlobalKey`** sent digits, P/X/U, arrows, Home/End and Delete to global shortcuts even when the User comments field had focus, so typing `5` rated the photo and Delete prompted to delete files. These keys now reach a focused framework text input. Esc and Ctrl+Z/Y stay global.

## Files

- **New**:
  - `src/core/batch_match_policy.{h,cpp}`
  - `scripts/checks/batch_match_checks.cpp`
  - `src/ui/framework/macos_ui_framework_form.cpp`
  - `src/ui/batch_processing_commands.cpp`
- **Modified**:
  - core and framework: `src/core/image_formats.{h,cpp}` (registry), `src/ui/framework/macos_ui_framework.{h,cpp}` (text input rewrite, form/picker API);
  - app headers: `src/app/application_support.h` (WM_APP+16, timer 21, PendingGlassAction, DocumentWindowState::titleKey), `src/app/quicksift_application_internal.h`, `src/app/quicksift_application.cpp`;
  - command ids and catalog: `src/ui/ui_command_ids.h` (ID_BATCH_PROCESSING = 1053), `src/ui/ui_control_catalog.h` (kFileButtons → 5);
  - UI modules: `popup_menus_commands_and_folder_tree.cpp`, `canvas_rendering_and_overlays.cpp`, `main_window_chrome_and_messages.cpp`, `navigation_input_and_session.cpp`, `app_theme_documents_and_localization.cpp`, `metadata_history_and_file_safety.cpp`, `ratings_exchange_commands.cpp` (`WindowsNameFold` shared);
  - `src/localization.cpp` (VI);
  - `CMakeLists.txt`;
  - docs: `CHANGELOG.md`, `USER_GUIDE.md`, `ARCHITECTURE.md`.

## Verification (Linux box)

- `python3 scripts/verify-source-portable.py --source-dir .` passed. No verifier rule was changed: the form dialog's `CreateWindowExW` lives in the framework, which the CreateWindowEx budget does not cover.
- The portable CMake build (`-DQS_WARNINGS_AS_ERRORS=ON`, core library) passed.
- Checks, all with `-Wall -Wextra -Wpedantic -Wshadow -Werror`:
  - ratings 70/70;
  - face interaction 58/58;
  - face policy 49/49;
  - batch match 62/62.
- The MinGW `-fsyntax-only -Wall -Wextra -Wshadow` check is clean on every changed Windows file, apart from known pre-existing noise.
- MSVC was not run: there is no Windows toolchain here. A Windows build and a manual UI pass are still required.

## Windows risks / to check on a real build

- **Form dialog chrome**: same styles as the document windows (`WS_OVERLAPPED|WS_THICKFRAME`, `WM_NCCALCSIZE` 0). Check on Windows 10 and 11 for caption or frame artefacts, the shadow, and that it cannot be resized.
- **Focus and activation**: check hide → glass alert → re-show (`SetForegroundWindow`/`SetActiveWindow`), and the owner enable/disable when Alt+Tabbing away mid-flow.
- **Text input DPI**: the D2D target is now explicitly 96 DPI with manual scaling. Check the User comments field and the dialog inputs at 100/125/150/200% and when moving between monitors.
- **Large lists**: very long name lists (hundreds of thousands of characters) make DirectWrite relayout per keystroke slow; the cap is 1 Mi characters.
- **Keys in the comment field**: digits, P/X/U and Delete no longer act as global shortcuts there (intended fix, but a behaviour change).
- **Dropdowns**: check `TrackDropdown` anchored in an owned top-level window (popup capture, positioning near screen edges).
- **File tab layout**: it now has 5 buttons and wraps to a second row on narrow panes.
