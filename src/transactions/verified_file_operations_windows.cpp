// OWNER: Locked-handle Windows copy, move, verification, and guarded deletion.
#include "verified_file_operations_windows.h"

#include "platform/storage_space.h"
#include "platform/locked_file_publication_windows.h"
#include "core/stable_file_identity.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <vector>

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

void SetDetail(std::wstring& detail, std::wstring_view operation, const fs::path& path = {}) {
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

constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void HashBytes(std::uint64_t& hash, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= kFnvPrime;
    }
}

bool SampleOpenFile(HANDLE handle, std::uintmax_t size,
    std::uint64_t& fingerprint) noexcept {
    constexpr DWORD kChunk = 64u * 1024u;
    std::array<unsigned char, kChunk> buffer{};
    const std::uint64_t fileSize = static_cast<std::uint64_t>(size);
    std::array<std::uint64_t, 3> offsets{
        0,
        fileSize > kChunk ? (fileSize - kChunk) / 2 : 0,
        fileSize > kChunk ? fileSize - kChunk : 0,
    };
    std::uint64_t hash = kFnvOffset;
    HashBytes(hash, &fileSize, sizeof(fileSize));
    std::uint64_t previous = std::numeric_limits<std::uint64_t>::max();
    for (const std::uint64_t offset : offsets) {
        if (offset == previous) continue;
        previous = offset;
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN)) return false;
        const DWORD requested = static_cast<DWORD>(std::min<std::uint64_t>(
            kChunk, fileSize - offset));
        DWORD read = 0;
        if (requested != 0 && (!ReadFile(handle, buffer.data(), requested, &read, nullptr) ||
            read != requested)) return false;
        HashBytes(hash, &offset, sizeof(offset));
        HashBytes(hash, buffer.data(), read);
    }
    fingerprint = hash;
    return true;
}

bool SameIdentity(const FileIdentity& left, const FileIdentity& right) noexcept {
    return left.size == right.size && left.stamp == right.stamp &&
        left.changeStamp == right.changeStamp &&
        left.contentFingerprint == right.contentFingerprint;
}

} // namespace

std::wstring WindowsVerifiedFileOperations::NormalizedPathKey(const fs::path& path) {
    std::error_code error;
    fs::path absolute = fs::absolute(path, error);
    std::wstring key = (error ? path : absolute).lexically_normal().wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(character)));
    });
    return key;
}

bool WindowsVerifiedFileOperations::FingerprintOpenFile(
    HANDLE handle, FileIdentity& identity) noexcept {
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) return false;
    BY_HANDLE_FILE_INFORMATION information{};
    FILE_BASIC_INFO basic{};
    if (!GetFileInformationByHandle(handle, &information) ||
        !GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return false;
    }
    identity.size = (static_cast<std::uintmax_t>(information.nFileSizeHigh) << 32) |
        static_cast<std::uintmax_t>(information.nFileSizeLow);
    const std::uint64_t fileIndex =
        (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32) |
        static_cast<std::uint64_t>(information.nFileIndexLow);
    const std::uint64_t lastWrite =
        (static_cast<std::uint64_t>(information.ftLastWriteTime.dwHighDateTime) << 32) |
        static_cast<std::uint64_t>(information.ftLastWriteTime.dwLowDateTime);
    identity.stamp = quicksift::core::StableFileIdentityStamp({
        information.dwVolumeSerialNumber, fileIndex, lastWrite });
    identity.changeStamp = basic.ChangeTime.QuadPart;
    return SampleOpenFile(handle, identity.size, identity.contentFingerprint);
}

bool WindowsVerifiedFileOperations::FingerprintPath(
    const fs::path& path, FileIdentity& identity) {
    HandleGuard handle{ CreateFileW(path.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr) };
    return FingerprintOpenFile(handle.value, identity);
}

bool WindowsVerifiedFileOperations::PathEntryExists(
    const fs::path& path, bool* isSymlink) {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory) {
        if (isSymlink) *isSymlink = false;
        return false;
    }
    if (error) {
        if (isSymlink) *isSymlink = false;
        return true;
    }
    if (isSymlink) *isSymlink = fs::is_symlink(status);
    return fs::exists(status);
}

