// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Disposable cache contract; original media remains authoritative.

#pragma once

#include "core/metadata_state.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;

namespace quicksift {

struct NormalizedFaceRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};
static_assert(sizeof(NormalizedFaceRect) == 16, "face BLOB layout is a persisted contract");

// Per-face quality verdict persisted next to the face boxes (parallel array, same
// order as CachedImageRecord::faces). Fixed 16-byte POD so the BLOB layout is a
// stable contract; analysis semantics are versioned by face_quality_version.
struct FaceQualityRecord {
    float sharpness = 0.0f;         // noise-corrected, contrast-normalized detail
    float motionAnisotropy = 0.0f;  // 0 = isotropic detail, 1 = detail only across one axis
    float eyeOpenness = 0.0f;       // heuristic; meaningful only with EyesChecked
    std::uint32_t flags = 0;        // FaceQualityFlag bits
};
static_assert(sizeof(FaceQualityRecord) == 16, "face-quality BLOB layout is a persisted contract");

namespace FaceQualityFlag {
inline constexpr std::uint32_t Analyzed = 1u << 0;     // analysis ran (verdict bits valid)
inline constexpr std::uint32_t TooSmall = 1u << 1;     // crop below analysis minimum; not judged
inline constexpr std::uint32_t Blurry = 1u << 2;       // defocus / general blur
inline constexpr std::uint32_t MotionBlur = 1u << 3;   // directional (motion/shake) blur
inline constexpr std::uint32_t EyesChecked = 1u << 4;  // both eye regions were evaluable
inline constexpr std::uint32_t EyesClosed = 1u << 5;   // both eyes look closed
inline constexpr std::uint32_t LowContrast = 1u << 6;  // too flat/dark to judge; not flagged
inline constexpr std::uint32_t KnownMask = (1u << 7) - 1u;
} // namespace FaceQualityFlag

struct CachedImageRecord {
    std::uint64_t fileSize = 0;
    std::int64_t modifiedStamp = 0;
    std::int64_t sidecarStamp = 0;
    int rating = 0;
    int colorLabel = 0;
    int pickState = 0;
    core::MetadataKnowledge ratingKnowledge = core::MetadataKnowledge::Known;
    core::MetadataKnowledge colorLabelKnowledge = core::MetadataKnowledge::Known;
    core::MetadataKnowledge pickStateKnowledge = core::MetadataKnowledge::Known;
    std::wstring rawRating;
    std::wstring rawColorLabel;
    std::wstring rawPickState;
    int sourceWidth = 0;
    int sourceHeight = 0;
    std::wstring exifText;
    std::vector<NormalizedFaceRect> faces;
    std::int64_t lastAccessStamp = 0;
    bool facesScanned = false;
    bool faceBlurScanned = false;
    bool faceBlurry = false;
    float faceSharpness = 0.0f;
    // Schema v2: per-face quality (parallel to faces) and the analysis version that
    // produced it. A version other than core::kFaceQualityAnalysisVersion is read
    // back as "quality not scanned" so old verdicts are lazily re-analyzed.
    std::vector<FaceQualityRecord> faceQualities;
    std::uint32_t faceQualityVersion = 0;
    // Schema v2: user-selected FaceLock target stored as a normalized box (never an
    // index) so it survives re-detection. faceSelectionKnown=false means "this
    // write does not carry selection knowledge": the stored value is preserved
    // while the fingerprint is unchanged.
    bool faceSelectionKnown = false;
    bool hasFaceSelection = false;
    NormalizedFaceRect faceSelection{};
    // Camera settings for indexed search (schema v3)
    int iso = -1;
    double aperture = 0.0;
    double focalLength = 0.0;
    double shutterSpeed = 0.0;
    std::wstring camera;
    std::wstring lens;
    bool cameraSettingsKnown = false;
};

struct CachedBitmapData {
    int width = 0;
    int height = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    bool previewOnly = false;
    std::vector<std::uint8_t> pixels;
};

class PersistentCache {
public:
    PersistentCache();
    explicit PersistentCache(std::filesystem::path baseDirectory);
    ~PersistentCache();

