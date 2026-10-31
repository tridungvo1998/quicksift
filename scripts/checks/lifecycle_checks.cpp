// Portable checks for app lifecycle policies: the worker shutdown token
// (core/worker_lifetime.h), diagnostic-file retention (core/log_retention.h) and the
// shared log-and-continue helper for catch (...) boundaries (core/log_and_continue.h).
#include "core/log_and_continue.h"
#include "core/log_retention.h"
#include "core/session_log_browser.h"
#include "core/worker_lifetime.h"

#include <atomic>
#include <new>
#include <stdexcept>
#include <string>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace wl = quicksift::core::worker_lifetime;
namespace lr = quicksift::core::log_retention;
namespace lac = quicksift::core::log_and_continue;
namespace sl = quicksift::core::session_logs;

namespace {
int gChecks = 0;
int gFailures = 0;

void Check(bool condition, const char* what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL: %s\n", what);
    }
}

void WorkerTokenChecks() {
    {
        wl::Token empty;
        Check(!empty.Valid() && empty.StopRequested(), "a default token is stopped");
        bool ran = false;
        Check(!empty.RunIfAlive([&] { ran = true; }) && !ran, "a default token never runs an action");
    }
    {
        wl::Gate gate;
        const wl::Token token = gate.Issue();
        Check(token.Valid() && !token.StopRequested(), "an issued token starts alive");
        Check(gate.Outstanding() == 1, "outstanding tokens are counted");
        gate.RequestStop();
        Check(token.StopRequested(), "RequestStop is visible to workers");
        int runs = 0;
        Check(token.RunIfAlive([&] { ++runs; }) && runs == 1, "posting still works after RequestStop");
        gate.Revoke();
        Check(gate.Revoked(), "Revoke marks the gate dead");
        Check(!token.RunIfAlive([&] { ++runs; }) && runs == 1, "no action runs after Revoke");
        gate.Revoke();
        Check(gate.Revoked(), "Revoke is idempotent");
    }
    {
        // A throwing action never escapes the worker.
        wl::Gate gate;
        const wl::Token token = gate.Issue();
        Check(!token.RunIfAlive([] { throw 1; }), "a throwing action is contained");
    }
    {
        // Revoke waits for an action that is already running.
        wl::Gate gate;
        std::atomic<bool> inAction{ false };
        std::atomic<bool> actionDone{ false };
        std::thread worker([token = gate.Issue(), &inAction, &actionDone] {
            token.RunIfAlive([&] {
                inAction.store(true);
                std::this_thread::sleep_for(std::chrono::milliseconds(60));
                actionDone.store(true);
            });
        });
        while (!inAction.load()) std::this_thread::yield();
        gate.Revoke();
        Check(actionDone.load(), "Revoke returns only after the in-flight action finished");
        worker.join();
    }
    {
        // Busy workers at close (the WM_DESTROY model): several threads poll
        // StopRequested and post as fast as they can; after Revoke returns no
        // post is counted, and every worker exits so the join is prompt.
        wl::Gate gate;
        std::atomic<long> posts{ 0 };
        std::atomic<bool> postedAfterRevoke{ false };
        std::atomic<bool> revoked{ false };
        std::vector<std::thread> workers;
        for (int i = 0; i < 6; ++i) {
            workers.emplace_back([token = gate.Issue(), &posts, &revoked, &postedAfterRevoke] {
                while (!token.StopRequested()) {
                    token.RunIfAlive([&] {
                        if (revoked.load()) postedAfterRevoke.store(true);
                        posts.fetch_add(1);
                    });
                }
                // A late post attempt after stop is refused once revoked.
                token.RunIfAlive([&] { if (revoked.load()) postedAfterRevoke.store(true); });
            });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        gate.Revoke();
        revoked.store(true);
        const auto joinStart = std::chrono::steady_clock::now();
        for (std::thread& worker : workers) worker.join();
        const auto joinMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - joinStart).count();
        Check(posts.load() > 0, "busy workers posted before close");
        Check(!postedAfterRevoke.load(), "no worker posts after Revoke");
        Check(joinMs < 1000, "busy workers stop promptly after Revoke");
        Check(gate.Outstanding() == 0, "joined workers release their tokens");
    }
}
void LogRetentionChecks() {
    using lr::Classify;
    using lr::Kind;
    Check(Classify(L"QuickSift-20261019-101010-123-4567.log") == Kind::SessionLog, "session log name");
    Check(Classify(L"QuickSift-20261019-101010-123-4567.LOG") == Kind::SessionLog, "extension case is ignored");
    Check(Classify(L"QuickSift-crash-20261019-101010-123-p4567.log") == Kind::CrashReport, "crash report name");
    Check(Classify(L"QuickSift-crash-20261019-101010-123-p4567.dmp") == Kind::CrashDump, "minidump name");
    for (const wchar_t* foreign : { L"QuickSift.session.ini", L"QuickSift-notes.log", L"QuickSift-20261019-101010-123-.log",
             L"QuickSift-20261019-101010-123-4567.log.bak", L"QuickSift-20261019-101010-123-4567.txt",
             L"QuickSift-2026101-101010-123-4567.log", L"quicksift-20261019-101010-123-4567.log",
             L"QuickSift-crash-20261019-101010-123-4567.dmp", L"QuickSift-crash-20261019-101010-123-p.dmp",
             L"QuickSift-crash-x.log", L"Other-20261019-101010-123-4567.log", L"QuickSift-20261019-101010-123-45a7.log",
             L"" })
        Check(Classify(foreign) == Kind::None, "names QuickSift did not create are never ours");

    const auto now = std::chrono::system_clock::now();
    const auto old = now - lr::kMaxAge - std::chrono::hours(1);
    const auto entry = [](const wchar_t* name, std::chrono::system_clock::time_point time) {
        lr::Entry e;
        e.name = name;
        e.lastWrite = time;
        e.regularFile = true;
        return e;
    };
    const wchar_t* current = L"QuickSift-20261019-101010-123-4567.log";
    Check(lr::Decide(lr::Folder::Logs, entry(L"QuickSift-20261001-101010-123-11.log", old), now, current) ==
        lr::Skip::Delete, "an old session log is deleted");
    Check(lr::Decide(lr::Folder::Logs, entry(current, old), now, current) == lr::Skip::CurrentSession,
        "the current session log is never deleted, even when its time looks old");
    Check(lr::Decide(lr::Folder::Logs, entry(L"QUICKSIFT-20261019-101010-123-4567.LOG", old), now,
        L"quicksift-20261019-101010-123-4567.log") != lr::Skip::Delete, "a foreign-case name is never deleted");
    Check(lr::Decide(lr::Folder::Logs, entry(L"QuickSift-20261018-101010-123-11.log", now - lr::kMaxAge), now, current) ==
        lr::Skip::TooNew, "exactly seven days old is kept");
    Check(lr::Decide(lr::Folder::Logs, entry(L"QuickSift-20261018-101010-123-11.log", now - std::chrono::hours(2)), now,
        current) == lr::Skip::TooNew, "a recent log is kept");
    Check(lr::Decide(lr::Folder::Logs, entry(L"QuickSift-20261018-101010-123-11.log", now + std::chrono::hours(48)), now,
        current) == lr::Skip::TooNew, "a future timestamp is never deleted");
    {
        lr::Entry link = entry(L"QuickSift-20261001-101010-123-11.log", old);
        link.reparsePoint = true;
        Check(lr::Decide(lr::Folder::Logs, link, now, current) == lr::Skip::ReparsePoint, "reparse points are never deleted");
        lr::Entry folder = entry(L"QuickSift-20261001-101010-123-11.log", old);
        folder.regularFile = false;
        Check(lr::Decide(lr::Folder::Logs, folder, now, current) == lr::Skip::NotPlainFile, "directories are never deleted");
    }
    Check(lr::Decide(lr::Folder::Logs, entry(L"QuickSift-crash-20261001-101010-123-p11.dmp", old), now, current) ==
        lr::Skip::WrongFolder, "a crash file in Logs is left alone");
    Check(lr::Decide(lr::Folder::CrashReports, entry(L"QuickSift-20261001-101010-123-11.log", old), now, current) ==
        lr::Skip::WrongFolder, "a session log in CrashReports is left alone");
    Check(lr::Decide(lr::Folder::CrashReports, entry(L"QuickSift-crash-20261001-101010-123-p11.dmp", old), now, current) ==
        lr::Skip::Delete, "an old minidump is deleted");
    Check(lr::Decide(lr::Folder::Logs, entry(L"QuickSift.session.ini", old), now, current) == lr::Skip::NotOurs,
        "the session file is never deleted");
    const std::vector<lr::Entry> listing = {
        entry(L"QuickSift-crash-20261001-101010-123-p11.log", old),
        entry(L"QuickSift-crash-20261001-101010-123-p11.dmp", old),
        entry(L"QuickSift-crash-20261018-101010-123-p12.dmp", now),
        entry(L"readme.txt", old) };
    const auto plan = lr::Plan(lr::Folder::CrashReports, listing, now, current);
    Check(plan.size() == 2 && plan[0] == L"QuickSift-crash-20261001-101010-123-p11.log" &&
        plan[1] == L"QuickSift-crash-20261001-101010-123-p11.dmp", "Plan lists exactly the old QuickSift files");
    Check(lr::kMaxAge == std::chrono::hours(24 * 7), "retention is seven days");
}
void LogAndContinueChecks() {
    std::vector<std::wstring> sunk;
    const auto sink = [&sunk](const std::wstring& message) { sunk.push_back(message); };
    {
        quicksift::core::LogRateLimiter limiter(lac::kBurstPerSite, std::chrono::milliseconds(lac::kWindowMs));
        try {
            throw std::runtime_error("disk gone");
        } catch (...) {
            Check(lac::Report(limiter, L"Cache.Flush", sink), "the first failure is reported");
        }
        Check(sunk.size() == 1 && sunk[0] == L"Cache.Flush failed and continued: disk gone",
            "the message names the site and the exception text");
        try {
            throw 42;
        } catch (...) {
            Check(lac::DescribeCurrentException() == "non-standard exception", "non-std exceptions are described");
        }
        Check(lac::DescribeCurrentException() == "no active exception", "outside a handler there is no exception");
    }
    {
        // Duplicates are rate-limited per site; the next allowed message reports them.
        quicksift::core::LogRateLimiter limiter(2, std::chrono::milliseconds(1000));
        int reported = 0;
        for (int i = 0; i < 50; ++i) {
            try { throw std::bad_alloc(); } catch (...) {
                std::uint64_t suppressed = 0;
                if (limiter.Allow(1000, &suppressed)) ++reported;
            }
        }
        Check(reported == 2, "a burst of 50 identical failures logs only the burst");
        std::uint64_t suppressed = 0;
        Check(limiter.Allow(2500, &suppressed) && suppressed == 48, "suppressed duplicates are counted");
        Check(lac::FormatContinueMessage(L"Site", "x", 48) == L"Site failed and continued: x (48 similar suppressed)",
            "the suppressed count is reported");
        Check(lac::FormatContinueMessage(L"Site", "", 0) == L"Site failed and continued: unknown exception",
            "an empty what() still produces a message");
    }
    {
        // A throwing sink never escapes (the helper is used inside catch handlers).
        quicksift::core::LogRateLimiter limiter(4, std::chrono::milliseconds(1000));
        bool escaped = false;
        try {
            try { throw std::logic_error("inner"); } catch (...) {
                const bool sent = lac::Report(limiter, L"Site",
                    [](const std::wstring&) { throw std::bad_alloc(); });
                Check(!sent, "a failing sink reports not-sent");
            }
        } catch (...) {
            escaped = true;
        }
        Check(!escaped, "Report never throws");
        static_assert(noexcept(lac::DescribeCurrentException()), "DescribeCurrentException is noexcept");
        static_assert(noexcept(lac::Report(limiter, std::wstring_view{}, sink)), "Report is noexcept");
    }
    QS_CATCH_SILENT_BY_DESIGN("the marker compiles to nothing");
}

