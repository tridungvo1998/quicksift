// OWNER: Deterministic, portable checks for the metadata write lane: Win32 failure
// classification, the lock-resolution retry state machine (quick backoff 50..800 ms
// within ~3 s, external phase up to 30 s, abort on corruption risks), the coalescing
// interactive-edit queue, the user-error table, and the MetadataTransactionService
// integration (retry, Skip, cancel/stop during a wait, urgent lane nesting and
// same-path deferral) driven by a fake writer with compressed time.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Wpedantic -Wshadow -Werror -Isrc
//       scripts/checks/write_lane_checks.cpp /tmp/qs-build/libQuickSiftCore.a -pthread
//   MSVC: cl /std:c++20 /EHsc /W4 /utf-8 /Isrc scripts\checks\write_lane_checks.cpp <build>\QuickSiftCore.lib

#include "core/metadata_edit_queue.h"
#include "core/user_error_catalog.h"
#include "core/write_retry_policy.h"
#include "transactions/metadata_transaction_service.h"
#include "transactions/transaction_coordinator.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace quicksift::core;
using namespace quicksift::core::write_lane;
using namespace std::chrono_literals;
namespace tx = quicksift::transactions;

int gFailures = 0;
int gChecks = 0;

void Check(bool condition, const char* label) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL: %s\n", label);
    }
}

void CheckClassification() {
    Check(ClassifyWin32Error(0) == WriteFailureKind::None, "0 -> none");
    Check(ClassifyWin32Error(32) == WriteFailureKind::InUse, "sharing violation -> in use");
    Check(ClassifyWin32Error(33) == WriteFailureKind::InUse, "lock violation -> in use");
    Check(ClassifyWin32Error(1224) == WriteFailureKind::InUse, "user-mapped section -> in use");
    Check(ClassifyWin32Error(5) == WriteFailureKind::AccessDenied, "access denied -> quick-only");
    Check(ClassifyWin32Error(112) == WriteFailureKind::LowStorage, "disk full -> low storage");
    Check(ClassifyWin32Error(39) == WriteFailureKind::LowStorage, "handle disk full");
    Check(ClassifyWin32Error(19) == WriteFailureKind::ReadOnly, "write protect -> read-only");
    Check(ClassifyWin32Error(21) == WriteFailureKind::DeviceBusy, "not ready -> device busy");
    Check(ClassifyWin32Error(2) == WriteFailureKind::Other, "file not found -> other");
    Check(IsTransient(WriteFailureKind::InUse) && IsTransient(WriteFailureKind::AccessDenied) &&
        !IsTransient(WriteFailureKind::Conflict) && !IsTransient(WriteFailureKind::Verification),
        "transient set");
}

