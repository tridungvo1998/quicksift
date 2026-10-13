# QuickSift 0.9 User Guide

## 1. Open and navigate a shoot

Choose **Open** and select a folder containing photographs. Hover the narrow strip at the left edge of the window to open the left command pane. Vertical tabs inside that pane switch between **Library**, **File**, **Cull & Rate**, **Filter**, **Info**, and **Settings**; the selected section’s buttons and menus wrap onto additional rows when horizontal space runs out. Move the pointer away to hide the pane. QuickSift builds its catalog and thumbnails in the background; you can begin reviewing while lower-priority work continues.

QuickSift has three review modes:

- **Thumbnails** shows the whole folder as a selectable grid.
- **Single** shows the active photograph at a larger size.
- **Compare** places two or more selected photographs side by side.

In the thumbnail grid, click an image to select it, use **Shift-click** for a range, and use **Ctrl+A** to select all currently visible photographs. In Compare, the arrow keys move focus among photographs already in the comparison set rather than replacing that set.

## 2. Cull, rate, and label

- Press **1–5** or use the Rating dropdown to set a rating.
- Press **0** to clear the rating.
- Use **Pick**, **Reject**, or **Unmark** to set or clear the cull state.
- Use **Label** to assign a color label.

In thumbnail view, rating, cull, and label operations apply to the current selection. Filters change what is visible; they do not delete or rewrite source files.

## 3. Inspect photographs

Use the mouse wheel to zoom and drag to pan. The zoom selector provides Fit, Fit width, Fit height, 100%, and Fill behavior.

In Compare mode, **Sync** keeps zoom and pan aligned between panes. **Face Lock** keeps the view anchored on a face when face data is available (see *Face Lock with several faces* below). **F11** enters fullscreen; the title bar retracts toward the top edge and reappears when the pointer rests there briefly.

Rotate left/right changes the display orientation only. It never rotates the source file.

A red border around a thumbnail means at least one detected face looks out of focus, motion-blurred, or has its eyes closed according to QuickSift's conservative analysis. Hover the border to see which face and why (for example "Face 2: eyes closed"; faces are numbered left to right); in Single and Compare the same reason appears in the status bar. Treat this as a prompt to inspect at 100%, not as an automatic rejection verdict.

### Face Lock with several faces

- **Default target:** with one face, that face. With several, the face closest to the centre of the image. Very small background faces are never chosen by default while a clearly larger face exists.
- **Outlines:** while Face Lock is on, every detected face shows a very faint outline in Single and Compare. The current target's outline is slightly brighter; a face that triggered the red border is tinted faint red.
- **Auto-hide:** outlines are shown for 1 second whenever the photo changes, faces are found, or the target changes, then fade out. They stay clickable while hidden. **Right-click** the image to keep them always shown; right-click again to return to auto-hide (Face Lock must be on; QuickSift remembers the choice).
- **Click to choose:** click inside a face's outline to make it the Face Lock target. QuickSift zooms so that face fills about 70% of the pane (up to the maximum zoom). Dragging still pans; only a click without movement selects (moving the mouse more than the Windows drag distance, or releasing outside the face, cancels the click; so does Esc).
- **Click again to zoom back:** clicking the same face again restores the zoom and position you had before the face zoom. The face stays the target. If you click another face first, the restore still goes back to the original view. Zooming, panning, rotating, resizing or moving to another photo forgets the restore point. In Compare the click applies to that pane; with **Sync** on, the other panes adopt the same zoom but stay centred on their own faces; with Sync off, only the clicked pane changes.
- **Remembered:** your choice is saved per photo in QuickSift's cache and restored next time. It is cleared automatically if the file's content changes, or if the chosen face can no longer be found.
- **Red-border causes:** out of focus, motion blur, or eyes closed, checked for every face. Faces too small to judge are skipped. Expect occasional false warnings with sunglasses, faces in profile, strong squints, very warm or dim light, or heavy makeup; and missed closed-eye cases when the lids are bright or the face is small.


## 4. Filter and sort

QuickSift can filter the visible catalog by rating, pick state, color label, format, and date. It can sort by name, date, rating, or format. Thumbnail size changes grid density without changing the underlying catalog.

### Metadata filters and selection scope

