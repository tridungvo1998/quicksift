// CODE GUIDE: See CODE_GUIDE.md -> "Logging and crashes".
// OWNER: Session log, export, crash report, and minidump implementation; crash paths must be re-entry safe.

#include "diagnostic_log.h"

#include "../version.h"
#include "platform/application_data_paths.h"
#include "core/priority_policy.h"
#include "core/lock_order.h"
#include "core/log_retention.h"

#include <commdlg.h>
#include <dbghelp.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <cstdint>
#include <climits>
#include <deque>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace quicksift::diagnostics {
namespace {

struct Entry {
    SYSTEMTIME time{};
    std::uint64_t monotonicMs = 0;
    std::uint64_t sequence = 0;
    DWORD processId = 0;
    DWORD threadId = 0;
    Level level = Level::Info;
    std::wstring category;
    std::wstring message;
    std::wstring event;
};

// Lock order: streamMutex -> mutex. Loggers (any thread, including the UI thread)
// take only `mutex`, for an in-memory push. Disk I/O happens under `streamMutex`
// alone (writer thread, Critical sync flush, shutdown), so a slow disk can never
// stall a logging thread (0.9.4.0 polish: the writer used to write under `mutex`,
// and every Warning flushed synchronously on the calling thread).
struct State {
    std::mutex streamMutex; // guards stream, diskBytes, diskLimitReached
    std::mutex mutex;
    std::deque<Entry> entries;
    fs::path sessionPath;
    fs::path dataRoot;
    fs::path crashDirectory;
    std::ofstream stream;
    std::deque<std::string> diskQueue;
    std::condition_variable diskCv;
    std::thread diskThread;
    bool diskStop = false;
    std::uint64_t diskQueueDropped = 0;
    std::uint64_t diskBytes = 0;
    std::uint64_t sessionId = 0;
    std::atomic<std::uint64_t> sequence{ 0 };
    std::uint64_t droppedEntries = 0;
    std::uint64_t eventsByLevel[5]{};
    std::uint64_t lastSystemSnapshotMs = 0;
    bool diskLimitReached = false;
    std::atomic<bool> streamWritable{ false };   // lock-free gate for producers
    std::atomic<bool> flushRequested{ false };   // a Warning+ line awaits an OS flush
    LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
    std::atomic<bool> initialized{ false };
    std::atomic_flag crashInProgress = ATOMIC_FLAG_INIT;
};

State& GetState() {
    static State state;
    return state;
}

std::atomic<Level> gLogLevel{Level::Info};
std::atomic<bool> gVerboseLogging{false};

std::uint64_t MonotonicMilliseconds() noexcept {
    static const std::uint64_t frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value.QuadPart > 0 ? static_cast<std::uint64_t>(value.QuadPart) : 1ull;
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<std::uint64_t>((counter.QuadPart * 1000ull) / frequency);
}

std::wstring SanitizeField(std::wstring_view value) {
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t ch : value) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t' || ch == L'|' || ch == L'=') out.push_back(L'_');
        else if (ch < 0x20) out.push_back(L'_');
        else out.push_back(ch);
    }
    return out;
}

std::wstring LevelName(Level level) {
    switch (level) {
    case Level::Trace: return L"TRACE";
    case Level::Info: return L"INFO";
    case Level::Warning: return L"WARN";
    case Level::Error: return L"ERROR";
    case Level::Critical: return L"CRITICAL";
    }
    return L"INFO";
}

std::wstring TimestampForFile() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04u%02u%02u-%02u%02u%02u-%03u", now.wYear, now.wMonth,
        now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    return buffer;
}

constexpr std::size_t kRetainedCrashIncidents = 20;
constexpr std::uint64_t kMaximumSessionLogBytes = 16ull * 1024ull * 1024ull;

[[maybe_unused]] bool ProbeDirectoryWriteAccess(const fs::path& directory) noexcept {
    try {
        static std::atomic<std::uint64_t> sequence{ 0 };
        for (int attempt = 0; attempt < 16; ++attempt) {
            const fs::path probe = directory / (L".QuickSift-write-probe-" +
                std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed)) + L".tmp");
            HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                const DWORD error = GetLastError();
                if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) continue;
                return false;
            }
            const char marker = 'Q';
            DWORD written = 0;
            const bool writeOk = WriteFile(file, &marker, 1, &written, nullptr) != FALSE &&
                written == 1 && FlushFileBuffers(file) != FALSE;
            CloseHandle(file);
            const bool deleteOk = DeleteFileW(probe.c_str()) != FALSE;
            return writeOk && deleteOk;
        }
    } catch (...) {
    }
    return false;
}

void PruneCrashReports(const fs::path& directory) noexcept {
    try {
        struct Incident { std::wstring stem; fs::file_time_type modified{}; };
        std::unordered_map<std::wstring, fs::file_time_type> newestByStem;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(directory,
            fs::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
            const std::wstring stem = entry.path().stem().wstring();
            if (!stem.starts_with(L"QuickSift-crash-")) continue;
            const auto modified = entry.last_write_time(ec);
            const auto value = ec ? fs::file_time_type{} : modified;
            ec.clear();
            auto [it, inserted] = newestByStem.emplace(stem, value);
            if (!inserted && value > it->second) it->second = value;
        }
        std::vector<Incident> incidents;
        incidents.reserve(newestByStem.size());
        for (const auto& [stem, modified] : newestByStem) incidents.push_back({ stem, modified });
        std::sort(incidents.begin(), incidents.end(), [](const Incident& left, const Incident& right) {
            if (left.modified != right.modified) return left.modified > right.modified;
            return left.stem > right.stem;
        });
        for (std::size_t index = kRetainedCrashIncidents; index < incidents.size(); ++index) {
            ec.clear(); fs::remove(directory / (incidents[index].stem + L".log"), ec);
            ec.clear(); fs::remove(directory / (incidents[index].stem + L".dmp"), ec);
        }
    } catch (...) {
    }
}

