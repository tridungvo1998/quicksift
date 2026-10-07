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

In Compare mode, **Sync** keeps zoom and pan aligned between panes. **Face Lock** uses the largest detected face as the navigation anchor when face data is available. **F11** enters fullscreen; the title bar retracts toward the top edge and reappears when the pointer rests there briefly.

Rotate left/right changes the display orientation only. It never rotates the source file.

A red border around a thumbnail means the largest detected face appears clearly blurred or out of focus according to QuickSift's conservative analysis. Treat this as a prompt to inspect at 100%, not as an automatic rejection verdict.

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

## 9. Metadata behavior

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
