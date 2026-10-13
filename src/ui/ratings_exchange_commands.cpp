// OWNER: Import / Export Ratings commands: in-canvas chooser, Windows file dialogs,
// the ratings worker thread (metadata disk reads, file read/write), the import
// confirmation chain, and the apply step through the metadata transaction pipeline.
// The exchange format itself (writer, strict parser, reconciliation) is portable and
// lives in core/ratings_exchange (checked by scripts/checks/ratings_exchange_checks.cpp).
#include "app/quicksift_application_internal.h"
#include "core/priority_policy.h"
#include "diagnostics/log_categories.h"
#include "version.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace quicksift::app {
namespace {

namespace rx = quicksift::core::ratings_exchange;

// An exported file for even a very large folder is a few MiB; anything far larger
// is not a QuickSift ratings file and is refused before it is read into memory.
constexpr std::uint64_t kMaximumImportBytes = 64ull * 1024ull * 1024ull;

rx::ExportTime LocalExportTime() {
    SYSTEMTIME local{};
    GetLocalTime(&local);
    rx::ExportTime time;
    time.year = static_cast<int>(local.wYear);
    time.month = static_cast<int>(local.wMonth);
    time.day = static_cast<int>(local.wDay);
    time.hour = static_cast<int>(local.wHour);
    time.minute = static_cast<int>(local.wMinute);
    time.second = static_cast<int>(local.wSecond);
    return time;
}

std::wstring WindowsErrorText(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring text = length && buffer ? std::wstring(buffer, length) : std::wstring();
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) text.pop_back();
    return text + L" (" + std::to_wstring(error) + L")";
}

// Writes next to the target and renames over it, so an interrupted export never
// leaves a truncated file under the chosen name.
bool WriteBytesAtomically(const fs::path& target, const std::string& bytes, std::wstring& error) {
    fs::path temporary = target;
    temporary += L".quicksift-partial";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = WindowsErrorText(GetLastError());
        return false;
    }
    bool ok = true;
    std::size_t offset = 0;
    while (ok && offset < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1u << 20));
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr) || written == 0) {
            error = WindowsErrorText(GetLastError());
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok && !FlushFileBuffers(file)) {
        error = WindowsErrorText(GetLastError());
        ok = false;
    }
    CloseHandle(file);
    if (ok && !MoveFileExW(temporary.c_str(), target.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = WindowsErrorText(GetLastError());
        ok = false;
    }
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
}

bool ReadBytes(const fs::path& path, std::string& bytes, std::wstring& error, bool& tooLarge) {
    tooLarge = false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = WindowsErrorText(GetLastError());
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
        error = WindowsErrorText(GetLastError());
        CloseHandle(file);
        return false;
    }
    if (static_cast<std::uint64_t>(size.QuadPart) > kMaximumImportBytes) {
        tooLarge = true;
        CloseHandle(file);
        return false;
    }
    bytes.assign(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1u << 20));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + offset, chunk, &read, nullptr)) {
            error = WindowsErrorText(GetLastError());
            CloseHandle(file);
            return false;
        }
        if (read == 0) break; // file shrank while reading
        offset += read;
    }
    bytes.resize(offset);
    CloseHandle(file);
    return true;
}

// IFileSaveDialog / IFileOpenDialog for *.txt, starting in the current folder.
std::optional<fs::path> ShowTextFileDialog(HWND owner, bool save, const std::wstring& title,
    const std::wstring& defaultName, const fs::path& folder, const std::wstring& filterLabel) {
    ComPtr<IFileDialog> dialog;
    if (save) {
        ComPtr<IFileSaveDialog> saveDialog;
        if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&saveDialog)))) return std::nullopt;
        dialog = saveDialog;
    } else {
        ComPtr<IFileOpenDialog> openDialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&openDialog)))) return std::nullopt;
        dialog = openDialog;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    options |= save ? (FOS_OVERWRITEPROMPT | FOS_NOREADONLYRETURN) : FOS_FILEMUSTEXIST;
    dialog->SetOptions(options);
    const COMDLG_FILTERSPEC filters[] = { { filterLabel.c_str(), L"*.txt" } };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetFileTypeIndex(1);
    dialog->SetDefaultExtension(L"txt");
    dialog->SetTitle(title.c_str());
    if (save && !defaultName.empty()) dialog->SetFileName(defaultName.c_str());
    if (!folder.empty()) {
        ComPtr<IShellItem> folderItem;
        if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&folderItem))))
            dialog->SetFolder(folderItem.Get());
    }
    if (FAILED(dialog->Show(owner))) return std::nullopt; // includes user cancel
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return std::nullopt;
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) return std::nullopt;
    fs::path result(path);
    CoTaskMemFree(path);
    return result;
}

