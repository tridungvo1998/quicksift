// CODE GUIDE: Isolated Shell handlers; no UI/COM resources cross the worker boundary.
#pragma once
#include "core/shell_preview_policy.h"
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace quicksift::work {
struct ShellPreviewRequest { std::wstring path; core::ShellPreviewIdentity identity; };
struct ShellPreviewResult {
    ShellPreviewRequest request;
    int width = 0, height = 0;
    std::vector<unsigned char> pixels;
};
// Must run before normal process initialization. Private mode uses only inherited memory.
int RunShellPreviewChild() noexcept;
class ShellPreviewService {
public:
    ~ShellPreviewService() { Stop(); }
    bool Request(ShellPreviewRequest request);
    bool TryPop(ShellPreviewResult& result);
    bool HasWork();
    void Cancel(std::uint64_t generation);
    void Stop() noexcept;
private:
    void Run() noexcept;
    ShellPreviewResult Extract(ShellPreviewRequest request);
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<ShellPreviewRequest> requests_;
    std::deque<ShellPreviewResult> results_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> active_{false};
    std::atomic<std::uint64_t> generation_{0};
};
}
