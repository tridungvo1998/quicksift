// CODE GUIDE: See CODE_GUIDE.md -> "Rules for native resources".
// OWNER: Process security, storage identity, and native helper implementation; check every OS result.

#include "runtime_support.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <memory>
#include <new>
#include <utility>
#include <winioctl.h>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace quicksift {
namespace {

std::size_t PageRound(std::size_t bytes) {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const std::size_t page = std::max<std::size_t>(4096, info.dwPageSize);
    return bytes > SIZE_MAX - (page - 1) ? SIZE_MAX : (bytes + page - 1) / page * page;
}

std::wstring ReadAheadPathKey(const fs::path& path) {
    std::wstring key = path.native();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return key;
}

void ApplyCpuSets(const std::vector<ULONG>& sets) {
    if (sets.empty()) return;
    using SetThreadSelectedCpuSetsFn = BOOL(WINAPI*)(HANDLE, const ULONG*, ULONG);
    static auto setCpuSets = reinterpret_cast<SetThreadSelectedCpuSetsFn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadSelectedCpuSets"));
    if (setCpuSets) setCpuSets(GetCurrentThread(), sets.data(), static_cast<ULONG>(sets.size()));
}

} // namespace

void InitializeProcessSecurity() noexcept {
    // These calls are defense-in-depth. The executable's PE flags remain the
    // primary enforcement and are checked by scripts/verify-binary.ps1.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    (void)HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
    (void)SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);

    PROCESS_MITIGATION_DEP_POLICY dep{};
    dep.Enable = 1;
    dep.Permanent = 1;
    (void)SetProcessMitigationPolicy(ProcessDEPPolicy, &dep, sizeof(dep));

    PROCESS_MITIGATION_ASLR_POLICY aslr{};
    aslr.EnableBottomUpRandomization = 1;
    aslr.EnableForceRelocateImages = 1;
    aslr.EnableHighEntropy = 1;
    (void)SetProcessMitigationPolicy(ProcessASLRPolicy, &aslr, sizeof(aslr));

    PROCESS_MITIGATION_IMAGE_LOAD_POLICY imageLoad{};
    imageLoad.NoRemoteImages = 1;
    imageLoad.NoLowMandatoryLabelImages = 1;
    imageLoad.PreferSystem32Images = 1;
    (void)SetProcessMitigationPolicy(ProcessImageLoadPolicy, &imageLoad, sizeof(imageLoad));
}

