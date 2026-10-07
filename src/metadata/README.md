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

It is **not** a general XML or RDF engine. Unsupported documents must fail without modification. Do not broaden it by adding loose regular expressions in `main.cpp`; add a focused test first and keep rejected input byte-for-byte unchanged.

### `jpeg_orientation.*`

Owns bounded parsing of JPEG APP1/Exif orientation. Every marker length, TIFF offset, entry count, and value offset must remain inside the APP1 segment. Malformed input returns no orientation; it is never guessed.

## Where neighboring work belongs

- Embedded JPEG/TIFF/PNG metadata transactions: `../embedded_metadata.*`
- Sidecar file reading, encoding preservation, atomic publication, and metadata precedence: `../main.cpp` composition adapters, with transaction behavior in `../ui/metadata_history_and_file_safety.cpp`
- Background metadata scheduling: `../work/background_work_engine.cpp`
- Persistent metadata records: `../persistent_cache.*`
- Metadata UI and history: `../ui/metadata_history_and_file_safety.cpp`

## Change checklist

1. Identify whether the change is parsing, XMP structure, JPEG binary structure, transaction safety, or UI behavior.
2. Edit the single owner listed above.
3. Add the smallest corrupt, boundary, duplicate, or unsupported input to `tests/core_tests.cpp` when portable.
4. Keep reads and writes symmetrical: a value written by QuickSift must be readable by the same owner.
5. Reject malformed values instead of clamping them into legitimate-looking metadata.
6. Run the validation sequence in `../../TESTING.md`.