std::wstring FormatEntry(const Entry& entry) {
    wchar_t timestamp[40]{};
    swprintf_s(timestamp, L"%04u-%02u-%02u %02u:%02u:%02u.%03u", entry.time.wYear,
        entry.time.wMonth, entry.time.wDay, entry.time.wHour, entry.time.wMinute,
        entry.time.wSecond, entry.time.wMilliseconds);
    std::wostringstream output;
    output << L'[' << timestamp << L"] [" << LevelName(entry.level) << L"] [S"
        << entry.sequence << L"] [P" << entry.processId << L"] [T" << entry.threadId
        << L"] [" << entry.category << L"]";
    if (!entry.event.empty()) output << L" event=" << SanitizeField(entry.event);
    if (!entry.message.empty()) output << L" msg=" << SanitizeField(entry.message);
    output << L" mono_ms=" << entry.monotonicMs;
    return output.str();
}

std::string Utf8FromWide(std::wstring_view text) {
    if (text.empty()) return {};
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return {};

    // Keep diagnostics usable when a damaged path or codec message contains an
    // unpaired UTF-16 surrogate. Valid pairs are preserved; invalid code units
    // are represented by the Unicode replacement character.
    std::wstring sanitized;
    sanitized.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        const wchar_t value = text[index];
        if (value >= 0xD800 && value <= 0xDBFF) {
            if (index + 1 < text.size() && text[index + 1] >= 0xDC00 && text[index + 1] <= 0xDFFF) {
                sanitized.push_back(value);
                sanitized.push_back(text[++index]);
            } else {
                sanitized.push_back(static_cast<wchar_t>(0xFFFD));
            }
        } else if (value >= 0xDC00 && value <= 0xDFFF) {
            sanitized.push_back(static_cast<wchar_t>(0xFFFD));
        } else {
            sanitized.push_back(value);
        }
    }
    if (sanitized.size() > static_cast<std::size_t>(INT_MAX)) return {};
    const int length = static_cast<int>(sanitized.size());
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        sanitized.data(), length, nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string output(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, sanitized.data(), length,
        output.data(), required, nullptr, nullptr) != required) return {};
    return output;
}

std::wstring SnapshotTextUnlocked(const State& state) {
    std::wstring output;
    output.reserve(state.entries.size() * 96);
    output += L"QuickSift ";
    output += QS_VERSION_WSTRING;
    output += L" diagnostic session\r\n";
    output += L"Session file: ";
    output += state.sessionPath.wstring();
    output += L"\r\nSession ID: " + std::to_wstring(state.sessionId) + L"\r\n";
    output += L"Entries retained: " + std::to_wstring(state.entries.size()) +
        L" / 12000\r\nDropped in-memory entries: " + std::to_wstring(state.droppedEntries) +
        L"\r\nDropped queued disk entries: " + std::to_wstring(state.diskQueueDropped) + L"\r\n";
    output += L"Level counts: trace=" + std::to_wstring(state.eventsByLevel[0]) +
        L" info=" + std::to_wstring(state.eventsByLevel[1]) +
        L" warn=" + std::to_wstring(state.eventsByLevel[2]) +
        L" error=" + std::to_wstring(state.eventsByLevel[3]) +
        L" critical=" + std::to_wstring(state.eventsByLevel[4]) + L"\r\n\r\n";
    for (const Entry& entry : state.entries) {
        output += FormatEntry(entry);
        output += L"\r\n";
    }
    return output;
}

bool WriteBytes(HANDLE file, const void* data, std::size_t bytes) noexcept {
    const auto* cursor = static_cast<const std::uint8_t*>(data);
    while (bytes != 0) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
            bytes, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (!WriteFile(file, cursor, request, &written, nullptr) || written != request) return false;
        cursor += written;
        bytes -= written;
    }
    return true;
}

bool WriteTextFile(const fs::path& path, std::wstring_view text) noexcept {
    try {
        const std::string utf8 = Utf8FromWide(text);
        if (!text.empty() && utf8.empty()) return false;

        static std::atomic<std::uint64_t> sequence{ 0 };
        fs::path temporary;
        HANDLE output = INVALID_HANDLE_VALUE;
        for (int attempt = 0; attempt < 64; ++attempt) {
            temporary = path;
            temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
                std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed)) + L".tmp";
            output = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
            if (output != INVALID_HANDLE_VALUE) break;
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) return false;
        }
        if (output == INVALID_HANDLE_VALUE) return false;

        struct TemporaryCleanup {
            fs::path path;
            HANDLE handle = INVALID_HANDLE_VALUE;
            ~TemporaryCleanup() {
                if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
                if (path.empty()) return;
                std::error_code error;
                fs::remove(path, error);
            }
        } cleanup{ temporary, output };

        static constexpr unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        if (!WriteBytes(output, bom, sizeof(bom)) ||
            !WriteBytes(output, utf8.data(), utf8.size()) ||
            !FlushFileBuffers(output)) {
            return false;
        }
        CloseHandle(output);
        cleanup.handle = INVALID_HANDLE_VALUE;

        if (!MoveFileExW(temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return false;
        cleanup.path.clear();
        return true;
    } catch (...) {
        return false;
    }
}