CpuTopology DetectCpuTopology() noexcept {
    CpuTopology topology;
    bool processCpuSetsEnumerated = false;
    topology.logicalProcessors = std::max<DWORD>(1, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));

    try {
    DWORD bytes = 0;
    GetSystemCpuSetInformation(nullptr, 0, &bytes, GetCurrentProcess(), 0);
    if (bytes != 0) {
        std::vector<std::uint8_t> storage(bytes);
        if (GetSystemCpuSetInformation(
            reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(storage.data()), bytes,
            &bytes, GetCurrentProcess(), 0)) {
            BYTE lowestEfficiency = 255;
            BYTE highestEfficiency = 0;
            std::size_t offset = 0;
            while (offset + sizeof(SYSTEM_CPU_SET_INFORMATION) <= storage.size()) {
                const auto* info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(storage.data() + offset);
                if (info->Size == 0 || offset + info->Size > storage.size()) break;
                if (info->Type == CpuSetInformation &&
                    (!info->CpuSet.Allocated || info->CpuSet.AllocatedToTargetProcess)) {
                    lowestEfficiency = std::min(lowestEfficiency, info->CpuSet.EfficiencyClass);
                    highestEfficiency = std::max(highestEfficiency, info->CpuSet.EfficiencyClass);
                }
                offset += info->Size;
            }
            topology.heterogeneous = lowestEfficiency != 255 && highestEfficiency > lowestEfficiency;
            unsigned eligibleLogicalProcessors = 0;
            std::vector<unsigned> eligibleCoreKeys;
            std::vector<unsigned> performanceCoreKeys;
            std::vector<unsigned> efficiencyCoreKeys;
            offset = 0;
            while (offset + sizeof(SYSTEM_CPU_SET_INFORMATION) <= storage.size()) {
                const auto* info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(storage.data() + offset);
                if (info->Size == 0 || offset + info->Size > storage.size()) break;
                if (info->Type == CpuSetInformation &&
                    (!info->CpuSet.Allocated || info->CpuSet.AllocatedToTargetProcess)) {
                    ++eligibleLogicalProcessors;
                    const unsigned coreKey = (static_cast<unsigned>(info->CpuSet.Group) << 16) |
                        static_cast<unsigned>(info->CpuSet.CoreIndex);
                    if (std::find(eligibleCoreKeys.begin(), eligibleCoreKeys.end(), coreKey) ==
                        eligibleCoreKeys.end()) {
                        eligibleCoreKeys.push_back(coreKey);
                    }
                    auto& classKeys = topology.heterogeneous &&
                        info->CpuSet.EfficiencyClass == lowestEfficiency ?
                        efficiencyCoreKeys : performanceCoreKeys;
                    if (std::find(classKeys.begin(), classKeys.end(), coreKey) == classKeys.end())
                        classKeys.push_back(coreKey);
                    // Parking is transient. Count parked CPUs for worker sizing, but
                    // do not pin a thread to one until Windows has made it schedulable.
                    if (!info->CpuSet.Parked) {
                        if (!topology.heterogeneous || info->CpuSet.EfficiencyClass == highestEfficiency) {
                            topology.performanceCpuSets.push_back(info->CpuSet.Id);
                        }
                        if (topology.heterogeneous && info->CpuSet.EfficiencyClass == lowestEfficiency) {
                            topology.efficiencyCpuSets.push_back(info->CpuSet.Id);
                        }
                    }
                }
                offset += info->Size;
            }
            if (eligibleLogicalProcessors != 0) {
                processCpuSetsEnumerated = true;
                topology.logicalProcessors = eligibleLogicalProcessors;
                topology.physicalCores = std::max(1u, static_cast<unsigned>(eligibleCoreKeys.size()));
                topology.performanceCores = std::max(1u,
                    static_cast<unsigned>(performanceCoreKeys.size()));
                topology.efficiencyCores = topology.heterogeneous ?
                    static_cast<unsigned>(efficiencyCoreKeys.size()) : 0u;
            }
        }
    }

    DWORD relationBytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &relationBytes);
    if (!processCpuSetsEnumerated && relationBytes != 0) {
        std::vector<std::uint8_t> relations(relationBytes);
        if (GetLogicalProcessorInformationEx(RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(relations.data()), &relationBytes)) {
            unsigned cores = 0;
            std::size_t offset = 0;
            while (offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= relations.size()) {
                const auto* relation = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(relations.data() + offset);
                if (relation->Size == 0 || offset + relation->Size > relations.size()) break;
                if (relation->Relationship == RelationProcessorCore) ++cores;
                offset += relation->Size;
            }
            topology.physicalCores = std::max(1u, cores);
            topology.performanceCores = topology.physicalCores;
            topology.efficiencyCores = 0;
        }
    }
    } catch (...) {
        // Topology is an optimization input. Under extreme allocation pressure,
        // retain the active-processor fallback rather than failing application startup.
        topology.performanceCpuSets.clear();
        topology.efficiencyCpuSets.clear();
        topology.heterogeneous = false;
        topology.physicalCores = std::max(1u, topology.physicalCores);
        topology.performanceCores = std::max(1u, topology.performanceCores);
        topology.efficiencyCores = 0;
    }
    topology.physicalCores = std::max(1u, topology.physicalCores);
    topology.performanceCores = std::clamp(topology.performanceCores, 1u,
        topology.physicalCores);
    if (!topology.heterogeneous) topology.efficiencyCores = 0;
    else topology.efficiencyCores = std::min(topology.efficiencyCores,
        topology.physicalCores - topology.performanceCores);
    return topology;
}

