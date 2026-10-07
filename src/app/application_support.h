#pragma once

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

#include <windows.h>
#include <roapi.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <d2d1.h>
#include <d2d1_3.h>
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <wincodec.h>
#include <wincodecsdk.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <propsys.h>
#include <propkey.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <gdiplus.h>
#include <richedit.h>

#include "bundled_codecs.h"
#include "face_detector.h"
#include "embedded_metadata.h"
#include "persistent_cache.h"
#include "localization.h"
#include "runtime_support.h"
#include "core/app_types.h"
#include "core/catalog_policy.h"
#include "core/catalog_store.h"
#include "core/system_profile.h"
#include "review/view_transform_policy.h"
#include "platform/storage_space.h"
#include "diagnostics/diagnostic_log.h"
#include "review/thumbnail_prefetch_policy.h"
#include "metadata/xmp_simple_property.h"
#include "metadata/metadata_value_parser.h"
#include "core/history_partition.h"
#include "core/history_store.h"
#include "core/work_queue_policy.h"
#include "core/stable_file_identity.h"
#include "core/scoped_boolean_flag.h"
#include "ui/ui_command_ids.h"
#include "ui/ui_control_catalog.h"
#include "ui/ui_design_system.h"
#include "ui/framework/macos_ui_framework.h"
#include "ui/ui_layout_metrics.h"
#include "../resource.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cassert>
#include <cstring>
#include <climits>
#include <cwctype>
#include <cwchar>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <regex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quicksift::app {

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
using namespace quicksift::core;
using namespace quicksift::ui::command_id;
using namespace quicksift::ui::control_catalog;
using namespace quicksift::ui::layout;
using quicksift::ui::ThemePalette;
using quicksift::ui::BlendColor;
using quicksift::ui::framework::MacOsUiFramework;
using quicksift::ui::framework::ButtonPresentation;
using quicksift::ui::framework::ButtonRole;
using quicksift::ui::framework::ButtonGlyph;
using quicksift::ui::framework::ButtonVisualState;
using quicksift::ui::framework::MenuItem;

class GdiObjectOwner {
public:
    explicit GdiObjectOwner(HGDIOBJ object = nullptr) noexcept : object_(object) {}
    ~GdiObjectOwner() { reset(); }
    GdiObjectOwner(const GdiObjectOwner&) = delete;
    GdiObjectOwner& operator=(const GdiObjectOwner&) = delete;
    HGDIOBJ get() const noexcept { return object_; }
    void reset(HGDIOBJ replacement = nullptr) noexcept {
        if (object_) DeleteObject(object_);
        object_ = replacement;
    }
    HGDIOBJ release() noexcept {
        HGDIOBJ object = object_;
        object_ = nullptr;
        return object;
    }
private:
    HGDIOBJ object_ = nullptr;
};

// CODE GUIDE: See CODE_GUIDE.md -> "The authoritative owners".
// OWNER: Native class names, messages, timers, subclass IDs, and property keys only.

// Shared native-window contract.
// Owns class names, application messages, and timer identifiers used by the
// main window and its child controls. Do not duplicate these values elsewhere.

inline constexpr wchar_t kMainClass[] = L"QuickSift.MainWindow";

inline constexpr wchar_t kCanvasClass[] = L"QuickSift.Canvas";


inline constexpr wchar_t kDocumentClass[] = L"QuickSift.DocumentWindow";

inline constexpr wchar_t kAppTitle[] = L"QuickSift";

inline constexpr wchar_t kWindowTitle[] = L"QuickSift — Fast Photo Shortlisting";

// Named window properties and subclass identifiers keep callback ownership
// discoverable and prevent unrelated controls from accidentally sharing an ID.
inline constexpr wchar_t kDocumentKindProperty[] = L"QuickSift.DocumentKind";

inline constexpr wchar_t kDocumentStateProperty[] = L"QuickSift.DocumentState";



inline constexpr UINT_PTR kFolderTreeSubclassId = 2;

inline constexpr UINT_PTR kTitleOverlaySubclassId = 3;

inline constexpr UINT_PTR kDocumentWindowSubclassId = 77;

inline constexpr UINT WM_APP_BACKGROUND_COMPLETION = WM_APP + 10;
inline constexpr UINT WM_APP_FILE_TRANSACTION_COMPLETION = WM_APP + 11;
inline constexpr UINT WM_APP_METADATA_TRANSACTION_COMPLETION = WM_APP + 12;