int AuthoritativeOrZero(const MetadataFieldValue& field) {
    // Known values export as-is. Unsupported (unrepresentable on-disk value),
    // Unreadable and Unknown export as the neutral token (0 / None).
    return field.IsKnown() ? field.value : 0;
}

std::wstring ReplaceToken(std::wstring text, std::wstring_view token, const std::wstring& value) {
    const std::size_t position = text.find(token);
    if (position != std::wstring::npos) text.replace(position, token.size(), value);
    return text;
}

} // namespace

// Invariant-locale upper-case fold: mirrors NTFS case-insensitive name matching
// (including non-ASCII letters) for FILENAME.EXTENSION reconciliation.
std::wstring WindowsNameFold(std::wstring_view name) {
    if (name.empty()) return {};
    const int length = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, name.data(),
        static_cast<int>(name.size()), nullptr, 0, nullptr, nullptr, 0);
    if (length <= 0) return quicksift::core::ratings_exchange::AsciiFold(name);
    std::wstring folded(static_cast<std::size_t>(length), L'\0');
    const int written = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, name.data(),
        static_cast<int>(name.size()), folded.data(), length, nullptr, nullptr, 0);
    if (written <= 0) return quicksift::core::ratings_exchange::AsciiFold(name);
    folded.resize(static_cast<std::size_t>(written));
    return folded;
}

bool QuickSiftApplicationImpl::RatingsWorkerBusy() const noexcept {
    return ratingsWorkerRunning_.load(std::memory_order_acquire);
}

void QuickSiftApplicationImpl::StopRatingsWorker() noexcept {
    ratingsWorkerCancel_.store(true, std::memory_order_release);
    try {
        if (ratingsWorker_.joinable()) ratingsWorker_.join();
    } catch (...) {
        // Shutdown boundary: never throw.
    }
    ratingsWorkerRunning_.store(false, std::memory_order_release);
}

void QuickSiftApplicationImpl::ShowRatingsExchangeChooser() {
    ShowGlassChoice(Tr(L"Import / Export Ratings"),
        Tr(L"Export saves the star rating, pick state and color tag of every file in this folder that has any of them to a UTF-8 text file. Import reads such a file and applies it to the files with the same names in this folder."),
        Tr(L"Export…"), PendingGlassAction::RatingsExport, Tr(L"Cancel"),
        GlassAlertKind::Information, Tr(L"Import…"), PendingGlassAction::RatingsImport, false);
}