Rating, Pick/Reject, color-label, format, and date filters narrow the visible catalog.
QuickSift intentionally keeps selection scoped to visible results: when a filter hides a
selected photograph, that photograph is removed from the selection rather than remaining
as an invisible command target. A toast and the status bar make this policy explicit.

Metadata-dependent filters include only authoritative values. Photographs whose metadata
is still being read, could not be read, or contains an unsupported custom value do not
pretend to be unrated, unmarked, or unlabeled. Results update progressively as authoritative
metadata arrives.

### Thumbnail search

Thumbnail view has a floating search field at the top right. It appears only in Thumbnail view.

- Press **Ctrl+F** to focus it, or click it.
- Results update as you type (after about 150 ms).
- **Enter**, **Tab** or **Down** apply the query and move focus to the grid.
- **Esc** clears the query; press **Esc** again to return to the grid. The **×** button also clears it.
- The counter shows *shown of total*, e.g. "37 of 1,024". The total is the number of photos that pass the Filter tab.

How search combines with the rest of the view:

- Search is combined (AND) with the Filter tab, and every term you type must match.
- The search stays applied when you open a result in Single view, so arrow keys move through the results.
- Clearing the search returns the grid to where it was.
- Selection follows the same visible-results rule as filters.

In Single and Compare views, Ctrl+F still means zoom to fit.

| You type | Finds | Notes |
|---|---|---|
| `wedding`, `IMG_12`, `dsc 0123` | file name **or** user comment containing the text | Fuzzy: numbers match whole digit runs (`123` finds `IMG_0123`), and words of 4+ letters tolerate one typo. Accent-insensitive (`dam cuoi` finds `đám cưới`). |
| `name:IMG_12` / `comment:bride` | file name only / comment only | `file:` and `filename:` work like `name:`; `note:` and `ghichu:` work like `comment:` |
| `"red"` | the literal text *red* in a name or comment | Quotes always mean text, never a keyword |
| `8AM`, `8 am`, `8pm`, `8:30pm`, `20:15`, `20h`, `20h15` | photos taken in that hour (`8AM` = 8:00–8:59) or at that minute | A bare number such as `8` or `12` is text, not a time |
| `time:8AM`, `time:20`, `time:8`, `time:20:15`, `time:8 pm` | as above; after `time:` a plain number is an hour on the 24-hour clock | `giờ:` / `gio:` work like `time:` |
| `date:12` | taken on the 12th day of any month | |
| `date:fri`, `date:friday`, `date:thứ sáu` | taken on that weekday | Bare weekday names are treated as text, so use `date:` |
| `date:12/10`, `date:12/10/2026`, `2026-10-12` | taken on that day | DD/MM order. ISO dates also work without `date:`. `ngày:` works like `date:` |
| `5 stars`, `5 sta`, `5 sao`, `5★`, `5*`, `★★★★★`, `rating:5` | exactly 5 stars | Any unit prefix of at least 3 letters (`sta`, `star`, `stars`, `sao`) |
| `>=4 stars`, `4+`, `>3 stars`, `<=2 stars`, `stars:>=4` | rating comparison | |
| `unrated`, `0 stars`, `rating:unrated` | no stars | |
| `red`, `yellow`, `green`, `blue`, `purple`, `đỏ`, `vàng`, `xanh lá` | that colour label | Fuzzy: `yelow`, `grn`, `purp`, `pur` |
| `no color`, `color:none` | no colour label | `label:`, `colour:` and `màu:` work like `color:` |
| `picked`, `pick`, `pik` | picked photos | Vietnamese: `đã chọn` |
| `rejected`, `rej` | rejected photos | Vietnamese: `loại` |
| `unmarked`, `unflagged`, `no pick` | photos with no pick state | `pick:` / `flag:` prefixes accepted |

**Precedence (how a word is read)**

1. A **prefix** (`name:`, `comment:`, `time:`, `date:`, `rating:`, `color:`, `pick:`) decides the meaning. If its value cannot be read (`time:abc`, `rating:9`), the term is ignored rather than guessed.
2. **Quoted** text is always text.
3. **Two-word phrases** are read as one term: `5 stars`, `8 am`, `no color`, `xanh lá`.
4. A bare word is tried as a time, then a star form, then an ISO date, then a colour or pick keyword. Keywords need at least 3 letters (2 after a prefix). A keyword that could mean two different things is treated as text.
5. Anything else is text matched against the file name **or** the user comment.