    // Initializes disk-backed cache storage. Safe to call once after the UI is visible.
    bool InitializeStorage();

    PersistentCache(const PersistentCache&) = delete;
    PersistentCache& operator=(const PersistentCache&) = delete;

    const std::filesystem::path& BaseDirectory() const noexcept { return baseDirectory_; }
    const std::filesystem::path& DatabasePath() const noexcept { return databasePath_; }

    std::optional<CachedImageRecord> LookupRecord(
        const std::filesystem::path& path,
        std::uint64_t fileSize,
        std::int64_t modifiedStamp,
        std::int64_t sidecarStamp,
        bool includeExifText = true,
        bool includeFaces = true);

    // Camera-settings index (schema v3). Stamp-independent so search never re-reads.
    // Lookup returns nullopt when no row or camera_settings_known=0.
    std::optional<CachedImageRecord> LookupCameraSettings(const std::filesystem::path& path);
    // Updates only the camera columns; never touches rating, faces, dimensions, or stamps.
    void UpdateCameraSettings(const std::filesystem::path& path, const CachedImageRecord& settings);

    void UpsertRecord(const std::filesystem::path& path, CachedImageRecord record);
    void UpsertRecords(std::vector<std::pair<std::filesystem::path, CachedImageRecord>> records);
    void Flush(bool force = false);
    // Shutdown path: release SQLite/WAL handles without forcing a synchronous checkpoint.
    // The cache is disposable; committed WAL frames remain recoverable on next startup.
    void CloseForShutdown() noexcept;
    void PerformMaintenance(bool force = false);
    void TrimMemory(bool critical = false);
    void ClearAll();
    // UI-thread entry points (0.9.4.0 polish). They never wait more than
    // kUiDatabaseWait for the database lock (held by the cache writer, search
    // hydration, maintenance, or the asynchronous open/migration):
    //  - writes that cannot get the lock are queued and applied, in order, by the
    //    next holder of the lock (at the latest when the database closes);
    //  - Try* reads return std::nullopt and the caller retries from a timer.
    // Every lock holder drains queued writes first, so reads observe them.
    static constexpr std::chrono::milliseconds kUiDatabaseWait{ 30 };
    void AddLibraryFolder(const std::filesystem::path& path, std::int64_t timestamp);
    std::vector<std::pair<std::wstring, std::int64_t>> GetLibraryFolders();
    std::optional<std::vector<std::pair<std::wstring, std::int64_t>>> TryGetLibraryFolders();
    std::optional<std::wstring> TryGetPhotoComment(const std::filesystem::path& path);
    void RemoveLibraryFolder(const std::filesystem::path& path);
    void WipeFolderCache(const std::filesystem::path& path);
    std::wstring GetPhotoComment(const std::filesystem::path& path);
    void SetPhotoComment(const std::filesystem::path& path, std::wstring comment);
    // Bulk comment lookup for the search-facts worker: result[i] is the comment of
    // paths[i] ("" when none). One prepared statement, database lock taken per chunk
    // of 256 paths so UI-thread cache users are never blocked for long.
    std::vector<std::wstring> GetPhotoComments(const std::vector<std::filesystem::path>& paths);

    bool LoadCachedBitmap(
        const std::filesystem::path& path,
        std::uint64_t fileSize,
        std::int64_t modifiedStamp,
        std::int64_t sidecarStamp,
        int targetSize,
        int cacheClass,
        CachedBitmapData& output) const;

    void StoreBitmap(
        const std::filesystem::path& path,
        std::uint64_t fileSize,
        std::int64_t modifiedStamp,
        std::int64_t sidecarStamp,
        int targetSize,
        int cacheClass,
        int width,
        int height,
        int sourceWidth,
        int sourceHeight,
        bool previewOnly,
        std::span<const std::uint8_t> pixels);

    bool LoadPyramidTile(
        const std::filesystem::path& path,
        std::uint64_t fileSize,
        std::int64_t modifiedStamp,
        std::int64_t sidecarStamp,
        int level,
        int tileX,
        int tileY,
        int tileSize,
        CachedBitmapData& output) const;

