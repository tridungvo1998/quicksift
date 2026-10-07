# QuickSift 0.9 R4 Safety Source Validation

Validation date: 2026-08-07

## Completed on the packaging host

- Cross-platform source/architecture verifier: passed.
- Fresh CMake Release configuration outside the source tree: passed.
- C++20 warnings-as-errors build of every portable target: passed.
- Clang AddressSanitizer and UndefinedBehaviorSanitizer warnings-as-errors build: passed.
- The release gate no longer depends on timing-sensitive automated test suites.
- GCC ThreadSanitizer passed the background-completion queue, metadata-service, file-service, and transaction-coordinator lifecycle targets (4/4).
- Source inventory found 65 production `.cpp` files and no unregistered source file.
- Conflict audit found no merge markers, duplicate registered source entries, stale pre-R4 transaction APIs, raw pointer `PostMessage` payloads, or CMake/source-registration conflicts.
- JSON policies parsed successfully and `git diff --check` found no whitespace errors.
- The source verifier enforces atomic multi-owner/shared-companion media grouping, group-preserving history compensation, strong guarded identity, explicit scanner error status, no-throw completion draining, conservative metadata recovery, popup message isolation, and handle-bound publication.
- The source verifier additionally enforces one shared thumbnail-layout owner, nonblocking folder-scan cancellation, cheap folder discovery without per-file fingerprint hydration, one executable-adjacent `QuickSiftData` runtime root (`QuickSift.session.ini`, `.cache`, `Logs`, `CrashReports`), 20-session retention, and fail-closed startup when the data root is unusable.

## Portable build validation


## R4 safety regressions covered

- Multiple selected images sharing one XMP companion commit or roll back as one media group.
- Undo/Redo retains group boundaries and compensates already-replayed members after a later failure.
- Same-size, timestamp-preserving content edits are rejected by sampled-content guarded identity.
- Duplicate catalog paths are rejected before mutation and throwing edits preserve the original record.
- History retention obeys its configured cap even for a final oversized entry.
- Missing metadata-completion bits remain incomplete rather than deleting Undo information.
- Metadata exception recovery preserves completed bits and truthful total failure counts.
- Scanner completion carries success, partial, or failure status instead of representing enumeration errors as an empty success.
- Completion draining does not transfer ownership until the consumer container exists.
- Transaction cancellation reaches an active backend and remains distinguishable from ordinary I/O failure.

## Native validation still required

This package was prepared on a non-Windows host. The Win32 application, Direct2D/Direct3D integration, Exiv2/WIC safe embedded-write path, Windows handle-bound publication, native `QuickSift.UiFoundation` framework smoke test, Windows SDK code, PowerShell verifier, and Windows-only tests could not be compiled or executed here.

Before public release, run on x64 Windows with the supported Visual Studio and Windows SDK toolchain:

```bat
build.cmd
```

Then run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify-source.ps1
```

Complete `PRE_RELEASE_CHECKLIST.md`, especially shared-sidecar groups, grouped Undo/Redo compensation, timestamp-preserving external edits, publication-path substitution, scanner access/device errors, shutdown during metadata writes, disk-full, sharing-violation, removable/network-media, DPI, and renderer-device-loss scenarios.

Portable compilation and source-verifier results are useful evidence for the extracted policy and transaction modules. They do not substitute for a clean native Windows build and runtime validation.