// streamMutex held. Writes `batch` in order; honours the 16 MiB session cap.
void WriteBatchStreamLocked(State& state, std::deque<std::string>& batch, bool flushStream) noexcept {
    try {
        if (!state.stream || state.diskLimitReached) { batch.clear(); return; }
        while (!batch.empty()) {
            const std::string& line = batch.front();
            if (state.diskBytes + line.size() > kMaximumSessionLogBytes) {
                static constexpr char marker[] =
                    "[QuickSift] On-disk session log reached the 16 MiB safety limit; additional entries remain in memory.\r\n";
                state.stream.write(marker, static_cast<std::streamsize>(sizeof(marker) - 1));
                state.diskLimitReached = true;
                state.streamWritable.store(false, std::memory_order_release);
                batch.clear();
                break;
            }
            state.stream.write(line.data(), static_cast<std::streamsize>(line.size()));
            if (!state.stream) {
                state.streamWritable.store(false, std::memory_order_release);
                batch.clear();
                break;
            }
            state.diskBytes += line.size();
            batch.pop_front();
        }
        if (flushStream && state.stream) state.stream.flush();
    } catch (...) {
    }
}

// Takes streamMutex, then briefly `mutex` to swap the queue out; writes outside `mutex`.
void DrainToDisk(State& state, bool flushStream) noexcept {
    try {
        std::lock_guard streamLock(state.streamMutex);
        QS_LOCK_ORDER(quicksift::core::lock_order::Rank::LogStream);
        std::deque<std::string> batch;
        {
            std::lock_guard lock(state.mutex);
            QS_LOCK_ORDER(quicksift::core::lock_order::Rank::LogState);
            batch.swap(state.diskQueue);
        }
        const bool flush = flushStream || state.flushRequested.exchange(false, std::memory_order_acq_rel);
        WriteBatchStreamLocked(state, batch, flush);
    } catch (...) {
    }
}

void DiskWriterMain() noexcept {
    State& state = GetState();
    try {
        for (;;) {
            bool stop = false;
            {
                std::unique_lock lock(state.mutex);
                state.diskCv.wait_for(lock, std::chrono::milliseconds(100), [&] {
                    return state.diskStop || !state.diskQueue.empty();
                });
                stop = state.diskStop;
            }
            DrainToDisk(state, false);
            if (stop) break;
        }
        DrainToDisk(state, true);
    } catch (...) {
    }
}

// Lock-free subsystem state for crash reports (atomics only; no locks, no I/O).
std::wstring SubsystemStateText() {
    namespace prio = quicksift::core::priority;
    const prio::DemandSnapshot demand = prio::Demand().Snapshot();
    std::wstring text;
    text += L"Priority demand: highest=" + std::wstring(prio::TierName(prio::HighestDemand(demand)));
    text += L" metadata_writes=" + std::to_wstring(demand.metadataWritesPending);
    text += L" interactive_write_queued=" + std::wstring(demand.interactiveWritePending ? L"1" : L"0");
    text += L" view_foreground=" + std::to_wstring(demand.viewForeground);
    text += L" search_active=" + std::wstring(demand.searchActive ? L"1" : L"0");
    text += L" search_focused=" + std::wstring(demand.searchFocused ? L"1" : L"0");
    text += L" demand_epoch=" + std::to_wstring(prio::Demand().Epoch()) + L"\r\n";
    return text;
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* exceptionInfo) noexcept {
    State& state = GetState();
    if (state.crashInProgress.test_and_set(std::memory_order_acq_rel))
        return EXCEPTION_EXECUTE_HANDLER;

    try {
        const std::wstring stamp = TimestampForFile();
        fs::path reportDirectory;
        fs::path sessionPath;
        {
            std::unique_lock lock(state.mutex, std::try_to_lock);
            if (lock.owns_lock()) {
                reportDirectory = state.crashDirectory;
                sessionPath = state.sessionPath;
            } else {
                const fs::path root = quicksift::platform::DataRootPath();
                if (!root.empty()) reportDirectory = root / L"CrashReports";
            }
        }
        if (reportDirectory.empty()) return EXCEPTION_EXECUTE_HANDLER;
        std::error_code ec;
        fs::create_directories(reportDirectory, ec);

        const std::wstring crashStem = L"QuickSift-crash-" + stamp + L"-p" +
            std::to_wstring(GetCurrentProcessId());
        const fs::path textPath = reportDirectory / (crashStem + L".log");
        std::wostringstream report;
        report << L"QuickSift " << QS_VERSION_WSTRING << L" crash report\r\n";
        report << L"Process ID: " << GetCurrentProcessId() << L"\r\n";
        report << L"Thread ID: " << GetCurrentThreadId() << L"\r\n";
        if (!sessionPath.empty()) report << L"Session log: " << sessionPath.wstring() << L"\r\n";
        if (exceptionInfo && exceptionInfo->ExceptionRecord) {
            report << L"Exception code: 0x" << std::hex << std::uppercase
                << exceptionInfo->ExceptionRecord->ExceptionCode << std::dec << L"\r\n";
            report << L"Exception address: " << exceptionInfo->ExceptionRecord->ExceptionAddress << L"\r\n";
        }
        report << L"\r\nRecent diagnostic log\r\n=====================\r\n";
        report << L"\r\nSubsystem state\r\n===============\r\n" << SubsystemStateText();
        report << L"\r\n";
        {
            // Same order as everywhere: streamMutex -> mutex; never block in a crash.
            std::unique_lock streamLock(state.streamMutex, std::try_to_lock);
            std::unique_lock lock(state.mutex, std::try_to_lock);
            if (lock.owns_lock()) {
                if (streamLock.owns_lock()) {
                    std::deque<std::string> batch;
                    batch.swap(state.diskQueue);
                    WriteBatchStreamLocked(state, batch, true);
                }
                report << SnapshotTextUnlocked(state);
            } else report << L"The in-memory log was unavailable because another thread held the log lock.\r\n"
                << L"Use the session-log path above for entries flushed before the crash.\r\n";
        }
        (void)WriteTextFile(textPath, report.str());
        (void)WriteTextFile(reportDirectory / L"LATEST_CRASH.txt", textPath.wstring());

        const fs::path dumpPath = reportDirectory / (crashStem + L".dmp");
        HANDLE dump = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (dump != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION info{};
            info.ThreadId = GetCurrentThreadId();
            info.ExceptionPointers = exceptionInfo;
            info.ClientPointers = FALSE;
            const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
                MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                MiniDumpWithUnloadedModules);
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, dumpType,
                exceptionInfo ? &info : nullptr, nullptr, nullptr);
            FlushFileBuffers(dump);
            CloseHandle(dump);
        }
        PruneCrashReports(reportDirectory);
    } catch (...) {
        // A crash reporter is best-effort. Never let reporting recursively throw.
    }

    if (exceptionInfo && state.previousFilter && state.previousFilter != CrashFilter)
        return state.previousFilter(exceptionInfo);
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

