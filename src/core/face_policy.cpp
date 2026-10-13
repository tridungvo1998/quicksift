// OWNER: Portable deterministic face-anchor ordering, FaceLock target selection,
// face hit-testing, and persisted-selection matching policy.
#include "core/face_policy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>

namespace quicksift::core {
namespace {

std::int64_t Quantize(double value) noexcept {
    return static_cast<std::int64_t>(std::llround(value * 1'000'000.0));
}

bool Finite(const quicksift::NormalizedFaceRect& face) noexcept {
    return std::isfinite(face.x) && std::isfinite(face.y) &&
        std::isfinite(face.width) && std::isfinite(face.height);
}

double Area(const quicksift::NormalizedFaceRect& face) noexcept {
    return static_cast<double>(face.width) * static_cast<double>(face.height);
}

float SanitizedAspect(float aspect) noexcept {
    return std::isfinite(aspect) && aspect > 0.01f && aspect < 100.0f ? aspect : 1.0f;
}

} // namespace

bool FaceAnchorComesBefore(const quicksift::NormalizedFaceRect& left,
    const quicksift::NormalizedFaceRect& right, float aspect) noexcept {
    const double xScale = static_cast<double>(SanitizedAspect(aspect));
    const auto distance = [&](const quicksift::NormalizedFaceRect& face) noexcept {
        const double dx = (face.x + face.width * 0.5 - 0.5) * xScale;
        const double dy = face.y + face.height * 0.5 - 0.5;
        return Quantize(dx * dx + dy * dy);
    };
    const std::int64_t leftDistance = distance(left);
    const std::int64_t rightDistance = distance(right);
    if (leftDistance != rightDistance) return leftDistance < rightDistance;
    const std::int64_t leftArea = Quantize(Area(left));
    const std::int64_t rightArea = Quantize(Area(right));
    if (leftArea != rightArea) return leftArea > rightArea;
    const std::int64_t leftTop = Quantize(left.y);
    const std::int64_t rightTop = Quantize(right.y);
    if (leftTop != rightTop) return leftTop < rightTop;
    return Quantize(left.x) < Quantize(right.x);
}

bool FaceAnchorComesBefore(const quicksift::NormalizedFaceRect& left,
    const quicksift::NormalizedFaceRect& right) noexcept {
    return FaceAnchorComesBefore(left, right, 1.0f);
}

bool IsNoiseFace(const quicksift::NormalizedFaceRect& face,
    int imageWidth, int imageHeight) noexcept {
    if (!Finite(face) || face.width <= 0.0f || face.height <= 0.0f) return true;
    if (imageWidth <= 0 || imageHeight <= 0) return false;
    const float widthPixels = face.width * static_cast<float>(imageWidth);
    const float heightPixels = face.height * static_cast<float>(imageHeight);
    const float shortEdgePixels = std::min(widthPixels, heightPixels);
    const float imageShortEdge = static_cast<float>(std::min(imageWidth, imageHeight));
    return shortEdgePixels < kFaceNoiseMinShortEdgePixels &&
        shortEdgePixels < imageShortEdge * kFaceNoiseMinShortEdgeFraction;
}

void RankFacesForAnchor(std::vector<quicksift::NormalizedFaceRect>& faces, float aspect,
    std::vector<quicksift::FaceQualityRecord>* qualities) {
    const bool parallel = qualities && qualities->size() == faces.size();
    if (qualities && !parallel) qualities->clear();
    if (faces.size() < 2) return;

    double largest = 0.0;
    for (const auto& face : faces) largest = std::max(largest, Area(face));
    // Linear ratio >= r  <=>  area ratio >= r^2.
    const double minimumArea = largest *
        static_cast<double>(kFaceDefaultMinLinearRatio) * static_cast<double>(kFaceDefaultMinLinearRatio);
    const auto eligible = [&](const quicksift::NormalizedFaceRect& face) noexcept {
        return Quantize(Area(face)) >= Quantize(minimumArea);
    };

    std::vector<std::size_t> order(faces.size());
    std::iota(order.begin(), order.end(), std::size_t{ 0 });
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const bool eligibleA = eligible(faces[a]);
        const bool eligibleB = eligible(faces[b]);
        if (eligibleA != eligibleB) return eligibleA;
        if (FaceAnchorComesBefore(faces[a], faces[b], aspect)) return true;
        if (FaceAnchorComesBefore(faces[b], faces[a], aspect)) return false;
        return a < b;
    });

    std::vector<quicksift::NormalizedFaceRect> sortedFaces;
    sortedFaces.reserve(faces.size());
    for (const std::size_t index : order) sortedFaces.push_back(faces[index]);
    faces = std::move(sortedFaces);
    if (parallel) {
        std::vector<quicksift::FaceQualityRecord> sortedQualities;
        sortedQualities.reserve(order.size());
        for (const std::size_t index : order) sortedQualities.push_back((*qualities)[index]);
        *qualities = std::move(sortedQualities);
    }
}

