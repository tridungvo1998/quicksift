// OWNER: Bounded asynchronous folder enumeration and scan-batch delivery.
#pragma once

#include "app/application_support.h"
#include "work/background_completion_queue.h"

#include <optional>

namespace quicksift::app {

class FolderScanner {
public:
    FolderScanner(HWND notify,
        quicksift::work::BackgroundCompletionQueue* completionQueue,
        quicksift::PersistentCache* cache,
        const quicksift::CpuTopology& topology = {});
    ~FolderScanner();
    FolderScanner(const FolderScanner&) = delete;
    FolderScanner& operator=(const FolderScanner&) = delete;

    void SetNotify(HWND notify);
    void AcknowledgeBatch(uint64_t generation);
    void Cancel() noexcept;
    void Start(const fs::path& folder, uint64_t generation);

private:
    struct ScanRequest {
        fs::path folder;
        std::uint64_t generation = 0;
        HWND notify = nullptr;
    };

    static constexpr size_t kInitialBatchSize = 64;
    static constexpr size_t kSteadyBatchSize = 256;
    static constexpr size_t kMaximumOutstandingBatches = 8;

    void ThreadMain();
    void RunScan(const ScanRequest& request);
    [[nodiscard]] bool Cancelled(std::uint64_t generation) const noexcept;

    HWND notify_ = nullptr;
    quicksift::work::BackgroundCompletionQueue* completionQueue_ = nullptr;
    quicksift::PersistentCache* cache_ = nullptr; // retained for composition/API compatibility
    quicksift::CpuTopology topology_;
    std::thread thread_;
    std::atomic<bool> shutdown_{ false };
    std::atomic<std::uint64_t> requestedGeneration_{ 0 };
    std::mutex requestMutex_;
    std::condition_variable requestCv_;
    std::optional<ScanRequest> pendingRequest_;
    std::mutex batchMutex_;
    std::condition_variable batchCv_;
    size_t outstandingBatches_ = 0;
    uint64_t activeBatchGeneration_ = 0;
};

} // namespace quicksift::app
