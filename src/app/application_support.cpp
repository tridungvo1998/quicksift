// OWNER: Shared native helpers used by UI and background services.
#include "core/sidecar_write_policy.h"
#include "core/xmp_interop_policy.h"
#include "app/application_support.h"
#include "core/metadata_vocabulary.h"
#include "diagnostics/log_and_continue.h"

namespace quicksift::app {

std::atomic<std::uint64_t> gMaximumDecodedPixelBytes{ 1024ull * 1024ull * 1024ull };
std::atomic<UINT> gWicJpegIndexInterval{ 2048u };
std::atomic<unsigned> gRetainedWicDecoderSessions{ 1u };
std::atomic<std::uint32_t> gMetadataDirectMask{ kDefaultMetadataDirectMask };

float Clamp(float value, float lo, float hi) {
    return std::max(lo, std::min(value, hi));
}
std::wstring ToLower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
    });
    return text;
}
std::wstring ExtensionLower(const fs::path& path) {
    return ToLower(path.extension().wstring());
}
bool IsRawExtension(const std::wstring& ext) {
    return quicksift::core::IsCameraRawExtension(ext);
}
bool IsJpegExtension(const std::wstring& ext) {
    return quicksift::core::IsJpegExtension(ext);
}
void NormalizeSourceGeometryToPixelOrientation(int pixelWidth, int pixelHeight,
    int& sourceWidth, int& sourceHeight) {
    if (pixelWidth <= 0 || pixelHeight <= 0 || sourceWidth <= 0 || sourceHeight <= 0 ||
        pixelWidth == pixelHeight || sourceWidth == sourceHeight) return;
    if ((pixelHeight > pixelWidth) != (sourceHeight > sourceWidth))
        std::swap(sourceWidth, sourceHeight);
}
std::uint32_t MetadataDirectFlagForPath(const fs::path& path) {
    const std::wstring ext = ExtensionLower(path);
    if (ext == L".jpg" || ext == L".jpeg" || ext == L".jpe") return MetadataDirectJpeg;
    if (ext == L".png") return MetadataDirectPng;
    if (ext == L".tif" || ext == L".tiff") return MetadataDirectTiff;
    if (IsRawExtension(ext)) return MetadataDirectRaw;
    return 0;
}
bool IsJpegPath(const fs::path& path) {
    return MetadataDirectFlagForPath(path) == MetadataDirectJpeg;
}
bool UsesDirectMetadata(const fs::path& path) {
    if (!quicksift::EmbeddedMetadataAvailable()) return false;
    const std::uint32_t flag = MetadataDirectFlagForPath(path);
    return flag != 0 && (gMetadataDirectMask.load(std::memory_order_relaxed) & flag) != 0;
}
std::int64_t MetadataStampFor(const fs::path& path) {
    std::uint64_t stamp = static_cast<std::uint64_t>(quicksift::PersistentCache::SidecarStampFor(path));
    const std::uint64_t policy = static_cast<std::uint64_t>(MetadataDirectFlagForPath(path)) << 56;
    if (UsesDirectMetadata(path)) stamp ^= policy ^ 0x6d657461646972ULL;
    return static_cast<std::int64_t>(stamp);
}
std::wstring FormatFileSize(uintmax_t bytes) {
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    std::wostringstream out;
    out << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << value << L' ' << units[unit];
    return out.str();
}
std::optional<std::uint64_t> FreeBytesForPath(const fs::path& path) {
    return quicksift::platform::FreeBytesForPath(path);
}
bool HasStorageHeadroom(const fs::path& path, std::uint64_t requiredBytes,
    std::uint64_t reserveBytes, std::uint64_t* availableOut) {
    return quicksift::platform::HasStorageHeadroom(
        path, requiredBytes, reserveBytes, availableOut);
}
std::optional<std::wstring> VolumeRootForPath(const fs::path& path) {
    return quicksift::platform::VolumeRootForPath(path);
}
std::uint64_t SaturatingAdd(std::uint64_t left, std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return std::numeric_limits<std::uint64_t>::max();
    return left + right;
}
size_t SaturatingSizeAdd(size_t left, size_t right) noexcept {
    if (right > std::numeric_limits<size_t>::max() - left)
        return std::numeric_limits<size_t>::max();
    return left + right;
}
bool IsWellFormedUtf16(std::wstring_view value) noexcept {
    for (std::size_t index = 0; index < value.size(); ++index) {
        const std::uint16_t unit = static_cast<std::uint16_t>(value[index]);
        if (unit >= 0xD800u && unit <= 0xDBFFu) {
            if (++index >= value.size()) return false;
            const std::uint16_t low = static_cast<std::uint16_t>(value[index]);
            if (low < 0xDC00u || low > 0xDFFFu) return false;
        } else if (unit >= 0xDC00u && unit <= 0xDFFFu) {
            return false;
        }
    }
    return true;
}
std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !IsWellFormedUtf16(value)) return {};
    const int length = static_cast<int>(value.size());
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), length, nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length,
        result.data(), count, nullptr, nullptr) != count) return {};
    return result;
}
std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return {};
    const int length = static_cast<int>(value.size());
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        length, nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        length, result.data(), count) != count) return {};
    return result;
}
std::wstring ReadTextFile(const fs::path& path, TextFileEncoding* encoding,
    bool* decodedSuccessfully) {
    if (encoding) *encoding = TextFileEncoding::Utf8;
    if (decodedSuccessfully) *decodedSuccessfully = false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    constexpr std::streamoff kMaximumSidecarBytes = 16ll * 1024ll * 1024ll;
    if (length < 0 || length > kMaximumSidecarBytes) return {};
    input.seekg(0, std::ios::beg);
    std::string bytes(static_cast<size_t>(length), '\0');
    if (length > 0) input.read(bytes.data(), static_cast<std::streamsize>(length));
    if (!input && length > 0) return {};
    if (bytes.empty()) {
        if (decodedSuccessfully) *decodedSuccessfully = true;
        return {};
    }

    if (bytes.size() >= 2) {
        const auto first = static_cast<unsigned char>(bytes[0]);
        const auto second = static_cast<unsigned char>(bytes[1]);
        if ((first == 0xFF && second == 0xFE) || (first == 0xFE && second == 0xFF)) {
            if ((bytes.size() - 2) % 2 != 0) return {};
            const bool littleEndian = first == 0xFF;
            if (encoding) *encoding = littleEndian ? TextFileEncoding::Utf16LittleEndian :
                TextFileEncoding::Utf16BigEndian;
            std::wstring result((bytes.size() - 2) / 2, L'\0');
            for (size_t index = 0; index < result.size(); ++index) {
                const unsigned char a = static_cast<unsigned char>(bytes[2 + index * 2]);
                const unsigned char b = static_cast<unsigned char>(bytes[3 + index * 2]);
                const std::uint16_t codeUnit = littleEndian ?
                    static_cast<std::uint16_t>(a | (static_cast<std::uint16_t>(b) << 8)) :
                    static_cast<std::uint16_t>(b | (static_cast<std::uint16_t>(a) << 8));
                result[index] = static_cast<wchar_t>(codeUnit);
            }
            if (!IsWellFormedUtf16(result)) return {};
            if (decodedSuccessfully) *decodedSuccessfully = true;
            return result;
        }
    }

    bool hadUtf8Bom = false;
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
        hadUtf8Bom = true;
    }
    const std::wstring result = Utf8ToWide(bytes);
    if (!bytes.empty() && result.empty()) return {};
    if (encoding) *encoding = hadUtf8Bom ? TextFileEncoding::Utf8Bom : TextFileEncoding::Utf8;
    if (decodedSuccessfully) *decodedSuccessfully = true;
    return result;
}
bool ExistingTextFileCouldNotBeDecoded(const fs::path& path, bool decodedSuccessfully) {
    if (decodedSuccessfully) return false;
    std::error_code ec;
    return fs::exists(path, ec) && !ec && fs::file_size(path, ec) > 0 && !ec;
}
std::vector<char> EncodeTextFile(const std::wstring& text, TextFileEncoding encoding) {
    std::vector<char> bytes;
    if (encoding == TextFileEncoding::Utf8 || encoding == TextFileEncoding::Utf8Bom) {
        const std::string utf8 = WideToUtf8(text);
        if (!text.empty() && utf8.empty()) return {};
        if (encoding == TextFileEncoding::Utf8Bom) {
            bytes.push_back(static_cast<char>(0xEF));
            bytes.push_back(static_cast<char>(0xBB));
            bytes.push_back(static_cast<char>(0xBF));
        }
        bytes.insert(bytes.end(), utf8.begin(), utf8.end());
        return bytes;
    }

    if (text.size() > (std::numeric_limits<size_t>::max() - 2) / 2) return {};
    bytes.reserve(2 + text.size() * 2);
    const bool littleEndian = encoding == TextFileEncoding::Utf16LittleEndian;
    bytes.push_back(static_cast<char>(littleEndian ? 0xFF : 0xFE));
    bytes.push_back(static_cast<char>(littleEndian ? 0xFE : 0xFF));
    for (wchar_t character : text) {
        const std::uint16_t codeUnit = static_cast<std::uint16_t>(character);
        const unsigned char low = static_cast<unsigned char>(codeUnit & 0xFFu);
        const unsigned char high = static_cast<unsigned char>((codeUnit >> 8) & 0xFFu);
        bytes.push_back(static_cast<char>(littleEndian ? low : high));
        bytes.push_back(static_cast<char>(littleEndian ? high : low));
    }
    return bytes;
}
fs::path SidecarPathFor(const fs::path& image) {
    fs::path sidecar = image;
    if (IsRawExtension(ExtensionLower(image))) {
        sidecar.replace_extension(L".xmp");
    } else {
        sidecar += L".xmp";
    }
    return sidecar;
}
int RatingFromShellValue(ULONG value) {
    if (value == 0) return 0;
    if (value <= 12) return 1;
    if (value <= 37) return 2;
    if (value <= 62) return 3;
    if (value <= 87) return 4;
    return 5;
}
bool WriteXmpProperties(const fs::path& image, std::initializer_list<XmpPropertyUpdate> updates) {
    const fs::path sidecar = SidecarPathFor(image);
    AtomicFileSnapshot expectedSidecar;
    if (!CaptureAtomicFileSnapshot(sidecar, expectedSidecar)) return false;
    TextFileEncoding encoding = TextFileEncoding::Utf8;
    bool decodedSuccessfully = false;
    std::wstring text = ReadTextFile(sidecar, &encoding, &decodedSuccessfully);
    if (ExistingTextFileCouldNotBeDecoded(sidecar, decodedSuccessfully)) return false;

    if (text.empty()) {
        encoding = TextFileEncoding::Utf8;
        text = LR"(<?xpacket begin="﻿" id="W5M0MpCehiHzreSzNTczkc9d"?>
<x:xmpmeta xmlns:x="adobe:ns:meta/" x:xmptk="QuickSift">
  <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
    <rdf:Description rdf:about=""/>
  </rdf:RDF>
</x:xmpmeta>
<?xpacket end="w"?>
)";
    }

    // Existing non-empty XMP is never replaced with a fresh document merely
    // because its layout is unfamiliar. Every requested field must be updated
    // or inserted into an existing rdf:Description before one atomic write.
    for (const XmpPropertyUpdate& update : updates) {
        if (!UpdateXmpSimpleValue(text, update)) return false;
    }
    return WriteTextFile(sidecar, text, encoding, expectedSidecar);
}
std::optional<int> ReadShellRating(const fs::path& image) {
    ComPtr<IPropertyStore> store;
    HRESULT hr = SHGetPropertyStoreFromParsingName(image.c_str(), nullptr, GPS_BESTEFFORT,
        IID_PPV_ARGS(&store));
    if (FAILED(hr)) return std::nullopt;

    PROPVARIANT value;
    PropVariantInit(&value);
    hr = store->GetValue(PKEY_Rating, &value);
    std::optional<int> rating;
    if (SUCCEEDED(hr) && value.vt != VT_EMPTY && value.vt != VT_NULL) {
        ULONG raw = 0;
        if (SUCCEEDED(PropVariantToUInt32(value, &raw))) rating = RatingFromShellValue(raw);
    }
    PropVariantClear(&value);
    return rating;
}
// Both names come from the canonical vocabulary (core/metadata_vocabulary.h).
std::wstring LabelName(int label) {
    return std::wstring(quicksift::core::vocabulary::Color(label).xmpLabel);
}

