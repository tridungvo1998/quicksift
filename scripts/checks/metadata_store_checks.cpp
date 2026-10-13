// OWNER: Deterministic, portable checks for the unified authoritative metadata model:
// CatalogStore metadata epoch + immutable snapshots, the Filter tab running on the
// shared predicate engine (checked against an independent reference implementation
// of the pre-0.9.4.0 filter semantics, exhaustively), and the export/import token
// table delegating to the canonical vocabulary.
//
// Not a CTest suite (CTest stays retired). Build against the portable core library:
//
//   cmake -S . -B /tmp/qs-build && cmake --build /tmp/qs-build
//   g++ -std=c++20 -O1 -Wall -Wextra -Isrc scripts/checks/metadata_store_checks.cpp
//       /tmp/qs-build/libQuickSiftCore.a -o /tmp/metadata_store_checks
//   /tmp/metadata_store_checks
//
//   MSVC: cl /std:c++20 /EHsc /W4 /utf-8 /Isrc scripts\checks\metadata_store_checks.cpp
//       <build>\QuickSiftCore.lib

#include "core/catalog_policy.h"
#include "core/catalog_store.h"
#include "core/image_metadata.h"
#include "core/metadata_vocabulary.h"
#include "core/ratings_exchange.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace quicksift::core;

int gFailures = 0;
int gChecks = 0;

void Check(bool condition, const char* label) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL: %s\n", label);
    }
}

PhotoItem MakePhoto(const wchar_t* name, int rating, int pick, int color, MetadataKnowledge knowledge) {
    PhotoItem photo;
    photo.path = std::wstring(L"C:/shoot/") + name;
    photo.name = name;
    photo.extension = L".jpg";
    photo.rating = rating;
    photo.pickState = pick;
    photo.colorLabel = color;
    photo.ratingKnowledge = knowledge;
    photo.pickStateKnowledge = knowledge;
    photo.colorLabelKnowledge = knowledge;
    return photo;
}

// Independent reference of the Filter tab semantics before the unification
// (active rating/pick/label filters require Known values).
bool ReferenceFilter(const PhotoItem& item, RatingFilter rating, PickFilter pick, ColorLabelFilter label) {
    if (rating != RatingFilter::All) {
        if (item.ratingKnowledge != MetadataKnowledge::Known) return false;
        switch (rating) {
        case RatingFilter::Unrated: if (item.rating != 0) return false; break;
        case RatingFilter::Exact1: if (item.rating != 1) return false; break;
        case RatingFilter::Exact2: if (item.rating != 2) return false; break;
        case RatingFilter::Exact3: if (item.rating != 3) return false; break;
        case RatingFilter::Exact4: if (item.rating != 4) return false; break;
        case RatingFilter::Exact5: if (item.rating != 5) return false; break;
        case RatingFilter::AtLeast4: if (item.rating < 4) return false; break;
        case RatingFilter::All: break;
        }
    }
    if (pick != PickFilter::All) {
        if (item.pickStateKnowledge != MetadataKnowledge::Known) return false;
        if (pick == PickFilter::Picks && item.pickState <= 0) return false;
        if (pick == PickFilter::Rejects && item.pickState >= 0) return false;
        if (pick == PickFilter::Unmarked && item.pickState != 0) return false;
    }
    if (label != ColorLabelFilter::All) {
        if (item.colorLabelKnowledge != MetadataKnowledge::Known) return false;
        if (item.colorLabel != static_cast<int>(label) - 1) return false;
    }
    return true;
}

void CheckFilterEquivalence() {
    std::vector<PhotoItem> photos;
    const MetadataKnowledge knowledges[] = { MetadataKnowledge::Unknown, MetadataKnowledge::Known,
        MetadataKnowledge::Unsupported };
    for (MetadataKnowledge knowledge : knowledges)
        for (int rating = 0; rating <= 5; ++rating)
            for (int pick = -1; pick <= 1; ++pick)
                for (int color = 0; color <= 5; ++color)
                    photos.push_back(MakePhoto(L"x.jpg", rating, pick, color, knowledge));
    bool same = true;
    int combos = 0;
    for (int r = 0; r <= 7; ++r)
        for (int p = 0; p <= 3; ++p)
            for (int c = 0; c <= 6; ++c) {
                ++combos;
                for (const PhotoItem& photo : photos) {
                    const bool now = IsCullAllowed(photo, static_cast<RatingFilter>(r),
                        static_cast<PickFilter>(p), static_cast<ColorLabelFilter>(c));
                    const bool before = ReferenceFilter(photo, static_cast<RatingFilter>(r),
                        static_cast<PickFilter>(p), static_cast<ColorLabelFilter>(c));
                    if (now != before) same = false;
                }
            }
    Check(combos == 8 * 4 * 7, "all 224 filter combinations exercised");
    Check(same, "Filter tab on the shared predicate engine == pre-unification semantics");
    // Mixed knowledge per field.
    PhotoItem mixed = MakePhoto(L"m.jpg", 5, 1, 2, MetadataKnowledge::Known);
    mixed.pickStateKnowledge = MetadataKnowledge::Unknown;
    Check(IsCullAllowed(mixed, RatingFilter::Exact5, PickFilter::All, ColorLabelFilter::Yellow),
        "unknown pick does not affect rating/label filters");
    Check(!IsCullAllowed(mixed, RatingFilter::Exact5, PickFilter::Picks, ColorLabelFilter::All),
        "unknown pick fails an active pick filter");
}

