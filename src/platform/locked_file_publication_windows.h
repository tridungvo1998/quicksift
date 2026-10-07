#pragma once

// OWNER: Allocation-before-mutation handle-bound rename and deletion primitives.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

namespace quicksift::platform {

class LockedRenameRequest {
public:
    explicit LockedRenameRequest(const std::filesystem::path& destination) {
        const std::wstring text = destination.wstring();
        if (text.size() > std::numeric_limits<DWORD>::max() / sizeof(wchar_t)) return;
        const std::size_t nameBytes = text.size() * sizeof(wchar_t);
        if (nameBytes > std::numeric_limits<std::size_t>::max() -
            offsetof(FILE_RENAME_INFO, FileName) - sizeof(wchar_t)) return;
        const std::size_t total = offsetof(FILE_RENAME_INFO, FileName) +
            nameBytes + sizeof(wchar_t);
        if (total > std::numeric_limits<DWORD>::max()) return;
        storage_.resize(total);
        std::memset(storage_.data(), 0, storage_.size());
        auto* info = reinterpret_cast<FILE_RENAME_INFO*>(storage_.data());
        info->ReplaceIfExists = FALSE;
        info->RootDirectory = nullptr;
        info->FileNameLength = static_cast<DWORD>(nameBytes);
        if (nameBytes != 0) std::memcpy(info->FileName, text.data(), nameBytes);
    }

    [[nodiscard]] bool Valid() const noexcept { return !storage_.empty(); }

    [[nodiscard]] bool Apply(HANDLE handle) const noexcept {
        return Valid() && handle != nullptr && handle != INVALID_HANDLE_VALUE &&
            SetFileInformationByHandle(handle, FileRenameInfo,
                const_cast<std::byte*>(storage_.data()),
                static_cast<DWORD>(storage_.size())) != FALSE;
    }

private:
    std::vector<std::byte> storage_;
};

[[nodiscard]] inline bool MarkLockedFileForDeletion(HANDLE handle) noexcept {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return false;
    FILE_DISPOSITION_INFO disposition{ TRUE };
    return SetFileInformationByHandle(handle, FileDispositionInfo,
        &disposition, sizeof(disposition)) != FALSE;
}

} // namespace quicksift::platform