So `red` means the red label; type `"red"` or `name:red` to search for the word in names.

**Where the facts come from**

- **Ratings, picks and labels:** the same authoritative values the Filter tab uses. Photos whose metadata is still being read do not match these terms until the value is known.
- **Capture time:** the EXIF *DateTimeOriginal*. If it is missing, QuickSift falls back to *DateTimeDigitized*, then *DateTime*, then the file's modified time (local time). It is read in the background the first time a time or date term is used, and the counter shows "…" while that is in progress.
- **User comments:** loaded in the background the first time you search for text.


## 5. View information and configure behavior

Open the left pane and choose **Info** to inspect EXIF for the active photograph, or **Settings** for appearance, language, badges, metadata-writing behavior, and related preferences. Switching between English and Vietnamese updates the interface immediately.

**Settings → Load only JPG previews for RAW** is off by default. When enabled, RAW photographs are displayed only from a JPEG preview embedded in the RAW container. QuickSift does not demosaic RAW sensor pixels, does not fall back to WIC/Shell full-RAW rendering, does not request RAW deep-zoom tiles, and does not reuse an ambiguous persistent RAW bitmap created under full-decode mode. A RAW without a usable embedded JPEG may therefore remain unavailable until this setting is turned off. Changing the setting cancels stale queued/in-flight RAW decode policy and refreshes visible images.

## 6. Diagnostics and log export

Open **Settings → Diagnostic log** to view recent lifecycle, catalog, decode, metadata, cache, and file-operation events. The viewer follows the active theme and provides **Refresh**, **Export…**, and **Close** controls.

QuickSift automatically creates `QuickSiftData` beside `QuickSift.exe`. The session/preferences file is `QuickSiftData\QuickSift.session.ini`; the disposable persistent cache is `QuickSiftData\.cache`; UTF-8 session logs are written to `QuickSiftData\Logs`; and unhandled failures create text reports and minidumps under `QuickSiftData\CrashReports`. The newest 20 session logs and newest 20 crash incidents are retained, and an individual session log is capped at 16 MiB. If `QuickSiftData` cannot be created and written, QuickSift displays a startup error and does not launch. Logs may include local paths and filenames, and minidumps may contain fragments of process memory; inspect them before sharing.

## 7. Copy and move safely

**Copy** duplicates the selected photographs to a chosen folder. **Move** transfers them after destination checks. QuickSift does not silently overwrite an existing destination and does not invent a suffixed name such as “(2)” to bypass a conflict.

While Copy, Move, or Delete-to-`Deleted` is running, a second line appears in the bottom status bar showing the operation, fully committed media-file count, total media-file count, and estimated minutes remaining. XMP sidecars do not inflate the file count; a file is counted complete only after its complete media group has committed. Until the first file commits, QuickSift reports that it is calculating the remaining time.

The optional **Copy/Move RAW with JPG** setting is off by default. When enabled, an operation on a JPG also includes every recognized RAW file with the same base name. Unrelated non-RAW formats with that base name are left alone. A matching shared XMP sidecar is transferred once. If one or more selected JPGs have no matching RAW, QuickSift issues a single batch warning.

## 8. Delete behavior

Delete asks for confirmation, then moves selected photographs and matching XMP sidecars into a `Deleted` subfolder inside the folder currently being browsed. QuickSift does not use the Windows Recycle Bin for this operation.

If the main window is closed while a serialized file operation is still active, QuickSift asks whether to **Close anyway** or **Wait**. **Wait** dismisses the prompt and returns to normal use. **Close anyway** requests cancellation and exits only after the transaction reaches a safe boundary; already completed and verified media groups remain complete, and QuickSift does not abandon a partially published media group.

Undo and Redo can reverse or reapply protected metadata and file operations while those operations remain in the current history. They are convenience safeguards, not a substitute for an independent backup. If another application changes a file after QuickSift recorded it, Undo or Redo refuses to touch those different bytes and offers **Relinquish history** (drop the stuck entry and refresh catalog state from disk) or **Keep history** (leave everything untouched).

