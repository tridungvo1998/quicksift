// OWNER: Deterministic optimization invariants, not timing benchmarks.
#include "core/bitmap_cache_key.h"
#include "core/cache_eviction_policy.h"
#include "core/compare_layout_key.h"
#include "core/derivative_space_budget.h"
#include "core/queue_append_transaction.h"
#include "core/shell_preview_policy.h"
#include "core/snapshot_writer.h"
#include "core/catalog_store.h"
#include "review/review_state_model.h"
#include <array>
#include <cstdio>
#include <deque>
#include <future>
#include <limits>
#include <stdexcept>
#include <unordered_map>
using namespace quicksift::core;
namespace {
int checks = 0, failures = 0;
void Check(bool ok, const char* name) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", name); } }
}
int main() {
    {
        std::unordered_map<CacheKey, int, CacheKeyHash, CacheKeyEqual> cache;
        const std::wstring path = L"C:\\shoot\\Ảnh.RAW";
        cache.emplace(CacheKey{path, 512, CacheClass::Thumbnail}, 7);
        Check(cache.find(CacheKeyView{path, 512, CacheClass::Thumbnail})->second == 7, "transparent key hit");
        Check(CacheKeyHash{}(CacheKey{path, 512, CacheClass::Thumbnail}) == CacheKeyHash{}(CacheKeyView{path, 512, CacheClass::Thumbnail}), "owning/view hashes agree");
        Check(!cache.contains(CacheKeyView{path, 1024, CacheClass::Thumbnail}), "size distinguishes cache identity");
        Check(!cache.contains(CacheKeyView{path, 512, CacheClass::Full}), "class distinguishes identity");
        Check(!cache.contains(CacheKeyView{L"c:\\shoot\\Ảnh.RAW", 512, CacheClass::Thumbnail}), "existing case-sensitive cache semantics preserved");
    }
    {
        CompareLayoutKey key;
        std::array<float, 5> geometry{0, 0, 1000, 700, 96};
        std::array<std::wstring, 2> paths{L"a", L"b"}; std::array<float, 2> aspects{1.5f, 1.0f};
        Check(!key.Matches(geometry, 1, 2, paths, aspects), "layout initially invalid");
        key.Assign(geometry, 1, 2, paths, aspects);
        Check(key.Matches(geometry, 1, 2, paths, aspects), "unchanged layout reused");
        for (std::size_t i = 0; i < geometry.size(); ++i) { auto changed = geometry; changed[i] += 1; Check(!key.Matches(changed, 1, 2, paths, aspects), "bounds/DPI invalidate"); }
        Check(!key.Matches(geometry, 2, 2, paths, aspects), "folder generation invalidates");
        Check(!key.Matches(geometry, 1, 1, paths, aspects), "mode invalidates");
        aspects[0] = 0.75f; Check(!key.Matches(geometry, 1, 2, paths, aspects), "async geometry/rotation invalidates"); aspects[0] = 1.5f;
        std::swap(paths[0], paths[1]); Check(!key.Matches(geometry, 1, 2, paths, aspects), "ordered identities invalidate");
        key.Invalidate(); Check(!key.Matches(geometry, 1, 2, paths, aspects), "explicit device invalidation");
    }
    {
        ShellPreviewIdentity id{1, 100, 200, 256};
        Check(!ShellPreviewResultCurrent(id, id, 2, 1) && ShellPreviewResultCurrent(id, id, 2, 2), "late completion after explicit retry/device reset cannot settle a new request");
        Check(ShellPreviewReusable(id, id, true, 29999), "negative cache suppresses repeated handler calls");
        Check(!ShellPreviewReusable(id, id, true, 30000), "negative cache expires");
        for (int field = 0; field < 4; ++field) {
            auto changed = id;
            if (field == 0) ++changed.generation; if (field == 1) ++changed.fileSize;
            if (field == 2) ++changed.modifiedStamp; if (field == 3) ++changed.target;
            Check(!ShellPreviewReusable(id, changed, false, 0), "identity/size/folder change invalidates Shell cache");
        }
    }
    {
        std::promise<void> entered, release;
        auto released = release.get_future().share();
        std::vector<std::wstring> writes;
        SnapshotWriter writer([&](const TextSnapshot& s) {
            writes.push_back(s.document);
            if (s.document == L"a") { entered.set_value(); released.wait(); }
            return s.document != L"fail";
        });
        writer.Submit({L"session", L"a"}); entered.get_future().wait();
        writer.Submit({L"session", L"b"}); const auto final = writer.Submit({L"session", L"c"}); release.set_value();
        Check(writer.Drain(final), "final-save drain observes completion");
        Check(writes == std::vector<std::wstring>{L"a", L"c"}, "pending snapshots coalesce, serialize and preserve latest order");
        Check(writer.Drain(writer.Submit({L"session", L"c"})), "unchanged save succeeds");
        Check(writes.size() == 2, "unchanged successful content skips disk write");
        Check(!writer.Drain(writer.Submit({L"session", L"fail"})), "final write failure observable");
        Check(!writer.Drain(writer.Submit({L"session", L"fail"})), "failed identical content retried");
        Check(writes.size() == 4, "failed writes are not memoized");
        writer.Submit({L"session", L"last"}); writer.Stop();
        Check(writes.back() == L"last", "shutdown drains queued immutable settings");
        Check(writer.Submit({L"session", L"late"}) == 0, "shutdown refuses new submissions");
    }
    {
        std::deque<std::size_t> queue{1, 2};
        try { QueueAppendTransaction guard(queue); queue.push_back(3); queue.push_back(4); throw std::runtime_error("commit fails"); } catch (...) {}
        Check(queue == std::deque<std::size_t>{1, 2}, "failed preparation/commit rolls queue back");
        { QueueAppendTransaction guard(queue); queue.push_back(3); guard.Commit(); }
        Check(queue == std::deque<std::size_t>{1, 2, 3}, "successful catalog commit retains appended queue");
        Check(ScanBatchTarget(64) == 256 && ScanBatchTarget(8192) == 1024 && ScanBatchTarget(1000000) == 4096, "progressive scan growth is bounded");
        CatalogStore catalog; PhotoItem photo; photo.path = L"a.jpg";
        static_cast<void>(catalog.AppendBatchWithVisibleOrder(std::span<const PhotoItem>(&photo, 1), {0}));
        try { static_cast<void>(catalog.AppendBatchWithVisibleOrder(std::span<const PhotoItem>(&photo, 1), {0, 999})); Check(false, "invalid order throws"); } catch (const std::out_of_range&) {}
        Check(catalog.PhotoCount() == 1 && catalog.VisibleCount() == 1 && catalog.ValidateVisibleOrder(), "invalid scan commit preserves catalog and ordering");
    }
    {
        DerivativeSpaceBudget space; const auto now = DerivativeSpaceBudget::Clock::time_point{};
        constexpr std::uint64_t minimum = 2ull * 1024 * 1024 * 1024;
        Check(space.NeedsRefresh(1, minimum, now), "space query required initially");
        Check(!space.Reserve(1, minimum), "unknown free space fails closed");
        space.Observe(minimum + 64ull * 1024 * 1024, now);
        Check(space.Reserve(1024, minimum), "room for payload plus overhead");
        Check(!space.NeedsRefresh(1024, minimum, now), "small derivative reuses bounded observation");
        Check(space.NeedsRefresh(1024, minimum, now + std::chrono::milliseconds(250)), "250ms refresh ceiling");
        Check(space.NeedsRefresh(DerivativeSpaceBudget::kBatchBytes, minimum, now), "large derivative refreshes immediately");
        space.Observe(minimum + 1024, now);
        Check(!space.Reserve(1024, minimum), "free-space reserve includes file overhead");
        Check(space.NeedsRefresh(1, minimum, now), "low-space condition never amortized");
        Check(!space.Reserve(std::numeric_limits<std::uint64_t>::max(), minimum), "overflowing payload rejected");
        space.Invalidate(); Check(space.NeedsRefresh(1, minimum, now), "storage reset invalidates observation");
    }
    {
        Check(!EvictionEligible(true, false) && EvictionEligible(false, false) && EvictionEligible(true, true), "protected images only evicted on explicit force");
        Check(EvictionReservationLimit(100, 40) == 60 && EvictionReservationLimit(100, 100) == 0 && EvictionReservationLimit(100, 1000) == 0, "incoming-byte cap cannot underflow");
        Check(EvictionRankLess({false, true, true, 99}, {true, false, false, 1}), "recency grace outranks frequency/face");
        Check(EvictionRankLess({false, false, true, 99}, {false, true, false, 1}), "face preference retained");
        Check(EvictionRankLess({false, false, false, 99}, {false, false, true, 1}), "frequency preference retained");
        Check(EvictionRankLess({false, false, false, 1}, {false, false, false, 2}), "LRU tie-break preserved");
    }
    {
        quicksift::review::ReviewStateModel state;
        // The legacy set overload remains for compatibility; the new predicate only probes compared paths.
        std::size_t probes = 0;
        state.RetainComparedPaths([&](const std::wstring&) { ++probes; return true; });
        Check(probes == 0, "no whole-folder visibility materialization outside Compare");
        Check(state.EnterCompare({L"a", L"b", L"c"}), "Compare entry unchanged");
        state.SetSyncCompareView(false);
        state.SetActiveComparePath(L"a");
        state.RetainComparedPaths([](const std::wstring& path) { return path != L"a"; });
        Check(state.ComparePaths() == std::vector<std::wstring>{L"b", L"c"} && state.ActiveComparePath() == L"b", "predicate retention cleans removed active pane");
        Check(state.RetainComparedPaths([](const std::wstring& path) { return path == L"b"; }) && state.ComparePaths().empty(), "fewer than two visible photos exits Compare and clears panes");
    }
    std::printf("optimization_checks: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