StorageProfile DetectStorageProfile(const fs::path& path, bool lowMemory) {
    StorageProfile profile;
    wchar_t volumePath[MAX_PATH]{};
    if (!GetVolumePathNameW(path.c_str(), volumePath, static_cast<DWORD>(std::size(volumePath)))) {
        profile.readAheadFiles = lowMemory ? 1 : 2;
        return profile;
    }

    const UINT driveType = GetDriveTypeW(volumePath);
    profile.remote = driveType == DRIVE_REMOTE;
    profile.removable = driveType == DRIVE_REMOVABLE;
    if (profile.remote) profile.storageClass = StorageClass::Network;
    else if (profile.removable) profile.storageClass = StorageClass::Removable;

    wchar_t volumeName[MAX_PATH]{};
    if (GetVolumeNameForVolumeMountPointW(volumePath, volumeName,
        static_cast<DWORD>(std::size(volumeName)))) {
        // GetVolumeNameForVolumeMountPoint returns a root path ending in a
        // backslash. CreateFile needs the volume device name without that final
        // separator when it is used for storage-property IOCTLs.
        std::wstring volumeDevice(volumeName);
        if (!volumeDevice.empty() && (volumeDevice.back() == L'\\' || volumeDevice.back() == L'/')) {
            volumeDevice.pop_back();
        }
        HANDLE volume = CreateFileW(volumeDevice.c_str(), 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (volume != INVALID_HANDLE_VALUE) {
            DEVICE_SEEK_PENALTY_DESCRIPTOR descriptor{};
            STORAGE_PROPERTY_QUERY query{};
            query.PropertyId = StorageDeviceSeekPenaltyProperty;
            query.QueryType = PropertyStandardQuery;
            DWORD returned = 0;
            if (DeviceIoControl(volume, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                &descriptor, sizeof(descriptor), &returned, nullptr) &&
                returned >= sizeof(descriptor)) {
                profile.seekPenalty = descriptor.IncursSeekPenalty != FALSE;
                if (!profile.remote && !profile.removable) {
                    profile.storageClass = profile.seekPenalty ? StorageClass::Rotational : StorageClass::SolidState;
                }
            }
            CloseHandle(volume);
        }
    }

    switch (profile.storageClass) {
    case StorageClass::SolidState:
        profile.readAheadFiles = lowMemory ? 1 : 3;
        profile.mappedFileLimit = lowMemory ? 128ull << 20 : 512ull << 20;
        break;
    case StorageClass::Rotational:
        profile.readAheadFiles = lowMemory ? 1 : 2;
        profile.mappedFileLimit = lowMemory ? 96ull << 20 : 256ull << 20;
        break;
    case StorageClass::Network:
    case StorageClass::Removable:
        profile.readAheadFiles = 1;
        profile.mappedFileLimit = lowMemory ? 64ull << 20 : 128ull << 20;
        break;
    default:
        profile.readAheadFiles = lowMemory ? 1 : 2;
        profile.mappedFileLimit = lowMemory ? 96ull << 20 : 256ull << 20;
        break;
    }
    return profile;
}

void ApplyInteractiveThreadPolicy(const CpuTopology& topology) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    if (topology.heterogeneous) ApplyCpuSets(topology.performanceCpuSets);

    THREAD_POWER_THROTTLING_STATE power{};
    power.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    power.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    power.StateMask = 0;
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &power, sizeof(power));
}

void ApplyBackgroundThreadPolicy(const CpuTopology& topology, bool ecoQos) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
    if (topology.heterogeneous) ApplyCpuSets(topology.efficiencyCpuSets);

    MEMORY_PRIORITY_INFORMATION memoryPriority{};
    memoryPriority.MemoryPriority = MEMORY_PRIORITY_LOW;
    SetThreadInformation(GetCurrentThread(), ThreadMemoryPriority,
        &memoryPriority, sizeof(memoryPriority));

    THREAD_POWER_THROTTLING_STATE power{};
    power.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    power.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    power.StateMask = ecoQos ? THREAD_POWER_THROTTLING_EXECUTION_SPEED : 0;
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &power, sizeof(power));
}

void RestoreNeutralThreadPolicy() {
    using SetThreadSelectedCpuSetsFn = BOOL(WINAPI*)(HANDLE, const ULONG*, ULONG);
    static auto setCpuSets = reinterpret_cast<SetThreadSelectedCpuSetsFn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadSelectedCpuSets"));
    if (setCpuSets) setCpuSets(GetCurrentThread(), nullptr, 0);

    MEMORY_PRIORITY_INFORMATION memoryPriority{};
    memoryPriority.MemoryPriority = MEMORY_PRIORITY_NORMAL;
    SetThreadInformation(GetCurrentThread(), ThreadMemoryPriority,
        &memoryPriority, sizeof(memoryPriority));

    THREAD_POWER_THROTTLING_STATE power{};
    power.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    power.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    power.StateMask = 0;
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &power, sizeof(power));

    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
}