std::wstring LabelDisplayName(int label) {
    return std::wstring(quicksift::core::vocabulary::Color(label).label);
}

namespace {

const quicksift::metadata::XmpPropertyQuery kRatingQuery{
    L"xmp:Rating", L"xmp", L"http://ns.adobe.com/xap/1.0/" };
const quicksift::metadata::XmpPropertyQuery kLabelQuery{
    L"xmp:Label", L"xmp", L"http://ns.adobe.com/xap/1.0/" };
const quicksift::metadata::XmpPropertyQuery kPickQuery{
    L"quicksift:Pick", L"quicksift", L"https://quicksift.local/ns/1.0/" };
// The star rating kept while a photo is rejected (core/xmp_interop_policy.h).
const quicksift::metadata::XmpPropertyQuery kStoredStarsQuery{
    L"quicksift:Rating", L"quicksift", L"https://quicksift.local/ns/1.0/" };

MetadataFieldValue IntegerFieldFromInspection(
    const quicksift::metadata::XmpSimpleValueInspection& inspected,
    int low, int high, bool absentIsKnown) {
    using quicksift::metadata::XmpSimpleValueState;
    if (inspected.state == XmpSimpleValueState::Absent)
        return absentIsKnown ? KnownMetadataValue(0) : UnknownMetadataValue();
    if (inspected.state == XmpSimpleValueState::Unsupported)
        return UnsupportedMetadataValue(inspected.value);
    if (const auto parsed = quicksift::metadata::ParseBoundedInteger(
        inspected.value, low, high)) return KnownMetadataValue(*parsed);
    return UnsupportedMetadataValue(inspected.value);
}

MetadataFieldValue LabelFieldFromInspection(
    const quicksift::metadata::XmpSimpleValueInspection& inspected,
    bool absentIsKnown) {
    using quicksift::metadata::XmpSimpleValueState;
    if (inspected.state == XmpSimpleValueState::Absent)
        return absentIsKnown ? KnownMetadataValue(0) : UnknownMetadataValue();
    if (inspected.state == XmpSimpleValueState::Unsupported)
        return UnsupportedMetadataValue(inspected.value);
    if (const auto parsed = quicksift::metadata::ParseKnownColorLabel(inspected.value))
        return KnownMetadataValue(*parsed);
    return UnsupportedMetadataValue(inspected.value);
}

PhotoMetadataValues SidecarValuesFromText(const std::wstring& text, bool absentIsKnown) {
    PhotoMetadataValues values;
    if (text.empty()) {
        const MetadataFieldValue empty = absentIsKnown ? KnownMetadataValue(0) : UnknownMetadataValue();
        return { empty, empty, empty };
    }
    namespace interop = quicksift::core::xmp_interop;
    values.rating = IntegerFieldFromInspection(
        quicksift::metadata::InspectXmpSimpleValue(text, kRatingQuery),
        interop::kXmpRatingMinimum, interop::kXmpRatingMaximum, absentIsKnown);
    values.colorLabel = LabelFieldFromInspection(
        quicksift::metadata::InspectXmpSimpleValue(text, kLabelQuery), absentIsKnown);
    values.pickState = IntegerFieldFromInspection(
        quicksift::metadata::InspectXmpSimpleValue(text, kPickQuery), -1, 1, absentIsKnown);
    std::optional<int> storedStars;
    const auto stars = quicksift::metadata::InspectXmpSimpleValue(text, kStoredStarsQuery);
    if (stars.state == quicksift::metadata::XmpSimpleValueState::Value)
        storedStars = quicksift::metadata::ParseBoundedInteger(stars.value, 0, 5);
    interop::DecodeRatingAndPick(values.rating, values.pickState, storedStars);
    quicksift::core::sidecar_write::CompleteQuickSiftSidecar(values);
    return values;
}

PhotoMetadataValues ReadSidecarMetadata(const fs::path& image, bool absentIsKnown) {
    const fs::path sidecar = SidecarPathFor(image);
    TextFileEncoding encoding = TextFileEncoding::Utf8;
    bool decoded = false;
    const std::wstring text = ReadTextFile(sidecar, &encoding, &decoded);
    // One existence probe at most (was up to three per image: the read, then
    // ExistingTextFileCouldNotBeDecoded, then fs::exists). Most photos have no
    // sidecar, so this runs for nearly every file during indexing.
    std::error_code ec;
    const bool exists = (decoded && !text.empty()) || (fs::exists(sidecar, ec) && !ec);
    if (!decoded && exists) {
        std::error_code sizeError;
        if (fs::file_size(sidecar, sizeError) > 0 && !sizeError) {
            return { UnreadableMetadataValue(), UnreadableMetadataValue(),
                UnreadableMetadataValue() };
        }
    }
    if (!exists || text.empty()) {
        const MetadataFieldValue empty = absentIsKnown ? KnownMetadataValue(0) :
            UnknownMetadataValue();
        return { empty, empty, empty };
    }

    return SidecarValuesFromText(text, absentIsKnown);
}

MetadataFieldValue ChooseMetadataField(const MetadataFieldValue& preferred,
    const MetadataFieldValue& fallback, int emptyValue) {
    if (preferred.IsAuthoritative()) return preferred;
    if (fallback.IsAuthoritative()) return fallback;
    if (preferred.knowledge == MetadataKnowledge::Unreadable ||
        fallback.knowledge == MetadataKnowledge::Unreadable)
        return UnreadableMetadataValue();
    (void)emptyValue;
    return UnknownMetadataValue();
}

bool PatchValuesValid(const MetadataPatch& patch) noexcept {
    return (!patch.rating.active || (patch.rating.desired >= 0 && patch.rating.desired <= 5)) &&
        (!patch.colorLabel.active || (patch.colorLabel.desired >= 0 && patch.colorLabel.desired <= 5)) &&
        (!patch.pickState.active || (patch.pickState.desired >= -1 && patch.pickState.desired <= 1));
}

} // namespace

