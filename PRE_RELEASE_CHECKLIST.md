# QuickSift 0.9 — Pre-release Checklist

## Product and legal

- [ ] Choose and include an explicit distribution license.
- [ ] Confirm author/copyright, support email, icon ownership, and third-party notices.
- [ ] Confirm every visible version, file property, manifest identity, package name, and document reads 0.9.

## Clean build and validation

- [ ] Start from a fresh source extraction on Windows 10/11 x64.
- [ ] Run `scripts\verify-source.ps1`.
- [ ] Run `build-release.cmd -RequireSignature` with release signing variables configured.
- [ ] Confirm the clean native Release build succeeds and the source verifier passes.
- [ ] Confirm `verify-binary.ps1` passes version, CFG, ASLR, high-entropy VA, DEP, CET, dependent-load, security-cookie, and Authenticode checks. EH continuation is optional until bundled static dependencies support it.
- [ ] Record the archive SHA-256 and preserve the exact signed artifact/source pair.

## Architecture gate

- [ ] `src/main.cpp` remains under the enforced composition-root line limit.
- [ ] New filtering, sorting, scheduling, accounting, or safety policy has one module owner and a deterministic validation scenario when practical.
- [ ] No generated build output, backup source copies, handwritten Visual Studio projects, stale patch notes, or duplicate command IDs are present.
- [ ] Persistent main-window controls are declared once in `src/ui/ui_control_catalog.h`; creation, area membership, flyout membership, and title widths do not have parallel lists.

## Unified UI checks

- [ ] Confirm source verification passes UI ID, control-catalog membership, subclass ID, property-key, native-window ownership, and timer-routing checks.
- [ ] Open Help, About, and Diagnostic Log in both themes at 100%, 125%, 150%, and 200% DPI.
- [ ] Exercise popups and maximize sizing across multiple monitors, then repeat after monitor disconnect/reconnect.
- [ ] In a disposable diagnostic build, force required child-control, subclass, and timer failures; verify clean failure and actionable log entries.

## Runtime and responsiveness

- [ ] Launch as a standard non-administrator on clean Windows 10 and Windows 11.
- [ ] Test 100%, 150%, 200%, mixed-DPI, light, dark, and fullscreen.
- [ ] Test JPEG, PNG, TIFF, WebP, AVIF, HEIF/HEIC where available, and representative RAW files.
- [ ] Test thumbnail, single, compare, zoom, pan, synchronized view, face lock, rotation, filters, sorting, and language switching.
- [ ] Rapidly wheel/touchpad-scroll a large uncached folder in both directions; current rows should recover first and old rows must not dominate the queue.
- [ ] Reverse scroll direction while decoding, resize the window, change thumbnail size, clear cache, and repeat on HDD/removable/network storage.
- [ ] Test low-memory pressure, rapid cancellation, app close during background work, and cache rebuild.

## Diagnostics and runtime data

- [ ] Launch beside a writable folder and confirm `QuickSiftData\QuickSift.session.ini`, `QuickSiftData\.cache`, `QuickSiftData\Logs`, and `QuickSiftData\CrashReports` are the only QuickSift runtime-data locations created.
- [ ] Place an earlier 0.9 `QuickSift.session.ini`/`QuickSift.cache` beside the executable and confirm first launch migrates them into `QuickSiftData` without losing preferences or cache validity.
- [ ] Confirm an unwritable application folder produces the startup error and refuses to launch rather than falling back to LocalAppData.
- [ ] Open **Settings → Diagnostic log** in light and dark themes.
- [ ] Verify Refresh, Export, Close, keyboard focus, DPI scaling, and long export paths.
- [ ] Confirm exported logs are UTF-8 and include current version/lifecycle events.
- [ ] Trigger a controlled crash in a disposable debug build; confirm text report, minidump, and `LATEST_CRASH.txt` are created.
- [ ] Review logs/minidumps for privacy before sharing.

## Metadata and file safety

Use expendable copies for every destructive test.

- [ ] Test rating, pick/reject, labels, sidecars, safe/direct metadata writes, undo, and redo.
- [ ] Interrupt a metadata write where practical and confirm the original remains valid.
- [ ] Test copy/move/delete on NTFS, removable media, long paths, read-only sources, and network shares.
- [ ] Test conflicts, case-only names, locked files, changed source identity, insufficient space, unplugged media, and partial batches.
- [ ] Test RAW/JPEG pairing with no RAW, one RAW, multiple RAW formats including `.ari`/`.cr3`, shared XMP, and unrelated same-base-name files.
- [ ] Confirm delete uses the expected `Deleted` folder and never silently overwrites.

## Security and reputation

- [ ] Review the pinned vcpkg baseline and advisories.
- [ ] Confirm the final archive contains only the signed executable and intended documentation.
- [ ] Scan the exact final artifact with current Microsoft Defender definitions.
- [ ] Submit the signed build through the chosen reputation/distribution process.
- [ ] Publish the SHA-256 over HTTPS from the official page.
- [ ] Keep signing keys outside source and logs.

## Release gate

- [ ] No open release-blocking crash, corruption, destructive-operation, responsiveness, diagnostics, signing, mitigation, or clean-machine issue.
- [ ] Documentation matches tested behavior.
- [ ] Final package is reproducibly associated with the tagged 0.9 source state.


## MSIX / Microsoft Store

- [ ] Reserve QuickSift in Partner Center and copy the exact Package/Identity Name and Publisher values.
- [ ] Publish the exact GPL-2.0-or-later corresponding source and set `QS_SOURCE_URL` to that immutable release/tag/archive URL.
- [ ] Run `package-msix.bat` and confirm `makeappx.exe` validates the manifest/package.
- [ ] Install a test-signed MSIX and confirm QuickSift-owned state appears only under package `LocalState`.
- [ ] Confirm no file is created or modified inside the package installation directory.
- [ ] Repeat photo open/cull/metadata/copy/move/delete tests and compare behavior with the portable build.
- [ ] Verify Start-menu identity, icon assets, version `0.9.3.0`, publisher display name, uninstall, reinstall, and update behavior.
- [ ] Submit the final package to Store certification and archive the submitted MSIX SHA-256 plus corresponding source SHA-256.
