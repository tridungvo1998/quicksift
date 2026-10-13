// OWNER: Deterministic, portable FaceLock multi-face policy checks (0.9.4.0).
//
// Not a CTest suite (CTest stays retired): a single self-contained executable with
// no timing, filesystem, UI, or platform dependencies. Build and run from the
// source root on any host with a C++20 compiler, e.g.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Isrc scripts/checks/face_policy_checks.cpp
//       src/core/face_policy.cpp src/core/face_quality_policy.cpp
//       src/review/view_transform_policy.cpp -o /tmp/face_policy_checks
//   /tmp/face_policy_checks
//
//   cl /std:c++20 /EHsc /W4 /utf-8 /Isrc scripts\checks\face_policy_checks.cpp
//       src\core\face_policy.cpp src\core\face_quality_policy.cpp
//       src\review\view_transform_policy.cpp
//
// Covers: centre-closest default ordering and tie breaks, small-face eligibility,
// noise filter, parallel quality permutation, outline hit testing, the 70% click
// framing math (incl. rotation and zoom limits), SourceToScreen, persisted-selection
// matching, and synthetic per-face quality sanity.

#include "core/face_policy.h"
#include "core/face_quality_policy.h"
#include "review/view_transform_policy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace {

int gFailures = 0;
int gChecks = 0;

void Check(bool condition, const char* what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL: %s\n", what);
    }
}

bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

using quicksift::NormalizedFaceRect;
using quicksift::FaceQualityRecord;
namespace core = quicksift::core;
namespace transform = quicksift::review::transform;

NormalizedFaceRect Face(float cx, float cy, float w, float h) {
    return { cx - w * 0.5f, cy - h * 0.5f, w, h };
}

void CheckOrdering() {
    // Larger face off-centre vs smaller (but eligible) face at the centre.
    std::vector<NormalizedFaceRect> faces{ Face(0.2f, 0.5f, 0.30f, 0.30f), Face(0.5f, 0.5f, 0.12f, 0.12f) };
    core::RankFacesForAnchor(faces, 1.0f);
    Check(Near(faces[0].x + faces[0].width * 0.5f, 0.5f, 1e-5f), "centre-closest face is the default target");

    // Equal centre distance: larger area wins.
    faces = { Face(0.3f, 0.5f, 0.10f, 0.10f), Face(0.7f, 0.5f, 0.14f, 0.14f) };
    core::RankFacesForAnchor(faces, 1.0f);
    Check(Near(faces[0].width, 0.14f, 1e-5f), "equal distance breaks ties by larger area");

    // Equal distance and area: top first, then left.
    faces = { Face(0.5f, 0.7f, 0.1f, 0.1f), Face(0.5f, 0.3f, 0.1f, 0.1f) };
    core::RankFacesForAnchor(faces, 1.0f);
    Check(faces[0].y < faces[1].y, "equal distance/area breaks ties top-first");
    faces = { Face(0.7f, 0.5f, 0.1f, 0.1f), Face(0.3f, 0.5f, 0.1f, 0.1f) };
    core::RankFacesForAnchor(faces, 1.0f);
    Check(faces[0].x < faces[1].x, "equal distance/area/top breaks ties left-first");

    // Determinism: permutations of the same input give the same order.
    std::vector<NormalizedFaceRect> a{ Face(0.3f, 0.4f, 0.1f, 0.1f), Face(0.6f, 0.6f, 0.1f, 0.1f), Face(0.5f, 0.2f, 0.2f, 0.2f) };
    std::vector<NormalizedFaceRect> b{ a[2], a[0], a[1] };
    core::RankFacesForAnchor(a, 1.5f);
    core::RankFacesForAnchor(b, 1.5f);
    bool same = true;
    for (std::size_t i = 0; i < a.size(); ++i) same = same && a[i].x == b[i].x && a[i].y == b[i].y;
    Check(same, "ordering is deterministic across input permutations");

    // Aspect: on a wide image horizontal offsets count in pixel-proportional units.
    faces = { Face(0.5f + 0.15f, 0.5f, 0.1f, 0.1f), Face(0.5f, 0.5f + 0.2f, 0.1f, 0.1f) };
    core::RankFacesForAnchor(faces, 2.0f); // dx 0.15*2 = 0.30 > dy 0.20
    Check(faces[0].y > 0.55f, "aspect-scaled distance prefers the vertically offset face on a 2:1 image");

    // Small-face eligibility: tiny face at dead centre must not steal the target.
    faces = { Face(0.5f, 0.5f, 0.03f, 0.03f), Face(0.3f, 0.5f, 0.25f, 0.25f) };
    core::RankFacesForAnchor(faces, 1.0f);
    Check(Near(faces[0].width, 0.25f, 1e-5f), "tiny centre face is ordered after the real subject");
    Check(faces.size() == 2, "ineligible faces are kept (drawn, clickable, analysed)");

    // Parallel qualities are permuted identically; non-parallel are cleared.
    faces = { Face(0.2f, 0.5f, 0.2f, 0.2f), Face(0.5f, 0.5f, 0.2f, 0.2f) };
    std::vector<FaceQualityRecord> qualities(2);
    qualities[0].sharpness = 1.0f;
    qualities[1].sharpness = 2.0f;
    core::RankFacesForAnchor(faces, 1.0f, &qualities);
    Check(qualities.size() == 2 && qualities[0].sharpness == 2.0f, "qualities follow their faces");
    std::vector<FaceQualityRecord> wrong(1);
    core::RankFacesForAnchor(faces, 1.0f, &wrong);
    Check(wrong.empty(), "non-parallel qualities are cleared");
}

