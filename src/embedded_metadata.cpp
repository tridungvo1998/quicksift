// CODE GUIDE: See CODE_GUIDE.md -> "Writing metadata".
// OWNER: Embedded metadata implementation; preserve temporary-copy verification for source media.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "embedded_metadata.h"
#include "metadata/metadata_value_parser.h"
#include "platform/locked_file_publication_windows.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#if defined(QS_USE_EXIV2)
#include <exiv2/exiv2.hpp>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace quicksift {

namespace {

struct EmbeddedMetadataGate {
    std::mutex mutex;
    std::condition_variable cv;
    int holders = 0;
    int writeInterest = 0;
};

EmbeddedMetadataGate& MetadataGate() {
    static EmbeddedMetadataGate gate;
    return gate;
}

void AcquireEmbeddedMetadataAccess(bool write) {
    EmbeddedMetadataGate& gate = MetadataGate();
    std::unique_lock lock(gate.mutex);
    if (write) ++gate.writeInterest;
    gate.cv.wait(lock, [&] {
        if (gate.holders != 0) return false;
        return write || gate.writeInterest == 0;
    });
    ++gate.holders;
}

void ReleaseEmbeddedMetadataAccess(bool write) noexcept {
    try {
        EmbeddedMetadataGate& gate = MetadataGate();
        std::lock_guard lock(gate.mutex);
        if (gate.holders > 0) --gate.holders;
        if (write && gate.writeInterest > 0) --gate.writeInterest;
        gate.cv.notify_all();
    } catch (...) {
    }
}

class EmbeddedMetadataAccessLease {
public:
    explicit EmbeddedMetadataAccessLease(bool write) : write_(write) {
        AcquireEmbeddedMetadataAccess(write_);
    }
    ~EmbeddedMetadataAccessLease() { ReleaseEmbeddedMetadataAccess(write_); }
    EmbeddedMetadataAccessLease(const EmbeddedMetadataAccessLease&) = delete;
    EmbeddedMetadataAccessLease& operator=(const EmbeddedMetadataAccessLease&) = delete;
private:
    bool write_;
};

} // namespace

void PreferEmbeddedMetadataWrites() noexcept {
    try {
        EmbeddedMetadataGate& gate = MetadataGate();
        std::lock_guard lock(gate.mutex);
        ++gate.writeInterest;
        gate.cv.notify_all();
    } catch (...) {
    }
}

void ReleaseEmbeddedMetadataWritePreference() noexcept {
    try {
        EmbeddedMetadataGate& gate = MetadataGate();
        std::lock_guard lock(gate.mutex);
        if (gate.writeInterest > 0) --gate.writeInterest;
        gate.cv.notify_all();
    } catch (...) {
    }
}

bool EmbeddedMetadataWritePreferred() noexcept {
    try {
        EmbeddedMetadataGate& gate = MetadataGate();
        std::lock_guard lock(gate.mutex);
        return gate.writeInterest > 0;
    } catch (...) {
        return true;
    }
}

#if defined(QS_USE_EXIV2)
namespace {

constexpr std::uint64_t kMetadataSafetyReserve = 64ull * 1024ull * 1024ull;
constexpr std::uint64_t kMetadataGrowthAllowance = 8ull * 1024ull * 1024ull;

std::string Utf8Path(const fs::path& path) {
    const std::u8string value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

Exiv2::Image::UniquePtr OpenImage(const fs::path& path) {
#ifdef EXV_UNICODE_PATH
    return Exiv2::ImageFactory::open(path.wstring(), false);
#else
    return Exiv2::ImageFactory::open(Utf8Path(path), false);
#endif
}

std::wstring WideFromUtf8(const char* value) {
    if (!value || !*value) return {};
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (required <= 1) return {};
    std::wstring result(static_cast<size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
        result.data(), required) != required) return {};
    result.resize(static_cast<size_t>(required - 1));
    return result;
}

void RegisterQuickSiftNamespace() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (!Exiv2::XmpParser::initialize()) {
            throw std::runtime_error("Exiv2 XMP initialization failed");
        }
        std::atexit(Exiv2::XmpParser::terminate);
        Exiv2::XmpProperties::registerNs("https://quicksift.local/ns/1.0/", "quicksift");
    });
}

const char* LabelName(int label) {
    static constexpr const char* names[] = { "", "Red", "Yellow", "Green", "Blue", "Purple" };
    return names[std::clamp(label, 0, 5)];
}