void CheckStoreEpochAndSnapshot() {
    CatalogStore store;
    const std::uint64_t start = store.MetadataEpoch();
    const auto indexA = store.Append(MakePhoto(L"a.jpg", 3, 0, 0, MetadataKnowledge::Known));
    const auto indexB = store.Append(MakePhoto(L"b.jpg", 0, 1, 4, MetadataKnowledge::Known));
    Check(indexA == 0 && indexB == 1, "append indices");
    const std::uint64_t afterAppend = store.MetadataEpoch();
    Check(afterAppend > start, "Append bumps the metadata epoch");

    const CatalogSnapshotPtr first = store.Snapshot();
    Check(first && first->epoch == afterAppend && first->Size() == 2, "snapshot epoch + size");
    Check(first->Record(1).catalogIndex == 1 && first->Record(1).name == L"b.jpg" &&
        first->Record(1).colorLabel == 4, "snapshot record content");
    Check(store.Snapshot() == first, "snapshot cached within an epoch");

    store.EditPhoto(0, [](PhotoItem& photo) { photo.sourceWidth = 6000; photo.facesScanned = true; });
    Check(store.MetadataEpoch() == afterAppend, "non-fact edits (size/faces) keep the epoch");
    store.EditPhoto(0, [](PhotoItem& photo) { photo.rating = 3; });
    Check(store.MetadataEpoch() == afterAppend, "no-op fact edit keeps the epoch");

    store.EditPhoto(0, [](PhotoItem& photo) { photo.rating = 5; });
    const std::uint64_t afterRating = store.MetadataEpoch();
    Check(afterRating > afterAppend, "rating edit bumps the epoch");
    const CatalogSnapshotPtr second = store.Snapshot();
    Check(second != first && second->Record(0).rating == 5, "new snapshot after a fact change");
    Check(first->Record(0).rating == 3, "old snapshot is immutable (workers keep a stable view)");

    store.EditPhoto(1, [](PhotoItem& photo) { photo.userComment = L"bride"; photo.userCommentKnown = true; });
    Check(store.MetadataEpoch() > afterRating, "comment hydration bumps the epoch");
    const std::uint64_t afterComment = store.MetadataEpoch();
    store.EditPhoto(1, [](PhotoItem& photo) {
        photo.captureTime = { 2026, 10, 9, 8, 15, 0 };
        photo.captureTimeSource = predicate::CaptureTimeSource::Exif;
    });
    Check(store.MetadataEpoch() > afterComment, "capture-time hydration bumps the epoch");
    const CatalogSnapshotPtr third = store.Snapshot();
    const predicate::MetadataFactsView facts = FactsOf(third->Record(1));
    Check(facts.commentKnown && facts.comment == L"bride" && facts.capture.hour == 8 &&
        facts.captureSource == predicate::CaptureTimeSource::Exif, "snapshot carries search facts");
    Check(SameMetadataFacts(store.PhotoAt(1), store.PhotoAt(1)), "SameMetadataFacts reflexive");

    store.ClearAndRelease();
    Check(store.MetadataEpoch() > afterComment && store.Snapshot()->Empty(), "clear bumps + empties");
}

