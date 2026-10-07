# QuickSift 0.9.3.0 RC

QuickSift is a native Windows photo-culling application built for fast, careful review of production media. Version **0.9.3.0 RC** is a release candidate: the feature set is substantially complete, but the native Windows binary still requires final build, signing, clean-machine, and destructive-operation validation before public release.

## Current feature set

- **Three review modes:** thumbnail grid, single-image view, and synchronized multi-image comparison.
- **Fast culling:** 0–5 star ratings, pick/reject marks, color labels, range selection, filtering, and sorting.
- **Focused inspection:** fit/fill/100% zoom, pan, synchronized compare view, display-only rotation, fullscreen, and face lock.
- **Quality assistance:** background face detection and a conservative warning when the largest detected face appears clearly blurred or out of focus.
- **Production file handling:** conflict-checked copy, move, and guarded delete-to-`Deleted` operations, with bounded undo/redo.
- **RAW/JPEG pairing:** optional copy/move of recognized same-base-name RAW files and the shared XMP sidecar.
- **Metadata:** EXIF inspection, XMP sidecars, and configurable direct metadata writes using temporary-file, validation, flush, and replacement safeguards.
- **Broad decoding:** JPEG, PNG, TIFF, BMP, GIF, WebP, AVIF, common LibRaw formats, and installed Windows Imaging Component codecs such as HEIF/HEIC where available.
- **Responsive loading:** current viewport work outranks direction-predictive rows; stale off-screen thumbnail queues are replaced as the user scrolls; memory, CPU, GPU, storage, read-ahead, and cache limits adapt to the machine.
- **Diagnostics:** themed in-app log viewer, manual UTF-8 log export, persistent session logs, automatic crash text reports, and minidumps.
- **Interface:** English/Vietnamese localization, per-monitor DPI awareness, dark/light themes, and long-path-aware file access.

## Architecture and validation

The former 13,000-line application body is split into separately compiled responsibility-named modules, and `src/main.cpp` is now a roughly 60-line entry point. Catalog storage, Undo/Redo, folder scanning, cross-thread completion ownership, verified file transactions, and metadata transactions have independent services or stores. Worker messages are payload-free wake-ups, while synchronized queues retain result ownership. The release gate uses source verification, clean native builds, binary checks, and targeted manual regression scenarios; the former CTest suite has been retired.

Start with [CODE_GUIDE.md](CODE_GUIDE.md) for the plain-English map and change recipes. Use [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md), [ARCHITECTURE.md](ARCHITECTURE.md), and [src/ui/README.md](src/ui/README.md) for deeper ownership details; see [TESTING.md](TESTING.md) for automated and manual validation.

## Quick start

1. Double-click `build.cmd` for a development build, or `open-in-visual-studio.cmd` to generate and open the Visual Studio solution (`.slnx` on VS 2026, `.sln` on VS 2022).
2. Open a folder containing photographs.
3. Cull with `1`–`5`, clear with `0`, and use Pick, Reject, labels, filters, and sorting.
4. Select multiple photographs and switch to Compare for synchronized close inspection.
5. Review destinations before Copy or Move. QuickSift stops on conflicts rather than overwriting or silently inventing a filename.

## Diagnostics and runtime data

Open **Settings → Diagnostic log** to inspect recent events, refresh the viewer, or export a text snapshot. QuickSift detects Windows package identity at runtime and uses one canonical writable data root:

- **Portable/unpackaged:** `QuickSiftData` beside `QuickSift.exe`.
- **MSIX/Store:** `%LOCALAPPDATA%\Packages\<QuickSift package family>\LocalState`.

Under that root, session preferences are `QuickSift.session.ini`, the disposable persistent cache is `.cache`, session logs are in `Logs`, and crash reports/minidumps are in `CrashReports`. The newest 20 session logs and newest 20 crash incidents are retained. A packaged process never falls back to writing beside its read-only packaged executable. If the active data root cannot be created and written, QuickSift shows a startup error and refuses to launch.

Logs may contain local paths and filenames. Inspect them before sharing.

## Important file-safety behavior

- Display rotation never modifies source images.
- Delete moves files and matching XMP sidecars into a `Deleted` subfolder; it does not use the Recycle Bin.
- XMP sidecars are the conservative default where direct writes are riskier.
- Copy, move, delete, and metadata operations re-check file identity and conflict conditions. Unexpected states stop the operation.
- Keep an independent backup of irreplaceable media. No desktop application should be the only copy of a shoot.

## Building and release status

Use `build.cmd` for a verified Debug build, `build.bat` for the portable Release/package path, `package-msix.bat` for the Microsoft Store MSIX path, and `open-in-visual-studio.cmd` for a generated solution with QuickSift as the startup target. Both distribution paths build the same native application behavior; package identity only changes where QuickSift-owned runtime state is stored. Build products stay under `%LOCALAPPDATA%\QuickSiftBuild` rather than polluting the source tree.

Read [BUILDING.md](BUILDING.md), [USER_GUIDE.md](USER_GUIDE.md), [SECURITY.md](SECURITY.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and [PRE_RELEASE_CHECKLIST.md](PRE_RELEASE_CHECKLIST.md) before distribution.

## License and author

QuickSift is free software licensed under **GPL-2.0-or-later**. See `LICENSE` and `SOURCE_CODE.txt`.

Copyright © 2026 Vo Hoang Tri Dung  
https://quicksift.pages.dev/

Selection is deliberately limited to the current visible filter results, so hidden photographs never remain invisible command targets. Metadata-dependent filters wait for authoritative metadata rather than treating unread or unsupported values as empty.
