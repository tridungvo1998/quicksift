// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Decoder API; keep format routing aligned with core/image_formats.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace quicksift {

struct DecodedImageData {
    int width = 0;
    int height = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    bool embeddedPreview = false;
    std::vector<std::uint8_t> pixels;
};

struct DecodeOptions {
    int targetSize = 0;
    bool preferEmbeddedPreview = false;
    bool rawJpegPreviewOnly = false;
    bool allowTurboJpeg = true;
    bool prefetchEncodedInput = false;
    std::uint64_t mappedInputLimit = 256ull * 1024ull * 1024ull;
    std::function<bool()> isCancelled;
};

void ConfigureBundledCodecRuntime(
    unsigned maxThreads,
    std::uint64_t maxDecodedPixelBytes,
    std::uint64_t mappedInputLimit,
    bool enableTurboJpeg);

bool BundledCodecSupports(const std::filesystem::path& path);
bool DecodeWithBundledCodec(
    const std::filesystem::path& path,
    const DecodeOptions& options,
    DecodedImageData& output);

} // namespace quicksift