float FaceIntersectionOverUnion(const quicksift::NormalizedFaceRect& left,
    const quicksift::NormalizedFaceRect& right) noexcept {
    if (!Finite(left) || !Finite(right)) return 0.0f;
    const double x0 = std::max(left.x, right.x);
    const double y0 = std::max(left.y, right.y);
    const double x1 = std::min(left.x + left.width, right.x + right.width);
    const double y1 = std::min(left.y + left.height, right.y + right.height);
    const double intersection = std::max(0.0, x1 - x0) * std::max(0.0, y1 - y0);
    const double unionArea = Area(left) + Area(right) - intersection;
    if (!(unionArea > 0.0)) return 0.0f;
    return static_cast<float>(intersection / unionArea);
}

std::optional<std::size_t> MatchFaceSelection(const quicksift::NormalizedFaceRect& stored,
    std::span<const quicksift::NormalizedFaceRect> faces) noexcept {
    if (faces.empty() || !Finite(stored) || stored.width <= 0.0f || stored.height <= 0.0f)
        return std::nullopt;
    std::optional<std::size_t> best;
    float bestIoU = 0.0f;
    for (std::size_t index = 0; index < faces.size(); ++index) {
        const float iou = FaceIntersectionOverUnion(stored, faces[index]);
        if (iou > bestIoU) {
            bestIoU = iou;
            best = index;
        }
    }
    if (best && bestIoU >= kFaceSelectionMinIoU) return best;

    const double storedSize = std::sqrt(Area(stored));
    const double storedCx = stored.x + stored.width * 0.5;
    const double storedCy = stored.y + stored.height * 0.5;
    std::optional<std::size_t> nearest;
    double nearestDistance = static_cast<double>(kFaceSelectionMaxCenterDistance);
    for (std::size_t index = 0; index < faces.size(); ++index) {
        const auto& face = faces[index];
        const double size = std::sqrt(Area(face));
        if (!(size > 0.0)) continue;
        const double ratio = size > storedSize ? size / storedSize : storedSize / size;
        if (ratio > static_cast<double>(kFaceSelectionMaxSizeRatio)) continue;
        const double dx = face.x + face.width * 0.5 - storedCx;
        const double dy = face.y + face.height * 0.5 - storedCy;
        const double distance = std::sqrt(dx * dx + dy * dy) / storedSize;
        if (distance <= nearestDistance) {
            if (!nearest || distance < nearestDistance) nearest = index;
            nearestDistance = distance;
        }
    }
    return nearest;
}

std::optional<std::size_t> ResolveTargetFaceIndex(
    std::span<const quicksift::NormalizedFaceRect> faces,
    const std::optional<quicksift::NormalizedFaceRect>& selection) noexcept {
    if (faces.empty()) return std::nullopt;
    if (selection) {
        if (const auto matched = MatchFaceSelection(*selection, faces)) return matched;
    }
    return std::size_t{ 0 };
}

FaceHitBox InflateFaceHitBox(float left, float top, float right, float bottom,
    std::size_t faceIndex) noexcept {
    if (right < left) std::swap(left, right);
    if (bottom < top) std::swap(top, bottom);
    const float padX = std::max(kFaceHitMinInflateDip, (right - left) * kFaceHitInflateFraction);
    const float padY = std::max(kFaceHitMinInflateDip, (bottom - top) * kFaceHitInflateFraction);
    return { left - padX, top - padY, right + padX, bottom + padY, faceIndex };
}

std::optional<std::size_t> HitTestFaceBoxes(std::span<const FaceHitBox> boxes,
    float x, float y) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
    const FaceHitBox* best = nullptr;
    double bestArea = 0.0;
    double bestDistance = 0.0;
    for (const FaceHitBox& box : boxes) {
        if (x < box.left || x > box.right || y < box.top || y > box.bottom) continue;
        const double area = static_cast<double>(box.right - box.left) *
            static_cast<double>(box.bottom - box.top);
        const double dx = x - (box.left + box.right) * 0.5;
        const double dy = y - (box.top + box.bottom) * 0.5;
        const double distance = dx * dx + dy * dy;
        const bool better = !best || Quantize(area) < Quantize(bestArea) ||
            (Quantize(area) == Quantize(bestArea) &&
                (Quantize(distance) < Quantize(bestDistance) ||
                    (Quantize(distance) == Quantize(bestDistance) && box.faceIndex < best->faceIndex)));
        if (better) {
            best = &box;
            bestArea = area;
            bestDistance = distance;
        }
    }
    if (!best) return std::nullopt;
    return best->faceIndex;
}

std::size_t SpatialFaceNumber(std::span<const quicksift::NormalizedFaceRect> faces,
    std::size_t index) noexcept {
    if (index >= faces.size()) return 0;
    const auto& target = faces[index];
    const std::int64_t targetX = Quantize(target.x + target.width * 0.5);
    const std::int64_t targetY = Quantize(target.y + target.height * 0.5);
    std::size_t before = 0;
    for (std::size_t other = 0; other < faces.size(); ++other) {
        if (other == index) continue;
        const std::int64_t x = Quantize(faces[other].x + faces[other].width * 0.5);
        const std::int64_t y = Quantize(faces[other].y + faces[other].height * 0.5);
        if (x < targetX || (x == targetX && (y < targetY || (y == targetY && other < index))))
            ++before;
    }
    return before + 1;
}

} // namespace quicksift::core
