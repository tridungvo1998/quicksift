// OWNER: Portable checks for the largest-embedded-JPEG scanner and the decode timeout policy.
#include "core/decode_timeout_policy.h"
#include "core/embedded_jpeg_scan.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
int g_checks = 0;
int g_failures = 0;
void Check(bool condition, const char* name) {
    ++g_checks;
    if (!condition) { ++g_failures; std::printf("FAIL: %s\n", name); }
}
using Bytes = std::vector<std::uint8_t>;

void Seg(Bytes& o, std::uint8_t m, const Bytes& payload) {
    const std::size_t len = payload.size() + 2;
    o.insert(o.end(), { 0xFF, m, static_cast<std::uint8_t>(len >> 8), static_cast<std::uint8_t>(len & 0xFF) });
    o.insert(o.end(), payload.begin(), payload.end());
}
Bytes Sof(int w, int h) {
    return { 8, static_cast<std::uint8_t>(h >> 8), static_cast<std::uint8_t>(h), static_cast<std::uint8_t>(w >> 8),
        static_cast<std::uint8_t>(w), 1, 1, 0x11, 0 };
}
}

int main() {
    using namespace quicksift::core;
    {
        // Entropy data with FF00 stuffing and RST0 must not end the stream early.
        Bytes j{ 0xFF, 0xD8 };
        Seg(j, 0xC0, Sof(6000, 4000));
        Seg(j, 0xDA, { 1, 1, 0, 0, 63, 0 });
        j.insert(j.end(), { 0x11, 0xFF, 0x00, 0x22, 0xFF, 0xD3, 0x33, 0xFF, 0xD9 });
        Bytes file(100, 0x00);
        const std::size_t off = file.size();
        file.insert(file.end(), j.begin(), j.end());
        file.insert(file.end(), 50, 0xAB);
        const auto r = FindLargestEmbeddedJpeg(file);
        Check(r.Found(), "jpeg after SOS is accepted (old scanner stopped at SOS)");
        Check(r.offset == off && r.length == j.size(), "offset and length span SOI..EOI");
        Check(r.width == 6000 && r.height == 4000, "dimensions from SOF0");
    }
    {
        // Three previews: small, large, medium -> large wins regardless of order.
        Bytes file;
        auto add = [&](int w, int h) {
            Bytes j{ 0xFF, 0xD8 };
            Seg(j, 0xC0, Sof(w, h));
            Seg(j, 0xDA, { 1, 1, 0, 0, 63, 0 });
            j.insert(j.end(), { 0x01, 0x02, 0xFF, 0xD9 });
            file.insert(file.end(), 16, 0x00);
            const std::size_t o = file.size();
            file.insert(file.end(), j.begin(), j.end());
            return o;
        };
        add(160, 120);
        const std::size_t big = add(6048, 4024);
        add(1620, 1080);
        const auto r = FindLargestEmbeddedJpeg(file);
        Check(r.offset == big && r.LongestEdge() == 6048, "largest of several embedded JPEGs is chosen");
    }
    {
        // A JPEG whose APP1 contains a nested EXIF thumbnail: the outer one counts.
        Bytes inner{ 0xFF, 0xD8 };
        Seg(inner, 0xC0, Sof(160, 120));
        Seg(inner, 0xDA, { 1, 1, 0, 0, 63, 0 });
        inner.insert(inner.end(), { 0x05, 0xFF, 0xD9 });
        Bytes outer{ 0xFF, 0xD8 };
        Seg(outer, 0xE1, inner);
        Seg(outer, 0xC2, Sof(3000, 2000)); // progressive
        Seg(outer, 0xDA, { 1, 1, 0, 0, 63, 0 });
        outer.insert(outer.end(), { 0x07, 0x08 });
        Seg(outer, 0xC4, { 0x00, 0x01 }); // DHT between progressive scans
        Seg(outer, 0xDA, { 1, 1, 0, 0, 63, 0 });
        outer.insert(outer.end(), { 0x09, 0xFF, 0xD9 });
        const auto r = FindLargestEmbeddedJpeg(outer);
        Check(r.offset == 0 && r.length == outer.size() && r.width == 3000,
            "nested APP1 thumbnail skipped; progressive multi-scan accepted");
    }
    {
        // Lossless (SOF3) RAW sensor data is not a preview; truncated streams are rejected.
        Bytes lossless{ 0xFF, 0xD8 };
        Seg(lossless, 0xC3, Sof(8000, 6000));
        Seg(lossless, 0xDA, { 1, 1, 0, 0, 63, 0 });
        lossless.insert(lossless.end(), { 0x01, 0xFF, 0xD9 });
        Check(!FindLargestEmbeddedJpeg(lossless).Found(), "SOF3 lossless stream ignored");
        Bytes cut{ 0xFF, 0xD8 };
        Seg(cut, 0xC0, Sof(4000, 3000));
        Seg(cut, 0xDA, { 1, 1, 0, 0, 63, 0 });
        cut.insert(cut.end(), { 0x01, 0x02, 0x03 });
        Check(!FindLargestEmbeddedJpeg(cut).Found(), "truncated JPEG (no EOI) ignored");
        Check(!FindLargestEmbeddedJpeg(Bytes{}).Found(), "empty input");
        Bytes junk(4096, 0xFF);
        Check(!FindLargestEmbeddedJpeg(junk).Found(), "all-0xFF input terminates");
    }
    {
        int probes = 0;
        Bytes junk(200000, 0);
        Check(!FindLargestEmbeddedJpeg(junk, [&] { return ++probes == 3; }).Found() && probes == 3,
            "outer scan cancels at bounded checkpoints");
        Bytes entropy{0xFF, 0xD8}; Seg(entropy, 0xC0, Sof(6000, 4000)); Seg(entropy, 0xDA, {1,1,0,0,63,0});
        entropy.insert(entropy.end(), 200000, 0x01); entropy.insert(entropy.end(), {0xFF,0xD9});
        probes = 0;
        Check(!FindLargestEmbeddedJpeg(entropy, [&] { return ++probes == 3; }).Found() && probes == 3,
            "entropy scan cancellation does not return partial JPEG");
        Check(!FindLargestEmbeddedJpeg(entropy, []() -> bool { throw 1; }).Found(), "throwing cancellation probe treated as cancellation");
        Check(FindLargestEmbeddedJpeg(entropy, [] { return false; }).Found(), "uncancelled entropy retains full validation");
        Bytes malformed{0xFF, 0xD8}; Seg(malformed, 0xC0, Sof(6000, 4000)); Seg(malformed, 0xDA, {1,1,0,0,63,0});
        malformed.insert(malformed.end(), 20000, 0x01);
        malformed.insert(malformed.end(), {0xFF, 0xC4, 0, 1}); // invalid segment rewinds outer scan
        probes = 0;
        Check(!FindLargestEmbeddedJpeg(malformed, [&] { return ++probes == 7; }).Found() && probes == 7,
            "cancellation remains bounded after malformed-candidate rewind");
        entropy.resize(entropy.size() - 2);
        Check(!FindLargestEmbeddedJpeg(entropy, [] { return false; }).Found(), "truncated entropy still rejected");
    }
    {
        using namespace quicksift::core::decode_timeout;
        Check(AdaptiveTimeoutMs(0, true, true) >= 12000, "foreground RAW decode gets >= 12 s");
        Check(AdaptiveTimeoutMs(0, false, true) >= 8000, "foreground non-RAW decode gets >= 8 s");
        Check(AdaptiveTimeoutMs(0, true, false) == 1500, "background RAW preview stays 1.5 s");
        Check(AdaptiveTimeoutMs(1, false, false) == 2000 && AdaptiveTimeoutMs(2, false, false) == 4000,
            "retry ladder doubles");
        Check(AdaptiveTimeoutMs(99, true, true) <= 60000 && AdaptiveTimeoutMs(-5, false, false) == 1000,
            "attempts are clamped; ceiling 60 s");
    }
    std::printf("raw_preview_checks: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
