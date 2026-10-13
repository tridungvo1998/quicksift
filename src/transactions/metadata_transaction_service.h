// OWNER: Serialized, cancellable metadata batches and owned completion delivery.
#pragma once

#include "core/history_store.h"
#include "core/write_retry_policy.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace quicksift::transactions {

enum class MetadataTransactionMode {
    Apply,
    UndoHistory,
    RedoHistory,
};

struct MetadataChange {
    quicksift::core::MetadataSnapshot before;
    quicksift::core::MetadataSnapshot after;
    // Apply transactions carry a field-specific patch. History replay derives
    // its patch from the before/after snapshots so only fields originally
    // changed by QuickSift are touched.
    quicksift::core::MetadataPatch patch;
    quicksift::core::PhotoMetadataValues resultingValues;
    bool hasResultingValues = false;
    // Set when a write refused because on-disk identity or CAS values diverged
    // from what QuickSift expected (another app changed the file/sidecar).
    bool externalConflict = false;
    // Why an incomplete item failed (None when completed or never attempted).
    quicksift::core::write_lane::WriteFailureKind failureKind =
        quicksift::core::write_lane::WriteFailureKind::None;
    std::uint32_t win32Error = 0;
    unsigned attempts = 0;
    // File stamps captured on the transaction thread after a successful write, so the
    // UI thread never stats files while integrating a completion.
    struct FileStamps {
        bool valid = false;
        std::uint64_t fileSize = 0;
        std::int64_t modifiedStamp = 0;
        std::int64_t sidecarStamp = 0;
    } stamps;
};

// Cumulative free-space shortfall found by the worker-side preflight.
struct MetadataStorageShortfall {
    bool sizeUnknown = false;   // an image size could not be read; nothing changed
    std::wstring volume;
    std::uint64_t requiredBytes = 0;
    std::optional<std::uint64_t> availableBytes;
};

struct MetadataWriteOutcome {
    bool ok = false;
    bool conflict = false;
    // Classified failure (InUse / AccessDenied / DeviceBusy are retried by the
    // write lane; see core/write_retry_policy.h). Conflict implies kind Conflict.
    quicksift::core::write_lane::WriteFailureKind failureKind =
        quicksift::core::write_lane::WriteFailureKind::None;
    std::uint32_t win32Error = 0;
    quicksift::core::PhotoMetadataValues before;
    quicksift::core::PhotoMetadataValues after;
    std::wstring detail;
};

struct MetadataTransactionPlan {
    std::uint64_t token = 0;
    std::wstring label;
    MetadataTransactionMode mode = MetadataTransactionMode::Apply;
    bool recordHistory = true;
    std::size_t initialFailures = 0;
    std::wstring initialDetail;
    std::vector<MetadataChange> changes;
};

struct MetadataTransactionResult {
    std::uint64_t token = 0;
    std::wstring label;
    MetadataTransactionMode mode = MetadataTransactionMode::Apply;
    bool recordHistory = true;
    bool failed = false;
    bool cancelled = false;
    std::size_t initialFailureCount = 0;
    std::size_t failureCount = 0;
    std::size_t conflictCount = 0;
    // Items that stayed locked by another process through the whole write-lane budget
    // (or were skipped by the user while waiting). The UI offers Retry / Skip.
    std::size_t inUseCount = 0;
    bool urgent = false; // ran on the interactive-edit lane
    std::optional<MetadataStorageShortfall> storageShortfall;
    std::wstring detail;
    std::vector<MetadataChange> changes;
    std::vector<bool> completed;
};

// Items processed so far in the running batch (lock-free snapshot for status UI).
struct MetadataTransactionProgress {
    std::size_t completedItems = 0;
    std::size_t totalItems = 0;
    // Write-lane lock resolution: the item currently waiting for another handle.
    std::wstring waitingPath;
    bool waitingExternal = false; // past the quick budget: another process holds it
    std::uint64_t waitingSerial = 0; // changes once per waiting episode
    std::chrono::milliseconds waitedFor{ 0 };
};

