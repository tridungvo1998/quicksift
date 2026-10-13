#include "core/exif_capture_time.h"

#include <algorithm>

namespace quicksift::core::exif {
namespace {

constexpr std::uint16_t kTagDateTime = 0x0132;
constexpr std::uint16_t kTagExifIfd = 0x8769;
constexpr std::uint16_t kTagOriginal = 0x9003;
constexpr std::uint16_t kTagDigitized = 0x9004;
constexpr std::uint16_t kTagMake = 0x010F;
constexpr std::uint16_t kTagModel = 0x0110;
constexpr std::uint16_t kTagExposureTime = 0x829A;
constexpr std::uint16_t kTagFNumber = 0x829D;
constexpr std::uint16_t kTagISO = 0x8827;
constexpr std::uint16_t kTagFocalLength = 0x920A;
constexpr std::uint16_t kTagLensModel = 0xA434;
constexpr std::uint16_t kTypeAscii = 2;
constexpr std::uint16_t kTypeShort = 3;
constexpr std::uint16_t kTypeRational = 5;
constexpr std::uint16_t kMaxEntries = 1024;

class TiffReader {
public:
    explicit TiffReader(std::span<const std::uint8_t> data) noexcept : data_(data) {}

    bool Init() noexcept {
        if (data_.size() < 8) return false;
        if (data_[0] == 'I' && data_[1] == 'I') littleEndian_ = true;
        else if (data_[0] == 'M' && data_[1] == 'M') littleEndian_ = false;
        else return false;
        return true;
    }

    bool U16(std::size_t offset, std::uint16_t& value) const noexcept {
        if (offset > data_.size() || data_.size() - offset < 2) return false;
        value = littleEndian_ ?
            static_cast<std::uint16_t>(data_[offset] | (data_[offset + 1] << 8)) :
            static_cast<std::uint16_t>((data_[offset] << 8) | data_[offset + 1]);
        return true;
    }

    bool U32(std::size_t offset, std::uint32_t& value) const noexcept {
        if (offset > data_.size() || data_.size() - offset < 4) return false;
        const std::uint32_t b0 = data_[offset], b1 = data_[offset + 1];
        const std::uint32_t b2 = data_[offset + 2], b3 = data_[offset + 3];
        value = littleEndian_ ? (b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)) :
            ((b0 << 24) | (b1 << 16) | (b2 << 8) | b3);
        return true;
    }

    std::string_view Ascii(std::size_t entry) const noexcept {
        std::uint16_t type = 0;
        std::uint32_t count = 0;
        if (!U16(entry + 2, type) || !U32(entry + 4, count) || type != kTypeAscii) return {};
        if (count < 19 || count > 64) return {};
        std::size_t offset = entry + 8;
        if (count > 4) {
            std::uint32_t pointer = 0;
            if (!U32(entry + 8, pointer)) return {};
            offset = pointer;
        }
        if (offset > data_.size() || data_.size() - offset < count) return {};
        return { reinterpret_cast<const char*>(data_.data() + offset), count };
    }

    // General ASCII (for Make/Model/Lens; shorter strings allowed).
    std::string AsciiFlexible(std::size_t entry) const noexcept {
        std::uint16_t type = 0;
        std::uint32_t count = 0;
        if (!U16(entry + 2, type) || !U32(entry + 4, count) || type != kTypeAscii || count == 0 || count > 256) return {};
        std::size_t offset = entry + 8;
        if (count > 4) {
            std::uint32_t pointer = 0;
            if (!U32(entry + 8, pointer)) return {};
            offset = pointer;
        }
        if (offset > data_.size() || data_.size() - offset < count) return {};
        std::string_view raw{ reinterpret_cast<const char*>(data_.data() + offset), count };
        while (!raw.empty() && (raw.back() == '\0' || raw.back() == ' ')) raw.remove_suffix(1);
        return std::string(raw);
    }

    double Rational(std::size_t entry) const noexcept {
        std::uint16_t type = 0;
        std::uint32_t count = 0;
        if (!U16(entry + 2, type) || !U32(entry + 4, count) || type != kTypeRational || count == 0) return 0.0;
        std::uint32_t pointer = 0;
        if (!U32(entry + 8, pointer)) return 0.0;
        std::uint32_t num = 0, den = 0;
        if (!U32(pointer, num) || !U32(pointer + 4, den) || den == 0) return 0.0;
        return static_cast<double>(num) / static_cast<double>(den);
    }

