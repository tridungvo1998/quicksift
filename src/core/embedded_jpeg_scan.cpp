// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#include "core/embedded_jpeg_scan.h"

namespace quicksift::core {
namespace {
struct ScanCancellation {
    const std::function<bool()>& probe;
    unsigned remaining = 0;
    bool cancelled = false;
    bool Stop(std::size_t /*position*/) noexcept {
        if (cancelled) return true;
        if (!probe) return false;
        // Count visited work, not a monotonic byte offset: malformed candidates
        // may rewind the outer scanner after a long entropy walk.
        if (remaining != 0) { --remaining; return false; }
        remaining = 4095;
        try { cancelled = probe(); } catch (...) { cancelled = true; }
        return cancelled;
    }
};

// Parses one JPEG starting at bytes[start] == FFD8. Returns false if the
// stream is not a complete displayable JPEG; `end` is where scanning resumes.
bool ParseOne(std::span<const std::uint8_t> b, std::size_t start, EmbeddedJpegLocation& out,
    std::size_t& end, ScanCancellation& cancellation) noexcept {
    const std::size_t n = b.size();
    std::size_t pos = start + 2;
    int width = 0, height = 0;
    bool sof = false;
    for (;;) {
        if (cancellation.Stop(pos)) return false;
        if (pos >= n || b[pos] != 0xFF) { end = start + 2; return false; }
        while (pos < n && b[pos] == 0xFF) { if (cancellation.Stop(pos)) return false; ++pos; }
        if (pos >= n) { end = n; return false; }
        const std::uint8_t marker = b[pos++];
        if (marker == 0xD9) { end = start + 2; return false; } // EOI before scan
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue; // no length
        if (pos + 2 > n) { end = n; return false; }
        const std::size_t seglen = (static_cast<std::size_t>(b[pos]) << 8) | b[pos + 1];
        if (seglen < 2 || pos + seglen > n) { end = start + 2; return false; }
        if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
            if (seglen < 7) { end = start + 2; return false; }
            height = (b[pos + 3] << 8) | b[pos + 4];
            width = (b[pos + 5] << 8) | b[pos + 6];
            sof = true;
        } else if (marker == 0xC3 || (marker >= 0xC5 && marker <= 0xCF && marker != 0xC8 && marker != 0xCC)) {
            end = pos + seglen; return false; // lossless/arithmetic: not a preview
        }
        pos += seglen;
        if (marker != 0xDA) continue;
        // Entropy-coded data: look for a real marker.
        for (;;) {
            if (cancellation.Stop(pos)) return false;
            while (pos < n && b[pos] != 0xFF) { if (cancellation.Stop(pos)) return false; ++pos; }
            if (pos + 1 >= n) { end = n; return false; }
            const std::uint8_t next = b[pos + 1];
            if (next == 0x00 || next == 0xFF || (next >= 0xD0 && next <= 0xD7)) {
                pos += (next == 0xFF) ? 1 : 2; continue;
            }
            break;
        }
        if (b[pos + 1] == 0xD9) {
            end = pos + 2;
            if (!sof || width <= 0 || height <= 0) return false;
            out = { start, end - start, width, height };
            return true;
        }
        // Another segment (progressive: DHT/SOS ...): continue the marker walk.
    }
}

} // namespace

EmbeddedJpegLocation FindLargestEmbeddedJpeg(std::span<const std::uint8_t> bytes,
    const std::function<bool()>& isCancelled) noexcept {
    ScanCancellation cancellation{isCancelled};
    if (cancellation.Stop(0)) return {};
    EmbeddedJpegLocation best;
    std::size_t i = 0;
    while (i + 3 < bytes.size()) {
        if (cancellation.Stop(i)) return {};
        if (bytes[i] != 0xFF || bytes[i + 1] != 0xD8 || bytes[i + 2] != 0xFF) { ++i; continue; }
        EmbeddedJpegLocation found;
        std::size_t end = i + 2;
        if (ParseOne(bytes, i, found, end, cancellation) &&
            (static_cast<std::int64_t>(found.width) * found.height >
             static_cast<std::int64_t>(best.width) * best.height)) best = found;
        if (cancellation.cancelled) return {};
        i = end > i ? end : i + 1;
    }
    return best;
}

} // namespace quicksift::core
