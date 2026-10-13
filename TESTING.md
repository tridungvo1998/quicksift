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

Localization is also checked by `scripts/check-localization.py` (run by `scripts/verify-source-portable.py`; `--self-test` checks the checker). It reports missing Vietnamese translations, English literals passed to pre-localized sinks, concatenated keys, untranslated (identical) entries, placeholder mismatches, duplicates and unaccented translations. Literals that reach `Tr()` through a variable are found through local label tables, `detail` assignments and streamed status text; any other key provider is marked with `// loc-key` or a `// loc-keys-begin` … `// loc-keys-end` block.

Each must exit 0 with `0 failures`. All of them can be linked against the portable `libQuickSiftCore.a`.

## Manual regression philosophy

UI behavior, filesystem operations, cancellation, metadata writing, and other
stateful behavior should be validated through focused manual regression
scenarios on the actual supported Windows environment rather than through a
large collection of brittle timing- or environment-dependent CTest cases.

See `CODE_GUIDE.md` for coding and regression guidance.