struct MetadataTransactionCallbacks {
    std::function<bool(const std::vector<std::filesystem::path>&, std::wstring&)> beginExclusive;
    std::function<void(const std::vector<std::filesystem::path>&)> endExclusive;
    std::function<MetadataWriteOutcome(const std::filesystem::path&,
        const quicksift::core::MetadataPatch&, quicksift::core::MetadataStorageKind,
        bool jpeg, bool safeJpegWrite)> write;
    // Optional: stamps for a written path (runs on the transaction thread).
    std::function<MetadataChange::FileStamps(const std::filesystem::path&)> stamps;
    // Optional: cumulative free-space preflight for the whole plan (transaction thread).
    std::function<std::optional<MetadataStorageShortfall>(
        const std::vector<quicksift::core::MetadataSnapshot>&)> preflight;
    // Optional: diagnostic hook for write-lane events (attempt, waiting, abort).
    std::function<void(const std::filesystem::path&, unsigned attempt,
        quicksift::core::write_lane::WriteFailureKind, std::uint32_t win32Error,
        std::chrono::milliseconds elapsed, bool external)> onRetry;
    // Test hook: retry delays and budgets are divided by this (1 in production).
    unsigned retryTimeDivisor = 1;
};

class MetadataTransactionService {
public:
    MetadataTransactionService();
    ~MetadataTransactionService();
    MetadataTransactionService(const MetadataTransactionService&) = delete;
    MetadataTransactionService& operator=(const MetadataTransactionService&) = delete;

    void Configure(MetadataTransactionCallbacks callbacks);
    [[nodiscard]] bool Submit(MetadataTransactionPlan&& plan);
    // Interactive-edit lane (P1): accepted while another plan runs. It runs at the
    // next file boundary (or during a lock-resolution wait) of the running plan when
    // its paths are disjoint from that plan; otherwise right after it. One slot.
    [[nodiscard]] bool SubmitUrgent(MetadataTransactionPlan&& plan);
    [[nodiscard]] bool UrgentSlotFree() const;
    [[nodiscard]] bool Busy() const noexcept { return busy_.load(std::memory_order_acquire); }
    void CancelCurrent() noexcept;
    [[nodiscard]] bool CancellationRequested() const noexcept {
        return cancellationRequested_.load(std::memory_order_acquire);
    }
    // Stops waiting for the file currently locked by another app (user chose Skip).
    void SkipWaiting() noexcept;
    void SetCompletionSink(std::function<void()> sink);
    // nullopt when idle.
    [[nodiscard]] std::optional<MetadataTransactionProgress> Progress() const noexcept;
    [[nodiscard]] std::optional<MetadataTransactionResult> TakeResult();
    void Stop() noexcept;

private:
    void Execute(MetadataTransactionPlan&& plan,
        const MetadataTransactionCallbacks& callbacks,
        MetadataTransactionResult& result, bool allowNestedUrgent);
    // Runs the queued urgent plan now if its paths are disjoint from `gatedPaths`.
    void ServiceUrgentNested(const MetadataTransactionCallbacks& callbacks,
        const std::vector<std::filesystem::path>& gatedPaths);
    // Write one item with lock-resolution retries. Returns the final outcome.
    MetadataWriteOutcome WriteWithRetry(const MetadataTransactionCallbacks& callbacks,
        const quicksift::core::MetadataSnapshot& storage,
        const quicksift::core::MetadataPatch& patch, unsigned& attempts,
        const std::vector<std::filesystem::path>* gatedPaths, bool allowNestedUrgent);
    void PublishResult(MetadataTransactionResult&& result, bool endsRun);
    void RecomputeBusyLocked() noexcept;
    void Run() noexcept;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<MetadataTransactionPlan> plan_;
    std::optional<MetadataTransactionPlan> urgentPlan_;
    std::deque<MetadataTransactionResult> results_;
    bool running_ = false;
    std::wstring waitingPath_;          // guarded by mutex_
    std::chrono::steady_clock::time_point waitingSince_{}; // guarded by mutex_
    std::uint64_t waitingSerial_ = 0;   // guarded by mutex_
    std::atomic<bool> waitingExternal_{false};
    std::atomic<bool> skipWaitRequested_{false};
    std::shared_ptr<const MetadataTransactionCallbacks> callbacks_;
    std::shared_ptr<const std::function<void()>> completionSink_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancellationRequested_{false};
    std::atomic<std::size_t> progressCompleted_{0};
    std::atomic<std::size_t> progressTotal_{0};
    bool stopping_ = false;

    // Must remain last: the worker may observe every member above as soon as it starts.
    std::thread thread_;
};

} // namespace quicksift::transactions