### Import / Export Ratings

**File tab → Import / Export Ratings** saves or restores star ratings, pick/reject state and colour tags for the open folder as a plain UTF-8 text file.

- **Export…** asks where to save (default name `QuickSift-Ratings-YYYYMMDD-HHMMSS.txt`) and lists every file in the folder that has a rating, a pick/reject or a colour tag (filters are ignored; unmarked files are omitted). The file looks like:

  ```
  Header: QuickSift version 0.9.4.0 - Ratings/Pick state / Color tag data
  Exported time: 14:05:09 09 Oct 2026
  Total files count: 812 files total
  Files that has been rated, picked or rejected, or color tagged: 2 files
  —
  Exported data:
  FILENAME.EXTENSION | Star Rating | Pick State | Color Tag
  IMG_0001.CR3 | 5 | Picked | Red
  IMG_0002.CR3 | 0 | Rejected | None
  —
  Footer: End of QuickSift exported data.
  ```

  Star Rating is `0`–`5`; Pick State is `Picked`, `Rejected` or `None`; Color Tag is `Red`, `Yellow`, `Green`, `Blue`, `Purple` or `None`.

  The file format (the header, the footer, the column line and the `Picked`/`Rejected`/colour tokens) is **always English**, whatever the interface language. This keeps exported files readable by every QuickSift installation and by scripts. Only the dialogs and messages around Import/Export are translated.
- **Import…** reads such a file and applies it to files with the same names in the open folder (names compare case-insensitively, as Windows does). A file that is not exactly in this format is refused with the line that is wrong, and nothing changes. If the folder's file count differs from the file's, or some listed files are missing, QuickSift asks before continuing; **See missing files list** shows the missing lines (select and copy with Ctrl+A, Ctrl+C) without closing the question. A final confirmation follows. Files not listed are never touched.
- Import writes through the same verified, conflict-checked metadata path as normal rating changes and reports how many files were applied, skipped (already identical or missing) and failed. **It cannot be undone** with Undo; export first if you want a backup.

### Batch Processing

**File tab → Batch Processing** sets the star rating, pick state and/or colour tag of every file in the open folder whose name is on a list. A typical use is a list of picks a client sends back.

1. **File name list**: type or paste names. Separate them with spaces, commas, semicolons, tabs or new lines. `IMG_0123`, `IMG_0123.JPG` and `C:\Shoot\IMG_0123.jpg` all mean the same entry. Repeated names count once, ignoring case.
   - A name that itself contains spaces must be put in double quotes (`"Wedding 012"`); otherwise it is split into separate names.
   - **Load names from folder** picks a folder and adds the names of its image files (without extensions) to the list, skipping names already there. Only that folder is read (no subfolders; hidden and system files are ignored). You can edit the list afterwards.
2. **Do what?**: choose a new **Star rating**, **Pick state** and/or **Color tag**. Anything left at *No change* is not touched. At least one change is required.
3. **Additional settings**:
   - **Extensions**: for example `jpg, arw` or `.CR3 *.jpg`, separated by spaces, commas or semicolons, ignoring case. A type QuickSift does not support is flagged in red and blocks Execute. **Leave it empty to include every supported type**; the confirmation then says so.
   - **Matching method**:
     - **Complete matching**: the file name without its extension must equal the entry, ignoring case.
     - **Partial matching** (default): numbers count first and must match a whole number in the file name, ignoring leading zeros. `123` matches `IMG_0123` and `DSC123`, but not `IMG_1234` or `IMG_51230`. Letters must appear in the name, ignoring case. Words of 4 or more letters may contain one typo (`weding` finds `Wedding_012`). An entry of only letters matches any file name containing them.
4. **Execute** finds the matching files in the open folder. Folder filters and format toggles are ignored; only the chosen extensions count, and each file is counted once. You then see, for example, *"Batch operations: 37 files (30 JPG, 7 ARW) will be processed. Confirm? This cannot be undone."*, followed by the chosen changes. If some names matched nothing, their number is shown, and **See unmatched names** lists them (select and copy with Ctrl+A, Ctrl+C). If nothing matches, QuickSift says so and changes nothing. **Cancel** returns to the dialog with everything you entered.
5. Changes are written through the same verified metadata path as rating changes. Progress shows in the status bar, and a toast reports how many files were applied, skipped (already set) and failed. **Batch Processing cannot be undone** with Undo. It is unavailable while another file, metadata or ratings operation is running.