quicksift::core::MetadataFieldValue ReadIntegerField(const Exiv2::XmpData& data,
    const char* key, int low, int high) {
    const auto it = data.findKey(Exiv2::XmpKey(key));
    if (it == data.end()) return quicksift::core::KnownMetadataValue(0);
    const std::string raw = it->toString();
    if (const auto parsed = metadata::ParseBoundedInteger(raw, low, high))
        return quicksift::core::KnownMetadataValue(*parsed);
    return quicksift::core::UnsupportedMetadataValue(WideFromUtf8(raw.c_str()));
}

quicksift::core::MetadataFieldValue ReadLabelField(const Exiv2::XmpData& data) {
    const auto it = data.findKey(Exiv2::XmpKey("Xmp.xmp.Label"));
    if (it == data.end()) return quicksift::core::KnownMetadataValue(0);
    const std::string raw = it->toString();
    if (const auto parsed = metadata::ParseKnownColorLabel(raw))
        return quicksift::core::KnownMetadataValue(*parsed);
    return quicksift::core::UnsupportedMetadataValue(WideFromUtf8(raw.c_str()));
}

quicksift::core::PhotoMetadataValues ReadMetadataValues(const Exiv2::XmpData& xmp) {
    quicksift::core::PhotoMetadataValues result;
    result.rating = ReadIntegerField(xmp, "Xmp.xmp.Rating", 0, 5);
    result.colorLabel = ReadLabelField(xmp);
    result.pickState = ReadIntegerField(xmp, "Xmp.quicksift.Pick", -1, 1);
    return result;
}

bool MetadataPatchValuesValid(const quicksift::core::MetadataPatch& patch) noexcept {
    return (!patch.rating.active || (patch.rating.desired >= 0 && patch.rating.desired <= 5)) &&
        (!patch.colorLabel.active || (patch.colorLabel.desired >= 0 && patch.colorLabel.desired <= 5)) &&
        (!patch.pickState.active || (patch.pickState.desired >= -1 && patch.pickState.desired <= 1));
}

void ApplyMetadataPatch(Exiv2::XmpData& xmp, const quicksift::core::MetadataPatch& patch) {
    if (patch.rating.active)
        xmp["Xmp.xmp.Rating"].setValue(std::to_string(patch.rating.desired));
    if (patch.colorLabel.active)
        xmp["Xmp.xmp.Label"].setValue(LabelName(patch.colorLabel.desired));
    if (patch.pickState.active)
        xmp["Xmp.quicksift.Pick"].setValue(std::to_string(patch.pickState.desired));
}

bool PatchWasApplied(const quicksift::core::PhotoMetadataValues& values,
    const quicksift::core::MetadataPatch& patch) noexcept {
    return (!patch.rating.active ||
            (values.rating.IsKnown() && values.rating.value == patch.rating.desired)) &&
        (!patch.colorLabel.active ||
            (values.colorLabel.IsKnown() && values.colorLabel.value == patch.colorLabel.desired)) &&
        (!patch.pickState.active ||
            (values.pickState.IsKnown() && values.pickState.value == patch.pickState.desired));
}

struct ContainerSignature {
    Exiv2::ImageType type = Exiv2::ImageType::none;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string mimeType;
};

std::optional<ContainerSignature> ReadContainerSignature(const fs::path& path) {
    RegisterQuickSiftNamespace();
    auto image = OpenImage(path);
    if (!image || !image->good()) return std::nullopt;
    image->readMetadata();
    return ContainerSignature{ image->imageType(), image->pixelWidth(), image->pixelHeight(), image->mimeType() };
}

bool SameContainerSignature(const ContainerSignature& left, const ContainerSignature& right) {
    return left.type == right.type && left.width == right.width && left.height == right.height &&
        left.mimeType == right.mimeType;
}

std::optional<EmbeddedMetadataValues> ReadFromImage(const fs::path& path) {
    RegisterQuickSiftNamespace();
    auto image = OpenImage(path);
    if (!image || !image->good()) return std::nullopt;
    image->readMetadata();
    const Exiv2::XmpData& xmp = image->xmpData();

    return ReadMetadataValues(xmp);
}

