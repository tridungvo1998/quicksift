# Metadata Code Map

Read [`../../CODE_GUIDE.md`](../../CODE_GUIDE.md) first. This folder owns **small, deterministic metadata rules**. It does not own UI state, background queues, filesystem transactions, or Exiv2 container I/O.

## Components

### `metadata_value_parser.*`

Owns conversion from untrusted metadata text into QuickSift's narrow value domains:

- rating: `0..5`
- color label: `0..5`
- pick state: `-1..1`

Use `ParseBoundedInteger`, `ParseColorLabel`, and `AreMetadataValuesValid`. Do not add `stoi`, clamping, or a second label-name table at a call site.

### `xmp_simple_property.*`

Owns the deliberately narrow XMP subset QuickSift needs for rating, label, and pick values. It can:

- read one unambiguous qualified attribute;
- read one text-only qualified element;
- update either representation;
- insert a new attribute into an existing `rdf:Description`;
- preserve unknown XML and refuse ambiguous or malformed structures.

It is **not** a general XML or RDF engine. Unsupported documents must fail without modification. Do not broaden it by adding loose regular expressions at a call site; add a focused check first and keep rejected input byte-for-byte unchanged.

### `jpeg_orientation.*`

Owns bounded parsing of JPEG APP1/Exif orientation. Every marker length, TIFF offset, entry count, and value offset must remain inside the APP1 segment. Malformed input returns no orientation; it is never guessed.

## Where neighboring work belongs

- Embedded JPEG/TIFF/PNG metadata transactions: `../embedded_metadata.*`
- Sidecar file reading/writing and metadata precedence: `../app/application_support.cpp` (`SidecarPathFor`, `ReadSidecarMetadata`, `WritePhotoMetadataPatch`, `ReadPhotoMetadata`); the sidecar write plan is `../core/sidecar_write_policy.h`
- Encoding preservation and atomic text publication: `../app/application_text_io.cpp`
- JPEG XMP packet location: `jpeg_xmp_locator.*` (this folder)
- Exiv2 gate admission: `../core/metadata_gate_policy.h` (gate in `../embedded_metadata.cpp`)
- Canonical rating/pick/label tokens: `../core/metadata_vocabulary.h`
- Background metadata scheduling: `../work/background_work_engine.cpp` (`ExecuteMetadataReadJob`); lane admission, retries and queue order: `../core/metadata_lane_policy.h`, `metadata_progress.h`, `metadata_queue_order.h`
- Write-lane retry and coalescing: `../core/write_retry_policy.h`, `../core/metadata_edit_queue.h`, `../ui/metadata_write_lane_controller.cpp`
- Persistent metadata records: `../persistent_cache.*`
- Metadata UI and history: `../ui/metadata_history_and_file_safety.cpp`

## Change checklist

1. Identify whether the change is parsing, XMP structure, JPEG binary structure, transaction safety, or UI behavior.
2. Edit the single owner listed above.
3. Add the smallest corrupt, boundary, duplicate, or unsupported input to the matching `../../scripts/checks/*.cpp` program (`jpeg_xmp_checks.cpp`, `sidecar_write_checks.cpp`, …) when portable.
4. Keep reads and writes symmetrical: a value written by QuickSift must be readable by the same owner.
5. Reject malformed values instead of clamping them into legitimate-looking metadata.
6. Run the validation sequence in `../../TESTING.md`.
