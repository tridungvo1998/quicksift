// CODE GUIDE: See CODE_GUIDE.md -> "Rules for sizes and arithmetic".
// OWNER: Image decoder implementation; validate dimensions and byte counts before allocation.

#include "bundled_codecs.h"
#include "core/image_formats.h"
#include "metadata/jpeg_orientation.h"
#include "runtime_support.h"
#include "app/application_support.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#if defined(_WIN32)
#include <chrono>
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdint>
#if !defined(_WIN32)
#include <fstream>
#endif
#include <limits>
#include <cwctype>
#include <memory>
#include <semaphore>
#include <span>
#include <string>
#include <thread>
#include <utility>

#if defined(QS_USE_LIBRAW)
#include <libraw/libraw.h>
#endif
#if defined(QS_USE_LIBAVIF)
#include <avif/avif.h>
#endif
#if defined(QS_USE_WEBP)
#include <webp/decode.h>
#endif
#if defined(QS_USE_TURBOJPEG)
#include <turbojpeg.h>
#endif
#if defined(QS_USE_LIBYUV)
#include <libyuv/scale_argb.h>
#endif

namespace fs = std::filesystem;

namespace quicksift {
namespace {

#if !defined(_WIN32)
// Test/portable fallback with the same small interface as the Windows mapping
// wrapper. Production Windows builds retain file mappings, access hints and
// targeted PrefetchVirtualMemory; non-Windows sanitizer builds read once.
class MappedFileView {
public:
    enum class AccessPattern { Sequential, Random };
    bool Open(const fs::path& path, AccessPattern, std::uint64_t maximumBytes, bool) {
        bytes_.clear();
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) return false;
        const std::streamoff length = stream.tellg();
        if (length <= 0 || static_cast<std::uint64_t>(length) > maximumBytes ||
            static_cast<std::uint64_t>(length) > SIZE_MAX) return false;
        bytes_.resize(static_cast<std::size_t>(length));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(bytes_.data()), length);
        if (!stream) { bytes_.clear(); return false; }
        return true;
    }
    std::span<const std::uint8_t> Bytes() const noexcept { return bytes_; }
private:
    std::vector<std::uint8_t> bytes_;
};
#endif

std::atomic<std::uint64_t> gMappedInputLimit{ 256ull * 1024ull * 1024ull };
std::atomic<unsigned> gCodecThreadLimit{ 1u };
std::atomic<bool> gTurboJpegEnabled{ true };

// TurboJPEG itself is single-decode-session oriented in this path. Run up to
// six independent sessions concurrently instead of serializing JPEG work or
// creating an unbounded number of simultaneous decoder sessions. The worker
// pool supplies the actual parallelism; this gate only caps JPEG sessions.
constexpr std::ptrdiff_t kMaxParallelTurboJpegDecoders = 6;
std::counting_semaphore<kMaxParallelTurboJpegDecoders> gTurboJpegSlots(
    kMaxParallelTurboJpegDecoders);

#if defined(QS_USE_LIBRAW) || defined(QS_USE_LIBAVIF) || defined(QS_USE_WEBP) || defined(QS_USE_TURBOJPEG)
bool Cancelled(const DecodeOptions& options) noexcept {
    if (!options.isCancelled) return false;
    try {
        return options.isCancelled();
    } catch (...) {
        // Cancellation hooks can originate in UI/worker state. Treat a broken
        // hook as cancellation rather than allowing C++ exceptions to cross a
        // third-party C decoder callback boundary.
        return true;
    }
}
#endif

std::wstring ExtensionLower(const fs::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(value)));
    });
    return extension;
}


#if defined(QS_USE_LIBRAW)
struct LibRawImageDeleter {
    void operator()(libraw_processed_image_t* image) const noexcept {
        if (image) LibRaw::dcraw_clear_mem(image);
    }
};

int LibRawProgress(void* context, enum LibRaw_progress, int, int) noexcept {
    const auto* options = static_cast<const DecodeOptions*>(context);
    return options && Cancelled(*options) ? 1 : 0;
}
#endif