    void StorePyramidTile(
        const std::filesystem::path& path,
        std::uint64_t fileSize,
        std::int64_t modifiedStamp,
        std::int64_t sidecarStamp,
        int level,
        int tileX,
        int tileY,
        int tileSize,
        int width,
        int height,
        int sourceWidth,
        int sourceHeight,
        std::span<const std::uint8_t> pixels);

    static std::int64_t FileTimeStamp(const std::filesystem::file_time_type& value);
    static std::int64_t SourceStampFor(const std::filesystem::path& imagePath);
    static std::int64_t SidecarStampFor(const std::filesystem::path& imagePath);

private:
    std::wstring NormalizeKey(const std::filesystem::path& path) const;
    std::filesystem::path BitmapPathFor(
        const std::filesystem::path& path,
        std::uint64_t fileSize,
        std::int64_t modifiedStamp,
        std::int64_t sidecarStamp,
        int targetSize,
        int cacheClass,
        bool previewVariant = false) const;
    std::filesystem::path PyramidPathFor(
        const std::filesystem::path& path,
        std::uint64_t fileSize,
        std::int64_t modifiedStamp,
        std::int64_t sidecarStamp,
        int level,
        int tileX,
        int tileY,
        int tileSize) const;
    void ConfigureStoragePolicy();
    bool OpenDatabase();
    void CloseDatabase();
    bool InitializeSchema();
    // Upgrades an existing cache database in place (schema v1 -> v2 adds per-face
    // quality, its analysis version, and the persisted FaceLock selection).
    bool MigrateSchema(int fromVersion);
    void RefreshFileAccessTime(const std::filesystem::path& cachedPath) const;
    void PruneDatabaseRecords(bool aggressive);
    void PruneDiskFiles(bool force);
    bool LoadBitmapFile(const std::filesystem::path& cachedPath,
        std::uint64_t fileSize, std::int64_t modifiedStamp, std::int64_t sidecarStamp,
        int targetSize, int cacheClass, CachedBitmapData& output) const;
    void StoreBitmapFile(const std::filesystem::path& destination,
        std::uint64_t fileSize, std::int64_t modifiedStamp, std::int64_t sidecarStamp,
        int targetSize, int cacheClass, int width, int height,
        int sourceWidth, int sourceHeight, bool previewOnly,
        std::span<const std::uint8_t> pixels);
    static std::int64_t CurrentAccessStamp();

    std::filesystem::path baseDirectory_;
    std::filesystem::path bitmapDirectory_;
    std::filesystem::path pyramidDirectory_;
    std::filesystem::path databasePath_;
    // Timed so UI-thread callers can bound their wait (see kUiDatabaseWait).
    mutable std::timed_mutex databaseMutex_;
    mutable std::mutex maintenanceMutex_;
    // Small writes deferred by UI callers that could not get databaseMutex_ in time.
    // Lock order: databaseMutex_ -> deferredMutex_ (enqueue takes deferredMutex_ only).
    mutable std::mutex deferredMutex_;
    std::vector<std::function<void()>> deferredWrites_;
    std::atomic<bool> deferredPending_{ false };
    void DrainDeferredWritesLocked() noexcept; // databaseMutex_ held
    void DeferWrite(std::function<void()> write);
    sqlite3* database_ = nullptr;
    std::uint64_t maximumDiskCacheBytes_ = 4ull * 1024ull * 1024ull * 1024ull;
    std::uint64_t targetDiskCacheBytes_ = 3ull * 1024ull * 1024ull * 1024ull;
    std::uint64_t minimumFreeSpaceBytes_ = 2ull * 1024ull * 1024ull * 1024ull;
    std::size_t sqlitePageCacheKiB_ = 32 * 1024;
    std::uint64_t sqliteMmapBytes_ = 64ull * 1024ull * 1024ull;
    std::uint64_t sqliteJournalLimitBytes_ = 32ull * 1024ull * 1024ull;
    bool sqliteTempStoreMemory_ = true;
    std::size_t maximumRecordCount_ = 250'000;
    std::chrono::steady_clock::time_point lastMaintenance_{};
};

} // namespace quicksift
