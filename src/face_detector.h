// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Face-detection adapter contract; UI scheduling belongs elsewhere.

#pragma once

#include "core/face_detection_policy.h"
#include "persistent_cache.h"

#include <cstdint>
#include <vector>

namespace quicksift {

core::FaceDetectorCapability QueryFaceDetectorCapability(bool refresh = false) noexcept;
bool FaceDetectionAvailable();

// Releases the detector cached on the calling worker thread. Call this before
// RoUninitialize/CoUninitialize so its WinRT interface is not released after the
// apartment has already been torn down.
void ReleaseThreadFaceDetector() noexcept;

core::FaceAnalysisOutcome DetectFacesFastBgra(
    const std::uint8_t* pixels,
    int width,
    int height,
    std::vector<NormalizedFaceRect>& output);

} // namespace quicksift