MappedFileView::~MappedFileView() { Close(); }
MappedFileView::MappedFileView(MappedFileView&& other) noexcept { *this = std::move(other); }
MappedFileView& MappedFileView::operator=(MappedFileView&& other) noexcept {
    if (this == &other) return *this;
    Close();
    file_ = std::exchange(other.file_, INVALID_HANDLE_VALUE);
    mapping_ = std::exchange(other.mapping_, nullptr);
    view_ = std::exchange(other.view_, nullptr);
    size_ = std::exchange(other.size_, 0);
    return *this;
}

bool MappedFileView::Open(const fs::path& path, AccessPattern pattern,
    std::uint64_t maximumBytes, bool prefetch) {
    Close();
    const DWORD flags = pattern == AccessPattern::Sequential ? FILE_FLAG_SEQUENTIAL_SCAN : FILE_FLAG_RANDOM_ACCESS;
    file_ = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | flags, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file_, &size) || size.QuadPart <= 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > maximumBytes ||
        static_cast<std::uint64_t>(size.QuadPart) > SIZE_MAX) {
        Close();
        return false;
    }
    size_ = static_cast<std::size_t>(size.QuadPart);
    mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping_) { Close(); return false; }
    view_ = MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0);
    if (!view_) { Close(); return false; }

    if (prefetch) {
        using PrefetchVirtualMemoryFn = BOOL(WINAPI*)(HANDLE, ULONG_PTR, PWIN32_MEMORY_RANGE_ENTRY, ULONG);
        static auto prefetchMemory = reinterpret_cast<PrefetchVirtualMemoryFn>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "PrefetchVirtualMemory"));
        if (prefetchMemory) {
            // Prefetch only the leading encoded range. Pulling an entire 100–500 MiB
            // file into the standby list can evict useful pages on 8 GiB systems;
            // the sequential file hint handles the remainder as the codec advances.
            constexpr std::size_t kMaximumPrefetchBytes = 32ull * 1024ull * 1024ull;
            WIN32_MEMORY_RANGE_ENTRY range{ view_, std::min(size_, kMaximumPrefetchBytes) };
            prefetchMemory(GetCurrentProcess(), 1, &range, 0);
        }
    }
    return true;
}

void MappedFileView::Close() noexcept {
    if (view_) UnmapViewOfFile(view_);
    if (mapping_) CloseHandle(mapping_);
    if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
    view_ = nullptr;
    mapping_ = nullptr;
    file_ = INVALID_HANDLE_VALUE;
    size_ = 0;
}

VirtualBuffer::~VirtualBuffer() { Release(); }
VirtualBuffer::VirtualBuffer(VirtualBuffer&& other) noexcept { *this = std::move(other); }
VirtualBuffer& VirtualBuffer::operator=(VirtualBuffer&& other) noexcept {
    if (this == &other) return *this;
    Release();
    base_ = std::exchange(other.base_, nullptr);
    reserved_ = std::exchange(other.reserved_, 0);
    committed_ = std::exchange(other.committed_, 0);
    offered_ = std::exchange(other.offered_, false);
    return *this;
}

bool VirtualBuffer::Ensure(std::size_t bytes) {
    bytes = PageRound(bytes);
    if (bytes == 0 || bytes == SIZE_MAX) return false;
    if (base_ && reserved_ >= bytes) {
        if (offered_ && !Reclaim()) {
            // Discarded offered pages are still committed but contain undefined data.
            offered_ = false;
        }
        if (committed_ < bytes) {
            if (!VirtualAlloc(static_cast<std::uint8_t*>(base_) + committed_, bytes - committed_,
                MEM_COMMIT, PAGE_READWRITE)) return false;
            committed_ = bytes;
        }
        return true;
    }
    Release();
    base_ = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!base_) return false;
    reserved_ = committed_ = bytes;
    return true;
}

void VirtualBuffer::Release() noexcept {
    if (base_) VirtualFree(base_, 0, MEM_RELEASE);
    base_ = nullptr;
    reserved_ = committed_ = 0;
    offered_ = false;
}