#if defined(QS_USE_LIBRAW)
std::string Utf8Path(const fs::path& path) {
#if defined(_WIN32)
    const std::wstring wide = path.wstring();
    if (wide.empty() || wide.size() > static_cast<std::size_t>(INT_MAX)) return {};
    const int wideLength = static_cast<int>(wide.size());
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string utf8(static_cast<size_t>(needed), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wideLength,
        utf8.data(), needed, nullptr, nullptr) != needed) return {};
    return utf8;
#else
    return path.string();
#endif
}
#endif

#if defined(QS_USE_LIBRAW) || defined(QS_USE_LIBAVIF) || defined(QS_USE_WEBP) || defined(QS_USE_TURBOJPEG)
bool CheckedBgraBytes(int width, int height, std::size_t& bytes) {
    if (width <= 0 || height <= 0) return false;
    const std::uint64_t byteCount = static_cast<std::uint64_t>(width) *
        static_cast<std::uint64_t>(height) * 4ull;
    if (byteCount == 0 || byteCount > quicksift::app::gMaximumDecodedPixelBytes.load(std::memory_order_relaxed) ||
        byteCount > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) return false;
    bytes = static_cast<std::size_t>(byteCount);
    return true;
}

void CalculateTarget(int sourceWidth, int sourceHeight, int targetSize,
    int& outputWidth, int& outputHeight) {
    outputWidth = sourceWidth;
    outputHeight = sourceHeight;
    if (targetSize <= 0 || std::max(sourceWidth, sourceHeight) <= targetSize) return;
    const double scale = static_cast<double>(targetSize) / std::max(sourceWidth, sourceHeight);
    outputWidth = std::max(1, static_cast<int>(std::lround(sourceWidth * scale)));
    outputHeight = std::max(1, static_cast<int>(std::lround(sourceHeight * scale)));
}
#endif

#if defined(QS_USE_TURBOJPEG)
void ScaleBgra(const std::uint8_t* source, int sourceWidth, int sourceHeight,
    std::size_t sourceStride, int outputWidth, int outputHeight,
    std::vector<std::uint8_t>& destination) {
    std::size_t outputBytes = 0;
    if (!source || !CheckedBgraBytes(outputWidth, outputHeight, outputBytes)) return;
    destination.resize(outputBytes);
#if defined(QS_USE_LIBYUV)
    if (sourceStride <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
        outputWidth <= std::numeric_limits<int>::max() / 4 &&
        libyuv::ARGBScale(source, static_cast<int>(sourceStride), sourceWidth, sourceHeight,
            destination.data(), outputWidth * 4, outputWidth, outputHeight,
            libyuv::kFilterBilinear) == 0) return;
#endif
    for (int y = 0; y < outputHeight; ++y) {
        const int sourceY = std::min(sourceHeight - 1,
            static_cast<int>(static_cast<std::int64_t>(y) * sourceHeight / outputHeight));
        for (int x = 0; x < outputWidth; ++x) {
            const int sourceX = std::min(sourceWidth - 1,
                static_cast<int>(static_cast<std::int64_t>(x) * sourceWidth / outputWidth));
            std::memcpy(destination.data() + (static_cast<std::size_t>(y) * outputWidth + x) * 4,
                source + static_cast<std::size_t>(sourceY) * sourceStride + static_cast<std::size_t>(sourceX) * 4, 4);
        }
    }
}
#endif

