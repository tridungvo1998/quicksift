// CODE GUIDE: See CODE_GUIDE.md -> "Rules for sizes and arithmetic".
// OWNER: Face-detection adapter; validate pixel geometry and isolate optional platform capability.

#include "face_detector.h"
#include "core/face_policy.h"

#include <algorithm>
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
        DataWriter writer;
        writer.WriteBytes(winrt::array_view<const std::uint8_t>(pixels, pixels + byteCount));
        const IBuffer buffer = writer.DetachBuffer();
        const SoftwareBitmap bitmap = SoftwareBitmap::CreateCopyFromBuffer(
            buffer,
            BitmapPixelFormat::Bgra8,
            width,
            height,
            BitmapAlphaMode::Premultiplied);

        // One detector per worker thread avoids repeated WinRT activation while
        // keeping detector objects off the UI thread.
        if (!gThreadFaceDetector) gThreadFaceDetector = FaceDetector::CreateAsync().get();

        const auto faces = gThreadFaceDetector.DetectFacesAsync(bitmap).get();
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
            output.push_back(face);
        }

        // The largest face is the default anchor. This is deterministic and avoids
        // expensive subject classification during rapid navigation.
        std::sort(output.begin(), output.end(), quicksift::core::FaceAnchorComesBefore);
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