// 0.9.5.0 round 20: previous-session log browser policy (core/session_log_browser.h).
void SessionLogBrowserChecks() {
    const auto file = [](const wchar_t* name, std::uint64_t bytes) {
        sl::Candidate c;
        c.name = name;
        c.bytes = bytes;
        c.regularFile = true;
        return c;
    };
    const std::wstring current = L"QuickSift-20261019-230000-000-100.log";
    std::vector<sl::Candidate> candidates{
        file(L"QuickSift-20261017-080000-000-11.log", 10),
        file(current.c_str(), 99),
        file(L"QuickSift-20261018-143205-123-22.log", 2048),
        file(L"QuickSift-crash-20261018-143205-123-p22.log", 5),
        file(L"QuickSift-notes.log", 5),
        file(L"QuickSift-20261018-143205-123-21.log", 7),
    };
    sl::Candidate link = file(L"QuickSift-20261019-120000-000-33.log", 1);
    link.reparsePoint = true;
    candidates.push_back(link);
    sl::Candidate folder = file(L"QuickSift-20261019-130000-000-34.log", 0);
    folder.regularFile = false;
    candidates.push_back(folder);

    const auto listed = sl::Select(candidates, current);
    Check(listed.size() == 3, "only previous session logs are listed");
    bool hasCurrent = false;
    for (const auto& item : listed) hasCurrent = hasCurrent || item.name == current;
    Check(!hasCurrent, "the current session is not listed (the window already shows it)");
    Check(listed.size() == 3 && listed[0].name.find(L"20261018-143205") != std::wstring::npos &&
        listed[1].name.find(L"20261018-143205") != std::wstring::npos &&
        listed[2].name == L"QuickSift-20261017-080000-000-11.log", "newest session first");
    Check(listed.size() == 3 && listed[0].name > listed[1].name, "same-stamp logs keep a stable order");

    std::vector<sl::Candidate> many;
    for (int i = 0; i < 100; ++i) {
        wchar_t name[64]{};
        std::swprintf(name, 64, L"QuickSift-20261001-%06d-000-1.log", i);
        many.push_back(file(name, 1));
    }
    const auto capped = sl::Select(many, L"");
    Check(capped.size() == sl::kMaxListed, "the list is capped");
    Check(!capped.empty() && capped.front().name == L"QuickSift-20261001-000099-000-1.log", "cap keeps the newest");

    Check(sl::IsBrowsableName(L"QuickSift-20261018-143205-123-22.log"), "session log name is browsable");
    for (const wchar_t* bad : { L"..\\QuickSift-20261018-143205-123-22.log", L"a/QuickSift-20261018-143205-123-22.log",
             L"C:QuickSift-20261018-143205-123-22.log", L"QuickSift-crash-20261018-143205-123-p22.log",
             L"QuickSift.session.ini", L"" })
        Check(!sl::IsBrowsableName(bad), "paths and other files are never opened");

    Check(sl::SizeText(512) == L"512 B", "bytes label");
    Check(sl::SizeText(2048) == L"2 KB", "kilobytes label");
    Check(sl::SizeText(3u * 1024u * 1024u / 2u) == L"1.5 MB", "megabytes label");
    Check(sl::Label(L"QuickSift-20261018-143205-123-22.log", 2048) == L"2026-10-18 14:32:05 \u00B7 2 KB", "menu label");

    const auto whole = sl::PlanTail(100);
    Check(whole.offset == 0 && whole.length == 100 && !whole.truncated, "small log is read whole");
    const auto tail = sl::PlanTail(16ull * 1024ull * 1024ull);
    Check(tail.truncated && tail.length == sl::kMaxDisplayBytes &&
        tail.offset == 16ull * 1024ull * 1024ull - sl::kMaxDisplayBytes, "a 16 MiB log reads only the newest 2 MiB");
    Check(sl::PlanTail(0).length == 0, "empty log");
    Check(sl::FirstShownByte("\xEF\xBB\xBFline", false) == 3, "BOM is skipped");
    Check(sl::FirstShownByte("partial\nnext", true) == 8, "a cut tail starts at the next whole line");
    Check(sl::FirstShownByte("nonewline", true) == 0, "a tail without a newline is shown as is");
}
} // namespace

int main() {
    WorkerTokenChecks();
    LogRetentionChecks();
    LogAndContinueChecks();
    SessionLogBrowserChecks();
    std::printf("lifecycle_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
