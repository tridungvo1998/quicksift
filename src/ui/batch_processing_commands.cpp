// OWNER: Batch Processing (File tab). Builds the framework form dialog, runs the
// folder-name load and the match preflight on the batch worker thread, shows the
// glass confirmation, and applies star rating / pick state / color tag through the
// existing metadata transaction pipeline (SubmitRatingsImport, no Undo record).
// Parsing, extension validation, matching and the confirmation text are portable
// and live in core/batch_match_policy (scripts/checks/batch_match_checks.cpp).
#include "app/quicksift_application_internal.h"
#include "diagnostics/log_categories.h"

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace quicksift::app {
namespace {

namespace bm = quicksift::core::batch_match;
using quicksift::ui::framework::FormEvent;
using quicksift::ui::framework::FormRow;
using quicksift::ui::framework::FormRowKind;
using quicksift::ui::framework::MacOsUiFramework;

// Dialog-local control ids (the dialog is its own top-level window).
constexpr int kNamesId = 101;
constexpr int kLoadNamesId = 102;
constexpr int kStarId = 103;
constexpr int kPickId = 104;
constexpr int kColorId = 105;
constexpr int kExtensionsId = 106;
constexpr int kMethodId = 107;
constexpr int kExecuteId = IDOK;
constexpr int kCancelId = IDCANCEL;

// The framework text input caps its content at TextInputOptions::maxLength.
constexpr std::size_t kNameListMaxChars = std::size_t{ 1 } << 20;

// Dropdown options (English keys; translated when the dialog is built). Index 0 is
// always "No change". Values match the app's own Rating / Pick / Color menus.
// Labels and values come from the canonical vocabulary (core/metadata_vocabulary.h).
namespace vocab = quicksift::core::vocabulary;
constexpr std::wstring_view kStarOptions[] = { L"No change", vocab::kStarLabels[0],
    vocab::kStarLabels[1], vocab::kStarLabels[2], vocab::kStarLabels[3], vocab::kStarLabels[4],
    vocab::kStarLabels[5] };
constexpr std::wstring_view kPickOptions[] = { L"No change", vocab::kPicks[0].command,
    vocab::kPicks[1].command, vocab::kPicks[2].command };
constexpr int kPickValues[] = { 0, vocab::kPicks[0].value, vocab::kPicks[1].value, vocab::kPicks[2].value };
constexpr std::wstring_view kColorOptions[] = { L"No change", vocab::kColors[0].label,
    vocab::kColors[1].label, vocab::kColors[2].label, vocab::kColors[3].label, vocab::kColors[4].label,
    vocab::kColors[5].label };
constexpr std::wstring_view kMethodOptions[] = { L"Partial matching", L"Complete matching" };

template <std::size_t N>
int ClampIndex(int index, const std::wstring_view (&)[N]) noexcept {
    return std::clamp(index, 0, static_cast<int>(N) - 1);
}

std::optional<int> StarValue(int index) {
    if (index <= 0) return std::nullopt;
    return std::clamp(index - 1, 0, 5);
}
std::optional<int> PickValue(int index) {
    if (index <= 0 || index >= static_cast<int>(std::size(kPickValues))) return std::nullopt;
    return kPickValues[index];
}
std::optional<int> ColorValue(int index) {
    if (index <= 0) return std::nullopt;
    return std::clamp(index - 1, 0, 5);
}

std::wstring Replace(std::wstring text, std::wstring_view token, const std::wstring& value) {
    const std::size_t position = text.find(token);
    if (position != std::wstring::npos) text.replace(position, token.size(), value);
    return text;
}

bool HasAnyName(std::wstring_view text) noexcept {
    return std::any_of(text.begin(), text.end(), [](wchar_t ch) {
        return !bm::IsListSeparator(ch) && ch != L'"';
    });
}

// Regular, non-hidden, non-system files with a supported image extension directly
// inside folder (non-recursive, like the catalog scanner). Sorted case-insensitively.
bool ListImageFileNames(const std::wstring& folder, const std::atomic<bool>& cancel,
    std::vector<std::wstring>& names, std::wstring& error) {
    std::wstring pattern = folder;
    if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') pattern += L'\\';
    pattern += L'*';
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
        FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND) return true;
        error = L"Windows error " + std::to_wstring(code);
        return false;
    }
    do {
        if (cancel.load(std::memory_order_acquire)) break;
        if (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN |
            FILE_ATTRIBUTE_SYSTEM)) continue;
        const std::wstring_view name(data.cFileName);
        const std::size_t dot = name.rfind(L'.');
        if (dot == std::wstring_view::npos || dot == 0) continue;
        if (!bm::IsSupportedImageExtension(bm::NormalizeExtensionToken(name.substr(dot)))) continue;
        names.emplace_back(name);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) {
        return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
            static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
    });
    return true;
}

} // namespace