void VirtualBuffer::Offer() noexcept {
    if (!base_ || committed_ == 0 || offered_) return;
    using OfferVirtualMemoryFn = DWORD(WINAPI*)(PVOID, SIZE_T, OFFER_PRIORITY);
    static auto offerMemory = reinterpret_cast<OfferVirtualMemoryFn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "OfferVirtualMemory"));
    if (offerMemory && offerMemory(base_, committed_, VmOfferPriorityLow) == ERROR_SUCCESS) offered_ = true;
}

bool VirtualBuffer::Reclaim() noexcept {
    if (!offered_) return true;
    using ReclaimVirtualMemoryFn = DWORD(WINAPI*)(PVOID, SIZE_T);
    static auto reclaimMemory = reinterpret_cast<ReclaimVirtualMemoryFn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ReclaimVirtualMemory"));
    if (!reclaimMemory) { offered_ = false; return true; }
    const DWORD result = reclaimMemory(base_, committed_);
    offered_ = false;
    return result == ERROR_SUCCESS;
}

VirtualBuffer BoundedVirtualBufferPool::Acquire(std::size_t bytes) {
    std::lock_guard lock(mutex_);
    auto best = buffers_.end();
    for (auto it = buffers_.begin(); it != buffers_.end(); ++it) {
        if (it->Size() >= bytes && (best == buffers_.end() || it->Size() < best->Size())) best = it;
    }
    if (best == buffers_.end()) {
        VirtualBuffer buffer;
        buffer.Ensure(bytes);
        return buffer;
    }
    VirtualBuffer result = std::move(*best);
    retainedBytes_ = result.Size() > retainedBytes_ ? 0 : retainedBytes_ - result.Size();
    buffers_.erase(best);
    result.Reclaim();
    return result;
}

void BoundedVirtualBufferPool::Return(VirtualBuffer buffer) noexcept {
    if (buffer.Size() == 0) return;
    try {
        std::lock_guard lock(mutex_);
        const std::size_t bytes = buffer.Size();
        if (bytes > byteLimit_ / 2 || retainedBytes_ > byteLimit_ - std::min(byteLimit_, bytes)) return;
        buffer.Offer();
        buffers_.push_back(std::move(buffer));
        retainedBytes_ += bytes;
    } catch (...) {
        // Read-ahead is opportunistic. Allocation failure should discard the
        // scratch buffer, never escape a Windows thread-pool callback.
    }
}

void BoundedVirtualBufferPool::Trim(bool critical) {
    std::lock_guard lock(mutex_);
    if (critical) {
        buffers_.clear();
        retainedBytes_ = 0;
        return;
    }
    const std::size_t target = byteLimit_ / 4;
    while (retainedBytes_ > target && !buffers_.empty()) {
        retainedBytes_ = buffers_.back().Size() > retainedBytes_ ? 0 : retainedBytes_ - buffers_.back().Size();
        buffers_.pop_back();
    }
    for (auto& buffer : buffers_) buffer.Offer();
}

void BoundedVirtualBufferPool::SetLimit(std::size_t bytes) {
    std::lock_guard lock(mutex_);
    byteLimit_ = bytes;
    while (retainedBytes_ > byteLimit_ && !buffers_.empty()) {
        retainedBytes_ = buffers_.back().Size() > retainedBytes_ ? 0 : retainedBytes_ - buffers_.back().Size();
        buffers_.pop_back();
    }
}


struct ThreadPoolReadAhead::Request {
    ThreadPoolReadAhead* owner = nullptr;
    std::filesystem::path path;
    std::wstring key;
    PTP_WORK work = nullptr;
};

ThreadPoolReadAhead::~ThreadPoolReadAhead() { CancelAll(); }

void ThreadPoolReadAhead::Configure(const CpuTopology& topology, unsigned maximumOutstanding,
    std::size_t maximumBytesPerFile, std::size_t bufferPoolBytes) {
    std::lock_guard lock(mutex_);
    topology_ = topology;
    maximumOutstanding_ = std::clamp(maximumOutstanding, 1u, 8u);
    maximumBytesPerFile_ = std::clamp<std::size_t>(maximumBytesPerFile,
        1ull * 1024ull * 1024ull, 256ull * 1024ull * 1024ull);
    const std::size_t minimumPool = 4ull * 1024ull * 1024ull;
    readBuffers_.SetLimit(std::clamp<std::size_t>(bufferPoolBytes,
        minimumPool, 256ull * 1024ull * 1024ull));
    stopping_.store(false, std::memory_order_release);
    paused_.store(false, std::memory_order_release);
}