bool Initialize() noexcept {
    State& state = GetState();
    try {
        {
            std::lock_guard lock(state.mutex);
            if (state.initialized.load(std::memory_order_relaxed)) return true;
            state.entries.clear();
            state.sessionPath.clear();
            state.dataRoot.clear();
            state.crashDirectory.clear();
            state.diskStop = false;
            state.diskQueue.clear();
            state.diskQueueDropped = 0;
            state.diskBytes = 0;
            state.diskLimitReached = false;
            state.sequence.store(0, std::memory_order_relaxed);
            state.droppedEntries = 0;
            std::fill(std::begin(state.eventsByLevel), std::end(state.eventsByLevel), 0ull);
            state.lastSystemSnapshotMs = 0;
            if (state.stream.is_open()) state.stream.close();
            state.stream.clear();

            const fs::path root = quicksift::platform::DataRootPath();
            if (root.empty()) return false;
            const fs::path logDirectory = root / L"Logs";
            const fs::path crashDirectory = root / L"CrashReports";
            const fs::path cacheDirectory = quicksift::platform::CacheDirectoryPath();
            (void)cacheDirectory; // Created lazily by PersistentCache after the first frame.
            std::error_code ec;
            fs::create_directories(root, ec);
            if (ec || !fs::is_directory(root, ec) || ec) return false;
            ec.clear();
            fs::create_directories(logDirectory, ec);
            if (ec || !fs::is_directory(logDirectory, ec) || ec) return false;
            ec.clear();
            fs::create_directories(crashDirectory, ec);
            if (ec || !fs::is_directory(crashDirectory, ec) || ec) return false;
            // Startup must not perform forced write probes or cache-directory I/O before
            // the main window exists. A busy/contended disk must never make QuickSift
            // appear hung before first paint. Runtime writes remain best-effort and the
            // cache directory is initialized by PersistentCache after the UI is visible.
            const fs::path sessionPath = logDirectory / (L"QuickSift-" + TimestampForFile() + L"-" +
                std::to_wstring(GetCurrentProcessId()) + L".log");
            state.stream.open(sessionPath, std::ios::binary | std::ios::trunc);
            if (!state.stream) {
                state.stream.clear();
                return false;
            }
            static constexpr unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
            state.stream.write(reinterpret_cast<const char*>(bom), sizeof(bom));
            state.stream.flush();
            if (!state.stream) {
                state.stream.close();
                state.stream.clear();
                ec.clear(); fs::remove(sessionPath, ec);
                return false;
            }
            state.dataRoot = root;
            state.crashDirectory = crashDirectory;
            state.sessionPath = sessionPath;
            state.sessionId = (static_cast<std::uint64_t>(GetCurrentProcessId()) << 32) ^ MonotonicMilliseconds();
            state.diskBytes = sizeof(bom);
            PruneCrashReports(crashDirectory);
            state.streamWritable.store(true, std::memory_order_release);
            state.initialized.store(true, std::memory_order_release);
            state.diskThread = std::thread(DiskWriterMain);
        }
        Write(Level::Info, L"Lifecycle", L"Diagnostic logging initialized under " + DataRootPath().wstring());
        return true;
    } catch (...) {
        state.initialized.store(false, std::memory_order_release);
        try {
            state.diskStop = true;
            state.diskCv.notify_all();
            if (state.diskThread.joinable()) state.diskThread.join();
            std::lock_guard streamLock(state.streamMutex);
            state.streamWritable.store(false, std::memory_order_release);
            if (state.stream) state.stream.close();
        } catch (...) {}
        return false;
    }
}

