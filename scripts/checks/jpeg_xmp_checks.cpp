// OWNER: Portable checks for the JPEG XMP fast path and EXIF camera-settings parsing.
#include "core/exif_capture_time.h"
#include "metadata/jpeg_xmp_locator.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int g_checks = 0;
int g_failures = 0;
void Check(bool condition, const char* name) {
    ++g_checks;
    if (!condition) { ++g_failures; std::printf("FAIL: %s\n", name); }
}

using Bytes = std::vector<std::uint8_t>;
using namespace quicksift;

void Segment(Bytes& out, std::uint8_t marker, const std::string& payload) {
    const std::size_t length = payload.size() + 2;
    out.push_back(0xFF); out.push_back(marker);
    out.push_back(static_cast<std::uint8_t>(length >> 8)); out.push_back(static_cast<std::uint8_t>(length & 0xFF));
    out.insert(out.end(), payload.begin(), payload.end());
}

std::string XmpPayload(const std::string& description) {
    std::string payload = "http://ns.adobe.com/xap/1.0/";
    payload.push_back('\0');
    payload += "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>"
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
        + description + "</rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>";
    return payload;
}

Bytes Jpeg(const std::vector<std::pair<std::uint8_t, std::string>>& segments, bool withScan = true) {
    Bytes out{ 0xFF, 0xD8 };
    for (const auto& [marker, payload] : segments) Segment(out, marker, payload);
    if (withScan) { out.push_back(0xFF); out.push_back(0xDA); out.push_back(0x00); out.push_back(0x02); }
    return out;
}

const char* kDescription =
    "<rdf:Description rdf:about=\"\" xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\""
    " xmlns:quicksift=\"https://quicksift.local/ns/1.0/\" xmp:Rating=\"4\" xmp:Label=\"Red\""
    " quicksift:Pick=\"1\"/>";