void ThreadPoolReadAhead::Pause() noexcept {
    paused_.store(true, std::memory_order_release);
}

void ThreadPoolReadAhead::Resume() noexcept {
    paused_.store(false, std::memory_order_release);
}

void ThreadPoolReadAhead::Queue(const fs::path& path) {
    if (path.empty() || stopping_.load(std::memory_order_acquire) ||
        paused_.load(std::memory_order_acquire)) return;

    auto request = std::unique_ptr<Request>(new (std::nothrow) Request());
    if (!request) return;
    std::wstring key;
    try {
        request->owner = this;
        request->path = path;
        key = ReadAheadPathKey(path);
        request->key = key;
    } catch (...) {
        return;
    }

    try {
        std::lock_guard lock(mutex_);
        if (stopping_.load(std::memory_order_relaxed) || blocked_.contains(key) ||
            pending_.size() >= maximumOutstanding_ || !pending_.insert(key).second) return;
        ++activeCount_;
    } catch (...) {
        return;
    }

    request->work = CreateThreadpoolWork(&ThreadPoolReadAhead::WorkCallback, request.get(), nullptr);
    if (!request->work) {
        Complete(request.release());
        return;
    }
    SubmitThreadpoolWork(request->work);
    request.release();
}

bool ThreadPoolReadAhead::BlockPathsAndWait(const std::vector<fs::path>& paths,
    std::chrono::milliseconds timeout) {
    std::vector<std::wstring> keys;
    keys.reserve(paths.size());
    for (const fs::path& path : paths) {
        if (!path.empty()) keys.push_back(ReadAheadPathKey(path));
    }
    if (keys.empty()) return true;

    std::unique_lock lock(mutex_);
    for (const std::wstring& key : keys) ++blocked_[key];
    const auto released = [&] {
        return std::none_of(keys.begin(), keys.end(), [&](const std::wstring& key) {
            return pending_.contains(key);
        });
    };
    return idleCv_.wait_for(lock, timeout, released);
}

void ThreadPoolReadAhead::UnblockPaths(const std::vector<fs::path>& paths) noexcept {
    try {
        std::lock_guard lock(mutex_);
        for (const fs::path& path : paths) {
            if (path.empty()) continue;
            const std::wstring key = ReadAheadPathKey(path);
            const auto found = blocked_.find(key);
            if (found == blocked_.end()) continue;
            if (found->second <= 1) blocked_.erase(found);
            else --found->second;
        }
    } catch (...) {
    }
}

bool ThreadPoolReadAhead::IsBlocked(const std::wstring& key) const noexcept {
    try {
        std::lock_guard lock(mutex_);
        return blocked_.contains(key);
    } catch (...) {
        return true;
    }
}

void ThreadPoolReadAhead::Drain() {
    paused_.store(false, std::memory_order_release);
    CancelAll();
    // CancelAll is also the permanent shutdown primitive. A temporary drain for
    // an exclusive source-file write must reopen the queue afterward.
    stopping_.store(false, std::memory_order_release);
}

void ThreadPoolReadAhead::CancelAll() {
    stopping_.store(true, std::memory_order_release);
    paused_.store(false, std::memory_order_release);
    std::unique_lock lock(mutex_);
    idleCv_.wait(lock, [this] { return activeCount_ == 0; });
    pending_.clear();
    lock.unlock();
    readBuffers_.Trim(true);
}

