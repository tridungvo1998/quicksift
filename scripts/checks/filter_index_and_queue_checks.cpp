// OWNER: Portable checks for the incremental filter-menu value index (kept by
// CatalogStore) and the visible-first metadata queue ordering.
#include "core/catalog_store.h"
#include "core/filter_value_index.h"
#include "core/metadata_queue_order.h"

#include <cstdio>
#include <deque>
#include <limits>
#include <vector>

namespace {
int g_checks = 0;
int g_failures = 0;
void Check(bool condition, const char* name) {
    ++g_checks;
    if (!condition) { ++g_failures; std::printf("FAIL: %s\n", name); }
}
using namespace quicksift::core;
namespace mo = quicksift::core::metadata_order;

PhotoItem Photo(const wchar_t* path) {
    PhotoItem photo;
    photo.path = path;
    photo.name = path;
    return photo;
}
void SetCamera(PhotoItem& p, int iso, double aperture, double focal, double shutter, const wchar_t* camera) {
    p.cameraSettingsKnown = true; p.iso = iso; p.aperture = aperture; p.focalLength = focal;
    p.shutterSpeed = shutter; p.camera = camera;
}
FilterValueIndex Rebuilt(const CatalogStore& store) {
    FilterValueIndex index;
    for (const PhotoItem& photo : store.Photos()) index.Add(photo);
    return index;
}
} // namespace

int main() {
    {
        CatalogStore store;
        PhotoItem a = Photo(L"C:\\f\\a.jpg");
        SetCamera(a, 400, 2.8, 50.0, 1.0 / 160.0, L"Canon EOS R5");
        PhotoItem b = Photo(L"C:\\f\\b.jpg");
        SetCamera(b, 400, 4.0, 70.0, 2.0, L"Nikon Z8");
        const std::vector<PhotoItem> batch{ a, b, Photo(L"C:\\f\\c.jpg") };
        (void)store.AppendBatch(batch);
        const auto& values = store.FilterValues();
        Check(values.Iso() == std::vector<int>{ 400 }, "ISO deduplicated across photos");
        Check(values.ApertureCents() == std::vector<int>({ 280, 400 }), "aperture keys sorted");
        Check(values.ShutterKeys() == std::vector<int>({ -2, 160 }), "shutter keys: seconds negative, 1/x reciprocal");
        Check(values.Cameras().size() == 2, "two cameras");
        Check(values.Dates().empty() && values.Hours().empty(), "unknown capture contributes no date/hour");

        // Fact arrives for c (as the background hydration does through EditPhoto).
        store.EditPhoto(2, [](PhotoItem& p) {
            p.captureTimeSource = predicate::CaptureTimeSource::Exif;
            p.captureTime.year = 2026; p.captureTime.month = 3; p.captureTime.day = 4; p.captureTime.hour = 9;
            SetCamera(p, 800, 1.8, 35.0, 1.0 / 1000.0, L"Sony A7 IV");
        });
        Check(store.FilterValues().Dates() == std::vector<std::wstring>{ L"2026-03-04" }, "date added incrementally");
        Check(store.FilterValues().Hours() == std::vector<int>{ 9 }, "hour added incrementally");
        Check(store.FilterValues().Iso() == std::vector<int>({ 400, 800 }), "ISO added incrementally");

        // Value changes (file edited and re-read): old value leaves when its last user goes.
        store.EditPhoto(0, [](PhotoItem& p) { p.iso = 100; });
        Check(store.FilterValues().Iso() == std::vector<int>({ 100, 400, 800 }), "ISO 400 kept while b still uses it");
        store.EditPhoto(1, [](PhotoItem& p) { p.cameraSettingsKnown = false; });
        Check(store.FilterValues().Iso() == std::vector<int>({ 100, 800 }), "ISO 400 removed with its last photo");
        Check(store.FilterValues().Cameras().size() == 2, "camera removed when settings become unknown");

        // Unrelated edits (ratings) and renames do not disturb the index.
        store.EditPhoto(0, [](PhotoItem& p) { p.rating = 5; });
        store.ReplacePath(0, L"C:\\f\\renamed.jpg");
        Check(store.FilterValues() == Rebuilt(store), "incremental index equals a full rebuild");

        store.ClearAndRelease();
        Check(store.FilterValues().Iso().empty() && store.FilterValues().Dates().empty(), "folder change clears index");
    }
    {
        Check(FilterValueIndex::DateKey({ 2026, 1, 9 }) == L"2026-01-09", "date key zero-padded");
    }
    {
        const auto window = mo::WindowAround(10, 19, 100, 10, 20);
        Check(window.first == 0 && window.last == 39, "window extends before/after and clamps at 0");
        Check(mo::WindowAround(90, 99, 100, 10, 20).last == 99, "window clamps at the end");
        Check(mo::WindowAround(0, 0, 0, 5, 5).Empty(), "empty catalog: empty window");
        Check(mo::WindowAround(5, 4, 100, 5, 5).Empty(), "inverted shown range: empty window");

        std::deque<std::size_t> queue{ 0, 1, 2, 3, 4, 5, 6, 7 };
        // Visible position = catalog index reversed; index 7 filtered out.
        const auto positionOf = [](std::size_t index) {
            return index == 7 ? std::numeric_limits<std::size_t>::max() : 6 - index;
        };
        mo::VisibleWindow shown; shown.first = 0; shown.last = 2; // positions 0..2 -> indexes 6,5,4
        const std::size_t promoted = mo::PromoteWindow(queue, shown, positionOf);
        Check(promoted == 3, "three entries promoted");
        Check(queue == std::deque<std::size_t>({ 4, 5, 6, 0, 1, 2, 3, 7 }), "stable: promoted and rest keep their order");
        Check(queue.size() == 8, "no entries added or lost (pending counter stays paired)");
        std::deque<std::size_t> duplicates{ 3, 3, 1 };
        mo::VisibleWindow only3; only3.first = 3; only3.last = 3;
        mo::PromoteWindow(duplicates, only3, [](std::size_t i) { return i; });
        Check(duplicates == std::deque<std::size_t>({ 3, 3, 1 }), "duplicate entries are both kept");
        std::deque<std::size_t> untouched{ 2, 1 };
        Check(mo::PromoteWindow(untouched, mo::VisibleWindow{}, [](std::size_t i) { return i; }) == 0 &&
            untouched == std::deque<std::size_t>({ 2, 1 }), "empty window leaves the queue alone");
    }
    std::printf("filter_index_and_queue_checks: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
