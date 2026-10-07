// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: Disposable cache contract; original media remains authoritative.

#pragma once

#include "core/metadata_state.h"

#include <chrono>
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

    void UpsertRecord(const std::filesystem::path& path, CachedImageRecord record);
    void UpsertRecords(std::vector<std::pair<std::filesystem::path, CachedImageRecord>> records);
    void Flush(bool force = false);
    // Shutdown path: release SQLite/WAL handles without forcing a synchronous checkpoint.
    // The cache is disposable; committed WAL frames remain recoverable on next startup.
    void CloseForShutdown() noexcept;
    void PerformMaintenance(bool force = false);
    void TrimMemory(bool critical = false);
    void ClearAll();
    void AddLibraryFolder(const std::filesystem::path& path, std::int64_t timestamp);
    std::vector<std::pair<std::wstring, std::int64_t>> GetLibraryFolders();
    void RemoveLibraryFolder(const std::filesystem::path& path);
    void WipeFolderCache(const std::filesystem::path& path);
    std::wstring GetPhotoComment(const std::filesystem::path& path);
    void SetPhotoComment(const std::filesystem::path& path, std::wstring comment);

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
    mutable std::mutex databaseMutex_;
    mutable std::mutex maintenanceMutex_;
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
