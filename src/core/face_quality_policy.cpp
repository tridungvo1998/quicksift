// OWNER: Portable per-face quality analysis (defocus, motion blur, closed eyes) and
// the single tunable threshold table behind the red thumbnail border.
#include "core/face_quality_policy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace quicksift::core {
namespace {

struct CropGeometry {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool valid = false;
};

CropGeometry CropFor(int width, int height, const quicksift::NormalizedFaceRect& face) noexcept {
    CropGeometry crop;
    if (width <= 0 || height <= 0) return crop;
    if (!std::isfinite(face.x) || !std::isfinite(face.y) ||
        !std::isfinite(face.width) || !std::isfinite(face.height)) return crop;
    const float imageWidth = static_cast<float>(width);
    const float imageHeight = static_cast<float>(height);
    const float left = std::clamp(face.x, 0.0f, 1.0f) * imageWidth;
    const float top = std::clamp(face.y, 0.0f, 1.0f) * imageHeight;
    const float right = std::clamp(face.x + face.width, 0.0f, 1.0f) * imageWidth;
    const float bottom = std::clamp(face.y + face.height, 0.0f, 1.0f) * imageHeight;
    if (!(right - left >= 1.0f) || !(bottom - top >= 1.0f)) return crop;
    crop = { left, top, right - left, bottom - top, true };
    return crop;
}

struct Channels {
    float luma = 0.0f;
    float cb = 0.0f;
    float cr = 0.0f;
};

inline Channels PixelChannels(const std::uint8_t* pixel) noexcept {
    const float blue = static_cast<float>(pixel[0]);
    const float green = static_cast<float>(pixel[1]);
    const float red = static_cast<float>(pixel[2]);
    return { (29.0f * blue + 150.0f * green + 77.0f * red) / 256.0f,
        -0.1687f * red - 0.3313f * green + 0.5f * blue,
        0.5f * red - 0.4187f * green - 0.0813f * blue };
}

struct Patch {
    int size = 0;
    std::vector<float> luma;
    std::vector<float> chroma;   // |(Cb, Cr)| of the area-averaged colour
};

// Resamples the crop to size x size: box average when downsampling, bilinear when
// the crop is smaller than the analysis square (only possible for non-square crops).
void BuildPatch(const std::uint8_t* pixels, int width, int height, const CropGeometry& crop,
    int size, Patch& patch) {
    patch.size = size;
    const std::size_t count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    patch.luma.assign(count, 0.0f);
    patch.chroma.assign(count, 0.0f);
    const float stepX = crop.width / static_cast<float>(size);
    const float stepY = crop.height / static_cast<float>(size);
    const auto channelsAt = [&](int x, int y) noexcept {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return PixelChannels(pixels + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(x)) * 4u);
    };
    const bool boxFilter = stepX >= 1.0f && stepY >= 1.0f;
    for (int row = 0; row < size; ++row) {
        const float sy0 = crop.y + stepY * static_cast<float>(row);
        for (int column = 0; column < size; ++column) {
            const float sx0 = crop.x + stepX * static_cast<float>(column);
            Channels value;
            if (boxFilter) {
                const int x0 = static_cast<int>(std::floor(sx0));
                const int y0 = static_cast<int>(std::floor(sy0));
                const int x1 = std::max(x0 + 1, static_cast<int>(std::floor(sx0 + stepX)));
                const int y1 = std::max(y0 + 1, static_cast<int>(std::floor(sy0 + stepY)));
                Channels sum;
                int samples = 0;
                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) {
                        const Channels sample = channelsAt(x, y);
                        sum.luma += sample.luma; sum.cb += sample.cb; sum.cr += sample.cr;
                        ++samples;
                    }
                }
                const float inverse = samples > 0 ? 1.0f / static_cast<float>(samples) : 0.0f;
                value = { sum.luma * inverse, sum.cb * inverse, sum.cr * inverse };
            } else {
                const float fx = sx0 + stepX * 0.5f - 0.5f;
                const float fy = sy0 + stepY * 0.5f - 0.5f;
                const int x0 = static_cast<int>(std::floor(fx));
                const int y0 = static_cast<int>(std::floor(fy));
                const float ax = fx - static_cast<float>(x0);
                const float ay = fy - static_cast<float>(y0);
                const Channels c00 = channelsAt(x0, y0), c10 = channelsAt(x0 + 1, y0);
                const Channels c01 = channelsAt(x0, y0 + 1), c11 = channelsAt(x0 + 1, y0 + 1);
                const auto lerp2 = [&](float v00, float v10, float v01, float v11) noexcept {
                    const float top = v00 * (1.0f - ax) + v10 * ax;
                    const float bottom = v01 * (1.0f - ax) + v11 * ax;
                    return top * (1.0f - ay) + bottom * ay;
                };
                value = { lerp2(c00.luma, c10.luma, c01.luma, c11.luma),
                    lerp2(c00.cb, c10.cb, c01.cb, c11.cb), lerp2(c00.cr, c10.cr, c01.cr, c11.cr) };
            }
            const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(size) +
                static_cast<std::size_t>(column);
            patch.luma[index] = value.luma;
            patch.chroma[index] = std::sqrt(value.cb * value.cb + value.cr * value.cr);
        }
    }
}