#if defined(QS_USE_LIBRAW)
void ConvertRgbToBgraResized(const std::uint8_t* source, int sourceWidth, int sourceHeight,
    std::size_t sourceStride, int sourceChannels, int targetSize, DecodedImageData& output) {
    if (!source || sourceWidth <= 0 || sourceHeight <= 0 || sourceChannels < 3 ||
        sourceStride < static_cast<std::size_t>(sourceWidth) * static_cast<std::size_t>(sourceChannels)) return;
    int outputWidth = sourceWidth;
    int outputHeight = sourceHeight;
    CalculateTarget(sourceWidth, sourceHeight, targetSize, outputWidth, outputHeight);
    std::size_t outputBytes = 0;
    if (!CheckedBgraBytes(outputWidth, outputHeight, outputBytes)) return;
    output.sourceWidth = sourceWidth;
    output.sourceHeight = sourceHeight;
    output.width = outputWidth;
    output.height = outputHeight;
    output.pixels.resize(outputBytes);
    for (int y = 0; y < outputHeight; ++y) {
        const int sourceY = std::min(sourceHeight - 1,
            static_cast<int>(static_cast<std::int64_t>(y) * sourceHeight / outputHeight));
        for (int x = 0; x < outputWidth; ++x) {
            const int sourceX = std::min(sourceWidth - 1,
                static_cast<int>(static_cast<std::int64_t>(x) * sourceWidth / outputWidth));
            const std::uint8_t* input = source + static_cast<std::size_t>(sourceY) * sourceStride +
                static_cast<std::size_t>(sourceX) * static_cast<std::size_t>(sourceChannels);
            std::uint8_t* destination = output.pixels.data() +
                (static_cast<std::size_t>(y) * outputWidth + x) * 4;
            destination[0] = input[2];
            destination[1] = input[1];
            destination[2] = input[0];
            destination[3] = 255;
        }
    }
}
#endif

#if defined(QS_USE_TURBOJPEG)
void ApplyOrientationAndResize(int orientation, int targetSize,
    int& width, int& height, std::vector<std::uint8_t>& pixels) {
    if (orientation < 1 || orientation > 8 || width <= 0 || height <= 0 || pixels.empty()) return;
    const bool swapAxes = orientation >= 5;
    const int orientedWidth = swapAxes ? height : width;
    const int orientedHeight = swapAxes ? width : height;
    int outputWidth = orientedWidth;
    int outputHeight = orientedHeight;
    CalculateTarget(orientedWidth, orientedHeight, targetSize, outputWidth, outputHeight);
    if (orientation == 1) {
        if (outputWidth == width && outputHeight == height) return;
        std::vector<std::uint8_t> scaled;
        ScaleBgra(pixels.data(), width, height, static_cast<std::size_t>(width) * 4,
            outputWidth, outputHeight, scaled);
        if (!scaled.empty()) {
            pixels.swap(scaled);
            width = outputWidth;
            height = outputHeight;
        }
        return;
    }

    std::size_t bytes = 0;
    if (!CheckedBgraBytes(outputWidth, outputHeight, bytes)) return;
    std::vector<std::uint8_t> transformed(bytes);
    auto sourceAt = [&](int x, int y) {
        return pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4;
    };
    for (int y = 0; y < outputHeight; ++y) {
        const int orientedY = std::min(orientedHeight - 1,
            static_cast<int>(static_cast<std::int64_t>(y) * orientedHeight / outputHeight));
        for (int x = 0; x < outputWidth; ++x) {
            const int orientedX = std::min(orientedWidth - 1,
                static_cast<int>(static_cast<std::int64_t>(x) * orientedWidth / outputWidth));
            int sourceX = orientedX;
            int sourceY = orientedY;
            switch (orientation) {
            case 2: sourceX = width - 1 - orientedX; sourceY = orientedY; break;
            case 3: sourceX = width - 1 - orientedX; sourceY = height - 1 - orientedY; break;
            case 4: sourceX = orientedX; sourceY = height - 1 - orientedY; break;
            case 5: sourceX = orientedY; sourceY = orientedX; break;
            case 6: sourceX = orientedY; sourceY = height - 1 - orientedX; break;
            case 7: sourceX = width - 1 - orientedY; sourceY = height - 1 - orientedX; break;
            case 8: sourceX = width - 1 - orientedY; sourceY = orientedX; break;
            default: break;
            }
            sourceX = std::clamp(sourceX, 0, width - 1);
            sourceY = std::clamp(sourceY, 0, height - 1);
            std::memcpy(transformed.data() +
                (static_cast<std::size_t>(y) * outputWidth + x) * 4,
                sourceAt(sourceX, sourceY), 4);
        }
    }
    pixels.swap(transformed);
    width = outputWidth;
    height = outputHeight;
}