void CALLBACK ThreadPoolReadAhead::WorkCallback(PTP_CALLBACK_INSTANCE instance, void* context, PTP_WORK) {
    auto* request = static_cast<Request*>(context);
    if (!request || !request->owner) return;
    ThreadPoolReadAhead* owner = request->owner;
    bool policyApplied = false;
    try {
        if (!owner->stopping_.load(std::memory_order_acquire) &&
            !owner->paused_.load(std::memory_order_acquire) &&
            !owner->IsBlocked(request->key)) {
            ApplyBackgroundThreadPolicy(owner->topology_, true);
            policyApplied = true;
            struct FileHandleGuard {
                HANDLE value = INVALID_HANDLE_VALUE;
                ~FileHandleGuard() {
                    if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
                }
            } file{
                CreateFileW(request->path.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)
            };
            if (file.value != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER size{};
                if (GetFileSizeEx(file.value, &size) && size.QuadPart > 0) {
                    const std::size_t bytes = static_cast<std::size_t>(std::min<std::uint64_t>(
                        static_cast<std::uint64_t>(size.QuadPart), owner->maximumBytesPerFile_));
                    constexpr std::size_t kChunk = 1024ull * 1024ull;
                    VirtualBuffer buffer = owner->readBuffers_.Acquire(std::min(kChunk, bytes));
                    std::size_t readTotal = 0;
                    if (buffer.Data() && buffer.Size() != 0) {
                        while (readTotal < bytes && !owner->stopping_.load(std::memory_order_relaxed) &&
                            !owner->paused_.load(std::memory_order_relaxed) &&
                            !owner->IsBlocked(request->key)) {
                            const DWORD requestBytes = static_cast<DWORD>(std::min<std::size_t>(
                                buffer.Size(), bytes - readTotal));
                            DWORD read = 0;
                            if (!ReadFile(file.value, buffer.Data(), requestBytes, &read, nullptr) || read == 0) break;
                            readTotal += read;
                        }
                        owner->readBuffers_.Return(std::move(buffer));
                    }
                }
            }
        }
    } catch (...) {
        // Read-ahead is a hint. Always release the pending slot and callback
        // object even under allocation pressure or an unexpected STL failure.
    }
    if (policyApplied) RestoreNeutralThreadPolicy();

    // Complete() closes the work object. Disassociate first so CloseThreadpoolWork
    // cannot wait for the callback that is currently performing the close.
    if (instance) DisassociateCurrentThreadFromCallback(instance);
    owner->Complete(request);
}

void ThreadPoolReadAhead::Complete(Request* request) noexcept {
    if (!request) return;
    if (request->work) CloseThreadpoolWork(request->work);
    {
        std::lock_guard lock(mutex_);
        pending_.erase(request->key);
        if (activeCount_ != 0) --activeCount_;
    }
    delete request;
    idleCv_.notify_all();
}

ResourcePressureMonitor::~ResourcePressureMonitor() { Stop(); }

bool ResourcePressureMonitor::StartWaitThread() noexcept {
    try {
        thread_ = std::thread([this] { Run(); });
        return true;
    } catch (...) {
        return false;
    }
}

void ResourcePressureMonitor::Start(HWND notifyWindow, UINT messageId, IDXGIAdapter3* adapter) {
    Stop();
    notifyWindow_ = notifyWindow;
    messageId_ = messageId;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    lowMemoryEvent_ = CreateMemoryResourceNotification(LowMemoryResourceNotification);
    highMemoryEvent_ = CreateMemoryResourceNotification(HighMemoryResourceNotification);
    if ((lowMemoryEvent_ == nullptr) != (highMemoryEvent_ == nullptr)) {
        if (lowMemoryEvent_) CloseHandle(lowMemoryEvent_);
        if (highMemoryEvent_) CloseHandle(highMemoryEvent_);
        lowMemoryEvent_ = highMemoryEvent_ = nullptr;
    }
    {
        std::lock_guard lock(mutex_);
        adapter_ = adapter;
        if (adapter_) {
            gpuBudgetEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (gpuBudgetEvent_ && FAILED(adapter_->RegisterVideoMemoryBudgetChangeNotificationEvent(
                gpuBudgetEvent_, &gpuCookie_))) {
                CloseHandle(gpuBudgetEvent_);
                gpuBudgetEvent_ = nullptr;
                gpuCookie_ = 0;
            }
        }
    }
    if (!stopEvent_ || !StartWaitThread()) {
        Stop();
    }
}

void ResourcePressureMonitor::SetAdapter(IDXGIAdapter3* adapter) {
    // The wait thread may currently be blocked on the old GPU event. Stop it
    // before replacing or closing that handle; closing a waited handle is undefined.
    if (stopEvent_) SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();

    {
        std::lock_guard lock(mutex_);
        if (adapter_ && gpuCookie_ != 0) {
            adapter_->UnregisterVideoMemoryBudgetChangeNotification(gpuCookie_);
        }
        if (gpuBudgetEvent_) CloseHandle(gpuBudgetEvent_);
        gpuBudgetEvent_ = nullptr;
        gpuCookie_ = 0;
        adapter_ = adapter;
        if (adapter_) {
            gpuBudgetEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (gpuBudgetEvent_ && FAILED(adapter_->RegisterVideoMemoryBudgetChangeNotificationEvent(
                gpuBudgetEvent_, &gpuCookie_))) {
                CloseHandle(gpuBudgetEvent_);
                gpuBudgetEvent_ = nullptr;
                gpuCookie_ = 0;
            }
        }
    }

    if (stopEvent_) {
        ResetEvent(stopEvent_);
        if (!StartWaitThread()) Stop();
    }
}