void QuickSiftApplicationImpl::BeginRatingsExport() {
    if (currentFolder_.empty()) {
        ShowGlassAlert(L"Export ratings", L"Open a folder first.", GlassAlertKind::Information);
        return;
    }
    if (!scanComplete_) {
        ShowGlassAlert(L"Export ratings", L"QuickSift is still listing this folder. Try again when the folder has finished loading.",
            GlassAlertKind::Information);
        return;
    }
    if (transactions_.Busy() || RatingsWorkerBusy() || ratingsImportToken_ != 0) {
        ShowGlassAlert(L"Export ratings",
            L"Wait for the current file, metadata or ratings operation to finish, then try again.",
            GlassAlertKind::Warning);
        return;
    }
    const rx::ExportTime exportTime = LocalExportTime();
    const auto target = ShowTextFileDialog(hwnd_, true, Tr(L"Export ratings"),
        rx::DefaultExportFileName(exportTime), currentFolder_, Tr(L"Text files (*.txt)"));
    if (!target) return;

    // Snapshot after the modal dialog. The catalog (the authoritative metadata store,
    // core/image_metadata.h) is used whenever it holds Known/Unsupported values; only
    // fields the store has not read yet are read from disk on the worker.
    struct ExportInput {
        fs::path path;
        std::wstring fileName;
        PhotoMetadataValues values;
        bool needsDisk = false;
    };
    std::vector<ExportInput> inputs;
    inputs.reserve(catalog_.PhotoCount());
    for (std::size_t index = 0; index < catalog_.PhotoCount(); ++index) {
        const PhotoItem& photo = catalog_.PhotoAt(index);
        ExportInput input;
        input.path = photo.path;
        input.fileName = photo.path.filename().wstring();
        input.values = {
            { photo.ratingKnowledge, photo.rating, photo.rawRating },
            { photo.colorLabelKnowledge, photo.colorLabel, photo.rawColorLabel },
            { photo.pickStateKnowledge, photo.pickState, photo.rawPickState } };
        input.needsDisk = !input.values.rating.IsAuthoritative() ||
            !input.values.colorLabel.IsAuthoritative() || !input.values.pickState.IsAuthoritative();
        inputs.push_back(std::move(input));
    }

    StopRatingsWorker(); // joins a finished previous worker (never a running one: checked above)
    ratingsWorkerCancel_.store(false, std::memory_order_release);
    ratingsWorkerDone_.store(0, std::memory_order_release);
    ratingsWorkerTotal_.store(inputs.size(), std::memory_order_release);
    ratingsWorkerRunning_.store(true, std::memory_order_release);
    {
        std::lock_guard lock(ratingsWorkerMutex_);
        ratingsWorkerResult_.reset();
    }
    const HWND window = hwnd_;
    const std::uint64_t generation = generation_;
    const fs::path file = *target;
    try {
        ratingsWorker_ = std::thread([this, window, generation, file, exportTime,
            inputs = std::move(inputs)]() mutable noexcept {
            RatingsWorkerResult result;
            result.import = false;
            result.file = file;
            result.generation = generation;
            // P5 background reads: below-normal CPU priority; yields to writes / view.
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            namespace prio = quicksift::core::priority;
            try {
                const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                rx::ExportDocument document;
                document.version = QS_VERSION_FILE_STRING;
                document.time = exportTime;
                document.totalFiles = inputs.size();
                bool cancelled = false;
                for (std::size_t index = 0; index < inputs.size(); ++index) {
                    if (ratingsWorkerCancel_.load(std::memory_order_acquire)) {
                        cancelled = true;
                        break;
                    }
                    ExportInput& input = inputs[index];
                    if (input.needsDisk) {
                        // Bounded cooperative yield (never starves: <= 1 s per file).
                        for (int wait = 0; wait < 100 &&
                            prio::ShouldYield(prio::Tier::Background, prio::Demand().Snapshot()) &&
                            !ratingsWorkerCancel_.load(std::memory_order_acquire); ++wait)
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        const PhotoMetadataValues disk = ReadPhotoMetadata(input.path);
                        if (!input.values.rating.IsAuthoritative()) input.values.rating = disk.rating;
                        if (!input.values.colorLabel.IsAuthoritative()) input.values.colorLabel = disk.colorLabel;
                        if (!input.values.pickState.IsAuthoritative()) input.values.pickState = disk.pickState;
                    }
                    rx::RatingsEntry entry;
                    entry.fileName = input.fileName;
                    namespace vocab = quicksift::core::vocabulary;
                    entry.starRating = std::clamp(AuthoritativeOrZero(input.values.rating),
                        vocab::kMinStars, vocab::kMaxStars);
                    entry.pickState = vocab::PickSign(AuthoritativeOrZero(input.values.pickState));
                    entry.colorLabel = std::clamp(AuthoritativeOrZero(input.values.colorLabel),
                        vocab::kColorNone, vocab::kMaxColor);
                    if (rx::HasData(entry)) document.entries.push_back(std::move(entry));
                    ratingsWorkerDone_.store(index + 1, std::memory_order_release);
                }
                if (SUCCEEDED(com)) CoUninitialize();
                if (cancelled) {
                    result.error = L"cancelled";
                } else {
                    result.totalFiles = document.totalFiles;
                    result.exportedEntries = document.entries.size();
                    std::wstring error;
                    if (WriteBytesAtomically(file, rx::WriteDocumentUtf8(document), error)) result.ok = true;
                    else result.error = error;
                }
            } catch (...) {
                result.ok = false;
                if (result.error.empty()) result.error = L"Unexpected error while exporting.";
            }
            try {
                std::lock_guard lock(ratingsWorkerMutex_);
                ratingsWorkerResult_ = std::move(result);
            } catch (...) {
            }
            ratingsWorkerRunning_.store(false, std::memory_order_release);
            // A lost post is recovered by OnTimer (worker joinable && not busy).
            static_cast<void>(PostMessageW(window, WM_APP_RATINGS_WORKER_DONE, 0, 0));
        });
    } catch (...) {
        ratingsWorkerRunning_.store(false, std::memory_order_release);
        ShowGlassAlert(L"Export ratings", L"QuickSift could not start the export.", GlassAlertKind::Error);
        return;
    }
    StartUiTimer(ID_TIMER_RATINGS_PROGRESS, 250, L"ratings progress");
    UpdateStatus();
    ShowToast(Tr(L"Exporting ratings…"));
}

