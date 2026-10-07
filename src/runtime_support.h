// CODE GUIDE: See CODE_GUIDE.md -> "Rules for native resources".
// OWNER: Windows runtime/filesystem/security helpers behind explicit ownership types.

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <atomic>
#include <cstddef>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <span>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace quicksift {

void InitializeProcessSecurity() noexcept;

enum class StorageClass {
    Unknown,
    Network,
    Rotational,
    SolidState,
    Removable
};

struct CpuTopology {
    unsigned logicalProcessors = 1;
    unsigned physicalCores = 1;
    unsigned performanceCores = 1;
    unsigned efficiencyCores = 0;
    std::vector<ULONG> performanceCpuSets;
    std::vector<ULONG> efficiencyCpuSets;
    bool heterogeneous = false;
};

struct StorageProfile {
    StorageClass storageClass = StorageClass::Unknown;
    bool seekPenalty = false;
    bool remote = false;
    bool removable = false;
    unsigned readAheadFiles = 1;
    std::size_t mappedFileLimit = 128ull * 1024ull * 1024ull;
};

CpuTopology DetectCpuTopology() noexcept;
StorageProfile DetectStorageProfile(const std::filesystem::path& path, bool lowMemory);
void ApplyInteractiveThreadPolicy(const CpuTopology& topology);
void ApplyBackgroundThreadPolicy(const CpuTopology& topology, bool ecoQos = true);
void RestoreNeutralThreadPolicy();

class MappedFileView {
public:
    enum class AccessPattern { Sequential, Random };

    MappedFileView() = default;
    ~MappedFileView();
    MappedFileView(const MappedFileView&) = delete;
    MappedFileView& operator=(const MappedFileView&) = delete;
    MappedFileView(MappedFileView&& other) noexcept;
    MappedFileView& operator=(MappedFileView&& other) noexcept;

    bool Open(const std::filesystem::path& path, AccessPattern pattern,
        std::uint64_t maximumBytes, bool prefetch);
    void Close() noexcept;
    std::span<const std::uint8_t> Bytes() const noexcept {
        return { static_cast<const std::uint8_t*>(view_), size_ };
    }
    bool Empty() const noexcept { return view_ == nullptr || size_ == 0; }

private:
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
    void* view_ = nullptr;
    std::size_t size_ = 0;
};

class VirtualBuffer {
public:
    VirtualBuffer() = default;
    ~VirtualBuffer();
    VirtualBuffer(const VirtualBuffer&) = delete;
    VirtualBuffer& operator=(const VirtualBuffer&) = delete;
    VirtualBuffer(VirtualBuffer&& other) noexcept;
    VirtualBuffer& operator=(VirtualBuffer&& other) noexcept;

    bool Ensure(std::size_t bytes);
    void Release() noexcept;
    void Offer() noexcept;
    bool Reclaim() noexcept;
    std::uint8_t* Data() noexcept { return static_cast<std::uint8_t*>(base_); }
    const std::uint8_t* Data() const noexcept { return static_cast<const std::uint8_t*>(base_); }
    std::size_t Size() const noexcept { return committed_; }

private:
    void* base_ = nullptr;
    std::size_t reserved_ = 0;
    std::size_t committed_ = 0;
    bool offered_ = false;
};

class BoundedVirtualBufferPool {
public:
    explicit BoundedVirtualBufferPool(std::size_t byteLimit = 64ull * 1024ull * 1024ull)
        : byteLimit_(byteLimit) {}

    VirtualBuffer Acquire(std::size_t bytes);
    void Return(VirtualBuffer buffer) noexcept;
    void Trim(bool critical);
    void SetLimit(std::size_t bytes);

private:
    std::mutex mutex_;
    std::vector<VirtualBuffer> buffers_;
    std::size_t retainedBytes_ = 0;
    std::size_t byteLimit_ = 0;
};

class ThreadPoolReadAhead {
public:
    ThreadPoolReadAhead() = default;
    ~ThreadPoolReadAhead();
    ThreadPoolReadAhead(const ThreadPoolReadAhead&) = delete;
    ThreadPoolReadAhead& operator=(const ThreadPoolReadAhead&) = delete;

    void Configure(const CpuTopology& topology, unsigned maximumOutstanding,
        std::size_t maximumBytesPerFile, std::size_t bufferPoolBytes);
    void Queue(const std::filesystem::path& path);
    bool BlockPathsAndWait(const std::vector<std::filesystem::path>& paths,
        std::chrono::milliseconds timeout);
    void UnblockPaths(const std::vector<std::filesystem::path>& paths) noexcept;
    void Pause() noexcept;
    void Resume() noexcept;
    void Drain();
    void CancelAll();

private:
    struct Request;
    static void CALLBACK WorkCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK work);
    void Complete(Request* request) noexcept;
    [[nodiscard]] bool IsBlocked(const std::wstring& key) const noexcept;

    mutable std::mutex mutex_;
    std::condition_variable idleCv_;
    std::unordered_set<std::wstring> pending_;
    // Reference counts matter because two independent metadata transactions can
    // temporarily exclude the same source path. Releasing one registration must
    // not reopen read-ahead while the other transaction still owns its block.
    std::unordered_map<std::wstring, unsigned> blocked_;
    unsigned activeCount_ = 0;
    CpuTopology topology_;
    unsigned maximumOutstanding_ = 1;
    std::size_t maximumBytesPerFile_ = 32ull * 1024ull * 1024ull;
    std::atomic<bool> stopping_{ false };
    std::atomic<bool> paused_{ false };
    BoundedVirtualBufferPool readBuffers_{ 8ull * 1024ull * 1024ull };
};

enum class ResourcePressureEvent : WPARAM {
    LowMemory = 1,
    HighMemory = 2,
    GpuBudgetChanged = 3
};

class ResourcePressureMonitor {
public:
    ResourcePressureMonitor() = default;
    ~ResourcePressureMonitor();
    ResourcePressureMonitor(const ResourcePressureMonitor&) = delete;
    ResourcePressureMonitor& operator=(const ResourcePressureMonitor&) = delete;

    void Start(HWND notifyWindow, UINT messageId, IDXGIAdapter3* adapter = nullptr);
    void SetAdapter(IDXGIAdapter3* adapter);
    void Stop();

private:
    bool StartWaitThread() noexcept;
    void Run();

    HWND notifyWindow_ = nullptr;
    UINT messageId_ = 0;
    HANDLE stopEvent_ = nullptr;
    HANDLE lowMemoryEvent_ = nullptr;
    HANDLE highMemoryEvent_ = nullptr;
    HANDLE gpuBudgetEvent_ = nullptr;
    DWORD gpuCookie_ = 0;
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter_;
    std::mutex mutex_;
    std::thread thread_;
    std::atomic<int> memoryState_{ -1 };
};

} // namespace quicksift
