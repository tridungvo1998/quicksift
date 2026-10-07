// OWNER: Background decode, cache, and metadata-read execution.
#include "work/background_work_engine.h"
#include "review/review_async_policy.h"
#include "embedded_metadata.h"
#include "metadata/jpeg_orientation.h"

namespace quicksift::app {
namespace {

const wchar_t* JobKindName(JobKind kind) noexcept {
    switch (kind) {
    case JobKind::DecodePreview: return L"preview";
    case JobKind::DecodeFull: return L"full";
    case JobKind::DecodeTile: return L"tile";
    case JobKind::Metadata: return L"metadata";
    case JobKind::Exif: return L"exif";
    case JobKind::Face: return L"face";
    }
    return L"unknown";
}

const wchar_t* JobPriorityName(JobPriority priority) noexcept {
    switch (priority) {
    case JobPriority::Interactive: return L"interactive";
    case JobPriority::Visible: return L"visible";
    case JobPriority::Predictive: return L"predictive";
    case JobPriority::Face: return L"face";
    case JobPriority::Idle: return L"idle";
    case JobPriority::Analysis: return L"analysis";
    }
    return L"unknown";
}

std::wstring DiagnosticFilename(const fs::path& path) {
    try { return path.filename().wstring(); } catch (...) { return L"<invalid>"; }
}

DecodePriority DecodePriorityFor(JobPriority priority) noexcept {
    switch (priority) {
    case JobPriority::Interactive: return DecodePriority::Interactive;
    case JobPriority::Visible: return DecodePriority::Visible;
    case JobPriority::Predictive: return DecodePriority::Predictive;
    case JobPriority::Face: return DecodePriority::Face;
    case JobPriority::Idle: return DecodePriority::Idle;
    case JobPriority::Analysis: return DecodePriority::Analysis;
    }
    return DecodePriority::Analysis;
}

thread_local Worker* gThreadWorkerOwner = nullptr;
thread_local size_t gThreadWorkerIndex = 0;

std::wstring ThumbnailPathKey(const fs::path& path) {
    std::error_code error;
    fs::path absolute = fs::absolute(path, error);
    std::wstring key = (error ? path : absolute).lexically_normal().wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(character)));
    });
    return key;
}

} // namespace

// CODE GUIDE: See CODE_GUIDE.md -> "Rules for asynchronous work".
// OWNER: Background decoding/cache queues and decoder-exclusion coordination.

// Folder enumeration is owned by folder_scanner.cpp. Cross-thread result ownership
// is owned by BackgroundCompletionQueue.

// Cross-thread payloads live in BackgroundCompletionQueue. This helper posts
// only a payload-free wake-up; a failed wake does not lose ownership because
// the UI also drains the queue from its regular timer path.
bool PostBackgroundWakeWithRetry(HWND notify, UINT message, WPARAM wParam,
    LPARAM lParam) noexcept {
    if (!notify) return false;
    constexpr DWORD kRetryDelaysMs[] = { 0, 1, 2, 4, 8 };
    for (DWORD delay : kRetryDelaysMs) {
        if (delay != 0) Sleep(delay);
        if (PostMessageW(notify, message, wParam, lParam) != FALSE) return true;
        if (!IsWindow(notify)) return false;
    }
    return false;
}

std::wstring JobKey(const WorkJob& job) {
    std::wstring key = job.path.wstring();
    key += L'|';
    key += std::to_wstring(static_cast<int>(job.kind));
    key += L'|';
    key += std::to_wstring(static_cast<int>(job.cacheClass));
    key += L'|';
    key += std::to_wstring(job.targetSize);
    key += L'|';
    key += std::to_wstring(job.tileX) + L"," + std::to_wstring(job.tileY) + L"," +
        std::to_wstring(job.tileWidth) + L"," + std::to_wstring(job.tileHeight) + L"," +
        std::to_wstring(job.tileLevel);
    key += L'|';
    key += std::to_wstring(job.generation);
    key += L'|';
    // Face analysis is catalog work rather than a distinct decode per navigation
    // epoch. Ignoring the epoch in its key lets a newly visible request promote
    // an already queued idle analysis instead of decoding the same file twice.
    if (job.kind == JobKind::Face) key += L"0";
    else key += std::to_wstring(job.requestEpoch);
    key += L'|';
    // Thumbnail viewport epochs decide cancellation, not decode identity. Keeping
    // one stable pending key lets an in-flight image that overlaps the next
    // viewport satisfy the new request instead of starting a duplicate decode.
    if (job.cacheClass == CacheClass::Thumbnail) key += L"0";
    else key += std::to_wstring(job.viewportEpoch);
    if (job.kind == JobKind::DecodePreview || job.kind == JobKind::DecodeFull ||
        job.kind == JobKind::DecodeTile || job.kind == JobKind::Face) {
        key += L'|';
        const bool rawJpegOnly = IsRawExtension(ExtensionLower(job.path)) && job.rawJpegPreviewOnly;
        key += rawJpegOnly ? L"raw-jpeg-only" : L"raw-normal";
    }
    if (job.cacheClass == CacheClass::Thumbnail && job.kind == JobKind::DecodePreview) {
        key += L"|retry=" + std::to_wstring(job.retryAttempt);
        key += L"|wic=";
        key += job.forceWic ? L"1" : L"0";
    }
    if (job.kind == JobKind::Metadata) {
        key += L'|';
        key += std::to_wstring(job.metadataRevision);
    }
    return key;
}

std::wstring DecodeFailureKey(const std::wstring& path, int targetSize, JobKind kind, CacheClass cacheClass) {
    return path + L"|" + std::to_wstring(targetSize) + L"|" +
        std::to_wstring(static_cast<int>(kind)) + L"|" +
        std::to_wstring(static_cast<int>(cacheClass));
}

bool PreviewResolutionAdequate(UINT previewWidth, UINT previewHeight, int targetSize,
    UINT sourceWidth, UINT sourceHeight) noexcept {
    if (previewWidth == 0 || previewHeight == 0) return false;
    if (targetSize <= 0) return true;
    const std::uint64_t sourceEdge = std::max<std::uint64_t>(sourceWidth, sourceHeight);
    const std::uint64_t requestedEdge = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(targetSize), sourceEdge == 0 ?
            static_cast<std::uint64_t>(targetSize) : sourceEdge);
    const std::uint64_t previewEdge = std::max<std::uint64_t>(previewWidth, previewHeight);
    return previewEdge * 10ull >= requestedEdge * 9ull;
}

WICBitmapTransformOptions OrientationTransformForValue(int orientation) noexcept {
    switch (orientation) {
    case 2: return WICBitmapTransformFlipHorizontal;
    case 3: return WICBitmapTransformRotate180;
    case 4: return WICBitmapTransformFlipVertical;
    case 5: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
    case 6: return WICBitmapTransformRotate90;
    case 7: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
    case 8: return WICBitmapTransformRotate270;
    default: return WICBitmapTransformRotate0;
    }
}

WICBitmapTransformOptions ReadJpegOrientationFromFile(const fs::path& path) noexcept {
    // JPEG EXIF orientation lives in an APP1 segment before the entropy-coded
    // image data. Read only a bounded header window instead of asking WIC's
    // metadata query reader to parse the JPEG metadata graph during every image
    // decode. A pathological file may put APP1 unusually late; in that case we
    // deliberately fall back to the neutral orientation rather than turning a
    // thumbnail decode into an unbounded metadata operation.
    constexpr DWORD kHeaderBytes = 1024u * 1024u;
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return WICBitmapTransformRotate0;
    std::vector<std::uint8_t> bytes(kHeaderBytes);
    DWORD read = 0;
    const bool ok = ReadFile(handle, bytes.data(), kHeaderBytes, &read, nullptr) != FALSE;
    CloseHandle(handle);
    if (!ok || read < 4) return WICBitmapTransformRotate0;
    bytes.resize(read);
    return OrientationTransformForValue(quicksift::metadata::ReadJpegOrientation(bytes));
}

WICBitmapTransformOptions ReadOrientationTransform(IWICBitmapFrameDecode* frame, const fs::path& path) {
    const std::wstring extension = ExtensionLower(path);
    if (extension == L".jpg" || extension == L".jpeg") {
        return ReadJpegOrientationFromFile(path);
    }

    ComPtr<IWICMetadataQueryReader> reader;
    if (FAILED(frame->GetMetadataQueryReader(&reader))) return WICBitmapTransformRotate0;

    const wchar_t* paths[] = { L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}" };
    USHORT orientation = 1;
    for (const wchar_t* queryPath : paths) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(reader->GetMetadataByName(queryPath, &value))) {
            if (value.vt == VT_UI2) orientation = value.uiVal;
            else if (value.vt == VT_UI4) orientation = static_cast<USHORT>(value.ulVal);
            PropVariantClear(&value);
            break;
        }
        PropVariantClear(&value);
    }
    return OrientationTransformForValue(orientation);
}

struct WicSourceIdentity {
    DWORD volumeSerial = 0;
    DWORD fileIndexHigh = 0;
    DWORD fileIndexLow = 0;
    DWORD fileSizeHigh = 0;
    DWORD fileSizeLow = 0;
    FILETIME lastWrite{};
    LONGLONG changeTime = 0;

    bool operator==(const WicSourceIdentity& other) const noexcept {
        return volumeSerial == other.volumeSerial &&
            fileIndexHigh == other.fileIndexHigh && fileIndexLow == other.fileIndexLow &&
            fileSizeHigh == other.fileSizeHigh && fileSizeLow == other.fileSizeLow &&
            lastWrite.dwHighDateTime == other.lastWrite.dwHighDateTime &&
            lastWrite.dwLowDateTime == other.lastWrite.dwLowDateTime &&
            changeTime == other.changeTime;
    }
};

bool CaptureWicSourceIdentity(const fs::path& path, WicSourceIdentity& identity) {
    identity = {};
    HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION information{};
    FILE_BASIC_INFO basic{};
    const bool valid = GetFileInformationByHandle(handle, &information) != FALSE &&
        GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) != FALSE &&
        (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
    CloseHandle(handle);
    if (!valid) return false;

    identity.volumeSerial = information.dwVolumeSerialNumber;
    identity.fileIndexHigh = information.nFileIndexHigh;
    identity.fileIndexLow = information.nFileIndexLow;
    identity.fileSizeHigh = information.nFileSizeHigh;
    identity.fileSizeLow = information.nFileSizeLow;
    identity.lastWrite = information.ftLastWriteTime;
    identity.changeTime = basic.ChangeTime.QuadPart;
    return true;
}

struct WicDecoderSession {
    fs::path path;
    WicSourceIdentity identity{};
    std::uint64_t lastUse = 0;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
};

thread_local std::vector<WicDecoderSession> gThreadWicDecoderSessions;
thread_local std::uint64_t gThreadWicUseCounter = 0;

void PublishThreadWicDecoderSessions() noexcept {
    if (!gThreadWorkerOwner) return;
    try {
        std::vector<fs::path> paths;
        paths.reserve(gThreadWicDecoderSessions.size());
        for (const auto& session : gThreadWicDecoderSessions) paths.push_back(session.path);
        gThreadWorkerOwner->PublishDecoderSessionPaths(gThreadWorkerIndex, paths);
    } catch (...) {
    }
}

void ReleaseBlockedThreadWicDecoderSessions() {
    if (!gThreadWorkerOwner) return;
    std::erase_if(gThreadWicDecoderSessions, [](const WicDecoderSession& session) {
        return gThreadWorkerOwner->IsPathBlockedForDecoder(session.path);
    });
    PublishThreadWicDecoderSessions();
}

void ReleaseThreadWicDecoderSessions() {
    // Release retained decoder/frame COM objects before RoUninitialize. Thread-local
    // destructors run after Run() returns, which is too late for apartment objects.
    std::vector<WicDecoderSession>().swap(gThreadWicDecoderSessions);
    gThreadWicUseCounter = 0;
    PublishThreadWicDecoderSessions();
}

bool AcquireWicDecoderSession(IWICImagingFactory* factory, const fs::path& path,
    ComPtr<IWICBitmapDecoder>& decoder, ComPtr<IWICBitmapFrameDecode>& frame,
    bool enableRandomAccessIndex = false) {
    if (!factory) return false;
    auto& sessions = gThreadWicDecoderSessions;
    auto& useCounter = gThreadWicUseCounter;
    WicSourceIdentity identity{};
    const bool identityAvailable = CaptureWicSourceIdentity(path, identity);
    const unsigned maximum = gRetainedWicDecoderSessions.load(std::memory_order_relaxed);
    if (maximum == 0) {
        std::vector<WicDecoderSession>().swap(sessions);
        PublishThreadWicDecoderSessions();
    } else if (sessions.size() > maximum) {
        std::sort(sessions.begin(), sessions.end(), [](const auto& a, const auto& b) {
            return a.lastUse > b.lastUse;
        });
        sessions.resize(maximum);
    }
    for (auto& session : sessions) {
        if (identityAvailable && session.path == path && session.identity == identity && session.frame) {
            session.lastUse = ++useCounter;
            decoder = session.decoder;
            frame = session.frame;
            PublishThreadWicDecoderSessions();
            return true;
        }
    }

    ComPtr<IWICBitmapDecoder> createdDecoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &createdDecoder))) return false;
    ComPtr<IWICBitmapFrameDecode> createdFrame;
    if (FAILED(createdDecoder->GetFrame(0, &createdFrame))) return false;

    // JPEG indexing is useful for random-access tile reads, but it is actively
    // harmful for the normal top-to-bottom thumbnail/full-image path: Microsoft
    // documents that without SetIndexing(), sequential CopyPixels is more
    // efficient. Generate an index only for tile jobs that genuinely need random
    // access; never make ordinary image loading pay this cost.
    if (enableRandomAccessIndex) {
        ComPtr<IWICJpegFrameDecode> jpegFrame;
        BOOL supportsIndexing = FALSE;
        if (SUCCEEDED(createdFrame.As(&jpegFrame)) && jpegFrame &&
            SUCCEEDED(jpegFrame->DoesSupportIndexing(&supportsIndexing)) && supportsIndexing) {
            jpegFrame->SetIndexing(WICJpegIndexingOptionsGenerateOnDemand,
                std::max<UINT>(256u, gWicJpegIndexInterval.load(std::memory_order_relaxed)));
        }
    }

    if (identityAvailable && maximum != 0) {
        WicDecoderSession session;
        session.path = path;
        session.identity = identity;
        session.lastUse = ++useCounter;
        session.decoder = createdDecoder;
        session.frame = createdFrame;
        if (sessions.size() >= maximum && !sessions.empty()) {
            auto victim = std::min_element(sessions.begin(), sessions.end(), [](const auto& a, const auto& b) {
                return a.lastUse < b.lastUse;
            });
            *victim = std::move(session);
        } else {
            sessions.push_back(std::move(session));
        }
    }
    decoder = std::move(createdDecoder);
    frame = std::move(createdFrame);
    PublishThreadWicDecoderSessions();
    return true;
}

struct WicCancellationContext {
    const std::function<bool()>* cancelled = nullptr;
};

HRESULT CALLBACK WicProgressCallback(LPVOID context, ULONG, WICProgressOperation, double) noexcept {
    try {
        const auto* cancellation = static_cast<const WicCancellationContext*>(context);
        return cancellation && cancellation->cancelled && *cancellation->cancelled &&
            (*cancellation->cancelled)() ? WINCODEC_ERR_ABORTED : S_OK;
    } catch (...) {
        // Never allow a std::function target to unwind through the WIC COM ABI.
        return WINCODEC_ERR_ABORTED;
    }
}

