// CODE GUIDE: See CODE_GUIDE.md -> "Rules for sizes and arithmetic".
// OWNER: SQLite/bitmap cache implementation; validate all persisted values before trusting them.

#include "persistent_cache.h"
#include "core/image_formats.h"
#include "platform/application_data_paths.h"
#include "diagnostics/diagnostic_log.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cerrno>
#endif
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <climits>
#include <cstring>
#include <cwctype>
#if !defined(_WIN32)
#include <codecvt>
#include <locale>
#endif
#include <fstream>
#include <iomanip>
#include <limits>
#include <queue>
#include <sstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace quicksift {
namespace {

constexpr std::array<char, 8> kBitmapMagic{ 'Q', 'P', 'S', 'B', 'G', 'R', 'A', '7' };
constexpr std::uint32_t kBitmapVersion = 7;
constexpr int kDatabaseSchemaVersion = 1;
constexpr std::uint64_t kMebibyte = 1024ull * 1024ull;
constexpr std::uint64_t kGibibyte = 1024ull * 1024ull * 1024ull;
constexpr std::uint64_t kMaximumBitmapBytes = 1024ull * 1024ull * 1024ull;
constexpr std::uint32_t kMaximumSourceDimension = 1'000'000;
constexpr auto kMaintenanceInterval = std::chrono::minutes(30);
constexpr auto kBitmapMaximumAge = std::chrono::hours(24 * 60);
constexpr auto kTemporaryMaximumAge = std::chrono::hours(24);
constexpr auto kAccessTouchInterval = std::chrono::hours(12);
constexpr std::int64_t kRecordMaximumAgeSeconds = 180ll * 24ll * 60ll * 60ll;
constexpr std::size_t kMaintenanceDeleteBatch = 4096;
constexpr std::uint32_t kMaximumExifCharacters = 65'536;
constexpr std::uint32_t kMaximumFacesPerRecord = 512;

bool IsValidNormalizedFaceRect(const NormalizedFaceRect& face) noexcept {
    const bool finite = std::isfinite(face.x) && std::isfinite(face.y) &&
        std::isfinite(face.width) && std::isfinite(face.height);
    return finite && face.x >= 0.0f && face.y >= 0.0f &&
        face.width > 0.0f && face.height > 0.0f &&
        face.x <= 1.0f && face.y <= 1.0f &&
        face.width <= 1.0f && face.height <= 1.0f &&
        face.x + face.width <= 1.001f && face.y + face.height <= 1.001f;
}

#pragma pack(push, 1)
struct BitmapHeader {
    char magic[8];
    std::uint32_t version;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t sourceWidth;
    std::uint32_t sourceHeight;
    std::uint32_t targetSize;
    std::int32_t cacheClass;
    std::uint64_t fileSize;
    std::int64_t modifiedStamp;
    std::int64_t sidecarStamp;
    std::uint64_t pixelBytes;
    std::uint8_t previewOnly;
    std::uint8_t reserved[7];
    std::uint64_t contentChecksum;
};
#pragma pack(pop)

std::uint64_t Fnv1a64(const void* bytes, std::size_t size, std::uint64_t seed = 1469598103934665603ull) {
    const auto* data = static_cast<const std::uint8_t*>(bytes);
    std::uint64_t hash = seed;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

template <typename T>
void HashValue(std::uint64_t& hash, const T& value) { hash = Fnv1a64(&value, sizeof(value), hash); }

std::uint64_t BitmapContentChecksum(BitmapHeader header, std::span<const std::uint8_t> pixels) {
    // The checksum covers both header metadata and pixels. Clear its own field so
    // a cached value does not participate in recomputing itself.
    header.contentChecksum = 0;
    std::uint64_t hash = Fnv1a64(&header, sizeof(header));
    return Fnv1a64(pixels.data(), pixels.size(), hash);
}

bool CanPruneMissingSource(const fs::path& path) {
#if defined(_WIN32)
    wchar_t volumePath[MAX_PATH]{};
    if (!GetVolumePathNameW(path.c_str(), volumePath, static_cast<DWORD>(MAX_PATH)))
        return false;
    const UINT driveType = GetDriveTypeW(volumePath);
    // A disconnected card, optical disc, or network share is not evidence that
    // its photos were deleted. Let age/size policies retire those records instead.
    return driveType == DRIVE_FIXED || driveType == DRIVE_RAMDISK;
#else
    (void)path;
    return true;
#endif
}

bool ReadExact(std::ifstream& stream, void* output, std::size_t bytes) {
    if (bytes == 0) return true;
    stream.read(static_cast<char*>(output), static_cast<std::streamsize>(bytes));
    return stream.good();
}

#if defined(_WIN32)
using NativeOutputFile = HANDLE;
const NativeOutputFile kInvalidNativeOutputFile = INVALID_HANDLE_VALUE;
#else
using NativeOutputFile = int;
constexpr NativeOutputFile kInvalidNativeOutputFile = -1;
#endif

bool NativeOutputFileIsValid(NativeOutputFile file) noexcept {
    return file != kInvalidNativeOutputFile;
}

void CloseNativeOutputFile(NativeOutputFile file) noexcept {
    if (!NativeOutputFileIsValid(file)) return;
#if defined(_WIN32)
    CloseHandle(file);
#else
    close(file);
#endif
}

bool WriteNativeOutputFile(NativeOutputFile file, const void* input, std::size_t bytes) {
    const auto* cursor = static_cast<const std::uint8_t*>(input);
    while (bytes > 0) {
#if defined(_WIN32)
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes, MAXDWORD));
        DWORD written = 0;
        if (!WriteFile(file, cursor, chunk, &written, nullptr) || written == 0) return false;
        cursor += written;
        bytes -= written;
#else
        const std::size_t maximumChunk = static_cast<std::size_t>(std::numeric_limits<ssize_t>::max());
        const ssize_t written = write(file, cursor, std::min(bytes, maximumChunk));
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (written == 0) return false;
        cursor += static_cast<std::size_t>(written);
        bytes -= static_cast<std::size_t>(written);
#endif
    }
    return true;
}

bool FlushNativeOutputFile(NativeOutputFile file) noexcept {
#if defined(_WIN32)
    return FlushFileBuffers(file) != FALSE;
#else
    return fsync(file) == 0;
#endif
}

NativeOutputFile CreateNewNativeOutputFile(const fs::path& path) noexcept {
#if defined(_WIN32)
    return CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
#else
    return open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
#endif
}

bool CreateUniqueTemporaryFile(const fs::path& base, fs::path& path, NativeOutputFile& file) {
    static std::atomic<std::uint64_t> sequence{0};
#if defined(_WIN32)
    const auto process = static_cast<unsigned long long>(GetCurrentProcessId());
#else
    const auto process = static_cast<unsigned long long>(getpid());
#endif
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        path = base;
        path += L"." + std::to_wstring(process) + L"." +
            std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed)) + L".tmp";
        file = CreateNewNativeOutputFile(path);
        if (NativeOutputFileIsValid(file)) return true;
#if defined(_WIN32)
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) return false;
#else
        if (errno != EEXIST) return false;
#endif
    }
    return false;
}

bool ReplaceFileAtomically(const fs::path& temporary, const fs::path& destination) {
#if defined(_WIN32)
    return MoveFileExW(temporary.c_str(), destination.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code ec;
    fs::rename(temporary, destination, ec);
    return !ec;
#endif
}

fs::path SidecarPath(const fs::path& image) {
    fs::path result = image;
    std::wstring extension = result.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
    });
    if (quicksift::core::IsCameraRawExtension(extension)) result.replace_extension(L".xmp");
    else result += L".xmp";
    return result;
}

std::wstring LowerPath(const fs::path& path) {
    std::wstring value;
    try { value = fs::weakly_canonical(path).wstring(); }
    catch (...) { value = path.lexically_normal().wstring(); }
#if defined(_WIN32)
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
    });
#endif
    return value;
}