void Shutdown() noexcept {
    State& state = GetState();
    try {
        if (!state.initialized.load(std::memory_order_acquire)) return;
        Write(Level::Info, L"Lifecycle", L"Diagnostic logging shutting down");
        {
            std::lock_guard lock(state.mutex);
            state.diskStop = true;
        }
        state.diskCv.notify_all();
        if (state.diskThread.joinable()) state.diskThread.join();
        DrainToDisk(state, true);
        {
            std::lock_guard streamLock(state.streamMutex);
            state.streamWritable.store(false, std::memory_order_release);
            if (state.stream) {
                state.stream.flush();
                state.stream.close();
            }
            state.initialized.store(false, std::memory_order_release);
        }
    } catch (...) {
        state.initialized.store(false, std::memory_order_release);
    }
}

void InstallCrashHandlers() noexcept {
    State& state = GetState();
    state.previousFilter = SetUnhandledExceptionFilter(CrashFilter);
    std::set_terminate([] {
        CrashFilter(nullptr);
        TerminateProcess(GetCurrentProcess(), 0xE0005153u);
    });
}

std::uint64_t SessionId() noexcept {
    return GetState().sessionId;
}

void SetLogLevel(Level level) noexcept { gLogLevel.store(level, std::memory_order_release); }
Level GetLogLevel() noexcept { return gLogLevel.load(std::memory_order_acquire); }
void SetVerboseLogging(bool enabled) noexcept {
    gVerboseLogging.store(enabled, std::memory_order_release);
    gLogLevel.store(enabled ? Level::Trace : Level::Info, std::memory_order_release);
}
bool VerboseLogging() noexcept { return gVerboseLogging.load(std::memory_order_acquire); }

void WriteEvent(Level level, std::wstring_view category, std::wstring_view event,
    std::initializer_list<Field> fields) noexcept {
    if (level < GetLogLevel()) return;
    try {
        State& state = GetState();
        if (!state.initialized.load(std::memory_order_acquire)) return;
        std::wstring message;
        bool first = true;
        for (const Field& field : fields) {
            if (field.key.empty()) continue;
            if (!first) message += L" ";
            first = false;
            message += std::wstring(field.key);
            message += L"=";
            message += SanitizeField(field.value);
        }
        Entry entry;
        GetLocalTime(&entry.time);
        entry.monotonicMs = MonotonicMilliseconds();
        entry.sequence = state.sequence.fetch_add(1, std::memory_order_relaxed) + 1;
        entry.processId = GetCurrentProcessId();
        entry.threadId = GetCurrentThreadId();
        entry.level = level;
        entry.category.assign(category);
        entry.event.assign(event);
        entry.message = std::move(message);
        const std::wstring line = FormatEntry(entry);
        const std::string utf8 = state.streamWritable.load(std::memory_order_acquire) ?
            Utf8FromWide(line + L"\r\n") : std::string();
        {
            std::lock_guard lock(state.mutex);
            QS_LOCK_ORDER(quicksift::core::lock_order::Rank::LogState);
            state.entries.push_back(std::move(entry));
            while (state.entries.size() > 12000) { state.entries.pop_front(); ++state.droppedEntries; }
            state.eventsByLevel[static_cast<int>(level)]++;
            if (!utf8.empty()) {
                if (state.diskQueue.size() >= 4096) {
                    state.diskQueue.pop_front();
                    ++state.diskQueueDropped;
                }
                state.diskQueue.push_back(utf8);
                // Warning/Error: the writer thread flushes the OS buffer on its next
                // pass (no disk I/O on the calling thread, which may be the UI).
                if (level >= Level::Warning) state.flushRequested.store(true, std::memory_order_release);
            }
        }
        if (!utf8.empty()) state.diskCv.notify_one();
        // Critical only: synchronous, so the line is on disk even if a crash follows.
        if (!utf8.empty() && level >= Level::Critical) DrainToDisk(state, true);
    } catch (...) {
    }
}

void WriteMetric(std::wstring_view category, std::wstring_view metric, double value,
    std::wstring_view unit) noexcept {
    try {
        std::wostringstream valueStream;
        valueStream << std::fixed << std::setprecision(3) << value;
        WriteEvent(Level::Trace, category, L"metric", {
            {L"name", std::wstring(metric)}, {L"value", valueStream.str()}, {L"unit", std::wstring(unit)}
        });
    } catch (...) {}
}

void WriteSystemSnapshot(std::wstring_view reason) noexcept {
    try {
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        GlobalMemoryStatusEx(&memory);
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
        SYSTEM_INFO system{};
        GetNativeSystemInfo(&system);
        WriteEvent(Level::Info, L"System", L"resource_snapshot", {
            {L"reason", std::wstring(reason)},
            {L"ram_total_mb", std::to_wstring(memory.ullTotalPhys / (1024ull*1024ull))},
            {L"ram_available_mb", std::to_wstring(memory.ullAvailPhys / (1024ull*1024ull))},
            {L"ram_load_pct", std::to_wstring(memory.dwMemoryLoad)},
            {L"process_working_set_mb", std::to_wstring(counters.WorkingSetSize / (1024ull*1024ull))},
            {L"process_private_mb", std::to_wstring(counters.PrivateUsage / (1024ull*1024ull))},
            {L"page_size", std::to_wstring(system.dwPageSize)},
            {L"processors", std::to_wstring(system.dwNumberOfProcessors)}
        });
    } catch (...) {}
}

