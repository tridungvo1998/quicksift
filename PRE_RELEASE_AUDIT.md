# QuickSift 0.9 — Exhaustive Bug Sweep and Maintainability Audit

## RC1 source-readiness update — 2026-08-07

The 0.9.3.0 RC source baseline now has runtime package-aware persistence. Portable/unpackaged execution uses executable-adjacent `QuickSiftData`; MSIX execution uses the package family `LocalState`. Session, cache, logs, and crash reports are all children of that single active root. Packaged execution has no writable-executable fallback. Pre-rename product migration/XMP compatibility and older SQLite schema migration have been removed at the public-release boundary.

Source-side RC gates completed in this environment: strict warnings-as-errors portable build and source verification. The RC is **not yet a releasable Windows binary** until the unchecked native gates in `PRE_RELEASE_CHECKLIST.md` are completed, especially MSVC `build-release.cmd -RequireSignature`, Windows diagnostics/data migration tests, clean-machine Win10/Win11 checks, PE mitigation/signature verification, Defender/reputation checks, and destructive-operation testing on expendable media.

## R4 media-safety repair status — 2026-08-06

The R4 source pass repairs the audit findings around shared XMP ownership, grouped Undo/Redo compensation, timestamp-preserving external changes, scanner error reporting, low-memory completion ownership, catalog collision handling, metadata partial completion, strict history limits, atomic session/XMP publication, embedded pixel-essence verification, popup reentrancy isolation, and Windows shutdown coordination. Portable Release, ASan/UBSan, and GCC TSan gates pass. Native Windows compilation and hostile-filesystem/runtime validation remain mandatory before release.

> **Historical audit note:** this report records the state before the compiled-module and serialized-transaction refactor. Current ownership and completion semantics are defined in `ARCHITECTURE.md`, `CODE_GUIDE.md`, and `TESTING.md`. Re-run every native Windows release gate before treating earlier pass statements as current evidence.

Date: 2026-08-05  
Scope: architecture ownership, UI/native lifetime, background work, prefetch arithmetic, metadata/XMP, cache validation, file transactions, diagnostics, documentation, source guardrails, and portable sanitizer/test coverage.

## Conclusion

This sweep found and fixed several concrete defects, including one metadata corruption bug and one scheduler-priority bug. It also converted the project documentation from descriptive notes into an enforceable maintenance contract.

The portable core and source structure pass strict Release plus combined AddressSanitizer/UndefinedBehaviorSanitizer testing. The native Windows executable still requires the documented MSVC/Win32 validation gate; this Linux environment cannot truthfully compile or exercise it.

## Major fixes from this sweep

### Fixed: priority promotion did not promote queue position

When a pending decode request was requested again at a higher priority, the job's priority field changed but the job could remain in its original lower-priority deque. A newly visible thumbnail could therefore wait behind stale predictive or idle work despite being labeled `Visible`.

Promotion now removes the job from its old lane and inserts it at the front of the correct higher-priority lane. The ranking and promotion mechanics live in `src/core/work_queue_policy.h` and have deterministic regression coverage.

### Fixed: predictive prefetch accepted non-finite/extreme geometry

Viewport offsets, dimensions, timing values, and velocity calculations could receive NaN, infinity, or extreme values. Converting those values to integer rows is undefined or implementation-dependent and could produce invalid ranges or pathological scheduling.

`PrefetchPlanner` now validates finite values before arithmetic and float-to-integer conversion, bounds derived row indices, expires invalid timing samples, and caps prediction depth. Tests cover NaN, infinity, invalid row height, extreme offsets, reversal, expiry, bounds, and duplicate prevention.

### Fixed: first XMP write could create malformed XML

QuickSift's default sidecar uses a self-closing `rdf:Description .../>`. The previous insertion logic could place a new attribute after the `/`, producing malformed XML on the first rating, label, or pick write.

XMP simple-property access now belongs to `src/metadata/xmp_simple_property.*`. It:

- inserts attributes before the self-closing slash;
- preserves whitespace and unknown content;
- replaces existing attributes or simple elements;
- avoids duplicate namespace declarations;
- XML-escapes inserted values;
- refuses to rewrite unfamiliar documents without a usable `rdf:Description`.

Regression tests include the exact self-closing document produced by QuickSift.


### Fixed: guarded Undo used rename-sensitive file timestamps

The previous destination fingerprint included the Windows change timestamp. A successful rename can change that timestamp without changing file contents or identity, which could make a newly completed operation immediately appear externally modified and disable guarded Undo.

Undo now uses a stable identity stamp built from volume/file identity and file size. Rename-sensitive metadata is excluded. Regression tests verify that renaming preserves identity while replacement or content-size changes do not.

### Fixed: history replay state could remain stuck after an exception

Undo and Redo previously set the history-replay flag manually. An exception could leave that flag enabled and suppress later history recording. `ScopedBooleanFlag` now restores the previous state through RAII, including nested and exceptional paths.

### Fixed: strict XMP reading and writing disagreed