void CheckRetryStateMachine() {
    Check(QuickBackoff(0) == 50ms && QuickBackoff(1) == 100ms && QuickBackoff(2) == 200ms &&
        QuickBackoff(3) == 400ms && QuickBackoff(4) == 800ms && QuickBackoff(5) == 1000ms &&
        QuickBackoff(40) == 1000ms, "backoff 50/100/200/400/800 then capped");

    {
        WriteRetryState state;
        const RetryDecision ok = state.OnAttempt(WriteFailureKind::None, 0ms);
        Check(ok.action == RetryAction::Done && state.Attempts() == 1, "success first try");
    }
    for (const WriteFailureKind fatal : { WriteFailureKind::Conflict, WriteFailureKind::LowStorage,
            WriteFailureKind::ReadOnly, WriteFailureKind::Verification,
            WriteFailureKind::Unsupported, WriteFailureKind::InvalidValue, WriteFailureKind::Other }) {
        WriteRetryState state;
        Check(state.OnAttempt(fatal, 0ms).action == RetryAction::Abort,
            "corruption-risk / permanent failures abort without retry");
    }
    {
        // Simulated clock: in-use until it frees at 1.2 s.
        WriteRetryState state;
        std::chrono::milliseconds clock{ 0 };
        std::vector<std::chrono::milliseconds> delays;
        for (;;) {
            const bool locked = clock < 1200ms;
            const RetryDecision decision = state.OnAttempt(
                locked ? WriteFailureKind::InUse : WriteFailureKind::None, clock);
            if (decision.action != RetryAction::RetryAfter) {
                Check(decision.action == RetryAction::Done, "released lock completes");
                break;
            }
            delays.push_back(decision.delay);
            clock += decision.delay;
        }
        Check(delays.size() == 5 && delays[0] == 50ms && delays[4] == 800ms &&
            state.Phase() == RetryPhase::Quick, "quick phase retried 5x with backoff");
    }
    {
        // Another process holds it for 10 s: one external-phase notification.
        WriteRetryState state;
        std::chrono::milliseconds clock{ 0 };
        int enteredExternal = 0;
        bool done = false;
        for (int guard = 0; guard < 200; ++guard) {
            const RetryDecision decision = state.OnAttempt(
                clock < 10000ms ? WriteFailureKind::InUse : WriteFailureKind::None, clock);
            if (decision.enteredExternalPhase) ++enteredExternal;
            if (decision.action == RetryAction::Done) { done = true; break; }
            if (decision.action == RetryAction::Abort) break;
            Check(decision.delay <= 1000ms, "never sleeps more than 1 s at a time");
            clock += decision.delay;
        }
        Check(done && enteredExternal == 1 && state.Phase() == RetryPhase::External,
            "external lock released at 10 s completes; notified once");
    }
    {
        WriteRetryState state;
        std::chrono::milliseconds clock{ 0 };
        RetryDecision last;
        for (int guard = 0; guard < 200; ++guard) {
            last = state.OnAttempt(WriteFailureKind::InUse, clock);
            if (last.action != RetryAction::RetryAfter) break;
            clock += last.delay;
        }
        Check(last.action == RetryAction::Abort && clock >= 30000ms && clock <= 31000ms,
            "permanently locked file gives up at ~30 s");
    }
    {
        WriteRetryState state;
        std::chrono::milliseconds clock{ 0 };
        RetryDecision last;
        for (int guard = 0; guard < 50; ++guard) {
            last = state.OnAttempt(WriteFailureKind::AccessDenied, clock);
            if (last.action != RetryAction::RetryAfter) break;
            clock += last.delay;
        }
        Check(last.action == RetryAction::Abort && clock < 4000ms,
            "access denied only gets the quick ~3 s budget");
    }
    {
        WriteRetryState state;
        (void)state.OnAttempt(WriteFailureKind::InUse, 0ms);
        const RetryDecision fatal = state.OnAttempt(WriteFailureKind::Conflict, 100ms);
        Check(fatal.action == RetryAction::Abort && fatal.reason == WriteFailureKind::Conflict,
            "a fingerprint change during retries aborts (reconcile)");
        Check(state.OnCancel().action == RetryAction::Abort, "cancel aborts");
    }
}

MetadataSnapshot Snap(const wchar_t* path, int rating, int pick, int color) {
    MetadataSnapshot snapshot;
    snapshot.path = path;
    snapshot.values.rating = KnownMetadataValue(rating);
    snapshot.values.pickState = KnownMetadataValue(pick);
    snapshot.values.colorLabel = KnownMetadataValue(color);
    return snapshot;
}