// ---- Worker ----------------------------------------------------------------------

bool QuickSiftApplicationImpl::BatchWorkerBusy() const noexcept {
    return batchWorkerRunning_.load(std::memory_order_acquire);
}

void QuickSiftApplicationImpl::StopBatchWorker() noexcept {
    batchWorkerCancel_.store(true, std::memory_order_release);
    try {
        if (batchWorker_.joinable()) batchWorker_.join();
    } catch (...) {
        // Shutdown boundary: never throw.
    }
    batchWorkerRunning_.store(false, std::memory_order_release);
}

bool QuickSiftApplicationImpl::StartBatchWorker(bool folderLoad,
    std::function<void(BatchWorkerResult&)> work) {
    if (BatchWorkerBusy()) return false;
    StopBatchWorker(); // joins a finished previous worker (never a running one)
    batchWorkerCancel_.store(false, std::memory_order_release);
    batchWorkerRunning_.store(true, std::memory_order_release);
    {
        std::lock_guard lock(batchWorkerMutex_);
        batchWorkerResult_.reset();
    }
    const std::uint64_t serial = ++batchWorkerSerial_;
    const std::uint64_t generation = generation_;
    const HWND window = hwnd_;
    try {
        batchWorker_ = std::thread([this, window, serial, generation, folderLoad,
            work = std::move(work)]() noexcept {
            BatchWorkerResult result;
            result.folderLoad = folderLoad;
            result.serial = serial;
            result.generation = generation;
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            try {
                work(result);
            } catch (...) {
                result.ok = false;
                if (result.error.empty()) result.error = L"Unexpected error.";
            }
            if (batchWorkerCancel_.load(std::memory_order_acquire)) {
                result.ok = false;
                result.error = L"cancelled";
            }
            try {
                std::lock_guard lock(batchWorkerMutex_);
                batchWorkerResult_ = std::move(result);
            } catch (...) {
            }
            batchWorkerRunning_.store(false, std::memory_order_release);
            // A lost post is recovered by OnTimer (worker joinable && not busy).
            static_cast<void>(PostMessageW(window, WM_APP_BATCH_WORKER_DONE, 0, 0));
        });
    } catch (...) {
        batchWorkerRunning_.store(false, std::memory_order_release);
        return false;
    }
    UpdateStatus();
    return true;
}

// ---- Dialog ----------------------------------------------------------------------

