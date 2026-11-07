# QuickSift Validation

QuickSift's release build uses source verification, clean native builds, binary checks, and manual regression scenarios. The former automated suite was removed because its timing-sensitive, filesystem-sensitive, UI-sensitive, and environment-sensitive failures were too brittle to serve as a release gate.

## Release validation

The supported release validation path is:

1. Configure and build a clean native x64 Windows Release build with
   `scripts/build.ps1` / `build-release.cmd`.
2. Run `scripts/verify-source.ps1` to validate source structure and release
   contracts.
3. Run the binary verification/signing checks where applicable.
4. Perform the manual functional and destructive-operation checks listed in
   `PRE_RELEASE_CHECKLIST.md`.

Optional deterministic policy check (not a CTest suite; pure math, no timing,
filesystem, or UI): `scripts/checks/face_policy_checks.cpp` covers FaceLock
multi-face ordering, the noise filter, outline hit testing, the 70% click framing,
selection matching, and synthetic per-face quality sanity. Build commands are in
the file header; it must print `49/49 face policy checks passed` (or the current
total) and exit 0.

The other deterministic check programs in `scripts/checks/` follow the same rules:

- `face_interaction_checks.cpp`
- `ratings_exchange_checks.cpp`
- `batch_match_checks.cpp`
- `search_query_checks.cpp`: the Thumbnail search language, the shared predicate engine, the vocabulary and the EXIF capture-time reader.
- `metadata_store_checks.cpp`: the store epoch and snapshots (including incremental snapshot chunks), exhaustive Filter-tab equivalence, and export tokens.
- `write_lane_checks.cpp`: Win32 error classification, the retry state machine (quick 50/100/200/400/800 ms phase, 30 s external phase), edit coalescing and requeue, the user-error table, and the metadata transaction service with a fake disk (retries, Skip, Cancel, Stop, conflicts, the urgent lane and same-path deferral). Time is compressed, so it runs in well under a second.
- `priority_policy_checks.cpp`: the P0–P5 priority model (demand, yielding, pool cap during view loads, search hydration batch sizes, starvation thresholds).
- `lock_order_checks.cpp`: the debug lock-order helper (`core/lock_order.h`) detects inversions and recursive acquisition.
- `localization_checks.cpp`: `Localizer::Text` (exact keys, longest-first fragments, composed details), Vietnamese coverage of the user-error catalog, and the Help text. Link `src/localization.cpp` in as well.
- `animation_checks.cpp`: the central animation system (easing, interruption, reduced motion).
- `design_tokens_checks.cpp`: design tokens (radii, spacing, motion durations).
- `overlay_and_frost_checks.cpp`: left-pane frost, overlay and toast layout.
- `thumbnail_layout_checks.cpp`: the shared thumbnail grid layout and hit testing.
- `filter_index_and_queue_checks.cpp`: the incremental filter-value index and queue ordering.
- `hydrated_facts_checks.cpp`: background hydration of filter facts.
- `jpeg_xmp_checks.cpp`: JPEG/XMP metadata read and write.
- `raw_preview_checks.cpp`: choosing the largest embedded RAW preview and RAW decode timeouts.
- `metadata_progress_checks.cpp`: metadata lane aging and the pending-count lifecycle (no endless "metadata pending").
- `sidecar_write_checks.cpp`: the XMP sidecar write plan (sidecar-first, materialised fields, QuickSift-sidecar completion) and the Exiv2 gate rule that admits the writer thread's own reads (RAW rating/pick self-deadlock regression).
- `ui_state_checks.cpp`: blocking-wait dialog text and log cadence, the early theme setting round trip, the Performance HUD formatter and its placement/clamping and drag-handle hit box.
- `lifecycle_checks.cpp`: the worker shutdown token (`core/worker_lifetime.h`), the 7-day diagnostic-file retention decisions (`core/log_retention.h`), the shared log-and-continue helper (`core/log_and_continue.h`: message format, per-site rate limiting and suppressed counts, and that it never throws, even from a throwing sink), and the previous-session log browser policy (`core/session_log_browser.h`: current session, reparse points, folders and foreign names excluded; newest first; the 60-entry cap; size labels; the 2 MiB tail plan; BOM and partial-line skipping; path-like names rejected).
- `view_load_checks.cpp`: the Single View load rules (`core/view_load_policy.h`: decode cap bounded by the device's maximum bitmap edge, the satisfiable edge, no RAW full re-request once the exact bucket holds a full-quality result, view-lane decodes free of the pan/zoom interaction epoch, a stale running view job never absorbing a new request, the stall-watchdog timing) and decode-budget liveness (oversized reservation admitted after the drain, a cancelled waiter returns with no permit/byte leak, `ShrinkTo`).
- `filmstrip_checks.cpp`: the Single View filmstrip rules (`core/filmstrip_policy.h`: window centring and clamping for empty / 1-image folders, the ends, fast stepping and a filtered-out current image; band and cell hit-testing edges, including the background below the last cell; the image width reserved while the strip is open; toggle placement left of the open strip at 100% and 200% DPI; wheel stepping with precision-wheel accumulation; the grid thumbnail target; the one-thumbnail cap while the image on screen decodes; round 23: the fade target and logical hit-testing, the decode window with its margin and the drop rule for cells that scrolled out, badge sizing, and the blurry-photo outline colours (contrast against both themes' card surfaces) and dash pattern from `core/blur_outline_policy.h`).
- `xmp_interop_checks.cpp` (0.9.6.0): the Lightroom / Capture One mapping in `core/xmp_interop_policy.h`. A reject writes `xmp:Rating=-1` and keeps its stars in `quicksift:Rating`. A Lightroom `-1` reads as a reject. A legacy QuickSift reject stays rejected, and a photo un-rejected in another app reads as not rejected. Every stars × pick round trip is checked, and so is the pick → colour label companion (never replaces a user label).
- `keyboard_shortcut_checks.cpp` (0.9.6.0): the shortcut table (`core/keyboard_shortcuts.h`) has no duplicate binding and every row has a section. It also checks the 0.9.5 key behaviour that `Find` keeps (Ctrl+Z vs Z, Ctrl+Shift+R vs R, F5, Ctrl+digit), the auto-advance policy (setting OR Caps Lock, last photo, a photo that the filters just hid) and the settings migration that makes RAW JPG previews the default.

Every program above is also required by both `scripts/verify-source.ps1` and `scripts/verify-source-portable.py`.

Localization is also checked by `scripts/check-localization.py` (run by `scripts/verify-source-portable.py`; `--self-test` checks the checker). It reports missing Vietnamese translations, Vietnamese text written inline in `src/ui` / `src/app` instead of the table (`inline-language`; the native language name is marked `// loc-native`), English literals passed to pre-localized sinks, concatenated keys, untranslated (identical) entries, placeholder mismatches, duplicates and unaccented translations. Literals that reach `Tr()` through a variable are found through local label tables, `detail` assignments and streamed status text; any other key provider is marked with `// loc-key` or a `// loc-keys-begin` … `// loc-keys-end` block.

Each must exit 0 with `0 failures`. All of them can be linked against the portable `libQuickSiftCore.a`.

## Manual regression philosophy

UI behavior, filesystem operations, cancellation, metadata writing, and other
stateful behavior should be validated through focused manual regression
scenarios on the actual supported Windows environment rather than through a
large collection of brittle timing- or environment-dependent CTest cases.

See `CODE_GUIDE.md` for coding and regression guidance.

## Overflow tooltip trace (0.9.5.0)

Turn on Verbose logging in the diagnostic log window, hover a truncated Settings button (for example "Show diagno…") for about half a second, then look for `[Tooltip]` lines: `marked-truncated` (paint detected the ellipsis), `hover-enter`, `armed`, `timer-fired hot`, `shown at=… size=… visible=1`, then `hover-leave` and `hidden`. The first missing step pinpoints the failure. `scripts/harness/overflow_tooltip_harness.cpp` reproduces the same sequence outside the app.

First-pane timing: start the app, open the left pane once, then open the diagnostic log and look for `[Pane]` lines. `event=pane_open msg=section_4 first_1 ms_…` and `event=pane_first_paint … ms_…` should both be well under 100 ms. `event=pane_reveal … ms_… slowest_… segments_…` names the slowest layout segment. Any `event=pane_slow_paint msg=control_id_… ms_…` line names a child that took 50 ms or more to paint. Later opens and tab switches are logged at TRACE unless they are slow.

## Round-19 maintenance checks (0.9.5.0)

Source rules, enforced by both verifiers. The rule functions in `scripts/verify-source-portable.py` are `verify_worker_lifetime_contracts`, `verify_diagnostic_retention_contracts`, `verify_dead_image_source_cache_removed`, `verify_title_layout_contracts`, `verify_app_state_split_contracts`, `verify_log_and_continue_contracts` and `verify_layout_constants_contracts`. `scripts/verify-source.ps1` has a matching block for each, marked `0.9.5.0 round 19`. Background: CODE_GUIDE §16.

- Workers: no background thread captures raw `this` without a lifetime token, and close and Force quit both stop every worker before teardown (`OnMainWindowDestroyed`: `StopAllWorkersForTeardown` before `StopServicesForShutdown`).
- Diagnostic cleanup: `kMaxAge` is 7 days and deletion goes only through `DeleteOwnDiagnosticFile`, which re-checks the handle and never follows reparse points. The cleanup never recurses or bulk-deletes, never touches the current session log, runs on a cancellable background-priority thread started from `StartDeferredStartup`, and is joined at teardown. The EN and VI Help and `USER_GUIDE.md` must describe the 7-day cleanup.
- The dead `wicImageSources_` cache stays removed.
- `LayoutTitleControls` runs only when its inputs change.
- `QuickSiftApplicationImpl` state lives in the data-only `src/app/state/*.h` structs.
- (No longer enforced.) Keeping the eleven former monolith functions under about 160 lines is a CODE_GUIDE §16 guideline only; neither verifier counts lines.
- No empty `catch (...)` in the Windows app layer. Each one must call `QS_LOG_AND_CONTINUE` or carry `QS_CATCH_SILENT_BY_DESIGN("reason")`. The logger, the crash path, the portable core library and the UI framework are exempt.
- The left-pane section layout, the status bar, the window minimum size and the document windows take their DIP values from `src/ui/ui_layout_constants.h`, never from integer literals.

Manual checks:

1. **Close while busy.** Open a folder with a few hundred large JPEGs and close the window within a second or two, while thumbnails are still decoding. Also try Force quit from the blocking-wait dialog.
   - The process must exit promptly with code 0 and no crash report.
   - The log ends with `event=shutdown_stage msg=stage_app_workers_stop … tokens_left_0`, then the remaining stages and `shutdown_sequence_complete`.
2. **Log retention.**
   - Set up the test files:
     - in `QuickSiftData\Logs`: an old session log (`QuickSift-20260901-101010-123-11.log`), plus a non-matching `QuickSift-notes.log`
     - in `QuickSiftData\CrashReports`: an old minidump and an old crash report
     - all old files set to a last-write time more than 7 days ago
     - a symbolic link (`mklink`) and a junction among the old Logs files
   - About 3 s after startup, the old QuickSift files are gone, and each one is logged as `event=old_file_deleted`.
   - Everything else must remain, and each link must survive along with its target: the current log, recent files, other names, files in the wrong folder, and the links themselves. Only real Windows can test the links; under Wine a Unix symlink looks like a plain file.
3. **Recovered failures.**
   - Lines of the form `<site> failed and continued: <reason>` appear at Warning level.
   - Under a repeated failure, at most 3 lines per call site appear in 30 s. The next allowed line reports `(N similar suppressed)`.
4. **Layout.** Every left-pane tab, the Settings pane and the document windows (Help, About, Diagnostic log) must look exactly as in the previous build at 100% and 150% DPI. That includes the close light and its hover and press, resizing, and the minimum window size.

## Round-20 checks (0.9.5.0)

Source rules, enforced by both verifiers (`verify_session_log_browser_contracts` and `verify_build_failure_help_contracts`, plus `0.9.5.0 round 20` blocks in `verify-source.ps1`):

- The session log browser is read-only. The controller has no delete, move or file-open call. `ReadSessionLogTail` validates the name, opens with `GENERIC_READ` + `FILE_FLAG_OPEN_REPARSE_POINT`, rejects reparse points and reads through `PlanTail`. The worker uses a lifetime token, teardown joins it, its IDs are in `ui_command_ids.h`, and `lifecycle_checks` runs `SessionLogBrowserChecks`.
- Every build script dot-sources `scripts/build-failure-help.ps1` (ASCII only) and has a trap that prints the explanation and exits 1. The `.cmd` wrappers keep `%ERRORLEVEL%` and explain a missing PowerShell.

Manual checks:

1. **Previous sessions.** Start QuickSift a few times, then open **Settings → Diagnostic log → Previous sessions…**.
   - The list is newest first with start time and size, without the current session. With no earlier logs it says "No previous session logs".
   - Choosing a log opens a read-only window titled "Previous session log — <time · size>". A log over 2 MB starts at a whole line and shows the "Showing the newest …" note.
   - Check it in Vietnamese, dark and light themes, and at 100% and 150% DPI. Put a symbolic link named like a session log into `Logs`: it must not be listed. Close the main window while a large log is loading: it must exit cleanly.
2. **Build failures.** On a test machine, run `build.cmd` with Visual Studio's C++ workload removed, or with `QS_SIGN_CERT_SHA1` set to a wrong thumbprint.
   - Each run ends with `Build failed:` / `What to do:` / `Technical detail:` and a non-zero `%ERRORLEVEL%`.
   - A successful build prints nothing new.

## Optimization regression gate (0.9.6.0)

`optimization_checks.cpp` covers transparent key/hash equivalence and existing path semantics; Compare key invalidation (bounds, DPI, generation, mode, ordered identity, aspect/rotation); Shell negative-entry identity/size/expiry and stale request sequence rejection after retry/device reset; deterministic latest-wins save ordering, unchanged-save skipping, observable failures, retry and final shutdown drain; metadata queue append rollback and invalid catalog order; bounded scan batches; low-space/overflow/query-expiry decisions; protected eviction eligibility, rank and incoming-byte caps; indexed Compare retention/pane cleanup. `raw_preview_checks.cpp` also covers outer and entropy-loop cancellation, throwing probes and truncated JPEGs.

Both source verifiers enforce asynchronous paint, the single restricted self-launch, inherited handle allowlisting, suspended child/job assignment, bounded cancellation waits, final session drains, scratch reuse and retained cache flush/atomic-publication primitives. Run all existing checks as well as the new suite. These are invariant checks, not Windows performance measurements.

Windows manual gate:
- Broken/unsupported JPEG/RAW/HEIC: fallback success, no fallback available, handler crash and a deliberately unresponsive test handler; close and switch folders during extraction. Confirm no child survives app exit. Test packaged and portable launches, including restrictive job/process policies and paths with spaces/non-ASCII characters.
- Large folders (1k/10k/100k): first results remain progressive, ordering/filter counts stay stable, metadata never outranks decode; inject allocation failure during queue tail preparation/catalog commit and verify rollback.
- Compare 2–6 mixed orientations: async dimensions, rotation, pin/unpin, resize/fullscreen, 100/150/200% DPI, theme/high contrast, display-driver reset. Painting and hit-testing must agree.
- Change multiple settings rapidly, close immediately, reopen and check settings/migration/first-run flags. Disk-full/read-only settings folder: final-save error prevents silent loss; retry after restoring access. Exercise normal close, Force quit and Windows logoff/cancelled shutdown.
- Saturate GPU cache with thumbnails, Full, Preview and tiles; revisit active/visible photos, replace lower-resolution surfaces, force device loss and memory pressure. Check byte accounting and protected-photo behavior.
- Cache disk fills or directories are deleted while writing: repair/reject safely; corrupt/truncate cache entries and verify checksum rejection/regeneration. No durability setting for originals/XMP/session may change.
- Rapid Single View navigation through large RAWs while scanning; cancelled scans must not continue into fallback decoding or display stale previews. Verify Lightroom interop, auto-advance, shortcut sheet, filmstrip badges/fade and all EN/VI help still work.

Off-Windows object compilation and portable/sanitizer checks do not substitute for these UI, handler, packaging and driver tests.
