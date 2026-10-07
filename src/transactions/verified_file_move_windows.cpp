// OWNER: Locked-handle same-volume and verified cross-volume move operations.
#include "verified_file_operations_windows.h"

#include "platform/locked_file_publication_windows.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace quicksift::transactions {
namespace {
struct HandleGuard {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~HandleGuard() { if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value); }
    void Reset() noexcept {
        if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value);
        value = INVALID_HANDLE_VALUE;
    }
};

void SetDetail(std::wstring& detail, std::wstring_view operation,
    const fs::path& path = {}) {
    const DWORD error = GetLastError();
    detail.assign(operation);
    if (!path.empty()) {
        detail += L": ";
        detail += path.wstring();
    }
    if (error != ERROR_SUCCESS) {
        detail += L" (Windows error ";
        detail += std::to_wstring(error);
        detail += L")";
    }
}

bool SameIdentity(const FileIdentity& left, const FileIdentity& right) noexcept {
    return left.size == right.size && left.stamp == right.stamp &&
        left.changeStamp == right.changeStamp &&
        left.contentFingerprint == right.contentFingerprint;
}
} // namespace

bool WindowsVerifiedFileOperations::MoveVerified(
    const fs::path& source, const fs::path& destination,
    FileIdentity& destinationIdentity, std::wstring& detail) {
    return MoveVerifiedImpl(source, destination, nullptr, destinationIdentity, detail);
}

bool WindowsVerifiedFileOperations::MoveIfUnchanged(
    const fs::path& source, const fs::path& destination,
    const FileIdentity& expectedSource, FileIdentity& destinationIdentity,
    std::wstring& detail) {
    return MoveVerifiedImpl(source, destination, &expectedSource,
        destinationIdentity, detail);
}

bool WindowsVerifiedFileOperations::MoveVerifiedImpl(
    const fs::path& source, const fs::path& destination,
    const FileIdentity* expectedSource, FileIdentity& destinationIdentity,
    std::wstring& detail) {
    if (NormalizedPathKey(source) == NormalizedPathKey(destination)) {
        detail = L"Source and destination identify the same path.";
        return false;
    }
    if (PathEntryExists(destination)) {
        detail = L"The destination already exists or cannot be safely inspected.";
        return false;
    }

    std::error_code error;
    fs::create_directories(destination.parent_path(), error);
    if (error) {
        detail = L"The destination folder could not be created.";
        return false;
    }

    HandleGuard sourceHandle{ CreateFileW(source.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES | DELETE,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr) };
    if (sourceHandle.value == INVALID_HANDLE_VALUE) {
        SetDetail(detail, L"Could not lock the source against writers and replacement", source);
        return false;
    }
    FileIdentity sourceIdentity;
    if (!FingerprintOpenFile(sourceHandle.value, sourceIdentity)) {
        detail = L"The source is not a normal, fingerprintable file.";
        return false;
    }
    if (expectedSource && (!SameIdentity(sourceIdentity, *expectedSource))) {
        detail = L"The source changed after QuickSift recorded it; it was left untouched.";
        return false;
    }

    // Allocate both the destination request and a non-allocating diagnostic
    // before the irreversible rename. Once SetFileInformationByHandle succeeds,
    // the exact locked object is the destination even if sampled fingerprinting
    // later encounters a transient read failure.
    quicksift::platform::LockedRenameRequest moveToDestination(destination);
    std::wstring degradedVerificationDetail =
        L"The file was moved through its locked handle, but the post-rename sampled fingerprint could not be refreshed. Undo will remain conservative.";
    if (!moveToDestination.Valid()) {
        detail = L"Memory allocation for the locked-handle rename failed.";
        return false;
    }

    if (moveToDestination.Apply(sourceHandle.value)) {
        if (FingerprintOpenFile(sourceHandle.value, destinationIdentity)) return true;

        // Rename cannot change size, file ID, last-write time, or bytes while the
        // no-write/no-delete-sharing handle is held. Refresh change time when
        // possible and retain the already sampled source-content fingerprint.
        destinationIdentity = sourceIdentity;
        FILE_BASIC_INFO basic{};
        if (GetFileInformationByHandleEx(sourceHandle.value, FileBasicInfo,
            &basic, sizeof(basic))) {
            destinationIdentity.changeStamp = basic.ChangeTime.QuadPart;
        }
        detail.swap(degradedVerificationDetail);
        return true;
    }
    const DWORD renameError = GetLastError();
    if (renameError != ERROR_NOT_SAME_DEVICE) {
        SetLastError(renameError);
        SetDetail(detail, L"The locked-handle rename failed", source);
        return false;
    }

    sourceHandle.Reset();
    FileIdentity copiedSourceIdentity;
    if (!CopyIfUnchanged(source, destination, sourceIdentity,
        destinationIdentity, copiedSourceIdentity, detail)) return false;
    if (!SameIdentity(copiedSourceIdentity, sourceIdentity)) {
        std::wstring ignored;
        RemoveIfUnchanged(destination, destinationIdentity, ignored);
        detail = L"The source identity changed during the cross-volume handoff.";
        return false;
    }

    HandleGuard destinationHandle{ CreateFileW(destination.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr) };
    FileIdentity committedDestination;
    if (destinationHandle.value == INVALID_HANDLE_VALUE ||
        !FingerprintOpenFile(destinationHandle.value, committedDestination) ||
        !SameIdentity(committedDestination, destinationIdentity)) {
        destinationHandle.Reset();
        std::wstring ignored;
        RemoveIfUnchanged(destination, destinationIdentity, ignored);
        detail = L"The published destination changed before source retirement.";
        return false;
    }

    HandleGuard deleteHandle{ CreateFileW(source.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES | DELETE,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr) };
    FileIdentity deleteIdentity;
    FILE_DISPOSITION_INFO disposition{ TRUE };
    if (deleteHandle.value == INVALID_HANDLE_VALUE ||
        !FingerprintOpenFile(deleteHandle.value, deleteIdentity) ||
        !SameIdentity(deleteIdentity, sourceIdentity) ||
        !FilesAreByteIdenticalCancellable(source, destination) ||
        !SetFileInformationByHandle(deleteHandle.value, FileDispositionInfo,
            &disposition, sizeof(disposition))) {
        destinationHandle.Reset();
        std::wstring ignored;
        RemoveIfUnchanged(destination, destinationIdentity, ignored);
        detail = L"The source could not be retired after final destination verification.";
        return false;
    }
    return true;
}

} // namespace quicksift::transactions
