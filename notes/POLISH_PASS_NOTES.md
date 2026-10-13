# QuickSift 0.9.4.0 — Polishing pass notes (Oct 9 2026)

Tree: /workspace/quicksift/QuickSift-0.9.3.1-filmstrip-final-repair (not renamed, not zipped).
All prior features and decisions kept, including "view jumps to the face when results arrive".

## Localization
- New `scripts/check-localization.py` (with self-test), run by `scripts/verify-source-portable.py`.
  Reports missing VI text, English passed to pre-localized sinks, untranslated (identical) entries,
  placeholder mismatches and concatenated keys.
- First run: 196 missing keys + 6 untranslated status tags, plus a fragment-order mistranslation bug.
- About 211 VI entries added (139 exact strings, ~66 detail fragments + 12 composed-conflict sentences,
  6 status tags). Help (EN/VI) gained Search, Saving ratings (write lane), Import/Export sections.
- Now: 406 exact keys, 241 fragments, 526 source keys, no findings. `localization_checks`: 80 checks.
- Ratings export format is always English (documented in USER_GUIDE and Help).
- Fixed-width spots: status-bar tags and short labels use short VI forms; actual pixel fit not measured (needs a Windows run).

## Bugs fixed
1. A refused edit left optimistic values on screen -> previous values restored.
2. A rapid edit during a running write got a modal refusal -> now coalesced and flushed next.
3. Search wake `PostMessageW` failure could stall results -> flag reset, retried.
4. Ratings/batch completion posts could be lost -> flag reset + OnTimer backstop.
5. Sharing violations were misclassified -> ClassifyWin32Error maps them to InUse.
6. UI-thread disk I/O for post-write stamps and storage preflight -> moved to the metadata worker.
7. Search hydration opened files gated for writing -> deferred until the gate releases.
8. Retry waits were uninterruptible -> `condition_.wait_for` (Stop/Cancel/Skip end them at once).
9. `hwnd_` race in completion sinks -> HWND captured by value.
10. Full catalog snapshot copied per epoch -> chunked snapshot, incremental rebuild.
11. Short localization fragments pre-empted longer ones -> exact hash match + longest-first fragments.
12. `Tr(L"All")` and status filter tags untranslated -> translated.
13. Logger wrote to disk on the calling thread (every Warning) and under the producer lock -> writer thread, separate stream mutex.
14. UI could block on the cache DB mutex (writer/async open) -> 30 ms bounded wait, then defer/retry.
15. Full paths in Worker/FaceDetection/Decode/Catalog logs -> file name + path_id.
16. Logging inside `noexcept` worker catch blocks could throw -> wrapped.

Reviewed, no change needed: search precedence, batch matching, ratings import, face-click state
machine, outline fade, batch glass re-show timer, OnTimer backstop ordering.

## Priority model (P0–P5)
P0 UI > P1 metadata writes > P2 Single/Compare view load > P3 search reads > P4 visible thumbnails > P5 background.
Demand signal of atomics; bounded cooperative yields; pool capped to 1 thumbnail decode during a view decode;
face/analysis paused during search; adaptive search batch sizes; OS hints (metadata writer NORMAL,
ratings/batch BELOW_NORMAL, background lanes background mode). Details: ARCHITECTURE.md.

## Write-lock flow
Edit shown immediately -> coalesced queue -> metadata thread -> our readers release the file ->
quick retries 50/100/200/400/800/1000 ms (~3 s) -> file in use by another app: retry every 1 s up to 30 s
with a "waiting" toast -> Retry / Skip. Stops immediately only on corruption risk (conflict, low storage,
read-only, verification) and restores previous values with a localized message. UI never blocks.

## Logging
New categories (Scheduler, WriteLane, Search, RatingsIO, Batch, FaceLock, ViewLane, ThumbReliability,
MetadataStore, DbMigration); structured key=value events without query text or full paths;
rate limiter for hot paths (verbose-only traces); `UserErrorCode` -> localized messages;
crash report "Subsystem state" section; CODE_GUIDE §5.6 updated.

## Verification (Linux box)
- `cmake -DQS_WARNINGS_AS_ERRORS=ON` fresh configure + build: OK.
- Checks: ratings 70, face_interaction 58, face_policy 49/49, batch 62, search 168, metadata_store 31,
  write_lane 90, priority 44, lock_order 9, localization 80 — all pass.
- `verify-source-portable.py`: passed. `check-localization.py` and `--self-test`: passed.
- MinGW syntax check on every touched Win32 file: clean apart from known SDK gaps
  (WIC JPEG/progress types in background_work_engine.cpp, D2DERR sign-compare).

## Windows risks (not testable here)
- No real MSVC/Windows build or run on this box.
- WIC progressive/JPEG-index code is only covered by MSVC (MinGW headers lack it).
- `std::timed_mutex` timing for the 30 ms UI cache wait; deferred cache writes are dropped if the DB never opens.
- The exclusive gate is held during the 30 s external-lock wait (view cannot decode that file meanwhile).
- WipeFolderCache/RemoveLibraryFolder still run on the UI thread (rare, explicit action).
- Ratings import reports in-use files in its summary (no Retry); a nested urgent edit is cancelled if the outer plan is cancelled; urgent/outer Undo order may swap.