bool DecodeWithWic(IWICImagingFactory* factory, const fs::path& path, int targetSize,
    WorkResult& result, bool preferEmbeddedPreview = false,
    const std::function<bool()>& isCancelled = {}) {
    if (!factory || (isCancelled && isCancelled())) return false;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (!AcquireWicDecoderSession(factory, path, decoder, frame, false)) return false;

    WicCancellationContext cancellationContext{ &isCancelled };
    ComPtr<IWICBitmapCodecProgressNotification> progressNotification;
    if (isCancelled && SUCCEEDED(decoder.As(&progressNotification))) {
        progressNotification->RegisterProgressNotification(
            WicProgressCallback, &cancellationContext,
            static_cast<DWORD>(WICProgressNotificationAll));
    }
    struct ProgressReset {
        IWICBitmapCodecProgressNotification* notification = nullptr;
        ~ProgressReset() {
            if (notification) notification->RegisterProgressNotification(nullptr, nullptr, 0);
        }
    } progressReset{ progressNotification.Get() };

    UINT originalW = 0, originalH = 0;
    if (FAILED(frame->GetSize(&originalW, &originalH)) || originalW == 0 || originalH == 0) return false;
    const WICBitmapTransformOptions transform = ReadOrientationTransform(frame.Get(), path);
    const bool quarterTurn = (transform & WICBitmapTransformRotate90) == WICBitmapTransformRotate90 ||
        (transform & WICBitmapTransformRotate270) == WICBitmapTransformRotate270;
    const UINT orientedW = quarterTurn ? originalH : originalW;
    const UINT orientedH = quarterTurn ? originalW : originalH;
    if (orientedW > static_cast<UINT>(std::numeric_limits<int>::max()) ||
        orientedH > static_cast<UINT>(std::numeric_limits<int>::max())) return false;
    result.sourceWidth = static_cast<int>(orientedW);
    result.sourceHeight = static_cast<int>(orientedH);

    UINT outW = orientedW, outH = orientedH;
    if (targetSize > 0 && (orientedW > static_cast<UINT>(targetSize) || orientedH > static_cast<UINT>(targetSize))) {
        const double scale = std::min(static_cast<double>(targetSize) / orientedW,
            static_cast<double>(targetSize) / orientedH);
        outW = std::max<UINT>(1, static_cast<UINT>(std::lround(orientedW * scale)));
        outH = std::max<UINT>(1, static_cast<UINT>(std::lround(orientedH * scale)));
    }

    // Decoder-native transforms can combine DCT scaling, orientation and format
    // conversion without materialising the full source image first.
    if (!preferEmbeddedPreview) {
        ComPtr<IWICBitmapSourceTransform> nativeTransform;
        if (SUCCEEDED(frame.As(&nativeTransform))) {
            BOOL supported = FALSE;
            if (SUCCEEDED(nativeTransform->DoesSupportTransform(transform, &supported)) && supported) {
                // IWICBitmapSourceTransform scales before it rotates. Therefore
                // uiWidth/uiHeight are pre-rotation dimensions, while the output
                // stride and buffer are based on the transformed dimensions.
                UINT nativeW = quarterTurn ? outH : outW;
                UINT nativeH = quarterTurn ? outW : outH;
                WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
                if (SUCCEEDED(nativeTransform->GetClosestSize(&nativeW, &nativeH)) &&
                    SUCCEEDED(nativeTransform->GetClosestPixelFormat(&format)) &&
                    IsEqualGUID(format, GUID_WICPixelFormat32bppPBGRA) && nativeW && nativeH) {
                    const UINT transformedW = quarterTurn ? nativeH : nativeW;
                    const UINT transformedH = quarterTurn ? nativeW : nativeH;
                    const std::uint64_t requestedLongest = static_cast<std::uint64_t>(
                        std::max(outW, outH));
                    const std::uint64_t transformedLongest = static_cast<std::uint64_t>(
                        std::max(transformedW, transformedH));
                    // Some codecs report only a coarse native reduction (or the
                    // original size). Do not cache a multi-megapixel surface under
                    // a 128-pixel thumbnail key; let the normal WIC chain perform
                    // the remaining streaming scale instead.
                    const bool closeEnough = targetSize <= 0 ||
                        transformedLongest <= std::max<std::uint64_t>(
                            requestedLongest + 64ull, requestedLongest * 3ull / 2ull);
                    const std::uint64_t stride64 = static_cast<std::uint64_t>(transformedW) * 4ull;
                    const std::uint64_t bytes64 = stride64 * transformedH;
                    if (closeEnough && stride64 <= UINT_MAX && bytes64 <= UINT_MAX &&
                        bytes64 <= gMaximumDecodedPixelBytes.load(std::memory_order_relaxed)) {
                        try { result.pixels.resize(static_cast<std::size_t>(bytes64)); }
                        catch (...) { return false; }
                        if ((!isCancelled || !isCancelled()) && SUCCEEDED(nativeTransform->CopyPixels(
                            nullptr, nativeW, nativeH, &format, transform,
                            static_cast<UINT>(stride64), static_cast<UINT>(bytes64), result.pixels.data()))) {
                            result.width = static_cast<int>(transformedW);
                            result.height = static_cast<int>(transformedH);
                            return true;
                        }
                        result.pixels.clear();
                    }
                }
            }
        }
    }

    ComPtr<IWICBitmapSource> source = frame;
    if (preferEmbeddedPreview) {
        ComPtr<IWICBitmapSource> thumbnail;
        if (FAILED(decoder->GetThumbnail(&thumbnail)) || !thumbnail) frame->GetThumbnail(&thumbnail);
        if (thumbnail) {
            UINT thumbW = 0, thumbH = 0;
            if (SUCCEEDED(thumbnail->GetSize(&thumbW, &thumbH)) &&
                PreviewResolutionAdequate(quarterTurn ? thumbH : thumbW,
                    quarterTurn ? thumbW : thumbH, targetSize, orientedW, orientedH)) {
                source = thumbnail;
                result.previewOnly = true;
            }
        }
        // A progressive JPEG's first scan is a quality stand-in, not a smaller
        // resolution contract. Grid thumbnails have no guaranteed later refine
        // pass, so an inadequate embedded preview falls through to a normal
        // target-sized frame decode instead of being cached permanently.
    } else {
        ComPtr<IWICProgressiveLevelControl> progressive;
        if (SUCCEEDED(frame.As(&progressive))) {
            UINT levels = 0;
            if (SUCCEEDED(progressive->GetLevelCount(&levels)) && levels > 0)
                progressive->SetCurrentLevel(levels - 1);
        }
    }

    UINT sourceW = 0, sourceH = 0;
    if (FAILED(source->GetSize(&sourceW, &sourceH)) || !sourceW || !sourceH) return false;
    const UINT sourceOrientedW = quarterTurn ? sourceH : sourceW;
    const UINT sourceOrientedH = quarterTurn ? sourceW : sourceH;
    UINT finalW = sourceOrientedW, finalH = sourceOrientedH;
    if (targetSize > 0 && (finalW > static_cast<UINT>(targetSize) || finalH > static_cast<UINT>(targetSize))) {
        const double scale = std::min(static_cast<double>(targetSize) / finalW,
            static_cast<double>(targetSize) / finalH);
        finalW = std::max<UINT>(1, static_cast<UINT>(std::lround(finalW * scale)));
        finalH = std::max<UINT>(1, static_cast<UINT>(std::lround(finalH * scale)));
    }

    // WIC's canonical order is scale, crop, then rotate/flip. Scaling first also
    // prevents a 60-MP source from being materialised by the rotator when the UI
    // only needs a small thumbnail.
    const UINT preRotateW = quarterTurn ? finalH : finalW;
    const UINT preRotateH = quarterTurn ? finalW : finalH;
    ComPtr<IWICBitmapSource> scaled = source;
    if (preRotateW != sourceW || preRotateH != sourceH) {
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(factory->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(
            source.Get(), preRotateW, preRotateH, WICBitmapInterpolationModeFant))) return false;
        scaled = scaler;
    }
    ComPtr<IWICBitmapSource> oriented = scaled;
    if (transform != WICBitmapTransformRotate0) {
        ComPtr<IWICBitmapFlipRotator> rotator;
        if (FAILED(factory->CreateBitmapFlipRotator(&rotator)) ||
            FAILED(rotator->Initialize(scaled.Get(), transform))) return false;
        oriented = rotator;
    }
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) || FAILED(converter->Initialize(
        oriented.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
        nullptr, 0.0, WICBitmapPaletteTypeCustom))) return false;
    const std::uint64_t stride64 = static_cast<std::uint64_t>(finalW) * 4ull;
    const std::uint64_t bytes64 = stride64 * finalH;
    if (stride64 > UINT_MAX || bytes64 > UINT_MAX ||
        bytes64 > gMaximumDecodedPixelBytes.load(std::memory_order_relaxed)) return false;
    try { result.pixels.resize(static_cast<std::size_t>(bytes64)); }
    catch (...) { return false; }
    if ((isCancelled && isCancelled()) || FAILED(converter->CopyPixels(nullptr,
        static_cast<UINT>(stride64), static_cast<UINT>(bytes64), result.pixels.data()))) {
        result.pixels.clear();
        return false;
    }
    result.width = static_cast<int>(finalW);
    result.height = static_cast<int>(finalH);
    return true;
}

bool CreateWicDerivative(IWICImagingFactory* factory, const WorkResult& source, int targetSize,
    WorkResult& output) {
    if (!factory || source.width <= 0 || source.height <= 0 || source.pixels.empty() || targetSize <= 0)
        return false;
    const int maximumEdge = std::max(source.width, source.height);
    if (targetSize >= maximumEdge) return false;
    const double scale = std::min(static_cast<double>(targetSize) / source.width,
        static_cast<double>(targetSize) / source.height);
    const UINT outW = std::max<UINT>(1, static_cast<UINT>(std::lround(source.width * scale)));
    const UINT outH = std::max<UINT>(1, static_cast<UINT>(std::lround(source.height * scale)));
    const std::uint64_t sourceStride64 = static_cast<std::uint64_t>(source.width) * 4ull;
    const std::uint64_t sourceBytes64 = sourceStride64 * static_cast<std::uint64_t>(source.height);
    const std::uint64_t outputStride64 = static_cast<std::uint64_t>(outW) * 4ull;
    const std::uint64_t outputBytes64 = outputStride64 * outH;
    if (sourceStride64 > UINT_MAX || sourceBytes64 != source.pixels.size() ||
        sourceBytes64 > UINT_MAX || outputStride64 > UINT_MAX || outputBytes64 > UINT_MAX) return false;

    ComPtr<IWICBitmap> memoryBitmap;
    if (FAILED(factory->CreateBitmapFromMemory(static_cast<UINT>(source.width),
        static_cast<UINT>(source.height), GUID_WICPixelFormat32bppPBGRA,
        static_cast<UINT>(sourceStride64), static_cast<UINT>(sourceBytes64),
        const_cast<BYTE*>(source.pixels.data()), &memoryBitmap))) return false;
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(factory->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(
        memoryBitmap.Get(), outW, outH, WICBitmapInterpolationModeFant))) return false;
    try { output.pixels.resize(static_cast<std::size_t>(outputBytes64)); }
    catch (...) { return false; }
    if (FAILED(scaler->CopyPixels(nullptr, static_cast<UINT>(outputStride64),
        static_cast<UINT>(outputBytes64), output.pixels.data()))) {
        output.pixels.clear();
        return false;
    }
    output.width = static_cast<int>(outW);
    output.height = static_cast<int>(outH);
    output.sourceWidth = source.sourceWidth;
    output.sourceHeight = source.sourceHeight;
    output.previewOnly = source.previewOnly;
    return true;
}