Sidecar reads could previously accept commented-out, duplicate, similarly named, case-mismatched, or incorrectly namespace-bound properties that the writer would refuse or interpret differently. Reading and writing now share the same conservative `xmp_simple_property` owner.

The parser now:

- respects XML case sensitivity and XML-name boundaries;
- ignores comments, CDATA, and processing instructions when locating live properties;
- rejects duplicate or unsupported property representations rather than guessing;
- validates namespace bindings before reading or updating existing properties;
- caches compiled property and namespace patterns per worker thread;
- updates one unambiguous attribute or simple text element, or refuses the operation.

### Fixed: bitmap-cache payload corruption was not detectable

The bitmap-cache format previously validated dimensions and lengths but could display payload bytes that had silently changed on disk. Cache format v7 now checksums the complete cache header and pixel payload. Mutation tests flip every individual bit in representative cache data and require corruption to be rejected and removed.

### Fixed: metadata queue completion could stall under allocation failure

Under extreme memory pressure, allocation of a small posted-result envelope could fail after background work had already completed. The UI-side in-flight count could then remain permanently nonzero. A no-allocation abort/completion message now releases progress accounting, and metadata counters increase only after the work engine confirms queue acceptance.

### Fixed: worker-to-UI message delivery and path matching edge cases

Background results are now retained by synchronized completion queues and private Win32 messages are payload-free wake-ups. File and metadata mutations use serialized transaction services; direct metadata batches coordinate decoder exclusion once across the affected paths using Windows case-insensitive path matching.

### Fixed: GDI resource refresh could discard the last working resource

Font and theme-resource recreation now creates replacements before destroying valid existing objects. A transient GDI allocation failure therefore preserves the last working UI resources rather than degrading the interface.

### Retained and rechecked earlier reliability fixes

- Decode-budget leases release reservations on allocation failure and shutdown wakes blocked reservations.
- Exclusive metadata writes coordinate with read-ahead queueing and draining.
- Stale queued thumbnail work is replaced when the viewport changes.
- Required child controls, subclasses, timers, document windows, monitor geometry, and theme resources use checked failure paths.
- Manual diagnostic export verifies the newly written output rather than accepting an old destination file.
- Crash reporting is re-entry-safe and preserves recent diagnostic context where possible.
- Persistent cache records reject corrupt dimensions, sizes, states, timestamps, rectangles, booleans, and non-finite values.
- Verified copy/move and guarded undo retain source authority until destination flush and verification succeed.

## Maintainability work

Added `CODE_GUIDE.md` as the first-stop map for humans and LLM coding assistants. It defines:

- the five-layer architecture;
- the exact owner for common changes;
- runtime stories for folder loading, scrolling, metadata, file operations, and diagnostics;
- feature-addition and bug-fix recipes;
- asynchronous ownership and posted-message rules;
- arithmetic, native-resource, naming, and commenting rules;
- a strict change contract and definition of done.

Major source and test components now begin with concise `CODE GUIDE` and `OWNER` headers. The durable explanation remains in the guide; source comments only identify ownership and the relevant invariant. Portable source verification rejects components that omit those headers.

The central rule is: **every decision has one owner; extend that owner instead of creating a second copy of the rule.**

## Automated validation completed

### Strict portable Release build

- C++20 configuration completed.
- Warnings treated as errors.
- Source verification passed; the former automated CTest executables are no longer part of the release gate.

### AddressSanitizer and UndefinedBehaviorSanitizer

- Core and persistent-cache tests built together with `-fsanitize=address,undefined`.
- Leak detection, halt-on-error, and undefined-behavior stack traces enabled.
- All portable tests passed.

### Source guardrails

Portable verification validates:

- version consistency;
- JSON/XML and documentation links;
- CMake source references;
- UI ownership boundaries;
- command/control ID uniqueness (124 IDs);
- persistent control catalog consistency (40 controls);
- named subclass/property/timer contracts;
- checked UI timer routing;
- `main.cpp`, public/private application-header, and compiled-module size limits;
- single authoritative registries;
- required `CODE GUIDE`/`OWNER` headers;
- absence of generated build artifacts and obsolete source debris in the release tree.

## Tests added or expanded

- Work-priority ranking and physical queue promotion.
- Self-closing and ordinary XMP description updates.
- Existing XMP attribute and element replacement.
- Namespace preservation and XML escaping.
- Refusal to destructively rewrite unfamiliar XMP.
- Non-finite and extreme prefetch geometry.
- Direction reversal, velocity expiry, bounds, ordering, and duplicate prevention.
- Metadata value parsing and invalid-value rejection.
- Cache corruption and invalid-record handling.

## Final source-package validation

The release source tree is packaged without generated build directories, analyzer output, bytecode caches, or obsolete patch artifacts. The final ZIP is extracted into a fresh directory, source guardrails are rerun, and the strict Release build and verification gates are executed from the extracted copy.

## Build and Visual Studio readiness

The source package now contains the build entry points that earlier documentation referenced but did not actually include:

- `build.cmd` for a verified Debug build;
- `build-release.cmd` for hardened Release verification and packaging;
- `open-in-visual-studio.cmd` for generated-solution configuration and launch;
- `scripts/build.ps1` as the single owner of prerequisite discovery, vcpkg bootstrap, CMake generation, building, tests, optional signing, and packaging;
- `.vsconfig` plus Visual Studio 2026/2022 CMake presets.

CMake now selects `QuickSift` as the generated Visual Studio startup project and applies one static MSVC runtime policy to every target. The latter prevents Windows-only runtime-library mismatches between tests and static vcpkg dependencies. Generated solutions and dependency trees are stored under `%LOCALAPPDATA%\QuickSiftBuild`, outside the source tree.

The portable source verifier requires these entry points and contracts. Native solution generation and compilation still need to be executed on Windows.

## Windows validation still required

Run `build-release.cmd` on supported Windows/Visual Studio and complete the full checklist before public release:

- compile the native application with MSVC warnings as errors;
- run Windows runtime and UI validation scenarios;
- verify posted-message ownership and shutdown while scanning/decoding;
- test rapid wheel, keyboard, scrollbar, and direction-reversal behavior on large folders;
- test local, removable, network, slow, and disconnected storage;
- exercise JPEG/TIFF/PNG/RAW metadata and XMP sidecars using a representative corpus;
- force locked-file, collision, disk-full, source-changing, and cross-volume failures;
- test copy/move/delete/undo only on disposable media copies;
- force diagnostics export and crash/minidump creation in a disposable build;
- verify linked PE mitigations, version resources, dependencies, and Authenticode signature.

## Honest release assessment

The architecture and portable logic are substantially safer and easier to modify than the original monolith, and the specific bugs found in this sweep have regression coverage. However, no amount of Linux-side source analysis substitutes for running the actual Win32 executable under MSVC, real codecs, real camera files, and hostile filesystem conditions. Public release should remain gated on that Windows validation.

## Visual Studio build-pipeline correction

The Windows release entry point was rechecked after its first real Visual Studio 2026 run. Two packaging defects were corrected:

- Windows and portable source verifiers no longer impose arbitrary per-file line-count limits. They continue to enforce structural boundaries such as CMake registration, the application PIMPL boundary, and the thin process entry point.
- The build script previously looked for `QuickSift.exe` in the root of the generated solution directory. Visual Studio is a multi-configuration generator, so the executable is now resolved from the selected configuration directory, such as `cmake-vs18\Release\QuickSift.exe`.

The portable source verifier checks that the Windows verifier continues to consume the shared policy and that the build script retains configuration-aware output handling.

## Visual Studio 2026 solution-format compatibility

The build pipeline detects both Visual Studio solution formats. CMake's Visual Studio 18 2026 generator emits `QuickSift.slnx`; Visual Studio 17 2022 emits `QuickSift.sln`. Build, configure-only, and IDE-launch paths use the generated file that actually exists instead of assuming the legacy extension.

## Visual Studio native-compile corrections (R4)

The first complete MSVC compile exposed five Windows-only issues that portable compilers could not instantiate. They are corrected in this revision:

- combined `MINIDUMP_TYPE` flags are explicitly cast before `MiniDumpWriteDump`;
- `INVALID_HANDLE_VALUE` is no longer declared as a `constexpr` pointer value;
- the common-dialog declaration required by `CommDlgExtendedError` is included;
- `WM_GETMINMAXINFO` uses explicit `LONG` arithmetic instead of ambiguous `int`/`LONG` template deduction;
- the modern WIC image-source cache now has a concrete file-identity implementation based on stable file ID, last-write time, and size.

Both source verifiers now encode these native compile contracts. The exact Windows executable must still be rebuilt and tested on the supported Visual Studio machine.

## R5 native-link correction

- Removed unconditional `/guard:ehcont` from the statically bundled executable after MSVC 18 correctly rejected Exiv2, LibRaw, TurboJPEG, libyuv, and other static objects that lack compatible EH-continuation metadata.
- Retained CFG, CET compatibility, ASLR, high-entropy VA, DEP, stack protection, and restricted dependent-DLL loading.
- Added an explicit `QS_ENABLE_EHCONT` opt-in for a future all-compatible dependency build.
- Explicitly forbids `/FORCE:GUARDEHCONT`, because forcing incomplete unwind metadata can make exception handling unsafe.
- Release builds now clean the generated CMake tree while preserving the dependency cache, avoiding stale object/runtime settings.
- Aligned the application output with the build script at `cmake-vsXX/Release/QuickSift.exe`; the earlier target property placed the executable in the build root.
- Replaced manual TurboJPEG/libyuv library discovery with configuration-aware vcpkg imported targets, preventing Debug archives and `LIBCMTD` directives from leaking into a Release link.


MSIX release preparation now includes runtime package detection, immutable-package write guards, Partner Center identity placeholders, required corresponding-source URL, package visual assets, and deterministic `package-msix.bat`/`makeappx.exe` staging. Native Store certification and install/uninstall behavior remain Windows release gates.