void QuickSiftApplicationImpl::BeginRatingsImport() {
    if (currentFolder_.empty()) {
        ShowGlassAlert(L"Import ratings", L"Open a folder first.", GlassAlertKind::Information);
        return;
    }
    if (!scanComplete_) {
        ShowGlassAlert(L"Import ratings", L"QuickSift is still listing this folder. Try again when the folder has finished loading.",
            GlassAlertKind::Information);
        return;
    }
    if (transactions_.Busy() || RatingsWorkerBusy() || ratingsImportToken_ != 0) {
        ShowGlassAlert(L"Import ratings",
            L"Wait for the current file, metadata or ratings operation to finish, then try again.",
            GlassAlertKind::Warning);
        return;
    }
    CancelRatingsImport();
    const auto source = ShowTextFileDialog(hwnd_, false, Tr(L"Import ratings"), {},
        currentFolder_, Tr(L"Text files (*.txt)"));
    if (!source) return;

    StopRatingsWorker();
    ratingsWorkerCancel_.store(false, std::memory_order_release);
    ratingsWorkerDone_.store(0, std::memory_order_release);
    ratingsWorkerTotal_.store(0, std::memory_order_release);
    ratingsWorkerRunning_.store(true, std::memory_order_release);
    {
        std::lock_guard lock(ratingsWorkerMutex_);
        ratingsWorkerResult_.reset();
    }
    const HWND window = hwnd_;
    const std::uint64_t generation = generation_;
    const fs::path file = *source;
    try {
        ratingsWorker_ = std::thread([this, window, generation, file]() noexcept {
            RatingsWorkerResult result;
            result.import = true;
            result.file = file;
            result.generation = generation;
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            try {
                std::string bytes;
                std::wstring error;
                bool tooLarge = false;
                if (!ReadBytes(file, bytes, error, tooLarge)) {
                    result.error = tooLarge ? std::wstring(L"too-large") : error;
                } else {
                    result.parse = rx::ParseDocumentUtf8(bytes);
                    result.ok = true; // read succeeded; parse.ok carries validation
                }
            } catch (...) {
                result.ok = false;
                if (result.error.empty()) result.error = L"Unexpected error while reading the file.";
            }
            try {
                std::lock_guard lock(ratingsWorkerMutex_);
                ratingsWorkerResult_ = std::move(result);
            } catch (...) {
            }
            ratingsWorkerRunning_.store(false, std::memory_order_release);
            // A lost post is recovered by OnTimer (worker joinable && not busy).
            static_cast<void>(PostMessageW(window, WM_APP_RATINGS_WORKER_DONE, 0, 0));
        });
    } catch (...) {
        ratingsWorkerRunning_.store(false, std::memory_order_release);
        ShowGlassAlert(L"Import ratings", L"QuickSift could not start reading the file.", GlassAlertKind::Error);
        return;
    }
    StartUiTimer(ID_TIMER_RATINGS_PROGRESS, 250, L"ratings progress");
    UpdateStatus();
}