void QuickSiftApplicationImpl::OpenBatchProcessingDialog() {
    if (batchDialog_ && IsWindow(batchDialog_)) {
        if (!batchAwaitingGlass_) uiFramework_.ShowFormDialog(batchDialog_, true);
        return;
    }
    batchDialog_ = nullptr;
    if (currentFolder_.empty()) {
        ShowGlassAlert(L"Batch Processing", L"Open a folder first.", GlassAlertKind::Information);
        return;
    }
    if (transactions_.Busy() || RatingsWorkerBusy() || ratingsImportToken_ != 0) {
        ShowGlassAlert(L"Batch Processing",
            L"Wait for the current file, metadata or ratings operation to finish, then try again.",
            GlassAlertKind::Warning);
        return;
    }
    const auto options = [this](const auto& keys) {
        std::vector<std::wstring> translated;
        for (std::wstring_view key : keys) translated.push_back(Tr(key));
        return translated;
    };
    quicksift::ui::framework::FormDialogSpec spec{};
    spec.owner = hwnd_;
    spec.instance = instance_;
    spec.title = Tr(L"Batch Processing");
    spec.palette = CurrentDocumentTheme();
    spec.widthDip = 620;
    spec.labelWidthDip = 150;
    {
        FormRow row{};
        row.kind = FormRowKind::SectionHeader;
        row.label = Tr(L"File name list");
        spec.rows.push_back(std::move(row));
    }
    {
        FormRow row{};
        row.kind = FormRowKind::TextArea;
        row.id = kNamesId;
        row.text = batchSettings_.names;
        row.placeholder = Tr(L"Type or paste file names, separated by spaces, commas, semicolons or new lines.");
        row.heightDip = 150;
        spec.rows.push_back(std::move(row));
    }
    {
        FormRow row{};
        row.kind = FormRowKind::Button;
        row.id = kLoadNamesId;
        row.label = Tr(L"Load names from folder");
        spec.rows.push_back(std::move(row));
    }
    {
        FormRow row{};
        row.kind = FormRowKind::SectionHeader;
        row.label = Tr(L"Do what?");
        spec.rows.push_back(std::move(row));
    }
    const auto dropdown = [&](int id, std::wstring_view label, std::vector<std::wstring> values, int selected) {
        FormRow row{};
        row.kind = FormRowKind::Dropdown;
        row.id = id;
        row.label = Tr(label);
        row.options = std::move(values);
        row.selected = selected;
        spec.rows.push_back(std::move(row));
    };
    dropdown(kStarId, L"Star rating", options(kStarOptions), ClampIndex(batchSettings_.starRating, kStarOptions));
    dropdown(kPickId, L"Pick state", options(kPickOptions), ClampIndex(batchSettings_.pickState, kPickOptions));
    dropdown(kColorId, L"Color tag", options(kColorOptions), ClampIndex(batchSettings_.colorLabel, kColorOptions));
    {
        FormRow row{};
        row.kind = FormRowKind::SectionHeader;
        row.label = Tr(L"Additional settings");
        spec.rows.push_back(std::move(row));
    }
    {
        FormRow row{};
        row.kind = FormRowKind::TextField;
        row.id = kExtensionsId;
        row.label = Tr(L"Extensions");
        row.text = batchSettings_.extensions;
        row.placeholder = Tr(L"e.g. jpg, arw (empty = all supported types)");
        spec.rows.push_back(std::move(row));
    }
    dropdown(kMethodId, L"Matching method", options(kMethodOptions),
        ClampIndex(batchSettings_.matchingMethod, kMethodOptions));
    spec.primaryId = kExecuteId;
    spec.primaryText = Tr(L"Execute");
    spec.cancelId = kCancelId;
    spec.cancelText = Tr(L"Cancel");
    spec.onEvent = [this](HWND dialog, int id, FormEvent event) { OnBatchFormEvent(dialog, id, event); };
    spec.maintenancePump = DropdownMaintenancePump();
    spec.errorSink = [](std::wstring_view operation) {
        quicksift::diagnostics::WriteLastError(quicksift::diagnostics::Level::Error, L"UI", operation);
    };
    batchDialog_ = uiFramework_.CreateFormDialog(std::move(spec));
    batchDialogLanguage_ = localizer_.CurrentLanguage();
    batchAwaitingGlass_ = false;
    if (!batchDialog_) {
        ShowGlassAlert(L"Batch Processing", L"QuickSift could not open the Batch Processing window.",
            GlassAlertKind::Error);
        return;
    }
    ValidateBatchExtensions();
}