bool DecodeJpegBytes(std::span<const std::uint8_t> encoded, const DecodeOptions& options,
    DecodedImageData& output, bool embeddedPreview) {
    if (encoded.empty() || Cancelled(options)) return false;

    // Each invocation owns its own TurboJPEG handle. The semaphore limits only
    // the number of JPEG decode sessions executing at once; it does not reduce
    // the application worker pool or throttle other image formats.
    while (!gTurboJpegSlots.try_acquire_for(std::chrono::milliseconds(5))) {
        if (Cancelled(options)) return false;
    }
    struct SlotGuard {
        ~SlotGuard() { gTurboJpegSlots.release(); }
    } slotGuard;

    tjhandle decoder = tjInitDecompress();
    if (!decoder) return false;
    struct Guard { tjhandle handle; ~Guard() { if (handle) tjDestroy(handle); } } guard{ decoder };

    int width = 0, height = 0, subsamp = 0, colorSpace = 0;
    if (tjDecompressHeader3(decoder, encoded.data(), static_cast<unsigned long>(encoded.size()),
        &width, &height, &subsamp, &colorSpace) != 0 || width <= 0 || height <= 0) return false;

    const int orientation = quicksift::metadata::ReadJpegOrientation(encoded);
    const bool swapAxes = orientation >= 5;
    const int orientedSourceWidth = swapAxes ? height : width;
    const int orientedSourceHeight = swapAxes ? width : height;

    int scaledWidth = width;
    int scaledHeight = height;
    int factorCount = 0;
    tjscalingfactor* factors = tjGetScalingFactors(&factorCount);
    if (options.targetSize > 0 && factors && factorCount > 0) {
        int bestWidth = width, bestHeight = height;
        std::uint64_t bestArea = static_cast<std::uint64_t>(width) * height;
        for (int i = 0; i < factorCount; ++i) {
            const int candidateWidth = TJSCALED(width, factors[i]);
            const int candidateHeight = TJSCALED(height, factors[i]);
            const int candidateLongest = std::max(candidateWidth, candidateHeight);
            const std::uint64_t area = static_cast<std::uint64_t>(candidateWidth) * candidateHeight;
            if (candidateLongest >= options.targetSize && area < bestArea) {
                bestWidth = candidateWidth;
                bestHeight = candidateHeight;
                bestArea = area;
            }
        }
        scaledWidth = bestWidth;
        scaledHeight = bestHeight;
    }

    std::size_t bytes = 0;
    if (!CheckedBgraBytes(scaledWidth, scaledHeight, bytes) ||
        scaledWidth > std::numeric_limits<int>::max() / 4) return false;
    output.pixels.resize(bytes);
    const int flags = TJFLAG_FASTDCT | (options.targetSize <= 1024 ? TJFLAG_FASTUPSAMPLE : 0);
    if (tjDecompress2(decoder, encoded.data(), static_cast<unsigned long>(encoded.size()),
        output.pixels.data(), scaledWidth, scaledWidth * 4, scaledHeight, TJPF_BGRA, flags) != 0 ||
        Cancelled(options)) {
        output.pixels.clear();
        return false;
    }

    int currentWidth = scaledWidth;
    int currentHeight = scaledHeight;
    ApplyOrientationAndResize(orientation, options.targetSize,
        currentWidth, currentHeight, output.pixels);
    output.sourceWidth = orientedSourceWidth;
    output.sourceHeight = orientedSourceHeight;
    output.width = currentWidth;
    output.height = currentHeight;
    output.embeddedPreview = embeddedPreview;
    return !output.pixels.empty();
}