SidecarMetadataWriteResult WritePhotoMetadataPatch(const fs::path& image,
    const MetadataPatch& patch) noexcept {
    SidecarMetadataWriteResult result;
    if (patch.Empty() || !PatchValuesValid(patch)) {
        result.detail = L"The metadata patch contains no supported change.";
        return result;
    }
    try {
        const fs::path sidecar = SidecarPathFor(image);
        AtomicFileSnapshot expectedSidecar;
        if (!CaptureAtomicFileSnapshot(sidecar, expectedSidecar)) {
            result.detail = L"The XMP sidecar identity could not be captured safely.";
            return result;
        }
        TextFileEncoding encoding = TextFileEncoding::Utf8;
        bool decoded = false;
        std::wstring text = ReadTextFile(sidecar, &encoding, &decoded);
        if (ExistingTextFileCouldNotBeDecoded(sidecar, decoded)) {
            result.detail = L"The existing XMP sidecar could not be decoded, so QuickSift preserved it unchanged.";
            return result;
        }
        // Sidecar-first: the text in memory answers "before"; the full multi-source
        // read (Exiv2 parse of the RAW + Shell property store) runs only when the
        // sidecar does not answer every field (core/sidecar_write_policy.h).
        const PhotoMetadataValues sidecarBefore = SidecarValuesFromText(text, false);
        result.before = quicksift::core::sidecar_write::NeedsFullRead(sidecarBefore) ?
            ReadPhotoMetadata(image) : sidecarBefore;
        MetadataFieldId conflict{};
        if (!MetadataExpectedMatches(result.before, patch, &conflict)) {
            result.conflict = true;
            // loc-composed: every full sentence variant is in kDynamicFragments.
            result.detail = std::wstring(L"The file's ") + MetadataFieldDisplayName(conflict) +
                L" changed outside QuickSift, so the metadata update was stopped.";
            return result;
        }
        if (text.empty()) {
            encoding = TextFileEncoding::Utf8;
            text = LR"(<?xpacket begin="﻿" id="W5M0MpCehiHzreSzNTczkc9d"?>
<x:xmpmeta xmlns:x="adobe:ns:meta/" x:xmptk="QuickSift">
  <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
    <rdf:Description rdf:about=""/>
  </rdf:RDF>
</x:xmpmeta>
<?xpacket end="w"?>
)";
        }
        // The edit plus every Known value the sidecar lacks, so the next read and
        // write of this photo are sidecar-only.
        // Rating and pick are written together through the interop mapping
        // (core/xmp_interop_policy.h): xmp:Rating=-1 for a reject (Lightroom and
        // Capture One), quicksift:Pick, and quicksift:Rating keeping the stars.
        bool ratingOrPickPlanned = false;
        PhotoMetadataValues planned = result.before;
        for (const auto& write : quicksift::core::sidecar_write::PlanWrites(sidecarBefore, result.before, patch)) {
            if (write.field == MetadataFieldId::ColorLabel) {
                if (!UpdateXmpSimpleValue(text, { kLabelQuery.qualifiedName, kLabelQuery.namespaceName,
                        kLabelQuery.namespaceUri, LabelName(write.value) })) {
                    result.detail = L"The color-label property could not be updated without rewriting unfamiliar XMP.";
                    return result;
                }
                continue;
            }
            ratingOrPickPlanned = true;
            if (write.field == MetadataFieldId::Rating) planned.rating = KnownMetadataValue(write.value);
            else planned.pickState = KnownMetadataValue(write.value);
        }
        if (ratingOrPickPlanned) {
            namespace interop = quicksift::core::xmp_interop;
            const interop::EncodedRatingPick encoded = interop::EncodeRatingAndPick(
                interop::KnownValue(planned.rating), interop::KnownValue(planned.pickState));
            if (encoded.writeXmpRating && !UpdateXmpSimpleValue(text, { kRatingQuery.qualifiedName,
                    kRatingQuery.namespaceName, kRatingQuery.namespaceUri, std::to_wstring(encoded.xmpRating) })) {
                result.detail = L"The rating property could not be updated without rewriting unfamiliar XMP.";
                return result;
            }
            if (encoded.writePick && !UpdateXmpSimpleValue(text, { kPickQuery.qualifiedName,
                    kPickQuery.namespaceName, kPickQuery.namespaceUri, std::to_wstring(encoded.pick) })) {
                result.detail = L"The pick-state property could not be updated without rewriting unfamiliar XMP.";
                return result;
            }
            if (encoded.writeStoredStars && !UpdateXmpSimpleValue(text, { kStoredStarsQuery.qualifiedName,
                    kStoredStarsQuery.namespaceName, kStoredStarsQuery.namespaceUri,
                    std::to_wstring(encoded.storedStars) })) {
                result.detail = L"The rating property could not be updated without rewriting unfamiliar XMP.";
                return result;
            }
        }
        TextFileWriteStatus writeStatus;
        if (!WriteTextFile(sidecar, text, encoding, expectedSidecar, &writeStatus)) {
            // Only a real destination change is an external conflict (reconcile).
            // A refused handle/rename is a transient lock the write lane retries.
            result.conflict = writeStatus.destinationChanged;
            result.win32Error = writeStatus.win32Error;
            result.lowStorage = writeStatus.lowStorage;
            result.detail = writeStatus.destinationChanged ?
                L"The XMP sidecar changed outside QuickSift, so the metadata update was stopped." :
                (writeStatus.win32Error != 0 ?
                    L"Another app is using the XMP sidecar, so it could not be published yet." :
                    L"The XMP sidecar could not be published atomically.");
            return result;
        }
        // Verify against the sidecar only (the sidecar is the authority for this
        // photo's edits); the full re-read of the RAW is not repeated.
        const PhotoMetadataValues written = ReadSidecarMetadata(image, false);
        const PhotoMetadataValues requested = ApplyMetadataPatch(result.before, patch);
        result.after = requested;
        if ((patch.rating.active && !MetadataFieldEquivalent(written.rating, requested.rating)) ||
            (patch.colorLabel.active && !MetadataFieldEquivalent(written.colorLabel, requested.colorLabel)) ||
            (patch.pickState.active && !MetadataFieldEquivalent(written.pickState, requested.pickState))) {
            result.detail = L"The sidecar was written but the requested metadata could not be verified.";
            return result;
        }
        result.ok = true;
        return result;
    } catch (...) {
        QS_LOG_AND_CONTINUE(L"Metadata", L"WritePhotoMetadataPatch");
        result.detail = L"An unexpected XMP sidecar error occurred.";
        return result;
    }
}