inline constexpr UINT WM_APP_RESOURCE_PRESSURE = WM_APP + 13;
inline constexpr UINT WM_APP_STARTUP_READY = WM_APP + 14;


inline constexpr UINT_PTR ID_TIMER_IDLE_QUALITY = 1;

inline constexpr UINT_PTR ID_TIMER_CACHE_FLUSH = 2;

inline constexpr UINT_PTR ID_TIMER_AUTOHIDE = 3;

inline constexpr UINT_PTR ID_TIMER_TOAST = 4;

inline constexpr UINT_PTR ID_TIMER_METADATA_UI = 5;

inline constexpr UINT_PTR ID_TIMER_VIEW_RESOURCE_TRIM = 6;

inline constexpr UINT_PTR ID_TIMER_BACKGROUND_ANALYSIS = 7;
inline constexpr UINT_PTR ID_TIMER_FOLDER_SELECTION = 8;
inline constexpr UINT_PTR ID_TIMER_FILE_PROGRESS = 9;
inline constexpr UINT_PTR ID_TIMER_PERFORMANCE_HUD = 10;
inline constexpr UINT_PTR ID_TIMER_LOG_PERFORMANCE = 11;
inline constexpr UINT_PTR ID_TIMER_THUMBNAIL_RETRY_COOLDOWN = 12;
inline constexpr UINT_PTR ID_TIMER_ANIMATION = 13;
inline constexpr UINT_PTR ID_TIMER_DRAG_RENDER = 14;
inline constexpr UINT_PTR ID_TIMER_THROTTLED_SCHEDULE = 15;
// Shared virtualized-scroll frame pump (drag + wheel/keyboard): one paint/publish per frame.
inline constexpr UINT_PTR ID_TIMER_SCROLL_FRAME = 16;
// Settle debounce before visible decode enqueue after fling/coast ends.
inline constexpr UINT_PTR ID_TIMER_SCROLL_SETTLE = 17;

inline constexpr UINT_PTR kFaceWarningTooltipId = 1;

inline constexpr std::uint64_t kMebibyte = 1024ull * 1024ull;
inline constexpr std::uint64_t kGibibyte = 1024ull * 1024ull * 1024ull;
extern std::atomic<std::uint64_t> gMaximumDecodedPixelBytes;
extern std::atomic<UINT> gWicJpegIndexInterval;
extern std::atomic<unsigned> gRetainedWicDecoderSessions;
inline constexpr size_t kFallbackBitmapCacheLimit = 384ull * 1024ull * 1024ull;
inline constexpr int kTileSourceSize = 512;
inline constexpr int kFaceDecodeSize = 512;
inline constexpr int kFaceSharpnessDecodeSize = 1536;

struct DocumentWindowState {
    MacOsUiFramework* framework = nullptr;
    HWND edit = nullptr;
    HWND refresh = nullptr;
    HWND exportButton = nullptr;
    HWND verboseLogging = nullptr;
    HWND close = nullptr;
    bool richEdit = false;
    bool diagnosticLog = false;
    ThemePalette theme{};
    HBRUSH panelBrush = nullptr;
    HFONT normalFont = nullptr;
    HFONT semiboldFont = nullptr;
    bool titleCloseHot = false;
    bool titleClosePressed = false;
};

class QuickSiftApplicationImpl;


std::wstring NormalizeDocumentNewlines(std::wstring_view text);
std::vector<std::wstring> DocumentLines(std::wstring_view text);
bool IsDocumentRule(std::wstring_view line);
bool IsDocumentHeading(std::wstring_view line);
void AppendRtfEscaped(std::string& output, std::wstring_view text);
std::string BuildDocumentRtf(std::wstring_view text, bool helpDocument, const ThemePalette& theme);
DWORD CALLBACK StreamDocumentRtf(DWORD_PTR cookie, LPBYTE buffer, LONG requested, LONG* written) noexcept;
void SetDocumentControlText(HWND edit, std::wstring_view text, bool helpDocument, bool richEdit,
    const ThemePalette& theme);
bool EnsureRichEditLoaded();
LRESULT CALLBACK DocumentWindowSubclassProcImpl(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR, DWORD_PTR reference);
LRESULT CALLBACK DocumentWindowSubclassProc(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR subclassId, DWORD_PTR reference);


