// OWNER: Deterministic, portable checks for overlay text geometry (toast/alert
// measurement == paint, multi-line text, explicit line breaks) and the left-pane
// frost image builder (src/ui/framework/ui_models.h, src/ui/framework/ui_frost.h).
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Wpedantic -Wshadow -Werror -Isrc
//       scripts/checks/overlay_and_frost_checks.cpp /tmp/qs-build/libQuickSiftCore.a -pthread

#include "ui/framework/ui_frost.h"
#include "ui/framework/ui_models.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace quicksift::ui;
using namespace quicksift::ui::framework;

int gFailures = 0;
int gChecks = 0;

void Check(bool condition, const char* label) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL: %s\n", label);
    }
}

// Mimics the painter's line metrics: a block of `lines` lines of `lineHeight`, the
// widest `widest` DIPs, measured at ToastTextLayoutWidth.
void CheckToastCovers(float surfaceW, float surfaceH, float widest, int lines, const char* label) {
    const float layoutWidth = ToastTextLayoutWidth(surfaceW);
    const float lineHeight = 18.62f; // 14 pt Segoe UI Variable Display, semibold
    const float textW = std::min(widest, layoutWidth);
    const float textH = lineHeight * static_cast<float>(lines);
    const ToastGeometry g = ComputeToastGeometry(surfaceW, surfaceH, textW, textH);
    std::string base(label);
    Check(g.Valid(), (base + ": geometry valid").c_str());
    // Text block (centred in the body) fits inside the padded body on every side.
    Check(g.width >= textW + 2.0f * kToastPaddingX, (base + ": body width covers the widest line + padding").c_str());
    if (!g.clipped) {
        Check(g.height >= textH + 2.0f * kToastPaddingY, (base + ": body height covers every line + padding").c_str());
        Check(g.textTop + textH <= g.height - kToastPaddingY + 0.01f, (base + ": text bottom inside the body").c_str());
        Check(g.layoutHeight >= textH, (base + ": clip box never cuts measured lines").c_str());
    }
    Check(g.textTop >= kToastPaddingY - 0.01f, (base + ": text top below the padding").c_str());
    // Measurement and paint use one layout width: the body never forces a narrower re-wrap.
    Check(g.layoutWidth == layoutWidth, (base + ": paint layout width == measurement width").c_str());
    Check(g.width <= layoutWidth + 2.0f * kToastPaddingX + 0.01f, (base + ": body never exceeds the max width").c_str());
}