void QuickSiftApplicationImpl::OnRatingsWorkerCompleted() {
    std::optional<RatingsWorkerResult> taken;
    {
        std::lock_guard lock(ratingsWorkerMutex_);
        taken = std::move(ratingsWorkerResult_);
        ratingsWorkerResult_.reset();
    }
    if (ratingsWorker_.joinable() && !RatingsWorkerBusy()) {
        try { ratingsWorker_.join(); } catch (...) {}
    }
    if (ratingsImportToken_ == 0) KillTimer(hwnd_, ID_TIMER_RATINGS_PROGRESS);
    UpdateStatus();
    if (!taken) return;
    RatingsWorkerResult& result = *taken;
    const std::wstring fileName = result.file.filename().wstring();
    QS_LOG_EVENT(result.ok ? quicksift::diagnostics::Level::Info : quicksift::diagnostics::Level::Warning,
        quicksift::diagnostics::category::kRatingsIo, result.import ? L"import_read_finished" : L"export_finished",
        { L"ok", result.ok ? L"1" : L"0" },
        { L"error_code", result.ok ? L"" : (result.import ? L"ratings.import_failed" : L"ratings.export_failed") },
        { L"file", quicksift::diagnostics::LogFileName(result.file) },
        { L"path_id", quicksift::diagnostics::LogPathId(result.file) },
        { L"total_files", std::to_wstring(result.totalFiles) },
        { L"exported_entries", std::to_wstring(result.exportedEntries) });

    if (!result.import) {
        if (result.ok) {
            std::wstring text = Tr(L"Ratings exported: {count} of {total} files carry data — {file}");
            text = ReplaceToken(text, L"{count}", std::to_wstring(result.exportedEntries));
            text = ReplaceToken(text, L"{total}", std::to_wstring(result.totalFiles));
            text = ReplaceToken(text, L"{file}", fileName);
            ShowToast(text);
        } else if (result.error != L"cancelled") {
            ShowGlassAlertText(Tr(L"Export failed"),
                Tr(L"QuickSift could not write the ratings file. Nothing was exported.") +
                    L"\n\n" + fileName + L"\n" + result.error,
                GlassAlertKind::Error);
        }
        return;
    }

    // ---- Import: validation result ----
    if (result.generation != generation_ || currentFolder_.empty()) {
        ShowToast(Tr(L"Ratings import cancelled because the folder changed."));
        return;
    }
    if (!result.ok) {
        const std::wstring detail = result.error == L"too-large" ?
            Tr(L"The file is far too large to be a QuickSift ratings file.") : result.error;
        ShowGlassAlertText(Tr(L"Import failed"),
            Tr(L"QuickSift could not read the ratings file. Nothing was changed.") +
                L"\n\n" + fileName + L"\n" + detail,
            GlassAlertKind::Error);
        return;
    }
    if (!result.parse.ok) {
        std::wstring reason;
        switch (result.parse.error) {
        case rx::ParseError::Empty: reason = Tr(L"The file is empty."); break;
        case rx::ParseError::InvalidUtf8: reason = Tr(L"The file is not valid UTF-8 text."); break;
        case rx::ParseError::BadHeader: reason = Tr(L"The header line is missing or is not a QuickSift header."); break;
        case rx::ParseError::BadExportTime: reason = Tr(L"The \"Exported time\" line is missing or invalid."); break;
        case rx::ParseError::BadTotalCount: reason = Tr(L"The \"Total files count\" line is missing or invalid."); break;
        case rx::ParseError::BadDataCount: reason = Tr(L"The rated/picked/tagged files count line is missing or invalid."); break;
        case rx::ParseError::MissingFirstSeparator: reason = Tr(L"The first separator line (—) is missing."); break;
        case rx::ParseError::MissingDataLabel: reason = Tr(L"The \"Exported data:\" line is missing."); break;
        case rx::ParseError::BadDataLine: reason = Tr(L"A data line is not \"FILENAME.EXTENSION | Star Rating | Pick State | Color Tag\" with valid values."); break;
        case rx::ParseError::DuplicateEntry: reason = Tr(L"The same file name is listed more than once."); break;
        case rx::ParseError::MissingSecondSeparator: reason = Tr(L"The closing separator line (—) is missing."); break;
        case rx::ParseError::MissingFooter: reason = Tr(L"The footer line is missing."); break;
        case rx::ParseError::TrailingContent: reason = Tr(L"There is unexpected text after the footer."); break;
        case rx::ParseError::DataCountMismatch: reason = Tr(L"The number of data lines does not match the count in the header."); break;
        default: reason = Tr(L"The file is not a valid QuickSift ratings export."); break;
        }
        std::wstring message = Tr(L"This file is not a valid QuickSift ratings export, so nothing was imported.") +
            L"\n\n" + reason;
        if (result.parse.lineNumber != 0) {
            message += L"\n" + ReplaceToken(Tr(L"Line {line}:"), L"{line}",
                std::to_wstring(result.parse.lineNumber));
            if (!result.parse.offendingLine.empty()) {
                std::wstring line = result.parse.offendingLine;
                if (line.size() > 160) line = line.substr(0, 157) + L"…";
                message += L" " + line;
            }
        }
        ShowGlassAlertText(Tr(L"Import failed"), message, GlassAlertKind::Error);
        return;
    }

    PendingRatingsImport pending;
    pending.file = result.file;
    pending.folder = currentFolder_;
    pending.generation = generation_;
    pending.document = std::move(result.parse.document);
    std::vector<rx::FolderFile> folderFiles;
    folderFiles.reserve(catalog_.PhotoCount());
    for (std::size_t index = 0; index < catalog_.PhotoCount(); ++index)
        folderFiles.push_back({ catalog_.PhotoAt(index).path.filename().wstring(), index });
    pending.reconciliation = rx::ReconcileWithFolder(pending.document, folderFiles, WindowsNameFold);
    if (!pending.reconciliation.duplicateEntries.empty()) {
        const auto& entry = pending.document.entries[pending.reconciliation.duplicateEntries.front()];
        ShowGlassAlertText(Tr(L"Import failed"),
            Tr(L"This file is not a valid QuickSift ratings export, so nothing was imported.") + L"\n\n" +
                Tr(L"The same file name is listed more than once.") + L"\n" +
                ReplaceToken(Tr(L"Line {line}:"), L"{line}", std::to_wstring(entry.lineNumber)) +
                L" " + entry.sourceLine,
            GlassAlertKind::Error);
        return;
    }
    pending.missingText = rx::MissingLinesText(pending.document, pending.reconciliation);
    const bool countMismatch = pending.reconciliation.CountMismatch();
    const std::size_t expected = pending.reconciliation.expectedFiles;
    const std::size_t present = pending.reconciliation.presentFiles;
    pendingRatingsImport_ = std::move(pending);

    if (countMismatch) {
        std::wstring message = Tr(L"Total files count mismatch. Expected is {expected} files, {present} files present. Continue?");
        message = ReplaceToken(message, L"{expected}", std::to_wstring(expected));
        message = ReplaceToken(message, L"{present}", std::to_wstring(present));
        ShowGlassChoice(Tr(L"Import ratings"), message, Tr(L"Continue"),
            PendingGlassAction::RatingsImportCountMismatchContinue, Tr(L"Cancel"),
            GlassAlertKind::Warning);
        return;
    }
    ContinueRatingsImport(PendingGlassAction::RatingsImportCountMismatchContinue);
}