std::wstring HexHash(std::uint64_t hash) {
    std::wostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill(L'0') << hash;
    return stream.str();
}

struct SqlStatement {
    sqlite3_stmt* value = nullptr;
    ~SqlStatement() { if (value) sqlite3_finalize(value); }
    SqlStatement(const SqlStatement&) = delete;
    SqlStatement& operator=(const SqlStatement&) = delete;
    SqlStatement() = default;
};

bool Prepare(sqlite3* db, const char* sql, SqlStatement& statement) {
    return db && sqlite3_prepare_v2(db, sql, -1, &statement.value, nullptr) == SQLITE_OK;
}

#if defined(_WIN32)
void BindText16(sqlite3_stmt* statement, int index, const std::wstring& value) {
    const std::size_t maximumCharacters = static_cast<std::size_t>(INT_MAX) / sizeof(wchar_t);
    const std::size_t characters = std::min(value.size(), maximumCharacters);
    sqlite3_bind_text16(statement, index, value.data(),
        static_cast<int>(characters * sizeof(wchar_t)), SQLITE_TRANSIENT);
}

std::wstring ColumnText16(sqlite3_stmt* statement, int index) {
    const auto* text = static_cast<const wchar_t*>(sqlite3_column_text16(statement, index));
    const int bytes = sqlite3_column_bytes16(statement, index);
    if (!text || bytes <= 0) return {};
    return std::wstring(text, text + bytes / static_cast<int>(sizeof(wchar_t)));
}

int OpenSqlitePath(const fs::path& path, sqlite3** database) {
    return sqlite3_open16(path.c_str(), database);
}
#else
std::string WideToUtf8(const std::wstring& value) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.to_bytes(value);
}

std::wstring Utf8ToWide(const char* value, int bytes) {
    if (!value || bytes <= 0) return {};
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.from_bytes(value, value + bytes);
}

void BindText16(sqlite3_stmt* statement, int index, const std::wstring& value) {
    const std::string utf8 = WideToUtf8(value);
    sqlite3_bind_text(statement, index, utf8.data(),
        static_cast<int>(std::min<std::size_t>(utf8.size(), INT_MAX)), SQLITE_TRANSIENT);
}

std::wstring ColumnText16(sqlite3_stmt* statement, int index) {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, index));
    return Utf8ToWide(text, sqlite3_column_bytes(statement, index));
}

int OpenSqlitePath(const fs::path& path, sqlite3** database) {
    const std::u8string utf8Path = path.u8string();
    const std::string utf8(reinterpret_cast<const char*>(utf8Path.data()), utf8Path.size());
    return sqlite3_open(utf8.c_str(), database);
}
#endif

constexpr char kUpsertRecordSql[] =
    "INSERT INTO image_records(path,file_size,modified_stamp,sidecar_stamp,rating,color_label,pick_state,"
    "source_width,source_height,exif_text,faces,faces_scanned,face_blur_scanned,face_blurry,face_sharpness,last_access,"
    "rating_knowledge,color_label_knowledge,pick_state_knowledge,raw_rating,raw_color_label,raw_pick_state)"
    " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18,?19,?20,?21,?22)"
    " ON CONFLICT(path) DO UPDATE SET file_size=excluded.file_size,modified_stamp=excluded.modified_stamp,"
    "sidecar_stamp=excluded.sidecar_stamp,rating=excluded.rating,color_label=excluded.color_label,"
    "pick_state=excluded.pick_state,rating_knowledge=excluded.rating_knowledge,"
    "color_label_knowledge=excluded.color_label_knowledge,pick_state_knowledge=excluded.pick_state_knowledge,"
    "raw_rating=excluded.raw_rating,raw_color_label=excluded.raw_color_label,raw_pick_state=excluded.raw_pick_state,"
    "source_width=excluded.source_width,source_height=excluded.source_height,"
    "exif_text=CASE WHEN image_records.file_size=excluded.file_size AND "
    "image_records.modified_stamp=excluded.modified_stamp AND image_records.sidecar_stamp=excluded.sidecar_stamp "
    "AND length(excluded.exif_text)=0 THEN image_records.exif_text ELSE excluded.exif_text END,"
    "faces=CASE WHEN image_records.file_size=excluded.file_size AND "
    "image_records.modified_stamp=excluded.modified_stamp AND image_records.sidecar_stamp=excluded.sidecar_stamp "
    "AND excluded.faces_scanned=0 THEN image_records.faces ELSE excluded.faces END,"
    "faces_scanned=CASE WHEN image_records.file_size=excluded.file_size AND "
    "image_records.modified_stamp=excluded.modified_stamp AND image_records.sidecar_stamp=excluded.sidecar_stamp "
    "AND excluded.faces_scanned=0 THEN image_records.faces_scanned ELSE excluded.faces_scanned END,"
    "face_blur_scanned=CASE WHEN image_records.file_size=excluded.file_size AND "
    "image_records.modified_stamp=excluded.modified_stamp AND image_records.sidecar_stamp=excluded.sidecar_stamp "
    "AND excluded.faces_scanned=0 THEN image_records.face_blur_scanned ELSE excluded.face_blur_scanned END,"
    "face_blurry=CASE WHEN image_records.file_size=excluded.file_size AND "
    "image_records.modified_stamp=excluded.modified_stamp AND image_records.sidecar_stamp=excluded.sidecar_stamp "
    "AND excluded.faces_scanned=0 THEN image_records.face_blurry ELSE excluded.face_blurry END,"
    "face_sharpness=CASE WHEN image_records.file_size=excluded.file_size AND "
    "image_records.modified_stamp=excluded.modified_stamp AND image_records.sidecar_stamp=excluded.sidecar_stamp "
    "AND excluded.faces_scanned=0 THEN image_records.face_sharpness ELSE excluded.face_sharpness END,"
    "last_access=excluded.last_access;";

void BindRecord(sqlite3_stmt* statement, const std::wstring& key, const CachedImageRecord& record) {
    BindText16(statement, 1, key);
    sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(record.fileSize));
    sqlite3_bind_int64(statement, 3, record.modifiedStamp);
    sqlite3_bind_int64(statement, 4, record.sidecarStamp);
    sqlite3_bind_int(statement, 5, record.rating);
    sqlite3_bind_int(statement, 6, record.colorLabel);
    sqlite3_bind_int(statement, 7, record.pickState);
    sqlite3_bind_int(statement, 8, record.sourceWidth);
    sqlite3_bind_int(statement, 9, record.sourceHeight);
    BindText16(statement, 10, record.exifText);
    if (record.faces.empty()) sqlite3_bind_null(statement, 11);
    else sqlite3_bind_blob(statement, 11, record.faces.data(),
        static_cast<int>(record.faces.size() * sizeof(NormalizedFaceRect)), SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 12, record.facesScanned ? 1 : 0);
    sqlite3_bind_int(statement, 13, record.faceBlurScanned ? 1 : 0);
    sqlite3_bind_int(statement, 14, record.faceBlurry ? 1 : 0);
    sqlite3_bind_double(statement, 15, static_cast<double>(record.faceSharpness));
    sqlite3_bind_int64(statement, 16, record.lastAccessStamp);
    sqlite3_bind_int(statement, 17, static_cast<int>(record.ratingKnowledge));
    sqlite3_bind_int(statement, 18, static_cast<int>(record.colorLabelKnowledge));
    sqlite3_bind_int(statement, 19, static_cast<int>(record.pickStateKnowledge));
    BindText16(statement, 20, record.rawRating);
    BindText16(statement, 21, record.rawColorLabel);
    BindText16(statement, 22, record.rawPickState);
}

} // namespace

PersistentCache::PersistentCache()
    : baseDirectory_(quicksift::platform::CacheDirectoryPath()) {}

PersistentCache::PersistentCache(fs::path baseDirectory)
    : baseDirectory_(std::move(baseDirectory)) {}