void CheckCoalescing() {
    PendingMetadataEdits queue;
    const MetadataSnapshot a = Snap(L"C:/s/A.jpg", 2, 0, 0);
    const MetadataSnapshot b = Snap(L"C:/s/B.jpg", 0, 0, 0);
    PhotoMetadataValues shown = queue.Record(a, MetadataFieldId::Rating, 3);
    Check(shown.rating.value == 3 && queue.Size() == 1, "first edit queued");
    (void)queue.Record(b, MetadataFieldId::PickState, 1);
    shown = queue.Record(Snap(L"c:/S/a.JPG", 3, 0, 0), MetadataFieldId::Rating, 5);
    Check(shown.rating.value == 5 && queue.Size() == 2, "same file (case-insensitive) coalesces; last value wins");
    const PendingMetadataEdit* entryA = queue.Find(L"C:/s/A.jpg");
    Check(entryA && entryA->before.values.rating.value == 2 && entryA->patch.rating.active &&
        entryA->patch.rating.requireExpected && entryA->patch.rating.expected.value == 2 &&
        entryA->patch.rating.desired == 5, "CAS expectation stays the first before value");
    (void)queue.Record(a, MetadataFieldId::ColorLabel, 4);
    Check(queue.Find(L"C:/s/A.jpg")->patch.ActiveCount() == 2 &&
        queue.Find(L"C:/s/A.jpg")->fields.size() == 2, "different fields merge into one patch");
    (void)queue.Record(a, MetadataFieldId::ColorLabel, 0);
    Check(queue.Find(L"C:/s/A.jpg")->patch.ActiveCount() == 1, "field set back to before becomes inactive");
    (void)queue.Record(a, MetadataFieldId::Rating, 2);
    Check(queue.Find(L"C:/s/A.jpg") == nullptr && queue.Size() == 1,
        "entry with nothing left to write is dropped");
    (void)queue.Record(a, MetadataFieldId::Rating, 1);
    const std::vector<PendingMetadataEdit> taken = queue.TakeAll();
    Check(taken.size() == 2 && taken[0].before.path == L"C:/s/B.jpg" && taken[1].before.path == L"C:/s/A.jpg" &&
        queue.Empty(), "per-file first-edit order preserved");

    (void)queue.Record(a, MetadataFieldId::Rating, 4);
    PhotoMetadataValues written = a.values;
    written.rating = KnownMetadataValue(4);
    Check(!queue.Rebase(L"C:/s/A.jpg", written) && queue.Empty(),
        "rebase onto a write that already produced the value drops the entry");
    (void)queue.Record(a, MetadataFieldId::Rating, 5);
    written.rating = KnownMetadataValue(3);
    Check(queue.Rebase(L"C:/s/A.jpg", written) && queue.Find(L"C:/s/A.jpg")->patch.rating.expected.value == 3,
        "rebase moves the CAS expectation to the finished write");
    Check(queue.RemovePath(L"C:/S/A.JPG").has_value() && queue.Empty(), "failed base removes the entry");

    // Requeue after a refused submit: order kept, newer values win, older base kept.
    (void)queue.Record(a, MetadataFieldId::Rating, 4);          // A: 2 -> 4
    (void)queue.Record(b, MetadataFieldId::PickState, 1);       // B
    std::vector<PendingMetadataEdit> refused = queue.TakeAll();
    MetadataSnapshot aShown = a;
    aShown.values.rating = KnownMetadataValue(4);
    (void)queue.Record(aShown, MetadataFieldId::Rating, 1);     // newer edit on A: 4 -> 1
    (void)queue.Record(Snap(L"C:/s/C.jpg", 0, 0, 0), MetadataFieldId::ColorLabel, 3);
    queue.Requeue(std::move(refused));
    const std::vector<PendingMetadataEdit> requeued = queue.TakeAll();
    Check(requeued.size() == 3 && requeued[0].before.path == L"C:/s/A.jpg" &&
        requeued[1].before.path == L"C:/s/B.jpg" && requeued[2].before.path == L"C:/s/C.jpg",
        "requeue keeps refused entries ahead of newer ones");
    Check(requeued[0].before.values.rating.value == 2 && requeued[0].patch.rating.desired == 1 &&
        requeued[0].patch.rating.expected.value == 2, "requeue: newer value, older on-disk base");

    MetadataSnapshot unknown = Snap(L"C:/s/U.jpg", 0, 0, 0);
    unknown.values.rating = UnknownMetadataValue();
    (void)queue.Record(unknown, MetadataFieldId::Rating, 0);
    Check(queue.Find(L"C:/s/U.jpg") && !queue.Find(L"C:/s/U.jpg")->patch.rating.requireExpected,
        "unknown before value writes without a CAS expectation (matches direct edits)");
}

void CheckErrorTable() {
    std::size_t count = 0;
    const UserErrorEntry* table = UserErrorTable(count);
    bool complete = count > 10;
    for (std::size_t index = 1; index < count; ++index)
        if (table[index].event.empty() || table[index].title.empty() || table[index].message.empty())
            complete = false;
    Check(complete, "every error has an event, title and message key");
    Check(UserError(UserErrorForWriteFailure(WriteFailureKind::InUse)).code == UserErrorCode::WriteInUseGaveUp &&
        UserError(UserErrorForWriteFailure(WriteFailureKind::Conflict)).code == UserErrorCode::WriteConflict &&
        UserError(UserErrorForWriteFailure(WriteFailureKind::ReadOnly)).code == UserErrorCode::WriteReadOnly,
        "write failure kinds map to user errors");
    Check(UserError(UserErrorCode::WriteInUseWaiting).message.find(L"{file}") != std::wstring_view::npos,
        "waiting message names the file");
}