bool FlushPath(const fs::path& path) {
    HANDLE handle = CreateFileW(path.wstring().c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const bool ok = FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
    return ok;
}

struct FileIdentity {
    std::uintmax_t size = 0;
    FILETIME lastWrite{};
    std::int64_t changeTime = 0;
    DWORD volumeSerial = 0;
    DWORD fileIndexHigh = 0;
    DWORD fileIndexLow = 0;
};

std::uint64_t SaturatingAdd64(std::uint64_t left, std::uint64_t right) noexcept {
    return right > std::numeric_limits<std::uint64_t>::max() - left ?
        std::numeric_limits<std::uint64_t>::max() : left + right;
}

class ScopedWin32Handle {
public:
    explicit ScopedWin32Handle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept : handle_(handle) {}
    ~ScopedWin32Handle() { reset(); }
    ScopedWin32Handle(const ScopedWin32Handle&) = delete;
    ScopedWin32Handle& operator=(const ScopedWin32Handle&) = delete;
    ScopedWin32Handle(ScopedWin32Handle&& other) noexcept : handle_(other.release()) {}
    ScopedWin32Handle& operator=(ScopedWin32Handle&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }
    HANDLE get() const noexcept { return handle_; }
    HANDLE release() noexcept {
        const HANDLE value = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return value;
    }
    void reset(HANDLE handle = INVALID_HANDLE_VALUE) noexcept {
        if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr) CloseHandle(handle_);
        handle_ = handle;
    }
private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

bool FingerprintHandle(HANDLE handle, FileIdentity& identity) {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    FILE_BASIC_INFO basic{};
    const bool infoOk = GetFileInformationByHandle(handle, &info) != FALSE;
    const bool basicOk = GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) != FALSE;
    if (!infoOk || !basicOk ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return false;
    identity.size = (static_cast<std::uintmax_t>(info.nFileSizeHigh) << 32) |
        static_cast<std::uintmax_t>(info.nFileSizeLow);
    identity.lastWrite = info.ftLastWriteTime;
    identity.changeTime = basic.ChangeTime.QuadPart;
    identity.volumeSerial = info.dwVolumeSerialNumber;
    identity.fileIndexHigh = info.nFileIndexHigh;
    identity.fileIndexLow = info.nFileIndexLow;
    return true;
}

bool Fingerprint(const fs::path& path, FileIdentity& identity) {
    ScopedWin32Handle handle(CreateFileW(path.wstring().c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    return FingerprintHandle(handle.get(), identity);
}

bool SameIdentity(const FileIdentity& left, const FileIdentity& right) {
    return left.size == right.size && CompareFileTime(&left.lastWrite, &right.lastWrite) == 0 &&
        left.changeTime == right.changeTime && left.volumeSerial == right.volumeSerial &&
        left.fileIndexHigh == right.fileIndexHigh && left.fileIndexLow == right.fileIndexLow;
}

bool FilesAreByteIdentical(const fs::path& left, const fs::path& right) {
    std::error_code ec;
    const std::uintmax_t leftSize = fs::file_size(left, ec);
    if (ec) return false;
    const std::uintmax_t rightSize = fs::file_size(right, ec);
    if (ec || leftSize != rightSize) return false;

    std::ifstream a(left, std::ios::binary);
    std::ifstream b(right, std::ios::binary);
    if (!a || !b) return false;
    constexpr std::size_t kBufferSize = 1024 * 1024;
    std::vector<char> aBuffer(kBufferSize);
    std::vector<char> bBuffer(kBufferSize);
    while (a && b) {
        a.read(aBuffer.data(), static_cast<std::streamsize>(aBuffer.size()));
        b.read(bBuffer.data(), static_cast<std::streamsize>(bBuffer.size()));
        const std::streamsize aCount = a.gcount();
        const std::streamsize bCount = b.gcount();
        if (aCount != bCount) return false;
        if (aCount == 0) break;
        if (!std::equal(aBuffer.begin(), aBuffer.begin() + static_cast<std::size_t>(aCount), bBuffer.begin())) return false;
    }
    return a.eof() && b.eof();
}

std::optional<std::uint64_t> FreeBytes(const fs::path& path) {
    fs::path probe = path;
    std::error_code ec;
    if (!fs::is_directory(probe, ec)) probe = probe.parent_path();
    ec.clear();
    while (!probe.empty() && !fs::exists(probe, ec)) {
        probe = probe.parent_path();
        ec.clear();
    }
    if (probe.empty()) return std::nullopt;
    ULARGE_INTEGER available{};
    if (!GetDiskFreeSpaceExW(probe.wstring().c_str(), &available, nullptr, nullptr)) return std::nullopt;
    return available.QuadPart;
}

fs::path CreatePrivateTemporaryCopy(const fs::path& image, HANDLE sourceHandle) {
    static std::atomic<std::uint64_t> sequence{ 0 };
    constexpr DWORD kBufferSize = 1024u * 1024u;
    std::vector<unsigned char> buffer(kBufferSize);
    for (int attempt = 0; attempt < 64; ++attempt) {
        fs::path candidate = image;
        candidate += L".quicksift.metadata." + std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed)) + L".tmp";
        ScopedWin32Handle destination(CreateFileW(candidate.c_str(),
            GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY, nullptr));
        if (destination.get() == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) continue;
            return {};
        }
        LARGE_INTEGER begin{};
        if (!SetFilePointerEx(sourceHandle, begin, nullptr, FILE_BEGIN)) {
            destination.reset();
            std::error_code cleanup;
            fs::remove(candidate, cleanup);
            return {};
        }
        bool copied = true;
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(sourceHandle, buffer.data(), kBufferSize, &read, nullptr)) {
                copied = false;
                break;
            }
            if (read == 0) break;
            DWORD offset = 0;
            while (offset < read) {
                DWORD written = 0;
                if (!WriteFile(destination.get(), buffer.data() + offset,
                    read - offset, &written, nullptr) || written == 0) {
                    copied = false;
                    break;
                }
                offset += written;
            }
            if (!copied) break;
        }
        if (!copied || !FlushFileBuffers(destination.get())) {
            destination.reset();
            std::error_code cleanup;
            fs::remove(candidate, cleanup);
            return {};
        }
        return candidate;
    }
    return {};
}