void QuickSiftApplicationImpl::OnBatchFormEvent(HWND dialog, int id, FormEvent event) {
    if (event == FormEvent::Destroyed) {
        if (dialog == batchDialog_) {
            batchDialog_ = nullptr;
            batchAwaitingGlass_ = false;
            batchWorkerCancel_.store(true, std::memory_order_release);
            KillTimer(hwnd_, ID_TIMER_BATCH_GLASS_WATCH);
        }
        return;
    }
    if (dialog != batchDialog_) return;
    switch (event) {
    case FormEvent::TextChanged:
        if (id == kExtensionsId) ValidateBatchExtensions();
        uiFramework_.SetFormStatus(batchDialog_, {}, false);
        return;
    case FormEvent::SelectionChanged:
        uiFramework_.SetFormStatus(batchDialog_, {}, false);
        return;
    case FormEvent::Clicked:
        if (id == kCancelId) {
            batchWorkerCancel_.store(true, std::memory_order_release);
            CaptureBatchSettings();
            CloseBatchDialog();
        } else if (id == kLoadNamesId) {
            BeginBatchFolderLoad();
        } else if (id == kExecuteId) {
            BeginBatchPreflight();
        }
        return;
    default:
        return;
    }
}

void QuickSiftApplicationImpl::CaptureBatchSettings() {
    if (!batchDialog_) return;
    batchSettings_.names = MacOsUiFramework::FormText(batchDialog_, kNamesId);
    batchSettings_.extensions = MacOsUiFramework::FormText(batchDialog_, kExtensionsId);
    batchSettings_.starRating = MacOsUiFramework::FormSelection(batchDialog_, kStarId);
    batchSettings_.pickState = MacOsUiFramework::FormSelection(batchDialog_, kPickId);
    batchSettings_.colorLabel = MacOsUiFramework::FormSelection(batchDialog_, kColorId);
    batchSettings_.matchingMethod = MacOsUiFramework::FormSelection(batchDialog_, kMethodId);
}

bool QuickSiftApplicationImpl::ValidateBatchExtensions() {
    if (!batchDialog_) return false;
    const bm::ExtensionList list = bm::ParseExtensionList(MacOsUiFramework::FormText(batchDialog_, kExtensionsId));
    if (list.Valid()) {
        uiFramework_.SetFormFieldError(batchDialog_, kExtensionsId, {});
        return true;
    }
    std::wstring joined;
    for (const std::wstring& token : list.unknown) {
        if (!joined.empty()) joined += L", ";
        joined += token;
    }
    uiFramework_.SetFormFieldError(batchDialog_, kExtensionsId,
        Replace(Tr(L"Not a supported image type: {list}"), L"{list}", joined));
    return false;
}

void QuickSiftApplicationImpl::BeginBatchFolderLoad() {
    if (!batchDialog_ || BatchWorkerBusy()) return;
    quicksift::ui::framework::FolderPickerRequest request{};
    request.owner = batchDialog_;
    request.title = Tr(L"Load names from folder");
    request.okLabel = Tr(L"Load names");
    request.initialFolder = currentFolder_.wstring();
    const auto folder = MacOsUiFramework::PickFolder(request);
    if (!folder || !batchDialog_) return;
    const std::wstring existing = MacOsUiFramework::FormText(batchDialog_, kNamesId);
    const std::wstring path = *folder;
    const bool started = StartBatchWorker(true, [this, path, existing](BatchWorkerResult& result) {
        result.folder = path;
        std::vector<std::wstring> names;
        if (!ListImageFileNames(path, batchWorkerCancel_, names, result.error)) return;
        result.filesSeen = names.size();
        result.merge = bm::MergeNamesIntoText(existing, names, WindowsNameFold);
        result.ok = true;
    });
    if (!started) {
        uiFramework_.SetFormStatus(batchDialog_, Tr(L"QuickSift could not start reading that folder."), true);
        return;
    }
    uiFramework_.SetFormBusy(batchDialog_, true);
    uiFramework_.SetFormStatus(batchDialog_, Tr(L"Reading folder…"), false);
}