struct Percentiles {
    float low = 0.0f;
    float high = 0.0f;
};

Percentiles RegionPercentiles(const std::vector<float>& patch, int size,
    int x0, int y0, int x1, int y1, float lowFraction, float highFraction) noexcept {
    std::array<int, 256> histogram{};
    int total = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float value = patch[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                static_cast<std::size_t>(x)];
            ++histogram[static_cast<std::size_t>(std::clamp(static_cast<int>(value + 0.5f), 0, 255))];
            ++total;
        }
    }
    Percentiles result;
    if (total == 0) return result;
    const int lowTarget = static_cast<int>(static_cast<float>(total) * lowFraction);
    const int highTarget = static_cast<int>(static_cast<float>(total) * highFraction);
    int running = 0;
    bool lowSet = false;
    for (int bin = 0; bin < 256; ++bin) {
        running += histogram[static_cast<std::size_t>(bin)];
        if (!lowSet && running > lowTarget) {
            result.low = static_cast<float>(bin);
            lowSet = true;
        }
        if (running > highTarget) {
            result.high = static_cast<float>(bin);
            break;
        }
    }
    return result;
}

float RegionMedian(const std::vector<float>& values, int size,
    int x0, int y0, int x1, int y1) noexcept {
    std::array<int, 256> histogram{};
    int total = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float value = values[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                static_cast<std::size_t>(x)];
            ++histogram[static_cast<std::size_t>(std::clamp(static_cast<int>(value + 0.5f), 0, 255))];
            ++total;
        }
    }
    int running = 0;
    for (int bin = 0; bin < 256; ++bin) {
        running += histogram[static_cast<std::size_t>(bin)];
        if (running * 2 > total) return static_cast<float>(bin);
    }
    return 0.0f;
}

struct EyeVerdict {
    bool evaluable = false;
    bool closed = false;
    float scleraFraction = 0.0f;
};

EyeVerdict EvaluateEye(const Patch& patch, int x0, int y0, int x1, int y1,
    float skinLuma, float skinChroma, const FaceQualityThresholds& t) noexcept {
    EyeVerdict verdict;
    if (x1 - x0 < 6 || y1 - y0 < 6) return verdict;
    const float scleraLuma = skinLuma * t.eyeScleraLumaFraction;
    const float scleraChroma = skinChroma * t.eyeScleraChromaFraction;
    const float darkLuma = skinLuma * t.eyeDarkLumaFraction;
    int sclera = 0;
    int dark = 0;
    int total = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(patch.size) +
                static_cast<std::size_t>(x);
            const float luma = patch.luma[index];
            if (luma >= scleraLuma && patch.chroma[index] <= scleraChroma) ++sclera;
            if (luma < darkLuma) ++dark;
            ++total;
        }
    }
    if (total == 0) return verdict;
    const float darkCoverage = static_cast<float>(dark) / static_cast<float>(total);
    if (darkCoverage < t.eyeMinimumDarkCoverage || darkCoverage > t.eyeMaximumDarkCoverage)
        return verdict;
    verdict.evaluable = true;
    verdict.scleraFraction = static_cast<float>(sclera) / static_cast<float>(total);
    verdict.closed = verdict.scleraFraction < t.eyeClosedMaximumScleraFraction;
    return verdict;
}

} // namespace

