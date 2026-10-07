// OWNER: Cross-domain transaction serialization, token ownership, completion routing, and shutdown.
#include "transaction_coordinator.h"

#include <utility>

namespace quicksift::transactions {

TransactionCoordinator::TransactionCoordinator(
    std::unique_ptr<VerifiedFileOperations> fileOperations)
    : fileService_(std::move(fileOperations)) {}

TransactionCoordinator::~TransactionCoordinator() {
    Stop();
}

void TransactionCoordinator::ConfigureMetadata(MetadataTransactionCallbacks callbacks) {
    if (stopped_) return;
    metadataService_.Configure(std::move(callbacks));
}

void TransactionCoordinator::SetCompletionSinks(std::function<void()> fileSink,
    std::function<void()> metadataSink) {
    if (stopped_) return;
    fileService_.SetCompletionSink(std::move(fileSink));
    metadataService_.SetCompletionSink(std::move(metadataSink));
}

bool TransactionCoordinator::Busy() const noexcept {
    return fileService_.Busy() || metadataService_.Busy();
}

bool TransactionCoordinator::FileBusy() const noexcept {
    return fileService_.Busy();
}

std::optional<FileTransactionProgress> TransactionCoordinator::FileProgress() const {
    return fileService_.Progress();
}

std::uint64_t TransactionCoordinator::SubmitFile(FileTransactionPlan& plan) {
    if (stopped_ || closeRequested_ || Busy()) return 0;
    const std::uint64_t token = nextFileToken_;
    plan.token = token;
    if (!fileService_.Submit(std::move(plan))) {
        plan.token = 0;
        return 0;
    }
    if (++nextFileToken_ == 0) ++nextFileToken_;
    activeFileToken_ = token;
    return token;
}

std::uint64_t TransactionCoordinator::SubmitMetadata(MetadataTransactionPlan& plan) {
    if (stopped_ || closeRequested_ || Busy()) return 0;
    const std::uint64_t token = nextMetadataToken_;
    plan.token = token;
    if (!metadataService_.Submit(std::move(plan))) {
        plan.token = 0;
        return 0;
    }
    if (++nextMetadataToken_ == 0) ++nextMetadataToken_;
    activeMetadataToken_ = token;
    return token;
}

std::optional<FileTransactionResult> TransactionCoordinator::TakeFileResult() {
    return fileService_.TakeResult();
}

std::optional<MetadataTransactionResult> TransactionCoordinator::TakeMetadataResult() {
    return metadataService_.TakeResult();
}

bool TransactionCoordinator::AcceptFileResult(std::uint64_t token) noexcept {
    if (token == 0 || token != activeFileToken_) return false;
    activeFileToken_ = 0;
    return true;
}

bool TransactionCoordinator::AcceptMetadataResult(std::uint64_t token) noexcept {
    if (token == 0 || token != activeMetadataToken_) return false;
    activeMetadataToken_ = 0;
    return true;
}

void TransactionCoordinator::RequestCloseAtSafeBoundary() noexcept {
    if (stopped_) return;
    closeRequested_ = true;
    CancelAll();
}

void TransactionCoordinator::CancelCloseRequest() noexcept {
    if (!stopped_) closeRequested_ = false;
}

bool TransactionCoordinator::ConsumeCloseRequestIfIdle() noexcept {
    if (!closeRequested_ || Busy()) return false;
    closeRequested_ = false;
    return true;
}

void TransactionCoordinator::CancelAll() noexcept {
    fileService_.CancelCurrent();
    metadataService_.CancelCurrent();
}

void TransactionCoordinator::Stop() noexcept {
    if (stopped_) return;
    stopped_ = true;
    closeRequested_ = false;
    fileService_.SetCompletionSink({});
    metadataService_.SetCompletionSink({});
    CancelAll();
    metadataService_.Stop();
    fileService_.Stop();
    activeFileToken_ = 0;
    activeMetadataToken_ = 0;
}

} // namespace quicksift::transactions