void CheckNoiseFilter() {
    // 512 x 384 detection bitmap: short edge 384 -> fraction limit 5.76 px, pixel limit 8.
    Check(core::IsNoiseFace({ 0.5f, 0.5f, 5.0f / 512.0f, 5.0f / 384.0f }, 512, 384), "5 px box is noise");
    Check(!core::IsNoiseFace({ 0.5f, 0.5f, 12.0f / 512.0f, 12.0f / 384.0f }, 512, 384), "12 px box is kept");
    // Above the pixel limit but tiny relative to a huge image -> kept (BOTH limits must fail).
    Check(!core::IsNoiseFace({ 0.5f, 0.5f, 10.0f / 4000.0f, 10.0f / 3000.0f }, 4000, 3000),
        "10 px box on a large image is kept (conservative)");
}

void CheckHitTest() {
    std::vector<core::FaceHitBox> boxes{
        core::InflateFaceHitBox(100.0f, 100.0f, 300.0f, 300.0f, 0),
        core::InflateFaceHitBox(150.0f, 150.0f, 200.0f, 200.0f, 1),
        core::InflateFaceHitBox(500.0f, 100.0f, 540.0f, 140.0f, 2) };
    Check(core::HitTestFaceBoxes(boxes, 175.0f, 175.0f) == std::optional<std::size_t>(1), "nested boxes resolve to the smallest");
    Check(core::HitTestFaceBoxes(boxes, 120.0f, 280.0f) == std::optional<std::size_t>(0), "outer box hit");
    Check(core::HitTestFaceBoxes(boxes, 95.0f, 200.0f) == std::optional<std::size_t>(0), "inflated margin is clickable");
    Check(core::HitTestFaceBoxes(boxes, 545.0f, 120.0f) == std::optional<std::size_t>(2), "minimum inflation for small boxes");
    Check(!core::HitTestFaceBoxes(boxes, 400.0f, 400.0f).has_value(), "miss returns nothing");
}