// ---- Service integration with a fake writer ----

struct FakeDisk {
    std::mutex mutex;
    std::map<std::wstring, int> lockedAttempts; // path -> remaining in-use attempts (-1 = forever)
    std::map<std::wstring, int> ratings;
    std::vector<std::wstring> writeOrder;
    std::atomic<int> gates{ 0 };
    std::atomic<int> blockFirstWriteMs{ 0 };
};

tx::MetadataTransactionCallbacks MakeCallbacks(FakeDisk& disk) {
    tx::MetadataTransactionCallbacks callbacks;
    callbacks.retryTimeDivisor = 100; // 30 s -> 300 ms
    callbacks.beginExclusive = [&disk](const std::vector<std::filesystem::path>&, std::wstring&) {
        disk.gates.fetch_add(1);
        return true;
    };
    callbacks.endExclusive = [&disk](const std::vector<std::filesystem::path>&) { disk.gates.fetch_sub(1); };
    callbacks.write = [&disk](const std::filesystem::path& path, const MetadataPatch& patch,
        MetadataStorageKind, bool, bool) {
        const int sleepMs = disk.blockFirstWriteMs.exchange(0);
        if (sleepMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        tx::MetadataWriteOutcome outcome;
        std::lock_guard lock(disk.mutex);
        const std::wstring key = path.wstring();
        auto locked = disk.lockedAttempts.find(key);
        if (locked != disk.lockedAttempts.end() && locked->second != 0) {
            if (locked->second > 0) --locked->second;
            outcome.failureKind = WriteFailureKind::InUse;
            outcome.win32Error = 32;
            outcome.detail = L"sharing violation";
            return outcome;
        }
        int& rating = disk.ratings[key];
        outcome.before.rating = KnownMetadataValue(rating);
        if (patch.rating.requireExpected && patch.rating.expected.value != rating) {
            outcome.conflict = true;
            return outcome;
        }
        if (patch.rating.active) rating = patch.rating.desired;
        outcome.after.rating = KnownMetadataValue(rating);
        outcome.ok = true;
        disk.writeOrder.push_back(key);
        return outcome;
    };
    callbacks.stamps = [](const std::filesystem::path&) {
        tx::MetadataChange::FileStamps stamps;
        stamps.valid = true;
        stamps.fileSize = 42;
        return stamps;
    };
    return callbacks;
}

tx::MetadataTransactionPlan RatingPlan(std::initializer_list<std::pair<const wchar_t*, int>> items) {
    tx::MetadataTransactionPlan plan;
    plan.label = L"Rating";
    for (const auto& [path, rating] : items) {
        tx::MetadataChange change;
        change.before = Snap(path, 0, 0, 0);
        change.after = change.before;
        change.patch = MakeSingleFieldPatch(MetadataFieldId::Rating, rating);
        change.after.values = ApplyMetadataPatch(change.before.values, change.patch);
        plan.changes.push_back(std::move(change));
    }
    return plan;
}

std::optional<tx::MetadataTransactionResult> WaitResult(tx::MetadataTransactionService& service,
    std::chrono::milliseconds timeout = 5000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto result = service.TakeResult()) return result;
        std::this_thread::sleep_for(2ms);
    }
    return std::nullopt;
}

