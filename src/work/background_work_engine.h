#pragma once

#include "app/application_support.h"
#include "core/performance_policy.h"
#include "work/background_completion_queue.h"

#include <array>

namespace quicksift::app {

[[nodiscard]] std::wstring JobKey(const WorkJob& job);

struct WorkerTelemetrySnapshot {
    double queueWaitMilliseconds = 0.0;
    double decodeBudgetWaitMilliseconds = 0.0;
    double decodeMilliseconds = 0.0;
    std::array<double, 7> decodeMillisecondsByFormat{};
    std::uint64_t completedJobs = 0;
    std::uint64_t cancelledJobs = 0;
    std::uint64_t oversizedReservations = 0;
    std::size_t interactiveQueued = 0;
    std::size_t visibleQueued = 0;
    std::size_t predictiveQueued = 0;
    std::size_t faceQueued = 0;
    std::size_t idleQueued = 0;
    std::size_t analysisQueued = 0;
    std::size_t activeVisualJobs = 0;
    std::size_t inFlightVisualFamilies = 0;
    std::uint64_t persistentCacheHits = 0;
    std::uint64_t persistentCacheMisses = 0;
};

class Worker {
public:
    Worker(HWND notify, quicksift::work::BackgroundCompletionQueue* completionQueue,
        quicksift::PersistentCache* persistentCache, const SystemProfile& profile);
    ~Worker();
    void Stop();
    void SetNotify(HWND notify);
    void SetPersistentCache(quicksift::PersistentCache* persistentCache);
    void SetStorageProfile(const quicksift::StorageProfile& storage);
    void ApplyPerformancePolicy(const quicksift::core::PerformancePolicy& policy);
    [[nodiscard]] WorkerTelemetrySnapshot Telemetry() const;
    void QueueEncodedReadAhead(const fs::path& path);
    void ClearQueuedIdleWork();
    void ClearPersistentCache();
    void AcknowledgePostedResult(const WorkResult& result);
    void SetActiveGeneration(uint64_t generation);
    void SetThumbnailTarget(int targetSize);
    void SetThumbnailDragActive(bool active);
    void SetThumbnailDragLoadMode(int mode);
    // Size-aware concurrent thumbnail decode cap while scroll motion is active.
    // Used together with SetThumbnailDragLoadMode; 0 means reject new thumbnail work.
    void SetThumbnailScrollConcurrentCap(unsigned maxConcurrent);
    void SetRawJpegPreviewOnly(bool enabled);
    void SetNavigationEpoch(uint64_t epoch);
    void SetViewInteractionEpoch(uint64_t epoch);
    void SetInteractiveVisualFocus(bool focused);
    void MarkInteractiveActivity();
    void ApplyThumbnailViewport(std::uint64_t viewportEpoch,
        const std::vector<std::wstring>& desiredPaths,
        const std::vector<std::wstring>& visiblePaths);
    [[nodiscard]] bool IsThumbnailJobPendingOrActive(const fs::path& path) const;
    void DiscardQueuedTiles();
    void RequestCacheFlush();
    void QueueRecordCacheWrite(const fs::path& path, quicksift::CachedImageRecord record);
    void RequestDecoderSessionTrim();
    size_t AnalysisBacklog();
    bool BeginExclusivePathWrite(const fs::path& path,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(3500));
    bool BeginExclusivePathWrites(const std::vector<fs::path>& paths,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(3500));
    void EndExclusivePathWrite(const fs::path& path) noexcept;
    void EndExclusivePathWrites(const std::vector<fs::path>& paths) noexcept;
    void ReleaseExclusivePathWriteKey(const std::wstring& key) noexcept;
    void TrimForMemoryPressure(bool critical);
    void EnqueuePreview(const fs::path& path, int targetSize, uint64_t generation,
        JobPriority priority = JobPriority::Visible,
        CacheClass cacheClass = CacheClass::Thumbnail, uint64_t epoch = 0,
        uint64_t viewportEpoch = 0, int retryAttempt = 0, bool forceWic = false,
        int timeoutMs = 0, bool ignoreThumbnailViewport = false);
    void EnqueueFull(const fs::path& path, int targetSize, uint64_t generation,
        JobPriority priority = JobPriority::Visible,
        CacheClass cacheClass = CacheClass::Preview, uint64_t epoch = 0,
        uint64_t viewportEpoch = 0, int retryAttempt = 0, bool forceWic = false,
        bool isSingleView = false, uint64_t viewInteractionEpoch = 0);
    void EnqueueTile(const fs::path& path, int x, int y, int width, int height,
        int level, uint64_t generation, uint64_t epoch);
    [[nodiscard]] bool PromoteJob(const std::wstring& key, CacheClass cacheClass);
    void PurgeSpeculative(CacheClass cacheClass);
    void SetFocusKey(const std::wstring& key);
    // Compare View: paths are ordered, focus first. Full decodes for these paths
    // outrank other interactive work. Pass empty to clear. Single View is unchanged
    // while the set is empty.
    void SetVisualPriorityPaths(const std::vector<std::wstring>& paths);
    [[nodiscard]] bool IsQueueEmpty() const;
    [[nodiscard]] int CalculateAdaptiveTimeoutMs(const fs::path& path, int attempt, bool isSingleView) const;
    [[nodiscard]] bool EnqueueMetadata(const fs::path& path, uint64_t generation,
        uint64_t metadataRevision = 0, JobPriority priority = JobPriority::Idle);
    void PromoteQueuedMetadata(uint64_t generation,
        JobPriority priority = JobPriority::Predictive);
    void EnqueueExif(const fs::path& path, uint64_t generation);
    void EnqueueFace(const fs::path& path, uint64_t generation, uint64_t epoch,
        JobPriority priority = JobPriority::Face, int targetSize = kFaceDecodeSize);
    void ClearOlderThan(uint64_t generation);

