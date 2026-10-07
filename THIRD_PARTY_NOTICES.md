# Third-party Dependency Inventory — QuickSift 0.9.3.0 RC

QuickSift's release build uses the dependency set pinned by `vcpkg.json`:

- C++/WinRT — Windows Runtime projections used by native Windows features.
- Exiv2 0.28.8 — image metadata and XMP handling; GPL-2.0-or-later.
- SQLite — persistent metadata cache.
- LibRaw — RAW image decoding.
- libavif with dav1d — AVIF decoding.
- libwebp — WebP decoding.
- libjpeg-turbo — accelerated JPEG decoding.
- libyuv — pixel-format conversion.

This inventory is provided for release preparation; it is **not** a replacement for the license and copyright notices required by each dependency. Before public distribution, collect the exact notice/license files from the resolved vcpkg packages used for the final build, review their terms, and include the required material with the distributed archive or installer.

Windows system libraries and Windows Imaging Component codecs are supplied by the operating system or separately installed codec providers and are not redistributed in the portable QuickSift executable.


## QuickSift license

QuickSift itself is distributed under GPL-2.0-or-later. The full GPL version 2 text is in `LICENSE`. Before public binary distribution, the release must also provide the exact corresponding QuickSift source and all third-party notices/source obligations applicable to the dependency versions actually linked into that binary.
