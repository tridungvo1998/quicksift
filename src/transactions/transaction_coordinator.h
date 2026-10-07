// OWNER: Cross-domain transaction serialization, token ownership, completion routing, and shutdown.
#pragma once

#include "transactions/file_transaction_service.h"
#include "transactions/metadata_transaction_service.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace quicksift::transactions {

class TransactionCoordinator {
public:
    explicit TransactionCoordinator(std::unique_ptr<VerifiedFileOperations> fileOperations);
    ~TransactionCoordinator();
    TransactionCoordinator(const TransactionCoordinator&) = delete;
    TransactionCoordinator& operator=(const TransactionCoordinator&) = delete;

    void ConfigureMetadata(MetadataTransactionCallbacks callbacks);
    void SetCompletionSinks(std::function<void()> fileSink,
        std::function<void()> metadataSink);

    [[nodiscard]] bool Busy() const noexcept;
    [[nodiscard]] bool FileBusy() const noexcept;
    [[nodiscard]] std::optional<FileTransactionProgress> FileProgress() const;
    [[nodiscard]] std::uint64_t SubmitFile(FileTransactionPlan& plan);
    [[nodiscard]] std::uint64_t SubmitMetadata(MetadataTransactionPlan& plan);
    [[nodiscard]] std::optional<FileTransactionResult> TakeFileResult();
    [[nodiscard]] std::optional<MetadataTransactionResult> TakeMetadataResult();

    // Returns false for stale/duplicate completion tokens. A successful accept
    // clears the active token before UI integration can trigger another command.
    [[nodiscard]] bool AcceptFileResult(std::uint64_t token) noexcept;
    [[nodiscard]] bool AcceptMetadataResult(std::uint64_t token) noexcept;

    void RequestCloseAtSafeBoundary() noexcept;
    void CancelCloseRequest() noexcept;
    [[nodiscard]] bool ConsumeCloseRequestIfIdle() noexcept;
    void CancelAll() noexcept;
    void Stop() noexcept;

private:
    FileTransactionService fileService_;
    MetadataTransactionService metadataService_;
    std::uint64_t nextFileToken_ = 1;
    std::uint64_t nextMetadataToken_ = 1;
    std::uint64_t activeFileToken_ = 0;
    std::uint64_t activeMetadataToken_ = 0;
    bool closeRequested_ = false;
    bool stopped_ = false;
};

} // namespace quicksift::transactions
