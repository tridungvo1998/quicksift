// OWNER: Atomic text-file snapshots, encoding publication, and final-path verification.
#include "app/application_support.h"
#include "platform/locked_file_publication_windows.h"

namespace quicksift::app {

bool CaptureAtomicFileSnapshotFromHandle(HANDLE handle, AtomicFileSnapshot& snapshot) {
    snapshot = {};
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return false;

    BY_HANDLE_FILE_INFORMATION info{};
    FILE_BASIC_INFO basic{};
    const bool infoOk = GetFileInformationByHandle(handle, &info) != FALSE;
    const bool basicOk = GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) != FALSE;
    if (!infoOk || !basicOk ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return false;

    snapshot.exists = true;
    snapshot.size = (static_cast<std::uintmax_t>(info.nFileSizeHigh) << 32) |
        static_cast<std::uintmax_t>(info.nFileSizeLow);
    snapshot.lastWrite = info.ftLastWriteTime;
    snapshot.changeTime = basic.ChangeTime.QuadPart;
    snapshot.volumeSerial = info.dwVolumeSerialNumber;
    snapshot.fileIndexHigh = info.nFileIndexHigh;
    snapshot.fileIndexLow = info.nFileIndexLow;
    return true;
}
bool CaptureAtomicFileSnapshot(const fs::path& path, AtomicFileSnapshot& snapshot) {
    snapshot = {};
    HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    const bool captured = CaptureAtomicFileSnapshotFromHandle(handle, snapshot);
    CloseHandle(handle);
    return captured;
}
std::uint64_t WicFileIdentity(const fs::path& path) {
    AtomicFileSnapshot snapshot{};
    if (!CaptureAtomicFileSnapshot(path, snapshot) || !snapshot.exists) return 0;

    const std::uint64_t fileIndex =
        (static_cast<std::uint64_t>(snapshot.fileIndexHigh) << 32) | snapshot.fileIndexLow;
    const std::uint64_t lastWriteTime =
        (static_cast<std::uint64_t>(snapshot.lastWrite.dwHighDateTime) << 32) |
        snapshot.lastWrite.dwLowDateTime;
    std::uint64_t identity = static_cast<std::uint64_t>(quicksift::core::StableFileIdentityStamp({
        snapshot.volumeSerial, fileIndex, lastWriteTime
    }));
    // Size catches coarse-timestamp filesystems and keeps same-path replacements distinct.
    identity ^= snapshot.size + 0x9E3779B97F4A7C15ull + (identity << 6) + (identity >> 2);
    return identity == 0 ? 1 : identity;
}
bool SameAtomicFileSnapshot(const AtomicFileSnapshot& left, const AtomicFileSnapshot& right) {
    if (left.exists != right.exists) return false;
    if (!left.exists) return true;
    return left.size == right.size &&
        CompareFileTime(&left.lastWrite, &right.lastWrite) == 0 &&
        left.changeTime == right.changeTime && left.volumeSerial == right.volumeSerial &&
        left.fileIndexHigh == right.fileIndexHigh && left.fileIndexLow == right.fileIndexLow;
}
bool WriteAllBytes(HANDLE handle, const std::vector<char>& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(remaining,
            static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (!WriteFile(handle, bytes.data() + offset, request, &written, nullptr) || written != request) {
            return false;
        }
        offset += written;
    }
    return true;
}
bool FileHandleMatchesBytes(HANDLE handle, const std::vector<char>& expected) {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER begin{};
    if (!SetFilePointerEx(handle, begin, nullptr, FILE_BEGIN)) return false;
    constexpr DWORD kChunk = 256u * 1024u;
    std::array<char, kChunk> buffer{};
    std::size_t offset = 0;
    while (offset < expected.size()) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
            buffer.size(), expected.size() - offset));
        DWORD read = 0;
        if (!ReadFile(handle, buffer.data(), request, &read, nullptr) || read != request ||
            !std::equal(buffer.begin(), buffer.begin() + read,
                expected.begin() + static_cast<std::ptrdiff_t>(offset))) return false;
        offset += read;
    }
    char extra = 0;
    DWORD read = 0;
    return ReadFile(handle, &extra, 1, &read, nullptr) != FALSE && read == 0;
}

fs::path UniqueSidecarPrivatePath(const fs::path& path, std::wstring_view role) {
    static std::atomic<std::uint64_t> sequence{ 0 };
    for (int attempt = 0; attempt < 64; ++attempt) {
        fs::path candidate = path;
        candidate += L".quicksift.xmp.";
        candidate += role;
        candidate += L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
        if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES &&
            GetLastError() == ERROR_FILE_NOT_FOUND) return candidate;
    }
    return {};
}