PhotoMetadataValues ReadPhotoMetadataFromStorage(const fs::path& image,
    MetadataStorageKind storage) {
    if (storage == MetadataStorageKind::Embedded) {
        if (const auto embedded = quicksift::ReadEmbeddedMetadata(image)) return *embedded;
        return { UnreadableMetadataValue(), UnreadableMetadataValue(),
            UnreadableMetadataValue() };
    }
    return ReadSidecarMetadata(image, true);
}

PhotoMetadataValues ReadPhotoMetadata(const fs::path& image) {
    // Do not probe both metadata stores for every file. Embedded metadata is the
    // preferred source for formats configured for direct metadata access; for the
    // normal sidecar-first path, the embedded reader is only needed when the
    // sidecar did not answer every field. Exiv2 can be materially slower than a
    // small XMP sidecar read, and its process-wide guard makes unnecessary probes
    // especially expensive on large folders.
    const bool preferEmbedded = UsesDirectMetadata(image);
    PhotoMetadataValues preferred;
    if (preferEmbedded) {
        const auto embeddedOptional = quicksift::ReadEmbeddedMetadata(image);
        preferred = embeddedOptional ? *embeddedOptional :
            PhotoMetadataValues{ UnknownMetadataValue(), UnknownMetadataValue(),
                UnknownMetadataValue() };
    } else {
        preferred = ReadSidecarMetadata(image, false);
    }

    PhotoMetadataValues fallback;
    const bool needsFallback =
        !preferred.rating.IsAuthoritative() ||
        !preferred.colorLabel.IsAuthoritative() ||
        !preferred.pickState.IsAuthoritative();

    if (needsFallback) {
        if (preferEmbedded) {
            fallback = ReadSidecarMetadata(image, false);
        } else {
            const auto embeddedOptional = quicksift::ReadEmbeddedMetadata(image);
            fallback = embeddedOptional ? *embeddedOptional :
                PhotoMetadataValues{ UnknownMetadataValue(), UnknownMetadataValue(),
                    UnknownMetadataValue() };
        }
    }

    PhotoMetadataValues values;
    values.rating = needsFallback ?
        ChooseMetadataField(preferred.rating, fallback.rating, 0) : preferred.rating;
    values.colorLabel = needsFallback ?
        ChooseMetadataField(preferred.colorLabel, fallback.colorLabel, 0) : preferred.colorLabel;
    values.pickState = needsFallback ?
        ChooseMetadataField(preferred.pickState, fallback.pickState, 0) : preferred.pickState;
    if (!values.rating.IsAuthoritative()) {
        if (const auto shellRating = ReadShellRating(image))
            values.rating = KnownMetadataValue(*shellRating);
    }
    // Every source answered "nothing here" (no sidecar, no XMP/EXIF rating that
    // Exiv2 could read, no shell rating). For a file that opens, that is an unset
    // value (0 stars), not "still unknown": leaving it Unknown made the photo
    // display as unrated but never match "0 stars"/"unrated" or the Unrated filter.
    // A file that cannot be opened becomes Unreadable (terminal).
    if (values.rating.knowledge == MetadataKnowledge::Unknown ||
        values.colorLabel.knowledge == MetadataKnowledge::Unknown ||
        values.pickState.knowledge == MetadataKnowledge::Unknown) {
        HANDLE probe = CreateFileW(image.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        const bool opens = probe != INVALID_HANDLE_VALUE;
        if (opens) CloseHandle(probe);
        // Terminal either way: a file that cannot be opened is Unreadable, not
        // Unknown (Unknown kept "Rating pending" and the filters waiting forever).
        ResolveMetadataReadOutcome(values, opens);
    }
    return values;
}

std::wstring ReadPropertyDisplay(IPropertyStore* store, const wchar_t* canonicalName) {
    if (!store || !canonicalName) return {};
    PROPERTYKEY key{};
    if (FAILED(PSGetPropertyKeyFromName(canonicalName, &key))) return {};

    PROPVARIANT value;
    PropVariantInit(&value);
    if (FAILED(store->GetValue(key, &value)) || value.vt == VT_EMPTY || value.vt == VT_NULL) {
        PropVariantClear(&value);
        return {};
    }

    PWSTR display = nullptr;
    std::wstring result;
    if (SUCCEEDED(PSFormatForDisplayAlloc(key, value, PDFF_DEFAULT, &display)) && display) {
        result = display;
        CoTaskMemFree(display);
    } else if (SUCCEEDED(PropVariantToStringAlloc(value, &display)) && display) {
        result = display;
        CoTaskMemFree(display);
    }
    PropVariantClear(&value);
    constexpr size_t maximumPropertyCharacters = 8192;
    if (result.size() > maximumPropertyCharacters) {
        result.resize(maximumPropertyCharacters - 16);
        result += L" [truncated]";
    }
    return result;
}
std::wstring BuildExifText(const fs::path& path) {
    ComPtr<IPropertyStore> store;
    SHGetPropertyStoreFromParsingName(path.c_str(), nullptr, GPS_BESTEFFORT, IID_PPV_ARGS(&store));

    std::vector<std::pair<std::wstring, std::wstring>> fields;
    auto add = [&](const wchar_t* label, const wchar_t* canonical) {
        const std::wstring value = ReadPropertyDisplay(store.Get(), canonical);
        if (!value.empty()) fields.emplace_back(label, value);
    };

    add(L"Taken", L"System.Photo.DateTaken");
    add(L"Dimensions", L"System.Image.Dimensions");
    if (fields.empty() || fields.back().first != L"Dimensions") {
        const std::wstring width = ReadPropertyDisplay(store.Get(), L"System.Image.HorizontalSize");
        const std::wstring height = ReadPropertyDisplay(store.Get(), L"System.Image.VerticalSize");
        if (!width.empty() && !height.empty()) fields.emplace_back(L"Dimensions", width + L" × " + height);
    }
    add(L"Camera maker", L"System.Photo.CameraManufacturer");
    add(L"Camera", L"System.Photo.CameraModel");
    add(L"Lens", L"System.Photo.LensModel");
    add(L"Exposure", L"System.Photo.ExposureTime");
    add(L"Aperture", L"System.Photo.FNumber");
    add(L"ISO", L"System.Photo.ISOSpeed");
    add(L"Focal length", L"System.Photo.FocalLength");
    add(L"Exposure bias", L"System.Photo.ExposureBias");
    add(L"Flash", L"System.Photo.Flash");
    add(L"White balance", L"System.Photo.WhiteBalance");
    add(L"Bit depth", L"System.Image.BitDepth");
    add(L"Color space", L"System.Image.ColorSpace");

    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    if (!ec) fields.emplace_back(L"File size", FormatFileSize(size));
    fields.emplace_back(L"Format", ExtensionLower(path).empty() ? L"Unknown" : ToLower(path.extension().wstring()));

    std::wostringstream out;
    out << path.filename().wstring();
    for (const auto& [label, value] : fields) {
        out << L"\r\n" << label << L":  " << value;
    }
    if (fields.size() <= 2) out << L"\r\n\r\nNo additional EXIF fields were exposed by the installed codec.";
    std::wstring result = out.str();
    constexpr size_t maximumExifCharacters = 65'536;
    if (result.size() > maximumExifCharacters) {
        result.resize(maximumExifCharacters - 32);
        result += L"\r\n[EXIF text truncated]";
    }
    return result;
}
std::optional<fs::path> PickFolder(HWND owner, const wchar_t* title) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog)))) return std::nullopt;

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(title);
    if (FAILED(dialog->Show(owner))) return std::nullopt;

    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return std::nullopt;
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return std::nullopt;
    fs::path result(path);
    CoTaskMemFree(path);
    return result;
}

