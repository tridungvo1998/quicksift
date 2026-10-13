// CODE GUIDE: See CODE_GUIDE.md -> "Rules for sizes and arithmetic".
// OWNER: Face-detection adapter; validate pixel geometry and isolate optional platform capability.

#include "face_detector.h"
#include "core/face_policy.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>

#if __has_include(<winrt/Windows.Media.FaceAnalysis.h>)
#define QS_HAS_WINDOWS_FACE_ANALYSIS 1
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.FaceAnalysis.h>
#include <winrt/Windows.Storage.Streams.h>
#endif

#if defined(QS_REQUIRE_FACE_DETECTOR) && !defined(QS_HAS_WINDOWS_FACE_ANALYSIS)
#error QuickSift face lock requires the C++/WinRT Windows.Media.FaceAnalysis headers.
#endif

namespace quicksift {

namespace {
#if defined(QS_HAS_WINDOWS_FACE_ANALYSIS)
constexpr std::uint32_t kMaximumFacesPerImage = 512;
thread_local winrt::Windows::Media::FaceAnalysis::FaceDetector gThreadFaceDetector{ nullptr };
// Set once if the Gray8 input path ever throws on this machine; every later call goes
// straight to the original Bgra8 path instead of paying for a failing attempt.
std::atomic<bool> gGray8InputRejected{ false };

// BT.601-style luma on tightly packed BGRA (same weights as the quality analyzer).
std::vector<std::uint8_t> BgraToGray8(const std::uint8_t* pixels, std::size_t pixelCount) {
    std::vector<std::uint8_t> gray(pixelCount);
    for (std::size_t index = 0; index < pixelCount; ++index) {
        const std::uint8_t* pixel = pixels + index * 4u;
        const unsigned luma = (29u * pixel[0] + 150u * pixel[1] + 77u * pixel[2]) >> 8;
        gray[index] = static_cast<std::uint8_t>(std::min(luma, 255u));
    }
    return gray;
}
#endif
std::mutex gCapabilityMutex;
core::FaceDetectorCapability gCapability = core::FaceDetectorCapability::Unknown;
}

core::FaceDetectorCapability QueryFaceDetectorCapability(bool refresh) noexcept {
    std::lock_guard lock(gCapabilityMutex);
    if (refresh) gCapability = core::FaceDetectorCapability::Unknown;
    if (gCapability != core::FaceDetectorCapability::Unknown) return gCapability;
#if defined(QS_HAS_WINDOWS_FACE_ANALYSIS)
    try {
        gCapability = winrt::Windows::Media::FaceAnalysis::FaceDetector::IsSupported() ?
            core::FaceDetectorCapability::Available : core::FaceDetectorCapability::Unavailable;
    } catch (...) {
        gCapability = core::FaceDetectorCapability::TemporarilyFailed;
    }
#else
    gCapability = core::FaceDetectorCapability::Unavailable;
#endif
    return gCapability;
}

bool FaceDetectionAvailable() {
    return QueryFaceDetectorCapability() == core::FaceDetectorCapability::Available;
}

void ReleaseThreadFaceDetector() noexcept {
#if defined(QS_HAS_WINDOWS_FACE_ANALYSIS)
    try {
        gThreadFaceDetector = nullptr;
    } catch (...) {
        // Releasing an expendable cached detector must never prevent thread teardown.
    }
#endif
}

core::FaceAnalysisOutcome DetectFacesFastBgra(
    const std::uint8_t* pixels,
    int width,
    int height,
    std::vector<NormalizedFaceRect>& output) {
    output.clear();

#if defined(QS_HAS_WINDOWS_FACE_ANALYSIS)
    if (!pixels || width < 1 || height < 1) return core::FaceAnalysisOutcome::TransientFailure;
    const core::FaceDetectorCapability capability = QueryFaceDetectorCapability();
    if (capability == core::FaceDetectorCapability::Unavailable)
        return core::FaceAnalysisOutcome::Unavailable;
    if (capability != core::FaceDetectorCapability::Available)
        return core::FaceAnalysisOutcome::TransientFailure;

    const std::uint64_t byteCount64 = static_cast<std::uint64_t>(width) *
        static_cast<std::uint64_t>(height) * 4ull;
    if (byteCount64 == 0 ||
        byteCount64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return core::FaceAnalysisOutcome::TransientFailure;
    }

    try {
        using namespace winrt::Windows::Graphics::Imaging;
        using namespace winrt::Windows::Media::FaceAnalysis;
        using namespace winrt::Windows::Storage::Streams;

        const std::size_t byteCount = static_cast<std::size_t>(byteCount64);

        // One detector per worker thread avoids repeated WinRT activation while
        // keeping detector objects off the UI thread.
        if (!gThreadFaceDetector) gThreadFaceDetector = FaceDetector::CreateAsync().get();

        // Gray8 is the detector's native input format; handing it Gray8 skips the
        // detector's internal colour conversion and quarters the copied buffer. Any
        // failure in this optional path falls back to the original Bgra8 path below.
        winrt::Windows::Foundation::Collections::IVector<DetectedFace> faces{ nullptr };
        if (!gGray8InputRejected.load(std::memory_order_relaxed)) {
            try {
                if (FaceDetector::IsBitmapPixelFormatSupported(BitmapPixelFormat::Gray8)) {
                    const std::vector<std::uint8_t> gray = BgraToGray8(pixels, byteCount / 4u);
                    DataWriter grayWriter;
                    grayWriter.WriteBytes(winrt::array_view<const std::uint8_t>(
                        gray.data(), gray.data() + gray.size()));
                    const SoftwareBitmap grayBitmap = SoftwareBitmap::CreateCopyFromBuffer(
                        grayWriter.DetachBuffer(), BitmapPixelFormat::Gray8, width, height);
                    faces = gThreadFaceDetector.DetectFacesAsync(grayBitmap).get();
                }
            } catch (...) {
                faces = nullptr;
                gGray8InputRejected.store(true, std::memory_order_relaxed);
            }
        }
        if (!faces) {
            DataWriter writer;
            writer.WriteBytes(winrt::array_view<const std::uint8_t>(pixels, pixels + byteCount));
            const IBuffer buffer = writer.DetachBuffer();
            const SoftwareBitmap bitmap = SoftwareBitmap::CreateCopyFromBuffer(
                buffer,
                BitmapPixelFormat::Bgra8,
                width,
                height,
                BitmapAlphaMode::Premultiplied);
            faces = gThreadFaceDetector.DetectFacesAsync(bitmap).get();
        }
        output.reserve(std::min<std::uint32_t>(faces.Size(), kMaximumFacesPerImage));
        for (const auto& detected : faces) {
            if (output.size() >= kMaximumFacesPerImage) break;
            const auto box = detected.FaceBox();
            const std::uint64_t imageWidth = static_cast<std::uint64_t>(width);
            const std::uint64_t imageHeight = static_cast<std::uint64_t>(height);
            const std::uint64_t left = std::min<std::uint64_t>(box.X, imageWidth);
            const std::uint64_t top = std::min<std::uint64_t>(box.Y, imageHeight);
            const std::uint64_t right = std::min<std::uint64_t>(
                static_cast<std::uint64_t>(box.X) + box.Width, imageWidth);
            const std::uint64_t bottom = std::min<std::uint64_t>(
                static_cast<std::uint64_t>(box.Y) + box.Height, imageHeight);
            if (right <= left || bottom <= top) continue;
            NormalizedFaceRect face{
                static_cast<float>(static_cast<double>(left) / width),
                static_cast<float>(static_cast<double>(top) / height),
                static_cast<float>(static_cast<double>(right - left) / width),
                static_cast<float>(static_cast<double>(bottom - top) / height)
            };
            // Conservative noise filter: only boxes that are tiny both in pixels and
            // relative to the image are dropped (see core/face_policy.h).
            if (core::IsNoiseFace(face, width, height)) continue;
            output.push_back(face);
        }

        // Default FaceLock target = face closest to the dead centre of the image
        // (ties: larger, then top, then left). Deterministic and cheap; faces[0] is
        // the default anchor everywhere (FaceAnchorComesBefore ordering).
        core::RankFacesForAnchor(output,
            static_cast<float>(static_cast<double>(width) / static_cast<double>(height)));
        return core::FaceAnalysisOutcome::Completed;
    } catch (...) {
        output.clear();
        try {
            gThreadFaceDetector = nullptr;
        } catch (...) {
        }
        return core::FaceAnalysisOutcome::TransientFailure;
    }
#else
    (void)pixels;
    (void)width;
    (void)height;
    return core::FaceAnalysisOutcome::Unavailable;
#endif
}

} // namespace quicksift
