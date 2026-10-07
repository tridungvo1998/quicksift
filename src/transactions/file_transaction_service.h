// OWNER: Serialized, cancellable file-transaction worker and owned completion delivery.
#pragma once

#include "transactions/file_transaction.h"

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace quicksift::transactions {

class FileTransactionService {
public:
    explicit FileTransactionService(std::unique_ptr<VerifiedFileOperations> operations);
    ~FileTransactionService();
    FileTransactionService(const FileTransactionService&) = delete;
    FileTransactionService& operator=(const FileTransactionService&) = delete;

    [[nodiscard]] bool Submit(FileTransactionPlan&& plan);
    [[nodiscard]] bool Busy() const noexcept { return busy_.load(std::memory_order_acquire); }
    [[nodiscard]] std::optional<FileTransactionProgress> Progress() const;
    void CancelCurrent() noexcept { cancellationRequested_.store(true, std::memory_order_release); }
    void SetCompletionSink(std::function<void()> sink);
    [[nodiscard]] std::optional<FileTransactionResult> TakeResult();
    void Stop() noexcept;

private:
    void Run() noexcept;

    std::unique_ptr<VerifiedFileOperations> operations_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<FileTransactionPlan> plan_;
    std::optional<FileTransactionResult> result_;
    std::optional<FileTransactionProgress> progress_;
    std::chrono::steady_clock::time_point progressStartedAt_{};
    std::shared_ptr<const std::function<void()>> completionSink_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancellationRequested_{false};
    bool stopping_ = false;

    // Must remain last: the worker may observe every member above as soon as it starts.
    std::thread thread_;
};

} // namespace quicksift::transactions