float FaceCropShortEdge(int width, int height, const quicksift::NormalizedFaceRect& face) noexcept {
    const CropGeometry crop = CropFor(width, height, face);
    return crop.valid ? std::min(crop.width, crop.height) : 0.0f;
}

int RequiredFaceQualityDecodeEdge(int width, int height,
    const quicksift::NormalizedFaceRect& face, const FaceQualityThresholds& t) noexcept {
    const float shortEdge = FaceCropShortEdge(width, height, face);
    if (!(shortEdge > 0.0f) || shortEdge >= static_cast<float>(t.preferredCropPixels)) return 0;
    const int longEdge = std::max(width, height);
    if (longEdge >= kFaceQualityMaxDecodeSize) return 0;
    const float scale = static_cast<float>(t.preferredCropPixels) / shortEdge;
    const float wanted = std::ceil(static_cast<float>(longEdge) * scale * 1.02f);
    const int required = static_cast<int>(std::min(wanted, static_cast<float>(kFaceQualityMaxDecodeSize)));
    return required > longEdge ? required : 0;
}

bool FaceQualityFlagged(const quicksift::FaceQualityRecord& quality) noexcept {
    using namespace quicksift::FaceQualityFlag;
    if ((quality.flags & Analyzed) == 0) return false;
    return (quality.flags & (Blurry | MotionBlur | EyesClosed)) != 0;
}