void QuickSiftApplicationImpl::BeginBatchPreflight() {
    if (!batchDialog_ || BatchWorkerBusy()) return;
    CaptureBatchSettings();
    const auto fail = [this](std::wstring_view message, int focusId) {
        uiFramework_.SetFormStatus(batchDialog_, Tr(message), true);
        if (focusId != 0) uiFramework_.FocusFormControl(batchDialog_, focusId);
    };
    if (currentFolder_.empty()) return fail(L"Open a folder first.", 0);
    if (!scanComplete_)
        return fail(L"QuickSift is still listing this folder. Try again when the folder has finished loading.", 0);
    if (transactions_.Busy() || RatingsWorkerBusy() || ratingsImportToken_ != 0)
        return fail(L"Wait for the current file, metadata or ratings operation to finish, then try again.", 0);
    if (!HasAnyName(batchSettings_.names)) return fail(L"Enter at least one file name.", kNamesId);
    if (batchSettings_.starRating == 0 && batchSettings_.pickState == 0 && batchSettings_.colorLabel == 0)
        return fail(L"Choose at least one change under \u201CDo what?\u201D.", kStarId);
    if (!ValidateBatchExtensions())
        return fail(L"Fix the Extensions field: it lists a type QuickSift does not support.", kExtensionsId);

    // Scope: every photo of the current folder's catalog (non-recursive, all
    // supported formats; view filters and format toggles do not apply), narrowed
    // to the target extensions by the matcher.
    const bm::ExtensionList list = bm::ParseExtensionList(batchSettings_.extensions);
    std::vector<bm::CandidateFile> files;
    std::vector<fs::path> paths;
    files.reserve(catalog_.PhotoCount());
    paths.reserve(catalog_.PhotoCount());
    for (std::size_t index = 0; index < catalog_.PhotoCount(); ++index) {
        const fs::path& path = catalog_.PhotoAt(index).path;
        const std::wstring fileName = path.filename().wstring();
        bm::CandidateFile file;
        file.id = paths.size();
        file.stem = bm::StemOf(fileName);
        file.extension = bm::NormalizeExtensionToken(std::wstring_view(fileName).substr(file.stem.size()));
        files.push_back(std::move(file));
        paths.push_back(path);
    }
    const std::vector<std::wstring> targets = bm::TargetExtensions(list);
    const bm::MatchMethod method = batchSettings_.matchingMethod == 1 ?
        bm::MatchMethod::Complete : bm::MatchMethod::Partial;
    const std::wstring text = batchSettings_.names;
    const bool started = StartBatchWorker(false, [this, text, files = std::move(files),
        paths = std::move(paths), targets, method](BatchWorkerResult& result) {
        result.entries = bm::ParseNameList(text, WindowsNameFold);
        if (result.entries.empty()) {
            result.error = L"no-names";
            return;
        }
        result.match = bm::MatchFiles(result.entries, files, targets, method, WindowsNameFold,
            &batchWorkerCancel_);
        if (result.match.cancelled) return;
        result.matchedPaths.reserve(result.match.matchedIds.size());
        for (const std::size_t id : result.match.matchedIds)
            if (id < paths.size()) result.matchedPaths.push_back(paths[id]);
        result.ok = true;
    });
    if (!started) return fail(L"QuickSift could not start Batch Processing.", 0);
    uiFramework_.SetFormBusy(batchDialog_, true);
    uiFramework_.SetFormStatus(batchDialog_, Tr(L"Finding matching files…"), false);
}