bool DecodeJpeg(const fs::path& path, const DecodeOptions& options, DecodedImageData& output) {
    if (!gTurboJpegEnabled.load(std::memory_order_relaxed) || !options.allowTurboJpeg) return false;
    MappedFileView mapped;
    const std::uint64_t limit = std::min(options.mappedInputLimit,
        gMappedInputLimit.load(std::memory_order_relaxed));
    if (!mapped.Open(path, MappedFileView::AccessPattern::Sequential, limit,
        options.prefetchEncodedInput)) return false;
    return DecodeJpegBytes(mapped.Bytes(), options, output, false);
}
#endif

#if defined(QS_USE_LIBRAW)
void SetRawSourceGeometry(DecodedImageData& decoded, int rawWidth, int rawHeight) {
    int sourceWidth = rawWidth > 0 ? rawWidth : decoded.sourceWidth;
    int sourceHeight = rawHeight > 0 ? rawHeight : decoded.sourceHeight;
    if (sourceWidth > 0 && sourceHeight > 0 && decoded.width > 0 && decoded.height > 0 &&
        sourceWidth != sourceHeight && decoded.width != decoded.height) {
        const bool sourcePortrait = sourceHeight > sourceWidth;
        const bool decodedPortrait = decoded.height > decoded.width;
        if (sourcePortrait != decodedPortrait) std::swap(sourceWidth, sourceHeight);
    }
    decoded.sourceWidth = sourceWidth;
    decoded.sourceHeight = sourceHeight;
}

bool DecodeRawEmbedded(LibRaw& processor, const DecodeOptions& options,
    int rawWidth, int rawHeight, DecodedImageData& output) {
    if (!options.preferEmbeddedPreview || Cancelled(options)) return false;
    if (processor.unpack_thumb() != LIBRAW_SUCCESS || Cancelled(options)) return false;
    int error = LIBRAW_SUCCESS;
    std::unique_ptr<libraw_processed_image_t, LibRawImageDeleter> thumbnail(
        processor.dcraw_make_mem_thumb(&error));
    if (!thumbnail || error != LIBRAW_SUCCESS) return false;

    DecodedImageData decoded;
    if (options.rawJpegPreviewOnly && thumbnail->type != LIBRAW_IMAGE_JPEG) return false;
    if (thumbnail->type == LIBRAW_IMAGE_JPEG) {
#if defined(QS_USE_TURBOJPEG)
        DecodeOptions previewOptions = options;
        previewOptions.preferEmbeddedPreview = false;
        if (!DecodeJpegBytes({ thumbnail->data, thumbnail->data_size }, previewOptions, decoded, true)) return false;
#else
        return false;
#endif
    } else if (thumbnail->type == LIBRAW_IMAGE_BITMAP && thumbnail->bits == 8 &&
        (thumbnail->colors == 3 || thumbnail->colors == 4) &&
        thumbnail->width > 0 && thumbnail->height > 0 &&
        std::in_range<int>(thumbnail->width) && std::in_range<int>(thumbnail->height)) {
        const int thumbnailWidth = static_cast<int>(thumbnail->width);
        const int thumbnailHeight = static_cast<int>(thumbnail->height);
        const std::uint64_t stride64 = static_cast<std::uint64_t>(thumbnailWidth) * thumbnail->colors;
        const std::uint64_t requiredBytes = stride64 * static_cast<std::uint64_t>(thumbnailHeight);
        if (stride64 > std::numeric_limits<std::size_t>::max() ||
            requiredBytes > thumbnail->data_size) return false;
        ConvertRgbToBgraResized(thumbnail->data, thumbnailWidth,
            thumbnailHeight, static_cast<std::size_t>(stride64), thumbnail->colors,
            options.targetSize, decoded);
        decoded.embeddedPreview = true;
    } else {
        return false;
    }

    if (decoded.pixels.empty()) return false;
    // Preserve full RAW dimensions even though the pixels came from an embedded JPEG,
    // while matching those dimensions to the orientation of the decoded preview.
    SetRawSourceGeometry(decoded, rawWidth, rawHeight);
    const int longest = std::max(decoded.width, decoded.height);
    const std::int64_t minimumPreview = options.targetSize > 0
        ? std::max<std::int64_t>(256, static_cast<std::int64_t>(options.targetSize) * 3 / 4)
        : 0;
    if (!options.rawJpegPreviewOnly && minimumPreview > 0 &&
        static_cast<std::int64_t>(longest) < minimumPreview) return false;
    output = std::move(decoded);
    return true;
}