    // Called only from the owning worker thread's decoder-session helpers.
    void PublishDecoderSessionPaths(size_t threadIndex,
        const std::vector<std::filesystem::path>& paths) noexcept;
    [[nodiscard]] bool IsPathBlockedForDecoder(const fs::path& path) const noexcept;

private:
    [[nodiscard]] bool IsThumbnailPathCurrent(const fs::path& path) const noexcept;
    void AcknowledgePendingKey(const std::wstring& key);
    void ReleaseDesiredVisualTarget(const fs::path& path, JobKind kind, CacheClass cacheClass,
        uint64_t generation, uint64_t requestEpoch, uint64_t viewportEpoch, int targetSize,
        bool rawJpegPreviewOnly);
    void AcknowledgeUnpostedJob(const WorkJob& job, const std::wstring& key);
    static std::wstring VisualFamilyKey(const WorkJob& job);
    bool IsSupersededByLargerDecode(const WorkJob& job) const;
    std::unordered_map<std::wstring, unsigned>::const_iterator
        FindBlockedPathLocked(const wchar_t* path) const;
    std::unordered_map<std::wstring, unsigned>::iterator
        FindBlockedPathLocked(const wchar_t* path);
    bool IsCancelled(const WorkJob& job) const;
    bool JobTimedOut(const WorkJob& job) const noexcept;
    size_t EstimatedDecodeReservation(const WorkJob& job) const;
    bool DecodeWithBundledGuarded(const WorkJob& job, WorkResult& result,
        bool preferEmbeddedPreview = false);
    static bool IsCoalescibleVisualJob(const WorkJob& job);
    static bool SameDecodeFamily(const WorkJob& a, const WorkJob& b);
    bool Enqueue(WorkJob job);
    WorkJob PopWork(bool allowIdle);
    void FinishAnalysisJob(const WorkJob& job);
    static bool FileFingerprint(const fs::path& path, uint64_t& size, int64_t& modified);
    void QueueDerivedCacheLevels(IWICImagingFactory* factory, const WorkJob& sourceJob,
        uint64_t fileSize, int64_t modifiedStamp, int64_t sidecarStamp,
        const std::shared_ptr<WorkResult>& source);
    bool TryPostMetadataAbort(const WorkJob& job);
    bool TryPostResult(const WorkJob& job, const std::shared_ptr<WorkResult>& result);
    void Run(bool allowIdle, size_t threadIndex, bool metadataOnly = false);
    void QueueBitmapCacheWrite(const WorkJob& job, uint64_t fileSize, int64_t modifiedStamp,
        int64_t sidecarStamp, const std::shared_ptr<const WorkResult>& result);
    void RunCacheWriter();
    [[nodiscard]] bool TryBeginHandleJob(const WorkJob& job);
    void EndHandleJob(const WorkJob& job) noexcept;
    [[nodiscard]] bool ExclusivePathsReadyLocked(const std::vector<std::wstring>& keys) const;
    [[nodiscard]] bool IsThumbnailPathCurrentKey(const std::wstring& key) const noexcept;
    void RecordTelemetry(const WorkJob& job, double queueWaitMs, double budgetWaitMs,
        double executionMs, bool cancelled, bool oversized) noexcept;
    void ConfigureReadAheadLocked();
    void RefreshReadAheadGate() noexcept;

