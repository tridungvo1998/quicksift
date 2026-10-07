# Security and File-Safety Notes — QuickSift 0.9

## Reporting

Report a suspected security or destructive file-handling issue privately to **tridung.vo.1998@gmail.com**. Include the QuickSift version, Windows version, operation, filesystem type, exported diagnostic log if safe, and the smallest reproducible steps. Do not attach private photographs unless essential and safe to share.

## Native Windows mitigations

The MSVC release target requests:

- `/GS` and `/sdl`
- Control Flow Guard
- exception-handler continuation protection
- ASLR and high-entropy virtual addresses
- DEP compatibility
- CET shadow-stack compatibility
- EH-continuation metadata is available as an opt-in build setting, but is disabled in the bundled static build until every linked library is compiled compatibly
- restricted dependent-DLL loading
- non-incremental optimized Release linking and interprocedural optimization where supported

Startup also applies best-effort DEP, forced-relocation/high-entropy ASLR, heap-corruption termination, System32-first image loading, remote/low-integrity image rejection, and restricted default DLL search. Optional system components are loaded explicitly from System32.

`scripts/verify-binary.ps1` checks the linked PE with `dumpbin`, validates file/product version resources, and reports Authenticode status. Public builds should use `-RequireSignature`.

## File-operation model

- Destination files are never silently overwritten.
- Copy and move lock and revalidate exact source/destination objects before commit; publication uses verified private objects and handle-bound non-overwrite rename.
- Multiple selected media files that share one XMP companion are one atomic media group. A failed owner triggers group compensation instead of stranding metadata.
- Delete moves media and matching companions into a local `Deleted` subfolder through the same grouped transaction path.
- Sidecars are the conservative metadata default. Safe direct writes use an exact source lock, verified private output, canonical pixel-essence comparison, handle-bound publication, and recoverable backup/quarantine paths.
- Guarded replay compares stable file identity, change time, size, and sampled content before deleting or moving a previously produced file.
- Decoder/read-ahead work is blocked and drained around exclusive metadata writes.
- Undo/redo is bounded convenience protection, not backup.

## Diagnostic data

QuickSift writes local session logs and, after an unhandled failure, a text report and minidump. These files may contain:

- local file paths and filenames;
- operation names and timings;
- Windows error codes/messages;
- thread IDs and exception addresses.

They do not intentionally contain decoded image pixels, full EXIF/XMP payloads, passwords, or cloud telemetry. A minidump can nevertheless contain fragments of process memory. Inspect logs before sharing and treat minidumps as potentially sensitive.

Runtime-data locations:

```text
<QuickSift.exe folder>\QuickSiftData\QuickSift.session.ini
<QuickSift.exe folder>\QuickSiftData\.cache
<QuickSift.exe folder>\QuickSiftData\Logs
<QuickSift.exe folder>\QuickSiftData\CrashReports
```

The session file and cache stay local to the QuickSift installation folder. The cache is disposable; the session file contains user preferences and last-session navigation state.

## Dependencies and release policy

Dependencies are pinned through `vcpkg.json` and statically linked in the release configuration. Before every public release:

1. Review the dependency baseline and advisories.
2. Rebuild from a clean source extraction.
3. Run source checks and the native build verification; the former CTest suite is retired.
4. Verify the final PE and signature.
5. Test clean Windows systems plus local, removable, and network storage.
6. Hash the exact distributed archive.

## Scope and limitations

QuickSift parses untrusted images and metadata through multiple codecs. Hardening and tests reduce risk; they do not prove absence of vulnerabilities. Avoid running elevated, keep dependencies current, and retain independent backups of production media.