bool DecodeRaw(const fs::path& path, const DecodeOptions& options, DecodedImageData& output) {
    LibRaw processor;
    processor.set_progress_handler(LibRawProgress, const_cast<DecodeOptions*>(&options));
#ifdef _WIN32
    const int openResult = processor.open_file(path.c_str());
#else
    const std::string utf8 = Utf8Path(path);
    const int openResult = processor.open_file(utf8.c_str());
#endif
    if (openResult != LIBRAW_SUCCESS || Cancelled(options)) return false;

    const int rawWidth = static_cast<int>(processor.imgdata.sizes.width);
    const int rawHeight = static_cast<int>(processor.imgdata.sizes.height);
    if (DecodeRawEmbedded(processor, options, rawWidth, rawHeight, output)) return true;
    if (options.rawJpegPreviewOnly || Cancelled(options)) return false;

    const int longest = std::max(rawWidth, rawHeight);
    const std::uint64_t fullBgraBytes = static_cast<std::uint64_t>(std::max(0, rawWidth)) *
        static_cast<std::uint64_t>(std::max(0, rawHeight)) * 4ull;
    processor.imgdata.params.output_bps = 8;
    processor.imgdata.params.use_camera_wb = 1;
    processor.imgdata.params.no_auto_bright = 1;
    processor.imgdata.params.output_color = 1;
    processor.imgdata.params.half_size =
        (options.targetSize > 0 && static_cast<std::int64_t>(longest) >
            static_cast<std::int64_t>(options.targetSize) * 2) ||
        fullBgraBytes > quicksift::app::gMaximumDecodedPixelBytes.load(std::memory_order_relaxed);
    if (processor.unpack() != LIBRAW_SUCCESS || Cancelled(options) ||
        processor.dcraw_process() != LIBRAW_SUCCESS || Cancelled(options)) return false;

    int error = LIBRAW_SUCCESS;
    std::unique_ptr<libraw_processed_image_t, LibRawImageDeleter> image(
        processor.dcraw_make_mem_image(&error));
    if (!image || error != LIBRAW_SUCCESS || image->type != LIBRAW_IMAGE_BITMAP ||
        image->bits != 8 || (image->colors != 3 && image->colors != 4) ||
        image->width == 0 || image->height == 0 ||
        !std::in_range<int>(image->width) || !std::in_range<int>(image->height) ||
        Cancelled(options)) return false;

    const int width = static_cast<int>(image->width);
    const int height = static_cast<int>(image->height);
    const std::uint64_t stride64 = static_cast<std::uint64_t>(width) * image->colors;
    const std::uint64_t requiredBytes = stride64 * static_cast<std::uint64_t>(height);
    if (stride64 > std::numeric_limits<std::size_t>::max() || requiredBytes > image->data_size) return false;
    ConvertRgbToBgraResized(image->data, width, height, static_cast<std::size_t>(stride64),
        image->colors, options.targetSize, output);
    SetRawSourceGeometry(output, rawWidth, rawHeight);
    return !output.pixels.empty() && !Cancelled(options);
}
#endif

#if defined(QS_USE_LIBAVIF)
struct AvifDecoderDeleter { void operator()(avifDecoder* decoder) const noexcept { if (decoder) avifDecoderDestroy(decoder); } };
struct AvifImageDeleter { void operator()(avifImage* image) const noexcept { if (image) avifImageDestroy(image); } };