void CheckServiceRetries() {
    FakeDisk disk;
    tx::MetadataTransactionService service;
    service.Configure(MakeCallbacks(disk));
    disk.lockedAttempts[L"C:/s/locked.jpg"] = 3; // quick phase resolves it
    Check(service.Submit(RatingPlan({ { L"C:/s/locked.jpg", 4 } })), "submit");
    auto result = WaitResult(service);
    Check(result && !result->failed && result->completed[0] && result->changes[0].attempts == 4 &&
        disk.ratings[L"C:/s/locked.jpg"] == 4, "transient in-use resolved by quick retries");
    Check(result && result->changes[0].stamps.valid && result->changes[0].stamps.fileSize == 42,
        "stamps captured on the transaction thread");
    Check(!service.Busy(), "idle after take");

    disk.lockedAttempts[L"C:/s/held.jpg"] = -1; // another app keeps it
    Check(service.Submit(RatingPlan({ { L"C:/s/held.jpg", 2 }, { L"C:/s/free.jpg", 3 } })), "submit held");
    bool sawExternal = false;
    const auto deadline = std::chrono::steady_clock::now() + 2000ms;
    while (std::chrono::steady_clock::now() < deadline) {
        if (const auto progress = service.Progress(); progress && progress->waitingExternal &&
            progress->waitingPath == L"C:/s/held.jpg") { sawExternal = true; break; }
        std::this_thread::sleep_for(1ms);
    }
    Check(sawExternal, "progress reports the externally held file");
    result = WaitResult(service);
    Check(result && result->failed && !result->completed[0] && result->completed[1] &&
        result->inUseCount == 1 && result->changes[0].failureKind == WriteFailureKind::InUse &&
        result->changes[0].win32Error == 32, "gives up after the (scaled) 30 s; other files still written");

    // Skip while waiting.
    Check(service.Submit(RatingPlan({ { L"C:/s/held.jpg", 2 } })), "submit held again");
    std::this_thread::sleep_for(60ms);
    service.SkipWaiting();
    const auto skipStart = std::chrono::steady_clock::now();
    result = WaitResult(service);
    Check(result && !result->completed[0] && result->inUseCount == 1 &&
        std::chrono::steady_clock::now() - skipStart < 250ms, "Skip ends the wait promptly");

    // Cancel during a wait.
    Check(service.Submit(RatingPlan({ { L"C:/s/held.jpg", 2 }, { L"C:/s/free2.jpg", 1 } })), "submit for cancel");
    std::this_thread::sleep_for(40ms);
    service.CancelCurrent();
    result = WaitResult(service);
    Check(result && result->cancelled && !result->completed[1], "cancel during a wait stops at the boundary");

    // Conflict aborts immediately (no retries).
    disk.ratings[L"C:/s/conflict.jpg"] = 1;
    tx::MetadataTransactionPlan conflictPlan = RatingPlan({ { L"C:/s/conflict.jpg", 5 } });
    conflictPlan.changes[0].patch.rating.requireExpected = true;
    conflictPlan.changes[0].patch.rating.expected = KnownMetadataValue(3);
    Check(service.Submit(std::move(conflictPlan)), "submit conflict");
    result = WaitResult(service);
    Check(result && result->conflictCount == 1 && result->changes[0].attempts == 1 &&
        result->changes[0].failureKind == WriteFailureKind::Conflict, "conflict is never retried");
    Check(disk.gates.load() == 0, "gate released after every plan");

    // Stop during a long external wait is bounded.
    Check(service.Submit(RatingPlan({ { L"C:/s/held.jpg", 2 } })), "submit before stop");
    std::this_thread::sleep_for(40ms);
    const auto stopStart = std::chrono::steady_clock::now();
    service.Stop();
    Check(std::chrono::steady_clock::now() - stopStart < 500ms, "Stop interrupts a lock-resolution wait");
}

void CheckUrgentLane() {
    FakeDisk disk;
    tx::MetadataTransactionService service;
    service.Configure(MakeCallbacks(disk));
    // Outer "import" of 40 files; each write takes ~2 ms via the locked-once trick.
    std::vector<std::pair<const wchar_t*, int>> items;
    static std::vector<std::wstring> names;
    names.clear();
    for (int index = 0; index < 40; ++index) names.push_back(L"C:/s/imp" + std::to_wstring(index) + L".jpg");
    tx::MetadataTransactionPlan outer;
    outer.label = L"Import";
    for (const std::wstring& name : names) {
        tx::MetadataTransactionPlan one = RatingPlan({ { name.c_str(), 3 } });
        outer.changes.push_back(std::move(one.changes[0]));
        disk.lockedAttempts[name] = 1; // one 50 ms/100 = 1 ms retry each
    }
    disk.blockFirstWriteMs = 30;
    Check(service.Submit(std::move(outer)), "outer submitted");
    std::this_thread::sleep_for(5ms);
    Check(service.UrgentSlotFree(), "urgent slot free while outer runs");
    Check(service.SubmitUrgent(RatingPlan({ { L"C:/s/user.jpg", 5 } })), "urgent accepted");
    Check(!service.UrgentSlotFree(), "one urgent slot");
    auto first = WaitResult(service);
    Check(first && first->urgent && first->completed[0], "urgent edit on another file completes first (nested)");
    {
        std::lock_guard lock(disk.mutex);
        std::size_t position = 0;
        for (; position < disk.writeOrder.size(); ++position)
            if (disk.writeOrder[position] == L"C:/s/user.jpg") break;
        Check(position < 10, "urgent write interrupted the import within a few files");
    }
    auto second = WaitResult(service);
    Check(second && !second->urgent && !second->failed && second->changes.size() == 40,
        "outer import finishes afterwards");

    // Same-path urgent edit waits for the outer plan.
    disk.lockedAttempts[L"C:/s/same.jpg"] = 2;
    disk.blockFirstWriteMs = 20;
    Check(service.Submit(RatingPlan({ { L"C:/s/same.jpg", 2 }, { L"C:/s/other.jpg", 1 } })), "outer same");
    std::this_thread::sleep_for(5ms);
    tx::MetadataTransactionPlan urgent = RatingPlan({ { L"C:/s/same.jpg", 4 } });
    Check(service.SubmitUrgent(std::move(urgent)), "same-path urgent accepted");
    auto outerResult = WaitResult(service);
    auto urgentResult = WaitResult(service);
    Check(outerResult && !outerResult->urgent && urgentResult && urgentResult->urgent,
        "same-path urgent runs after the outer plan (never interleaved)");
    Check(disk.ratings[L"C:/s/same.jpg"] == 4, "last value wins on disk");
    Check(!service.Busy(), "idle after both results");
}