quicksift::FaceQualityRecord AnalyzeFaceQualityBgra(const std::uint8_t* pixels,
    int width, int height, const quicksift::NormalizedFaceRect& face,
    const FaceQualityThresholds& t) noexcept {
    using namespace quicksift::FaceQualityFlag;
    quicksift::FaceQualityRecord result;
    if (!pixels || width < 16 || height < 16) return result;
    const CropGeometry crop = CropFor(width, height, face);
    if (!crop.valid) return result;
    const float cropShort = std::min(crop.width, crop.height);
    if (cropShort < static_cast<float>(t.minimumCropPixels)) {
        result.flags = Analyzed | TooSmall;
        return result;
    }

    try {
        const int size = std::clamp(static_cast<int>(cropShort), t.minimumCropPixels, t.analysisMaxSize);
        Patch patch;
        BuildPatch(pixels, width, height, crop, size, patch);
        const auto at = [&](int x, int y) noexcept {
            return patch.luma[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                static_cast<std::size_t>(x)];
        };

        const Percentiles range = RegionPercentiles(patch.luma, size, 0, 0, size, size, 0.05f, 0.95f);
        const float contrast = range.high - range.low;
        if (contrast < t.minimumContrast) {
            result.flags = Analyzed | LowContrast;
            return result;
        }

        constexpr int kGrid = 8;
        const int blockSize = std::max(1, size / kGrid);
        std::array<double, kGrid * kGrid> blockSum{};
        std::array<int, kGrid * kGrid> blockCount{};
        std::array<double, 4> directional{};
        for (int y = 1; y < size - 1; ++y) {
            const int blockY = std::min(kGrid - 1, y / blockSize);
            for (int x = 1; x < size - 1; ++x) {
                const double c = at(x, y);
                const double l = at(x - 1, y), r = at(x + 1, y);
                const double u = at(x, y - 1), d = at(x, y + 1);
                const double ul = at(x - 1, y - 1), ur = at(x + 1, y - 1);
                const double dl = at(x - 1, y + 1), dr = at(x + 1, y + 1);
                const double laplacian = 4.0 * c - l - r - u - d;
                const int block = blockY * kGrid + std::min(kGrid - 1, x / blockSize);
                blockSum[static_cast<std::size_t>(block)] += laplacian * laplacian;
                ++blockCount[static_cast<std::size_t>(block)];
                const double dxx = l - 2.0 * c + r;
                const double dyy = u - 2.0 * c + d;
                const double d1 = 0.5 * (ul - 2.0 * c + dr);
                const double d2 = 0.5 * (ur - 2.0 * c + dl);
                directional[0] += dxx * dxx;
                directional[1] += dyy * dyy;
                directional[2] += d1 * d1;
                directional[3] += d2 * d2;
            }
        }

        std::vector<double> blockRms;
        blockRms.reserve(blockSum.size());
        for (std::size_t index = 0; index < blockSum.size(); ++index) {
            if (blockCount[index] > 0)
                blockRms.push_back(std::sqrt(blockSum[index] / static_cast<double>(blockCount[index])));
        }
        std::sort(blockRms.begin(), blockRms.end());
        const auto percentile = [&](double fraction) {
            const std::size_t index = static_cast<std::size_t>(
                fraction * static_cast<double>(blockRms.size() - 1));
            return blockRms[index];
        };
        const double high = blockRms.empty() ? 0.0 : percentile(0.90);
        const double floor = blockRms.empty() ? 0.0 : percentile(0.10);
        // Subtract the isotropic noise floor (smoothest blocks), but never more than
        // half of the peak energy so uniformly textured faces are not penalized.
        const double corrected = std::sqrt(std::max(0.0,
            high * high - std::min(floor * floor, 0.5 * high * high)));
        const float detail = static_cast<float>(corrected /
            static_cast<double>(std::max(contrast, t.minimumContrast)) * 100.0);

        const double maximumDirectional = *std::max_element(directional.begin(), directional.end());
        const double minimumDirectional = *std::min_element(directional.begin(), directional.end());
        const float anisotropy = maximumDirectional > 0.0 ?
            static_cast<float>(1.0 - minimumDirectional / maximumDirectional) : 0.0f;

        result.sharpness = std::isfinite(detail) ? std::max(0.0f, detail) : 0.0f;
        result.motionAnisotropy = std::isfinite(anisotropy) ? std::clamp(anisotropy, 0.0f, 1.0f) : 0.0f;
        result.flags = Analyzed;
        if (result.sharpness < t.blurDetailThreshold) result.flags |= Blurry;
        if (result.motionAnisotropy >= t.motionAnisotropyThreshold &&
            result.sharpness < t.motionDetailCeiling) {
            result.flags |= MotionBlur;
        }

        if (cropShort >= static_cast<float>(t.eyeMinimumCropPixels)) {
            const auto at01 = [&](float fraction) {
                return std::clamp(static_cast<int>(fraction * static_cast<float>(size)), 0, size);
            };
            // Skin reference: central lower face (cheeks/nose), robust medians.
            const int skinX0 = at01(0.30f), skinX1 = at01(0.70f);
            const int skinY0 = at01(0.52f), skinY1 = at01(0.78f);
            const float skinLuma = RegionMedian(patch.luma, size, skinX0, skinY0, skinX1, skinY1);
            const float skinChroma = RegionMedian(patch.chroma, size, skinX0, skinY0, skinX1, skinY1);
            if (skinLuma >= t.eyeMinimumSkinLuma && skinChroma >= t.eyeMinimumSkinChroma) {
                const int top = at01(t.eyeBandTop);
                const int bottom = at01(t.eyeBandBottom);
                const EyeVerdict leftEye = EvaluateEye(patch, at01(t.eyeLeftStart), top,
                    at01(t.eyeLeftEnd), bottom, skinLuma, skinChroma, t);
                const EyeVerdict rightEye = EvaluateEye(patch, at01(t.eyeRightStart), top,
                    at01(t.eyeRightEnd), bottom, skinLuma, skinChroma, t);
                if (leftEye.evaluable && rightEye.evaluable) {
                    result.flags |= EyesChecked;
                    result.eyeOpenness = std::min(leftEye.scleraFraction, rightEye.scleraFraction);
                    // Conservative: a blink closes both eyes; a single "closed" eye is
                    // more often a wink, squint, hair, or a turned head than a blink.
                    if (leftEye.closed && rightEye.closed) result.flags |= EyesClosed;
                }
            }
        }
    } catch (...) {
        // Allocation failure: report "not analyzed" so the worker retries later.
        result = {};
    }
    return result;
}

} // namespace quicksift::core