bool DecodeAvif(const fs::path& path, const DecodeOptions& options, DecodedImageData& output) {
    MappedFileView mapped;
    const std::uint64_t limit = std::min(options.mappedInputLimit,
        gMappedInputLimit.load(std::memory_order_relaxed));
    if (!mapped.Open(path, MappedFileView::AccessPattern::Sequential, limit,
        options.prefetchEncodedInput) || Cancelled(options)) return false;

    std::unique_ptr<avifDecoder, AvifDecoderDeleter> decoder(avifDecoderCreate());
    std::unique_ptr<avifImage, AvifImageDeleter> image(avifImageCreateEmpty());
    if (!decoder || !image) return false;
    decoder->maxThreads = static_cast<int>(gCodecThreadLimit.load(std::memory_order_relaxed));
    const std::uint64_t configuredPixelLimit = quicksift::app::gMaximumDecodedPixelBytes.load(std::memory_order_relaxed) / 4ull;
    decoder->imageSizeLimit = std::min<std::uint32_t>(decoder->imageSizeLimit,
        static_cast<std::uint32_t>(std::min<std::uint64_t>(configuredPixelLimit, 268435456u)));
    decoder->imageDimensionLimit = std::min<std::uint32_t>(decoder->imageDimensionLimit, 32768u);
    const auto bytes = mapped.Bytes();
    if (avifDecoderReadMemory(decoder.get(), image.get(), bytes.data(), bytes.size()) != AVIF_RESULT_OK ||
        Cancelled(options)) return false;

    const int sourceWidth = static_cast<int>(image->width);
    const int sourceHeight = static_cast<int>(image->height);
    int outputWidth = sourceWidth, outputHeight = sourceHeight;
    CalculateTarget(sourceWidth, sourceHeight, options.targetSize, outputWidth, outputHeight);
    if ((outputWidth != sourceWidth || outputHeight != sourceHeight) &&
        avifImageScale(image.get(), static_cast<std::uint32_t>(outputWidth),
            static_cast<std::uint32_t>(outputHeight), &decoder->diag) != AVIF_RESULT_OK) return false;
    if (Cancelled(options)) return false;

    avifRGBImage rgb{};
    avifRGBImageSetDefaults(&rgb, image.get());
    rgb.depth = 8;
    rgb.format = AVIF_RGB_FORMAT_BGRA;
    rgb.alphaPremultiplied = AVIF_TRUE;
    rgb.maxThreads = static_cast<int>(gCodecThreadLimit.load(std::memory_order_relaxed));
    std::size_t outputBytes = 0;
    if (!CheckedBgraBytes(static_cast<int>(rgb.width), static_cast<int>(rgb.height), outputBytes)) return false;
    output.pixels.resize(outputBytes);
    rgb.pixels = output.pixels.data();
    rgb.rowBytes = rgb.width * 4;
    if (avifImageYUVToRGB(image.get(), &rgb) != AVIF_RESULT_OK || Cancelled(options)) {
        output.pixels.clear();
        return false;
    }
    output.sourceWidth = sourceWidth;
    output.sourceHeight = sourceHeight;
    output.width = static_cast<int>(rgb.width);
    output.height = static_cast<int>(rgb.height);
    return true;
}
#endif

