// OWNER: Platform-neutral frosted-backdrop image builder (see ui_frost.h).
#include "ui/framework/ui_frost.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace quicksift::ui::frost {
namespace {

// One horizontal or vertical box pass with edge clamping on a planar float buffer.
void BoxPass(std::vector<float>& data, std::vector<float>& scratch, int width, int height,
    int radius, bool horizontal) {
    const int count = horizontal ? width : height;
    const int lines = horizontal ? height : width;
    const float norm = 1.0f / static_cast<float>(2 * radius + 1);
    scratch.resize(data.size());
    for (int line = 0; line < lines; ++line) {
        const auto at = [&](int i) -> float {
            const int c = std::clamp(i, 0, count - 1);
            const std::size_t index = horizontal
                ? static_cast<std::size_t>(line) * static_cast<std::size_t>(width) + static_cast<std::size_t>(c)
                : static_cast<std::size_t>(c) * static_cast<std::size_t>(width) + static_cast<std::size_t>(line);
            return data[index];
        };
        float sum = 0.0f;
        for (int i = -radius; i <= radius; ++i) sum += at(i);
        for (int i = 0; i < count; ++i) {
            const std::size_t index = horizontal
                ? static_cast<std::size_t>(line) * static_cast<std::size_t>(width) + static_cast<std::size_t>(i)
                : static_cast<std::size_t>(i) * static_cast<std::size_t>(width) + static_cast<std::size_t>(line);
            scratch[index] = sum * norm;
            sum += at(i + radius + 1) - at(i - radius);
        }
    }
    data.swap(scratch);
}

std::uint8_t ToByte(float value) noexcept {
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

} // namespace

Image Solid(int width, int height, Rgb color) {
    Image image;
    if (width <= 0 || height <= 0) return image;
    image.width = width;
    image.height = height;
    image.pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), Pack(color));
    image.signature = 0x9E3779B97F4A7C15ull ^ Pack(color);
    return image;
}

Image Build(const std::uint8_t* bgra, int width, int height, int strideBytes,
    Rgb tint, float tintAlpha, int downscale, int blurRadius, int blurPasses) {
    Image image;
    if (!bgra || width <= 0 || height <= 0 || strideBytes < width * 4) return image;
    downscale = std::max(1, downscale);
    blurRadius = std::max(0, blurRadius);
    blurPasses = std::max(0, blurPasses);
    const float alpha = std::clamp(tintAlpha, 0.0f, 1.0f);

    // 1. Box-average downscale (partial edge blocks average what they cover).
    const int sw = (width + downscale - 1) / downscale;
    const int sh = (height + downscale - 1) / downscale;
    const std::size_t smallCount = static_cast<std::size_t>(sw) * static_cast<std::size_t>(sh);
    std::vector<float> channel[3];
    for (auto& c : channel) c.assign(smallCount, 0.0f);
    for (int by = 0; by < sh; ++by) {
        const int y0 = by * downscale;
        const int y1 = std::min(height, y0 + downscale);
        for (int bx = 0; bx < sw; ++bx) {
            const int x0 = bx * downscale;
            const int x1 = std::min(width, x0 + downscale);
            float r = 0.0f, g = 0.0f, b = 0.0f;
            for (int y = y0; y < y1; ++y) {
                const std::uint8_t* row = bgra + static_cast<std::size_t>(y) * static_cast<std::size_t>(strideBytes);
                for (int x = x0; x < x1; ++x) {
                    const std::uint8_t* p = row + static_cast<std::size_t>(x) * 4u;
                    b += p[0]; g += p[1]; r += p[2];
                }
            }
            const float n = static_cast<float>((y1 - y0) * (x1 - x0));
            const std::size_t i = static_cast<std::size_t>(by) * static_cast<std::size_t>(sw) + static_cast<std::size_t>(bx);
            channel[0][i] = r / n;
            channel[1][i] = g / n;
            channel[2][i] = b / n;
        }
    }

    // 2. Separable box blur, repeated (2 passes ~ gaussian).
    std::vector<float> scratch;
    if (blurRadius > 0) {
        for (auto& c : channel) {
            for (int pass = 0; pass < blurPasses; ++pass) {
                BoxPass(c, scratch, sw, sh, blurRadius, true);
                BoxPass(c, scratch, sw, sh, blurRadius, false);
            }
        }
    }

    // 3. Tint mix + quantized signature (FNV-1a).
    const float tintChannel[3] = { static_cast<float>(tint.r), static_cast<float>(tint.g),
        static_cast<float>(tint.b) };
    std::uint64_t hash = 1469598103934665603ull;
    for (std::size_t i = 0; i < smallCount; ++i) {
        for (int k = 0; k < 3; ++k) {
            const float mixed = tintChannel[k] * alpha + channel[k][i] * (1.0f - alpha);
            channel[k][i] = mixed;
            hash ^= static_cast<std::uint64_t>(ToByte(mixed) >> 2);
            hash *= 1099511628211ull;
        }
    }
    hash ^= (static_cast<std::uint64_t>(width) << 32) ^ static_cast<std::uint64_t>(height);

    // 4. Bilinear upscale to the full size (sample centres aligned).
    image.width = width;
    image.height = height;
    image.signature = hash;
    image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    const float inv = 1.0f / static_cast<float>(downscale);
    std::vector<int> xi0(static_cast<std::size_t>(width)), xi1(static_cast<std::size_t>(width));
    std::vector<float> xf(static_cast<std::size_t>(width));
    for (int x = 0; x < width; ++x) {
        const float sx = std::clamp((static_cast<float>(x) + 0.5f) * inv - 0.5f, 0.0f,
            static_cast<float>(sw - 1));
        const int x0 = static_cast<int>(sx);
        xi0[static_cast<std::size_t>(x)] = x0;
        xi1[static_cast<std::size_t>(x)] = std::min(x0 + 1, sw - 1);
        xf[static_cast<std::size_t>(x)] = sx - static_cast<float>(x0);
    }
    for (int y = 0; y < height; ++y) {
        const float sy = std::clamp((static_cast<float>(y) + 0.5f) * inv - 0.5f, 0.0f,
            static_cast<float>(sh - 1));
        const int y0 = static_cast<int>(sy);
        const int y1 = std::min(y0 + 1, sh - 1);
        const float fy = sy - static_cast<float>(y0);
        const std::size_t row0 = static_cast<std::size_t>(y0) * static_cast<std::size_t>(sw);
        const std::size_t row1 = static_cast<std::size_t>(y1) * static_cast<std::size_t>(sw);
        std::uint32_t* out = image.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        for (int x = 0; x < width; ++x) {
            const std::size_t a = row0 + static_cast<std::size_t>(xi0[static_cast<std::size_t>(x)]);
            const std::size_t b = row0 + static_cast<std::size_t>(xi1[static_cast<std::size_t>(x)]);
            const std::size_t c = row1 + static_cast<std::size_t>(xi0[static_cast<std::size_t>(x)]);
            const std::size_t d = row1 + static_cast<std::size_t>(xi1[static_cast<std::size_t>(x)]);
            const float fx = xf[static_cast<std::size_t>(x)];
            std::uint8_t v[3];
            for (int k = 0; k < 3; ++k) {
                const std::vector<float>& ch = channel[k];
                const float top = ch[a] + (ch[b] - ch[a]) * fx;
                const float bottom = ch[c] + (ch[d] - ch[c]) * fx;
                v[k] = ToByte(top + (bottom - top) * fy);
            }
            out[x] = Pack(Rgb{ v[0], v[1], v[2] });
        }
    }
    return image;
}

} // namespace quicksift::ui::frost
