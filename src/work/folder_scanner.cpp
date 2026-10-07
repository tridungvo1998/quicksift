// OWNER: Bounded folder enumeration and queue-owned scan completion delivery.
#include "work/folder_scanner.h"

#include <chrono>

namespace quicksift::app {
namespace {

bool PostScannerWakeWithRetry(HWND notify) noexcept {
    if (!notify) return false;
    constexpr DWORD kRetryDelaysMs[] = { 0, 1, 2, 4, 8 };
    for (DWORD delay : kRetryDelaysMs) {
        if (delay != 0) Sleep(delay);
        if (PostMessageW(notify, WM_APP_BACKGROUND_COMPLETION, 0, 0) != FALSE) return true;
        if (!IsWindow(notify)) return false;
    }
    return false;
}

std::wstring ScanFailureDetail(DWORD error, std::wstring_view operation) {
    std::wstring detail(operation);
    detail += L" (Windows error ";
    detail += std::to_wstring(error);
    detail += L").";
    return detail;
}

std::filesystem::file_time_type FileTimeToFilesystemTime(const FILETIME& value) noexcept {
    ULARGE_INTEGER ticks{};
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    using HundredNanoseconds = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;
    constexpr std::int64_t kWindowsToUnixEpochSeconds = 11'644'473'600ll;
    const auto windowsDuration = HundredNanoseconds(static_cast<std::int64_t>(ticks.QuadPart));
    const auto systemDuration = std::chrono::duration_cast<std::chrono::system_clock::duration>(
        windowsDuration - std::chrono::seconds(kWindowsToUnixEpochSeconds));
    const auto systemTime = std::chrono::system_clock::time_point(systemDuration);
    const auto delta = systemTime - std::chrono::system_clock::now();
    return std::filesystem::file_time_type::clock::now() +
        std::chrono::duration_cast<std::filesystem::file_time_type::duration>(delta);
}

} // namespace

FolderScanner::FolderScanner(HWND notify,
    quicksift::work::BackgroundCompletionQueue* completionQueue,
    quicksift::PersistentCache* cache, const quicksift::CpuTopology& topology)
    : notify_(notify), completionQueue_(completionQueue), cache_(cache), topology_(topology) {
    (void)cache_;
}

FolderScanner::~FolderScanner() {
    shutdown_.store(true, std::memory_order_release);
    requestedGeneration_.store(0, std::memory_order_release);
    {
        std::lock_guard lock(requestMutex_);
        pendingRequest_.reset();
    }
    requestCv_.notify_all();
    batchCv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void FolderScanner::SetNotify(HWND notify) {
    std::lock_guard lock(requestMutex_);
    notify_ = notify;
}

bool FolderScanner::Cancelled(std::uint64_t generation) const noexcept {
    return shutdown_.load(std::memory_order_acquire) ||
        requestedGeneration_.load(std::memory_order_acquire) != generation;
}

void FolderScanner::AcknowledgeBatch(uint64_t generation) {
    {
        std::lock_guard lock(batchMutex_);
        if (generation == activeBatchGeneration_ && outstandingBatches_ > 0) --outstandingBatches_;
    }
    batchCv_.notify_all();
}

void FolderScanner::Cancel() noexcept {
    requestedGeneration_.store(0, std::memory_order_release);
    {
        std::lock_guard lock(requestMutex_);
        pendingRequest_.reset();
    }
    requestCv_.notify_all();
    batchCv_.notify_all();
}

void FolderScanner::Start(const fs::path& folder, uint64_t generation) {
    HWND notify = nullptr;
    {
        std::lock_guard lock(requestMutex_);
        notify = notify_;
    }
    if (!notify || !IsWindow(notify)) {
        QS_LOG_ERROR(L"Catalog", L"Folder scan was not started because its UI target is invalid");
        return;
    }

    try {
        if (!thread_.joinable()) thread_ = std::thread([this] { ThreadMain(); });
    } catch (...) {
        QS_LOG_CRITICAL(L"Catalog", L"Folder scanner thread creation failed");
        if (completionQueue_ && completionQueue_->PushTerminal(
            quicksift::work::BackgroundCompletion::ForScanCompletion(
                generation, quicksift::work::ScanCompletionStatus::Failed,
                ERROR_NOT_ENOUGH_MEMORY,
                L"QuickSift could not create the folder-scanner thread."))) {
            PostScannerWakeWithRetry(notify);
        }
        return;
    }

    requestedGeneration_.store(generation, std::memory_order_release);
    {
        std::lock_guard lock(batchMutex_);
        activeBatchGeneration_ = generation;
        outstandingBatches_ = 0;
    }
    {
        std::lock_guard lock(requestMutex_);
        pendingRequest_ = ScanRequest{ folder, generation, notify };
    }
    batchCv_.notify_all();
    requestCv_.notify_one();
}

void FolderScanner::ThreadMain() {
    quicksift::ApplyBackgroundThreadPolicy(topology_, true);
    struct BackgroundModeEnd {
        ~BackgroundModeEnd() { SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END); }
    } backgroundModeEnd;

    while (!shutdown_.load(std::memory_order_acquire)) {
        ScanRequest request;
        {
            std::unique_lock lock(requestMutex_);
            requestCv_.wait(lock, [&] {
                return shutdown_.load(std::memory_order_acquire) || pendingRequest_.has_value();
            });
            if (shutdown_.load(std::memory_order_acquire)) break;
            request = std::move(*pendingRequest_);
            pendingRequest_.reset();
        }
        if (!Cancelled(request.generation)) RunScan(request);
    }
}

void FolderScanner::RunScan(const ScanRequest& request) {
    const auto scanStarted = std::chrono::steady_clock::now();
    const std::uint64_t generation = request.generation;
    QS_LOG_EVENT(quicksift::diagnostics::Level::Info, L"Catalog", L"scan_started",
        {L"generation", std::to_wstring(generation)},
        {L"folder", request.folder.filename().wstring()});
    const HWND notify = request.notify;
    auto postCompletion = [&](quicksift::work::ScanCompletionStatus status,
        DWORD error, std::wstring detail) {
        if (!completionQueue_ || Cancelled(generation)) return false;
        bool transitionedFromEmpty = false;
        const bool queued = completionQueue_->PushTerminal(
            quicksift::work::BackgroundCompletion::ForScanCompletion(
                generation, status, error, std::move(detail)), &transitionedFromEmpty);
        if (queued && transitionedFromEmpty) PostScannerWakeWithRetry(notify);
        return queued;
    };

    try {
        auto postBatch = [&](std::unique_ptr<ScanBatch>& batch) {
            {
                std::unique_lock lock(batchMutex_);
                batchCv_.wait(lock, [&] {
                    return Cancelled(generation) || activeBatchGeneration_ != generation ||
                        outstandingBatches_ < kMaximumOutstandingBatches;
                });
                if (Cancelled(generation) || activeBatchGeneration_ != generation) return false;
                ++outstandingBatches_;
            }
            struct BatchReservationGuard {
                FolderScanner* scanner = nullptr;
                std::uint64_t generation = 0;
                bool active = true;
                ~BatchReservationGuard() { if (active && scanner) scanner->AcknowledgeBatch(generation); }
                void Release() noexcept { active = false; }
            } reservation{ this, generation };

            if (!completionQueue_) return false;
            std::shared_ptr<ScanBatch> posted(std::move(batch));
            // The persistent scanner has no single stop flag suitable for PushWait;
            // use bounded non-blocking retries so a superseded generation exits quickly.
            while (!Cancelled(generation)) {
                bool transitionedFromEmpty = false;
                if (completionQueue_->Push(
                        quicksift::work::BackgroundCompletion::ForScanBatch(posted),
                        &transitionedFromEmpty)) {
                    reservation.Release();
                    if (transitionedFromEmpty) PostScannerWakeWithRetry(notify);
                    return true;
                }
                Sleep(5);
            }
            return false;
        };

        size_t batchTarget = kInitialBatchSize;
        auto batch = std::make_unique<ScanBatch>();
        batch->generation = generation;
        batch->photos.reserve(batchTarget);
        quicksift::work::ScanCompletionStatus completionStatus =
            quicksift::work::ScanCompletionStatus::Succeeded;
        DWORD completionError = ERROR_SUCCESS;
        std::wstring completionDetail;

        const fs::path pattern = request.folder / L"*";
        WIN32_FIND_DATAW found{};
        HANDLE search = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &found,
            FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (search == INVALID_HANDLE_VALUE) {
            const DWORD firstError = GetLastError();
            search = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &found,
                FindExSearchNameMatch, nullptr, 0);
            if (search == INVALID_HANDLE_VALUE) {
                completionError = GetLastError();
                if (completionError == ERROR_SUCCESS) completionError = firstError;
                if (completionError != ERROR_FILE_NOT_FOUND && completionError != ERROR_NO_MORE_FILES) {
                    completionStatus = quicksift::work::ScanCompletionStatus::Failed;
                    completionDetail = ScanFailureDetail(completionError,
                        L"QuickSift could not enumerate the folder");
                }
            }
        }
        struct FindHandleGuard {
            HANDLE value = INVALID_HANDLE_VALUE;
            ~FindHandleGuard() { if (value != INVALID_HANDLE_VALUE) FindClose(value); }
        } searchGuard{ search };

        const quicksift::core::FormatSelection allFormats{};
        if (search != INVALID_HANDLE_VALUE) {
            bool continueEnumeration = true;
            while (continueEnumeration && !Cancelled(generation)) {
                if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
                    wcscmp(found.cFileName, L".") != 0 && wcscmp(found.cFileName, L"..") != 0) {
                    const fs::path candidate = request.folder / found.cFileName;
                    const std::wstring extension = ExtensionLower(candidate);
                    if (quicksift::core::IsFormatAllowed(extension, allFormats)) {
                        PhotoItem item;
                        item.path = candidate;
                        item.name = found.cFileName;
                        item.sortName = ToLower(item.name);
                        item.extension = extension;
                        item.modified = FileTimeToFilesystemTime(found.ftLastWriteTime);
                        // Cheap discovery uses data already returned by FindFirst/Next.
                        // Strong identity and sidecar fingerprints are deferred to
                        // metadata/decode workers before persistent-cache admission.
                        item.modifiedStamp = quicksift::PersistentCache::FileTimeStamp(item.modified);
                        item.fileSize = (static_cast<std::uint64_t>(found.nFileSizeHigh) << 32) |
                            static_cast<std::uint64_t>(found.nFileSizeLow);
                        item.sidecarStamp = 0;
                        batch->photos.push_back(std::move(item));
                        if (batch->photos.size() >= batchTarget) {
                            if (!postBatch(batch)) break;
                            batchTarget = kSteadyBatchSize;
                            batch = std::make_unique<ScanBatch>();
                            batch->generation = generation;
                            batch->photos.reserve(batchTarget);
                        }
                    }
                }

                continueEnumeration = FindNextFileW(search, &found) != FALSE;
                if (!continueEnumeration) {
                    const DWORD nextError = GetLastError();
                    if (nextError != ERROR_NO_MORE_FILES) {
                        completionError = nextError;
                        completionStatus = quicksift::work::ScanCompletionStatus::Partial;
                        completionDetail = ScanFailureDetail(nextError,
                            L"Folder enumeration stopped before reaching the end");
                    }
                }
            }
        }