bool WriteTextFile(const fs::path& path, const std::wstring& text, TextFileEncoding encoding,
    const AtomicFileSnapshot& expectedDestination) {
    // Never truncate an existing sidecar/session file in place. The verified
    // private object is published by its own locked handle, so path swapping
    // cannot redirect either publication or rollback to an unrelated file.
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return false;

    const std::vector<char> bytes = EncodeTextFile(text, encoding);
    if (!text.empty() && bytes.empty()) return false;
    constexpr std::uint64_t kSidecarReserveBytes = 8ull * 1024ull * 1024ull;
    if (!HasStorageHeadroom(path, static_cast<std::uint64_t>(bytes.size()),
        kSidecarReserveBytes)) return false;

    static std::atomic<std::uint64_t> sequence{ 0 };
    fs::path temporary;
    ScopedWin32Handle temporaryHandle;
    for (int attempt = 0; attempt < 64; ++attempt) {
        temporary = path;
        temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed)) + L".tmp";
        temporaryHandle.reset(CreateFileW(temporary.c_str(),
            GENERIC_READ | GENERIC_WRITE | FILE_READ_ATTRIBUTES | DELETE,
            FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr));
        if (temporaryHandle.get() != INVALID_HANDLE_VALUE) break;
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) return false;
    }
    if (temporaryHandle.get() == INVALID_HANDLE_VALUE) return false;

    struct LockedTemporaryCleanup {
        HANDLE handle = INVALID_HANDLE_VALUE;
        bool active = true;
        ~LockedTemporaryCleanup() {
            if (active) static_cast<void>(
                quicksift::platform::MarkLockedFileForDeletion(handle));
        }
        void Release() noexcept { active = false; }
    } temporaryCleanup{ temporaryHandle.get() };

    if (!WriteAllBytes(temporaryHandle.get(), bytes) ||
        !FlushFileBuffers(temporaryHandle.get()) ||
        !FileHandleMatchesBytes(temporaryHandle.get(), bytes)) return false;

    ScopedWin32Handle originalGuard;
    AtomicFileSnapshot currentDestination;
    fs::path backup;
    const fs::path failed = UniqueSidecarPrivatePath(path, L"failed");
    if (failed.empty()) return false;

    quicksift::platform::LockedRenameRequest publishTemporary(path);
    quicksift::platform::LockedRenameRequest restoreOriginal(path);
    quicksift::platform::LockedRenameRequest quarantineFailed(failed);
    std::optional<quicksift::platform::LockedRenameRequest> moveOriginalToBackup;
    if (!publishTemporary.Valid() || !quarantineFailed.Valid()) return false;

    if (expectedDestination.exists) {
        originalGuard.reset(CreateFileW(path.c_str(),
            GENERIC_READ | FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (originalGuard.get() == INVALID_HANDLE_VALUE ||
            !CaptureAtomicFileSnapshotFromHandle(originalGuard.get(), currentDestination) ||
            !SameAtomicFileSnapshot(currentDestination, expectedDestination)) return false;

        backup = UniqueSidecarPrivatePath(path, L"backup");
        if (backup.empty()) return false;
        moveOriginalToBackup.emplace(backup);
        if (!moveOriginalToBackup->Valid() || !restoreOriginal.Valid() ||
            !moveOriginalToBackup->Apply(originalGuard.get())) return false;
    } else if (!CaptureAtomicFileSnapshot(path, currentDestination) ||
        currentDestination.exists) {
        return false;
    }

    if (!publishTemporary.Apply(temporaryHandle.get())) {
        if (expectedDestination.exists) {
            static_cast<void>(restoreOriginal.Apply(originalGuard.get()));
        }
        return false;
    }
    temporaryCleanup.Release();

    // The exact published handle denies writers and delete-sharing. A path
    // handle may share broadly only to coexist with it; the exact handle remains
    // the authority that prevents replacement during this final binding check.
    AtomicFileSnapshot exactSnapshot;
    ScopedWin32Handle finalPathGuard(CreateFileW(path.c_str(),
        GENERIC_READ | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    AtomicFileSnapshot pathSnapshot;
    const bool verified =
        CaptureAtomicFileSnapshotFromHandle(temporaryHandle.get(), exactSnapshot) &&
        CaptureAtomicFileSnapshotFromHandle(finalPathGuard.get(), pathSnapshot) &&
        SameAtomicFileSnapshot(exactSnapshot, pathSnapshot) &&
        exactSnapshot.size == bytes.size() &&
        FileHandleMatchesBytes(temporaryHandle.get(), bytes);
    if (!verified) {
        finalPathGuard.reset();
        // Quarantine first so the canonical pathname becomes free before the
        // unchanged original is restored. A delete-pending file keeps its name
        // until the last handle closes, so deletion alone is not enough here.
        bool canonicalPathCleared = quarantineFailed.Apply(temporaryHandle.get());
        if (canonicalPathCleared) {
            static_cast<void>(quicksift::platform::MarkLockedFileForDeletion(
                temporaryHandle.get()));
        } else if (quicksift::platform::MarkLockedFileForDeletion(
            temporaryHandle.get())) {
            temporaryHandle.reset();
            canonicalPathCleared = true;
        }
        if (expectedDestination.exists && canonicalPathCleared) {
            static_cast<void>(restoreOriginal.Apply(originalGuard.get()));
        }
        return false;
    }

    if (expectedDestination.exists) {
        // If retirement fails, the exact unchanged original remains at its
        // unique private backup path; the verified replacement stays canonical.
        static_cast<void>(quicksift::platform::MarkLockedFileForDeletion(
            originalGuard.get()));
    }
    return true;
}

} // namespace quicksift::app