    HWND notify_ = nullptr;
    quicksift::work::BackgroundCompletionQueue* completionQueue_ = nullptr;
    quicksift::PersistentCache* persistentCache_ = nullptr;
    std::shared_ptr<DecodeBudget> decodeBudget_;
    std::vector<std::thread> threads_;
    std::thread metadataThread_;
    std::thread cacheWriter_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable cacheCv_;
    std::condition_variable decoderTrimCv_;
    size_t cacheWriteQueueLimit_ = 32ull * kMebibyte;
    size_t recordWriteQueueLimit_ = 1024;
    size_t recordWriteBatchLimit_ = 16;
    size_t maintenanceWriteThreshold_ = 256ull * kMebibyte;
    int idleDelayMilliseconds_ = 650;
    bool serializeHeavyBundledDecodes_ = false;
    bool singleWorkerMode_ = false;
    SystemProfile profile_;
    quicksift::StorageProfile storageProfile_;
    quicksift::core::PerformancePolicy performancePolicy_;
    std::mutex readAheadGateMutex_;
    quicksift::ThreadPoolReadAhead readAhead_;
    std::atomic<unsigned> runtimeReadAheadFiles_{ 0 };
    std::atomic<std::size_t> mappedInputLimit_{ 96ull * kMebibyte };
    std::atomic<std::size_t> policyMappedInputLimit_{ 96ull * kMebibyte };
    std::atomic<std::size_t> runtimeReusableBufferLimit_{ 8ull * kMebibyte };
    // True only while the thumbnail grid has a published visible window. This
    // lets foreground thumbnail work dominate lower-value analysis without
    // affecting Single/Compare, which publishes an empty thumbnail window.
    std::atomic<bool> thumbnailViewportActive_{ false };
    // While the user drags the thumbnail scrollbar, thumbnail decoding is a
    // non-interactive background task and must yield completely to the UI.
    std::atomic<bool> thumbnailDragActive_{ false };
    // 0 = suspended/fast, 1 = minimal, 2 = moderate (capped by thumbnailScrollConcurrentCap_).
    std::atomic<int> thumbnailDragLoadMode_{ 0 };
    std::atomic<unsigned> thumbnailScrollConcurrentCap_{ 0 };
    std::atomic<unsigned> activeThumbnailJobs_{ 0 };
    std::atomic<unsigned> runtimeDerivativeLevels_{ 0 };
    std::atomic<bool> allowPredictive_{ true };
    std::atomic<bool> allowAnalysis_{ true };
    std::atomic<bool> allowDerivatives_{ true };
    std::atomic<bool> allowSpeculativeCacheWrites_{ true };
    mutable std::mutex desiredMutex_;
    std::unordered_map<std::wstring, int> desiredVisualTargets_;
    std::mutex cacheIoMutex_;
    std::mutex heavyBundledDecodeMutex_;
    std::deque<WorkJob> interactiveQueue_, visibleQueue_, predictiveQueue_, faceQueue_, idleQueue_, analysisQueue_, metadataQueue_;
    size_t analysisInFlight_ = 0;
    size_t foregroundBurstSinceIdle_ = 0;
    std::deque<BitmapCacheWrite> cacheWriteQueue_;
    std::unordered_map<std::wstring, RecordCacheWrite> recordWriteQueue_;
    size_t cacheWriteBytes_ = 0;
    bool flushRequested_ = false;
    std::chrono::steady_clock::time_point cacheMaintenanceNotBefore_ =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::unordered_set<std::wstring> pending_;
    std::unordered_map<std::wstring, int> activeVisualFamilies_;
    std::wstring currentFocusKey_;
    std::vector<std::wstring> visualPriorityPaths_;
    std::atomic<std::uint64_t> persistentCacheHits_{ 0 };
    std::atomic<std::uint64_t> persistentCacheMisses_{ 0 };
    std::chrono::steady_clock::time_point idleNotBefore_ = std::chrono::steady_clock::now();
    std::atomic<uint64_t> activeGeneration_{ 0 };
    std::atomic<unsigned> activeVisualJobs_{ 0 };
    std::atomic<unsigned> activeTileJobs_{ 0 };
    std::atomic<uint64_t> navigationEpoch_{ 0 };
    std::atomic<uint64_t> currentViewInteractionEpoch_{ 1 };
    std::atomic<bool> interactiveVisualFocus_{ false };
    std::atomic<uint64_t> thumbnailViewportEpoch_{ 1 };
    mutable std::mutex thumbnailViewportMutex_;
    std::unordered_set<std::wstring> desiredThumbnailPaths_;
    std::unordered_set<std::wstring> visibleThumbnailPaths_;
    std::atomic<int> activeThumbnailTarget_{ 0 };
    std::atomic<bool> rawJpegPreviewOnly_{ false };
    std::atomic<uint64_t> cacheEpoch_{ 1 };
    std::atomic<uint64_t> decoderSessionTrimEpoch_{ 0 };
    std::uint64_t decoderTrimAckEpoch_ = 0;
    size_t decoderTrimAckCount_ = 0;
    mutable std::mutex blockedMutex_;
    std::condition_variable blockedCv_;
    std::unordered_map<std::wstring, unsigned> blockedPaths_;
    std::unordered_map<std::wstring, unsigned> activeHandleJobs_;
    std::vector<std::vector<std::filesystem::path>> decoderSessionPathsByThread_;
    std::atomic<unsigned> exclusivePathWrites_{ 0 };
    mutable std::mutex telemetryMutex_;
    WorkerTelemetrySnapshot telemetry_;
    std::atomic<bool> stoppingAtomic_{ false };
    bool stopping_ = false;
};

} // namespace quicksift::app