bool FilesAreByteIdentical(HANDLE sourceHandle, const fs::path& right) {
    FileIdentity sourceIdentity{};
    if (!FingerprintHandle(sourceHandle, sourceIdentity)) return false;
    ScopedWin32Handle rightHandle(CreateFileW(right.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    FileIdentity rightIdentity{};
    if (!FingerprintHandle(rightHandle.get(), rightIdentity) ||
        sourceIdentity.size != rightIdentity.size) return false;
    constexpr DWORD kBufferSize = 1024u * 1024u;
    std::vector<unsigned char> left(kBufferSize);
    std::vector<unsigned char> rightBytes(kBufferSize);
    LARGE_INTEGER begin{};
    if (!SetFilePointerEx(sourceHandle, begin, nullptr, FILE_BEGIN) ||
        !SetFilePointerEx(rightHandle.get(), begin, nullptr, FILE_BEGIN)) return false;
    for (;;) {
        DWORD leftRead = 0;
        DWORD rightRead = 0;
        if (!ReadFile(sourceHandle, left.data(), kBufferSize, &leftRead, nullptr) ||
            !ReadFile(rightHandle.get(), rightBytes.data(), kBufferSize, &rightRead, nullptr) ||
            leftRead != rightRead) return false;
        if (leftRead == 0) return true;
        if (!std::equal(left.begin(), left.begin() + leftRead, rightBytes.begin())) return false;
    }
}

std::optional<std::uint64_t> PixelEssenceHash(const fs::path& path) {
    using Microsoft::WRL::ComPtr;
    const HRESULT initialized = CoInitializeEx(nullptr,
        COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninitialize = initialized == S_OK || initialized == S_FALSE;
    struct ComCleanup {
        bool active = false;
        ~ComCleanup() { if (active) CoUninitialize(); }
    } cleanup{ uninitialize };
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return std::nullopt;

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return std::nullopt;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder))) return std::nullopt;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return std::nullopt;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        return std::nullopt;
    }
    UINT width = 0;
    UINT height = 0;
    if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0 ||
        width > std::numeric_limits<UINT>::max() / 4u ||
        width > static_cast<UINT>(std::numeric_limits<INT>::max()) ||
        height > static_cast<UINT>(std::numeric_limits<INT>::max())) return std::nullopt;
    const UINT stride = width * 4u;
    std::vector<unsigned char> row(stride);
    std::uint64_t hash = 1469598103934665603ull;
    auto hashBytes = [&](const void* data, std::size_t count) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0; index < count; ++index) {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
    };
    hashBytes(&width, sizeof(width));
    hashBytes(&height, sizeof(height));
    for (UINT y = 0; y < height; ++y) {
        WICRect rectangle{ 0, static_cast<INT>(y), static_cast<INT>(width), 1 };
        if (FAILED(converter->CopyPixels(&rectangle, stride, stride, row.data()))) {
            return std::nullopt;
        }
        hashBytes(row.data(), row.size());
    }
    return hash;
}

fs::path UniquePrivateMetadataPath(const fs::path& image, std::wstring_view role) {
    static std::atomic<std::uint64_t> sequence{ 0 };
    for (int attempt = 0; attempt < 64; ++attempt) {
        fs::path candidate = image;
        candidate += L".quicksift.metadata.";
        candidate += role;
        candidate += L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
        if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES &&
            GetLastError() == ERROR_FILE_NOT_FOUND) return candidate;
    }
    return {};
}

class TemporaryFileGuard {
public:
    explicit TemporaryFileGuard(fs::path path) : path_(std::move(path)) {}
    ~TemporaryFileGuard() {
        if (path_.empty()) return;
        std::error_code ec;
        fs::remove(path_, ec);
    }
    TemporaryFileGuard(const TemporaryFileGuard&) = delete;
    TemporaryFileGuard& operator=(const TemporaryFileGuard&) = delete;
    void release() noexcept { path_.clear(); }
private:
    fs::path path_;
};