enum MetadataDirectFlag : std::uint32_t {
    MetadataDirectJpeg = 1u << 0,
    MetadataDirectPng = 1u << 1,
    MetadataDirectTiff = 1u << 2,
    MetadataDirectRaw = 1u << 3
};
inline constexpr std::uint32_t kDefaultMetadataDirectMask =
#if defined(QS_USE_EXIV2)
    MetadataDirectJpeg | MetadataDirectPng | MetadataDirectTiff;
#else
    0;
#endif
extern std::atomic<std::uint32_t> gMetadataDirectMask;

enum class TextFileEncoding {
    Utf8,
    Utf8Bom,
    Utf16LittleEndian,
    Utf16BigEndian
};
struct AtomicFileSnapshot {
    bool exists = false;
    std::uintmax_t size = 0;
    FILETIME lastWrite{};
    std::int64_t changeTime = 0;
    DWORD volumeSerial = 0;
    DWORD fileIndexHigh = 0;
    DWORD fileIndexLow = 0;
};
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
using quicksift::metadata::XmpPropertyUpdate;
using quicksift::metadata::UpdateXmpSimpleValue;

struct SidecarMetadataWriteResult {
    bool ok = false;
    bool conflict = false;
    PhotoMetadataValues before;
    PhotoMetadataValues after;
    std::wstring detail;
};

float Clamp(float value, float lo, float hi);
std::wstring ToLower(std::wstring text);
std::wstring ExtensionLower(const fs::path& path);
bool IsRawExtension(const std::wstring& ext);
bool IsJpegExtension(const std::wstring& ext);
void NormalizeSourceGeometryToPixelOrientation(int pixelWidth, int pixelHeight,
    int& sourceWidth, int& sourceHeight);
std::uint32_t MetadataDirectFlagForPath(const fs::path& path);
bool IsJpegPath(const fs::path& path);
bool UsesDirectMetadata(const fs::path& path);
std::int64_t MetadataStampFor(const fs::path& path);
std::wstring FormatFileSize(uintmax_t bytes);
std::optional<std::uint64_t> FreeBytesForPath(const fs::path& path);
bool HasStorageHeadroom(const fs::path& path, std::uint64_t requiredBytes,
    std::uint64_t reserveBytes, std::uint64_t* availableOut = nullptr);
std::optional<std::wstring> VolumeRootForPath(const fs::path& path);
std::uint64_t SaturatingAdd(std::uint64_t left, std::uint64_t right);
size_t SaturatingSizeAdd(size_t left, size_t right) noexcept;
std::wstring DecodeFailureKey(const std::wstring& path, int targetSize, JobKind kind, CacheClass cacheClass);
WICBitmapTransformOptions ReadOrientationTransform(IWICBitmapFrameDecode* frame, const fs::path& path);
bool IsWellFormedUtf16(std::wstring_view value) noexcept;
std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);
std::wstring ReadTextFile(const fs::path& path, TextFileEncoding* encoding = nullptr,
    bool* decodedSuccessfully = nullptr);
bool ExistingTextFileCouldNotBeDecoded(const fs::path& path, bool decodedSuccessfully);
std::vector<char> EncodeTextFile(const std::wstring& text, TextFileEncoding encoding);
bool CaptureAtomicFileSnapshotFromHandle(HANDLE handle, AtomicFileSnapshot& snapshot);
bool CaptureAtomicFileSnapshot(const fs::path& path, AtomicFileSnapshot& snapshot);
std::uint64_t WicFileIdentity(const fs::path& path);
bool SameAtomicFileSnapshot(const AtomicFileSnapshot& left, const AtomicFileSnapshot& right);
bool WriteAllBytes(HANDLE handle, const std::vector<char>& bytes);
bool WriteTextFile(const fs::path& path, const std::wstring& text, TextFileEncoding encoding,
    const AtomicFileSnapshot& expectedDestination);
fs::path SidecarPathFor(const fs::path& image);
int RatingFromShellValue(ULONG value);
bool WriteXmpProperties(const fs::path& image, std::initializer_list<XmpPropertyUpdate> updates);
std::optional<int> ReadShellRating(const fs::path& image);
std::wstring LabelName(int label);
std::wstring LabelDisplayName(int label);
SidecarMetadataWriteResult WritePhotoMetadataPatch(const fs::path& image,
    const MetadataPatch& patch) noexcept;
PhotoMetadataValues ReadPhotoMetadataFromStorage(const fs::path& image,
    MetadataStorageKind storage);
PhotoMetadataValues ReadPhotoMetadata(const fs::path& image);
std::wstring ReadPropertyDisplay(IPropertyStore* store, const wchar_t* canonicalName);
std::wstring BuildExifText(const fs::path& path);
std::optional<fs::path> PickFolder(HWND owner, const wchar_t* title);