void ToastChecks() {
    Check(ToastTextLayoutWidth(1200.0f) == kToastMaxWidth - 2.0f * kToastPaddingX, "wide surface: layout width is the max");
    Check(ToastTextLayoutWidth(300.0f) == 300.0f - 2.0f * kToastEdge - 2.0f * kToastPaddingX, "narrow surface: layout width follows it");
    Check(ToastTextLayoutWidth(80.0f) <= 0.0f, "tiny surface: no toast");
    Check(!ComputeToastGeometry(80.0f, 400.0f, 10.0f, 10.0f).Valid(), "tiny surface: invalid geometry");

    CheckToastCovers(1200.0f, 800.0f, 60.0f, 1, "short single line");
    CheckToastCovers(1200.0f, 800.0f, 388.0f, 1, "full-width single line");
    CheckToastCovers(1200.0f, 800.0f, 388.0f, 3, "wrapped 3 lines");
    CheckToastCovers(1200.0f, 800.0f, 250.0f, 2, "explicit line break");
    CheckToastCovers(1200.0f, 800.0f, 387.6f, 6, "write-lock waiting (6 lines)");
    CheckToastCovers(420.0f, 800.0f, 500.0f, 5, "narrow window, long text");
    CheckToastCovers(1200.0f, 120.0f, 388.0f, 20, "short window clips but stays in the surface");

    const ToastGeometry minimum = ComputeToastGeometry(1200.0f, 800.0f, 10.0f, 18.0f);
    Check(minimum.width >= kToastMinWidth && minimum.height >= kToastMinHeight, "short text keeps the minimum capsule");
    // Fractional widths round outward (device snapping slack): never narrower than text.
    const ToastGeometry fractional = ComputeToastGeometry(1200.0f, 800.0f, 200.4f, 37.3f);
    Check(fractional.width >= 201.0f + 2.0f * kToastPaddingX, "fractional width rounds outward");
    Check(fractional.height >= 38.0f + 2.0f * kToastPaddingY, "fractional height rounds outward");
    const ToastGeometry clipped = ComputeToastGeometry(1200.0f, 120.0f, 300.0f, 900.0f);
    Check(clipped.clipped && clipped.height <= 120.0f - 2.0f * kToastEdge + 0.01f, "overflowing text: body capped to the surface");

    Check(NormalizeOverlayText(L"a\r\nb") == L"a\nb", "CRLF -> LF");
    Check(NormalizeOverlayText(L"a\rb") == L"a\nb", "CR -> LF");
    Check(NormalizeOverlayText(L"a\tb") == L"a b", "tab -> space");
    Check(NormalizeOverlayText(L"line\n\n") == L"line", "trailing breaks trimmed (no empty measured line)");
    Check(NormalizeOverlayText(L"a\n\nb") == L"a\n\nb", "inner blank line kept");
    Check(NormalizeOverlayText(L"") .empty(), "empty stays empty");

    // Alerts: grow with the text, clip only at the surface limit.
    const AlertCardGeometry small = ComputeAlertCardGeometry(800.0f, 20.0f, 18.0f);
    Check(small.cardHeight == kAlertMinHeight && !small.clipped, "short alert keeps the minimum card");
    const AlertCardGeometry tall = ComputeAlertCardGeometry(900.0f, 40.0f, 300.0f);
    Check(tall.cardHeight >= 40.0f + 300.0f + kAlertChromeHeight && !tall.clipped, "long alert grows past the old 340 cap");
    Check(tall.messageBoxHeight >= 300.0f, "long alert: message box holds every line");
    const AlertCardGeometry huge = ComputeAlertCardGeometry(500.0f, 40.0f, 2000.0f);
    Check(huge.clipped && huge.cardHeight <= 500.0f - 48.0f + 0.01f, "huge alert: card stays inside the surface");
    Check(huge.messageBoxHeight > 0.0f && huge.messageBoxHeight < 2000.0f, "huge alert: message clipped inside the card");
}

std::vector<std::uint8_t> Bgra(int w, int h, int stride, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    std::vector<std::uint8_t> data(static_cast<std::size_t>(stride) * static_cast<std::size_t>(h), 0xEE);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = data.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(x) * 4u;
            p[0] = b; p[1] = g; p[2] = r; p[3] = 255;
        }
    }
    return data;
}

int Channel(std::uint32_t pixel, int shift) { return static_cast<int>((pixel >> shift) & 0xFFu); }