void Write(Level level, std::wstring_view category, std::wstring_view message) noexcept {
    if (level < GetLogLevel()) return;
    try {
        State& state = GetState();
        if (!state.initialized.load(std::memory_order_acquire)) return;
        Entry entry;
        GetLocalTime(&entry.time);
        entry.monotonicMs = MonotonicMilliseconds();
        entry.sequence = state.sequence.fetch_add(1, std::memory_order_relaxed) + 1;
        entry.processId = GetCurrentProcessId();
        entry.threadId = GetCurrentThreadId();
        entry.level = level;
        entry.category.assign(category);
        entry.message.assign(message);
        const std::wstring line = FormatEntry(entry);
        const std::string utf8 = state.streamWritable.load(std::memory_order_acquire) ?
            Utf8FromWide(line + L"\r\n") : std::string();
        {
            std::lock_guard lock(state.mutex);
            QS_LOCK_ORDER(quicksift::core::lock_order::Rank::LogState);
            state.entries.push_back(std::move(entry));
            while (state.entries.size() > 12000) { state.entries.pop_front(); ++state.droppedEntries; }
            state.eventsByLevel[static_cast<int>(level)]++;
            if (!utf8.empty()) {
                if (state.diskQueue.size() >= 4096) {
                    state.diskQueue.pop_front();
                    ++state.diskQueueDropped;
                }
                state.diskQueue.push_back(utf8);
                // Warning/Error: the writer thread flushes the OS buffer on its next
                // pass (no disk I/O on the calling thread, which may be the UI).
                if (level >= Level::Warning) state.flushRequested.store(true, std::memory_order_release);
            }
        }
        if (!utf8.empty()) state.diskCv.notify_one();
        // Critical only: synchronous, so the line is on disk even if a crash follows.
        if (!utf8.empty() && level >= Level::Critical) DrainToDisk(state, true);
    } catch (...) {}
}

void WriteLastError(Level level, std::wstring_view category, std::wstring_view operation,
    DWORD error) noexcept {
    try {
        wchar_t systemMessage[512]{};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
            error, 0, systemMessage, static_cast<DWORD>(std::size(systemMessage)), nullptr);
        std::wstring message(operation);
        message += L" failed (Win32 ";
        message += std::to_wstring(error);
        message += L"): ";
        message += systemMessage;
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) message.pop_back();
        Write(level, category, message);
    } catch (...) {
        Write(level, category, L"A Win32 operation failed; the detailed error message could not be allocated");
    }
}

std::wstring SnapshotText() {
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    return SnapshotTextUnlocked(state);
}

fs::path DataRootPath() noexcept {
    State& state = GetState();
    try {
        std::lock_guard lock(state.mutex);
        if (!state.dataRoot.empty()) return state.dataRoot;
        return quicksift::platform::DataRootPath();
    } catch (...) {
        return {};
    }
}

fs::path SessionLogPath() {
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    return state.sessionPath;
}

fs::path CrashReportDirectory() {
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    return state.crashDirectory;
}

bool ExportSnapshot(const fs::path& destination) noexcept {
    try {
        if (destination.empty()) return false;
        std::error_code ec;
        if (!destination.parent_path().empty()) fs::create_directories(destination.parent_path(), ec);
        if (!WriteTextFile(destination, SnapshotText())) return false;
        ec.clear();
        if (!fs::is_regular_file(destination, ec) || ec) return false;
        ec.clear();
        return fs::file_size(destination, ec) > 3 && !ec;
    } catch (...) {
        return false;
    }
}

bool ExportSnapshotInteractive(HWND owner, fs::path* destination) noexcept {
    try {
        wchar_t fileName[MAX_PATH] = L"QuickSift-diagnostic-log.txt";
        OPENFILENAMEW dialog{ sizeof(dialog) };
        dialog.hwndOwner = owner;
        dialog.lpstrFilter = L"Text log (*.txt)\0*.txt\0All files (*.*)\0*.*\0\0";
        dialog.lpstrFile = fileName;
        dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
        dialog.lpstrDefExt = L"txt";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&dialog)) return false;
        const fs::path path(fileName);
        if (destination) *destination = path;
        return ExportSnapshot(path);
    } catch (...) {
        return false;
    }
}

namespace {
// FILETIME (100 ns since 1601-01-01 UTC) to system_clock (Unix epoch on MSVC).
std::chrono::system_clock::time_point FromFileTime(const FILETIME& time) noexcept {
    ULARGE_INTEGER ticks{};
    ticks.LowPart = time.dwLowDateTime;
    ticks.HighPart = time.dwHighDateTime;
    constexpr std::uint64_t kUnixEpochTicks = 116444736000000000ull;
    if (ticks.QuadPart < kUnixEpochTicks) return {};
    return std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::microseconds((ticks.QuadPart - kUnixEpochTicks) / 10)));
}