class NoFileOperations final : public tx::VerifiedFileOperations {
public:
    bool CopyVerified(const std::filesystem::path&, const std::filesystem::path&, tx::FileIdentity&,
        tx::FileIdentity&, std::wstring&) override { return false; }
    bool CopyIfUnchanged(const std::filesystem::path&, const std::filesystem::path&, const tx::FileIdentity&,
        tx::FileIdentity&, tx::FileIdentity&, std::wstring&) override { return false; }
    bool MoveVerified(const std::filesystem::path&, const std::filesystem::path&, tx::FileIdentity&,
        std::wstring&) override { return false; }
    bool MoveIfUnchanged(const std::filesystem::path&, const std::filesystem::path&, const tx::FileIdentity&,
        tx::FileIdentity&, std::wstring&) override { return false; }
    bool RemoveIfUnchanged(const std::filesystem::path&, const tx::FileIdentity&, std::wstring&) override { return false; }
    bool IsUnchanged(const std::filesystem::path&, const tx::FileIdentity&) override { return false; }
};

void CheckCoordinatorTokens() {
    tx::TransactionCoordinator coordinator(std::make_unique<NoFileOperations>());
    FakeDisk disk;
    coordinator.ConfigureMetadata(MakeCallbacks(disk));
    tx::MetadataTransactionPlan urgent = RatingPlan({ { L"C:/s/x.jpg", 1 } });
    Check(coordinator.SubmitUrgentMetadata(urgent) == 0, "urgent refused when nothing runs (use the normal lane)");
    disk.blockFirstWriteMs = 40;
    tx::MetadataTransactionPlan outer = RatingPlan({ { L"C:/s/y.jpg", 1 } });
    const std::uint64_t outerToken = coordinator.SubmitMetadata(outer);
    tx::MetadataTransactionPlan urgent2 = RatingPlan({ { L"C:/s/z.jpg", 2 } });
    const std::uint64_t urgentToken = coordinator.SubmitUrgentMetadata(urgent2);
    Check(outerToken != 0 && urgentToken != 0 && urgentToken != outerToken, "distinct tokens");
    int accepted = 0;
    const auto deadline = std::chrono::steady_clock::now() + 3000ms;
    while (accepted < 2 && std::chrono::steady_clock::now() < deadline) {
        if (auto result = coordinator.TakeMetadataResult()) {
            if (coordinator.AcceptMetadataResult(result->token)) ++accepted;
            Check(!coordinator.AcceptMetadataResult(result->token), "duplicate completion rejected");
        } else {
            std::this_thread::sleep_for(2ms);
        }
    }
    Check(accepted == 2 && !coordinator.Busy(), "both lanes' results accepted once");
    coordinator.Stop();
}

} // namespace

int main() {
    CheckClassification();
    CheckRetryStateMachine();
    CheckCoalescing();
    CheckErrorTable();
    CheckServiceRetries();
    CheckUrgentLane();
    CheckCoordinatorTokens();
    std::printf("write_lane_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