ExifHeaderFacts ReadExifHeaderFacts(const fs::path& image, bool wantCapture, bool wantCamera) {
    namespace exif = quicksift::core::exif;
    ExifHeaderFacts facts;
    HANDLE file = CreateFileW(image.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return facts;
    constexpr DWORD kFirst = 64u * 1024u;
    thread_local std::vector<std::uint8_t> buffer; // reused: no 512 KB allocation per file
    buffer.resize(exif::kCaptureTimeReadBytes);
    DWORD read = 0;
    if (!ReadFile(file, buffer.data(), kFirst, &read, nullptr) || read == 0) { CloseHandle(file); return facts; }
    facts.opened = true;
    auto evaluate = [&](std::size_t size) {
        const std::span<const std::uint8_t> bytes(buffer.data(), size);
        if (wantCapture) {
            auto found = exif::FindCaptureTime(bytes);
            if (found && (!facts.capture || found->tag > facts.capture->tag)) facts.capture = found;
        }
        if (wantCamera && !facts.camera.known) facts.camera = exif::FindCameraSettings(bytes);
    };
    evaluate(read);
    const bool missing = (wantCapture && (!facts.capture || facts.capture->tag != exif::CaptureTag::Original)) ||
        (wantCamera && !facts.camera.known);
    if (missing && read == kFirst) {
        DWORD more = 0;
        if (ReadFile(file, buffer.data() + read, static_cast<DWORD>(buffer.size() - read), &more, nullptr) && more > 0)
            evaluate(static_cast<std::size_t>(read) + more);
    }
    CloseHandle(file);
    return facts;
}

} // namespace quicksift::app