        if (!Cancelled(generation) && batch && !batch->photos.empty()) {
            if (!postBatch(batch) && !Cancelled(generation)) {
                completionStatus = quicksift::work::ScanCompletionStatus::Failed;
                completionDetail = L"A scan batch could not be delivered to the catalog.";
            }
        }
        const double scanMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - scanStarted).count();
        QS_LOG_EVENT(completionStatus == quicksift::work::ScanCompletionStatus::Succeeded ?
                quicksift::diagnostics::Level::Info : quicksift::diagnostics::Level::Warning,
            L"Catalog", L"scan_finished",
            {L"generation", std::to_wstring(generation)},
            {L"status", std::to_wstring(static_cast<int>(completionStatus))},
            {L"error", std::to_wstring(completionError)},
            {L"duration_ms", std::to_wstring(scanMs)},
            {L"cancelled", Cancelled(generation) ? L"1" : L"0"});
        if (!Cancelled(generation) && !postCompletion(completionStatus, completionError,
            std::move(completionDetail))) {
            QS_LOG_ERROR(L"Catalog", L"Folder scan completion could not be queued");
        }
    } catch (...) {
        QS_LOG_ERROR(L"Catalog", L"Folder scanner aborted after an unexpected exception");
        if (!Cancelled(generation) && !postCompletion(
            quicksift::work::ScanCompletionStatus::Failed, ERROR_GEN_FAILURE,
            L"The folder scanner stopped after an unexpected exception.")) {
            QS_LOG_ERROR(L"Catalog", L"Failed folder scan completion could not be queued");
        }
    }
}

} // namespace quicksift::app