void CheckLocator() {
    using metadata::JpegXmpStatus;
    Check(metadata::LocateJpegXmp(Jpeg({ { 0xE0, "JFIF" } })).status == JpegXmpStatus::Absent,
        "no XMP segment before SOS -> absent");
    Check(metadata::LocateJpegXmp(Jpeg({ { 0xE1, XmpPayload(kDescription) } })).status == JpegXmpStatus::Found,
        "standard XMP APP1 found");
    Check(metadata::LocateJpegXmp(Jpeg({ { 0xE0, "JFIF" } }, false)).status == JpegXmpStatus::Inconclusive,
        "prefix ends before SOS -> inconclusive");
    Check(metadata::LocateJpegXmp(Jpeg({ { 0xE1, XmpPayload(kDescription) }, { 0xE1, XmpPayload(kDescription) } })).status ==
        JpegXmpStatus::Inconclusive, "two standard packets -> inconclusive");
    std::string extended = "http://ns.adobe.com/xmp/extension/";
    extended.push_back('\0');
    extended += "0123456789ABCDEF0123456789ABCDEF";
    Check(metadata::LocateJpegXmp(Jpeg({ { 0xE1, XmpPayload(kDescription) }, { 0xE1, extended } })).status ==
        JpegXmpStatus::Inconclusive, "extended XMP -> inconclusive (Exiv2 merges)");
    const Bytes png{ 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    Check(metadata::LocateJpegXmp(png).status == JpegXmpStatus::Inconclusive, "non-JPEG -> inconclusive");
}

void CheckFields() {
    const auto values = metadata::FastJpegXmpFields(Jpeg({ { 0xE1, XmpPayload(kDescription) } }));
    Check(values && values->rating.IsKnown() && values->rating.value == 4, "rating read");
    Check(values && values->colorLabel.IsKnown() && values->colorLabel.value == 1, "label Red -> 1");
    Check(values && values->pickState.IsKnown() && values->pickState.value == 1, "pick read");
    const auto none = metadata::FastJpegXmpFields(Jpeg({ { 0xE0, "JFIF" } }));
    Check(none && none->rating.IsKnown() && none->rating.value == 0 && none->pickState.value == 0,
        "no XMP -> Known 0 (same as Exiv2 path)");
    const auto element = metadata::FastJpegXmpFields(Jpeg({ { 0xE1, XmpPayload(
        "<rdf:Description rdf:about=\"\" xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\">"
        "<xmp:Rating>2</xmp:Rating></rdf:Description>") } }));
    Check(element && element->rating.value == 2 && element->colorLabel.value == 0, "element form read");
    Check(!metadata::FastJpegXmpFields(Jpeg({ { 0xE1, XmpPayload(
        "<rdf:Description rdf:about=\"\" xmlns:xap=\"http://ns.adobe.com/xap/1.0/\" xap:Rating=\"5\"/>") } })),
        "namespace under an alias prefix -> fall back to Exiv2 (never a wrong 0)");
    Check(!metadata::FastJpegXmpFields(Jpeg({ { 0xE1, XmpPayload(
        "<rdf:Description rdf:about=\"\" xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\" xmp:Rating=\"9\"/>") } })),
        "out-of-range value -> fall back to Exiv2");
    Check(!metadata::FastJpegXmpFields(Jpeg({ { 0xE1, XmpPayload(kDescription) } }, false)),
        "truncated prefix -> fall back");
}

// Minimal little-endian TIFF with IFD0 (Make, Model, ExifIFD) + Exif IFD (ISO).
std::string Exif(const std::string& make, const std::string& model, std::uint16_t isoType) {
    std::string tiff = "II*";
    tiff.push_back('\0');
    const auto u16 = [&](std::uint16_t v) { tiff.push_back(char(v & 0xFF)); tiff.push_back(char(v >> 8)); };
    const auto u32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) tiff.push_back(char((v >> (8 * i)) & 0xFF)); };
    u32(8);
    const std::uint32_t ifd0Size = 2 + 3 * 12 + 4;
    const std::uint32_t makeOffset = 8 + ifd0Size;
    const std::uint32_t modelOffset = makeOffset + static_cast<std::uint32_t>(make.size() + 1);
    const std::uint32_t exifOffset = modelOffset + static_cast<std::uint32_t>(model.size() + 1);
    u16(3);
    u16(0x010F); u16(2); u32(static_cast<std::uint32_t>(make.size() + 1)); u32(makeOffset);
    u16(0x0110); u16(2); u32(static_cast<std::uint32_t>(model.size() + 1)); u32(modelOffset);
    u16(0x8769); u16(4); u32(1); u32(exifOffset);
    u32(0);
    tiff += make; tiff.push_back('\0');
    tiff += model; tiff.push_back('\0');
    u16(1);
    u16(0x8827); u16(isoType); u32(1); u32(800);
    u32(0);
    std::string payload = "Exif";
    payload.push_back('\0'); payload.push_back('\0');
    return payload + tiff;
}

void CheckCamera() {
    namespace exif = core::exif;
    const auto canon = exif::FindCameraSettings(Jpeg({ { 0xE1, Exif("Canon", "Canon EOS R5", 3) } }));
    Check(canon.known && canon.camera == L"Canon EOS R5", "Model starting with Make is not duplicated");
    Check(canon.iso == 800, "SHORT ISO read");
    const auto nikon = exif::FindCameraSettings(Jpeg({ { 0xE1, Exif("NIKON CORPORATION", "NIKON Z 6", 4) } }));
    Check(nikon.camera == L"NIKON CORPORATION NIKON Z 6", "different Make prefix kept");
    Check(nikon.iso == 800, "LONG ISO read");
    const auto none = exif::FindCameraSettings(Jpeg({ { 0xE0, "JFIF" } }));
    Check(!none.known, "JPEG without EXIF: no settings (no scan of image data)");
}

} // namespace

int main() {
    CheckLocator();
    CheckFields();
    CheckCamera();
    std::printf("jpeg_xmp_checks: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