void QuickSiftApplicationImpl::ContinueRatingsImport(PendingGlassAction step) {
    if (!pendingRatingsImport_) return;
    if (pendingRatingsImport_->generation != generation_ || pendingRatingsImport_->folder != currentFolder_) {
        CancelRatingsImport();
        ShowToast(Tr(L"Ratings import cancelled because the folder changed."));
        return;
    }
    if (step == PendingGlassAction::RatingsImportCountMismatchContinue) {
        if (pendingRatingsImport_->reconciliation.HasMissing()) {
            std::wstring message = Tr(L"Some files to be modified are missing. Ignore and continue?");
            std::wstring detail = Tr(L"{missing} of {listed} listed files are not in this folder.");
            detail = ReplaceToken(detail, L"{missing}",
                std::to_wstring(pendingRatingsImport_->reconciliation.missingEntries.size()));
            detail = ReplaceToken(detail, L"{listed}",
                std::to_wstring(pendingRatingsImport_->document.entries.size()));
            ShowGlassChoice(Tr(L"Import ratings"), message + L"\n\n" + detail, Tr(L"Yes"),
                PendingGlassAction::RatingsImportMissingContinue, Tr(L"Cancel"),
                GlassAlertKind::Warning, Tr(L"See missing files list"),
                PendingGlassAction::RatingsImportShowMissing, /*tertiaryKeepsOpen=*/true);
            return;
        }
        step = PendingGlassAction::RatingsImportMissingContinue;
    }
    if (step == PendingGlassAction::RatingsImportMissingContinue) {
        std::wstring title = Tr(L"Import ratings for {count} files");
        title = ReplaceToken(title, L"{count}",
            std::to_wstring(pendingRatingsImport_->reconciliation.matches.size()));
        ShowGlassChoice(title, Tr(L"Are you sure? This cannot be undone."), Tr(L"Import"),
            PendingGlassAction::RatingsImportFinalConfirm, Tr(L"Cancel"), GlassAlertKind::Warning);
        return;
    }
    if (step == PendingGlassAction::RatingsImportFinalConfirm) ApplyRatingsImport();
}