bool WindowsVerifiedFileOperations::CancellationRequested() const noexcept {
    return cancellationRequested_ &&
        cancellationRequested_->load(std::memory_order_acquire);
}

bool WindowsVerifiedFileOperations::FilesAreByteIdenticalCancellable(
    const fs::path& left, const fs::path& right) const {
    std::error_code error;
    const std::uintmax_t leftSize = fs::file_size(left, error);
    if (error) return false;
    const std::uintmax_t rightSize = fs::file_size(right, error);
    if (error || leftSize != rightSize) return false;

    std::ifstream leftStream(left, std::ios::binary);
    std::ifstream rightStream(right, std::ios::binary);
    if (!leftStream || !rightStream) return false;
    constexpr std::size_t bufferSize = 1024 * 1024;
    std::vector<char> leftBuffer(bufferSize);
    std::vector<char> rightBuffer(bufferSize);
    while (leftStream && rightStream) {
        if (CancellationRequested()) return false;
        leftStream.read(leftBuffer.data(), static_cast<std::streamsize>(leftBuffer.size()));
        rightStream.read(rightBuffer.data(), static_cast<std::streamsize>(rightBuffer.size()));
        const std::streamsize leftCount = leftStream.gcount();
        const std::streamsize rightCount = rightStream.gcount();
        if (leftCount != rightCount) return false;
        if (leftCount == 0) break;
        if (std::memcmp(leftBuffer.data(), rightBuffer.data(),
            static_cast<std::size_t>(leftCount)) != 0) return false;
    }
    return leftStream.eof() && rightStream.eof() && !CancellationRequested();
}