struct CacheKey {
    std::wstring path;
    int size = 0;
    CacheClass cacheClass = CacheClass::Preview;
    bool operator==(const CacheKey& other) const noexcept {
        return size == other.size && cacheClass == other.cacheClass && path == other.path;
    }
};

struct CacheKeyHash {
    size_t operator()(const CacheKey& key) const noexcept {
        const std::uint64_t sizeHash =
            static_cast<std::uint64_t>(key.size) * 0x9E3779B185EBCA87ull;
        const std::uint64_t classShift =
            static_cast<std::uint64_t>(key.cacheClass) << 58;
        return std::hash<std::wstring>{}(key.path) ^
            static_cast<size_t>(sizeHash ^ classShift);
    }
};

struct BitmapEntry {
    ComPtr<ID2D1Bitmap> bitmap;
    int width = 0;
    int height = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    bool previewOnly = false;
    CacheClass cacheClass = CacheClass::Preview;
    size_t bytes = 0;
    uint64_t lastUse = 0;
    std::chrono::steady_clock::time_point lastUseTime = std::chrono::steady_clock::now();
    std::uint8_t hitCount = 1;
    bool frequent = false;
};

struct TileKey {
    std::wstring path;
    int x = 0, y = 0, width = 0, height = 0;
    int level = 0;
    bool operator==(const TileKey& other) const noexcept {
        return x == other.x && y == other.y && width == other.width && height == other.height &&
            level == other.level && path == other.path;
    }
};

struct TileKeyHash {
    size_t operator()(const TileKey& key) const noexcept {
        size_t value = std::hash<std::wstring>{}(key.path);
        value ^= static_cast<size_t>(key.x) * 0x9E3779B1u;
        value ^= static_cast<size_t>(key.y) * 0x85EBCA77u;
        value ^= static_cast<size_t>(key.width) << 17;
        value ^= static_cast<size_t>(key.height) << 31;
        value ^= static_cast<size_t>(key.level) * 0xC2B2AE3Du;
        return value;
    }
};

struct WicImageSourceEntry {
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<ID2D1ImageSourceFromWic> imageSource;
    std::uint64_t identity = 0;
    std::uint64_t lastUse = 0;
    std::chrono::steady_clock::time_point lastUseTime = std::chrono::steady_clock::now();
    UINT width = 0;
    UINT height = 0;
    D2D1_RECT_U lastTrim{};
    bool hasTrim = false;
    bool offered = false;
};

struct TileEntry {
    ComPtr<ID2D1Bitmap> bitmap;
    int sourceWidth = 0;
    int sourceHeight = 0;
    size_t bytes = 0;
    uint64_t lastUse = 0;
    std::chrono::steady_clock::time_point lastUseTime = std::chrono::steady_clock::now();
};

using ViewState = quicksift::review::transform::ViewState;

struct ColorBlob {
    float x = 0.5f;
    float y = 0.5f;
    float radius = 0.25f;
    COLORREF color = RGB(70, 120, 255);
};

enum class TrafficLight {
    None,
    Close,
    Minimize,
    Zoom
};

enum class FlyoutPanel {
    None,
    File,
    Cull,
    Filter,
    Folders,
    Info,
    Settings
};

using GlassAlertKind = quicksift::ui::framework::AlertKind;
using GlassAlertMode = quicksift::ui::framework::AlertMode;

enum class PendingGlassAction {
    None,
    DeleteSelection,
    CloseWithPendingOperations,
    // Drop a guarded Undo/Redo entry after on-disk identity diverged from history.
    RelinquishUndoHistory,
    RelinquishRedoHistory,
    // Accept current on-disk metadata/identity as the catalog baseline after a
    // rating/pick/label write was refused due to external change.
    RefreshMetadataFromDisk
};

inline constexpr size_t kInvalidVisiblePosition = quicksift::core::CatalogStore::InvalidVisiblePosition;
enum class DocumentKind : ULONG_PTR { Help = 1, About = 2, DiagnosticLog = 3 };
struct AdaptivePrefetchPolicy {
    int extraThumbnailRows = 1;
    int depth = 2;
    int currentTarget = 2048;
    int prefetchTarget = 1280;
    float thumbnailPredictionHorizonSeconds = 0.35f;
};
struct VisibleTreeMetrics {
    int count = 0;
    int firstIndex = 0;
};

} // namespace quicksift::app