void QuickSiftApplicationImpl::OnBatchWorkerCompleted() {
    std::optional<BatchWorkerResult> taken;
    {
        std::lock_guard lock(batchWorkerMutex_);
        taken = std::move(batchWorkerResult_);
        batchWorkerResult_.reset();
    }
    if (batchWorker_.joinable() && !BatchWorkerBusy()) {
        try { batchWorker_.join(); } catch (...) {}
    }
    UpdateStatus();
    if (!taken || taken->serial != batchWorkerSerial_ || !batchDialog_) return;
    BatchWorkerResult& result = *taken;
    QS_LOG_EVENT(result.ok ? quicksift::diagnostics::Level::Info : quicksift::diagnostics::Level::Warning,
        quicksift::diagnostics::category::kBatch, result.folderLoad ? L"folder_load_finished" : L"preflight_finished",
        { L"ok", result.ok ? L"1" : L"0" },
        { L"cancelled", result.error == L"cancelled" ? L"1" : L"0" },
        { L"files_seen", std::to_wstring(result.filesSeen) },
        { L"entries", std::to_wstring(result.entries.size()) },
        { L"matched", std::to_wstring(result.match.Total()) },
        { L"unmatched", std::to_wstring(result.match.unmatchedEntries.size()) });
    uiFramework_.SetFormBusy(batchDialog_, false);
    if (result.error == L"cancelled") {
        uiFramework_.SetFormStatus(batchDialog_, {}, false);
        return;
    }

    if (result.folderLoad) {
        if (!result.ok) {
            uiFramework_.SetFormStatus(batchDialog_,
                Tr(L"QuickSift could not read that folder.") + L" " + result.error, true);
            return;
        }
        if (result.filesSeen == 0) {
            uiFramework_.SetFormStatus(batchDialog_, Tr(L"That folder has no supported image files."), true);
            return;
        }
        if (result.merge.text.size() > kNameListMaxChars) {
            uiFramework_.SetFormStatus(batchDialog_,
                Tr(L"The name list would be too long. Nothing was added."), true);
            return;
        }
        if (result.merge.added > 0) uiFramework_.SetFormText(batchDialog_, kNamesId, result.merge.text);
        std::wstring note = Tr(L"Added {added} names ({duplicates} already listed)");
        note = Replace(note, L"{added}", std::to_wstring(result.merge.added));
        note = Replace(note, L"{duplicates}", std::to_wstring(result.merge.duplicates));
        uiFramework_.SetFormNote(batchDialog_, kLoadNamesId, note);
        uiFramework_.SetFormStatus(batchDialog_, {}, false);
        uiFramework_.FocusFormControl(batchDialog_, kNamesId);
        return;
    }

    // ---- Preflight ----
    if (result.generation != generation_ || currentFolder_.empty()) {
        uiFramework_.SetFormStatus(batchDialog_, Tr(L"The folder changed. Check the settings and try again."), true);
        return;
    }
    if (result.error == L"no-names") {
        uiFramework_.SetFormStatus(batchDialog_, Tr(L"Enter at least one file name."), true);
        uiFramework_.FocusFormControl(batchDialog_, kNamesId);
        return;
    }
    if (!result.ok) {
        uiFramework_.SetFormStatus(batchDialog_, Tr(L"QuickSift could not prepare Batch Processing.") +
            (result.error.empty() ? std::wstring() : L" " + result.error), true);
        return;
    }
    const bm::ExtensionList list = bm::ParseExtensionList(batchSettings_.extensions);
    const std::wstring scopeLine = list.allSupported ?
        Tr(L"Extensions: all supported image types (the Extensions field is empty).") : std::wstring();
    const std::size_t unmatched = result.match.unmatchedEntries.size();

    if (result.match.Total() == 0) {
        std::wstring message = Tr(L"No files in this folder match the names and extensions given. Nothing was changed.");
        if (!scopeLine.empty()) message += L"\n\n" + scopeLine;
        batchAwaitingGlass_ = true;
        uiFramework_.ShowFormDialog(batchDialog_, false);
        ShowGlassAlertText(Tr(L"Batch Processing"), message, GlassAlertKind::Information,
            PendingGlassAction::BatchNoMatches);
        StartUiTimer(ID_TIMER_BATCH_GLASS_WATCH, 250, L"batch glass watch");
        return;
    }

    PendingBatch pending;
    pending.folder = currentFolder_;
    pending.generation = generation_;
    pending.paths = std::move(result.matchedPaths);
    pending.starRating = StarValue(batchSettings_.starRating);
    pending.pickState = PickValue(batchSettings_.pickState);
    pending.colorLabel = ColorValue(batchSettings_.colorLabel);
    pending.unmatchedCount = unmatched;
    pending.unmatchedText = bm::UnmatchedEntriesText(result.entries, result.match.unmatchedEntries);

    std::wstring message = bm::FormatConfirmation(Tr(bm::kConfirmationTemplate),
        Tr(bm::kConfirmationTemplateSingular), result.match.Total(), result.match.extensionCounts);
    std::wstring changes;
    const auto addChange = [&](std::wstring_view label, int index, const auto& keys) {
        if (index <= 0) return;
        if (!changes.empty()) changes += L"\n";
        changes += Tr(label) + L": " + Tr(keys[static_cast<std::size_t>(index)]);
    };
    addChange(L"Star rating", ClampIndex(batchSettings_.starRating, kStarOptions), kStarOptions);
    addChange(L"Pick state", ClampIndex(batchSettings_.pickState, kPickOptions), kPickOptions);
    addChange(L"Color tag", ClampIndex(batchSettings_.colorLabel, kColorOptions), kColorOptions);
    message += L"\n\n" + changes;
    if (!scopeLine.empty()) message += L"\n\n" + scopeLine;
    std::wstring tertiary;
    if (unmatched > 0) {
        message += L"\n\n" + Replace(Tr(unmatched == 1 ? L"{count} name matched no file." :
            L"{count} names matched no file."), L"{count}", std::to_wstring(unmatched));
        tertiary = Tr(L"See unmatched names");
    }
    pendingBatch_ = std::move(pending);
    batchAwaitingGlass_ = true;
    uiFramework_.ShowFormDialog(batchDialog_, false);
    ShowGlassChoice(Tr(L"Batch Processing"), message, Tr(L"Confirm"), PendingGlassAction::BatchConfirm,
        Tr(L"Cancel"), GlassAlertKind::Warning, tertiary,
        unmatched > 0 ? PendingGlassAction::BatchShowUnmatched : PendingGlassAction::None,
        /*tertiaryKeepsOpen=*/true);
    StartUiTimer(ID_TIMER_BATCH_GLASS_WATCH, 250, L"batch glass watch");
}

