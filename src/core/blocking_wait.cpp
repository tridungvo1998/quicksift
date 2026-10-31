// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#include "core/blocking_wait.h"

#include <algorithm>

namespace quicksift::core::blocking_wait {
namespace {
std::wstring Fill(std::wstring text, std::wstring_view token, const std::wstring& value) {
    const std::size_t at = text.find(token);
    if (at != std::wstring::npos) text.replace(at, token.size(), value);
    return text;
}
} // namespace

std::wstring_view CommandKey(Command command) noexcept {
    switch (command) {
    case Command::Close: return L"Closing QuickSift is waiting"; // loc-key
    case Command::RatingsImport: return L"Ratings import is waiting"; // loc-key
    case Command::RatingsExport: return L"Ratings export is waiting"; // loc-key
    case Command::Batch: return L"Batch Processing is waiting"; // loc-key
    }
    return L"Closing QuickSift is waiting"; // loc-key
}

std::wstring_view CommandLogName(Command command) noexcept {
    switch (command) {
    case Command::Close: return L"close";
    case Command::RatingsImport: return L"ratings_import";
    case Command::RatingsExport: return L"ratings_export";
    case Command::Batch: return L"batch";
    }
    return L"close";
}

std::wstring_view ReasonKey(Reason reason) noexcept {
    switch (reason) {
    case Reason::FileLockedByOtherApp: return L"The file is locked by another app."; // loc-key
    case Reason::WriteRetry: return L"Retrying a metadata write that failed temporarily."; // loc-key
    case Reason::MetadataWrite: return L"Writing ratings, pick states or labels."; // loc-key
    case Reason::FileOperation: return L"Copying, moving or deleting files."; // loc-key
    case Reason::RatingsWorker: return L"Importing or exporting ratings."; // loc-key
    case Reason::BatchWorker: return L"Batch processing is running."; // loc-key
    case Reason::UnsavedEdits: return L"Rating, pick or label edits are not saved yet."; // loc-key
    }
    return L"Writing ratings, pick states or labels."; // loc-key
}

std::wstring_view ReasonLogName(Reason reason) noexcept {
    switch (reason) {
    case Reason::FileLockedByOtherApp: return L"file_locked_external";
    case Reason::WriteRetry: return L"write_retry";
    case Reason::MetadataWrite: return L"metadata_write";
    case Reason::FileOperation: return L"file_operation";
    case Reason::RatingsWorker: return L"ratings_worker";
    case Reason::BatchWorker: return L"batch_worker";
    case Reason::UnsavedEdits: return L"unsaved_edits";
    }
    return L"metadata_write";
}

std::wstring FormatElapsed(std::chrono::milliseconds elapsed) {
    const long long total = std::max<long long>(0, elapsed.count() / 1000);
    if (total < 60) return std::to_wstring(total) + L" s";
    const long long seconds = total % 60;
    return std::to_wstring(total / 60) + L" m " + (seconds < 10 ? L"0" : L"") + std::to_wstring(seconds) + L" s";
}

std::wstring Describe(Command command, const std::vector<Blocker>& blockers, const Translator& tr) {
    (void)command;
    const auto t = [&](std::wstring_view key) { return tr ? tr(key) : std::wstring(key); };
    std::wstring text;
    for (const Blocker& blocker : blockers) {
        if (!text.empty()) text += L"\n\n";
        text += t(ReasonKey(blocker.reason));
        if (!blocker.file.empty()) text += L"\n" + Fill(t(L"File: {file}"), L"{file}", blocker.file);
        if (blocker.total > 0) {
            const std::size_t left = blocker.total > blocker.completed ? blocker.total - blocker.completed : 0;
            text += L"\n" + Fill(Fill(t(L"{left} of {total} left"), L"{left}", std::to_wstring(left)),
                L"{total}", std::to_wstring(blocker.total));
        }
        text += L"\n" + Fill(t(L"Waiting for {elapsed}"), L"{elapsed}", FormatElapsed(blocker.elapsed));
    }
    if (text.empty()) text = t(L"Nothing is blocking any more.");
    return text;
}

} // namespace quicksift::core::blocking_wait
