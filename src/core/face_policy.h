// OWNER: Portable deterministic face-anchor ordering, FaceLock target selection,
// face hit-testing, and persisted-selection matching policy.
#pragma once

#include "persistent_cache.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace quicksift::core {

// ---- Tunable FaceLock multi-face constants (single source of truth) --------------
// Obvious-noise filter applied at detection time. Deliberately conservative: a box
// is dropped only when its short edge is below BOTH limits, i.e. it is tiny in the
// detection bitmap and tiny relative to the image. Real faces this small cannot be
// inspected meaningfully and are almost always detector noise or texture.
inline constexpr float kFaceNoiseMinShortEdgeFraction = 0.015f; // of image short edge
inline constexpr float kFaceNoiseMinShortEdgePixels = 8.0f;     // detection-bitmap pixels
// Default-target eligibility: a face whose linear size is below this fraction of the
// largest face is still kept (drawn, clickable, quality-checked) but is ordered after
// every larger face, so a tiny background face at dead centre does not steal the
// default FaceLock target from the actual subjects.
inline constexpr float kFaceDefaultMinLinearRatio = 0.20f;
// Persisted-selection matching against a fresh detection.
inline constexpr float kFaceSelectionMinIoU = 0.30f;
inline constexpr float kFaceSelectionMaxCenterDistance = 0.60f; // in stored-face sizes
inline constexpr float kFaceSelectionMaxSizeRatio = 2.0f;
// Face outline hit box inflation (screen DIPs / fraction of the drawn box).
inline constexpr float kFaceHitInflateFraction = 0.12f;
inline constexpr float kFaceHitMinInflateDip = 6.0f;
// Clicking a face frames it so the face box fills this fraction of the pane.
inline constexpr float kFaceClickFillFraction = 0.70f;
// Mouse travel (DIPs) beyond which a press on a face becomes a pan, not a click.
inline constexpr float kFaceClickMaxTravelDip = 4.0f;

// Default ordering: centre closest to the dead centre of the image first, then larger
// area, then top, then left. All keys are quantized so ordering is deterministic.
// The two-argument form measures distance in normalized coordinates (aspect 1);
// the aspect form measures it in pixel-proportional units (aspect = width / height).
bool FaceAnchorComesBefore(const quicksift::NormalizedFaceRect& left,
    const quicksift::NormalizedFaceRect& right) noexcept;
bool FaceAnchorComesBefore(const quicksift::NormalizedFaceRect& left,
    const quicksift::NormalizedFaceRect& right, float aspect) noexcept;

// True when a detected box is obvious noise (see constants above).
bool IsNoiseFace(const quicksift::NormalizedFaceRect& face,
    int imageWidth, int imageHeight) noexcept;

// Sorts faces into FaceLock default order: eligible faces (>= kFaceDefaultMinLinearRatio
// of the largest) first, each group by FaceAnchorComesBefore. faces[0] is therefore
// the default target. When qualities is non-null and parallel (same size) it is
// permuted identically; otherwise it is cleared (the verdicts no longer line up).
void RankFacesForAnchor(std::vector<quicksift::NormalizedFaceRect>& faces, float aspect,
    std::vector<quicksift::FaceQualityRecord>* qualities = nullptr);

float FaceIntersectionOverUnion(const quicksift::NormalizedFaceRect& left,
    const quicksift::NormalizedFaceRect& right) noexcept;

// Matches a persisted selection box against freshly detected faces: best IoU at or
// above kFaceSelectionMinIoU, otherwise the nearest centre within
// kFaceSelectionMaxCenterDistance stored-face sizes and a plausible size ratio.
std::optional<std::size_t> MatchFaceSelection(const quicksift::NormalizedFaceRect& stored,
    std::span<const quicksift::NormalizedFaceRect> faces) noexcept;

// FaceLock target: the matched user selection when present, otherwise faces[0].
std::optional<std::size_t> ResolveTargetFaceIndex(
    std::span<const quicksift::NormalizedFaceRect> faces,
    const std::optional<quicksift::NormalizedFaceRect>& selection) noexcept;

// Screen-space hit box for one face outline. Rotation is restricted to quarter
// turns, so a drawn face box is always axis-aligned on screen.
struct FaceHitBox {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    std::size_t faceIndex = 0;
};

FaceHitBox InflateFaceHitBox(float left, float top, float right, float bottom,
    std::size_t faceIndex) noexcept;

// Returns the faceIndex of the hit box containing (x, y). Nested/overlapping boxes
// resolve to the smallest box, then the nearest centre, then the lowest index.
std::optional<std::size_t> HitTestFaceBoxes(std::span<const FaceHitBox> boxes,
    float x, float y) noexcept;

// 1-based spatial number (left to right, then top to bottom) used in user-facing
// text such as "Face 2: eyes closed".
std::size_t SpatialFaceNumber(std::span<const quicksift::NormalizedFaceRect> faces,
    std::size_t index) noexcept;

} // namespace quicksift::core
