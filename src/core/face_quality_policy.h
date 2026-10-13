// OWNER: Portable per-face quality analysis (defocus, motion blur, closed eyes) and
// the single tunable threshold table behind the red thumbnail border.
#pragma once

#include "persistent_cache.h"

#include <cstdint>

namespace quicksift::core {

// Bump whenever the algorithm or any threshold below changes meaningfully. Cached
// verdicts carrying another version are treated as "not analyzed" and re-run lazily
// by the worker the next time the photo's face job is scheduled.
inline constexpr std::uint32_t kFaceQualityAnalysisVersion = 2;

// Largest decode the worker performs to obtain a sufficiently high-resolution face
// crop (long edge, pixels). Bounded so one face job cannot balloon memory.
inline constexpr int kFaceQualityMaxDecodeSize = 2048;

struct FaceQualityThresholds {
    // Analysis resolution. Each face crop is resampled to a square of
    // clamp(cropShortEdge, minimumCropPixels, analysisMaxSize) pixels; thresholds are
    // expressed in that analysis space, so blur is judged relative to face size.
    int analysisMaxSize = 256;
    // Crops below this short edge (decoded pixels) are not judged (TooSmall).
    int minimumCropPixels = 64;
    // Preferred crop short edge; the worker re-decodes larger (up to
    // kFaceQualityMaxDecodeSize) when the first decode gives less than this.
    int preferredCropPixels = 256;
    // Robust contrast (p95 - p5 luminance, 0..255) below which nothing is judged.
    float minimumContrast = 28.0f;

    // Defocus/general blur: noise-corrected 90th-percentile block Laplacian RMS,
    // divided by robust contrast, x100. Lower = softer. Calibrated on real
    // portraits (sharp faces 14..41; Gaussian sigma 1 px at the analysis decode
    // drops them to ~5..21, sigma 2 px to ~2..9). 6.0 is roughly 1.3x more
    // sensitive than the 0.9.3 Laplacian-variance threshold while leaving more than
    // 2x headroom below the softest sharp sample.
    float blurDetailThreshold = 6.0f;

    // Motion/shake blur: second-derivative energy along the weakest of four
    // directions (0, 90, 45, 135 degrees) relative to the strongest
    // (anisotropy = 1 - min/max). Natural frontal faces measured 0.46..0.70; 9..17 px
    // linear motion measured 0.78..0.98. Faces whose detail is crisp in every
    // direction cannot be motion blurred, hence the detail ceiling.
    float motionAnisotropyThreshold = 0.80f;
    float motionDetailCeiling = 16.0f;

    // Eyes-closed heuristic (Windows FaceDetector provides no landmarks). Each eye
    // window is searched for visible sclera: pixels at least as bright as
    // scleraLumaFraction x skin luma with chroma below scleraChromaFraction x skin
    // chroma (skin reference = central lower face). Open eyes show sclera; a closed
    // lid is skin plus a lash line. Both eyes must lack sclera to flag.
    // Calibration (10 real frontal portraits): open eyes 0.0015..0.057 sclera
    // fraction (lowest = dim tungsten light), closed eyes 0.0000..0.0011. The
    // threshold sits below every open sample, trading recall (about half of the
    // closed samples) for a low false-positive rate.
    int eyeMinimumCropPixels = 96;          // skip the eye check on smaller faces
    float eyeBandTop = 0.24f;               // fractions of face-box height
    float eyeBandBottom = 0.54f;
    float eyeLeftStart = 0.14f;             // fractions of face-box width
    float eyeLeftEnd = 0.47f;
    float eyeRightStart = 0.53f;
    float eyeRightEnd = 0.86f;
    float eyeMinimumSkinChroma = 10.0f;     // monochrome/heavily graded: skip
    float eyeMinimumSkinLuma = 45.0f;       // too dark to judge: skip
    float eyeScleraLumaFraction = 0.90f;
    float eyeScleraChromaFraction = 0.55f;
    float eyeDarkLumaFraction = 0.45f;      // "dark" = below this x skin luma
    float eyeMinimumDarkCoverage = 0.01f;   // no lash/iris at all: window misplaced, skip
    float eyeMaximumDarkCoverage = 0.40f;   // sunglasses / deep shadow: skip
    float eyeClosedMaximumScleraFraction = 0.0005f;
};

inline constexpr FaceQualityThresholds kFaceQualityThresholds{};

// Analyzes one face on a tightly packed BGRA buffer (stride = width * 4). Pure and
// allocation-light; intended for worker threads only.
quicksift::FaceQualityRecord AnalyzeFaceQualityBgra(const std::uint8_t* pixels,
    int width, int height, const quicksift::NormalizedFaceRect& face,
    const FaceQualityThresholds& thresholds = kFaceQualityThresholds) noexcept;

// Short edge (pixels) the face occupies in a buffer of the given size.
float FaceCropShortEdge(int width, int height, const quicksift::NormalizedFaceRect& face) noexcept;

// Long edge the worker should decode so the face reaches preferredCropPixels,
// clamped to kFaceQualityMaxDecodeSize. Returns 0 when the current buffer suffices.
int RequiredFaceQualityDecodeEdge(int width, int height,
    const quicksift::NormalizedFaceRect& face,
    const FaceQualityThresholds& thresholds = kFaceQualityThresholds) noexcept;

// True when the verdict should raise the red border.
bool FaceQualityFlagged(const quicksift::FaceQualityRecord& quality) noexcept;

} // namespace quicksift::core