void FrostChecks() {
    using namespace quicksift::ui::frost;
    Check(!Build(nullptr, 10, 10, 40, Rgb{}, 0.5f).Valid(), "null source -> invalid");
    Check(!Build(std::vector<std::uint8_t>(16).data(), 0, 4, 0, Rgb{}, 0.5f).Valid(), "zero size -> invalid");
    Check(!Build(std::vector<std::uint8_t>(64).data(), 4, 4, 8, Rgb{}, 0.5f).Valid(), "short stride -> invalid");
    Check(TintAlpha(true) >= 0.80f && TintAlpha(true) <= 0.88f && TintAlpha(false) >= 0.80f && TintAlpha(false) <= 0.88f,
        "tint stays in the readable 80-88% band");
    Check(FromColorRef(0x00332211u).r == 0x11 && FromColorRef(0x00332211u).b == 0x33, "COLORREF unpack");

    // Solid input: output is exactly tint*a + src*(1-a) everywhere, edges included.
    const int w = 37, h = 53, stride = w * 4 + 12; // odd sizes + padded stride
    const auto solid = Bgra(w, h, stride, 200, 100, 50);
    const Rgb tint{ 30, 34, 40 };
    const float a = 0.86f;
    const Image image = Build(solid.data(), w, h, stride, tint, a);
    Check(image.Valid() && image.width == w && image.height == h, "full-size output");
    const int er = static_cast<int>(std::lround(30 * a + 200 * (1 - a)));
    const int eg = static_cast<int>(std::lround(34 * a + 100 * (1 - a)));
    const int eb = static_cast<int>(std::lround(40 * a + 50 * (1 - a)));
    bool exact = true;
    for (std::uint32_t p : image.pixels) {
        exact = exact && std::abs(Channel(p, 16) - er) <= 1 && std::abs(Channel(p, 8) - eg) <= 1 &&
            std::abs(Channel(p, 0) - eb) <= 1 && (p >> 24) == 0;
    }
    Check(exact, "solid source: uniform tinted result, no edge darkening, padding ignored");

    // Alpha 1 -> solid tint; alpha 0 -> the (blurred) source.
    const Image opaque = Build(solid.data(), w, h, stride, tint, 1.0f);
    Check(opaque.pixels.front() == Pack(tint) && opaque.pixels.back() == Pack(tint), "alpha 1 is the solid tint");
    const Image clear = Build(solid.data(), w, h, stride, tint, 0.0f);
    Check(Channel(clear.pixels[100], 16) == 200, "alpha 0 keeps the source colour");

    // Checkerboard: blur flattens contrast (frost, not a sharp copy).
    const int cw = 64, ch = 64;
    std::vector<std::uint8_t> checker(static_cast<std::size_t>(cw) * ch * 4);
    for (int y = 0; y < ch; ++y) for (int x = 0; x < cw; ++x) {
        const std::uint8_t v = ((x / 2 + y / 2) % 2) ? 255 : 0;
        std::uint8_t* p = checker.data() + (static_cast<std::size_t>(y) * cw + x) * 4u;
        p[0] = p[1] = p[2] = v; p[3] = 255;
    }
    const Image blurred = Build(checker.data(), cw, ch, cw * 4, Rgb{}, 0.0f);
    int lo = 255, hi = 0;
    for (std::uint32_t p : blurred.pixels) { lo = std::min(lo, Channel(p, 16)); hi = std::max(hi, Channel(p, 16)); }
    Check(hi - lo <= 24, "checkerboard is blurred to near-uniform grey");

    // Signatures: stable for equal input, different when the content changes.
    const Image again = Build(solid.data(), w, h, stride, tint, a);
    Check(again.signature == image.signature, "signature is deterministic");
    const auto other = Bgra(w, h, stride, 20, 200, 220);
    Check(Build(other.data(), w, h, stride, tint, 0.5f).signature != Build(solid.data(), w, h, stride, tint, 0.5f).signature,
        "signature changes with content");
    const auto tiny = Bgra(w, h, stride, 201, 100, 50); // 1-level noise under heavy tint
    Check(Build(tiny.data(), w, h, stride, tint, a).signature == image.signature, "sub-visible noise keeps the signature");

    // Downscale 1 / radius 0 is a plain tinted copy; 1x1 images work.
    const Image copy = Build(solid.data(), w, h, stride, tint, 0.5f, 1, 0, 0);
    Check(copy.Valid() && Channel(copy.pixels[5], 16) == static_cast<int>(std::lround(30 * 0.5f + 200 * 0.5f)), "no-blur copy");
    const auto one = Bgra(1, 1, 4, 9, 9, 9);
    Check(Build(one.data(), 1, 1, 4, tint, 0.5f).Valid(), "1x1 source");
    const Image fallback = Solid(4, 3, tint);
    Check(fallback.Valid() && fallback.pixels.size() == 12 && fallback.pixels[0] == Pack(tint), "solid fallback");
    Check(!Solid(0, 3, tint).Valid(), "solid fallback rejects empty size");
}

} // namespace

int main() {
    ToastChecks();
    FrostChecks();
    std::printf("overlay_and_frost_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