// Non-recursive listing of QuickSift-* entries in one of our own folders. The
// folder itself is skipped when it is a reparse point (never followed).
bool ListRetentionCandidates(const fs::path& folder, std::vector<quicksift::core::log_retention::Entry>& entries,
    std::wstring& skipReason) {
    const DWORD folderAttributes = GetFileAttributesW(folder.c_str());
    if (folderAttributes == INVALID_FILE_ATTRIBUTES) { skipReason = L"missing"; return false; }
    if ((folderAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) { skipReason = L"not_a_folder"; return false; }
    if ((folderAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { skipReason = L"reparse_point"; return false; }
    WIN32_FIND_DATAW data{};
    const std::wstring pattern = (folder / L"QuickSift-*").wstring();
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
        FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) return true; // nothing matches
    do {
        quicksift::core::log_retention::Entry entry;
        entry.name = data.cFileName;
        entry.lastWrite = FromFileTime(data.ftLastWriteTime);
        entry.reparsePoint = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        entry.regularFile = (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)) == 0;
        entries.push_back(std::move(entry));
    } while (FindNextFileW(find, &data) != FALSE);
    FindClose(find);
    return true;
}

enum class DeleteOutcome { Deleted, Skipped, Failed };

// Deletes one entry the policy selected, through a handle opened on the entry
// itself (FILE_FLAG_OPEN_REPARSE_POINT: a link is never followed). The handle is
// re-checked before deleting: still a plain file, still not a reparse point,
// still older than the retention age. A file another process has open without
// delete sharing (a live session log) fails to open and is left alone.
DeleteOutcome DeleteOwnDiagnosticFile(const fs::path& target, std::chrono::system_clock::time_point now,
    DWORD& error) noexcept {
    HANDLE file = CreateFileW(target.c_str(), DELETE | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND ? DeleteOutcome::Skipped : DeleteOutcome::Failed;
    }
    DeleteOutcome outcome = DeleteOutcome::Skipped;
    FILE_ATTRIBUTE_TAG_INFO tag{};
    FILE_BASIC_INFO basic{};
    BY_HANDLE_FILE_INFORMATION identity{};
    if (GetFileInformationByHandleEx(file, FileAttributeTagInfo, &tag, sizeof(tag)) != FALSE &&
        GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof(basic)) != FALSE &&
        GetFileInformationByHandle(file, &identity) != FALSE &&
        (tag.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) == 0 &&
        identity.nNumberOfLinks == 1) {
        FILETIME written{};
        written.dwLowDateTime = static_cast<DWORD>(basic.LastWriteTime.QuadPart & 0xFFFFFFFFull);
        written.dwHighDateTime = static_cast<DWORD>(static_cast<std::uint64_t>(basic.LastWriteTime.QuadPart) >> 32);
        const auto lastWrite = FromFileTime(written);
        if (lastWrite <= now && now - lastWrite > quicksift::core::log_retention::kMaxAge) {
            FILE_DISPOSITION_INFO disposition{};
            disposition.DeleteFile = TRUE;
            if (SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE) {
                outcome = DeleteOutcome::Deleted;
            } else {
                error = GetLastError();
                outcome = DeleteOutcome::Failed;
            }
        }
    }
    CloseHandle(file);
    return outcome;
}
} // namespace