// ---- Confirmation / apply ----------------------------------------------------------

void QuickSiftApplicationImpl::ReturnToBatchDialog() {
    const bool wasAwaiting = batchAwaitingGlass_;
    batchAwaitingGlass_ = false;
    KillTimer(hwnd_, ID_TIMER_BATCH_GLASS_WATCH);
    CancelBatchPending();
    if (!wasAwaiting || !batchDialog_ || !IsWindow(batchDialog_)) return;
    if (localizer_.CurrentLanguage() != batchDialogLanguage_) {
        // Language changed while the dialog was hidden: rebuild it with the same inputs.
        CaptureBatchSettings();
        CloseBatchDialog();
        OpenBatchProcessingDialog();
        return;
    }
    uiFramework_.SetFormPalette(batchDialog_, CurrentDocumentTheme());
    uiFramework_.SetFormStatus(batchDialog_, {}, false);
    uiFramework_.ShowFormDialog(batchDialog_, true);
}

void QuickSiftApplicationImpl::CheckBatchGlassWatch() {
    if (!batchAwaitingGlass_) {
        KillTimer(hwnd_, ID_TIMER_BATCH_GLASS_WATCH);
        return;
    }
    // Another alert may have replaced ours (it never resolves): bring the dialog back.
    const auto& alerts = uiFramework_.Alerts();
    const auto token = alerts.Visible() ? static_cast<PendingGlassAction>(alerts.Request().actionToken) :
        PendingGlassAction::None;
    if (token != PendingGlassAction::BatchConfirm && token != PendingGlassAction::BatchNoMatches)
        ReturnToBatchDialog();
}