void CheckFraming() {
    const transform::ViewGeometry geometry{ { 0.0f, 0.0f, 1000.0f, 800.0f }, 4000, 3000, 1.25f };
    transform::ViewState fit{};
    // 400 x 300 px face at (0.4..0.5, 0.4..0.5).
    const transform::ViewRect faceRect{ 0.4f, 0.4f, 0.5f, 0.5f };
    const auto framed = transform::FrameSourceRect(fit, geometry, faceRect, core::kFaceClickFillFraction);
    const auto resolved = transform::ResolveView(framed, geometry);
    Check(framed.mode == quicksift::core::ZoomMode::Custom, "framing a small face zooms past Fit");
    Check(Near(resolved.scaleDip, 1.75f, 1e-3f), "70% framing scale (limiting axis)");
    Check(Near(400.0f * resolved.scaleDip / 1000.0f, 0.70f, 1e-3f), "face fills 70% of the pane width");
    Check(Near(resolved.state.centerX, 0.45f, 1e-4f) && Near(resolved.state.centerY, 0.45f, 1e-4f), "framing is centred on the face");
    const auto centre = transform::SourceToScreen(resolved, geometry, { 0.45f, 0.45f });
    Check(Near(centre.x, 500.0f, 0.05f) && Near(centre.y, 400.0f, 0.05f), "framed face centre lands at the pane centre");

    // Rotation 90: oriented face is 300 x 400 -> limiting axis is height (800 / 400).
    transform::ViewState rotated{};
    rotated.rotation = 90;
    const auto framedRotated = transform::ResolveView(
        transform::FrameSourceRect(rotated, geometry, faceRect, 0.70f), geometry);
    Check(Near(framedRotated.scaleDip, 0.70f * std::min(1000.0f / 300.0f, 800.0f / 400.0f), 1e-3f),
        "rotated framing uses the oriented face size");

    // Max zoom limit (32x Fit) and Fit floor.
    const auto tiny = transform::ResolveView(transform::FrameSourceRect(fit, geometry,
        { 0.5f, 0.5f, 0.5005f, 0.5005f }, 0.70f), geometry);
    Check(Near(tiny.relativeZoom, 32.0f, 1e-2f), "framing is limited by the maximum zoom");
    const auto huge = transform::FrameSourceRect(fit, geometry, { 0.0f, 0.0f, 1.0f, 1.0f }, 0.70f);
    Check(huge.mode == quicksift::core::ZoomMode::Fit, "a face larger than 70% of the pane at Fit stays at Fit");

    // Edge face: centre is clamped into the image, face stays fully visible.
    const auto edge = transform::ResolveView(transform::FrameSourceRect(fit, geometry,
        { 0.0f, 0.0f, 0.05f, 0.05f }, 0.70f), geometry);
    const auto corner = transform::SourceToScreen(edge, geometry, { 0.0f, 0.0f });
    Check(corner.x >= -0.05f && corner.y >= -0.05f, "edge face framing never pans past the image");

    // SourceToScreen under rotation matches the renderer's clockwise quarter turn.
    transform::ViewState r90{};
    r90.rotation = 90;
    const auto view90 = transform::ResolveView(r90, geometry);
    const auto right = transform::SourceToScreen(view90, geometry, { 0.6f, 0.5f });
    Check(Near(right.x, 500.0f, 0.05f) && right.y > 400.0f, "+x in source maps to +y on screen at 90 degrees");
}

void CheckSelectionMatching() {
    std::vector<NormalizedFaceRect> faces{ Face(0.3f, 0.4f, 0.1f, 0.12f), Face(0.7f, 0.4f, 0.1f, 0.12f) };
    const auto exact = core::MatchFaceSelection(faces[1], faces);
    Check(exact == std::optional<std::size_t>(1), "exact selection matches by IoU");
    const auto shifted = core::MatchFaceSelection(Face(0.72f, 0.41f, 0.11f, 0.12f), faces);
    Check(shifted == std::optional<std::size_t>(1), "re-detected (shifted) box still matches");
    const auto lowIoU = core::MatchFaceSelection(Face(0.75f, 0.40f, 0.1f, 0.12f), faces);
    Check(lowIoU == std::optional<std::size_t>(1), "nearest-centre fallback within limits");
    Check(!core::MatchFaceSelection(Face(0.5f, 0.9f, 0.1f, 0.12f), faces).has_value(), "unrelated box does not match");
    Check(!core::MatchFaceSelection(Face(0.7f, 0.4f, 0.4f, 0.4f), faces).has_value() ||
          core::FaceIntersectionOverUnion(Face(0.7f, 0.4f, 0.4f, 0.4f), faces[1]) >= core::kFaceSelectionMinIoU,
        "implausible size ratio does not match by centre alone");
    Check(core::ResolveTargetFaceIndex(faces, std::nullopt) == std::optional<std::size_t>(0), "no selection -> default face");
    Check(core::ResolveTargetFaceIndex(faces, Face(0.5f, 0.9f, 0.1f, 0.1f)) == std::optional<std::size_t>(0),
        "stale selection falls back to the default face");
    Check(core::ResolveTargetFaceIndex(faces, faces[1]) == std::optional<std::size_t>(1), "matched selection wins");
    Check(core::SpatialFaceNumber(faces, 1) == 2 && core::SpatialFaceNumber(faces, 0) == 1, "spatial numbering left to right");
}

std::vector<std::uint8_t> TexturedImage(int width, int height, unsigned seed) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u);
    std::mt19937 random(seed);
    std::uniform_int_distribution<int> noise(-60, 60);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Mixed-frequency isotropic texture (blobs + fine detail), skin-like tint.
            const double base = 128.0 + 40.0 * std::sin(x * 0.21) * std::cos(y * 0.17) +
                25.0 * std::sin((x + y) * 0.9) + noise(random) * 0.6;
            const int value = std::clamp(static_cast<int>(base), 0, 255);
            std::uint8_t* pixel = &pixels[(static_cast<std::size_t>(y) * width + x) * 4u];
            pixel[0] = static_cast<std::uint8_t>(std::clamp(value - 30, 0, 255));
            pixel[1] = static_cast<std::uint8_t>(std::clamp(value - 10, 0, 255));
            pixel[2] = static_cast<std::uint8_t>(value);
            pixel[3] = 255;
        }
    }
    return pixels;
}