bool PersistentCache::InitializeStorage() {
    if (baseDirectory_.empty()) return false;
    bitmapDirectory_ = baseDirectory_ / L"bitmaps";
    pyramidDirectory_ = baseDirectory_ / L"pyramid";
    databasePath_ = baseDirectory_ / L"metadata.sqlite3";
    std::error_code ec;
    fs::create_directories(bitmapDirectory_, ec);
    ec.clear();
    fs::create_directories(pyramidDirectory_, ec);
    ConfigureStoragePolicy();
    const bool opened = OpenDatabase();
    QS_LOG_INFO(L"Cache", opened ? L"Persistent cache initialized asynchronously" : L"Persistent cache initialization failed; continuing without disk cache");
    return opened;
}

PersistentCache::~PersistentCache() {
    // The cache is disposable. Do not force a WAL restart checkpoint during process
    // teardown: it can turn shutdown into a synchronous disk operation and keeps
    // metadata.sqlite3/-wal/-shm handles open while the UI is already gone.
    CloseForShutdown();
}

std::int64_t PersistentCache::CurrentAccessStamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::int64_t PersistentCache::FileTimeStamp(const fs::file_time_type& value) {
    // A duration_cast to nanoseconds can overflow for corrupted or extreme
    // filesystem timestamps on clocks whose native period is coarser. The cache
    // needs only a stable change token, not a wall-clock nanosecond value.
    std::uint64_t hash = 1469598103934665603ull;
    const auto nativeCount = value.time_since_epoch().count();
    HashValue(hash, nativeCount);
    constexpr auto numerator = fs::file_time_type::duration::period::num;
    constexpr auto denominator = fs::file_time_type::duration::period::den;
    HashValue(hash, numerator);
    HashValue(hash, denominator);
    const std::int64_t stamp = static_cast<std::int64_t>(hash & 0x7fffffffffffffffull);
    return stamp == 0 ? 1 : stamp;
}

