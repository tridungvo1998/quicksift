// OWNER: Canonical QuickSift runtime-data paths for portable and MSIX execution.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <appmodel.h>
#endif

namespace quicksift::platform {

inline std::filesystem::path ExecutableDirectory() noexcept {
    namespace fs = std::filesystem;
    try {
#if defined(_WIN32)
        std::wstring buffer(32768, L'\0');
        const DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (count != 0 && static_cast<std::size_t>(count) < buffer.size()) {
            buffer.resize(count);
            return fs::path(buffer).parent_path();
        }
#endif
        return fs::current_path();
    } catch (...) {
        return {};
    }
}

inline bool RunningWithPackageIdentity() noexcept {
#if defined(_WIN32)
    UINT32 length = 0;
    const LONG result = GetCurrentPackageFullName(&length, nullptr);
    return result == ERROR_INSUFFICIENT_BUFFER || result == ERROR_SUCCESS;
#else
    return false;
#endif
}

inline std::wstring CurrentPackageFamilyName() noexcept {
#if defined(_WIN32)
    UINT32 length = 0;
    LONG result = GetCurrentPackageFamilyName(&length, nullptr);
    if (result != ERROR_INSUFFICIENT_BUFFER || length == 0) return {};
    std::wstring family(static_cast<std::size_t>(length), L'\0');
    result = GetCurrentPackageFamilyName(&length, family.data());
    if (result != ERROR_SUCCESS) return {};
    if (!family.empty() && family.back() == L'\0') family.pop_back();
    return family;
#else
    return {};
#endif
}

inline std::filesystem::path LocalAppDataDirectory() noexcept {
#if defined(_WIN32)
    try {
        std::wstring buffer(32768, L'\0');
        const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (count == 0 || static_cast<std::size_t>(count) >= buffer.size()) return {};
        buffer.resize(count);
        return std::filesystem::path(buffer);
    } catch (...) {
        return {};
    }
#else
    return {};
#endif
}

inline std::filesystem::path ResolveDataRootForContext(bool packaged,
    const std::filesystem::path& executableDirectory,
    const std::filesystem::path& localAppDataDirectory,
    std::wstring_view packageFamilyName) noexcept {
    try {
        if (packaged) {
            // Never fall back to the executable directory for a packaged process.
            // MSIX package contents are immutable; all QuickSift-owned runtime state
            // belongs in the package's per-user LocalState directory.
            if (localAppDataDirectory.empty() || packageFamilyName.empty()) return {};
            return localAppDataDirectory / L"Packages" /
                std::filesystem::path(packageFamilyName) / L"LocalState";
        }
        if (executableDirectory.empty()) return {};
        return executableDirectory / L"QuickSiftData";
    } catch (...) {
        return {};
    }
}

inline std::filesystem::path DataRootPath() noexcept {
    const bool packaged = RunningWithPackageIdentity();
    const std::wstring family = packaged ? CurrentPackageFamilyName() : std::wstring{};
    const std::filesystem::path localAppData = packaged ? LocalAppDataDirectory() : std::filesystem::path{};
    return ResolveDataRootForContext(packaged, ExecutableDirectory(), localAppData, family);
}

inline std::filesystem::path CacheDirectoryPath() noexcept {
    const std::filesystem::path root = DataRootPath();
    if (root.empty()) return {};
    return root / L".cache";
}

inline std::filesystem::path SessionFilePath() noexcept {
    const std::filesystem::path root = DataRootPath();
    if (root.empty()) return {};
    return root / L"QuickSift.session.ini";
}

} // namespace quicksift::platform
