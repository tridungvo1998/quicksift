// OWNER: Portable deterministic face-anchor ordering policy.
#include "core/face_policy.h"

#include <cmath>
#include <cstdint>

namespace quicksift::core {

bool FaceAnchorComesBefore(const quicksift::NormalizedFaceRect& left,
    const quicksift::NormalizedFaceRect& right) noexcept {
    const auto quantize = [](double value) noexcept {
        return static_cast<std::int64_t>(std::llround(value * 1'000'000.0));
    };
    const auto area = [&](const quicksift::NormalizedFaceRect& face) noexcept {
        return quantize(static_cast<double>(face.width) * face.height);
    };
    const auto distance = [&](const quicksift::NormalizedFaceRect& face) noexcept {
        const double centerX = face.x + face.width * 0.5;
        const double centerY = face.y + face.height * 0.5;
        return quantize((centerX - 0.5) * (centerX - 0.5) +
            (centerY - 0.5) * (centerY - 0.5));
    };
    const std::int64_t leftArea = area(left);
    const std::int64_t rightArea = area(right);
    if (leftArea != rightArea) return leftArea > rightArea;
    const std::int64_t leftDistance = distance(left);
    const std::int64_t rightDistance = distance(right);
    if (leftDistance != rightDistance) return leftDistance < rightDistance;
    const std::int64_t leftTop = quantize(left.y);
    const std::int64_t rightTop = quantize(right.y);
    if (leftTop != rightTop) return leftTop < rightTop;
    return quantize(left.x) < quantize(right.x);
}

} // namespace quicksift::core