bool WindowsVerifiedFileOperations::OpenHandlesAreByteIdenticalCancellable(
    HANDLE left, HANDLE right) const {
    if (left == nullptr || left == INVALID_HANDLE_VALUE ||
        right == nullptr || right == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER leftSize{};
    LARGE_INTEGER rightSize{};
    if (!GetFileSizeEx(left, &leftSize) || !GetFileSizeEx(right, &rightSize) ||
        leftSize.QuadPart < 0 || leftSize.QuadPart != rightSize.QuadPart) return false;

    LARGE_INTEGER begin{};
    if (!SetFilePointerEx(left, begin, nullptr, FILE_BEGIN) ||
        !SetFilePointerEx(right, begin, nullptr, FILE_BEGIN)) return false;
    constexpr DWORD bufferSize = 1024u * 1024u;
    std::vector<unsigned char> leftBuffer(bufferSize);
    std::vector<unsigned char> rightBuffer(bufferSize);
    for (;;) {
        if (CancellationRequested()) return false;
        DWORD leftRead = 0;
        DWORD rightRead = 0;
        if (!ReadFile(left, leftBuffer.data(), bufferSize, &leftRead, nullptr) ||
            !ReadFile(right, rightBuffer.data(), bufferSize, &rightRead, nullptr) ||
            leftRead != rightRead) return false;
        if (leftRead == 0) return true;
        if (std::memcmp(leftBuffer.data(), rightBuffer.data(), leftRead) != 0) return false;
    }
}

bool WindowsVerifiedFileOperations::FilesAreByteIdentical(
    const fs::path& left, const fs::path& right) {
    WindowsVerifiedFileOperations operations;
    return operations.FilesAreByteIdenticalCancellable(left, right);
}

DWORD CALLBACK WindowsVerifiedFileOperations::CopyProgressRoutine(
    LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER,
    DWORD, DWORD, HANDLE, HANDLE, LPVOID data) noexcept {
    const auto* operations = static_cast<const WindowsVerifiedFileOperations*>(data);
    return operations && operations->CancellationRequested() ?
        PROGRESS_CANCEL : PROGRESS_CONTINUE;
}

bool WindowsVerifiedFileOperations::RemoveIfUnchanged(
    const fs::path& path, const FileIdentity& expected, std::wstring& detail) {
    HandleGuard handle{ CreateFileW(path.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES | DELETE,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr) };
    if (handle.value == INVALID_HANDLE_VALUE) {
        SetDetail(detail, L"Could not lock file for guarded deletion", path);
        return false;
    }
    FileIdentity current;
    if (!FingerprintOpenFile(handle.value, current) ||
        !SameIdentity(current, expected)) {
        detail = L"The file changed after QuickSift published it; it was left untouched.";
        return false;
    }
    FILE_DISPOSITION_INFO disposition{ TRUE };
    if (!SetFileInformationByHandle(handle.value, FileDispositionInfo,
        &disposition, sizeof(disposition))) {
        SetDetail(detail, L"Could not delete the unchanged file", path);
        return false;
    }
    return true;
}

bool WindowsVerifiedFileOperations::IsUnchanged(
    const fs::path& path, const FileIdentity& expected) {
    FileIdentity current;
    return FingerprintPath(path, current) && SameIdentity(current, expected);
}

bool WindowsVerifiedFileOperations::CopyVerified(
    const fs::path& source, const fs::path& destination,
    FileIdentity& destinationIdentity, FileIdentity& sourceIdentity,
    std::wstring& detail) {
    return CopyVerifiedImpl(source, destination, nullptr,
        destinationIdentity, sourceIdentity, detail);
}

bool WindowsVerifiedFileOperations::CopyIfUnchanged(
    const fs::path& source, const fs::path& destination,
    const FileIdentity& expectedSource, FileIdentity& destinationIdentity,
    FileIdentity& sourceIdentity, std::wstring& detail) {
    return CopyVerifiedImpl(source, destination, &expectedSource,
        destinationIdentity, sourceIdentity, detail);
}

bool WindowsVerifiedFileOperations::CopyVerifiedImpl(
    const fs::path& source, const fs::path& destination,
    const FileIdentity* expectedSource, FileIdentity& destinationIdentity,
    FileIdentity& sourceIdentity, std::wstring& detail) {
    if (NormalizedPathKey(source) == NormalizedPathKey(destination)) {
        detail = L"Source and destination identify the same path.";
        return false;
    }

    HandleGuard sourceHandle{ CreateFileW(source.c_str(), GENERIC_READ | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr) };
    if (sourceHandle.value == INVALID_HANDLE_VALUE) {
        SetDetail(detail, L"Could not lock source against writers", source);
        return false;
    }
    if (!FingerprintOpenFile(sourceHandle.value, sourceIdentity)) {
        detail = L"The source is not a normal, fingerprintable file.";
        return false;
    }
    if (expectedSource && !SameIdentity(sourceIdentity, *expectedSource)) {
        detail = L"The source changed after QuickSift recorded it; it was left untouched.";
        return false;
    }
    if (PathEntryExists(destination)) {
        detail = L"The destination already exists or cannot be safely inspected.";
        return false;
    }

    std::error_code error;
    fs::create_directories(destination.parent_path(), error);
    if (error || !quicksift::platform::HasStorageHeadroom(destination,
        static_cast<std::uint64_t>(sourceIdentity.size), 16ull * 1024ull * 1024ull)) {
        detail = L"The destination folder is unavailable or lacks verified-copy headroom.";
        return false;
    }

    static std::atomic<std::uint64_t> sequence{0};
    fs::path temporary;
    bool copied = false;
    for (int attempt = 0; attempt < 64; ++attempt) {
        temporary = destination;
        temporary += L".quicksift." + std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed)) + L".tmp";
        if (CopyFileExW(source.c_str(), temporary.c_str(),
            &WindowsVerifiedFileOperations::CopyProgressRoutine, this, nullptr,
            COPY_FILE_FAIL_IF_EXISTS)) {
            copied = true;
            break;
        }
        const DWORD copyError = GetLastError();
        if (copyError != ERROR_FILE_EXISTS && copyError != ERROR_ALREADY_EXISTS) {
            std::error_code cleanup;
            fs::remove(temporary, cleanup);
            if (CancellationRequested() || copyError == ERROR_REQUEST_ABORTED) {
                detail = L"The verified copy was cancelled; its private partial file was removed.";
            } else {
                SetLastError(copyError);
                SetDetail(detail, L"Copying into the private verification file failed", source);
            }
            return false;
        }
    }
    if (!copied) {
        detail = L"Could not reserve a private verification filename.";
        return false;
    }

    struct TemporaryGuard {
        fs::path path;
        ~TemporaryGuard() {
            if (path.empty()) return;
            std::error_code cleanup;
            fs::remove(path, cleanup);
        }
        void Release() noexcept { path.clear(); }
    } temporaryGuard{ temporary };

    // This exact handle stays open across publication. Its share mode denies all
    // writers and renamers while still allowing read-only verification handles.
    HandleGuard temporaryHandle{ CreateFileW(temporary.c_str(),
        GENERIC_READ | GENERIC_WRITE | FILE_READ_ATTRIBUTES | DELETE,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
    if (temporaryHandle.value == INVALID_HANDLE_VALUE ||
        !FlushFileBuffers(temporaryHandle.value) ||
        !FingerprintOpenFile(temporaryHandle.value, destinationIdentity) ||
        !OpenHandlesAreByteIdenticalCancellable(sourceHandle.value, temporaryHandle.value)) {
        SetDetail(detail, L"Flushing or verifying the private copy failed", temporary);
        return false;
    }

    fs::path quarantine;
    std::unique_ptr<quicksift::platform::LockedRenameRequest> quarantineRequest;
    for (int attempt = 0; attempt < 64; ++attempt) {
        quarantine = destination;
        quarantine += L".quicksift.failed." + std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
        if (PathEntryExists(quarantine)) continue;
        auto request = std::make_unique<quicksift::platform::LockedRenameRequest>(quarantine);
        if (!request->Valid()) {
            detail = L"Memory allocation for exact-object rollback failed.";
            return false;
        }
        quarantineRequest = std::move(request);
        break;
    }
    quicksift::platform::LockedRenameRequest publishRequest(destination);
    if (!quarantineRequest || !publishRequest.Valid()) {
        detail = L"Could not prepare handle-bound publication and rollback paths.";
        return false;
    }

    // Allocate diagnostics before the first filesystem mutation after which the
    // exact object may need non-allocating recovery.
    std::wstring deletedMismatch =
        L"The final published destination did not bind to the verified object; the exact published object was deleted.";
    std::wstring quarantinedMismatch =
        L"The final published destination did not bind to the verified object; the exact published object was retained under a private quarantine name.";
    std::wstring cleanupFailed =
        L"The final published destination did not bind to the verified object, and exact-object cleanup failed. The destination folder requires inspection.";

    if (!publishRequest.Apply(temporaryHandle.value)) {
        SetDetail(detail, L"Publishing the verified copy without overwrite failed", destination);
        return false;
    }
    temporaryGuard.Release();

    FileIdentity exactPublishedIdentity;
    HandleGuard destinationHandle{ CreateFileW(destination.c_str(),
        GENERIC_READ | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr) };
    FileIdentity pathIdentity;
    const bool verified =
        FingerprintOpenFile(temporaryHandle.value, exactPublishedIdentity) &&
        destinationHandle.value != INVALID_HANDLE_VALUE &&
        FingerprintOpenFile(destinationHandle.value, pathIdentity) &&
        SameIdentity(pathIdentity, exactPublishedIdentity);
    if (!verified) {
        destinationHandle.Reset();
        const bool deletedExactObject =
            quicksift::platform::MarkLockedFileForDeletion(temporaryHandle.value);
        const bool quarantinedExactObject = !deletedExactObject &&
            quarantineRequest->Apply(temporaryHandle.value);
        if (deletedExactObject) detail.swap(deletedMismatch);
        else if (quarantinedExactObject) detail.swap(quarantinedMismatch);
        else detail.swap(cleanupFailed);
        return false;
    }

    destinationIdentity = exactPublishedIdentity;
    return true;
}


} // namespace quicksift::transactions
