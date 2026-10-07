// CODE GUIDE: See CODE_GUIDE.md -> "Reading metadata".
// OWNER: Embedded metadata API; callers use narrow verified results rather than container details.

#pragma once

#include "core/metadata_state.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace quicksift {

enum class EmbeddedMetadataFailure {
    None,
    InvalidValue,
    LowStorage,
    UnsupportedFormat,
    UnsafePath,
    SourceChanged,
    WriteFailed,
    VerificationFailed
};

using EmbeddedMetadataValues = quicksift::core::PhotoMetadataValues;

struct EmbeddedMetadataWriteResult {
    bool ok = false;
    EmbeddedMetadataFailure failure = EmbeddedMetadataFailure::WriteFailed;
    std::uint64_t requiredBytes = 0;
    std::uint64_t availableBytes = 0;
    std::wstring detail;
    quicksift::core::PhotoMetadataValues before;
    quicksift::core::PhotoMetadataValues after;
};

bool EmbeddedMetadataAvailable() noexcept;

// Culling writes outrank background metadata reads on the process-wide Exiv2
// gate. Interest is announced before a write waits for decoders, so a metadata
// backlog cannot start another read ahead of rating/pick updates. Pair every
// successful prefer with a release. In-flight Exiv2 calls still finish; the
// library is not safe to preempt.
void PreferEmbeddedMetadataWrites() noexcept;
void ReleaseEmbeddedMetadataWritePreference() noexcept;
[[nodiscard]] bool EmbeddedMetadataWritePreferred() noexcept;

std::optional<EmbeddedMetadataValues> ReadEmbeddedMetadata(
    const std::filesystem::path& image) noexcept;


EmbeddedMetadataWriteResult WriteEmbeddedMetadataAtomic(
    const std::filesystem::path& image,
    const quicksift::core::MetadataPatch& patch) noexcept;

EmbeddedMetadataWriteResult WriteEmbeddedMetadataInPlace(
    const std::filesystem::path& image,
    const quicksift::core::MetadataPatch& patch) noexcept;


} // namespace quicksift