#if defined(QS_USE_WEBP)
bool DecodeWebp(const fs::path& path, const DecodeOptions& options, DecodedImageData& output) {
    MappedFileView mapped;
    const std::uint64_t limit = std::min(options.mappedInputLimit,
        gMappedInputLimit.load(std::memory_order_relaxed));
    if (!mapped.Open(path, MappedFileView::AccessPattern::Sequential, limit,
        options.prefetchEncodedInput) || Cancelled(options)) return false;
    const auto encoded = mapped.Bytes();

    WebPDecoderConfig config{};
    if (!WebPInitDecoderConfig(&config)) return false;
    if (WebPGetFeatures(encoded.data(), encoded.size(), &config.input) != VP8_STATUS_OK ||
        config.input.width <= 0 || config.input.height <= 0) return false;
    const int sourceWidth = config.input.width;
    const int sourceHeight = config.input.height;
    int outputWidth = sourceWidth, outputHeight = sourceHeight;
    CalculateTarget(sourceWidth, sourceHeight, options.targetSize, outputWidth, outputHeight);
    if (outputWidth != sourceWidth || outputHeight != sourceHeight) {
        config.options.use_scaling = 1;
        config.options.scaled_width = outputWidth;
        config.options.scaled_height = outputHeight;
    }
    config.options.use_threads = gCodecThreadLimit.load(std::memory_order_relaxed) > 1 ? 1 : 0;
    std::size_t outputBytes = 0;
    if (!CheckedBgraBytes(outputWidth, outputHeight, outputBytes) ||
        outputWidth > std::numeric_limits<int>::max() / 4) return false;
    output.pixels.resize(outputBytes);
    config.output.colorspace = MODE_bgrA;
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = output.pixels.data();
    config.output.u.RGBA.stride = outputWidth * 4;
    config.output.u.RGBA.size = outputBytes;
    const VP8StatusCode status = WebPDecode(encoded.data(), encoded.size(), &config);
    if (status != VP8_STATUS_OK || Cancelled(options) ||
        config.output.u.RGBA.rgba != output.pixels.data()) {
        WebPFreeDecBuffer(&config.output);
        output.pixels.clear();
        return false;
    }
    output.sourceWidth = sourceWidth;
    output.sourceHeight = sourceHeight;
    output.width = outputWidth;
    output.height = outputHeight;
    WebPFreeDecBuffer(&config.output);
    return true;
}
#endif

} // namespace

void ConfigureBundledCodecRuntime(unsigned maxThreads, std::uint64_t maxDecodedPixelBytes,
    std::uint64_t mappedInputLimit, bool enableTurboJpeg) {
    gCodecThreadLimit.store(std::clamp(maxThreads, 1u, 8u), std::memory_order_relaxed);
    quicksift::app::gMaximumDecodedPixelBytes.store(std::clamp<std::uint64_t>(maxDecodedPixelBytes,
        64ull * 1024ull * 1024ull, 2ull * 1024ull * 1024ull * 1024ull), std::memory_order_relaxed);
    gMappedInputLimit.store(std::clamp<std::uint64_t>(mappedInputLimit,
        32ull * 1024ull * 1024ull, 2ull * 1024ull * 1024ull * 1024ull), std::memory_order_relaxed);
    gTurboJpegEnabled.store(enableTurboJpeg, std::memory_order_relaxed);
}

bool BundledCodecSupports(const fs::path& path) {
    const std::wstring extension = ExtensionLower(path);
#if defined(QS_USE_TURBOJPEG)
    if (quicksift::core::IsJpegExtension(extension)) return true;
#endif
#if defined(QS_USE_LIBRAW)
    if (quicksift::core::IsCameraRawExtension(extension)) return true;
#endif
#if defined(QS_USE_LIBAVIF)
    if (extension == L".avif") return true;
#endif
#if defined(QS_USE_WEBP)
    if (extension == L".webp") return true;
#endif
    return false;
}

bool DecodeWithBundledCodec(const fs::path& path, [[maybe_unused]] const DecodeOptions& options,
    DecodedImageData& output) {
    output = {};
    const std::wstring extension = ExtensionLower(path);
#if defined(QS_USE_TURBOJPEG)
    if (quicksift::core::IsJpegExtension(extension) && DecodeJpeg(path, options, output)) return true;
#endif
#if defined(QS_USE_LIBRAW)
    if (quicksift::core::IsCameraRawExtension(extension)) return DecodeRaw(path, options, output);
#endif
#if defined(QS_USE_LIBAVIF)
    if (extension == L".avif") return DecodeAvif(path, options, output);
#endif
#if defined(QS_USE_WEBP)
    if (extension == L".webp") return DecodeWebp(path, options, output);
#endif
    return false;
}

} // namespace quicksift
