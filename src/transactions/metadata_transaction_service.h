// OWNER: Serialized, cancellable metadata batches and owned completion delivery.
#pragma once

#include "core/history_store.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
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
};

struct MetadataWriteOutcome {
    bool ok = false;
    bool conflict = false;
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
    std::wstring detail;
    std::vector<MetadataChange> changes;
    std::vector<bool> completed;
};

struct MetadataTransactionCallbacks {
    std::function<bool(const std::vector<std::filesystem::path>&, std::wstring&)> beginExclusive;
    std::function<void(const std::vector<std::filesystem::path>&)> endExclusive;
    std::function<MetadataWriteOutcome(const std::filesystem::path&,
        const quicksift::core::MetadataPatch&, quicksift::core::MetadataStorageKind,
        bool jpeg, bool safeJpegWrite)> write;
};

class MetadataTransactionService {
public:
    MetadataTransactionService();
    ~MetadataTransactionService();
    MetadataTransactionService(const MetadataTransactionService&) = delete;
    MetadataTransactionService& operator=(const MetadataTransactionService&) = delete;

    void Configure(MetadataTransactionCallbacks callbacks);
    [[nodiscard]] bool Submit(MetadataTransactionPlan&& plan);
    [[nodiscard]] bool Busy() const noexcept { return busy_.load(std::memory_order_acquire); }
    void CancelCurrent() noexcept { cancellationRequested_.store(true, std::memory_order_release); }
    void SetCompletionSink(std::function<void()> sink);
    [[nodiscard]] std::optional<MetadataTransactionResult> TakeResult();
    void Stop() noexcept;

private:
    void Execute(MetadataTransactionPlan&& plan,
        const MetadataTransactionCallbacks& callbacks,
        MetadataTransactionResult& result);
    void Run() noexcept;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<MetadataTransactionPlan> plan_;
    std::optional<MetadataTransactionResult> result_;
    std::shared_ptr<const MetadataTransactionCallbacks> callbacks_;
    std::shared_ptr<const std::function<void()>> completionSink_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancellationRequested_{false};
    bool stopping_ = false;

    // Must remain last: the worker may observe every member above as soon as it starts.
    std::thread thread_;
};

} // namespace quicksift::transactions