void ResourcePressureMonitor::Stop() {
    if (stopEvent_) SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard lock(mutex_);
        if (adapter_ && gpuCookie_ != 0) adapter_->UnregisterVideoMemoryBudgetChangeNotification(gpuCookie_);
        adapter_.Reset();
        gpuCookie_ = 0;
        if (gpuBudgetEvent_) CloseHandle(gpuBudgetEvent_);
        gpuBudgetEvent_ = nullptr;
    }
    if (lowMemoryEvent_) CloseHandle(lowMemoryEvent_);
    if (highMemoryEvent_) CloseHandle(highMemoryEvent_);
    if (stopEvent_) CloseHandle(stopEvent_);
    lowMemoryEvent_ = highMemoryEvent_ = stopEvent_ = nullptr;
    notifyWindow_ = nullptr;
    messageId_ = 0;
    memoryState_.store(-1, std::memory_order_release);
}

void ResourcePressureMonitor::Run() {
    bool policyApplied = false;
    try {
        ApplyBackgroundThreadPolicy(DetectCpuTopology(), true);
        policyApplied = true;

        BOOL lowMemory = FALSE;
        if (lowMemoryEvent_) QueryMemoryResourceNotification(lowMemoryEvent_, &lowMemory);
        bool inLowMemoryState = lowMemory != FALSE;
        const int currentState = inLowMemoryState ? 1 : 0;
        const int previousState = memoryState_.exchange(currentState, std::memory_order_acq_rel);
        if (notifyWindow_ && IsWindow(notifyWindow_)) {
            if (inLowMemoryState && previousState != currentState) {
                PostMessageW(notifyWindow_, messageId_,
                    static_cast<WPARAM>(ResourcePressureEvent::LowMemory), 0);
            } else if (!inLowMemoryState && previousState == 1) {
                PostMessageW(notifyWindow_, messageId_,
                    static_cast<WPARAM>(ResourcePressureEvent::HighMemory), 0);
            }
        }

        for (;;) {
            std::array<HANDLE, 3> handles{};
            DWORD count = 0;
            handles[count++] = stopEvent_;

            // Memory resource notification objects remain signalled while their
            // condition is true. Wait only for the opposite transition; waiting on
            // both continuously would spin and flood the UI with duplicate messages.
            HANDLE memoryTransition = inLowMemoryState ? highMemoryEvent_ : lowMemoryEvent_;
            if (memoryTransition) handles[count++] = memoryTransition;
            {
                std::lock_guard lock(mutex_);
                if (gpuBudgetEvent_) handles[count++] = gpuBudgetEvent_;
            }

            const DWORD wait = WaitForMultipleObjects(count, handles.data(), FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0) break;
            if (wait == WAIT_FAILED || wait < WAIT_OBJECT_0 || wait >= WAIT_OBJECT_0 + count) break;
            const HANDLE signalled = handles[wait - WAIT_OBJECT_0];
            ResourcePressureEvent event = ResourcePressureEvent::GpuBudgetChanged;
            if (signalled == memoryTransition) {
                inLowMemoryState = !inLowMemoryState;
                memoryState_.store(inLowMemoryState ? 1 : 0, std::memory_order_release);
                event = inLowMemoryState ? ResourcePressureEvent::LowMemory :
                    ResourcePressureEvent::HighMemory;
            }
            if (notifyWindow_ && IsWindow(notifyWindow_)) {
                PostMessageW(notifyWindow_, messageId_, static_cast<WPARAM>(event), 0);
            }
        }
    } catch (...) {
        // Monitoring is an optimization. A topology-allocation failure or other
        // unexpected exception must disable the monitor, not terminate the app.
    }
    if (policyApplied) RestoreNeutralThreadPolicy();
}

} // namespace quicksift