    int ShortValue(std::size_t entry) const noexcept {
        std::uint16_t type = 0;
        std::uint32_t count = 0;
        if (!U16(entry + 2, type) || !U32(entry + 4, count) || type != kTypeShort || count == 0) return -1;
        std::uint16_t value = 0;
        if (!U16(entry + 8, value)) return -1;
        return static_cast<int>(value);
    }

    void ScanCamera(std::size_t offset, CameraSettingsResult& out) const noexcept {
        std::uint16_t entries = 0;
        if (offset < 8 || !U16(offset, entries) || entries == 0 || entries > kMaxEntries) return;
        for (std::uint16_t index = 0; index < entries; ++index) {
            const std::size_t entry = offset + 2 + static_cast<std::size_t>(index) * 12;
            std::uint16_t tag = 0;
            if (!U16(entry, tag)) return;
            switch (tag) {
            case kTagMake: {
                const std::string make = AsciiFlexible(entry);
                if (!make.empty() && out.camera.empty()) {
                    out.camera.assign(make.begin(), make.end()); // Latin-1 / ASCII
                }
                break;
            }
            case kTagModel: {
                const std::string model = AsciiFlexible(entry);
                if (!model.empty()) {
                    if (!out.camera.empty()) out.camera += L" ";
                    out.camera.append(model.begin(), model.end());
                }
                break;
            }
            case kTagExposureTime: {
                const double v = Rational(entry);
                if (v > 0.0) out.shutterSpeed = v;
                break;
            }
            case kTagFNumber: {
                const double v = Rational(entry);
                if (v > 0.0) out.aperture = v;
                break;
            }
            case kTagISO: {
                const int v = ShortValue(entry);
                if (v > 0) out.iso = v;
                break;
            }
            case kTagFocalLength: {
                const double v = Rational(entry);
                if (v > 0.0) out.focalLength = v;
                break;
            }
            case kTagLensModel: {
                const std::string lens = AsciiFlexible(entry);
                if (!lens.empty()) out.lens.assign(lens.begin(), lens.end());
                break;
            }
            default: break;
            }
        }
        out.known = out.iso >= 0 || out.aperture > 0.0 || out.focalLength > 0.0 ||
            out.shutterSpeed > 0.0 || !out.camera.empty() || !out.lens.empty();
    }

    // Visits the IFD at offset; records date tags and the Exif sub-IFD pointer.
    void ScanIfd(std::size_t offset, CaptureTimeResult& best, std::uint32_t* exifPointer) const noexcept {
        std::uint16_t entries = 0;
        if (offset < 8 || !U16(offset, entries) || entries == 0 || entries > kMaxEntries) return;
        for (std::uint16_t index = 0; index < entries; ++index) {
            const std::size_t entry = offset + 2 + static_cast<std::size_t>(index) * 12;
            std::uint16_t tag = 0;
            if (!U16(entry, tag)) return;
            CaptureTag kind = CaptureTag::None;
            if (tag == kTagOriginal) kind = CaptureTag::Original;
            else if (tag == kTagDigitized) kind = CaptureTag::Digitized;
            else if (tag == kTagDateTime) kind = CaptureTag::DateTime;
            else if (tag == kTagExifIfd && exifPointer) {
                std::uint32_t pointer = 0;
                if (U32(entry + 8, pointer)) *exifPointer = pointer;
                continue;
            }
            if (kind == CaptureTag::None || kind <= best.tag) continue;
            if (const auto time = ParseExifDateTimeText(Ascii(entry))) {
                best.time = *time;
                best.tag = kind;
            }
        }
    }

private:
    std::span<const std::uint8_t> data_;
    bool littleEndian_ = true;
};

bool Better(const std::optional<CaptureTimeResult>& candidate,
    const std::optional<CaptureTimeResult>& current) noexcept {
    return candidate && (!current || candidate->tag > current->tag);
}

bool IsTiffHeader(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    if (at > bytes.size() || bytes.size() - at < 8) return false;
    const std::uint8_t* p = bytes.data() + at;
    return (p[0] == 'I' && p[1] == 'I' && p[2] == 0x2A && p[3] == 0x00) ||
        (p[0] == 'M' && p[1] == 'M' && p[2] == 0x00 && p[3] == 0x2A);
}

int Digits(std::string_view text, std::size_t at, std::size_t count) noexcept {
    if (at + count > text.size()) return -1;
    int value = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const char ch = text[at + index];
        if (ch < '0' || ch > '9') return -1;
        value = value * 10 + (ch - '0');
    }
    return value;
}

} // namespace