std::int64_t PersistentCache::SourceStampFor(const fs::path& imagePath) {
    std::uint64_t hash = 1469598103934665603ull;
#if defined(_WIN32)
    HANDLE handle = CreateFileW(imagePath.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info{};
        FILE_BASIC_INFO basic{};
        const bool ok = GetFileInformationByHandle(handle, &info) != FALSE &&
            GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) != FALSE;
        CloseHandle(handle);
        if (ok && (info.dwFileAttributes &
            (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0) {
            HashValue(hash, info.dwVolumeSerialNumber);
            HashValue(hash, info.nFileIndexHigh);
            HashValue(hash, info.nFileIndexLow);
            HashValue(hash, info.nFileSizeHigh);
            HashValue(hash, info.nFileSizeLow);
            HashValue(hash, basic.LastWriteTime.QuadPart);
            HashValue(hash, basic.ChangeTime.QuadPart);
            const std::int64_t stamp = static_cast<std::int64_t>(hash & 0x7fffffffffffffffull);
            return stamp == 0 ? 1 : stamp;
        }
    }
#else
    struct stat info{};
    if (lstat(imagePath.c_str(), &info) == 0 && S_ISREG(info.st_mode) && !S_ISLNK(info.st_mode)) {
        HashValue(hash, info.st_dev);
        HashValue(hash, info.st_ino);
        HashValue(hash, info.st_size);
        HashValue(hash, info.st_mtim.tv_sec);
        HashValue(hash, info.st_mtim.tv_nsec);
        HashValue(hash, info.st_ctim.tv_sec);
        HashValue(hash, info.st_ctim.tv_nsec);
        const std::int64_t stamp = static_cast<std::int64_t>(hash & 0x7fffffffffffffffull);
        return stamp == 0 ? 1 : stamp;
    }
#endif
    // Some network providers do not expose stable file IDs or change time.
    // Retain compatibility by falling back to the filesystem timestamp rather
    // than disabling the cache for those sources.
    std::error_code error;
    const auto modified = fs::last_write_time(imagePath, error);
    return error ? 0 : FileTimeStamp(modified);
}

std::int64_t PersistentCache::SidecarStampFor(const fs::path& imagePath) {
    const fs::path sidecar = SidecarPath(imagePath);
    std::uint64_t hash = 1469598103934665603ull;
#if defined(_WIN32)
    HANDLE handle = CreateFileW(sidecar.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return 0;
    BY_HANDLE_FILE_INFORMATION info{};
    FILE_BASIC_INFO basic{};
    const bool ok = GetFileInformationByHandle(handle, &info) != FALSE &&
        GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) != FALSE;
    CloseHandle(handle);
    if (!ok || (info.dwFileAttributes &
        (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0) return 0;
    HashValue(hash, info.dwVolumeSerialNumber);
    HashValue(hash, info.nFileIndexHigh);
    HashValue(hash, info.nFileIndexLow);
    HashValue(hash, info.nFileSizeHigh);
    HashValue(hash, info.nFileSizeLow);
    HashValue(hash, basic.LastWriteTime.QuadPart);
    HashValue(hash, basic.ChangeTime.QuadPart);
#else
    struct stat info{};
    if (lstat(sidecar.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || S_ISLNK(info.st_mode)) return 0;
    HashValue(hash, info.st_dev);
    HashValue(hash, info.st_ino);
    HashValue(hash, info.st_size);
    HashValue(hash, info.st_mtim.tv_sec);
    HashValue(hash, info.st_mtim.tv_nsec);
    HashValue(hash, info.st_ctim.tv_sec);
    HashValue(hash, info.st_ctim.tv_nsec);
#endif
    const std::int64_t stamp = static_cast<std::int64_t>(hash & 0x7fffffffffffffffull);
    return stamp == 0 ? 1 : stamp;
}

std::wstring PersistentCache::NormalizeKey(const fs::path& path) const { return LowerPath(path); }

bool PersistentCache::OpenDatabase() {
    std::lock_guard lock(databaseMutex_);
    if (database_) return true;
    std::error_code ec;
    fs::create_directories(baseDirectory_, ec);

    auto removeDatabaseFiles = [&]() noexcept {
        for (const fs::path& path : { databasePath_,
                fs::path(databasePath_.wstring() + L"-wal"),
                fs::path(databasePath_.wstring() + L"-shm") }) {
            ec.clear();
            fs::remove(path, ec);
        }
    };

    auto openOnce = [&](bool requireCurrentSchema) -> std::pair<bool, int> {
        if (OpenSqlitePath(databasePath_, &database_) != SQLITE_OK) {
            const int error = database_ ? sqlite3_errcode(database_) : SQLITE_CANTOPEN;
            if (database_) sqlite3_close_v2(database_);
            database_ = nullptr;
            return { false, error };
        }
        sqlite3_busy_timeout(database_, 2500);

        if (requireCurrentSchema) {
            SqlStatement versionQuery;
            if (!Prepare(database_, "PRAGMA user_version;", versionQuery) ||
                sqlite3_step(versionQuery.value) != SQLITE_ROW ||
                sqlite3_column_int(versionQuery.value, 0) != kDatabaseSchemaVersion) {
                sqlite3_close_v2(database_);
                database_ = nullptr;
                return { false, SQLITE_SCHEMA };
            }
        }

        if (InitializeSchema()) return { true, SQLITE_OK };
        const int error = sqlite3_errcode(database_);
        sqlite3_close_v2(database_);
        database_ = nullptr;
        return { false, error };
    };

    ec.clear();
    const bool databaseExisted = fs::is_regular_file(databasePath_, ec) && !ec;
    auto [opened, error] = openOnce(databaseExisted);
    if (opened) return true;
    if (error != SQLITE_SCHEMA && error != SQLITE_CORRUPT && error != SQLITE_NOTADB) return false;

    // The cache database is disposable. Older/unversioned schemas are deliberately
    // unsupported at the public-release boundary: rebuild rather than carrying
    // migration code forward indefinitely. Corrupt databases use the same path.
    removeDatabaseFiles();
    return openOnce(false).first;
}

void PersistentCache::CloseDatabase() {
    std::lock_guard lock(databaseMutex_);
    if (!database_) return;
    // Never checkpoint as part of generic handle closure. Explicit Flush() remains
    // available for user-requested cache synchronization; shutdown needs fast handle
    // release, and SQLite's WAL is designed to recover committed frames on reopen.
    sqlite3_close_v2(database_);
    database_ = nullptr;
}

void PersistentCache::CloseForShutdown() noexcept {
    try { CloseDatabase(); } catch (...) {}
}

bool PersistentCache::InitializeSchema() {
    if (!database_) return false;
    const std::string cachePragma = "PRAGMA cache_size=-" + std::to_string(sqlitePageCacheKiB_) + ";";
    const std::string sql =
        std::string("PRAGMA journal_mode=WAL;") +
        "PRAGMA synchronous=NORMAL;" +
        (sqliteTempStoreMemory_ ? "PRAGMA temp_store=MEMORY;" : "PRAGMA temp_store=FILE;") +
        "PRAGMA wal_autocheckpoint=1000;" +
        "PRAGMA mmap_size=" + std::to_string(sqliteMmapBytes_) + ";" +
        "PRAGMA journal_size_limit=" + std::to_string(sqliteJournalLimitBytes_) + ";" +
        "CREATE TABLE IF NOT EXISTS image_records("
        " path TEXT PRIMARY KEY NOT NULL,"
        " file_size INTEGER NOT NULL, modified_stamp INTEGER NOT NULL, sidecar_stamp INTEGER NOT NULL,"
        " rating INTEGER NOT NULL, color_label INTEGER NOT NULL, pick_state INTEGER NOT NULL,"
        " source_width INTEGER NOT NULL, source_height INTEGER NOT NULL,"
        " exif_text TEXT, faces BLOB, faces_scanned INTEGER NOT NULL,"
        " face_blur_scanned INTEGER NOT NULL DEFAULT 0, face_blurry INTEGER NOT NULL DEFAULT 0,"
        " face_sharpness REAL NOT NULL DEFAULT 0, last_access INTEGER NOT NULL,"
        " rating_knowledge INTEGER NOT NULL DEFAULT 1,"
        " color_label_knowledge INTEGER NOT NULL DEFAULT 1,"
        " pick_state_knowledge INTEGER NOT NULL DEFAULT 1,"
        " raw_rating TEXT NOT NULL DEFAULT '', raw_color_label TEXT NOT NULL DEFAULT '',"
        " raw_pick_state TEXT NOT NULL DEFAULT '');"
        "CREATE INDEX IF NOT EXISTS image_records_access ON image_records(last_access);"
        "CREATE TABLE IF NOT EXISTS library_folders("
        " path TEXT PRIMARY KEY NOT NULL,"
        " last_opened INTEGER NOT NULL,"
        " display_path TEXT);"
        "CREATE TABLE IF NOT EXISTS photo_comments(path TEXT PRIMARY KEY NOT NULL, comment TEXT NOT NULL DEFAULT '');"
        "PRAGMA user_version=" + std::to_string(kDatabaseSchemaVersion) + ";";
    char* error = nullptr;
    const int first = sqlite3_exec(database_, cachePragma.c_str(), nullptr, nullptr, &error);
    if (error) sqlite3_free(error);
    error = nullptr;
    const int second = sqlite3_exec(database_, sql.c_str(), nullptr, nullptr, &error);
    if (error) sqlite3_free(error);
    if (first == SQLITE_OK && second == SQLITE_OK) {
        sqlite3_exec(database_, "ALTER TABLE library_folders ADD COLUMN display_path TEXT;", nullptr, nullptr, nullptr);
        sqlite3_exec(database_, "UPDATE library_folders SET display_path=path WHERE display_path IS NULL;", nullptr, nullptr, nullptr);
    }
    return first == SQLITE_OK && second == SQLITE_OK;
}


void PersistentCache::AddLibraryFolder(const fs::path& path, std::int64_t timestamp) {
    const std::wstring key = NormalizeKey(path);
    const std::wstring display = path.wstring();
    std::lock_guard lock(databaseMutex_);
    if (!database_) return;
    SqlStatement stmt;
    if (!Prepare(database_, "INSERT OR REPLACE INTO library_folders(path,last_opened,display_path) VALUES(?1,?2,?3);", stmt)) return;
    BindText16(stmt.value, 1, key);
    sqlite3_bind_int64(stmt.value, 2, timestamp);
    BindText16(stmt.value, 3, display);
    sqlite3_step(stmt.value);
}

std::vector<std::pair<std::wstring, std::int64_t>> PersistentCache::GetLibraryFolders() {
    std::vector<std::pair<std::wstring, std::int64_t>> result;
    std::lock_guard lock(databaseMutex_);
    if (!database_) return result;
    SqlStatement stmt;
    if (!Prepare(database_, "SELECT COALESCE(display_path,path),last_opened FROM library_folders ORDER BY last_opened DESC;", stmt)) return result;
    while (sqlite3_step(stmt.value) == SQLITE_ROW) {
        const std::wstring path = ColumnText16(stmt.value, 0);
        const std::int64_t time = sqlite3_column_int64(stmt.value, 1);
        if (!path.empty()) result.emplace_back(path, time);
    }
    return result;
}

void PersistentCache::RemoveLibraryFolder(const fs::path& path) {
    const std::wstring key = NormalizeKey(path);
    std::lock_guard lock(databaseMutex_);
    if (!database_) return;
    SqlStatement stmt;
    if (!Prepare(database_, "DELETE FROM library_folders WHERE path=?1;", stmt)) return;
    BindText16(stmt.value, 1, key);
    sqlite3_step(stmt.value);
}

void PersistentCache::WipeFolderCache(const fs::path& path) {
    std::wstring key = NormalizeKey(path);
    if (!key.empty() && key.back() != L'\\') key.push_back(L'\\');
    key += L"%";
    std::wstring escaped;
    escaped.reserve(key.size() * 2);
    for (wchar_t ch : key) {
        if (ch == L'%' || ch == L'_' || ch == L'\\') escaped.push_back(L'\\');
        escaped.push_back(ch);
    }
    std::lock_guard lock(databaseMutex_);
    if (!database_) return;
    SqlStatement stmt;
    if (!Prepare(database_, "DELETE FROM image_records WHERE path LIKE ?1 ESCAPE '\\';", stmt)) return;
    BindText16(stmt.value, 1, escaped);
    sqlite3_step(stmt.value);
    SqlStatement commentStmt;
    if (Prepare(database_, "DELETE FROM photo_comments WHERE path LIKE ?1 ESCAPE '\\';", commentStmt)) {
        BindText16(commentStmt.value, 1, escaped);
        sqlite3_step(commentStmt.value);
    }
}

std::optional<CachedImageRecord> PersistentCache::LookupRecord(
    const fs::path& path, std::uint64_t fileSize, std::int64_t modifiedStamp,
    std::int64_t sidecarStamp, bool includeExifText, bool includeFaces) {
    const std::wstring key = NormalizeKey(path);
    std::lock_guard lock(databaseMutex_);
    if (!database_) return std::nullopt;
    SqlStatement query;
    if (!Prepare(database_,
        "SELECT file_size,modified_stamp,sidecar_stamp,rating,color_label,pick_state,"
        "source_width,source_height,exif_text,faces,faces_scanned,face_blur_scanned,face_blurry,face_sharpness,last_access,"
        "rating_knowledge,color_label_knowledge,pick_state_knowledge,raw_rating,raw_color_label,raw_pick_state "
        "FROM image_records WHERE path=?1;", query)) return std::nullopt;
    BindText16(query.value, 1, key);
    if (sqlite3_step(query.value) != SQLITE_ROW) return std::nullopt;
    const sqlite3_int64 storedFileSize = sqlite3_column_int64(query.value, 0);
    if (storedFileSize < 0 || static_cast<std::uint64_t>(storedFileSize) != fileSize ||
        sqlite3_column_int64(query.value, 1) != modifiedStamp ||
        sqlite3_column_int64(query.value, 2) != sidecarStamp) return std::nullopt;

    CachedImageRecord record;
    record.fileSize = fileSize;
    record.modifiedStamp = modifiedStamp;
    record.sidecarStamp = sidecarStamp;
    record.rating = sqlite3_column_int(query.value, 3);
    record.colorLabel = sqlite3_column_int(query.value, 4);
    record.pickState = sqlite3_column_int(query.value, 5);
    const int ratingKnowledge = sqlite3_column_int(query.value, 15);
    const int labelKnowledge = sqlite3_column_int(query.value, 16);
    const int pickKnowledge = sqlite3_column_int(query.value, 17);
    if (ratingKnowledge < 0 || ratingKnowledge > 3 || labelKnowledge < 0 ||
        labelKnowledge > 3 || pickKnowledge < 0 || pickKnowledge > 3) return std::nullopt;
    record.ratingKnowledge = static_cast<core::MetadataKnowledge>(ratingKnowledge);
    record.colorLabelKnowledge = static_cast<core::MetadataKnowledge>(labelKnowledge);
    record.pickStateKnowledge = static_cast<core::MetadataKnowledge>(pickKnowledge);
    record.rawRating = ColumnText16(query.value, 18);
    record.rawColorLabel = ColumnText16(query.value, 19);
    record.rawPickState = ColumnText16(query.value, 20);
    record.sourceWidth = sqlite3_column_int(query.value, 6);
    record.sourceHeight = sqlite3_column_int(query.value, 7);
    if (record.rating < 0 || record.rating > 5 ||
        record.colorLabel < 0 || record.colorLabel > 5 ||
        record.pickState < -1 || record.pickState > 1 ||
        record.sourceWidth < 0 ||
        record.sourceWidth > static_cast<int>(kMaximumSourceDimension) ||
        record.sourceHeight < 0 ||
        record.sourceHeight > static_cast<int>(kMaximumSourceDimension)) {
        return std::nullopt;
    }
    if (includeExifText) {
        const int exifBytes = sqlite3_column_bytes16(query.value, 8);
        constexpr int kMaximumExifBytes = static_cast<int>(kMaximumExifCharacters * 2u);
        if (exifBytes < 0 || exifBytes > kMaximumExifBytes) return std::nullopt;
        record.exifText = ColumnText16(query.value, 8);
    }
    if (includeFaces) {
        const void* blob = sqlite3_column_blob(query.value, 9);
        const int bytes = sqlite3_column_bytes(query.value, 9);
        constexpr int kMaximumFaceBytes = static_cast<int>(
            kMaximumFacesPerRecord * sizeof(NormalizedFaceRect));
        if (bytes < 0 || bytes > kMaximumFaceBytes ||
            bytes % static_cast<int>(sizeof(NormalizedFaceRect)) != 0 ||
            (bytes > 0 && !blob)) {
            return std::nullopt;
        }
        if (blob && bytes > 0) {
            const std::size_t count = static_cast<std::size_t>(bytes) / sizeof(NormalizedFaceRect);
            record.faces.resize(count);
            std::memcpy(record.faces.data(), blob, count * sizeof(NormalizedFaceRect));
            const bool invalidFace = std::any_of(record.faces.begin(), record.faces.end(),
                [](const NormalizedFaceRect& face) { return !IsValidNormalizedFaceRect(face); });
            if (invalidFace) return std::nullopt;
        }
    }
    const int storedFacesScanned = sqlite3_column_int(query.value, 10);
    const int storedFaceBlurScanned = sqlite3_column_int(query.value, 11);
    const int storedFaceBlurry = sqlite3_column_int(query.value, 12);
    if ((storedFacesScanned != 0 && storedFacesScanned != 1) ||
        (storedFaceBlurScanned != 0 && storedFaceBlurScanned != 1) ||
        (storedFaceBlurry != 0 && storedFaceBlurry != 1)) {
        return std::nullopt;
    }
    record.facesScanned = storedFacesScanned != 0;
    record.faceBlurScanned = storedFaceBlurScanned != 0;
    record.faceBlurry = storedFaceBlurry != 0;
    const double storedSharpness = sqlite3_column_double(query.value, 13);
    if (!std::isfinite(storedSharpness) || storedSharpness < 0.0 ||
        storedSharpness > static_cast<double>(std::numeric_limits<float>::max())) {
        return std::nullopt;
    }
    record.faceSharpness = static_cast<float>(storedSharpness);
    record.lastAccessStamp = sqlite3_column_int64(query.value, 14);

    constexpr std::int64_t kTouchSeconds = 24ll * 60ll * 60ll;
    const std::int64_t now = CurrentAccessStamp();
    if (record.lastAccessStamp <= now - kTouchSeconds) {
        SqlStatement touch;
        if (Prepare(database_, "UPDATE image_records SET last_access=?1 WHERE path=?2;", touch)) {
            sqlite3_bind_int64(touch.value, 1, now);
            BindText16(touch.value, 2, key);
            sqlite3_step(touch.value);
        }
        record.lastAccessStamp = now;
    }
    return record;
}

void PersistentCache::UpsertRecord(const fs::path& path, CachedImageRecord record) {
    std::vector<std::pair<fs::path, CachedImageRecord>> records;
    records.emplace_back(path, std::move(record));
    UpsertRecords(std::move(records));
}

void PersistentCache::UpsertRecords(
    std::vector<std::pair<fs::path, CachedImageRecord>> records) {
    if (records.empty()) return;
    const std::int64_t accessStamp = CurrentAccessStamp();
    struct PreparedRecord {
        std::wstring key;
        CachedImageRecord record;
    };
    std::vector<PreparedRecord> prepared;
    prepared.reserve(records.size());
    for (auto& [path, record] : records) {
        if (record.fileSize > static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max())) {
            continue;
        }
        if (record.ratingKnowledge == core::MetadataKnowledge::Known)
            record.rating = std::clamp(record.rating, 0, 5);
        if (record.colorLabelKnowledge == core::MetadataKnowledge::Known)
            record.colorLabel = std::clamp(record.colorLabel, 0, 5);
        if (record.pickStateKnowledge == core::MetadataKnowledge::Known)
            record.pickState = std::clamp(record.pickState, -1, 1);
        constexpr std::size_t kMaximumRawMetadataCharacters = 4096;
        record.rawRating.resize(std::min(record.rawRating.size(), kMaximumRawMetadataCharacters));
        record.rawColorLabel.resize(std::min(record.rawColorLabel.size(), kMaximumRawMetadataCharacters));
        record.rawPickState.resize(std::min(record.rawPickState.size(), kMaximumRawMetadataCharacters));
        record.sourceWidth = std::clamp(record.sourceWidth, 0,
            static_cast<int>(kMaximumSourceDimension));
        record.sourceHeight = std::clamp(record.sourceHeight, 0,
            static_cast<int>(kMaximumSourceDimension));
        record.exifText.resize(std::min<std::size_t>(record.exifText.size(), kMaximumExifCharacters));
        if (record.faces.size() > kMaximumFacesPerRecord) record.faces.resize(kMaximumFacesPerRecord);
        const std::size_t faceCountBeforeValidation = record.faces.size();
        std::erase_if(record.faces,
            [](const NormalizedFaceRect& face) { return !IsValidNormalizedFaceRect(face); });
        if (record.faces.size() != faceCountBeforeValidation) {
            record.facesScanned = false;
            record.faceBlurScanned = false;
            record.faceBlurry = false;
            record.faceSharpness = 0.0f;
        }
        if (!std::isfinite(record.faceSharpness) || record.faceSharpness < 0.0f) {
            record.faceBlurScanned = false;
            record.faceBlurry = false;
            record.faceSharpness = 0.0f;
        }
        record.lastAccessStamp = accessStamp;
        prepared.push_back({ NormalizeKey(path), std::move(record) });
    }

    std::lock_guard lock(databaseMutex_);
    if (!database_) return;
    if (sqlite3_exec(database_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK) return;
    bool success = true;
    SqlStatement statement;
    if (!Prepare(database_, kUpsertRecordSql, statement)) {
        success = false;
    } else {
        for (const PreparedRecord& item : prepared) {
            sqlite3_reset(statement.value);
            sqlite3_clear_bindings(statement.value);
            BindRecord(statement.value, item.key, item.record);
            if (sqlite3_step(statement.value) != SQLITE_DONE) {
                success = false;
                break;
            }
        }
    }
    if (success && sqlite3_exec(database_, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK) return;
    sqlite3_exec(database_, "ROLLBACK;", nullptr, nullptr, nullptr);
}

std::wstring PersistentCache::GetPhotoComment(const fs::path& path) {
    const std::wstring key = NormalizeKey(path);
    std::lock_guard lock(databaseMutex_);
    if (!database_) return {};
    SqlStatement stmt;
    if (!Prepare(database_, "SELECT comment FROM photo_comments WHERE path=?1;", stmt)) return {};
    BindText16(stmt.value, 1, key);
    return sqlite3_step(stmt.value) == SQLITE_ROW ? ColumnText16(stmt.value, 0) : std::wstring{};
}

void PersistentCache::SetPhotoComment(const fs::path& path, std::wstring comment) {
    const std::wstring key = NormalizeKey(path);
    std::lock_guard lock(databaseMutex_);
    if (!database_) return;
    SqlStatement stmt;
    if (comment.empty()) {
        if (!Prepare(database_, "DELETE FROM photo_comments WHERE path=?1;", stmt)) return;
        BindText16(stmt.value, 1, key);
    } else {
        if (!Prepare(database_, "INSERT INTO photo_comments(path,comment) VALUES(?1,?2) ON CONFLICT(path) DO UPDATE SET comment=excluded.comment;", stmt)) return;
        BindText16(stmt.value, 1, key); BindText16(stmt.value, 2, comment);
    }
    sqlite3_step(stmt.value);
}

fs::path PersistentCache::BitmapPathFor(const fs::path& path, std::uint64_t fileSize,
    std::int64_t modifiedStamp, std::int64_t sidecarStamp, int targetSize,
    int cacheClass, bool previewVariant) const {
    const std::wstring key = NormalizeKey(path);
    std::uint64_t hash = Fnv1a64(key.data(), key.size() * sizeof(wchar_t));
    HashValue(hash, fileSize); HashValue(hash, modifiedStamp); HashValue(hash, sidecarStamp);
    HashValue(hash, targetSize); HashValue(hash, cacheClass); HashValue(hash, previewVariant);
    return bitmapDirectory_ / (HexHash(hash) + (previewVariant ? L".preview.qpsbgra" : L".qpsbgra"));
}

fs::path PersistentCache::PyramidPathFor(const fs::path& path, std::uint64_t fileSize,
    std::int64_t modifiedStamp, std::int64_t sidecarStamp, int level,
    int tileX, int tileY, int tileSize) const {
    const std::wstring key = NormalizeKey(path);
    std::uint64_t hash = Fnv1a64(key.data(), key.size() * sizeof(wchar_t));
    HashValue(hash, fileSize); HashValue(hash, modifiedStamp); HashValue(hash, sidecarStamp);
    HashValue(hash, level); HashValue(hash, tileX); HashValue(hash, tileY); HashValue(hash, tileSize);
    return pyramidDirectory_ / (HexHash(hash) + L".qpstile");
}

bool PersistentCache::LoadBitmapFile(const fs::path& cachedPath, std::uint64_t fileSize,
    std::int64_t modifiedStamp, std::int64_t sidecarStamp, int targetSize,
    int cacheClass, CachedBitmapData& output) const {
    std::ifstream stream(cachedPath, std::ios::binary);
    BitmapHeader header{};
    if (!stream || !ReadExact(stream, &header, sizeof(header)) ||
        std::memcmp(header.magic, kBitmapMagic.data(), kBitmapMagic.size()) != 0 ||
        header.version != kBitmapVersion || header.fileSize != fileSize ||
        header.modifiedStamp != modifiedStamp || header.sidecarStamp != sidecarStamp ||
        targetSize <= 0 || header.targetSize != static_cast<std::uint32_t>(targetSize) ||
        header.cacheClass != cacheClass || header.previewOnly > 1 ||
        header.width == 0 || header.height == 0 || header.width > 100'000 || header.height > 100'000 ||
        header.sourceWidth > kMaximumSourceDimension ||
        header.sourceHeight > kMaximumSourceDimension) {
        stream.close();
        std::error_code ec; fs::remove(cachedPath, ec);
        return false;
    }
    if (cacheClass == 0) { // CacheClass::Thumbnail
        const std::uint64_t sourceEdge = std::max<std::uint64_t>(header.sourceWidth, header.sourceHeight);
        const std::uint64_t requestedEdge = sourceEdge == 0 ?
            static_cast<std::uint64_t>(targetSize) :
            std::min<std::uint64_t>(static_cast<std::uint64_t>(targetSize), sourceEdge);
        const std::uint64_t cachedEdge = std::max<std::uint64_t>(header.width, header.height);
        if (cachedEdge * 10ull < requestedEdge * 9ull) {
            stream.close();
            std::error_code ec; fs::remove(cachedPath, ec);
            return false;
        }
    }
    const std::uint64_t expected = static_cast<std::uint64_t>(header.width) * header.height * 4ull;
    if (expected != header.pixelBytes || expected > kMaximumBitmapBytes || expected > SIZE_MAX) {
        stream.close();
        std::error_code ec; fs::remove(cachedPath, ec);
        return false;
    }
    try { output.pixels.resize(static_cast<std::size_t>(expected)); }
    catch (...) { return false; }
    if (!ReadExact(stream, output.pixels.data(), output.pixels.size())) {
        output.pixels.clear();
        stream.close();
        std::error_code ec; fs::remove(cachedPath, ec);
        return false;
    }
    char trailingByte = 0;
    stream.read(&trailingByte, 1);
    if (stream.gcount() != 0 || stream.bad() ||
        BitmapContentChecksum(header, output.pixels) != header.contentChecksum) {
        output.pixels.clear();
        stream.close();
        std::error_code ec; fs::remove(cachedPath, ec);
        return false;
    }
    output.width = static_cast<int>(header.width);
    output.height = static_cast<int>(header.height);
    output.sourceWidth = static_cast<int>(header.sourceWidth);
    output.sourceHeight = static_cast<int>(header.sourceHeight);
    output.previewOnly = header.previewOnly != 0;
    RefreshFileAccessTime(cachedPath);
    return true;
}

bool PersistentCache::LoadCachedBitmap(const fs::path& path, std::uint64_t fileSize,
    std::int64_t modifiedStamp, std::int64_t sidecarStamp, int targetSize,
    int cacheClass, CachedBitmapData& output) const {
    output = {};
    const fs::path full = BitmapPathFor(path, fileSize, modifiedStamp, sidecarStamp, targetSize, cacheClass, false);
    if (LoadBitmapFile(full, fileSize, modifiedStamp, sidecarStamp, targetSize, cacheClass, output)) return true;
    const fs::path preview = BitmapPathFor(path, fileSize, modifiedStamp, sidecarStamp, targetSize, cacheClass, true);
    return LoadBitmapFile(preview, fileSize, modifiedStamp, sidecarStamp, targetSize, cacheClass, output);
}

void PersistentCache::StoreBitmapFile(const fs::path& destination, std::uint64_t fileSize,
    std::int64_t modifiedStamp, std::int64_t sidecarStamp, int targetSize, int cacheClass,
    int width, int height, int sourceWidth, int sourceHeight, bool previewOnly,
    std::span<const std::uint8_t> pixels) {
    if (width <= 0 || height <= 0 || width > 100'000 || height > 100'000 ||
        targetSize <= 0 || sourceWidth < 0 || sourceHeight < 0 ||
        static_cast<std::uint32_t>(sourceWidth) > kMaximumSourceDimension ||
        static_cast<std::uint32_t>(sourceHeight) > kMaximumSourceDimension) return;
    const std::uint64_t expected = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ull;
    if (expected != pixels.size() || expected > kMaximumBitmapBytes) return;
    std::error_code ec;
    const fs::space_info space = fs::space(baseDirectory_, ec);
    if (!ec && (space.available <= minimumFreeSpaceBytes_ || expected > space.available - minimumFreeSpaceBytes_)) return;
    fs::create_directories(destination.parent_path(), ec);
    if (ec) return;

    fs::path temporary;
    NativeOutputFile file = kInvalidNativeOutputFile;
    if (!CreateUniqueTemporaryFile(destination, temporary, file)) return;

    BitmapHeader header{};
    std::memcpy(header.magic, kBitmapMagic.data(), kBitmapMagic.size());
    header.version = kBitmapVersion;
    header.width = static_cast<std::uint32_t>(width);
    header.height = static_cast<std::uint32_t>(height);
    header.sourceWidth = static_cast<std::uint32_t>(sourceWidth);
    header.sourceHeight = static_cast<std::uint32_t>(sourceHeight);
    header.targetSize = static_cast<std::uint32_t>(targetSize);
    header.cacheClass = cacheClass;
    header.fileSize = fileSize;
    header.modifiedStamp = modifiedStamp;
    header.sidecarStamp = sidecarStamp;
    header.pixelBytes = expected;
    header.previewOnly = previewOnly ? 1 : 0;
    header.contentChecksum = BitmapContentChecksum(header, pixels);
    const bool wrote = WriteNativeOutputFile(file, &header, sizeof(header)) &&
        WriteNativeOutputFile(file, pixels.data(), pixels.size()) && FlushNativeOutputFile(file);
    CloseNativeOutputFile(file);
    if (!wrote || !ReplaceFileAtomically(temporary, destination)) fs::remove(temporary, ec);
}

void PersistentCache::StoreBitmap(const fs::path& path, std::uint64_t fileSize,
    std::int64_t modifiedStamp, std::int64_t sidecarStamp, int targetSize, int cacheClass,
    int width, int height, int sourceWidth, int sourceHeight, bool previewOnly,
    std::span<const std::uint8_t> pixels) {
    const fs::path destination = BitmapPathFor(path, fileSize, modifiedStamp, sidecarStamp,
        targetSize, cacheClass, previewOnly);
    StoreBitmapFile(destination, fileSize, modifiedStamp, sidecarStamp, targetSize, cacheClass,
        width, height, sourceWidth, sourceHeight, previewOnly, pixels);
    if (!previewOnly) {
        std::error_code ec;
        fs::remove(BitmapPathFor(path, fileSize, modifiedStamp, sidecarStamp,
            targetSize, cacheClass, true), ec);
    }
}

bool PersistentCache::LoadPyramidTile(const fs::path& path, std::uint64_t fileSize,
    std::int64_t modifiedStamp, std::int64_t sidecarStamp, int level,
    int tileX, int tileY, int tileSize, CachedBitmapData& output) const {
    output = {};
    const int cacheClass = 1000 + std::clamp(level, 0, 999);
    const int target = std::max(1, tileSize);
    return LoadBitmapFile(PyramidPathFor(path, fileSize, modifiedStamp, sidecarStamp,
        level, tileX, tileY, tileSize), fileSize, modifiedStamp, sidecarStamp,
        target, cacheClass, output);
}

void PersistentCache::StorePyramidTile(const fs::path& path, std::uint64_t fileSize,
    std::int64_t modifiedStamp, std::int64_t sidecarStamp, int level,
    int tileX, int tileY, int tileSize, int width, int height,
    int sourceWidth, int sourceHeight, std::span<const std::uint8_t> pixels) {
    const int cacheClass = 1000 + std::clamp(level, 0, 999);
    StoreBitmapFile(PyramidPathFor(path, fileSize, modifiedStamp, sidecarStamp,
        level, tileX, tileY, tileSize), fileSize, modifiedStamp, sidecarStamp,
        std::max(1, tileSize), cacheClass, width, height, sourceWidth, sourceHeight, false, pixels);
}

void PersistentCache::RefreshFileAccessTime(const fs::path& cachedPath) const {
    std::error_code ec;
    const auto previous = fs::last_write_time(cachedPath, ec);
    if (ec) return;
    const auto now = fs::file_time_type::clock::now();
    if (previous > now || now - previous >= kAccessTouchInterval)
        fs::last_write_time(cachedPath, now, ec);
}

void PersistentCache::ConfigureStoragePolicy() {
    std::uint64_t totalPhysicalBytes = 8ull * kGibibyte;
#if defined(_WIN32)
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory)) totalPhysicalBytes = memory.ullTotalPhys;
#else
    const long pages = sysconf(_SC_PHYS_PAGES), pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0) totalPhysicalBytes = static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize);
#endif
    if (totalPhysicalBytes <= 10ull * kGibibyte) {
        sqlitePageCacheKiB_ = 16 * 1024;
        sqliteMmapBytes_ = 16ull * kMebibyte;
        sqliteJournalLimitBytes_ = 16ull * kMebibyte;
        sqliteTempStoreMemory_ = false;
        maximumRecordCount_ = 150'000;
    } else if (totalPhysicalBytes <= 18ull * kGibibyte) {
        sqlitePageCacheKiB_ = 32 * 1024;
        sqliteMmapBytes_ = 64ull * kMebibyte;
        sqliteJournalLimitBytes_ = 32ull * kMebibyte;
        sqliteTempStoreMemory_ = true;
        maximumRecordCount_ = 300'000;
    } else {
        sqlitePageCacheKiB_ = 64 * 1024;
        sqliteMmapBytes_ = 128ull * kMebibyte;
        sqliteJournalLimitBytes_ = 64ull * kMebibyte;
        sqliteTempStoreMemory_ = true;
        maximumRecordCount_ = 500'000;
    }
    std::error_code ec;
    const fs::space_info space = fs::space(baseDirectory_, ec);
    if (ec || space.capacity == 0) return;
    maximumDiskCacheBytes_ = std::clamp<std::uint64_t>(space.capacity / 128ull, 1ull * kGibibyte, 8ull * kGibibyte);
    targetDiskCacheBytes_ = maximumDiskCacheBytes_ * 85ull / 100ull;
    minimumFreeSpaceBytes_ = std::clamp<std::uint64_t>(space.capacity / 100ull, 2ull * kGibibyte, 10ull * kGibibyte);
}

void PersistentCache::PruneDatabaseRecords(bool aggressive) {
    struct CandidateRecord {
        std::wstring path;
        std::int64_t lastAccess = 0;
    };
    std::vector<CandidateRecord> pathsToCheck;
    {
        std::lock_guard lock(databaseMutex_);
        if (!database_) return;
        const std::int64_t cutoff = CurrentAccessStamp() - kRecordMaximumAgeSeconds;
        SqlStatement expired;
        if (Prepare(database_, "DELETE FROM image_records WHERE rowid IN (SELECT rowid FROM image_records WHERE last_access<?1 ORDER BY last_access LIMIT ?2);", expired)) {
            sqlite3_bind_int64(expired.value, 1, cutoff);
            sqlite3_bind_int(expired.value, 2, aggressive ? 8192 : 1024);
            sqlite3_step(expired.value);
        }
        SqlStatement count;
        sqlite3_int64 records = 0;
        if (Prepare(database_, "SELECT count(*) FROM image_records;", count) && sqlite3_step(count.value) == SQLITE_ROW)
            records = sqlite3_column_int64(count.value, 0);
        const sqlite3_int64 target = static_cast<sqlite3_int64>(maximumRecordCount_ * 9 / 10);
        if (records > static_cast<sqlite3_int64>(maximumRecordCount_)) {
            SqlStatement trim;
            if (Prepare(database_, "DELETE FROM image_records WHERE rowid IN (SELECT rowid FROM image_records ORDER BY last_access ASC LIMIT ?1);", trim)) {
                sqlite3_bind_int64(trim.value, 1, records - target);
                sqlite3_step(trim.value);
            }
        }

        // Copy only a bounded candidate set while SQLite is locked. Slow disk,
        // network, or removable-media existence checks happen after releasing
        // databaseMutex_, so normal lookups and writer batches remain responsive.
        SqlStatement candidates;
        if (Prepare(database_, "SELECT path,last_access FROM image_records ORDER BY last_access ASC LIMIT ?1;", candidates)) {
            sqlite3_bind_int(candidates.value, 1, aggressive ? 4096 : 512);
            while (sqlite3_step(candidates.value) == SQLITE_ROW) {
                CandidateRecord record;
                record.path = ColumnText16(candidates.value, 0);
                record.lastAccess = sqlite3_column_int64(candidates.value, 1);
                if (!record.path.empty()) pathsToCheck.push_back(std::move(record));
            }
        }
    }

    std::vector<CandidateRecord> missing;
    missing.reserve(pathsToCheck.size());
    for (auto& record : pathsToCheck) {
        const fs::path source(record.path);
        if (!CanPruneMissingSource(source)) continue;
        std::error_code ec;
        if (!fs::exists(source, ec) && !ec) missing.push_back(std::move(record));
    }
    if (missing.empty()) return;

    std::lock_guard lock(databaseMutex_);
    if (!database_ || sqlite3_exec(database_, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK)
        return;
    SqlStatement remove;
    bool success = Prepare(database_,
        "DELETE FROM image_records WHERE path=?1 AND last_access=?2;", remove);
    if (success) {
        for (const auto& record : missing) {
            sqlite3_reset(remove.value);
            sqlite3_clear_bindings(remove.value);
            BindText16(remove.value, 1, record.path);
            sqlite3_bind_int64(remove.value, 2, record.lastAccess);
            if (sqlite3_step(remove.value) != SQLITE_DONE) { success = false; break; }
        }
    }
    if (!success || sqlite3_exec(database_, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK)
        sqlite3_exec(database_, "ROLLBACK;", nullptr, nullptr, nullptr);
}

void PersistentCache::PruneDiskFiles(bool force) {
    struct Candidate {
        fs::path path;
        std::uint64_t bytes = 0;
        fs::file_time_type access{};
    };
    struct OlderCandidateSet {
        // priority_queue keeps the newest retained candidate at the top, so a
        // still-older file can replace it. This keeps memory bounded while the
        // directory may contain millions of cache files.
        bool operator()(const Candidate& left, const Candidate& right) const noexcept {
            return left.access < right.access;
        }
    };

    const std::size_t candidateLimit = force ? 16'384u : 4'096u;
    const std::size_t deleteLimit = force ? 65'536u : kMaintenanceDeleteBatch;
    const unsigned maximumPasses = force ? 8u : 2u;
    std::size_t deletedOverall = 0;

    auto addSaturating = [](std::uint64_t& total, std::uint64_t bytes) {
        total = bytes > UINT64_MAX - total ? UINT64_MAX : total + bytes;
    };

    for (unsigned pass = 0; pass < maximumPasses && deletedOverall < deleteLimit; ++pass) {
        std::priority_queue<Candidate, std::vector<Candidate>, OlderCandidateSet> oldest;
        std::uint64_t total = 0;
        std::size_t deletedThisPass = 0;
        const auto now = fs::file_time_type::clock::now();

        auto retainOldCandidate = [&](Candidate candidate) {
            if (oldest.size() < candidateLimit) {
                oldest.push(std::move(candidate));
            } else if (candidate.access < oldest.top().access) {
                oldest.pop();
                oldest.push(std::move(candidate));
            }
        };

        auto collect = [&](const fs::path& directory) {
            std::error_code ec;
            for (fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec), end;
                !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file(ec)) { ec.clear(); continue; }
                const fs::path path = it->path();
                const auto stamp = it->last_write_time(ec);
                if (ec) { ec.clear(); continue; }
                const auto bytes = it->file_size(ec);
                if (ec) { ec.clear(); continue; }
                const bool temporary = path.extension() == L".tmp";
                const bool expired = stamp <= now && now - stamp >=
                    (temporary ? kTemporaryMaximumAge : kBitmapMaximumAge);

                // Age expiry is independent of the capacity limit and can be
                // handled while scanning, avoiding a second path allocation.
                if (expired && deletedOverall < deleteLimit) {
                    ec.clear();
                    if (fs::remove(path, ec)) {
                        ++deletedOverall;
                        ++deletedThisPass;
                        continue;
                    }
                }

                addSaturating(total, bytes);
                retainOldCandidate({ path, bytes, stamp });
            }
        };

        collect(bitmapDirectory_);
        collect(pyramidDirectory_);

        std::error_code ec;
        for (const fs::path& path : { databasePath_,
                fs::path(databasePath_.wstring() + L"-wal"),
                fs::path(databasePath_.wstring() + L"-shm") }) {
            ec.clear();
            const auto bytes = fs::file_size(path, ec);
            if (!ec) addSaturating(total, bytes);
        }

        ec.clear();
        const fs::space_info space = fs::space(baseDirectory_, ec);
        const bool freePressure = !ec && space.available < minimumFreeSpaceBytes_;
        const std::uint64_t trimTarget = (force || freePressure || total > maximumDiskCacheBytes_) ?
            targetDiskCacheBytes_ : maximumDiskCacheBytes_;
        if (total <= trimTarget) return;

        std::vector<Candidate> candidates;
        candidates.reserve(oldest.size());
        while (!oldest.empty()) {
            candidates.push_back(oldest.top());
            oldest.pop();
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
            return left.access < right.access;
        });

        for (const Candidate& candidate : candidates) {
            if (total <= trimTarget || deletedOverall >= deleteLimit) break;
            ec.clear();
            if (fs::remove(candidate.path, ec)) {
                total = candidate.bytes > total ? 0 : total - candidate.bytes;
                ++deletedOverall;
                ++deletedThisPass;
            }
        }

        if (total <= trimTarget || deletedThisPass == 0) return;
        // If the bounded candidate set was insufficient, rescan. Deleted files
        // are gone, so the next pass naturally selects the next-oldest tranche.
    }
}

