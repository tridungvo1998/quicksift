// OWNER: Windows locked-handle verified copy, move, fingerprint, and guarded deletion primitives.
#pragma once

#include "transactions/file_transaction.h"

#include <atomic>
#include <windows.h>

namespace quicksift::transactions {

class WindowsVerifiedFileOperations final : public VerifiedFileOperations {
public:
    void SetCancellationSource(const std::atomic<bool>* source) noexcept override {
        cancellationRequested_ = source;
    }
    bool CopyVerified(const fs::path& source, const fs::path& destination,
        FileIdentity& destinationIdentity, FileIdentity& sourceIdentity,
        std::wstring& detail) override;
    bool CopyIfUnchanged(const fs::path& source, const fs::path& destination,
        const FileIdentity& expectedSource, FileIdentity& destinationIdentity,
        FileIdentity& sourceIdentity, std::wstring& detail) override;
    bool MoveVerified(const fs::path& source, const fs::path& destination,
        FileIdentity& destinationIdentity, std::wstring& detail) override;
    bool MoveIfUnchanged(const fs::path& source, const fs::path& destination,
        const FileIdentity& expectedSource, FileIdentity& destinationIdentity,
        std::wstring& detail) override;
    bool RemoveIfUnchanged(const fs::path& path, const FileIdentity& expected,
        std::wstring& detail) override;
    bool IsUnchanged(const fs::path& path, const FileIdentity& expected) override;

    [[nodiscard]] static std::wstring NormalizedPathKey(const fs::path& path);
    [[nodiscard]] static bool FingerprintOpenFile(HANDLE handle, FileIdentity& identity) noexcept;
    [[nodiscard]] static bool FingerprintPath(const fs::path& path, FileIdentity& identity);
    [[nodiscard]] static bool PathEntryExists(const fs::path& path, bool* isSymlink = nullptr);
    [[nodiscard]] static bool FilesAreByteIdentical(const fs::path& left, const fs::path& right);

private:
    bool CopyVerifiedImpl(const fs::path& source, const fs::path& destination,
        const FileIdentity* expectedSource, FileIdentity& destinationIdentity,
        FileIdentity& sourceIdentity, std::wstring& detail);
    bool MoveVerifiedImpl(const fs::path& source, const fs::path& destination,
        const FileIdentity* expectedSource, FileIdentity& destinationIdentity,
        std::wstring& detail);
    [[nodiscard]] bool CancellationRequested() const noexcept;
    [[nodiscard]] bool FilesAreByteIdenticalCancellable(
        const fs::path& left, const fs::path& right) const;
    [[nodiscard]] bool OpenHandlesAreByteIdenticalCancellable(
        HANDLE left, HANDLE right) const;
    static DWORD CALLBACK CopyProgressRoutine(LARGE_INTEGER totalFileSize,
        LARGE_INTEGER totalBytesTransferred, LARGE_INTEGER streamSize,
        LARGE_INTEGER streamBytesTransferred, DWORD streamNumber,
        DWORD callbackReason, HANDLE sourceFile, HANDLE destinationFile,
        LPVOID data) noexcept;

    const std::atomic<bool>* cancellationRequested_ = nullptr;
};

} // namespace quicksift::transactions