std::optional<predicate::LocalDateTime> ParseExifDateTimeText(std::string_view text) noexcept {
    if (text.size() < 19) return std::nullopt;
    const auto dateSep = [&](std::size_t at) { return text[at] == ':' || text[at] == '-'; };
    if (!dateSep(4) || !dateSep(7) || (text[10] != ' ' && text[10] != 'T') ||
        text[13] != ':' || text[16] != ':') return std::nullopt;
    for (std::size_t at = 19; at < text.size(); ++at) {
        const char ch = text[at];
        if (ch != '\0' && ch != ' ' && ch != '.' && ch != 'Z' && ch != '+' && ch != '-' &&
            ch != ':' && (ch < '0' || ch > '9')) return std::nullopt;
    }
    predicate::LocalDateTime time;
    time.year = Digits(text, 0, 4);
    time.month = Digits(text, 5, 2);
    time.day = Digits(text, 8, 2);
    time.hour = Digits(text, 11, 2);
    time.minute = Digits(text, 14, 2);
    time.second = Digits(text, 17, 2);
    if (!time.Valid()) return std::nullopt;
    return time;
}

std::optional<CaptureTimeResult> ReadTiffCaptureTime(std::span<const std::uint8_t> tiff) noexcept {
    TiffReader reader(tiff);
    if (!reader.Init()) return std::nullopt;
    std::uint32_t ifd0 = 0;
    if (!reader.U32(4, ifd0)) return std::nullopt;
    CaptureTimeResult best;
    std::uint32_t exifPointer = 0;
    reader.ScanIfd(ifd0, best, &exifPointer);
    if (exifPointer != 0 && exifPointer != ifd0) reader.ScanIfd(exifPointer, best, nullptr);
    if (best.tag == CaptureTag::None) return std::nullopt;
    return best;
}

std::optional<CaptureTimeResult> FindCaptureTime(std::span<const std::uint8_t> bytes) noexcept {
    std::optional<CaptureTimeResult> best;
    const auto consider = [&](std::span<const std::uint8_t> tiff) {
        const auto candidate = ReadTiffCaptureTime(tiff);
        if (Better(candidate, best)) best = candidate;
    };
    // JPEG: walk the marker segments up to start-of-scan.
    if (bytes.size() >= 4 && bytes[0] == 0xFF && bytes[1] == 0xD8) {
        std::size_t at = 2;
        while (at + 4 <= bytes.size() && bytes[at] == 0xFF) {
            const std::uint8_t marker = bytes[at + 1];
            if (marker == 0xFF) { ++at; continue; }
            if (marker == 0xDA || marker == 0xD9) break;
            const std::size_t length = (static_cast<std::size_t>(bytes[at + 2]) << 8) | bytes[at + 3];
            if (length < 2) break;
            const std::size_t payload = at + 4;
            const std::size_t end = std::min(bytes.size(), at + 2 + length);
            if (marker == 0xE1 && end >= payload + 6 && bytes[payload] == 'E' &&
                bytes[payload + 1] == 'x' && bytes[payload + 2] == 'i' && bytes[payload + 3] == 'f' &&
                bytes[payload + 4] == 0 && bytes[payload + 5] == 0) {
                consider(bytes.subspan(payload + 6, end - (payload + 6)));
                if (best && best->tag == CaptureTag::Original) return best;
            }
            at += 2 + length;
        }
        if (best) return best;
    }
    // TIFF-based files (TIFF, DNG, NEF, ARW, CR2, ORF "IIRO", RW2 "IIU\0", ...).
    if (bytes.size() >= 8 && ((bytes[0] == 'I' && bytes[1] == 'I') || (bytes[0] == 'M' && bytes[1] == 'M'))) {
        consider(bytes);
        if (best && best->tag == CaptureTag::Original) return best;
    }
    // Bounded scan for embedded TIFF blocks (HEIF Exif item, WebP/PNG chunks, CR3 CMT boxes).
    for (std::size_t at = 1; at + 8 <= bytes.size(); ++at) {
        if (!IsTiffHeader(bytes, at)) continue;
        consider(bytes.subspan(at));
        if (best && best->tag == CaptureTag::Original) return best;
    }
    return best;
}