void QuickSiftApplicationImpl::ShowRatingsMissingFilesWindow() {
    if (!pendingRatingsImport_) return;
    if (ratingsMissingFilesWindow_ && IsWindow(ratingsMissingFilesWindow_)) {
        ShowWindow(ratingsMissingFilesWindow_, SW_SHOWNORMAL);
        SetForegroundWindow(ratingsMissingFilesWindow_);
        return;
    }
    // Verbatim lines from the file (selectable; Ctrl+A / Ctrl+C in the read-only
    // RichEdit). The glass warning stays open behind it (tertiaryKeepsOpen).
    ratingsMissingFilesWindow_ = ShowDocumentWindow(Tr(L"Missing files"),
        pendingRatingsImport_->missingText, DocumentKind::MissingFiles);
}

void QuickSiftApplicationImpl::CloseRatingsMissingFilesWindow() {
    if (ratingsMissingFilesWindow_ && IsWindow(ratingsMissingFilesWindow_))
        DestroyWindow(ratingsMissingFilesWindow_);
    ratingsMissingFilesWindow_ = nullptr;
}

void QuickSiftApplicationImpl::CancelRatingsImport() {
    pendingRatingsImport_.reset();
    CloseRatingsMissingFilesWindow();
}

void QuickSiftApplicationImpl::ApplyRatingsImport() {
    if (!pendingRatingsImport_) return;
    PendingRatingsImport pending = std::move(*pendingRatingsImport_);
    CancelRatingsImport();
    if (pending.generation != generation_ || pending.folder != currentFolder_ || !scanComplete_) {
        ShowToast(Tr(L"Ratings import cancelled because the folder changed."));
        return;
    }
    if (transactions_.Busy() || ratingsImportToken_ != 0) {
        ShowGlassAlert(L"Import ratings",
            L"Wait for the current file, metadata or ratings operation to finish, then try again.",
            GlassAlertKind::Warning);
        return;
    }
    // Re-reconcile against the catalog as it is NOW (copy/move/delete may have
    // changed it while the confirmations were open); newly missing files are
    // skipped like the accepted missing ones.
    std::vector<rx::FolderFile> folderFiles;
    folderFiles.reserve(catalog_.PhotoCount());
    for (std::size_t index = 0; index < catalog_.PhotoCount(); ++index)
        folderFiles.push_back({ catalog_.PhotoAt(index).path.filename().wstring(), index });
    const rx::ImportReconciliation now = rx::ReconcileWithFolder(pending.document, folderFiles, WindowsNameFold);
    std::vector<RatingsImportItem> items;
    items.reserve(now.matches.size());
    for (const rx::ImportMatch& match : now.matches) {
        const rx::RatingsEntry& entry = pending.document.entries[match.entryIndex];
        RatingsImportItem item;
        item.path = catalog_.PhotoAt(match.catalogIndex).path;
        item.starRating = entry.starRating;
        item.pickState = entry.pickState;
        item.colorLabel = entry.colorLabel;
        items.push_back(std::move(item));
    }
    RatingsImportCounts counts;
    counts.skipped = now.missingEntries.size() + now.duplicateEntries.size();
    ratingsImportIsBatch_ = false;
    const std::uint64_t token = SubmitRatingsImport(items, counts);
    if (token == 0) {
        if (counts.submitted == 0 && !uiFramework_.Alerts().Visible()) {
            std::wstring summary = Tr(L"Ratings import complete: {applied} applied, {skipped} skipped, {failed} failed.");
            summary = ReplaceToken(summary, L"{applied}", L"0");
            summary = ReplaceToken(summary, L"{skipped}", std::to_wstring(counts.skipped));
            summary = ReplaceToken(summary, L"{failed}", std::to_wstring(counts.failed));
            ShowToast(summary);
        }
        return;
    }
    ratingsImportToken_ = token;
    ratingsImportCounts_ = counts;
    StartUiTimer(ID_TIMER_RATINGS_PROGRESS, 250, L"ratings progress");
    UpdateStatus();
    ShowToast(Tr(L"Importing ratings…"));
}