void QuickSiftApplicationImpl::ApplyBatchProcessing() {
    batchAwaitingGlass_ = false;
    KillTimer(hwnd_, ID_TIMER_BATCH_GLASS_WATCH);
    if (!pendingBatch_) return;
    PendingBatch pending = std::move(*pendingBatch_);
    CancelBatchPending();
    const auto backToDialog = [this](std::wstring_view message) {
        batchAwaitingGlass_ = true; // ReturnToBatchDialog re-shows only an awaiting dialog
        ReturnToBatchDialog();
        if (batchDialog_) uiFramework_.SetFormStatus(batchDialog_, Tr(message), true);
    };
    if (pending.generation != generation_ || pending.folder != currentFolder_ || !scanComplete_)
        return backToDialog(L"The folder changed. Check the settings and try again.");
    if (transactions_.Busy() || RatingsWorkerBusy() || ratingsImportToken_ != 0)
        return backToDialog(L"Wait for the current file, metadata or ratings operation to finish, then try again.");

    // Files removed/renamed since the preflight are skipped (not failed).
    std::vector<RatingsImportItem> items;
    items.reserve(pending.paths.size());
    RatingsImportCounts counts;
    for (const fs::path& path : pending.paths) {
        if (!CatalogIndexForPath(path.wstring())) {
            ++counts.skipped;
            continue;
        }
        RatingsImportItem item;
        item.path = path;
        item.starRating = pending.starRating;
        item.pickState = pending.pickState;
        item.colorLabel = pending.colorLabel;
        items.push_back(std::move(item));
    }
    CaptureBatchSettings();
    CloseBatchDialog();
    ratingsImportIsBatch_ = true;
    const std::uint64_t token = SubmitRatingsImport(items, counts, L"Batch processing");
    if (token == 0) {
        ratingsImportIsBatch_ = false;
        if (counts.submitted == 0 && !uiFramework_.Alerts().Visible()) {
            std::wstring summary = Tr(L"Batch processing complete: {applied} applied, {skipped} skipped, {failed} failed.");
            summary = Replace(summary, L"{applied}", L"0");
            summary = Replace(summary, L"{skipped}", std::to_wstring(counts.skipped));
            summary = Replace(summary, L"{failed}", std::to_wstring(counts.failed));
            ShowToast(summary);
        }
        return;
    }
    ratingsImportToken_ = token;
    ratingsImportCounts_ = counts;
    StartUiTimer(ID_TIMER_RATINGS_PROGRESS, 250, L"batch progress");
    UpdateStatus();
    ShowToast(Tr(L"Batch processing…"));
}

void QuickSiftApplicationImpl::ShowBatchUnmatchedWindow() {
    if (!pendingBatch_ || pendingBatch_->unmatchedText.empty()) return;
    if (batchUnmatchedWindow_ && IsWindow(batchUnmatchedWindow_)) {
        ShowWindow(batchUnmatchedWindow_, SW_SHOWNORMAL);
        SetForegroundWindow(batchUnmatchedWindow_);
        return;
    }
    // Reuses the read-only missing-files list window (selectable, Ctrl+A / Ctrl+C).
    batchUnmatchedWindow_ = ShowDocumentWindow(Tr(L"Unmatched names"), pendingBatch_->unmatchedText,
        DocumentKind::MissingFiles, L"Unmatched names");
}

void QuickSiftApplicationImpl::CloseBatchUnmatchedWindow() {
    if (batchUnmatchedWindow_ && IsWindow(batchUnmatchedWindow_)) DestroyWindow(batchUnmatchedWindow_);
    batchUnmatchedWindow_ = nullptr;
}

void QuickSiftApplicationImpl::CancelBatchPending() {
    pendingBatch_.reset();
    CloseBatchUnmatchedWindow();
}

void QuickSiftApplicationImpl::CloseBatchDialog() {
    batchAwaitingGlass_ = false;
    if (hwnd_) KillTimer(hwnd_, ID_TIMER_BATCH_GLASS_WATCH);
    CancelBatchPending();
    if (batchDialog_ && IsWindow(batchDialog_)) uiFramework_.CloseFormDialog(batchDialog_);
    batchDialog_ = nullptr;
}

} // namespace quicksift::app