The dialog remembers its settings until QuickSift closes. Keyboard: Tab / Shift+Tab move between fields, Enter activates the focused button (in the name list it adds a line; Ctrl+Enter executes), Esc cancels.

## 9. Metadata behavior

### Saving ratings, picks and labels (write lane)

- Every rating, pick/reject and colour-label change is saved **in the background**. The interface never waits for the file write, and Single/Compare image loading keeps going.
- Changes made quickly one after another are **coalesced**: if you press 3, 4 and 5 on the same photo while a save is running, only 5 is written. Changes are applied in the order you made them for each file, and a coalesced burst is one Undo step.
- If another app (an editor, a cloud-sync client, a virus scanner) has the file open, QuickSift retries for about 3 seconds. If the file is still locked, a notice **"Waiting for <file> — in use by another app"** appears and QuickSift keeps trying for up to 30 seconds. After that you can choose **Retry** (try again now) or **Skip** (keep the file as it is). A skipped or failed change shows the previous value again.
- QuickSift stops for good only when writing could damage the file: the file changed outside QuickSift, there is not enough free space, or the file or drive is read-only. Nothing is overwritten in those cases, and the message says why.
- Closing QuickSift while changes are still waiting asks first; **Close anyway** discards the changes that were not written yet.

XMP sidecars are the conservative default for formats where direct metadata modification is risky. When direct writing is enabled for a supported format, the safe path writes to a temporary file, validates and flushes it, then replaces the original when the filesystem supports the required guarantees.

QuickSift blocks or drains source-file read activity around exclusive metadata writes. If a source identity, destination, lock, or conflict condition changes unexpectedly, the operation stops rather than guessing. When rating, pick, or color-label updates are blocked because another application changed the file, QuickSift offers **Refresh from disk** (accept the current file as the new catalog baseline and re-read metadata; on-disk bytes stay untouched) or **Keep catalog**.

## 10. Formats and codecs

The bundled release configuration supports common JPEG, PNG, TIFF, BMP, GIF, WebP, AVIF, and RAW workflows. QuickSift also uses Windows Imaging Component codecs where appropriate, so HEIF/HEIC availability can depend on codecs installed in Windows.

## 11. Cache and privacy

QuickSift keeps a disposable SQLite metadata cache and bounded image/tile cache under `QuickSiftData\.cache` to accelerate future browsing. Thumbnail loading follows scroll direction and velocity: during a fast scrollbar or wheel fling the grid stays responsive with placeholders/cache hits, ramps decode work when scrolling slows, and fully prioritizes the visible viewport (then predictive rows) once scrolling stops. Larger thumbnail sizes admit fewer concurrent decodes while scrolling. Stale queued rows are replaced after a rapid scroll. Clearing the cache does not delete the session file, logs, crash reports, source media, or metadata sidecars. The application has no cloud account or upload workflow; network folders are accessed through normal Windows filesystem paths.

## 12. Production safety

Work from verified media copies whenever practical, especially while evaluating a pre-release. Keep at least one independent backup of irreplaceable photographs, and test copy, move, delete, and metadata settings on expendable files before using them on a live production folder.

### Culling shortcuts

`1`–`5` assign a rating and `0` clears it. Pick, Reject, and Unmark change only the pick state.
`6`–`9` assign Red, Yellow, Green, and Blue labels; `Ctrl+5` assigns Purple and `Ctrl+0` clears
the color label. Reapplying an already authoritative value is a no-op and does not rewrite files.

### Images that fail to load

QuickSift retries a thumbnail or full-size decode up to three times, with a short pause and a
longer time limit on each attempt, before it shows **Couldn't load image**. Locked files (for
example, a file another app is still writing) get the full set of attempts. Unreadable files go
straight to the Windows Shell thumbnail fallback. Failed images are retried automatically when the
file changes on disk or when the folder is reloaded. Press **F5** or **Ctrl+Shift+R** to retry
every failed image immediately.