CleanupSummary CleanupOldDiagnosticFiles(const std::function<bool()>& cancelled) noexcept {
    namespace lr = quicksift::core::log_retention;
    CleanupSummary summary;
    try {
        const auto started = std::chrono::steady_clock::now();
        const fs::path root = DataRootPath();
        if (root.empty()) return summary;
        fs::path crashFolder = CrashReportDirectory();
        if (crashFolder.empty()) crashFolder = root / L"CrashReports";
        const std::wstring currentSession = SessionLogPath().filename().wstring();
        const auto now = std::chrono::system_clock::now();
        const std::pair<lr::Folder, fs::path> folders[] = {
            { lr::Folder::Logs, root / L"Logs" }, { lr::Folder::CrashReports, crashFolder } };
        for (const auto& [folder, path] : folders) {
            std::vector<lr::Entry> entries;
            std::wstring skipReason;
            if (!ListRetentionCandidates(path, entries, skipReason)) {
                WriteEvent(Level::Info, L"Diagnostics", L"cleanup_folder_skipped",
                    { { L"folder", folder == lr::Folder::Logs ? L"Logs" : L"CrashReports" }, { L"reason", skipReason } });
                continue;
            }
            summary.scanned += entries.size();
            for (const std::wstring& name : lr::Plan(folder, entries, now, currentSession)) {
                if (cancelled && cancelled()) { summary.cancelled = true; break; }
                const fs::path target = path / name;
                const lr::Kind kind = lr::Classify(name);
                DWORD error = ERROR_SUCCESS;
                const DeleteOutcome outcome = DeleteOwnDiagnosticFile(target, now, error);
                if (outcome == DeleteOutcome::Deleted) {
                    ++summary.deleted;
                    WriteEvent(Level::Info, L"Diagnostics", L"old_file_deleted",
                        { { L"kind", std::wstring(lr::KindName(kind)) }, { L"file", name } });
                } else if (outcome == DeleteOutcome::Failed) {
                    ++summary.failed;
                    WriteEvent(Level::Warning, L"Diagnostics", L"old_file_delete_failed",
                        { { L"kind", std::wstring(lr::KindName(kind)) }, { L"file", name },
                          { L"win32", std::to_wstring(error) } });
                } else {
                    WriteEvent(Level::Info, L"Diagnostics", L"old_file_skipped",
                        { { L"kind", std::wstring(lr::KindName(kind)) }, { L"file", name },
                          { L"reason", L"changed_or_link" } });
                }
            }
            if (summary.cancelled) break;
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        WriteEvent(Level::Info, L"Diagnostics", L"cleanup_complete",
            { { L"deleted", std::to_wstring(summary.deleted) }, { L"failed", std::to_wstring(summary.failed) },
              { L"scanned", std::to_wstring(summary.scanned) }, { L"cancelled", summary.cancelled ? L"1" : L"0" },
              { L"max_age_days", std::to_wstring(lr::kMaxAge.count() / 24) }, { L"ms", std::to_wstring(ms) } });
    } catch (...) {
        // Best effort: cleanup never affects the session.
    }
    return summary;
}

fs::path SessionLogDirectory() {
    const fs::path root = DataRootPath();
    return root.empty() ? fs::path{} : root / L"Logs";
}

namespace {
// The Logs folder, or empty when it is missing, not a folder, or a reparse point
// (never followed, same rule as the cleanup).
fs::path BrowsableLogsFolder() {
    const fs::path folder = SessionLogDirectory();
    if (folder.empty()) return {};
    const DWORD attributes = GetFileAttributesW(folder.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return {};
    return folder;
}

struct ScopedFileHandle {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ScopedFileHandle() = default;
    explicit ScopedFileHandle(HANDLE value) noexcept : handle(value) {}
    ScopedFileHandle(const ScopedFileHandle&) = delete;
    ScopedFileHandle& operator=(const ScopedFileHandle&) = delete;
    ~ScopedFileHandle() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
} // namespace

std::vector<quicksift::core::session_logs::Listed> ListPreviousSessionLogs() noexcept {
    namespace sl = quicksift::core::session_logs;
    try {
        const fs::path folder = BrowsableLogsFolder();
        if (folder.empty()) return {};
        std::vector<sl::Candidate> candidates;
        WIN32_FIND_DATAW data{};
        const std::wstring pattern = (folder / L"QuickSift-*").wstring();
        HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
            FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) return {};
        struct FindCloser {
            HANDLE handle;
            ~FindCloser() { FindClose(handle); }
        } closer{ find };
        do {
            sl::Candidate candidate;
            candidate.name = data.cFileName;
            candidate.bytes = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            candidate.reparsePoint = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            candidate.regularFile = (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)) == 0;
            candidates.push_back(std::move(candidate));
        } while (FindNextFileW(find, &data) != FALSE);
        return sl::Select(candidates, SessionLogPath().filename().wstring());
    } catch (...) {
        // Best effort: an empty list shows "No previous session logs".
        return {};
    }
}

SessionLogText ReadSessionLogTail(std::wstring_view name) noexcept {
    namespace sl = quicksift::core::session_logs;
    SessionLogText result;
    try {
        if (!sl::IsBrowsableName(name)) { result.error = ERROR_INVALID_NAME; return result; }
        const fs::path folder = BrowsableLogsFolder();
        if (folder.empty()) { result.error = ERROR_PATH_NOT_FOUND; return result; }
        // Read-only access with full sharing: the owning session may still be
        // writing (a second QuickSift window), and nothing here may block it.
        ScopedFileHandle file(CreateFileW((folder / std::wstring(name)).c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (file.handle == INVALID_HANDLE_VALUE) { result.error = GetLastError(); return result; }
        FILE_ATTRIBUTE_TAG_INFO tag{};
        LARGE_INTEGER size{};
        if (GetFileInformationByHandleEx(file.handle, FileAttributeTagInfo, &tag, sizeof(tag)) == FALSE ||
            (tag.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) != 0 ||
            GetFileSizeEx(file.handle, &size) == FALSE || size.QuadPart < 0) {
            result.error = ERROR_CANT_ACCESS_FILE;
            return result;
        }
        const sl::TailPlan plan = sl::PlanTail(static_cast<std::uint64_t>(size.QuadPart));
        LARGE_INTEGER offset{};
        offset.QuadPart = static_cast<LONGLONG>(plan.offset);
        if (SetFilePointerEx(file.handle, offset, nullptr, FILE_BEGIN) == FALSE) {
            result.error = GetLastError();
            return result;
        }
        std::string bytes(static_cast<std::size_t>(plan.length), '\0');
        std::size_t filled = 0;
        while (filled < bytes.size()) {
            DWORD read = 0;
            const DWORD request = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - filled, 1u << 20));
            if (ReadFile(file.handle, bytes.data() + filled, request, &read, nullptr) == FALSE) {
                result.error = GetLastError();
                return result;
            }
            if (read == 0) break; // the file shrank meanwhile: show what was read
            filled += read;
        }
        bytes.resize(filled);
        const std::size_t first = std::min(bytes.size(), sl::FirstShownByte(bytes, plan.truncated));
        const std::string_view shown(bytes.data() + first, bytes.size() - first);
        if (!shown.empty()) {
            // Invalid UTF-8 (a log cut off by a crash) becomes U+FFFD instead of failing.
            const int length = MultiByteToWideChar(CP_UTF8, 0, shown.data(), static_cast<int>(shown.size()), nullptr, 0);
            if (length > 0) {
                result.text.resize(static_cast<std::size_t>(length));
                MultiByteToWideChar(CP_UTF8, 0, shown.data(), static_cast<int>(shown.size()),
                    result.text.data(), length);
            }
        }
        result.ok = true;
        result.totalBytes = static_cast<std::uint64_t>(size.QuadPart);
        result.shownBytes = shown.size();
        result.truncated = plan.truncated;
    } catch (...) {
        // Out of memory or similar: report "could not read" rather than throw.
        result.ok = false;
        result.text.clear();
        if (result.error == ERROR_SUCCESS) result.error = ERROR_NOT_ENOUGH_MEMORY;
    }
    return result;
}

} // namespace quicksift::diagnostics