void CheckIncrementalSnapshotChunks() {
    // 600 photos = chunks [0,256) [256,512) [512,600).
    CatalogStore store;
    std::vector<PhotoItem> batch;
    for (int index = 0; index < 600; ++index)
        batch.push_back(MakePhoto((L"p" + std::to_wstring(index) + L".jpg").c_str(), index % 6, 0, 0,
            MetadataKnowledge::Known));
    (void)store.AppendBatch(batch);
    const CatalogSnapshotPtr base = store.Snapshot();
    Check(base->Size() == 600 && base->chunks.size() == 3, "600 records in 3 chunks");
    bool indexesOk = true;
    for (std::size_t index = 0; index < base->Size(); ++index)
        if (base->Record(index).catalogIndex != index) indexesOk = false;
    Check(indexesOk, "Record(i).catalogIndex == i across chunks");

    store.EditPhoto(300, [](PhotoItem& photo) { photo.rating = 5; photo.ratingKnowledge = MetadataKnowledge::Known; });
    const CatalogSnapshotPtr edited = store.Snapshot();
    Check(edited->chunks[0] == base->chunks[0] && edited->chunks[2] == base->chunks[2],
        "untouched chunks are shared, not copied");
    Check(edited->chunks[1] != base->chunks[1] && edited->Record(300).rating == 5 &&
        base->Record(300).rating == 0, "only the edited chunk is rebuilt; old snapshot immutable");

    (void)store.Append(MakePhoto(L"tail.jpg", 1, 0, 0, MetadataKnowledge::Known));
    const CatalogSnapshotPtr grown = store.Snapshot();
    Check(grown->Size() == 601 && grown->chunks[0] == edited->chunks[0] &&
        grown->chunks[1] == edited->chunks[1] && grown->chunks[2] != edited->chunks[2] &&
        grown->Record(600).name == L"tail.jpg", "append rebuilds only the tail chunk");

    std::vector<PhotoItem> more;
    for (int index = 0; index < 300; ++index)
        more.push_back(MakePhoto((L"q" + std::to_wstring(index) + L".jpg").c_str(), 2, 0, 0, MetadataKnowledge::Known));
    (void)store.AppendBatch(more);
    const CatalogSnapshotPtr big = store.Snapshot();
    Check(big->Size() == 901 && big->chunks.size() == 4 && big->chunks[0] == grown->chunks[0] &&
        big->Record(900).name == L"q299.jpg", "batch append grows chunks, shares the head");

    store.ReplacePath(5, L"renamed.jpg");
    const CatalogSnapshotPtr renamed = store.Snapshot();
    Check(renamed->chunks[0] != big->chunks[0] && renamed->chunks[1] == big->chunks[1],
        "rename marks only its chunk");
    store.ClearAndRelease();
    (void)store.Append(MakePhoto(L"fresh.jpg", 4, 0, 0, MetadataKnowledge::Known));
    const CatalogSnapshotPtr fresh = store.Snapshot();
    Check(fresh->Size() == 1 && fresh->Record(0).name == L"fresh.jpg", "no stale chunk after clear");
}

void CheckExchangeVocabulary() {
    namespace vocab = vocabulary;
    namespace rx = ratings_exchange;
    bool same = true;
    for (const vocab::PickEntry& pick : vocab::kPicks) {
        if (rx::PickToken(pick.value) != vocab::PickExportToken(pick.value)) same = false;
        const auto parsed = rx::ParsePickToken(rx::PickToken(pick.value));
        if (!parsed || *parsed != pick.value) same = false;
    }
    for (const vocab::ColorEntry& color : vocab::kColors) {
        if (rx::ColorToken(color.value) != vocab::ColorExportToken(color.value)) same = false;
        const auto parsed = rx::ParseColorToken(rx::ColorToken(color.value));
        if (!parsed || *parsed != color.value) same = false;
    }
    for (int stars = vocab::kMinStars; stars <= vocab::kMaxStars; ++stars) {
        const auto parsed = rx::ParseStarToken(rx::StarToken(stars));
        if (!parsed || *parsed != stars) same = false;
    }
    Check(same, "ratings exchange tokens == canonical vocabulary, round-trip");
    Check(rx::PickToken(1) == "Picked" && rx::PickToken(-1) == "Rejected" && rx::PickToken(0) == "None" && rx::ColorToken(1) == "Red" &&
        rx::ColorToken(5) == "Purple", "export file tokens unchanged");
    Check(!rx::IsValidColorLabel(6) && rx::IsValidColorLabel(0) && !rx::IsValidStarRating(6),
        "validation uses the vocabulary ranges");
    Check(vocab::PickSign(7) == 1 && vocab::PickSign(-3) == -1 && vocab::PickSign(0) == 0, "PickSign");
}

} // namespace

int main() {
    CheckFilterEquivalence();
    CheckStoreEpochAndSnapshot();
    CheckIncrementalSnapshotChunks();
    CheckExchangeVocabulary();
    std::printf("metadata_store_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
