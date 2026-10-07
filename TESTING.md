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


## Manual regression philosophy

UI behavior, filesystem operations, cancellation, metadata writing, and other
stateful behavior should be validated through focused manual regression
scenarios on the actual supported Windows environment rather than through a
large collection of brittle timing- or environment-dependent CTest cases.

See `CODE_GUIDE.md` for coding and regression guidance.