bool DecodeTileWithWic(IWICImagingFactory* factory, const fs::path& path, const WorkJob& job,
    WorkResult& result, const std::function<bool()>& isCancelled = {}) {
    if (!factory || job.tileWidth <= 0 || job.tileHeight <= 0 ||
        (isCancelled && isCancelled())) return false;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (!AcquireWicDecoderSession(factory, path, decoder, frame, true)) return false;

    WicCancellationContext cancellationContext{ &isCancelled };
    ComPtr<IWICBitmapCodecProgressNotification> progressNotification;
    if (isCancelled && SUCCEEDED(decoder.As(&progressNotification))) {
        progressNotification->RegisterProgressNotification(
            WicProgressCallback, &cancellationContext,
            static_cast<DWORD>(WICProgressNotificationAll));
    }
    struct TileProgressReset {
        IWICBitmapCodecProgressNotification* notification = nullptr;
        ~TileProgressReset() {
            if (notification) notification->RegisterProgressNotification(nullptr, nullptr, 0);
        }
    } progressReset{ progressNotification.Get() };
    ComPtr<IWICBitmapSource> oriented = frame;
    const WICBitmapTransformOptions transform = ReadOrientationTransform(frame.Get(), path);
    if (transform != WICBitmapTransformRotate0) {
        ComPtr<IWICBitmapFlipRotator> rotator;
        if (SUCCEEDED(factory->CreateBitmapFlipRotator(&rotator)) && SUCCEEDED(rotator->Initialize(frame.Get(), transform))) oriented = rotator;
    }
    UINT sourceW = 0, sourceH = 0;
    oriented->GetSize(&sourceW, &sourceH);
    if (!sourceW || !sourceH ||
        sourceW > static_cast<UINT>(std::numeric_limits<int>::max()) ||
        sourceH > static_cast<UINT>(std::numeric_limits<int>::max())) return false;
    const int x = std::clamp(job.tileX, 0, static_cast<int>(sourceW) - 1);
    const int y = std::clamp(job.tileY, 0, static_cast<int>(sourceH) - 1);
    const int width = std::min(job.tileWidth, static_cast<int>(sourceW) - x);
    const int height = std::min(job.tileHeight, static_cast<int>(sourceH) - y);
    if (width <= 0 || height <= 0) return false;

    ComPtr<IWICBitmapClipper> clipper;
    if (FAILED(factory->CreateBitmapClipper(&clipper))) return false;
    WICRect rect{ x, y, width, height };
    if (FAILED(clipper->Initialize(oriented.Get(), &rect))) return false;

    ComPtr<IWICBitmapSource> tileSource = clipper;
    const int level = std::clamp(job.tileLevel, 0, 8);
    const UINT divisor = 1u << level;
    const UINT outputWidth = std::max<UINT>(1, (static_cast<UINT>(width) + divisor - 1) / divisor);
    const UINT outputHeight = std::max<UINT>(1, (static_cast<UINT>(height) + divisor - 1) / divisor);
    if (level > 0 && (outputWidth != static_cast<UINT>(width) || outputHeight != static_cast<UINT>(height))) {
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(factory->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(
            clipper.Get(), outputWidth, outputHeight, WICBitmapInterpolationModeFant))) return false;
        tileSource = scaler;
    }
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) || FAILED(converter->Initialize(tileSource.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return false;
    result.width = static_cast<int>(outputWidth);
    result.height = static_cast<int>(outputHeight);
    result.sourceWidth = static_cast<int>(sourceW);
    result.sourceHeight = static_cast<int>(sourceH);
    result.tileX = x;
    result.tileY = y;
    result.tileWidth = width;
    result.tileHeight = height;
    result.tileLevel = level;
    const std::uint64_t stride64 = static_cast<std::uint64_t>(outputWidth) * 4ull;
    const std::uint64_t bytes64 = stride64 * static_cast<std::uint64_t>(outputHeight);
    if (stride64 > std::numeric_limits<UINT>::max() || bytes64 > gMaximumDecodedPixelBytes.load(std::memory_order_relaxed) ||
        bytes64 > std::numeric_limits<UINT>::max()) return false;
    const UINT stride = static_cast<UINT>(stride64);
    const UINT bytes = static_cast<UINT>(bytes64);
    try { result.pixels.resize(static_cast<size_t>(bytes)); }
    catch (...) { return false; }
    if ((isCancelled && isCancelled()) ||
        FAILED(converter->CopyPixels(nullptr, stride, bytes, result.pixels.data()))) {
        result.pixels.clear();
        return false;
    }
    return true;
}

bool DecodeWithBundled(const fs::path& path, const quicksift::DecodeOptions& options,
    WorkResult& result) {
    quicksift::DecodedImageData decoded;
    if (!quicksift::DecodeWithBundledCodec(path, options, decoded)) return false;
    result.width = decoded.width;
    result.height = decoded.height;
    result.sourceWidth = decoded.sourceWidth;
    result.sourceHeight = decoded.sourceHeight;
    result.previewOnly = decoded.embeddedPreview;
    result.pixels = std::move(decoded.pixels);
    return !result.pixels.empty();
}

bool DecodeWithShell(const fs::path& path, int targetSize, WorkResult& result) {
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item)))) return false;
    ComPtr<IShellItemImageFactory> imageFactory;
    if (FAILED(item.As(&imageFactory))) return false;

    SIZE size = { std::max(64, targetSize), std::max(64, targetSize) };
    HBITMAP bitmap = nullptr;
    HRESULT hr = imageFactory->GetImage(size,
        static_cast<SIIGBF>(SIIGBF_RESIZETOFIT | SIIGBF_THUMBNAILONLY), &bitmap);
    if (FAILED(hr)) {
        hr = imageFactory->GetImage(size,
            static_cast<SIIGBF>(SIIGBF_RESIZETOFIT), &bitmap);
    }
    if (FAILED(hr) || !bitmap) return false;
    GdiObjectOwner bitmapOwner(bitmap);

    BITMAP bm{};
    GetObject(bitmap, sizeof(bm), &bm);
    if (bm.bmWidth <= 0 || bm.bmHeight <= 0) {
        return false;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bm.bmWidth;
    info.bmiHeader.biHeight = -bm.bmHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    const std::uint64_t pixelBytes = static_cast<std::uint64_t>(bm.bmWidth) *
        static_cast<std::uint64_t>(bm.bmHeight) * 4ull;
    if (pixelBytes == 0 || pixelBytes > gMaximumDecodedPixelBytes.load(std::memory_order_relaxed) ||
        pixelBytes > std::numeric_limits<size_t>::max()) {
        return false;
    }
    result.width = bm.bmWidth;
    result.height = bm.bmHeight;
    result.sourceWidth = bm.bmWidth;
    result.sourceHeight = bm.bmHeight;
    result.pixels.resize(static_cast<size_t>(pixelBytes));

    HDC dc = GetDC(nullptr);
    if (!dc) {
        result.pixels.clear();
        return false;
    }
    const int lines = GetDIBits(dc, bitmap, 0, bm.bmHeight, result.pixels.data(), &info, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (lines != bm.bmHeight) {
        result.pixels.clear();
        return false;
    }

    bool anyAlpha = false;
    for (size_t i = 3; i < result.pixels.size(); i += 4) {
        if (result.pixels[i] != 0) {
            anyAlpha = true;
            break;
        }
    }
    if (!anyAlpha) {
        for (size_t i = 3; i < result.pixels.size(); i += 4) result.pixels[i] = 255;
    }
    return true;
}

struct FaceSharpnessResult {
    bool scanned = false;
    bool blurry = false;
    float score = 0.0f;
};

FaceSharpnessResult AnalyzePrimaryFaceSharpnessBgra(const std::uint8_t* pixels,
    int width, int height, const std::vector<quicksift::NormalizedFaceRect>& faces) {
    FaceSharpnessResult result;
    if (!pixels || width < 16 || height < 16 || faces.empty()) return result;

    const quicksift::NormalizedFaceRect& face = faces.front();
    const bool finiteFace = std::isfinite(face.x) && std::isfinite(face.y) &&
        std::isfinite(face.width) && std::isfinite(face.height);
    const bool normalizedFace = finiteFace && face.x >= 0.0f && face.y >= 0.0f &&
        face.width > 0.0f && face.height > 0.0f && face.x <= 1.0f && face.y <= 1.0f &&
        face.width <= 1.0f && face.height <= 1.0f &&
        face.x + face.width <= 1.001f && face.y + face.height <= 1.001f;
    if (!normalizedFace) return result;

    const float expandX = face.width * 0.12f;
    const float expandY = face.height * 0.12f;
    const int left = std::clamp(static_cast<int>(std::floor((face.x - expandX) * width)), 0, width - 1);
    const int top = std::clamp(static_cast<int>(std::floor((face.y - expandY) * height)), 0, height - 1);
    const int right = std::clamp(static_cast<int>(std::ceil((face.x + face.width + expandX) * width)), left + 1, width);
    const int bottom = std::clamp(static_cast<int>(std::ceil((face.y + face.height + expandY) * height)), top + 1, height);
    const int cropWidth = right - left;
    const int cropHeight = bottom - top;
    if (cropWidth < 32 || cropHeight < 32) return result;

    const int step = std::max(1, std::min(cropWidth, cropHeight) / 160);
    double lapMean = 0.0;
    double lapM2 = 0.0;
    double lumMean = 0.0;
    double lumM2 = 0.0;
    double gradientEnergySum = 0.0;
    std::size_t samples = 0;
    auto luminance = [&](int x, int y) {
        const std::uint8_t* pixel = pixels + (static_cast<std::size_t>(y) * width + x) * 4u;
        return (29.0 * pixel[0] + 150.0 * pixel[1] + 77.0 * pixel[2]) / 256.0;
    };

    for (int y = top + step; y < bottom - step; y += step) {
        for (int x = left + step; x < right - step; x += step) {
            const double center = luminance(x, y);
            const double leftLum = luminance(x - step, y);
            const double rightLum = luminance(x + step, y);
            const double topLum = luminance(x, y - step);
            const double bottomLum = luminance(x, y + step);
            const double laplacian = 4.0 * center - leftLum - rightLum - topLum - bottomLum;
            const double gradientX = rightLum - leftLum;
            const double gradientY = bottomLum - topLum;
            gradientEnergySum += gradientX * gradientX + gradientY * gradientY;
            ++samples;
            const double lapDelta = laplacian - lapMean;
            lapMean += lapDelta / static_cast<double>(samples);
            lapM2 += lapDelta * (laplacian - lapMean);
            const double lumDelta = center - lumMean;
            lumMean += lumDelta / static_cast<double>(samples);
            lumM2 += lumDelta * (center - lumMean);
        }
    }
    if (samples < 64) return result;

    const double lapVariance = lapM2 / static_cast<double>(samples - 1);
    const double luminanceVariance = lumM2 / static_cast<double>(samples - 1);
    const double gradientRms = std::sqrt(gradientEnergySum / static_cast<double>(samples));
    const int minimumEdge = std::min(cropWidth, cropHeight);

    // Medium sensitivity: flag only a clear focus miss. Laplacian variance catches
    // strong motion/general blur, while low broad-edge energy catches defocus. A
    // mildly soft portrait must fail both tests before it receives the red border.
    double laplacianThreshold = minimumEdge >= 128 ? 48.0 : (minimumEdge >= 80 ? 38.0 : 28.0);
    double defocusGradientThreshold = minimumEdge >= 128 ? 12.0 : (minimumEdge >= 80 ? 10.5 : 9.0);

    // Very dark, clipped, or low-contrast portraits naturally contain less edge
    // energy. Tighten the warning threshold so those faces are not painted red
    // merely because the exposure or lighting is difficult.
    double exposureScale = 1.0;
    if (luminanceVariance < 220.0) exposureScale *= 0.72;
    if (luminanceVariance < 90.0) exposureScale *= 0.78;
    if (lumMean < 35.0 || lumMean > 225.0) exposureScale *= 0.82;
    laplacianThreshold *= exposureScale;
    defocusGradientThreshold *= std::sqrt(exposureScale);

    const bool clearlyOutOfFocus = lapVariance < laplacianThreshold &&
        gradientRms < defocusGradientThreshold;
    const bool heavilyBlurred = lapVariance < laplacianThreshold * 0.28;

    result.scanned = true;
    result.score = static_cast<float>(lapVariance);
    result.blurry = clearlyOutOfFocus || heavilyBlurred;
    return result;
}

Worker::Worker(HWND notify, quicksift::work::BackgroundCompletionQueue* completionQueue,
    quicksift::PersistentCache* persistentCache, const SystemProfile& profile)
        : notify_(notify), completionQueue_(completionQueue), persistentCache_(persistentCache),
          decodeBudget_(std::make_shared<DecodeBudget>(profile.decodeBytesInFlightLimit,
              std::max(1u, profile.foregroundWorkers + profile.idleWorkers))),
          cacheWriteQueueLimit_(profile.cacheWriteQueueLimit),
          recordWriteQueueLimit_(profile.lowMemory ? 1024u :
              (profile.totalPhysicalBytes <= 18ull * kGibibyte ? 4096u : 8192u)),
          recordWriteBatchLimit_(profile.lowMemory ? 16u :
              (profile.totalPhysicalBytes <= 18ull * kGibibyte ? 64u : 128u)),
          idleDelayMilliseconds_(profile.idleDelayMilliseconds),
          serializeHeavyBundledDecodes_(false),
          singleWorkerMode_(profile.foregroundWorkers + profile.idleWorkers <= 1),
          profile_(profile), mappedInputLimit_(profile.mappedInputLimit),
          policyMappedInputLimit_(profile.mappedInputLimit),
          runtimeReusableBufferLimit_(profile.reusableBufferLimit) {
        readAhead_.Configure(profile.cpuTopology, profile.readAheadFiles,
            profile.lowMemory ? 16ull * kMebibyte : 64ull * kMebibyte,
            profile.reusableBufferLimit);
        maintenanceWriteThreshold_ = cacheWriteQueueLimit_ > SIZE_MAX / 8 ? SIZE_MAX :
            std::max<size_t>(128ull * kMebibyte, cacheWriteQueueLimit_ * 8);
        gMaximumDecodedPixelBytes.store(profile.maximumDecodedPixelBytes, std::memory_order_relaxed);
        gRetainedWicDecoderSessions.store(profile.retainedDecoderSessions, std::memory_order_relaxed);
        gWicJpegIndexInterval.store(profile.lowMemory ? 2048u :
            (profile.totalPhysicalBytes <= 18ull * kGibibyte ? 1024u : 512u),
            std::memory_order_relaxed);
        quicksift::ConfigureBundledCodecRuntime(
            profile.codecThreadsPerDecode, profile.maximumDecodedPixelBytes,
            profile.mappedInputLimit, profile.enableTurboJpeg);
        performancePolicy_.activeDecodePermits = std::max(1u,
            profile.foregroundWorkers + profile.idleWorkers);
        performancePolicy_.codecThreadsPerDecode = profile.codecThreadsPerDecode;
        performancePolicy_.decodeByteLimit = profile.decodeBytesInFlightLimit;
        performancePolicy_.readAheadFiles = profile.readAheadFiles;
        performancePolicy_.mappedInputLimit = profile.mappedInputLimit;
        performancePolicy_.reusableBufferLimit = profile.reusableBufferLimit;
        performancePolicy_.retainedDecoderSessions = profile.retainedDecoderSessions;
        performancePolicy_.derivativeLevels = profile.derivativeLevels;
        performancePolicy_.cacheWriteQueueLimit = profile.cacheWriteQueueLimit;
        runtimeReadAheadFiles_.store(profile.readAheadFiles, std::memory_order_relaxed);
        runtimeDerivativeLevels_.store(profile.derivativeLevels, std::memory_order_relaxed);
        decoderSessionPathsByThread_.resize(std::max(1u,
            profile.foregroundWorkers + profile.idleWorkers + 1));

        // Decode concurrency follows both CPU count and RAM class.  Low-memory
        // systems deliberately use fewer lanes, while machines with more cores
        // gain additional visible and idle workers without oversubscribing codecs.
        try {
            size_t threadIndex = 0;
            for (unsigned i = 0; i < profile.foregroundWorkers; ++i, ++threadIndex) {
                const size_t index = threadIndex;
                threads_.emplace_back([this, index] { Run(false, index); });
            }
            for (unsigned i = 0; i < profile.idleWorkers; ++i, ++threadIndex) {
                const size_t index = threadIndex;
                threads_.emplace_back([this, index] { Run(true, index); });
            }
            if (threads_.empty()) threads_.emplace_back([this] { Run(true, 0, false); });
            // Keep metadata parsing off the image-worker lanes. Exiv2/XMP reads can
            // be very slow for RAW files and are serialized by a process-wide mutex;
            // a stalled metadata read must never delay the visible image decode.
            metadataThread_ = std::thread([this] { Run(true, threads_.size(), true); });
            cacheWriter_ = std::thread([this] { RunCacheWriter(); });
        } catch (...) {
            QS_LOG_CRITICAL(L"Worker", L"Failed to start one or more worker threads");
            // std::thread destruction terminates the process while a thread remains
            // joinable. If any later lane fails to start, stop and join the lanes
            // already created before allowing the constructor exception to escape.
            Stop();
            throw;
        }
    }

    Worker::~Worker() { Stop(); }

    void Worker::Stop() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            stoppingAtomic_.store(true, std::memory_order_release);
            interactiveQueue_.clear();
            visibleQueue_.clear();
            predictiveQueue_.clear();
            faceQueue_.clear();
            idleQueue_.clear();
            analysisQueue_.clear();
            metadataQueue_.clear();
            cacheWriteQueue_.clear();
            recordWriteQueue_.clear();
            cacheWriteBytes_ = 0;
            pending_.clear();
        }
        // Broadcast every cancellation signal before waiting on any subsystem.
        // This lets active decoders/cache operations observe shutdown while we
        // drain unrelated thread-pool/read-ahead callbacks.
        if (decodeBudget_) decodeBudget_->Stop();
        cv_.notify_all();
        cacheCv_.notify_all();
        decoderTrimCv_.notify_all();
        blockedCv_.notify_all();
        readAhead_.CancelAll();
        for (std::thread& thread : threads_) if (thread.joinable()) thread.join();
        if (metadataThread_.joinable()) metadataThread_.join();
        if (cacheWriter_.joinable()) cacheWriter_.join();
    }

    void Worker::SetNotify(HWND notify) { std::lock_guard lock(mutex_); notify_ = notify; }

    void Worker::SetPersistentCache(quicksift::PersistentCache* persistentCache) {
        std::lock_guard lock(mutex_);
        persistentCache_ = persistentCache;
    }

    void Worker::ClearQueuedIdleWork() {
        std::lock_guard lock(mutex_);
        for (const auto& job : predictiveQueue_) pending_.erase(job.key.empty() ? JobKey(job) : job.key);
        for (const auto& job : idleQueue_) pending_.erase(job.key.empty() ? JobKey(job) : job.key);
        for (const auto& job : analysisQueue_) pending_.erase(job.key.empty() ? JobKey(job) : job.key);
        predictiveQueue_.clear();
        idleQueue_.clear();
        analysisQueue_.clear();
        metadataQueue_.clear();
        cacheWriteQueue_.clear();
        recordWriteQueue_.clear();
        cacheWriteBytes_ = 0;
        flushRequested_ = false;
    }

    void Worker::ClearPersistentCache() {
        // In-flight writes may already have left the queue. Advance the epoch
        // before clearing so those old payloads cannot repopulate a cache the
        // user explicitly emptied once they acquire the I/O mutex.
        cacheEpoch_.fetch_add(1, std::memory_order_acq_rel);
        ClearQueuedIdleWork();
        std::lock_guard cacheIoLock(cacheIoMutex_);
        if (persistentCache_) persistentCache_->ClearAll();
    }

    void Worker::AcknowledgePostedResult(const WorkResult& result) {
        AcknowledgePendingKey(result.pendingKey);
        ReleaseDesiredVisualTarget(result.path, result.kind, result.cacheClass,
            result.generation, result.requestEpoch, result.viewportEpoch, result.targetSize,
            result.rawJpegPreviewOnly);
    }

    void Worker::SetActiveGeneration(uint64_t generation) {
        activeGeneration_.store(generation, std::memory_order_release);
        {
            std::lock_guard desiredLock(desiredMutex_);
            desiredVisualTargets_.clear();
        }
        ClearOlderThan(generation);
        if (decodeBudget_) decodeBudget_->NotifyWaiters();
    }

    void Worker::SetThumbnailTarget(int targetSize) {
        targetSize = std::max(1, targetSize);
        const int previous = activeThumbnailTarget_.exchange(targetSize, std::memory_order_acq_rel);
        if (previous == targetSize) return;

        {
            std::lock_guard lock(mutex_);
            auto removeObsolete = [&](std::deque<WorkJob>& queue) {
                for (auto it = queue.begin(); it != queue.end();) {
                    if (it->cacheClass == CacheClass::Thumbnail && it->targetSize != targetSize) {
                        pending_.erase(it->key.empty() ? JobKey(*it) : it->key);
                        it = queue.erase(it);
                    } else {
                        ++it;
                    }
                }
            };
            removeObsolete(interactiveQueue_);
            removeObsolete(visibleQueue_);
            removeObsolete(predictiveQueue_);
            removeObsolete(faceQueue_);
            removeObsolete(idleQueue_);
            removeObsolete(analysisQueue_);
            for (auto it = cacheWriteQueue_.begin(); it != cacheWriteQueue_.end();) {
                if (it->cacheClass == CacheClass::Thumbnail && it->targetSize != targetSize) {
                    cacheWriteBytes_ = it->bytes > cacheWriteBytes_ ? 0 : cacheWriteBytes_ - it->bytes;
                    it = cacheWriteQueue_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        if (decodeBudget_) decodeBudget_->NotifyWaiters();
        cv_.notify_all();
    }

    void Worker::SetInteractiveVisualFocus(bool focused) {
        interactiveVisualFocus_.store(focused, std::memory_order_release);
        {
            std::lock_guard lock(mutex_);
            if (focused) {
                // Single/Compare are not background browsing operations. Once the
                // user has selected an image for review, queued speculative image
                // work must not consume a decode lane ahead of the exact requested
                // view. Interactive requests are left untouched.
                auto discardNonInteractiveVisual = [&](std::deque<WorkJob>& queue) {
                    for (auto it = queue.begin(); it != queue.end();) {
                        const bool visual = it->kind == JobKind::DecodePreview ||
                            it->kind == JobKind::DecodeFull ||
                            it->kind == JobKind::DecodeTile ||
                            it->kind == JobKind::Face;
                        if (visual && it->priority != JobPriority::Interactive) {
                            pending_.erase(it->key.empty() ? JobKey(*it) : it->key);
                            it = queue.erase(it);
                        } else {
                            ++it;
                        }
                    }
                };
                discardNonInteractiveVisual(visibleQueue_);
                discardNonInteractiveVisual(predictiveQueue_);
                discardNonInteractiveVisual(faceQueue_);
                discardNonInteractiveVisual(idleQueue_);
                discardNonInteractiveVisual(analysisQueue_);
            }
        }
        if (decodeBudget_) decodeBudget_->NotifyWaiters();
        cv_.notify_all();
        RefreshReadAheadGate();
    }

    void Worker::SetNavigationEpoch(uint64_t epoch) {
        navigationEpoch_.store(epoch, std::memory_order_release);
        {
            std::lock_guard desiredLock(desiredMutex_);
            desiredVisualTargets_.clear();
        }
        std::lock_guard lock(mutex_);
        auto removeObsolete = [&](std::deque<WorkJob>& queue) {
            for (auto it = queue.begin(); it != queue.end();) {
                const bool navigational = it->kind == JobKind::DecodePreview ||
                    it->kind == JobKind::DecodeFull || it->kind == JobKind::DecodeTile ||
                    it->kind == JobKind::Face;
                if (navigational && it->requestEpoch && it->requestEpoch < epoch) {
                    pending_.erase(it->key.empty() ? JobKey(*it) : it->key);
                    it = queue.erase(it);
                } else ++it;
            }
        };
        removeObsolete(interactiveQueue_);
        removeObsolete(visibleQueue_);
        removeObsolete(predictiveQueue_);
        removeObsolete(faceQueue_);
        removeObsolete(idleQueue_);
        removeObsolete(analysisQueue_);
        for (auto it = cacheWriteQueue_.begin(); it != cacheWriteQueue_.end();) {
            if (it->requestEpoch && it->requestEpoch < epoch) {
                cacheWriteBytes_ = it->bytes > cacheWriteBytes_ ? 0 : cacheWriteBytes_ - it->bytes;
                it = cacheWriteQueue_.erase(it);
            } else {
                ++it;
            }
        }
        if (decodeBudget_) decodeBudget_->NotifyWaiters();
        cv_.notify_all();
    }


    void Worker::SetViewInteractionEpoch(uint64_t epoch) {
        currentViewInteractionEpoch_.store(epoch, std::memory_order_release);
    }
    void Worker::DiscardQueuedTiles() {
        {
            std::lock_guard lock(mutex_);
            auto discard = [&](std::deque<WorkJob>& queue) {
                for (auto it = queue.begin(); it != queue.end();) {
                    if (it->kind == JobKind::DecodeTile) {
                        pending_.erase(it->key.empty() ? JobKey(*it) : it->key);
                        it = queue.erase(it);
                    } else {
                        ++it;
                    }
                }
            };
            discard(interactiveQueue_);
            discard(visibleQueue_);
            discard(predictiveQueue_);
            discard(faceQueue_);
            discard(idleQueue_);
            discard(analysisQueue_);
        }
        cv_.notify_all();
        RefreshReadAheadGate();
    }

    void Worker::RequestCacheFlush() {
        {
            std::lock_guard lock(mutex_);
            if (stopping_) return;
            flushRequested_ = true;
        }
        cacheCv_.notify_one();
    }

    void Worker::QueueRecordCacheWrite(const fs::path& path,
        quicksift::CachedImageRecord record) {
        if (!persistentCache_ || path.empty()) return;
        RecordCacheWrite write{ path, std::move(record),
            cacheEpoch_.load(std::memory_order_acquire) };
        const std::wstring key = path.wstring();
        {
            std::lock_guard lock(mutex_);
            if (stopping_) return;
            // The cache record is only a derivative of authoritative sidecars and
            // in-memory state. Keep one newest write per source path so a rapid
            // rating/face/EXIF burst cannot turn into thousands of SQLite commits.
            if (!recordWriteQueue_.contains(key) &&
                recordWriteQueue_.size() >= recordWriteQueueLimit_) {
                recordWriteQueue_.erase(recordWriteQueue_.begin());
            }
            recordWriteQueue_.insert_or_assign(key, std::move(write));
        }
        cacheCv_.notify_one();
    }

    void Worker::RequestDecoderSessionTrim() {
        decoderSessionTrimEpoch_.fetch_add(1, std::memory_order_acq_rel);
        cv_.notify_all();
    }

    size_t Worker::AnalysisBacklog() {
        std::lock_guard lock(mutex_);
        return analysisQueue_.size() + analysisInFlight_;
    }

    void Worker::EnqueuePreview(const fs::path& path, int targetSize, uint64_t generation,
        JobPriority priority, CacheClass cacheClass, uint64_t epoch, uint64_t viewportEpoch,
        int retryAttempt, bool forceWic, int timeoutMs, bool ignoreThumbnailViewport) {
        WorkJob job; job.kind = JobKind::DecodePreview; job.priority = priority; job.cacheClass = cacheClass;
        job.path = path; job.targetSize = targetSize; job.generation = generation;
        job.requestEpoch = epoch; job.viewportEpoch = viewportEpoch;
        job.retryAttempt = cacheClass == CacheClass::Thumbnail ? std::clamp(retryAttempt, 0, 7) : 0;
        job.timeoutMs = timeoutMs > 0 ? timeoutMs : CalculateAdaptiveTimeoutMs(path, job.retryAttempt, false);
        job.forceWic = cacheClass == CacheClass::Thumbnail && forceWic;
        job.ignoreThumbnailViewport = ignoreThumbnailViewport;
        job.rawJpegPreviewOnly = rawJpegPreviewOnly_.load(std::memory_order_acquire);
        Enqueue(std::move(job));
    }

    void Worker::EnqueueFull(const fs::path& path, int targetSize, uint64_t generation,
        JobPriority priority, CacheClass cacheClass, uint64_t epoch, uint64_t viewportEpoch,
        int retryAttempt, bool forceWic, bool isSingleView, uint64_t viewInteractionEpoch) {
        WorkJob job; job.kind = JobKind::DecodeFull; job.priority = priority; job.cacheClass = cacheClass;
        job.path = path; job.targetSize = targetSize; job.generation = generation;
        job.requestEpoch = epoch; job.viewportEpoch = viewportEpoch;
        job.viewInteractionEpoch = viewInteractionEpoch ? viewInteractionEpoch : currentViewInteractionEpoch_.load(std::memory_order_acquire);
        job.retryAttempt = std::clamp(retryAttempt, 0, 7);
        job.forceWic = forceWic;
        job.timeoutMs = CalculateAdaptiveTimeoutMs(path, job.retryAttempt, isSingleView);
        job.rawJpegPreviewOnly = rawJpegPreviewOnly_.load(std::memory_order_acquire);
        Enqueue(std::move(job));
    }

    void Worker::EnqueueTile(const fs::path& path, int x, int y, int width, int height,
        int level, uint64_t generation, uint64_t epoch) {
        WorkJob job; job.kind = JobKind::DecodeTile; job.priority = JobPriority::Interactive; job.cacheClass = CacheClass::Tile;
        job.path = path; job.targetSize = kTileSourceSize; job.generation = generation; job.requestEpoch = epoch;
        job.tileX = x; job.tileY = y; job.tileWidth = width; job.tileHeight = height;
        job.tileLevel = std::clamp(level, 0, 8);
        job.rawJpegPreviewOnly = rawJpegPreviewOnly_.load(std::memory_order_acquire);
        Enqueue(std::move(job));
    }

    bool Worker::PromoteJob(const std::wstring& key, CacheClass cacheClass) {
        std::lock_guard lock(mutex_);
        auto promote = [&](std::deque<WorkJob>& queue) {
            for (auto it = queue.begin(); it != queue.end(); ++it) {
                if (it->key == key && it->cacheClass == cacheClass) {
                    WorkJob job = std::move(*it);
                    queue.erase(it);
                    job.priority = JobPriority::Interactive;
                    interactiveQueue_.push_front(std::move(job));
                    cv_.notify_all();
                    return true;
                }
            }
            return false;
        };
        return promote(visibleQueue_) || promote(predictiveQueue_) || promote(faceQueue_) ||
            promote(idleQueue_) || promote(analysisQueue_);
    }

    void Worker::PurgeSpeculative(CacheClass cacheClass) {
        std::lock_guard lock(mutex_);
        auto eraseSpeculative = [&](std::deque<WorkJob>& queue) {
            for (auto it = queue.begin(); it != queue.end();) {
                if (it->cacheClass == cacheClass && it->priority == JobPriority::Predictive) {
                    pending_.erase(it->key.empty() ? JobKey(*it) : it->key);
                    it = queue.erase(it);
                } else ++it;
            }
        };
        eraseSpeculative(predictiveQueue_);
        eraseSpeculative(idleQueue_);
        cv_.notify_all();
    }

    void Worker::SetFocusKey(const std::wstring& key) {
        std::lock_guard lock(mutex_);
        currentFocusKey_ = key;
    }

    void Worker::SetVisualPriorityPaths(const std::vector<std::wstring>& paths) {
        std::lock_guard lock(mutex_);
        visualPriorityPaths_ = paths;
        currentFocusKey_ = paths.empty() ? std::wstring{} : paths.front();
        if (!visualPriorityPaths_.empty() && !interactiveQueue_.empty()) {
            std::deque<WorkJob> preferred;
            std::deque<WorkJob> rest;
            for (WorkJob& queued : interactiveQueue_) {
                const bool match = std::any_of(visualPriorityPaths_.begin(), visualPriorityPaths_.end(),
                    [&](const std::wstring& path) {
                        return _wcsicmp(queued.path.c_str(), path.c_str()) == 0;
                    });
                if (match && (queued.kind == JobKind::DecodeFull || queued.kind == JobKind::DecodePreview))
                    preferred.push_back(std::move(queued));
                else
                    rest.push_back(std::move(queued));
            }
            interactiveQueue_.clear();
            // Focus path first, then the other compared images, then unrelated work.
            auto take = [&](const std::wstring& path) {
                for (auto it = preferred.begin(); it != preferred.end();) {
                    if (_wcsicmp(it->path.c_str(), path.c_str()) == 0) {
                        interactiveQueue_.push_back(std::move(*it));
                        it = preferred.erase(it);
                    } else {
                        ++it;
                    }
                }
            };
            for (const std::wstring& path : visualPriorityPaths_) take(path);
            for (WorkJob& queued : preferred) interactiveQueue_.push_back(std::move(queued));
            for (WorkJob& queued : rest) interactiveQueue_.push_back(std::move(queued));
        }
        cv_.notify_all();
    }

    bool Worker::IsQueueEmpty() const {
        std::lock_guard lock(mutex_);
        return interactiveQueue_.empty() && visibleQueue_.empty() && predictiveQueue_.empty() &&
            faceQueue_.empty() && idleQueue_.empty() && analysisQueue_.empty();
    }

    int Worker::CalculateAdaptiveTimeoutMs(const fs::path& path, int attempt, bool isSingleView) const {
        int base = 1000;
        if (attempt >= 2 && attempt < 4) base = 2000;
        else if (attempt >= 4 && attempt < 6) base = 4000;
        else if (attempt >= 6) base = 8000;
        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        std::wstring ext = path.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        const bool large = !ec && size > 10ull * 1024ull * 1024ull;
        const bool raw = IsRawExtension(ext);
        if (large || raw) base = static_cast<int>(base * 1.5);
        if (isSingleView) {
            // Full-resolution Single View decodes can legitimately take many seconds
            // on large JPEG/TIFF/RAW files. The old 2x multiplier still produced
            // ~3 seconds for a large file, which cancelled valid native-resolution
            // requests before they could reach the UI. Give the foreground view a
            // real decode budget while keeping malformed/stalled codecs bounded.
            base = std::max(base * 4, large || raw ? 12000 : 8000);
        }
        return std::clamp(base, 1000, 60000);
    }

    void Worker::PromoteQueuedMetadata(uint64_t generation, JobPriority priority) {
        // Metadata has a dedicated lane. Do not promote it into image/decode queues.
        (void)generation;
        (void)priority;
        cv_.notify_all();
    }

    [[nodiscard]] bool Worker::EnqueueMetadata(const fs::path& path, uint64_t generation,
        uint64_t metadataRevision, JobPriority priority) {
        WorkJob job; job.kind = JobKind::Metadata; job.priority = priority;
        job.path = path; job.generation = generation; job.metadataRevision = metadataRevision;
        if (job.enqueuedAt == std::chrono::steady_clock::time_point{})
            job.enqueuedAt = std::chrono::steady_clock::now();
        const std::wstring key = JobKey(job);
        std::lock_guard lock(mutex_);
        if (stopping_ || pending_.contains(key)) return false;
        pending_.insert(key);
        metadataQueue_.push_back(std::move(job));
        cv_.notify_all();
        return true;
    }

    void Worker::EnqueueExif(const fs::path& path, uint64_t generation) {
        // EXIF is useful, but never more important than getting pixels on screen.
        WorkJob job; job.kind = JobKind::Exif; job.priority = JobPriority::Idle;
        job.path = path; job.generation = generation; Enqueue(std::move(job));
    }

    void Worker::EnqueueFace(const fs::path& path, uint64_t generation, uint64_t epoch,
        JobPriority priority, int targetSize ) {
        WorkJob job; job.kind = JobKind::Face; job.priority = priority; job.path = path;
        job.targetSize = std::max(kFaceDecodeSize, targetSize);
        job.generation = generation; job.requestEpoch = epoch;
        job.rawJpegPreviewOnly = rawJpegPreviewOnly_.load(std::memory_order_acquire);
        Enqueue(std::move(job));
    }

    void Worker::ClearOlderThan(uint64_t generation) {
        std::lock_guard lock(mutex_);
        auto clearQueue = [&](std::deque<WorkJob>& queue) {
            for (auto it = queue.begin(); it != queue.end();) {
                if (it->generation != generation) {
                    pending_.erase(it->key.empty() ? JobKey(*it) : it->key);
                    it = queue.erase(it);
                }
                else ++it;
            }
        };
        clearQueue(interactiveQueue_); clearQueue(visibleQueue_); clearQueue(predictiveQueue_); clearQueue(faceQueue_); clearQueue(idleQueue_); clearQueue(analysisQueue_); clearQueue(metadataQueue_);
        for (auto it = cacheWriteQueue_.begin(); it != cacheWriteQueue_.end();) {
            if (it->generation != generation) {
                cacheWriteBytes_ = it->bytes > cacheWriteBytes_ ? 0 : cacheWriteBytes_ - it->bytes;
                it = cacheWriteQueue_.erase(it);
            } else {
                ++it;
            }
        }
    }
void Worker::AcknowledgePendingKey(const std::wstring& key) {
        if (key.empty()) return;
        std::lock_guard lock(mutex_);
        pending_.erase(key);
    }

    void Worker::ReleaseDesiredVisualTarget(const fs::path& path, JobKind kind, CacheClass cacheClass,
        uint64_t generation, uint64_t requestEpoch, uint64_t viewportEpoch, int targetSize,
        bool rawJpegPreviewOnly) {
        WorkJob completed;
        completed.path = path;
        completed.kind = kind;
        completed.cacheClass = cacheClass;
        completed.generation = generation;
        completed.requestEpoch = requestEpoch;
        completed.viewportEpoch = viewportEpoch;
        completed.targetSize = targetSize;
        completed.rawJpegPreviewOnly = rawJpegPreviewOnly;
        if (!IsCoalescibleVisualJob(completed)) return;
        const std::wstring family = VisualFamilyKey(completed);
        std::lock_guard desiredLock(desiredMutex_);
        const auto desired = desiredVisualTargets_.find(family);
        if (desired != desiredVisualTargets_.end() && desired->second <= targetSize)
            desiredVisualTargets_.erase(desired);
    }

    void Worker::AcknowledgeUnpostedJob(const WorkJob& job, const std::wstring& key) {
        AcknowledgePendingKey(key);
        ReleaseDesiredVisualTarget(job.path, job.kind, job.cacheClass,
            job.generation, job.requestEpoch, job.viewportEpoch, job.targetSize,
            job.rawJpegPreviewOnly);
    }

    bool Worker::IsSupersededByLargerDecode(const WorkJob& job) const {
        if (!IsCoalescibleVisualJob(job)) return false;
        const std::wstring family = VisualFamilyKey(job);
        std::lock_guard desiredLock(desiredMutex_);
        const auto desired = desiredVisualTargets_.find(family);
        return desired != desiredVisualTargets_.end() && desired->second > job.targetSize;
    }

    std::unordered_map<std::wstring, unsigned>::const_iterator
Worker::FindBlockedPathLocked(const wchar_t* path) const {
        return std::find_if(blockedPaths_.begin(), blockedPaths_.end(),
            [&](const auto& item) { return _wcsicmp(item.first.c_str(), path) == 0; });
    }

    std::unordered_map<std::wstring, unsigned>::iterator
Worker::FindBlockedPathLocked(const wchar_t* path) {
        return std::find_if(blockedPaths_.begin(), blockedPaths_.end(),
            [&](const auto& item) { return _wcsicmp(item.first.c_str(), path) == 0; });
    }

    bool Worker::JobTimedOut(const WorkJob& job) const noexcept {
        // A freshly queued job has no execution start timestamp yet. Treat that
        // state as not timed out; otherwise the default epoch time_point would
        // make every newly dequeued job appear millions of seconds old.
        if (job.startedAt == std::chrono::steady_clock::time_point{}) return false;
        if (job.timeoutMs <= 0) return false;
        const auto elapsed = std::chrono::steady_clock::now() - job.startedAt;
        const auto dynamicLimit = std::chrono::milliseconds(job.timeoutMs);
        return elapsed >= dynamicLimit;
    }

    bool Worker::IsCancelled(const WorkJob& job) const {
        if (stoppingAtomic_.load(std::memory_order_acquire)) return true;
        // Scrollbar dragging deliberately suspends thumbnail decode. A running
        // thumbnail observes this through the decoder cancellation callback so
        // the UI is not held hostage by stale image work.
        if (thumbnailDragActive_.load(std::memory_order_acquire) &&
            thumbnailDragLoadMode_.load(std::memory_order_acquire) <= 0 &&
            job.cacheClass == CacheClass::Thumbnail &&
            (job.kind == JobKind::DecodePreview || job.kind == JobKind::DecodeFull)) return true;
        // Codec operations are untrusted from the worker's perspective: a malformed
        // image or a decoder/driver stall must never occupy a foreground worker
        // indefinitely. WIC receives this signal through its progress callback.
        // TurboJPEG cannot be force-aborted in-process, so its path remains bounded
        // by the decoder choice/concurrency policy rather than pretending this check
        // can interrupt tjDecompress2 itself.
        if (JobTimedOut(job)) return true;
        // A metadata write has claimed this path. Abort the in-flight decode at the
        // next checkpoint so rating/pick updates are not stuck behind a full demosaic.
        if ((job.kind == JobKind::DecodePreview || job.kind == JobKind::DecodeFull ||
                job.kind == JobKind::DecodeTile || job.kind == JobKind::Face ||
                job.kind == JobKind::Exif) &&
            IsPathBlockedForDecoder(job.path)) return true;
        if ((job.kind == JobKind::DecodePreview || job.kind == JobKind::DecodeFull ||
            job.kind == JobKind::DecodeTile || job.kind == JobKind::Face) &&
            IsRawExtension(ExtensionLower(job.path)) &&
            job.rawJpegPreviewOnly != rawJpegPreviewOnly_.load(std::memory_order_acquire)) return true;
        if (interactiveVisualFocus_.load(std::memory_order_acquire) &&
            (job.kind == JobKind::DecodePreview || job.kind == JobKind::DecodeFull ||
                job.kind == JobKind::DecodeTile || job.kind == JobKind::Face) &&
            job.priority != JobPriority::Interactive) {
            return true;
        }
        if (thumbnailViewportActive_.load(std::memory_order_acquire) &&
            job.kind == JobKind::Face) {
            // Face analysis is useful background work, but it must not consume a
            // decoder/worker slot while the visible thumbnail window is active.
            return true;
        }
        if (job.kind == JobKind::DecodePreview || job.kind == JobKind::DecodeFull ||
            job.kind == JobKind::DecodeTile || job.kind == JobKind::Face ||
            job.kind == JobKind::Metadata || job.kind == JobKind::Exif) {
            std::lock_guard blockedLock(blockedMutex_);
            if (FindBlockedPathLocked(job.path.c_str()) != blockedPaths_.end()) return true;
        }
        if (job.generation != activeGeneration_.load(std::memory_order_acquire)) return true;
        const int thumbnailTarget = activeThumbnailTarget_.load(std::memory_order_acquire);
        if (job.cacheClass == CacheClass::Thumbnail && thumbnailTarget > 0 &&
            job.targetSize != thumbnailTarget) return true;
        const uint64_t currentViewportEpoch = thumbnailViewportEpoch_.load(std::memory_order_acquire);
        if (job.cacheClass == CacheClass::Thumbnail && !job.ignoreThumbnailViewport &&
            job.viewportEpoch != 0 && job.viewportEpoch < currentViewportEpoch) {
            bool stillDesired = false;
            try {
                const std::wstring key = ThumbnailPathKey(job.path);
                std::lock_guard viewportLock(thumbnailViewportMutex_);
                // Predictive rows are part of the current desired set and must be
                // allowed to finish. Cancel only when the thumbnail is no longer
                // desired by the current prefetch plan.
                stillDesired = desiredThumbnailPaths_.contains(key);
            } catch (...) {
            }
            // Queued stale work is discarded immediately. During fling (cache-only
            // admission) cancel in-flight off-viewport work with no grace so successive
            // jumps cannot keep decoding abandoned cells. Outside fling, a short grace
            // avoids starving thumbnails when the viewport jitters by a row.
            if (!stillDesired) {
                // loadMode <= 0 is the UI fling/cache-only admission signal.
                const bool flingCancel =
                    thumbnailDragActive_.load(std::memory_order_acquire) &&
                    thumbnailDragLoadMode_.load(std::memory_order_acquire) <= 0;
                if (flingCancel) return true;
                const auto now = std::chrono::steady_clock::now();
                if (job.startedAt == std::chrono::steady_clock::time_point{}) return true;
                const int dynamicGraceMs = std::clamp(job.targetSize / 4, 20, 400);
                if (now - job.startedAt >= std::chrono::milliseconds(dynamicGraceMs)) return true;
            }
        }
        if (job.priority == JobPriority::Predictive &&
            !allowPredictive_.load(std::memory_order_acquire)) return true;
        if (job.priority == JobPriority::Analysis &&
            !allowAnalysis_.load(std::memory_order_acquire)) return true;
        if (job.viewInteractionEpoch != 0 &&
            job.viewInteractionEpoch < currentViewInteractionEpoch_.load(std::memory_order_acquire)) return true;
        if (IsSupersededByLargerDecode(job)) return true;
        const uint64_t currentEpoch = navigationEpoch_.load(std::memory_order_acquire);
        return quicksift::review::IsStaleNavigationRequest(
            job.requestEpoch, currentEpoch, job.kind, true);
    }

    size_t Worker::EstimatedDecodeReservation(const WorkJob& job) const {
        if (job.kind == JobKind::Metadata || job.kind == JobKind::Exif) return 0;

        const std::uint64_t maximumPixels =
            gMaximumDecodedPixelBytes.load(std::memory_order_relaxed);
        const std::uint64_t edge = static_cast<std::uint64_t>(std::max(64, job.targetSize));
        std::uint64_t estimate = edge > UINT64_MAX / edge / 4ull ?
            maximumPixels : edge * edge * 4ull;

        if (job.kind == JobKind::DecodeTile) {
            // WIC may still retain decoder state and scanline buffers even when a
            // small clipped tile is requested. Four MiB described only the final
            // tile and let several huge source files hide their working sets.
            estimate = std::clamp<std::uint64_t>(estimate, 8ull * kMebibyte, 24ull * kMebibyte);
        } else if (job.kind == JobKind::Face) {
            // Face analysis creates a DataWriter buffer and then a SoftwareBitmap
            // copy in addition to the decoded BGRA preview. Keep those hidden copies
            // inside the same reservation instead of charging only the 512 px output.
            const std::uint64_t multiplied = estimate > UINT64_MAX / 4ull ? UINT64_MAX : estimate * 4ull;
            estimate = std::clamp<std::uint64_t>(multiplied, 8ull * kMebibyte, maximumPixels);
        } else {
            estimate += std::min<std::uint64_t>(estimate / 4, 64ull * kMebibyte);
            estimate = std::min(estimate, maximumPixels);
        }

        // Output pixels are not the whole decoder footprint. WIC, Shell, LibRaw,
        // libavif and libwebp may retain compressed input, source planes, scanline
        // caches and color-conversion workspaces while producing a small preview.
        // Use a conservative file-size allowance so multiple workers cannot hide
        // several hundred MiB behind 256–512 px result buffers.
        const std::wstring extension = ExtensionLower(job.path);
        const bool raw = IsRawExtension(extension);
        const bool avif = extension == L".avif";
        const bool webp = extension == L".webp";
        std::error_code ec;
        const std::uint64_t fileBytes = fs::file_size(job.path, ec);
        const std::uint64_t safeFileBytes = ec ? 0 : fileBytes;

        std::uint64_t multiplier = 4;
        std::uint64_t minimumWorkspace = 24ull * kMebibyte;
        std::uint64_t workspaceCap = maximumPixels;
        if (raw) {
            multiplier = 6;
            minimumWorkspace = 128ull * kMebibyte;
        } else if (avif || extension == L".heic" || extension == L".heif") {
            multiplier = 10;
            minimumWorkspace = avif ? 192ull * kMebibyte : 128ull * kMebibyte;
        } else if (webp) {
            multiplier = 2;
            minimumWorkspace = 32ull * kMebibyte;
        } else if (extension == L".jpg" || extension == L".jpeg" || extension == L".jpe") {
            multiplier = 10;
            minimumWorkspace = 32ull * kMebibyte;
        } else if (extension == L".png") {
            multiplier = 12;
            minimumWorkspace = 48ull * kMebibyte;
        } else if (extension == L".tif" || extension == L".tiff") {
            multiplier = 4;
            minimumWorkspace = 64ull * kMebibyte;
        } else if (extension == L".bmp" || extension == L".dib" || extension == L".gif") {
            multiplier = 2;
            minimumWorkspace = 16ull * kMebibyte;
        }
        if (job.kind == JobKind::DecodeTile) workspaceCap = std::min<std::uint64_t>(workspaceCap, 96ull * kMebibyte);

        const std::uint64_t multiplied = safeFileBytes > workspaceCap / std::max<std::uint64_t>(1, multiplier) ?
            workspaceCap : safeFileBytes * multiplier;
        const std::uint64_t workspace = std::clamp<std::uint64_t>(
            multiplied, std::min(minimumWorkspace, workspaceCap), workspaceCap);
        const std::uint64_t combinedCap = maximumPixels > UINT64_MAX - 128ull * kMebibyte ?
            UINT64_MAX : maximumPixels + 128ull * kMebibyte;
        estimate = std::min(combinedCap,
            workspace > UINT64_MAX - estimate ? UINT64_MAX : workspace + estimate);
        return static_cast<size_t>(std::min<std::uint64_t>(estimate, SIZE_MAX));
    }

    bool Worker::DecodeWithBundledGuarded(const WorkJob& job, WorkResult& result,
        bool preferEmbeddedPreview ) {
        std::unique_lock lock(heavyBundledDecodeMutex_, std::defer_lock);
        if (serializeHeavyBundledDecodes_) lock.lock();
        quicksift::DecodeOptions options;
        options.targetSize = job.targetSize;
        options.preferEmbeddedPreview = preferEmbeddedPreview;
        options.rawJpegPreviewOnly = job.rawJpegPreviewOnly &&
            IsRawExtension(ExtensionLower(job.path));
        options.allowTurboJpeg = profile_.enableTurboJpeg;
        options.prefetchEncodedInput = job.priority == JobPriority::Interactive ||
            job.priority == JobPriority::Visible || job.priority == JobPriority::Predictive;
        options.mappedInputLimit = mappedInputLimit_.load(std::memory_order_acquire);
        options.isCancelled = [this, job] { return IsCancelled(job); };
        return DecodeWithBundled(job.path, options, result);
    }

    bool Worker::Enqueue(WorkJob job) {
        if (thumbnailDragActive_.load(std::memory_order_acquire) &&
            job.cacheClass == CacheClass::Thumbnail &&
            (job.kind == JobKind::DecodePreview || job.kind == JobKind::DecodeFull)) {
            const int dragMode = thumbnailDragLoadMode_.load(std::memory_order_acquire);
            const unsigned maxDragThumbnailWork =
                thumbnailScrollConcurrentCap_.load(std::memory_order_acquire);
            if (dragMode <= 0 || maxDragThumbnailWork == 0) {
                // Fast fling/drag: UI only. Newly exposed thumbnails wait until motion settles.
                return false;
            }
            // Coasting/slow motion: allow a size-capped rolling workload that never
            // accumulates behind the pointer. The UI remains the absolute priority.
            if (activeThumbnailJobs_.load(std::memory_order_acquire) >= maxDragThumbnailWork) return false;
            std::lock_guard queueLock(mutex_);
            std::size_t queued = 0;
            auto countQueued = [&](const std::deque<WorkJob>& queue) {
                for (const auto& queuedJob : queue) {
                    if (queuedJob.cacheClass == CacheClass::Thumbnail &&
                        (queuedJob.kind == JobKind::DecodePreview || queuedJob.kind == JobKind::DecodeFull)) {
                        ++queued;
                        if (queued >= maxDragThumbnailWork) return;
                    }
                }
            };
            countQueued(interactiveQueue_);
            if (queued < maxDragThumbnailWork) countQueued(visibleQueue_);
            if (queued >= maxDragThumbnailWork) return false;
        }
        const bool foregroundVisual = (job.kind == JobKind::DecodePreview ||
            job.kind == JobKind::DecodeFull || job.kind == JobKind::DecodeTile) &&
            (job.priority == JobPriority::Interactive || job.priority == JobPriority::Visible);
        if (foregroundVisual) readAhead_.Pause();
        if (job.priority == JobPriority::Predictive &&
            !allowPredictive_.load(std::memory_order_acquire)) return false;
        if (job.priority == JobPriority::Analysis &&
            !allowAnalysis_.load(std::memory_order_acquire)) return false;
        if (job.enqueuedAt == std::chrono::steady_clock::time_point{})
            job.enqueuedAt = std::chrono::steady_clock::now();
        const bool handleHolding = job.kind == JobKind::DecodePreview ||
            job.kind == JobKind::DecodeFull || job.kind == JobKind::DecodeTile ||
            job.kind == JobKind::Face || job.kind == JobKind::Metadata ||
            job.kind == JobKind::Exif;
        if (handleHolding) {
            std::lock_guard blockedLock(blockedMutex_);
            if (FindBlockedPathLocked(job.path.c_str()) != blockedPaths_.end()) return false;
        }
        if (job.key.empty()) job.key = JobKey(job);
        const std::wstring& key = job.key;
        {
            std::lock_guard lock(mutex_);
            if (IsCoalescibleVisualJob(job)) {
                const std::wstring family = VisualFamilyKey(job);
                std::lock_guard desiredLock(desiredMutex_);
                int& desired = desiredVisualTargets_[family];
                desired = std::max(desired, job.targetSize);
            }
            if (stopping_) return false;

            // Coalesce queued requests for the same image/quality family. A larger
            // bucket satisfies every smaller consumer, so rapid fit/zoom/layout
            // changes should upgrade one queued decode rather than reopen the file.
            if (IsCoalescibleVisualJob(job)) {
                auto queueForPriority = [&](JobPriority priority) -> std::deque<WorkJob>& {
                    switch (priority) {
                    case JobPriority::Interactive: return interactiveQueue_;
                    case JobPriority::Visible: return visibleQueue_;
                    case JobPriority::Predictive: return predictiveQueue_;
                    case JobPriority::Face: return faceQueue_;
                    case JobPriority::Idle: return idleQueue_;
                    case JobPriority::Analysis: return analysisQueue_;
                    }
                    return analysisQueue_;
                };
                auto coalesceIn = [&](std::deque<WorkJob>& queue) -> bool {
                    const auto it = std::find_if(queue.begin(), queue.end(), [&](const WorkJob& queued) {
                        return SameDecodeFamily(queued, job);
                    });
                    if (it == queue.end()) return false;
                    if (it->targetSize < job.targetSize) {
                        pending_.erase(it->key);
                        it->targetSize = job.targetSize;
                        it->key = JobKey(*it);
                        pending_.insert(it->key);
                    }
                    // Priority is monotonic: a later request may promote an
                    // existing decode, but it may never demote one that was
                    // already classified as more important. This is especially
                    // important for viewport thumbnails, which are Interactive.
                    if (IsHigherPriority(job.priority, it->priority)) {
                        PromoteQueuedJob(queue, it, queueForPriority(job.priority), job.priority);
                    }
                    return true;
                };
                if (coalesceIn(interactiveQueue_) || coalesceIn(visibleQueue_) ||
                    coalesceIn(predictiveQueue_) || coalesceIn(faceQueue_) || coalesceIn(idleQueue_) || coalesceIn(analysisQueue_)) {
                    cv_.notify_all();
                    return false;
                }
            }
            if (IsCoalescibleVisualJob(job)) {
                const std::wstring family = VisualFamilyKey(job);
                const auto active = activeVisualFamilies_.find(family);
                if (active != activeVisualFamilies_.end() && active->second >= job.targetSize) {
                    return false;
                }
            }
            if (pending_.contains(key)) {
                auto promoteFrom = [&](std::deque<WorkJob>& queue, std::deque<WorkJob>& destination) {
                    const auto it = std::find_if(queue.begin(), queue.end(), [&](const WorkJob& queued) {
                        return queued.key == key;
                    });
                    if (it == queue.end()) return false;
                    WorkJob promoted = std::move(*it);
                    queue.erase(it);
                    promoted.priority = job.priority;
                    promoted.requestEpoch = std::max(promoted.requestEpoch, job.requestEpoch);
                    destination.push_front(std::move(promoted));
                    return true;
                };
                if (job.priority == JobPriority::Interactive) {
                    if (!promoteFrom(analysisQueue_, interactiveQueue_) &&
                        !promoteFrom(idleQueue_, interactiveQueue_) &&
                        !promoteFrom(faceQueue_, interactiveQueue_) &&
                        !promoteFrom(predictiveQueue_, interactiveQueue_)) {
                        promoteFrom(visibleQueue_, interactiveQueue_);
                    }
                } else if (job.priority == JobPriority::Visible) {
                    if (!promoteFrom(analysisQueue_, visibleQueue_) &&
                        !promoteFrom(idleQueue_, visibleQueue_) &&
                        !promoteFrom(faceQueue_, visibleQueue_)) {
                        promoteFrom(predictiveQueue_, visibleQueue_);
                    }
                } else if (job.priority == JobPriority::Predictive) {
                    if (!promoteFrom(analysisQueue_, predictiveQueue_) &&
                        !promoteFrom(idleQueue_, predictiveQueue_)) {
                        promoteFrom(faceQueue_, predictiveQueue_);
                    }
                } else if (job.priority == JobPriority::Face) {
                    if (!promoteFrom(analysisQueue_, faceQueue_)) promoteFrom(idleQueue_, faceQueue_);
                } else if (job.priority == JobPriority::Idle) {
                    promoteFrom(analysisQueue_, idleQueue_);
                }
                cv_.notify_all();
                return false;
            }
            pending_.insert(key);
            if (job.kind == JobKind::Metadata) {
                metadataQueue_.push_back(std::move(job));
                cv_.notify_all();
                return true;
            }
            switch (job.priority) {
            case JobPriority::Interactive:
                if (!visualPriorityPaths_.empty() &&
                    (job.kind == JobKind::DecodeFull || job.kind == JobKind::DecodePreview) &&
                    std::any_of(visualPriorityPaths_.begin(), visualPriorityPaths_.end(),
                        [&](const std::wstring& path) {
                            return _wcsicmp(job.path.c_str(), path.c_str()) == 0;
                        })) {
                    interactiveQueue_.push_front(std::move(job));
                } else {
                    interactiveQueue_.push_back(std::move(job));
                }
                break;
            case JobPriority::Visible: visibleQueue_.push_back(std::move(job)); break;
            case JobPriority::Predictive: predictiveQueue_.push_back(std::move(job)); break;
            case JobPriority::Face: faceQueue_.push_back(std::move(job)); break;
            case JobPriority::Idle: idleQueue_.push_back(std::move(job)); break;
            case JobPriority::Analysis: analysisQueue_.push_back(std::move(job)); break;
            }
        }
        cv_.notify_all();
        RefreshReadAheadGate();
        return true;
    }

    WorkJob Worker::PopWork(bool allowIdle) {
        WorkJob job;
        const bool idleReady = allowIdle && std::chrono::steady_clock::now() >= idleNotBefore_;
        const bool tileCapReached = activeTileJobs_.load(std::memory_order_acquire) >= 4;
        auto popEligible = [&](std::deque<WorkJob>& queue) -> bool {
            if (queue.empty()) return false;
            if (!tileCapReached) {
                job = std::move(queue.front());
                queue.pop_front();
                return true;
            }
            // Keep tile decodes from occupying every worker while a pathological
            // tile is stalled. If the front is a tile, look for another image/metadata
            // job in the same priority lane that can make progress instead.
            const auto it = std::find_if(queue.begin(), queue.end(), [](const WorkJob& candidate) {
                return candidate.kind != JobKind::DecodeTile;
            });
            if (it == queue.end()) return false;
            job = std::move(*it);
            queue.erase(it);
            return true;
        };
        if (popEligible(interactiveQueue_)) {
            // foreground burst accounting is intentionally unchanged
        } else if (popEligible(visibleQueue_)) {
        } else if (ShouldServiceIdleAfterForegroundBurst(false, false, idleReady,
            !idleQueue_.empty(), foregroundBurstSinceIdle_)) {
            if (!popEligible(idleQueue_)) return {};
            foregroundBurstSinceIdle_ = 0;
        } else if (popEligible(predictiveQueue_)) {
            ++foregroundBurstSinceIdle_;
        } else if (popEligible(faceQueue_)) {
            ++foregroundBurstSinceIdle_;
        } else if (idleReady && popEligible(idleQueue_)) {
            foregroundBurstSinceIdle_ = 0;
        } else if (idleReady && allowAnalysis_.load(std::memory_order_acquire) &&
            popEligible(analysisQueue_)) {
            ++analysisInFlight_;
            foregroundBurstSinceIdle_ = 0;
        } else {
            return {};
        }
        if (job.kind == JobKind::DecodePreview || job.kind == JobKind::DecodeFull ||
            job.kind == JobKind::DecodeTile || job.kind == JobKind::Face) {
            const std::wstring family = VisualFamilyKey(job);
            activeVisualFamilies_[family] = std::max(activeVisualFamilies_[family], job.targetSize);
        }
        if (job.kind == JobKind::DecodeTile) {
            // Reserve the tile slot while mutex_ is held so multiple worker threads
            // cannot observe the cap as available and dequeue more than four tiles.
            activeTileJobs_.fetch_add(1, std::memory_order_acq_rel);
        }
        return job;
    }

    void Worker::FinishAnalysisJob(const WorkJob& job) {
        if (job.priority != JobPriority::Analysis) return;
        std::lock_guard lock(mutex_);
        if (analysisInFlight_ > 0) --analysisInFlight_;
    }

    bool Worker::FileFingerprint(const fs::path& path, uint64_t& size, int64_t& modified) {
        std::error_code ec;
        size = fs::file_size(path, ec); if (ec) return false;
        modified = quicksift::PersistentCache::SourceStampFor(path);
        return modified != 0;
    }

    void Worker::QueueDerivedCacheLevels(IWICImagingFactory* factory, const WorkJob& sourceJob,
        uint64_t fileSize, int64_t modifiedStamp, int64_t sidecarStamp,
        const std::shared_ptr<WorkResult>& source) {
        if (!factory || !persistentCache_ || !source || source->fromPersistentCache ||
            source->pixels.empty() || !allowDerivatives_.load(std::memory_order_acquire) ||
            runtimeDerivativeLevels_.load(std::memory_order_acquire) == 0 ||
            sourceJob.kind == JobKind::DecodeTile || sourceJob.kind == JobKind::Face ||
            sourceJob.targetSize <= 512 || sourceJob.rawJpegPreviewOnly || IsCancelled(sourceJob)) return;

        int target = std::min(2048, sourceJob.targetSize / 2);
        const unsigned derivativeLevels = runtimeDerivativeLevels_.load(std::memory_order_acquire);
        for (unsigned level = 0; level < derivativeLevels && target >= 256; ++level) {
            // Round to the same decode buckets used by the renderer so later lookups
            // hit exactly instead of creating near-duplicate cache files.
            static constexpr int buckets[] = { 256, 512, 1024, 2048 };
            int roundedTarget = 256;
            for (int bucket : buckets) {
                if (bucket <= target) roundedTarget = bucket;
                else break;
            }
            const double scale = std::min(static_cast<double>(roundedTarget) / source->width,
                static_cast<double>(roundedTarget) / source->height);
            const std::uint64_t outW = std::max<std::uint64_t>(1,
                static_cast<std::uint64_t>(std::llround(source->width * scale)));
            const std::uint64_t outH = std::max<std::uint64_t>(1,
                static_cast<std::uint64_t>(std::llround(source->height * scale)));
            const std::uint64_t bytes64 = outW > UINT64_MAX / outH / 4ull ? UINT64_MAX : outW * outH * 4ull;
            const std::uint64_t extraBytes = bytes64 / 4ull;
            const std::uint64_t reservation64 = bytes64 > UINT64_MAX - extraBytes ?
                UINT64_MAX : bytes64 + extraBytes;
            auto reservation = decodeBudget_->TryAcquire(static_cast<size_t>(
                std::min<std::uint64_t>(reservation64, SIZE_MAX)), DecodePriority::Idle);
            if (!reservation) break;

            auto derivative = std::make_shared<WorkResult>();
            derivative->kind = JobKind::DecodeFull;
            derivative->priority = JobPriority::Idle;
            derivative->cacheClass = roundedTarget <= 512 ? CacheClass::Thumbnail : CacheClass::Preview;
            derivative->path = sourceJob.path;
            derivative->targetSize = roundedTarget;
            derivative->generation = sourceJob.generation;
            derivative->requestEpoch = sourceJob.requestEpoch;
            derivative->viewportEpoch = sourceJob.viewportEpoch;
            derivative->decodeReservation = std::move(reservation);
            if (!CreateWicDerivative(factory, *source, roundedTarget, *derivative)) break;
            derivative->decodeReservation->ShrinkTo(derivative->pixels.size());

            WorkJob derivativeJob;
            derivativeJob.kind = JobKind::DecodeFull;
            derivativeJob.priority = JobPriority::Idle;
            derivativeJob.cacheClass = derivative->cacheClass;
            derivativeJob.path = sourceJob.path;
            derivativeJob.targetSize = roundedTarget;
            derivativeJob.generation = sourceJob.generation;
            derivativeJob.requestEpoch = sourceJob.requestEpoch;
            derivativeJob.viewportEpoch = sourceJob.viewportEpoch;
            derivativeJob.rawJpegPreviewOnly = sourceJob.rawJpegPreviewOnly;
            QueueBitmapCacheWrite(derivativeJob, fileSize, modifiedStamp, sidecarStamp, derivative);
            if (roundedTarget <= 256) break;
            target = roundedTarget / 2;
        }
    }

    bool Worker::TryPostMetadataAbort(const WorkJob& job) {
        // Metadata queue accounting belongs to the UI. Even cancellation by a
        // path-scoped write or revision change needs one lightweight terminal
        // envelope so the bounded metadata pump can release its in-flight slot.
        if (job.kind != JobKind::Metadata || !completionQueue_) return false;
        bool transitionedFromEmpty = false;
        if (!completionQueue_->PushWait(
                quicksift::work::BackgroundCompletion::ForGeneration(
                    quicksift::work::BackgroundCompletionKind::MetadataJobAborted,
                    job.generation), stoppingAtomic_, &transitionedFromEmpty)) {
            return false;
        }
        if (transitionedFromEmpty) {
            HWND notify = nullptr;
            {
                std::lock_guard lock(mutex_);
                notify = notify_;
            }
            PostBackgroundWakeWithRetry(notify, WM_APP_BACKGROUND_COMPLETION, 0, 0);
        }
        return true;
    }

    bool Worker::TryPostResult(const WorkJob& job, const std::shared_ptr<WorkResult>& result) {
        if (!result || (IsCancelled(job) && !result->workFailed) || IsSupersededByLargerDecode(job) || !completionQueue_) return false;
        bool transitionedFromEmpty = false;
        if (!completionQueue_->PushWait(
                quicksift::work::BackgroundCompletion::ForWorkResult(result),
                stoppingAtomic_, &transitionedFromEmpty)) {
            return false;
        }
        // A completion burst needs exactly one UI wake. During a scrollbar drag,
        // thumbnail results are serviced by the periodic UI timer instead, so they
        // deliberately do not create input-queue traffic.
        const bool suppressThumbnailWake =
            thumbnailDragActive_.load(std::memory_order_acquire) &&
            result->cacheClass == CacheClass::Thumbnail;
        if (transitionedFromEmpty && !suppressThumbnailWake) {
            HWND notify = nullptr;
            {
                std::lock_guard lock(mutex_);
                notify = notify_;
            }
            PostBackgroundWakeWithRetry(notify, WM_APP_BACKGROUND_COMPLETION, 0, 0);
        }
        return true;
    }

    void Worker::Run(bool allowIdle, size_t threadIndex, bool metadataOnly) {
        gThreadWorkerOwner = this;
        gThreadWorkerIndex = threadIndex;
        PublishThreadWicDecoderSessions();
        // RoInitialize also initializes COM for WIC/Shell work and makes WinRT face
        // analysis reliable on these background MTA threads.
        const HRESULT runtime = RoInitialize(RO_INIT_MULTITHREADED);
        const bool backgroundLane = allowIdle && !singleWorkerMode_ && metadataOnly;
        if (backgroundLane) quicksift::ApplyBackgroundThreadPolicy(profile_.cpuTopology, true);
        else quicksift::ApplyInteractiveThreadPolicy(profile_.cpuTopology);
        ComPtr<IWICImagingFactory> factory;
        std::uint64_t observedDecoderSessionTrim = decoderSessionTrimEpoch_.load(std::memory_order_acquire);
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));

        for (;;) {
            WorkJob job;
            {
                std::unique_lock lock(mutex_);
                for (;;) {
                    if (stopping_) break;
                    if (metadataOnly) {
                        // Metadata is strictly background work. Do not begin an
                        // Exiv2/XMP read while any visual decode is active or queued.
                        // A separate thread is not sufficient: both lanes still hit
                        // the same storage device, and RAW metadata can hold Exiv2's
                        // process-wide guard for a long time.
                        if (activeVisualJobs_.load(std::memory_order_acquire) != 0 ||
                            !interactiveQueue_.empty() || !visibleQueue_.empty() ||
                            exclusivePathWrites_.load(std::memory_order_acquire) != 0 ||
                            quicksift::EmbeddedMetadataWritePreferred()) {
                            // Viewing and culling writes outrank metadata reads.
                            // Do not start another Exiv2 call while a rating/pick
                            // batch is waiting or while a visible decode is active.
                            cv_.wait_for(lock, std::chrono::milliseconds(8));
                            continue;
                        }
                        if (!metadataQueue_.empty()) {
                            job = std::move(metadataQueue_.front());
                            metadataQueue_.pop_front();
                            break;
                        }
                        cv_.wait(lock);
                        continue;
                    }
                    const std::uint64_t requestedTrim = decoderSessionTrimEpoch_.load(std::memory_order_acquire);
                    if (requestedTrim != observedDecoderSessionTrim) {
                        lock.unlock();
                        if (exclusivePathWrites_.load(std::memory_order_acquire) != 0)
                            ReleaseBlockedThreadWicDecoderSessions();
                        else
                            ReleaseThreadWicDecoderSessions();
                        quicksift::ReleaseThreadFaceDetector();
                        observedDecoderSessionTrim = requestedTrim;
                        lock.lock();
                        continue;
                    }
                    const auto now = std::chrono::steady_clock::now();
                    if (!interactiveQueue_.empty() || !visibleQueue_.empty()) {
                        job = PopWork(false);
                        break;
                    }
                    // One idle-capable lane receives a guarded metadata turn after
                    // a bounded predictive/face burst. Interactive and visible work
                    // remain absolute priority, while folder metadata cannot starve
                    // forever during sustained scrolling or prefetch replenishment.
                    if (allowIdle && ShouldServiceIdleAfterForegroundBurst(
                        !interactiveQueue_.empty(), !visibleQueue_.empty(),
                        now >= idleNotBefore_, !idleQueue_.empty(), foregroundBurstSinceIdle_)) {
                        job = PopWork(true);
                        break;
                    }
                    if (!predictiveQueue_.empty() || !faceQueue_.empty()) {
                        job = PopWork(false);
                        break;
                    }
                    if (allowIdle && (!idleQueue_.empty() || !analysisQueue_.empty())) {
                        if (now >= idleNotBefore_) { job = PopWork(true); break; }
                        cv_.wait_until(lock, idleNotBefore_);
                    } else cv_.wait(lock);
                }
                if (stopping_) break;
            }

            const auto dequeuedAt = std::chrono::steady_clock::now();
            const double queueWaitMs = job.enqueuedAt == std::chrono::steady_clock::time_point{} ? 0.0 :
                std::chrono::duration<double, std::milli>(dequeuedAt - job.enqueuedAt).count();
            if (job.path.empty() && job.kind == JobKind::DecodeFull && job.targetSize == 0) {
                std::this_thread::yield();
                continue;
            }
            const bool visualJob = job.kind == JobKind::DecodePreview ||
                job.kind == JobKind::DecodeFull || job.kind == JobKind::DecodeTile ||
                job.kind == JobKind::Face;
            const bool tileJob = job.kind == JobKind::DecodeTile;
            QS_LOG_EVENT(quicksift::diagnostics::Level::Trace, L"Worker", L"job_dequeued",
                {L"job", JobKindName(job.kind)},
                {L"priority", JobPriorityName(job.priority)},
                {L"file", DiagnosticFilename(job.path)},
                {L"target", std::to_wstring(job.targetSize)},
                {L"generation", std::to_wstring(job.generation)},
                {L"request_epoch", std::to_wstring(job.requestEpoch)},
                {L"viewport_epoch", std::to_wstring(job.viewportEpoch)},
                {L"queue_ms", std::to_wstring(queueWaitMs)});
            // Mark visual work as active immediately after dequeue, before decoded-memory
            // admission. A visible decode can spend substantial time waiting for the
            // shared decode budget; metadata must not start a competing disk/RAW read
            // during that gap simply because the visual job has not reached its decoder yet.
            if (visualJob) activeVisualJobs_.fetch_add(1, std::memory_order_acq_rel);
            const bool thumbnailJob = job.cacheClass == CacheClass::Thumbnail &&
                (job.kind == JobKind::DecodePreview || job.kind == JobKind::DecodeFull);
            if (thumbnailJob) activeThumbnailJobs_.fetch_add(1, std::memory_order_acq_rel);
            const size_t decodeReservationBytes = EstimatedDecodeReservation(job);
            const auto budgetStarted = std::chrono::steady_clock::now();
            std::shared_ptr<DecodeReservation> decodeReservation = decodeBudget_ ?
                decodeBudget_->Acquire(decodeReservationBytes, DecodePriorityFor(job.priority),
                    [this, job] { return IsCancelled(job); }) : nullptr;
            const double budgetWaitMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - budgetStarted).count();
            if (decodeReservationBytes != 0 && !decodeReservation) {
                const bool cancelled = IsCancelled(job);
                if (visualJob) {
                    activeVisualJobs_.fetch_sub(1, std::memory_order_acq_rel);
                    if (thumbnailJob) activeThumbnailJobs_.fetch_sub(1, std::memory_order_acq_rel);
                    cv_.notify_all();
                }
                AcknowledgeUnpostedJob(job, job.key);
                TryPostMetadataAbort(job);
                FinishAnalysisJob(job);
                RecordTelemetry(job, queueWaitMs, budgetWaitMs, 0.0, cancelled, false);
                if (tileJob) activeTileJobs_.fetch_sub(1, std::memory_order_acq_rel);
                if (stoppingAtomic_.load(std::memory_order_acquire)) break;
                cv_.notify_all();
                continue;
            }
            const bool oversizedReservation = decodeReservation && decodeReservation->Oversized();
            if (oversizedReservation) {
                const std::size_t configuredLimit = decodeBudget_ ? decodeBudget_->Limit() : 0;
                QS_LOG_WARNING(L"Performance", L"Oversized decode admitted for " +
                    job.path.filename().wstring() + L": reservation " +
                    std::to_wstring(decodeReservationBytes / kMebibyte) + L" MiB exceeds the " +
                    std::to_wstring(configuredLimit / kMebibyte) +
                    L" MiB runtime ceiling; encoded read-ahead is paused until the exclusive decode releases.");
                std::lock_guard readAheadLock(readAheadGateMutex_);
                readAhead_.CancelAll();
                ConfigureReadAheadLocked();
            }
            const bool handleJobStarted = TryBeginHandleJob(job);
            const bool pathBlocked = IsPathBlockedForDecoder(job.path);
            if ((!handleJobStarted || IsCancelled(job)) && job.kind == JobKind::Metadata && pathBlocked &&
                !stoppingAtomic_.load(std::memory_order_acquire)) {
                if (handleJobStarted) EndHandleJob(job);

                // Exclusive file operations intentionally block metadata reads. Keep
                // the pending key alive and put the metadata job back after the write
                // releases the path instead of consuming its UI in-flight slot as a
                // permanent failure.
                std::unique_lock blockedLock(blockedMutex_);
                blockedCv_.wait(blockedLock, [this, &job] {
                    return stoppingAtomic_.load(std::memory_order_acquire) ||
                        FindBlockedPathLocked(job.path.c_str()) == blockedPaths_.end();
                });
                blockedLock.unlock();
                if (stoppingAtomic_.load(std::memory_order_acquire)) {
                    AcknowledgeUnpostedJob(job, job.key);
                    TryPostMetadataAbort(job);
                    cv_.notify_all();
                    continue;
                }
                {
                    std::lock_guard lock(mutex_);
                    if (!stopping_) {
                        job.enqueuedAt = std::chrono::steady_clock::now();
                        metadataQueue_.push_front(std::move(job));
                    }
                }
                cv_.notify_all();
                continue;
            }
            if (!handleJobStarted || IsCancelled(job)) {
                if (visualJob) {
                    activeVisualJobs_.fetch_sub(1, std::memory_order_acq_rel);
                    if (thumbnailJob) activeThumbnailJobs_.fetch_sub(1, std::memory_order_acq_rel);
                }
                if (handleJobStarted) EndHandleJob(job);
                AcknowledgeUnpostedJob(job, job.key);
                TryPostMetadataAbort(job);
                FinishAnalysisJob(job);
                RecordTelemetry(job, queueWaitMs, budgetWaitMs, 0.0, true, oversizedReservation);
                if (tileJob) activeTileJobs_.fetch_sub(1, std::memory_order_acq_rel);
                cv_.notify_all();
                continue;
            }
            const auto executionStarted = std::chrono::steady_clock::now();
            job.startedAt = executionStarted;
            double fingerprintMs = 0.0;
            double cacheLookupMs = 0.0;
            double decodePathMs = 0.0;
            double resultPostMs = 0.0;
            bool resultPosted = false;
            std::shared_ptr<WorkResult> result;
            bool resultFromPersistentCache = false;
            bool resultWorkFailed = false;
            try {
                result = std::make_shared<WorkResult>();
                result->decodeReservation = std::move(decodeReservation);
                result->kind = job.kind;
                result->priority = job.priority;
                result->cacheClass = job.cacheClass;
                result->path = job.path;
                result->targetSize = job.targetSize;
                result->generation = job.generation;
                result->requestEpoch = job.requestEpoch;
                result->viewportEpoch = job.viewportEpoch;
                result->viewInteractionEpoch = job.viewInteractionEpoch;
                result->metadataRevision = job.metadataRevision;
                result->rawJpegPreviewOnly = job.rawJpegPreviewOnly;
                result->retryAttempt = job.retryAttempt;
                result->forceWic = job.forceWic;
                result->pendingKey = job.key;
                result->tileX = job.tileX;
                result->tileY = job.tileY;
                result->tileWidth = job.tileWidth;
                result->tileHeight = job.tileHeight;
                result->tileLevel = job.tileLevel;

                uint64_t fileSize = 0;
                int64_t modifiedStamp = 0;
                int64_t sidecarStamp = 0;
                const auto fingerprintStarted = std::chrono::steady_clock::now();
                const bool fingerprinted = FileFingerprint(job.path, fileSize, modifiedStamp);
                fingerprintMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - fingerprintStarted).count();
                if (fingerprinted &&
                    (job.kind == JobKind::Metadata || job.kind == JobKind::Exif || job.kind == JobKind::Face)) {
                    sidecarStamp = MetadataStampFor(job.path);
                }
                if (fingerprinted) {
                    result->fileSize = fileSize;
                    result->modifiedStamp = modifiedStamp;
                    result->sidecarStamp = sidecarStamp;
                }
                bool queueBitmapCacheWrite = false;
                if (!IsCancelled(job)) {
                    if (job.kind == JobKind::Metadata) {
                        if (fingerprinted && persistentCache_) {
                            if (const auto cached = persistentCache_->LookupRecord(
                                job.path, fileSize, modifiedStamp, sidecarStamp, false, false)) {
                                result->rating = cached->rating;
                                result->colorLabel = cached->colorLabel;
                                result->pickState = cached->pickState;
                                result->ratingKnowledge = cached->ratingKnowledge;
                                result->colorLabelKnowledge = cached->colorLabelKnowledge;
                                result->pickStateKnowledge = cached->pickStateKnowledge;
                                result->rawRating = cached->rawRating;
                                result->rawColorLabel = cached->rawColorLabel;
                                result->rawPickState = cached->rawPickState;
                            } else {
                                const PhotoMetadataValues metadata = ReadPhotoMetadata(job.path);
                                result->rating = metadata.rating.value;
                                result->colorLabel = metadata.colorLabel.value;
                                result->pickState = metadata.pickState.value;
                                result->ratingKnowledge = metadata.rating.knowledge;
                                result->colorLabelKnowledge = metadata.colorLabel.knowledge;
                                result->pickStateKnowledge = metadata.pickState.knowledge;
                                result->rawRating = metadata.rating.rawValue;
                                result->rawColorLabel = metadata.colorLabel.rawValue;
                                result->rawPickState = metadata.pickState.rawValue;
                            }
                        } else {
                            const PhotoMetadataValues metadata = ReadPhotoMetadata(job.path);
                            result->rating = metadata.rating.value;
                            result->colorLabel = metadata.colorLabel.value;
                            result->pickState = metadata.pickState.value;
                            result->ratingKnowledge = metadata.rating.knowledge;
                            result->colorLabelKnowledge = metadata.colorLabel.knowledge;
                            result->pickStateKnowledge = metadata.pickState.knowledge;
                            result->rawRating = metadata.rating.rawValue;
                            result->rawColorLabel = metadata.colorLabel.rawValue;
                            result->rawPickState = metadata.pickState.rawValue;
                        }
                    } else if (job.kind == JobKind::Exif) {
                        if (fingerprinted && persistentCache_) {
                            if (auto cached = persistentCache_->LookupRecord(
                                job.path, fileSize, modifiedStamp, sidecarStamp, true, false);
                                cached && !cached->exifText.empty()) {
                                result->exifText = std::move(cached->exifText);
                            } else {
                                result->exifText = BuildExifText(job.path);
                            }
                        } else {
                            result->exifText = BuildExifText(job.path);
                        }
                    } else if (job.kind == JobKind::Face) {
                        if (fingerprinted && persistentCache_) {
                            if (auto cached = persistentCache_->LookupRecord(
                                job.path, fileSize, modifiedStamp, sidecarStamp, false, true);
                                cached && cached->facesScanned) {
                                result->faces = std::move(cached->faces);
                                result->facesScanned = true;
                                result->faceBlurScanned = cached->faceBlurScanned;
                                result->faceBlurry = cached->faceBlurry;
                                result->faceSharpness = cached->faceSharpness;
                            }
                        }
                        const bool needsDetection = !result->facesScanned;
                        const bool needsSharpness = result->facesScanned &&
                            !result->faces.empty() && !result->faceBlurScanned;
                        if (!needsDetection && !needsSharpness) {
                            result->faceAnalysisOutcome = quicksift::core::FaceAnalysisOutcome::Completed;
                        } else if (needsDetection) {
                            const auto capability = quicksift::QueryFaceDetectorCapability();
                            if (capability == quicksift::core::FaceDetectorCapability::Unavailable) {
                                result->faceAnalysisOutcome = quicksift::core::FaceAnalysisOutcome::Unavailable;
                            } else if (capability != quicksift::core::FaceDetectorCapability::Available) {
                                result->faceAnalysisOutcome = quicksift::core::FaceAnalysisOutcome::TransientFailure;
                            }
                        }

                        WorkResult preview;
                        bool decodedPreview = false;
                        const auto cancelled = [this, job] { return IsCancelled(job); };
                        const int faceTarget = std::max(kFaceDecodeSize, job.targetSize);
                        if (result->faceAnalysisOutcome == quicksift::core::FaceAnalysisOutcome::None &&
                            factory && !IsCancelled(job) && (needsDetection || needsSharpness)) {
                            preview.path = job.path;
                            WorkJob faceDecodeJob{ JobKind::Face, job.priority, CacheClass::Preview,
                                job.path, faceTarget, job.generation, job.requestEpoch };
                            faceDecodeJob.rawJpegPreviewOnly = job.rawJpegPreviewOnly;
                            const bool rawJpegOnly = job.rawJpegPreviewOnly &&
                                IsRawExtension(ExtensionLower(job.path));
                            decodedPreview = DecodeWithBundledGuarded(faceDecodeJob, preview, true);
                            if (!decodedPreview && !rawJpegOnly) {
                                decodedPreview = DecodeWithWic(factory.Get(), job.path, faceTarget,
                                    preview, true, cancelled) || DecodeWithShell(job.path, faceTarget, preview);
                            }
                        }
                        if (decodedPreview && !IsCancelled(job)) {
                            if (needsDetection) {
                                result->faceAnalysisOutcome = quicksift::DetectFacesFastBgra(
                                    preview.pixels.data(), preview.width, preview.height, result->faces);
                                result->facesScanned = result->faceAnalysisOutcome ==
                                    quicksift::core::FaceAnalysisOutcome::Completed;
                            }
                            if (result->facesScanned && !result->faces.empty()) {
                                FaceSharpnessResult sharpness = AnalyzePrimaryFaceSharpnessBgra(
                                    preview.pixels.data(), preview.width, preview.height, result->faces);
                                if (!sharpness.scanned && faceTarget < kFaceSharpnessDecodeSize &&
                                    !IsCancelled(job)) {
                                    WorkResult largerPreview;
                                    WorkJob sharpnessJob{ JobKind::Face, job.priority, CacheClass::Preview,
                                        job.path, kFaceSharpnessDecodeSize, job.generation, job.requestEpoch };
                                    sharpnessJob.rawJpegPreviewOnly = job.rawJpegPreviewOnly;
                                    const bool rawJpegOnly = job.rawJpegPreviewOnly &&
                                        IsRawExtension(ExtensionLower(job.path));
                                    bool decodedLarger = DecodeWithBundledGuarded(
                                        sharpnessJob, largerPreview, true);
                                    if (!decodedLarger && !rawJpegOnly) {
                                        decodedLarger = DecodeWithWic(factory.Get(), job.path,
                                            kFaceSharpnessDecodeSize, largerPreview, true, cancelled) ||
                                            DecodeWithShell(job.path, kFaceSharpnessDecodeSize, largerPreview);
                                    }
                                    if (decodedLarger && !IsCancelled(job)) {
                                        sharpness = AnalyzePrimaryFaceSharpnessBgra(largerPreview.pixels.data(),
                                            largerPreview.width, largerPreview.height, result->faces);
                                    }
                                }
                                result->faceBlurScanned = sharpness.scanned;
                                result->faceBlurry = sharpness.scanned && sharpness.blurry;
                                result->faceSharpness = sharpness.scanned ? sharpness.score : 0.0f;
                                result->faceAnalysisOutcome = sharpness.scanned ?
                                    quicksift::core::FaceAnalysisOutcome::Completed :
                                    quicksift::core::FaceAnalysisOutcome::TransientFailure;
                            } else if (result->facesScanned) {
                                result->faceAnalysisOutcome = quicksift::core::FaceAnalysisOutcome::Completed;
                            }
                        } else if (!IsCancelled(job) &&
                            result->faceAnalysisOutcome == quicksift::core::FaceAnalysisOutcome::None &&
                            (needsDetection || needsSharpness)) {
                            result->faceAnalysisOutcome = quicksift::core::FaceAnalysisOutcome::TransientFailure;
                        }
                    } else if (job.kind == JobKind::DecodeTile) {
                        bool loadedTile = false;
                        const bool rawJpegOnly = job.rawJpegPreviewOnly &&
                            IsRawExtension(ExtensionLower(job.path));
                        if (fingerprinted && persistentCache_ && profile_.enablePersistentPyramid &&
                            !rawJpegOnly) {
                            quicksift::CachedBitmapData cachedTile;
                            if (persistentCache_->LoadPyramidTile(job.path, fileSize, modifiedStamp,
                                sidecarStamp, job.tileLevel, job.tileX, job.tileY, kTileSourceSize, cachedTile)) {
                                result->width = cachedTile.width;
                                result->height = cachedTile.height;
                                result->sourceWidth = cachedTile.sourceWidth;
                                result->sourceHeight = cachedTile.sourceHeight;
                                result->tileX = job.tileX;
                                result->tileY = job.tileY;
                                result->tileWidth = job.tileWidth;
                                result->tileHeight = job.tileHeight;
                                result->tileLevel = job.tileLevel;
                                result->pixels = std::move(cachedTile.pixels);
                                result->fromPersistentCache = true;
                                resultFromPersistentCache = true;
                                loadedTile = true;
                            }
                        }
                        if (!loadedTile && factory && !rawJpegOnly && !IsCancelled(job)) {
                            const auto cancelled = [this, job] { return IsCancelled(job); };
                            loadedTile = DecodeTileWithWic(factory.Get(), job.path, job, *result, cancelled);
                            queueBitmapCacheWrite = loadedTile && fingerprinted && persistentCache_ &&
                                profile_.enablePersistentPyramid && !IsCancelled(job);
                        }
                    } else if (factory) {
                        bool loaded = false;
                        const bool rawJpegOnly = job.rawJpegPreviewOnly &&
                            IsRawExtension(ExtensionLower(job.path));
                        if (fingerprinted && persistentCache_ && !rawJpegOnly) {
                            quicksift::CachedBitmapData cached;
                            const auto cacheLookupStarted = std::chrono::steady_clock::now();
                            const bool cacheHit = persistentCache_->LoadCachedBitmap(
                                job.path, fileSize, modifiedStamp, sidecarStamp, job.targetSize,
                                static_cast<int>(job.cacheClass), cached);
                            cacheLookupMs = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - cacheLookupStarted).count();
                            if (cacheHit) {
                                persistentCacheHits_.fetch_add(1, std::memory_order_relaxed);
                                result->width = cached.width;
                                result->height = cached.height;
                                result->sourceWidth = cached.sourceWidth;
                                result->sourceHeight = cached.sourceHeight;
                                result->previewOnly = cached.previewOnly;
                                result->pixels = std::move(cached.pixels);
                                result->fromPersistentCache = true;
                                loaded = true;
                            } else {
                                persistentCacheMisses_.fetch_add(1, std::memory_order_relaxed);
                            }
                        }
                        if (!loaded && !IsCancelled(job)) {
                            const auto decodeStarted = std::chrono::steady_clock::now();
                            const bool raw = IsRawExtension(ExtensionLower(job.path));
                            if (job.kind == JobKind::DecodePreview) {
                                result->previewOnly = raw;
                                const auto cancelled = [this, job] { return IsCancelled(job); };
                                if (job.cacheClass == CacheClass::Thumbnail && job.forceWic) {
                                    loaded = DecodeWithWic(factory.Get(), job.path, job.targetSize, *result, true, cancelled);
                                } else if (raw) {
                                    loaded = DecodeWithBundledGuarded(job, *result, true);
                                    if (!loaded && !rawJpegOnly && !IsCancelled(job)) {
                                        loaded = DecodeWithShell(job.path, job.targetSize, *result) ||
                                            DecodeWithWic(factory.Get(), job.path, job.targetSize, *result, true, cancelled);
                                    }
                                } else {
                                    const std::wstring extension = ExtensionLower(job.path);
                                    const bool jpeg = extension == L".jpg" || extension == L".jpeg" || extension == L".jpe";
                                    if (jpeg) {
                                        loaded = DecodeWithBundledGuarded(job, *result, true);
                                        if (!loaded && !IsCancelled(job)) {
                                            loaded = DecodeWithWic(factory.Get(), job.path, job.targetSize, *result, true, cancelled);
                                        }
                                        if (!loaded && !IsCancelled(job)) {
                                            loaded = DecodeWithShell(job.path, job.targetSize, *result);
                                        }
                                    } else {
                                        loaded = DecodeWithWic(factory.Get(), job.path, job.targetSize, *result, true, cancelled);
                                        if (!loaded && !IsCancelled(job)) {
                                            loaded = DecodeWithBundledGuarded(job, *result, true);
                                        }
                                        if (!loaded && !IsCancelled(job)) {
                                            loaded = DecodeWithShell(job.path, job.targetSize, *result);
                                        }
                                    }
                                }
                            } else {
                                const auto cancelled = [this, job] { return IsCancelled(job); };
                                if (rawJpegOnly) {
                                    loaded = DecodeWithBundledGuarded(job, *result, true);
                                } else {
                                    const std::wstring extension = ExtensionLower(job.path);
                                    const bool jpeg = extension == L".jpg" || extension == L".jpeg" || extension == L".jpe";
                                    if (jpeg) {
                                        loaded = DecodeWithBundledGuarded(job, *result, false) ||
                                            DecodeWithWic(factory.Get(), job.path, job.targetSize, *result, false, cancelled);
                                    } else {
                                        loaded = DecodeWithWic(factory.Get(), job.path, job.targetSize, *result, false, cancelled) ||
                                            DecodeWithBundledGuarded(job, *result, false);
                                    }
                                    if (!loaded && DecodeWithShell(job.path, job.targetSize, *result)) {
                                        loaded = true;
                                        result->previewOnly = true;
                                    }
                                }
                            }
                            decodePathMs = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - decodeStarted).count();
                            // Persist only thumbnails and screen-resolution previews. Full-size/tile
                            // data is intentionally memory-only: duplicating 40–100 MP buffers during
                            // cache writes would create RAM spikes and steal responsiveness.
                            const int sourceEdge = std::max(result->sourceWidth, result->sourceHeight);
                            const int requestedEdge = sourceEdge > 0 ? std::min(job.targetSize, sourceEdge) : job.targetSize;
                            const bool adequateThumbnail = job.cacheClass != CacheClass::Thumbnail ||
                                std::max(result->width, result->height) * 10 >= std::max(1, requestedEdge) * 9;
                            const bool persistBitmap = adequateThumbnail &&
                                (job.cacheClass == CacheClass::Thumbnail ||
                                 (job.cacheClass == CacheClass::Preview && job.targetSize <= 2048));
                            // Never write the disk cache before posting a visible result.
                            // Slow storage, antivirus hooks, or a damaged old cache must not
                            // keep the UI on “loading…”. The shared pixel buffer is handed to
                            // a bounded background writer after the UI notification is queued.
                            queueBitmapCacheWrite = loaded && persistBitmap && fingerprinted &&
                                persistentCache_ && !rawJpegOnly && !IsCancelled(job);
                        }
                    }
                }

                // Normalize RAW source geometry before either the UI or persistent
                // bitmap writer observes it. This self-heals stale landscape geometry
                // and prevents portrait previews from being written back incorrectly.
                if (!result->pixels.empty() && result->kind != JobKind::DecodeTile &&
                    IsRawExtension(ExtensionLower(result->path))) {
                    NormalizeSourceGeometryToPixelOrientation(result->width, result->height,
                        result->sourceWidth, result->sourceHeight);
                }

                // Empty/metadata results no longer retain a decode buffer. Pixel-bearing
                // results keep their reservation until every posted/cache-writer owner is
                // finished, closing the previously unaccounted UI-message-queue loophole.
                if (result->pixels.empty()) result->decodeReservation.reset();
                else if (result->decodeReservation) result->decodeReservation->ShrinkTo(result->pixels.size());

                const bool timedOut = JobTimedOut(job);
                result->timedOut = timedOut;
                const bool cancellationOnly = IsCancelled(job) && !timedOut;
                // A visual decoder that returns no pixels without a genuine cancellation
                // is a transient decode failure. Mark it explicitly so the UI can clear
                // the loading state, release coalescing ownership, and retry later instead
                // of silently blacklisting or leaving the thumbnail permanently pending.
                const bool visualDecodeNoPixels =
                    (job.kind == JobKind::DecodePreview || job.kind == JobKind::DecodeFull ||
                     job.kind == JobKind::DecodeTile) && result->pixels.empty();
                if (visualDecodeNoPixels && !cancellationOnly) {
                    result->workFailed = true;
                    resultWorkFailed = true;
                }
                result->cancelled = IsCancelled(job);
                const auto resultPostStarted = std::chrono::steady_clock::now();
                resultPosted = TryPostResult(job, result);
                resultPostMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - resultPostStarted).count();
                if (queueBitmapCacheWrite && !IsCancelled(job)) {
                    QueueBitmapCacheWrite(job, fileSize, modifiedStamp, sidecarStamp, result);
                }
                // Derivative pyramid generation is intentionally deferred out of
                // interactive/visible decode jobs. CreateWicDerivative() can be expensive
                // and used to be included in the foreground "decode" telemetry, making a
                // successful image load appear to take tens of seconds. Foreground work
                // should hand pixels to the UI first; derivative maintenance can run only
                // from predictive/idle lanes.
                const bool derivativeSafePriority =
                    job.priority == JobPriority::Predictive ||
                    job.priority == JobPriority::Idle ||
                    job.priority == JobPriority::Analysis;
                if (resultPosted && derivativeSafePriority && fingerprinted &&
                    !result->fromPersistentCache &&
                    (job.kind == JobKind::DecodePreview || job.kind == JobKind::DecodeFull)) {
                    QueueDerivedCacheLevels(factory.Get(), job, fileSize, modifiedStamp, sidecarStamp, result);
                }
                if (!resultPosted) {
                    // Release coalescing ownership before telling the UI to pump
                    // another metadata job; the UI may process this message immediately.
                    AcknowledgeUnpostedJob(job, result->pendingKey);
                    TryPostMetadataAbort(job);
                }
            } catch (...) {
                try {
                    QS_LOG_ERROR(L"Worker", L"Unhandled worker exception for " + job.path.wstring() +
                        L" (kind " + std::to_wstring(static_cast<int>(job.kind)) + L")");
                } catch (...) {
                    QS_LOG_ERROR(L"Worker", L"Unhandled worker exception; detailed context was unavailable");
                }
                // Decoder, filesystem, and allocation failures must not escape a
                // std::thread entry point. Post a lightweight failed completion so
                // metadata/EXIF pipelines release their in-flight counters instead
                // of surviving the exception in a permanently stalled state.
                if (!resultPosted) {
                    try {
                        auto failure = std::make_shared<WorkResult>();
                        failure->kind = job.kind;
                        failure->priority = job.priority;
                        failure->cacheClass = job.cacheClass;
                        failure->path = job.path;
                        failure->targetSize = job.targetSize;
                        failure->generation = job.generation;
                        failure->requestEpoch = job.requestEpoch;
                        failure->viewportEpoch = job.viewportEpoch;
                        failure->rawJpegPreviewOnly = job.rawJpegPreviewOnly;
                        failure->retryAttempt = job.retryAttempt;
                        failure->forceWic = job.forceWic;
                        failure->timedOut = JobTimedOut(job);
                        failure->pendingKey = job.key;
                        failure->tileX = job.tileX;
                        failure->tileY = job.tileY;
                        failure->tileWidth = job.tileWidth;
                        failure->tileHeight = job.tileHeight;
                        failure->tileLevel = job.tileLevel;
                        failure->workFailed = true;
                        failure->cancelled = IsCancelled(job);
                        resultWorkFailed = true;
                        if (job.kind == JobKind::Face) {
                            failure->faceAnalysisOutcome =
                                quicksift::core::FaceAnalysisOutcome::TransientFailure;
                        }
                        resultPosted = TryPostResult(job, failure);
                    } catch (...) {
                        // At extreme allocation pressure even the small failure
                        // envelope may be unavailable. Releasing the key still lets
                        // a later UI request retry once memory becomes available.
                    }
                    if (!resultPosted) {
                        AcknowledgeUnpostedJob(job, job.key);
                        TryPostMetadataAbort(job);
                    }
                }
            }
            EndHandleJob(job);
            if (visualJob) {
                {
                    std::lock_guard lock(mutex_);
                    const std::wstring family = VisualFamilyKey(job);
                    auto active = activeVisualFamilies_.find(family);
                    if (active != activeVisualFamilies_.end() && active->second <= job.targetSize)
                        activeVisualFamilies_.erase(active);
                }
                activeVisualJobs_.fetch_sub(1, std::memory_order_acq_rel);
                if (thumbnailJob) activeThumbnailJobs_.fetch_sub(1, std::memory_order_acq_rel);
                cv_.notify_all();
                RefreshReadAheadGate();
            }
            FinishAnalysisJob(job);
            const double executionMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - executionStarted).count();
            if (quicksift::diagnostics::VerboseLogging()) {
                QS_LOG_EVENT(resultPosted ? quicksift::diagnostics::Level::Trace : quicksift::diagnostics::Level::Warning,
                    L"Worker", L"job_complete",
                    {L"file", DiagnosticFilename(job.path)},
                    {L"kind", JobKindName(job.kind)},
                    {L"priority", JobPriorityName(job.priority)},
                    {L"result", resultPosted ? L"posted" : (resultWorkFailed ? L"failed" : L"not_posted")},
                    {L"cancelled", (IsCancelled(job) || (result && result->cancelled)) ? L"1" : L"0"});
            }
            if (executionMs >= 2000.0) {
                const bool timedOutForLog = JobTimedOut(job);
                const std::wstring slowJobMessage =
                    std::wstring(L"Slow visual job: ") + job.path.filename().wstring() +
                    L" kind=" + std::to_wstring(static_cast<int>(job.kind)) +
                    L" target=" + std::to_wstring(job.targetSize) +
                    L" total=" + std::to_wstring(static_cast<long long>(executionMs)) + L" ms" +
                    L" fingerprint=" + std::to_wstring(static_cast<long long>(fingerprintMs)) + L" ms" +
                    L" cache=" + std::to_wstring(static_cast<long long>(cacheLookupMs)) + L" ms" +
                    L" decode=" + std::to_wstring(static_cast<long long>(decodePathMs)) + L" ms" +
                    L" post=" + std::to_wstring(static_cast<long long>(resultPostMs)) + L" ms" +
                    L" loaded=" + (resultPosted ? L"yes" : L"no") +
                    L" timed_out=" + (timedOutForLog ? L"1" : L"0");
                QS_LOG_WARNING(L"Performance", slowJobMessage);
            }
            RecordTelemetry(job, queueWaitMs, budgetWaitMs, executionMs,
                IsCancelled(job) && !resultPosted, oversizedReservation);
            if (tileJob) {
                activeTileJobs_.fetch_sub(1, std::memory_order_acq_rel);
                cv_.notify_all();
            }
            if (gThreadWorkerIndex == 0) {
                static thread_local std::uint64_t lastSnapshot = 0;
                const std::uint64_t nowMs = quicksift::diagnostics::SessionId() == 0 ? 0 :
                    static_cast<std::uint64_t>(GetTickCount64());
                if (nowMs != 0 && (lastSnapshot == 0 || nowMs - lastSnapshot >= 10000)) {
                    lastSnapshot = nowMs;
                    quicksift::diagnostics::WriteSystemSnapshot(L"worker_periodic");
                }
            }
        }
        // COM/WinRT objects must release while the apartment is still initialized.
        // Function-local and thread-local destructors otherwise run only after this
        // function returns, which is too late once RoUninitialize has been called.
        ReleaseThreadWicDecoderSessions();
        factory.Reset();
        quicksift::ReleaseThreadFaceDetector();
        if (backgroundLane) SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
        if (SUCCEEDED(runtime)) RoUninitialize();
        gThreadWorkerOwner = nullptr;
        gThreadWorkerIndex = 0;
    }

    void Worker::QueueBitmapCacheWrite(const WorkJob& job, uint64_t fileSize, int64_t modifiedStamp,
        int64_t sidecarStamp, const std::shared_ptr<const WorkResult>& result) {
        if (!persistentCache_ || !result || result->pixels.empty()) return;
        if (!allowSpeculativeCacheWrites_.load(std::memory_order_acquire) &&
            job.cacheClass != CacheClass::Thumbnail) return;
        BitmapCacheWrite write;
        write.path = job.path;
        write.fileSize = fileSize;
        write.modifiedStamp = modifiedStamp;
        write.sidecarStamp = sidecarStamp;
        write.targetSize = job.targetSize;
        write.generation = job.generation;
        write.requestEpoch = job.requestEpoch;
        write.viewportEpoch = job.viewportEpoch;
        write.ignoreThumbnailViewport = job.ignoreThumbnailViewport;
        write.cacheClass = job.cacheClass;
        write.priority = job.priority;
        write.result = result;
        write.bytes = result->pixels.size();
        write.cacheEpoch = cacheEpoch_.load(std::memory_order_acquire);

        if (write.bytes > cacheWriteQueueLimit_) return;
        if (write.requestEpoch &&
            write.requestEpoch < navigationEpoch_.load(std::memory_order_acquire)) return;
        if (write.cacheClass == CacheClass::Thumbnail && !write.ignoreThumbnailViewport &&
            quicksift::review::IsStaleThumbnailViewport(
            write.viewportEpoch, thumbnailViewportEpoch_.load(std::memory_order_acquire),
            IsThumbnailPathCurrent(write.path))) return;
        {
            std::lock_guard lock(mutex_);
            const auto writePriority = [](CacheClass cacheClass) {
                switch (cacheClass) {
                case CacheClass::Full: return 3;
                case CacheClass::Preview: return 2;
                case CacheClass::Thumbnail: return 1;
                case CacheClass::Tile: return 0;
                }
                return 0;
            };
            const int incomingPriority = writePriority(write.cacheClass);
            while (!cacheWriteQueue_.empty() &&
                write.bytes <= SIZE_MAX - cacheWriteBytes_ &&
                cacheWriteBytes_ + write.bytes > cacheWriteQueueLimit_) {
                // Do not let a flood of cheap thumbnails evict a queued full-size
                // image from the bounded write queue. Remove the oldest member of
                // the lowest cache class that is no more valuable than the incoming
                // write; otherwise drop the incoming write and regenerate later.
                auto victim = cacheWriteQueue_.end();
                int victimPriority = incomingPriority + 1;
                for (auto it = cacheWriteQueue_.begin(); it != cacheWriteQueue_.end(); ++it) {
                    const int priority = writePriority(it->cacheClass);
                    if (priority <= incomingPriority && priority < victimPriority) {
                        victim = it;
                        victimPriority = priority;
                        if (priority == 0) break;
                    }
                }
                if (victim == cacheWriteQueue_.end()) return;
                cacheWriteBytes_ = victim->bytes > cacheWriteBytes_ ? 0 : cacheWriteBytes_ - victim->bytes;
                cacheWriteQueue_.erase(victim);
            }
            if (write.bytes > SIZE_MAX - cacheWriteBytes_ ||
                cacheWriteBytes_ + write.bytes > cacheWriteQueueLimit_ || stopping_) return;
            cacheWriteBytes_ += write.bytes;
            cacheWriteQueue_.push_back(std::move(write));
        }
        cacheCv_.notify_one();
    }

    void Worker::RunCacheWriter() {
        quicksift::ApplyBackgroundThreadPolicy(profile_.cpuTopology, true);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
        size_t bytesSinceMaintenance = 0;
        bool preferRecord = true;
        for (;;) {
            try {
            BitmapCacheWrite bitmapWrite;
            std::vector<RecordCacheWrite> recordWrites;
            bool hasBitmapWrite = false;
            bool flush = false;
            {
                std::unique_lock lock(mutex_);
                cacheCv_.wait(lock, [&] {
                    return stopping_ || flushRequested_ || !cacheWriteQueue_.empty() ||
                        !recordWriteQueue_.empty();
                });
                if (stopping_) break;

                auto takeRecordBatch = [&] {
                    const size_t count = std::min(recordWriteBatchLimit_, recordWriteQueue_.size());
                    recordWrites.reserve(count);
                    for (size_t index = 0; index < count; ++index) {
                        auto it = recordWriteQueue_.begin();
                        recordWrites.push_back(std::move(it->second));
                        recordWriteQueue_.erase(it);
                    }
                };
                const auto cachePriorityRank = [](JobPriority p) {
                    switch (p) {
                    case JobPriority::Interactive: return 0;
                    case JobPriority::Visible: return 1;
                    case JobPriority::Predictive: return 2;
                    case JobPriority::Face: return 3;
                    case JobPriority::Idle: return 4;
                    case JobPriority::Analysis: return 5;
                    }
                    return 6;
                };
                auto bestThumbnail = cacheWriteQueue_.end();
                for (auto it = cacheWriteQueue_.begin(); it != cacheWriteQueue_.end(); ++it) {
                    if (it->cacheClass != CacheClass::Thumbnail) continue;
                    if (bestThumbnail == cacheWriteQueue_.end() ||
                        cachePriorityRank(it->priority) < cachePriorityRank(bestThumbnail->priority) ||
                        (cachePriorityRank(it->priority) == cachePriorityRank(bestThumbnail->priority) &&
                            it->viewportEpoch > bestThumbnail->viewportEpoch)) {
                        bestThumbnail = it;
                    }
                }
                if (bestThumbnail != cacheWriteQueue_.end()) {
                    // Building the thumbnail cache is part of the visible-grid
                    // delivery path. Always drain the highest-value thumbnail
                    // write before metadata or non-thumbnail cache maintenance.
                    bitmapWrite = std::move(*bestThumbnail);
                    cacheWriteQueue_.erase(bestThumbnail);
                    cacheWriteBytes_ = bitmapWrite.bytes > cacheWriteBytes_ ?
                        0 : cacheWriteBytes_ - bitmapWrite.bytes;
                    hasBitmapWrite = true;
                } else if (!recordWriteQueue_.empty() && preferRecord) {
                    takeRecordBatch();
                } else if (!cacheWriteQueue_.empty()) {
                    auto best = cacheWriteQueue_.begin();
                    for (auto it = std::next(cacheWriteQueue_.begin());
                        it != cacheWriteQueue_.end(); ++it) {
                        if (cachePriorityRank(it->priority) < cachePriorityRank(best->priority)) best = it;
                    }
                    bitmapWrite = std::move(*best);
                    cacheWriteQueue_.erase(best);
                    cacheWriteBytes_ = bitmapWrite.bytes > cacheWriteBytes_ ?
                        0 : cacheWriteBytes_ - bitmapWrite.bytes;
                    hasBitmapWrite = true;
                } else if (!recordWriteQueue_.empty()) {
                    takeRecordBatch();
                }
                if (hasBitmapWrite || !recordWrites.empty()) preferRecord = !preferRecord;

                // A requested flush remains pending while either queue contains
                // work. This makes the operation a real drain/checkpoint barrier
                // instead of flushing after one arbitrary queued item.
                if (!hasBitmapWrite && recordWrites.empty() && flushRequested_ &&
                    cacheWriteQueue_.empty() && recordWriteQueue_.empty()) {
                    flushRequested_ = false;
                    flush = true;
                }
            }
            try {
                std::lock_guard cacheIoLock(cacheIoMutex_);
                const uint64_t currentCacheEpoch = cacheEpoch_.load(std::memory_order_acquire);
                if (persistentCache_ && !recordWrites.empty()) {
                    std::vector<std::pair<fs::path, quicksift::CachedImageRecord>> batch;
                    batch.reserve(recordWrites.size());
                    for (RecordCacheWrite& write : recordWrites) {
                        if (write.cacheEpoch == currentCacheEpoch)
                            batch.emplace_back(std::move(write.path), std::move(write.record));
                    }
                    persistentCache_->UpsertRecords(std::move(batch));
                }
                if (persistentCache_ && hasBitmapWrite && bitmapWrite.result &&
                    bitmapWrite.cacheEpoch == currentCacheEpoch &&
                    !bitmapWrite.result->pixels.empty() &&
                    bitmapWrite.generation == activeGeneration_.load(std::memory_order_acquire) &&
                    (!bitmapWrite.requestEpoch ||
                        bitmapWrite.requestEpoch == navigationEpoch_.load(std::memory_order_acquire)) &&
                    (bitmapWrite.cacheClass != CacheClass::Thumbnail ||
                        bitmapWrite.ignoreThumbnailViewport || bitmapWrite.viewportEpoch == 0 ||
                        bitmapWrite.viewportEpoch == thumbnailViewportEpoch_.load(std::memory_order_acquire) ||
                        IsThumbnailPathCurrent(bitmapWrite.path))) {
                    if (bitmapWrite.cacheClass == CacheClass::Tile) {
                        persistentCache_->StorePyramidTile(
                            bitmapWrite.path, bitmapWrite.fileSize, bitmapWrite.modifiedStamp,
                            bitmapWrite.sidecarStamp, bitmapWrite.result->tileLevel,
                            bitmapWrite.result->tileX, bitmapWrite.result->tileY, kTileSourceSize,
                            bitmapWrite.result->width, bitmapWrite.result->height,
                            bitmapWrite.result->sourceWidth, bitmapWrite.result->sourceHeight,
                            bitmapWrite.result->pixels);
                    } else {
                        persistentCache_->StoreBitmap(
                            bitmapWrite.path, bitmapWrite.fileSize, bitmapWrite.modifiedStamp,
                            bitmapWrite.sidecarStamp, bitmapWrite.targetSize,
                            static_cast<int>(bitmapWrite.cacheClass), bitmapWrite.result->width,
                            bitmapWrite.result->height, bitmapWrite.result->sourceWidth,
                            bitmapWrite.result->sourceHeight, bitmapWrite.result->previewOnly,
                            bitmapWrite.result->pixels);
                    }
                    bytesSinceMaintenance = bitmapWrite.bytes > SIZE_MAX - bytesSinceMaintenance ?
                        SIZE_MAX : bytesSinceMaintenance + bitmapWrite.bytes;
                }
                // Do not retain decoded pixels (and their decode-budget lease)
                // while a directory maintenance pass or database flush runs.
                bitmapWrite.result.reset();
                if (flush && persistentCache_) persistentCache_->Flush();
                if (persistentCache_) {
                    const bool forceMaintenance = bytesSinceMaintenance >= maintenanceWriteThreshold_;
                    const auto now = std::chrono::steady_clock::now();
                    const bool deferredMaintenanceDue = flush && now >= cacheMaintenanceNotBefore_;
                    if (forceMaintenance || deferredMaintenanceDue) {
                        persistentCache_->PerformMaintenance(forceMaintenance);
                        cacheMaintenanceNotBefore_ = now + std::chrono::seconds(30);
                    }
                    if (forceMaintenance) bytesSinceMaintenance = 0;
                }
            } catch (...) {
                QS_LOG_WARNING(L"Cache", L"Persistent cache writer discarded a failed write batch");
                // Persistent cache data is disposable. Drop only the failed
                // derivative and keep the writer alive for later work.
                bitmapWrite.result.reset();
                bytesSinceMaintenance = 0;
            }
            } catch (...) {
                // Queue extraction itself can allocate (for example, a record
                // batch vector). Keep allocation failure inside the thread entry
                // point instead of letting std::thread call std::terminate.
                QS_LOG_WARNING(L"Cache", L"Persistent cache writer paused after a queue-allocation failure");
                Sleep(50);
            }
        }
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
    }

} // namespace quicksift::app
