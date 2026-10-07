# QuickSift 0.9 Modularization Notes

## What changed

- `src/main.cpp` is a thin process entry point.
- All former implementation fragments are gone; CMake separately compiles the UI modules.
- Catalog containers/path indexing are owned by `CatalogStore`; callers cannot mutate its backing vectors.
- Selection membership and range anchors are owned by `SelectionStore`.
- Undo/Redo stacks and memory accounting are owned by `HistoryStore`; callers cannot bypass trimming or replay transitions.
- Folder enumeration is separated from decoder execution in `FolderScanner`.
- Worker/scanner results are owned by `BackgroundCompletionQueue`; Win32 completion messages carry no object pointers.
- `TransactionCoordinator` owns cross-domain admission, tokens, cancellation, and close/logoff policy.
- File mutation is planned by the UI and executed serially by `FileTransactionService` through an injected locked-operation backend.
- Metadata mutation and metadata history replay are serialized by `MetadataTransactionService`.
- Both worker threads start only after their observed members are constructed; worker-side callback snapshots use shared ownership rather than allocating `std::function` copies.
- File and metadata Undo/Redo use the same asynchronous transaction paths as forward operations.
- Direct metadata batches perform one decoder-exclusion handoff for all affected paths.
- Native copy and verification observe cancellation during large files; publication and history still stop only at safe boundaries, and partial completion preserves truthful history.
- Per-item callback exceptions are contained so already completed file and metadata items remain represented accurately.
- `MacOsUiFramework` is the single owner for ordinary macOS-style native component creation, interaction state, and rendering; feature modules declare presentations and layout instead of maintaining independent paint implementations.
- Portable popup, toast, and alert behavior lives in `ui_models`; dropdowns use one `MenuItem` model rather than parallel visual and command arrays.
- Specialized photo bitmap/tile composition remains in the canvas, while cards, borders, badges, labels, alerts, and other reusable chrome call the framework.

## Preserved safety guarantees

- no destination overwrite;
- private temporary publication;
- flush and byte comparison for copies;
- source retirement only after final destination verification;
- reparse-point rejection and locked-handle identity checks;
- identity-guarded rollback and Undo/Redo;
- source preservation on ambiguous or partial failure;
- conservative XMP and embedded-metadata handling.

## Automated validation

The former CTest suite has been retired. Portable validation now focuses on the
warnings-as-errors build, source-structure checks, and sanitizer compilation
where supported. Native Windows behavior remains covered by the release build,
binary/source verification, and the manual regression matrix.

## Required final release validation

This source package was prepared on a non-Windows host. It still requires:

1. a clean Visual Studio/Windows SDK warnings-as-errors build of the application;
2. `scripts/verify-source.ps1`;
3. manual Direct2D/Direct3D, DPI, shutdown, removable/network-media, sharing-violation, disk-full, and destructive-operation scenarios;
4. signing, clean-machine reputation testing, and final release packaging.

Do not interpret a portable pass as proof that the native executable is release-ready.