CameraSettingsResult FindCameraSettings(std::span<const std::uint8_t> bytes) noexcept {
    CameraSettingsResult best;
    const auto consider = [&](std::span<const std::uint8_t> tiff) {
        TiffReader reader(tiff);
        if (!reader.Init()) return;
        std::uint32_t ifd0 = 0;
        if (!reader.U32(4, ifd0)) return;
        CameraSettingsResult candidate;
        reader.ScanCamera(ifd0, candidate);
        std::uint32_t exifPointer = 0;
        // Re-scan for Exif IFD pointer (ScanIfd is private to capture time; do a small walk).
        std::uint16_t entries = 0;
        if (reader.U16(ifd0, entries) && entries > 0 && entries <= 1024) {
            for (std::uint16_t i = 0; i < entries; ++i) {
                const std::size_t entry = ifd0 + 2 + static_cast<std::size_t>(i) * 12;
                std::uint16_t tag = 0;
                if (!reader.U16(entry, tag)) break;
                if (tag == 0x8769) {
                    reader.U32(entry + 8, exifPointer);
                    break;
                }
            }
        }
        if (exifPointer != 0 && exifPointer != ifd0) reader.ScanCamera(exifPointer, candidate);
        if (candidate.known) {
            // Prefer the first complete result; later candidates fill gaps only.
            if (best.iso < 0) best.iso = candidate.iso;
            if (best.aperture <= 0.0) best.aperture = candidate.aperture;
            if (best.focalLength <= 0.0) best.focalLength = candidate.focalLength;
            if (best.shutterSpeed <= 0.0) best.shutterSpeed = candidate.shutterSpeed;
            if (best.camera.empty()) best.camera = std::move(candidate.camera);
            if (best.lens.empty()) best.lens = std::move(candidate.lens);
            best.known = true;
        }
    };
    // JPEG APP1
    if (bytes.size() >= 4 && bytes[0] == 0xFF && bytes[1] == 0xD8) {
        std::size_t at = 2;
        while (at + 4 <= bytes.size() && bytes[at] == 0xFF) {
            const std::uint8_t marker = bytes[at + 1];
            if (marker == 0xFF) { ++at; continue; }
            if (marker == 0xDA || marker == 0xD9) break;
            const std::size_t length = (static_cast<std::size_t>(bytes[at + 2]) << 8) | bytes[at + 3];
            if (length < 2) break;
            const std::size_t payload = at + 4;
            const std::size_t end = std::min(bytes.size(), at + 2 + length);
            if (marker == 0xE1 && end >= payload + 6 && bytes[payload] == 'E' &&
                bytes[payload + 1] == 'x' && bytes[payload + 2] == 'i' && bytes[payload + 3] == 'f' &&
                bytes[payload + 4] == 0 && bytes[payload + 5] == 0) {
                consider(bytes.subspan(payload + 6, end - (payload + 6)));
                if (best.known) return best;
            }
            at += 2 + length;
        }
        if (best.known) return best;
    }
    // TIFF-based
    if (bytes.size() >= 8 && ((bytes[0] == 'I' && bytes[1] == 'I') || (bytes[0] == 'M' && bytes[1] == 'M'))) {
        consider(bytes);
        if (best.known) return best;
    }
    // Embedded TIFF scan
    for (std::size_t at = 1; at + 8 <= bytes.size(); ++at) {
        if (!IsTiffHeader(bytes, at)) continue;
        consider(bytes.subspan(at));
        if (best.known) return best;
    }
    return best;
}

} // namespace quicksift::core::exif
