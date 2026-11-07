// CODE GUIDE: Child process isolation bounds third-party Shell handler lifetime.
#include "work/shell_preview_service.h"
#include "core/priority_policy.h"
#include "runtime_support.h"
#include "diagnostics/log_and_continue.h"
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
namespace quicksift::work {
namespace {
using Microsoft::WRL::ComPtr;
constexpr std::size_t kPathCapacity = 32768;
constexpr std::size_t kPixelCapacity = static_cast<std::size_t>(core::kShellPreviewMaximumEdge) * core::kShellPreviewMaximumEdge * 4;
struct SharedPreview {
    unsigned target = 0, width = 0, height = 0;
    wchar_t path[kPathCapacity]{};
    unsigned char pixels[kPixelCapacity]{};
};
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Mapping {
    void* value = nullptr;
    ~Mapping() { if (value) UnmapViewOfFile(value); }
};
}
int RunShellPreviewChild() noexcept {
    quicksift::InitializeProcessSecurity();
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(com)) return 1;
    int status = 1;
    try {
        Mapping mapping{MapViewOfFile(GetStdHandle(STD_INPUT_HANDLE), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedPreview))};
        auto* shared = static_cast<SharedPreview*>(mapping.value);
        if (shared && shared->target > 0 && shared->target <= core::kShellPreviewMaximumEdge &&
            shared->path[0] && shared->path[kPathCapacity - 1] == 0) {
            ComPtr<IShellItemImageFactory> shell;
            HBITMAP bitmap = nullptr;
            ComPtr<IWICImagingFactory> wic;
            if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
                SUCCEEDED(SHCreateItemFromParsingName(shared->path, nullptr, IID_PPV_ARGS(&shell))) &&
                SUCCEEDED(shell->GetImage(SIZE{static_cast<LONG>(shared->target), static_cast<LONG>(shared->target)},
                    SIIGBF_RESIZETOFIT, &bitmap)) && bitmap) {
                ComPtr<IWICBitmap> image;
                ComPtr<IWICFormatConverter> converter;
                if (SUCCEEDED(wic->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapUsePremultipliedAlpha, &image)) &&
                    SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
                    SUCCEEDED(converter->Initialize(image.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                        nullptr, 0, WICBitmapPaletteTypeCustom))) {
                    UINT width = 0, height = 0;
                    if (SUCCEEDED(converter->GetSize(&width, &height)) && width > 0 && height > 0 &&
                        width <= core::kShellPreviewMaximumEdge && height <= core::kShellPreviewMaximumEdge &&
                        SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(kPixelCapacity), shared->pixels))) {
                        shared->width = width; shared->height = height; status = 0;
                    }
                }
                DeleteObject(bitmap);
            }
        }
    } catch (...) { status = 1; } // private child has no logger or UI lifetime
    CoUninitialize();
    return status;
}
bool ShellPreviewService::Request(ShellPreviewRequest request) {
    std::lock_guard lock(mutex_);
    if (stop_ || request.identity.generation != generation_ || requests_.size() >= core::kShellPreviewQueueLimit) return false;
    if (!thread_.joinable()) thread_ = std::thread([this] { Run(); });
    requests_.push_back(std::move(request)); cv_.notify_one(); return true;
}
bool ShellPreviewService::TryPop(ShellPreviewResult& result) {
    std::lock_guard lock(mutex_);
    if (results_.empty()) return false;
    result = std::move(results_.front()); results_.pop_front(); cv_.notify_one(); return true;
}
bool ShellPreviewService::HasWork() {
    std::lock_guard lock(mutex_);
    return active_ || !requests_.empty() || !results_.empty();
}
void ShellPreviewService::Cancel(std::uint64_t generation) {
    generation_.store(generation, std::memory_order_release);
    std::lock_guard lock(mutex_); requests_.clear(); results_.clear(); cv_.notify_one();
}
void ShellPreviewService::Stop() noexcept {
    stop_.store(true, std::memory_order_release); cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}
void ShellPreviewService::Run() noexcept {
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
    for (;;) {
        try {
            ShellPreviewRequest request;
            {
                std::unique_lock lock(mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(50), [&] { return stop_ || !requests_.empty(); });
                if (stop_) return;
                if (requests_.empty()) continue;
                if (core::priority::Demand().Snapshot().viewForeground != 0) {
                    cv_.wait_for(lock, std::chrono::milliseconds(50)); continue;
                }
                request = std::move(requests_.front()); requests_.pop_front(); active_.store(true);
            }
            struct ActiveGuard {
                std::atomic<bool>& active;
                ~ActiveGuard() { active.store(false, std::memory_order_release); }
            } activeGuard{active_};
            auto result = Extract(std::move(request));
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&] { return stop_ || results_.size() < core::kShellPreviewQueueLimit ||
                result.request.identity.generation != generation_; });
            if (stop_) return;
            if (result.request.identity.generation == generation_) results_.push_back(std::move(result));
        } catch (...) { QS_LOG_AND_CONTINUE(L"ShellPreview", L"ShellPreviewService::Run"); }
    }
}
ShellPreviewResult ShellPreviewService::Extract(ShellPreviewRequest request) {
    ShellPreviewResult result; result.request = std::move(request);
    if (result.request.path.size() >= kPathCapacity) return result;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle memory{CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0, sizeof(SharedPreview), nullptr)};
    if (!memory.value) return result;
    Mapping mapping{MapViewOfFile(memory.value, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedPreview))};
    if (!mapping.value) return result;
    auto* shared = static_cast<SharedPreview*>(mapping.value);
    shared->target = static_cast<unsigned>(result.request.identity.target);
    std::copy(result.request.path.begin(), result.request.path.end(), shared->path);
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    if (!length || length >= std::size(executable)) return result;
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --quicksift-shell-preview";
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<unsigned char> attributes(attributeBytes);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &attributeBytes)) return result;
    struct AttributeCleanup { LPPROC_THREAD_ATTRIBUTE_LIST list; ~AttributeCleanup() { DeleteProcThreadAttributeList(list); } } cleanup{list};
    HANDLE inherited[] = {memory.value};
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) return result;
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = list;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES; startup.StartupInfo.hStdInput = memory.value;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | BELOW_NORMAL_PRIORITY_CLASS,
        nullptr, nullptr, &startup.StartupInfo, &process)) return result;
    Handle child{process.hProcess}, childThread{process.hThread};
    Handle job{CreateJobObjectW(nullptr, nullptr)};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    const bool isolated = job.value && SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
        AssignProcessToJobObject(job.value, child.value);
    if (!isolated || ResumeThread(childThread.value) == static_cast<DWORD>(-1)) {
        TerminateProcess(child.value, 1); return result;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(core::kShellPreviewTimeoutMs);
    DWORD wait = WAIT_TIMEOUT;
    while ((wait = WaitForSingleObject(child.value, 50)) == WAIT_TIMEOUT) {
        if (stop_ || result.request.identity.generation != generation_ || std::chrono::steady_clock::now() >= deadline) return result;
    }
    DWORD exitCode = 1;
    if (wait != WAIT_OBJECT_0 || !GetExitCodeProcess(child.value, &exitCode) || exitCode != 0) return result;
    const auto width = shared->width, height = shared->height;
    if (!width || !height || width > core::kShellPreviewMaximumEdge || height > core::kShellPreviewMaximumEdge) return result;
    const std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
    result.pixels.assign(shared->pixels, shared->pixels + bytes);
    result.width = static_cast<int>(width); result.height = static_cast<int>(height);
    return result;
}
}