void PersistentCache::PerformMaintenance(bool force) {
    std::unique_lock maintenanceLock(maintenanceMutex_, std::try_to_lock);
    if (!maintenanceLock.owns_lock()) return;
    const auto now = std::chrono::steady_clock::now();
    if (!force && lastMaintenance_ != std::chrono::steady_clock::time_point{} &&
        now - lastMaintenance_ < kMaintenanceInterval) return;
    PruneDatabaseRecords(force);
    PruneDiskFiles(force);
    {
        std::lock_guard lock(databaseMutex_);
        if (database_) {
            sqlite3_wal_checkpoint_v2(database_, nullptr,
                force ? SQLITE_CHECKPOINT_RESTART : SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
            if (force) sqlite3_exec(database_, "PRAGMA optimize;", nullptr, nullptr, nullptr);
        }
    }
    lastMaintenance_ = now;
}

void PersistentCache::Flush(bool force) {
    std::lock_guard lock(databaseMutex_);
    if (!database_) return;
    sqlite3_wal_checkpoint_v2(database_, nullptr,
        force ? SQLITE_CHECKPOINT_RESTART : SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
}

void PersistentCache::TrimMemory(bool critical) {
    std::lock_guard lock(databaseMutex_);
    if (!database_) return;
    sqlite3_db_release_memory(database_);
    if (critical) sqlite3_wal_checkpoint_v2(database_, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
}

void PersistentCache::ClearAll() {
    CloseDatabase();
    std::error_code ec;
    fs::remove_all(baseDirectory_, ec);
    ec.clear(); fs::create_directories(bitmapDirectory_, ec);
    ec.clear(); fs::create_directories(pyramidDirectory_, ec);
    OpenDatabase();
}

} // namespace quicksift