bool QuickSiftApplicationImpl::FinishRatingsImportTransaction(
    const quicksift::transactions::MetadataTransactionResult& result, bool changed) {
    (void)changed;
    const RatingsImportCounts counts = ratingsImportCounts_;
    const bool batch = ratingsImportIsBatch_;
    ratingsImportToken_ = 0;
    ratingsImportCounts_ = {};
    ratingsImportIsBatch_ = false;
    KillTimer(hwnd_, ID_TIMER_RATINGS_PROGRESS);
    UpdateStatus();
    const std::size_t applied = static_cast<std::size_t>(
        std::count(result.completed.begin(), result.completed.end(), true));
    const std::size_t notCompleted = result.changes.size() > applied ? result.changes.size() - applied : 0;
    const std::size_t failed = counts.failed + notCompleted;
    std::wstring summary = batch ?
        Tr(L"Batch processing complete: {applied} applied, {skipped} skipped, {failed} failed.") :
        Tr(L"Ratings import complete: {applied} applied, {skipped} skipped, {failed} failed.");
    summary = ReplaceToken(summary, L"{applied}", std::to_wstring(applied));
    summary = ReplaceToken(summary, L"{skipped}", std::to_wstring(counts.skipped));
    summary = ReplaceToken(summary, L"{failed}", std::to_wstring(failed));
    if (result.failed || result.cancelled || result.failureCount > 0 || failed > 0) {
        std::wstring message = summary;
        if (!result.detail.empty()) message += L"\n\n" + Tr(result.detail);
        if (result.conflictCount > 0) {
            message += L"\n\n" + ReplaceToken(Tr(L"External-change conflicts: {count}"), L"{count}",
                std::to_wstring(result.conflictCount));
        }
        const std::wstring title = batch ?
            (result.cancelled ? Tr(L"Batch processing cancelled") : Tr(L"Batch processing incomplete")) :
            (result.cancelled ? Tr(L"Ratings import cancelled") : Tr(L"Ratings import incomplete"));
        ShowGlassAlertText(title,
            message, result.failed && applied == 0 ? GlassAlertKind::Error : GlassAlertKind::Warning);
    } else {
        ShowToast(summary);
    }
    return true;
}

std::wstring QuickSiftApplicationImpl::RatingsStatusLine() const {
    if (RatingsWorkerBusy()) {
        const std::size_t total = ratingsWorkerTotal_.load(std::memory_order_acquire);
        if (total == 0) return Tr(L"Reading ratings file…");
        std::wstring text = Tr(L"Exporting ratings: {done} / {total} files");
        text = ReplaceToken(text, L"{done}", std::to_wstring(
            std::min(total, ratingsWorkerDone_.load(std::memory_order_acquire))));
        return ReplaceToken(text, L"{total}", std::to_wstring(total));
    }
    if (ratingsImportToken_ != 0) {
        if (const auto progress = transactions_.MetadataProgress(); progress && progress->totalItems > 0) {
            std::wstring text = ratingsImportIsBatch_ ? Tr(L"Batch processing: {done} / {total} files") :
                Tr(L"Importing ratings: {done} / {total} files");
            text = ReplaceToken(text, L"{done}", std::to_wstring(progress->completedItems));
            return ReplaceToken(text, L"{total}", std::to_wstring(progress->totalItems));
        }
        return ratingsImportIsBatch_ ? Tr(L"Batch processing…") : Tr(L"Importing ratings…");
    }
    if (BatchWorkerBusy()) return Tr(L"Batch processing: preparing…");
    return {};
}

} // namespace quicksift::app
