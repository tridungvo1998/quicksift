// OWNER: Portable locator for the standard XMP packet in a JPEG header prefix.
#include "metadata/jpeg_xmp_locator.h"

#include "core/ratings_exchange.h"
#include "metadata/metadata_value_parser.h"
#include "metadata/xmp_simple_property.h"

#include <cstring>
#include <string>

namespace quicksift::metadata {
namespace {

constexpr char kStandardXmp[] = "http://ns.adobe.com/xap/1.0/"; // + NUL
constexpr char kExtendedXmp[] = "http://ns.adobe.com/xmp/extension/"; // + NUL

bool HasSignature(std::span<const std::uint8_t> payload, const char* signature) noexcept {
    const std::size_t length = std::strlen(signature) + 1; // includes the NUL
    return payload.size() >= length && std::memcmp(payload.data(), signature, length) == 0;
}

std::size_t CountOccurrences(std::wstring_view text, std::wstring_view needle) noexcept {
    std::size_t count = 0;
    for (std::size_t at = text.find(needle); at != std::wstring_view::npos;
        at = text.find(needle, at + needle.size())) ++count;
    return count;
}

// Every mention of the namespace URI must be a declaration under the expected
// prefix; otherwise the property could be bound under an alias the simple
// inspector does not resolve, and "absent" would be wrong.
bool NamespaceOnlyUnderPrefix(std::wstring_view text, std::wstring_view prefix, std::wstring_view uri) {
    const std::size_t mentions = CountOccurrences(text, uri);
    if (mentions == 0) return true;
    const std::wstring base = L"xmlns:" + std::wstring(prefix) + L"=";
    const std::size_t declared =
        CountOccurrences(text, base + L"\"" + std::wstring(uri) + L"\"") +
        CountOccurrences(text, base + L"'" + std::wstring(uri) + L"'");
    return declared == mentions;
}

} // namespace

JpegXmpLocation LocateJpegXmp(std::span<const std::uint8_t> bytes) noexcept {
    JpegXmpLocation result;
    if (bytes.size() < 4 || bytes[0] != 0xFF || bytes[1] != 0xD8) return result;
    std::size_t at = 2;
    std::string_view packet;
    bool found = false;
    for (;;) {
        if (at + 2 > bytes.size()) return result; // truncated before SOS: inconclusive
        if (bytes[at] != 0xFF) return result;    // malformed marker stream
        const std::uint8_t marker = bytes[at + 1];
        if (marker == 0xFF) { ++at; continue; }  // fill byte
        if (marker == 0xDA || marker == 0xD9) break; // start of scan / end of image
        // Standalone markers without a length (TEM, RSTn) are not valid here.
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) return result;
        if (at + 4 > bytes.size()) return result;
        const std::size_t length = (static_cast<std::size_t>(bytes[at + 2]) << 8) | bytes[at + 3];
        if (length < 2) return result;
        const std::size_t payloadStart = at + 4;
        const std::size_t segmentEnd = at + 2 + length;
        if (segmentEnd > bytes.size()) return result; // segment continues past the prefix
        if (marker == 0xE1) {
            const auto payload = bytes.subspan(payloadStart, segmentEnd - payloadStart);
            if (HasSignature(payload, kExtendedXmp)) return result; // ExtendedXMP: let Exiv2 merge
            if (HasSignature(payload, kStandardXmp)) {
                if (found) return result; // two standard packets: ambiguous
                const std::size_t skip = std::strlen(kStandardXmp) + 1;
                packet = std::string_view(reinterpret_cast<const char*>(payload.data() + skip),
                    payload.size() - skip);
                found = true;
            }
        }
        at = segmentEnd;
    }
    result.status = found ? JpegXmpStatus::Found : JpegXmpStatus::Absent;
    result.packet = packet;
    return result;
}

std::optional<core::PhotoMetadataValues> FastJpegXmpFields(std::span<const std::uint8_t> prefix) {
    const JpegXmpLocation location = LocateJpegXmp(prefix);
    if (location.status == JpegXmpStatus::Inconclusive) return std::nullopt;
    core::PhotoMetadataValues values;
    if (location.status == JpegXmpStatus::Absent) {
        values.rating = core::KnownMetadataValue(0);
        values.colorLabel = core::KnownMetadataValue(0);
        values.pickState = core::KnownMetadataValue(0);
        return values;
    }
    auto text = core::ratings_exchange::Utf8ToWide(location.packet);
    if (!text) return std::nullopt;
    // Packets are NUL/space padded; the inspector only needs the XML.
    while (!text->empty() && (text->back() == L'\0' || text->back() == L' ' ||
        text->back() == L'\n' || text->back() == L'\r')) text->pop_back();
    static const XmpPropertyQuery kRating{ L"xmp:Rating", L"xmp", L"http://ns.adobe.com/xap/1.0/" };
    static const XmpPropertyQuery kLabel{ L"xmp:Label", L"xmp", L"http://ns.adobe.com/xap/1.0/" };
    static const XmpPropertyQuery kPick{ L"quicksift:Pick", L"quicksift", L"https://quicksift.local/ns/1.0/" };
    if (!NamespaceOnlyUnderPrefix(*text, kRating.namespaceName, kRating.namespaceUri) ||
        !NamespaceOnlyUnderPrefix(*text, kPick.namespaceName, kPick.namespaceUri)) return std::nullopt;
    const auto integer = [&](const XmpPropertyQuery& query, int low, int high)
        -> std::optional<core::MetadataFieldValue> {
        const XmpSimpleValueInspection inspected = InspectXmpSimpleValue(*text, query);
        if (inspected.state == XmpSimpleValueState::Absent) return core::KnownMetadataValue(0);
        if (inspected.state != XmpSimpleValueState::Value) return std::nullopt;
        if (const auto parsed = ParseBoundedInteger(std::wstring_view(inspected.value), low, high))
            return core::KnownMetadataValue(*parsed);
        return std::nullopt; // unusual value: Exiv2 decides how to report it
    };
    const auto rating = integer(kRating, 0, 5);
    const auto pick = integer(kPick, -1, 1);
    if (!rating || !pick) return std::nullopt;
    const XmpSimpleValueInspection label = InspectXmpSimpleValue(*text, kLabel);
    if (label.state == XmpSimpleValueState::Absent) {
        values.colorLabel = core::KnownMetadataValue(0);
    } else if (label.state == XmpSimpleValueState::Value) {
        const auto parsed = ParseKnownColorLabel(std::wstring_view(label.value));
        if (!parsed) return std::nullopt;
        values.colorLabel = core::KnownMetadataValue(*parsed);
    } else {
        return std::nullopt;
    }
    values.rating = *rating;
    values.pickState = *pick;
    return values;
}

} // namespace quicksift::metadata