EmbeddedMetadataWriteResult Failure(EmbeddedMetadataFailure failure, std::wstring detail,
    std::uint64_t required = 0, std::uint64_t available = 0) {
    EmbeddedMetadataWriteResult result;
    result.failure = failure;
    result.requiredBytes = required;
    result.availableBytes = available;
    result.detail = std::move(detail);
    return result;
}

} // namespace

std::optional<EmbeddedMetadataValues> ReadEmbeddedMetadata(const fs::path& image) noexcept {
    try {
        EmbeddedMetadataAccessLease exiv2Lease(false);
        return ReadFromImage(image);
    } catch (...) {
        return std::nullopt;
    }
}

EmbeddedMetadataWriteResult WriteEmbeddedMetadataAtomic(const fs::path& image,
    const quicksift::core::MetadataPatch& patch) noexcept {
    if (patch.Empty() || !MetadataPatchValuesValid(patch)) {
        return Failure(EmbeddedMetadataFailure::InvalidValue,
            L"The requested rating, label, or pick state is outside QuickSift's supported range.");
    }
    try {
        EmbeddedMetadataAccessLease exiv2Lease(true);

        // Keep the exact source object readable and writer-locked throughout
        // preparation. Delete sharing permits a later identity-checked upgrade
        // to the handle that performs recoverable publication.
        ScopedWin32Handle sourceGuard(CreateFileW(image.c_str(),
            GENERIC_READ | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        FileIdentity originalIdentity{};
        if (!FingerprintHandle(sourceGuard.get(), originalIdentity)) {
            return Failure(EmbeddedMetadataFailure::UnsafePath,
                L"The image is missing, inaccessible, symbolic, or not a normal file.");
        }
        const DWORD originalAttributes = GetFileAttributesW(image.c_str());
        if (originalAttributes == INVALID_FILE_ATTRIBUTES ||
            (originalAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
            return Failure(EmbeddedMetadataFailure::UnsafePath,
                L"The image is read-only or its file attributes could not be verified.");
        }
        const std::uint64_t required = SaturatingAdd64(
            static_cast<std::uint64_t>(originalIdentity.size), kMetadataGrowthAllowance);
        const std::uint64_t requiredWithReserve = SaturatingAdd64(required, kMetadataSafetyReserve);
        if (const auto available = FreeBytes(image); available && *available < requiredWithReserve) {
            return Failure(EmbeddedMetadataFailure::LowStorage,
                L"There is not enough free space for QuickSift's verified temporary image copy.",
                requiredWithReserve, *available);
        }

        const fs::path temporary = CreatePrivateTemporaryCopy(image, sourceGuard.get());
        if (temporary.empty()) {
            return Failure(EmbeddedMetadataFailure::WriteFailed,
                L"QuickSift could not create a private temporary image copy beside the original.");
        }
        TemporaryFileGuard cleanup(temporary);
        if (!FilesAreByteIdentical(sourceGuard.get(), temporary)) {
            return Failure(EmbeddedMetadataFailure::VerificationFailed,
                L"QuickSift could not verify the temporary image as byte-identical to the locked original object.");
        }
        // Establish the immutable image baseline from the verified private copy,
        // not by reopening the source pathname while it still allows rename.
        const auto originalSignature = ReadContainerSignature(temporary);
        const auto originalEssence = PixelEssenceHash(temporary);
        if (!originalSignature || !originalEssence) {
            return Failure(EmbeddedMetadataFailure::UnsupportedFormat,
                L"This image could not be decoded strongly enough to verify unchanged pixel content.");
        }

        quicksift::core::PhotoMetadataValues beforeValues;
        bool beforeValuesValid = false;
        try {
            RegisterQuickSiftNamespace();
            auto copy = OpenImage(temporary);
            if (!copy || !copy->good()) {
                return Failure(EmbeddedMetadataFailure::UnsupportedFormat,
                    L"This image format does not expose a writable embedded XMP container.");
            }
            const Exiv2::AccessMode xmpMode = copy->checkMode(Exiv2::mdXmp);
            if ((static_cast<int>(xmpMode) & static_cast<int>(Exiv2::amWrite)) == 0) {
                return Failure(EmbeddedMetadataFailure::UnsupportedFormat,
                    L"This image format reports that embedded XMP is not writable.");
            }
            copy->readMetadata();
            Exiv2::XmpData& xmp = copy->xmpData();
            const quicksift::core::PhotoMetadataValues current = ReadMetadataValues(xmp);
            beforeValues = current;
            beforeValuesValid = true;
            quicksift::core::MetadataFieldId conflict{};
            if (!quicksift::core::MetadataExpectedMatches(current, patch, &conflict)) {
                return Failure(EmbeddedMetadataFailure::SourceChanged,
                    std::wstring(L"The image's ") +
                    quicksift::core::MetadataFieldDisplayName(conflict) +
                    L" changed outside QuickSift, so the metadata update was stopped.");
            }
            ApplyMetadataPatch(xmp, patch);
            copy->writeMetadata();
        } catch (const Exiv2::Error& error) {
            return Failure(EmbeddedMetadataFailure::UnsupportedFormat,
                L"This file's metadata container could not be updated safely: " + WideFromUtf8(error.what()));
        } catch (...) {
            return Failure(EmbeddedMetadataFailure::WriteFailed,
                L"The metadata library could not update the temporary image copy.");
        }

        if (!FlushPath(temporary)) {
            return Failure(EmbeddedMetadataFailure::WriteFailed,
                L"Windows could not flush the updated temporary image copy to storage.");
        }
        const auto updatedSignature = ReadContainerSignature(temporary);
        const auto updatedEssence = PixelEssenceHash(temporary);
        const auto verification = ReadFromImage(temporary);
        if (!updatedSignature || !SameContainerSignature(*originalSignature, *updatedSignature) ||
            !updatedEssence || *updatedEssence != *originalEssence) {
            return Failure(EmbeddedMetadataFailure::VerificationFailed,
                L"The temporary image's decoded pixel essence changed during metadata writing.");
        }
        if (!verification || !PatchWasApplied(*verification, patch)) {
            return Failure(EmbeddedMetadataFailure::VerificationFailed,
                L"The temporary image did not contain the requested metadata after writing.");
        }

        // Acquire exact-object publication handles only after Exiv2/WIC have
        // released path-based readers. Both handles deny writers and renamers;
        // publication and rollback are therefore bound to the verified objects,
        // not to mutable pathnames.
        ScopedWin32Handle temporaryPublicationGuard(CreateFileW(temporary.c_str(),
            GENERIC_READ | FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        FileIdentity temporaryIdentity{};
        if (!FingerprintHandle(temporaryPublicationGuard.get(), temporaryIdentity)) {
            return Failure(EmbeddedMetadataFailure::WriteFailed,
                L"The verified temporary image could not be locked for publication.");
        }

        sourceGuard.reset();
        ScopedWin32Handle publicationGuard(CreateFileW(image.c_str(),
            GENERIC_READ | FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        FileIdentity currentIdentity{};
        if (!FingerprintHandle(publicationGuard.get(), currentIdentity) ||
            !SameIdentity(currentIdentity, originalIdentity)) {
            return Failure(EmbeddedMetadataFailure::SourceChanged,
                L"The original pathname stopped identifying the source while metadata was being prepared.");
        }

        const fs::path backup = UniquePrivateMetadataPath(image, L"backup");
        const fs::path failed = UniquePrivateMetadataPath(image, L"failed");
        if (backup.empty() || failed.empty()) {
            return Failure(EmbeddedMetadataFailure::WriteFailed,
                L"QuickSift could not reserve private recovery names beside the image.");
        }
        quicksift::platform::LockedRenameRequest moveOriginalToBackup(backup);
        quicksift::platform::LockedRenameRequest publishTemporary(image);
        quicksift::platform::LockedRenameRequest restoreOriginal(image);
        quicksift::platform::LockedRenameRequest quarantineFailed(failed);
        if (!moveOriginalToBackup.Valid() || !publishTemporary.Valid() ||
            !restoreOriginal.Valid() || !quarantineFailed.Valid()) {
            return Failure(EmbeddedMetadataFailure::WriteFailed,
                L"The image path is too long for a recoverable handle-bound publication.");
        }

        if (!moveOriginalToBackup.Apply(publicationGuard.get())) {
            return Failure(EmbeddedMetadataFailure::WriteFailed,
                L"Windows could not move the locked original to a private recovery path.");
        }
        if (!publishTemporary.Apply(temporaryPublicationGuard.get())) {
            const bool restored = restoreOriginal.Apply(publicationGuard.get());
            return Failure(EmbeddedMetadataFailure::WriteFailed, restored ?
                std::wstring(L"Windows could not publish the verified metadata copy; the original was restored.") :
                std::wstring(L"Windows could not publish the verified metadata copy or restore its pathname. The unchanged original remains at: ") + backup.wstring());
        }
        cleanup.release();

        FileIdentity exactPublishedIdentity{};
        ScopedWin32Handle finalPathGuard(CreateFileW(image.c_str(),
            GENERIC_READ | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        FileIdentity pathIdentity{};
        const bool exactFingerprintValid =
            FingerprintHandle(temporaryPublicationGuard.get(), exactPublishedIdentity);
        const bool exactObjectPreserved = exactFingerprintValid &&
            exactPublishedIdentity.size == temporaryIdentity.size &&
            CompareFileTime(&exactPublishedIdentity.lastWrite,
                &temporaryIdentity.lastWrite) == 0 &&
            exactPublishedIdentity.volumeSerial == temporaryIdentity.volumeSerial &&
            exactPublishedIdentity.fileIndexHigh == temporaryIdentity.fileIndexHigh &&
            exactPublishedIdentity.fileIndexLow == temporaryIdentity.fileIndexLow;
        const bool verified = exactObjectPreserved &&
            FingerprintHandle(finalPathGuard.get(), pathIdentity) &&
            SameIdentity(exactPublishedIdentity, pathIdentity);
        if (!verified) {
            finalPathGuard.reset();
            bool canonicalPathCleared =
                quarantineFailed.Apply(temporaryPublicationGuard.get());
            if (canonicalPathCleared) {
                static_cast<void>(quicksift::platform::MarkLockedFileForDeletion(
                    temporaryPublicationGuard.get()));
            } else if (quicksift::platform::MarkLockedFileForDeletion(
                temporaryPublicationGuard.get())) {
                temporaryPublicationGuard.reset();
                canonicalPathCleared = true;
            }
            const bool restored = canonicalPathCleared &&
                restoreOriginal.Apply(publicationGuard.get());
            return Failure(EmbeddedMetadataFailure::VerificationFailed, restored ?
                std::wstring(L"The final published image could not be bound to the verified object, so QuickSift restored the original.") :
                std::wstring(L"The final image binding failed verification. The unchanged original remains at: ") + backup.wstring());
        }

        const bool retiredBackup =
            quicksift::platform::MarkLockedFileForDeletion(publicationGuard.get());
        EmbeddedMetadataWriteResult result;
        result.ok = true;
        result.failure = EmbeddedMetadataFailure::None;
        if (beforeValuesValid) result.before = beforeValues;
        if (verification) result.after = *verification;
        if (!retiredBackup) {
            result.detail = L"The update is verified, but QuickSift retained the unchanged private backup at: " +
                backup.wstring();
        }
        return result;
    } catch (...) {
        return Failure(EmbeddedMetadataFailure::WriteFailed,
            L"An unexpected metadata error occurred. QuickSift retained or restored the original image.");
    }
}

EmbeddedMetadataWriteResult WriteEmbeddedMetadataInPlace(const fs::path& image,
    const quicksift::core::MetadataPatch& patch) noexcept {
    if (patch.Empty() || !MetadataPatchValuesValid(patch)) {
        return Failure(EmbeddedMetadataFailure::InvalidValue,
            L"The requested rating, label, or pick state is outside QuickSift's supported range.");
    }
    try {
        EmbeddedMetadataAccessLease exiv2Lease(true);

        FileIdentity originalIdentity{};
        if (!Fingerprint(image, originalIdentity)) {
            return Failure(EmbeddedMetadataFailure::UnsafePath,
                L"The image is missing, inaccessible, symbolic, or not a normal file.");
        }
        const DWORD originalAttributes = GetFileAttributesW(image.wstring().c_str());
        if (originalAttributes == INVALID_FILE_ATTRIBUTES ||
            (originalAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
            return Failure(EmbeddedMetadataFailure::UnsafePath,
                L"The image is read-only or its file attributes could not be verified.");
        }
        const auto originalSignature = ReadContainerSignature(image);
        const auto originalEssence = PixelEssenceHash(image);
        if (!originalSignature || !originalEssence || originalSignature->mimeType != "image/jpeg") {
            return Failure(EmbeddedMetadataFailure::UnsupportedFormat,
                L"Fast direct metadata writing is restricted to verified JPEG containers.");
        }

        const std::uint64_t required = kMetadataGrowthAllowance;
        const std::uint64_t requiredWithReserve = SaturatingAdd64(required, kMetadataSafetyReserve);
        if (const auto available = FreeBytes(image)) {
            if (*available < requiredWithReserve) {
                return Failure(EmbeddedMetadataFailure::LowStorage,
                    L"There is not enough free space for a direct JPEG metadata update.",
                    requiredWithReserve, *available);
            }
        }

        quicksift::core::PhotoMetadataValues beforeValues;
        bool beforeValuesValid = false;
        try {
            RegisterQuickSiftNamespace();
            auto target = OpenImage(image);
            if (!target || !target->good()) {
                return Failure(EmbeddedMetadataFailure::UnsupportedFormat,
                    L"This JPEG does not expose a writable embedded XMP container.");
            }
            const Exiv2::AccessMode xmpMode = target->checkMode(Exiv2::mdXmp);
            if ((static_cast<int>(xmpMode) & static_cast<int>(Exiv2::amWrite)) == 0) {
                return Failure(EmbeddedMetadataFailure::UnsupportedFormat,
                    L"This JPEG reports that embedded XMP is not writable.");
            }
            target->readMetadata();

            // Recheck immediately before the irreversible in-place write. This
            // catches an external edit that happened while Exiv2 parsed the file.
            FileIdentity beforeWrite{};
            if (!Fingerprint(image, beforeWrite) || !SameIdentity(beforeWrite, originalIdentity)) {
                return Failure(EmbeddedMetadataFailure::SourceChanged,
                    L"The JPEG changed before metadata could be written, so QuickSift stopped.");
            }

            Exiv2::XmpData& xmp = target->xmpData();
            const quicksift::core::PhotoMetadataValues current = ReadMetadataValues(xmp);
            beforeValues = current;
            beforeValuesValid = true;
            quicksift::core::MetadataFieldId conflict{};
            if (!quicksift::core::MetadataExpectedMatches(current, patch, &conflict)) {
                return Failure(EmbeddedMetadataFailure::SourceChanged,
                    std::wstring(L"The JPEG's ") +
                    quicksift::core::MetadataFieldDisplayName(conflict) +
                    L" changed outside QuickSift, so the direct metadata update was stopped.");
            }
            ApplyMetadataPatch(xmp, patch);
            target->writeMetadata();
        } catch (const Exiv2::Error& error) {
            return Failure(EmbeddedMetadataFailure::WriteFailed,
                L"The direct JPEG metadata write failed. Because fast mode edits the original file, verify the image before continuing: " +
                WideFromUtf8(error.what()));
        } catch (...) {
            return Failure(EmbeddedMetadataFailure::WriteFailed,
                L"The direct JPEG metadata write failed unexpectedly. Because fast mode edits the original file, verify the image before continuing.");
        }

        if (!FlushPath(image)) {
            return Failure(EmbeddedMetadataFailure::WriteFailed,
                L"Windows could not flush the directly updated JPEG. The file may already contain the new metadata; verify it before continuing.");
        }

        const auto updatedSignature = ReadContainerSignature(image);
        const auto updatedEssence = PixelEssenceHash(image);
        if (!updatedSignature || !SameContainerSignature(*originalSignature, *updatedSignature) ||
            !updatedEssence || *updatedEssence != *originalEssence) {
            return Failure(EmbeddedMetadataFailure::VerificationFailed,
                L"The directly updated JPEG did not retain its original decoded pixel essence. Stop using this file until it has been checked.");
        }
        const auto verification = ReadFromImage(image);
        if (!verification || !PatchWasApplied(*verification, patch)) {
            return Failure(EmbeddedMetadataFailure::VerificationFailed,
                L"The JPEG was written directly but the requested metadata could not be verified. Inspect the file before making more edits.");
        }

        EmbeddedMetadataWriteResult result;
        result.ok = true;
        result.failure = EmbeddedMetadataFailure::None;
        if (beforeValuesValid) result.before = beforeValues;
        if (verification) result.after = *verification;
        return result;
    } catch (...) {
        return Failure(EmbeddedMetadataFailure::WriteFailed,
            L"An unexpected direct JPEG metadata error occurred. Because fast mode edits the original file, verify it before continuing.");
    }
}



bool EmbeddedMetadataAvailable() noexcept {
    return true;
}

#else

bool EmbeddedMetadataAvailable() noexcept {
    return false;
}

std::optional<EmbeddedMetadataValues> ReadEmbeddedMetadata(const fs::path&) noexcept {
    return std::nullopt;
}

EmbeddedMetadataWriteResult WriteEmbeddedMetadataAtomic(const fs::path&,
    const quicksift::core::MetadataPatch&) noexcept {
    EmbeddedMetadataWriteResult result;
    result.failure = EmbeddedMetadataFailure::UnsupportedFormat;
    result.detail = L"This build does not include embedded-metadata support. Use XMP sidecars or build with build-release.cmd.";
    return result;
}

EmbeddedMetadataWriteResult WriteEmbeddedMetadataInPlace(const fs::path&,
    const quicksift::core::MetadataPatch&) noexcept {
    EmbeddedMetadataWriteResult result;
    result.failure = EmbeddedMetadataFailure::UnsupportedFormat;
    result.detail = L"This build does not include embedded-metadata support. Use XMP sidecars or build with build-release.cmd.";
    return result;
}



#endif

} // namespace quicksift