std::vector<std::uint8_t> BoxBlur(const std::vector<std::uint8_t>& source, int width, int height,
    int radiusX, int radiusY) {
    std::vector<std::uint8_t> output(source.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int channel = 0; channel < 4; ++channel) {
                int sum = 0;
                int count = 0;
                for (int dy = -radiusY; dy <= radiusY; ++dy) {
                    for (int dx = -radiusX; dx <= radiusX; ++dx) {
                        const int sx = std::clamp(x + dx, 0, width - 1);
                        const int sy = std::clamp(y + dy, 0, height - 1);
                        sum += source[(static_cast<std::size_t>(sy) * width + sx) * 4u + channel];
                        ++count;
                    }
                }
                output[(static_cast<std::size_t>(y) * width + x) * 4u + channel] =
                    static_cast<std::uint8_t>(sum / count);
            }
        }
    }
    return output;
}

void CheckQuality() {
    using namespace quicksift::FaceQualityFlag;
    const int width = 400;
    const int height = 400;
    const NormalizedFaceRect face{ 0.2f, 0.2f, 0.6f, 0.6f };
    const auto sharp = TexturedImage(width, height, 7);
    const auto sharpQuality = core::AnalyzeFaceQualityBgra(sharp.data(), width, height, face);
    Check((sharpQuality.flags & Analyzed) != 0, "quality analysis runs on a valid crop");
    Check((sharpQuality.flags & (Blurry | MotionBlur)) == 0, "sharp texture is not flagged as blurred");

    const auto blurred = BoxBlur(sharp, width, height, 5, 5);
    const auto blurredQuality = core::AnalyzeFaceQualityBgra(blurred.data(), width, height, face);
    Check((blurredQuality.flags & Blurry) != 0, "heavy defocus is flagged");
    Check(blurredQuality.sharpness < sharpQuality.sharpness, "blur lowers the sharpness score");

    const auto motion = BoxBlur(sharp, width, height, 9, 0);
    const auto motionQuality = core::AnalyzeFaceQualityBgra(motion.data(), width, height, face);
    Check(motionQuality.motionAnisotropy > sharpQuality.motionAnisotropy, "directional blur raises anisotropy");
    Check(motionQuality.motionAnisotropy >= core::kFaceQualityThresholds.motionAnisotropyThreshold,
        "horizontal motion blur crosses the anisotropy threshold");
    // Motion blur combined with the usual softness of a shaken frame is flagged.
    const auto shaken = BoxBlur(BoxBlur(sharp, width, height, 9, 0), width, height, 1, 1);
    const auto shakenQuality = core::AnalyzeFaceQualityBgra(shaken.data(), width, height, face);
    Check(core::FaceQualityFlagged(shakenQuality), "shaken (motion + soft) face is flagged");

    const auto tinyQuality = core::AnalyzeFaceQualityBgra(sharp.data(), width, height,
        { 0.5f, 0.5f, 0.05f, 0.05f });
    Check((tinyQuality.flags & TooSmall) != 0 && !core::FaceQualityFlagged(tinyQuality),
        "faces below the minimum crop are never flagged");
    Check(core::RequiredFaceQualityDecodeEdge(width, height, { 0.5f, 0.5f, 0.05f, 0.05f }) > width,
        "too-small faces request a larger decode");
    Check(core::RequiredFaceQualityDecodeEdge(4000, 3000, { 0.5f, 0.5f, 0.01f, 0.01f }) == 0,
        "decodes at the cap never request more");

    std::vector<std::uint8_t> flat(static_cast<std::size_t>(width) * height * 4u, 120);
    const auto flatQuality = core::AnalyzeFaceQualityBgra(flat.data(), width, height, face);
    Check((flatQuality.flags & LowContrast) != 0 && !core::FaceQualityFlagged(flatQuality),
        "flat/low-contrast crops are not judged");
    Check(!core::FaceQualityFlagged(core::AnalyzeFaceQualityBgra(nullptr, width, height, face)),
        "null input is never flagged");
}

} // namespace

int main() {
    CheckOrdering();
    CheckNoiseFilter();
    CheckHitTest();
    CheckFraming();
    CheckSelectionMatching();
    CheckQuality();
    std::printf("%d/%d face policy checks passed\n", gChecks - gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
