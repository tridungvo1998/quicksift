// OWNER: Platform-neutral file-transaction planning, execution, rollback, and history replay.
#pragma once

#include "core/history_store.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace quicksift::transactions {

namespace fs = std::filesystem;

struct FileIdentity {
    std::uintmax_t size = 0;
    std::int64_t stamp = 0;
    std::int64_t changeStamp = 0;
    std::uint64_t contentFingerprint = 0;
};

enum class FileTransactionMode {
    Transfer,
    UndoHistory,
    RedoHistory,
};

struct TransferItem {
    fs::path source;
    fs::path destination;
};

// One atomic user-visible media unit. Several RAW files may legitimately own
// one basename XMP; all selected owners and the shared companion therefore
// commit or roll back together.
struct TransferGroup {
    std::vector<TransferItem> owners;
    std::optional<TransferItem> companion;

    TransferGroup() = default;
    TransferGroup(fs::path source, fs::path destination,
        std::optional<fs::path> sourceSidecar = std::nullopt,
        std::optional<fs::path> destinationSidecar = std::nullopt) {
        owners.push_back({ std::move(source), std::move(destination) });
        if (sourceSidecar && destinationSidecar) {
            companion = TransferItem{ std::move(*sourceSidecar), std::move(*destinationSidecar) };
        }
    }
};

struct FileTransactionPlan {
    std::uint64_t token = 0;
    std::wstring label;
    FileTransactionMode mode = FileTransactionMode::Transfer;
    bool copy = true;
    bool deleteToFolder = false;
    bool recordHistory = true;
    bool missingRawCompanion = false;
    std::vector<TransferGroup> groups;
    std::vector<quicksift::core::FileHistoryItem> historyReplayItems;
};


struct FileTransactionProgress {
    std::uint64_t token = 0;
    bool copy = true;
    bool deleteToFolder = false;
    std::size_t completedFiles = 0;
    std::size_t totalFiles = 0;
    double elapsedSeconds = 0.0;
};

using FileTransactionProgressSink =
    std::function<void(std::size_t completedFiles, std::size_t totalFiles)>;

struct FileTransactionResult {
    std::uint64_t token = 0;
    std::wstring label;
    FileTransactionMode mode = FileTransactionMode::Transfer;
    bool copy = true;
    bool deleteToFolder = false;
    bool recordHistory = true;
    bool missingRawCompanion = false;
    bool failed = false;
    bool cancelled = false;
    std::size_t lastingTransferItems = 0;
    std::wstring detail;
    std::vector<quicksift::core::FileHistoryItem> historyItems;
    std::vector<bool> replayCompleted;
};

class VerifiedFileOperations {
public:
    virtual ~VerifiedFileOperations() = default;

    // The service supplies a cancellation source whose lifetime exceeds every
    // operation call. Portable fakes may ignore it; native backends should
    // observe it during long-running copy/verification loops.
    virtual void SetCancellationSource(const std::atomic<bool>* source) noexcept {
        (void)source;
    }

    virtual bool CopyVerified(const fs::path& source, const fs::path& destination,
        FileIdentity& destinationIdentity, FileIdentity& sourceIdentity,
        std::wstring& detail) = 0;
    virtual bool CopyIfUnchanged(const fs::path& source, const fs::path& destination,
        const FileIdentity& expectedSource, FileIdentity& destinationIdentity,
        FileIdentity& sourceIdentity, std::wstring& detail) = 0;
    virtual bool MoveVerified(const fs::path& source, const fs::path& destination,
        FileIdentity& destinationIdentity, std::wstring& detail) = 0;
    virtual bool MoveIfUnchanged(const fs::path& source, const fs::path& destination,
        const FileIdentity& expectedSource, FileIdentity& destinationIdentity,
        std::wstring& detail) = 0;
    virtual bool RemoveIfUnchanged(const fs::path& path, const FileIdentity& expected,
        std::wstring& detail) = 0;
    virtual bool IsUnchanged(const fs::path& path, const FileIdentity& expected) = 0;
};

[[nodiscard]] FileTransactionResult ExecuteFileTransaction(
    const FileTransactionPlan& plan,
    VerifiedFileOperations& operations,
    const std::atomic<bool>& cancellationRequested);

[[nodiscard]] FileTransactionResult ExecuteFileTransaction(
    const FileTransactionPlan& plan,
    VerifiedFileOperations& operations,
    const std::atomic<bool>& cancellationRequested,
    const FileTransactionProgressSink& progressSink);

} // namespace quicksift::transactions
