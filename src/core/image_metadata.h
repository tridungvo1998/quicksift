// OWNER: The authoritative per-image metadata model ("metadata system").
//
// CatalogStore's PhotoItem records ARE the single source of truth for every
// metadata fact the app shows, filters, sorts, searches, imports or exports:
//   rating / color label / pick state (+ MetadataKnowledge per field)  - written only
//       by the metadata transaction pipeline (FinishMetadataTransaction) and the
//       external-change reconcile, read by everything else;
//   capture time (EXIF DateTimeOriginal, else file-modified time)      - hydrated
//       by the search-facts worker;
//   user comment                                                         - hydrated
//       from the comment store and updated in place by SaveUserComment.
// UI-thread readers use PhotoItem directly (FactsOf). Worker threads never touch
// PhotoItem: they receive an immutable, versioned CatalogSnapshot (shared_ptr to
// const) built by CatalogStore::Snapshot(), tagged with the store's MetadataEpoch so
// stale results are discarded. Portable; no Windows dependencies.
#pragma once

#include "app_types.h"
#include "photo_predicate.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace quicksift::core {

// Owning copy of one image's metadata facts (worker-safe).
struct ImageMetadataRecord {
    std::size_t catalogIndex = 0;
    std::wstring name;
    std::wstring comment;
    bool commentKnown = false;
    int rating = 0;
    int colorLabel = 0;
    int pickState = 0;
    MetadataKnowledge ratingKnowledge = MetadataKnowledge::Unknown;
    MetadataKnowledge colorLabelKnowledge = MetadataKnowledge::Unknown;
    MetadataKnowledge pickStateKnowledge = MetadataKnowledge::Unknown;
    predicate::LocalDateTime capture{};
    predicate::CaptureTimeSource captureSource = predicate::CaptureTimeSource::Unknown;
    int iso = -1;
    double aperture = 0.0;
    double focalLength = 0.0;
    double shutterSpeed = 0.0;
    std::wstring camera;
    std::wstring lens;
    bool cameraSettingsKnown = false;
};

// Immutable, versioned snapshot of the whole catalog's metadata facts.
// Records are held in shared, immutable chunks of kChunkSize. CatalogStore::Snapshot()
// rebuilds only the chunks whose records changed since the previous snapshot and
// shares every other chunk, so a refresh after one edit or one scan batch costs
// O(changed chunks), not O(catalog) (UI-thread budget, see ARCHITECTURE.md).
struct CatalogSnapshot {
    static constexpr std::size_t kChunkSize = 256;
    using Chunk = std::vector<ImageMetadataRecord>;
    std::uint64_t epoch = 0;
    std::size_t size = 0;
    std::vector<std::shared_ptr<const Chunk>> chunks; // record i: chunks[i / kChunkSize]
    [[nodiscard]] std::size_t Size() const noexcept { return size; }
    [[nodiscard]] bool Empty() const noexcept { return size == 0; }
    // record.catalogIndex == index
    [[nodiscard]] const ImageMetadataRecord& Record(std::size_t index) const {
        return (*chunks[index / kChunkSize])[index % kChunkSize];
    }
};
using CatalogSnapshotPtr = std::shared_ptr<const CatalogSnapshot>;

[[nodiscard]] inline predicate::MetadataFactsView FactsOf(const PhotoItem& photo) noexcept {
    predicate::MetadataFactsView facts;
    facts.name = photo.name;
    facts.comment = photo.userComment;
    facts.commentKnown = photo.userCommentKnown;
    facts.rating = photo.rating;
    facts.colorLabel = photo.colorLabel;
    facts.pickState = photo.pickState;
    facts.ratingKnowledge = photo.ratingKnowledge;
    facts.colorLabelKnowledge = photo.colorLabelKnowledge;
    facts.pickStateKnowledge = photo.pickStateKnowledge;
    facts.capture = photo.captureTime;
    facts.captureSource = photo.captureTimeSource;
    facts.iso = photo.iso;
    facts.aperture = photo.aperture;
    facts.focalLength = photo.focalLength;
    facts.shutterSpeed = photo.shutterSpeed;
    facts.camera = photo.camera;
    facts.lens = photo.lens;
    facts.cameraSettingsKnown = photo.cameraSettingsKnown;
    return facts;
}

[[nodiscard]] inline predicate::MetadataFactsView FactsOf(const ImageMetadataRecord& record) noexcept {
    predicate::MetadataFactsView facts;
    facts.name = record.name;
    facts.comment = record.comment;
    facts.commentKnown = record.commentKnown;
    facts.rating = record.rating;
    facts.colorLabel = record.colorLabel;
    facts.pickState = record.pickState;
    facts.ratingKnowledge = record.ratingKnowledge;
    facts.colorLabelKnowledge = record.colorLabelKnowledge;
    facts.pickStateKnowledge = record.pickStateKnowledge;
    facts.capture = record.capture;
    facts.captureSource = record.captureSource;
    facts.iso = record.iso;
    facts.aperture = record.aperture;
    facts.focalLength = record.focalLength;
    facts.shutterSpeed = record.shutterSpeed;
    facts.camera = record.camera;
    facts.lens = record.lens;
    facts.cameraSettingsKnown = record.cameraSettingsKnown;
    return facts;
}

[[nodiscard]] inline ImageMetadataRecord RecordOf(const PhotoItem& photo, std::size_t catalogIndex) {
    ImageMetadataRecord record;
    record.catalogIndex = catalogIndex;
    record.name = photo.name;
    record.comment = photo.userComment;
    record.commentKnown = photo.userCommentKnown;
    record.rating = photo.rating;
    record.colorLabel = photo.colorLabel;
    record.pickState = photo.pickState;
    record.ratingKnowledge = photo.ratingKnowledge;
    record.colorLabelKnowledge = photo.colorLabelKnowledge;
    record.pickStateKnowledge = photo.pickStateKnowledge;
    record.capture = photo.captureTime;
    record.captureSource = photo.captureTimeSource;
    record.iso = photo.iso;
    record.aperture = photo.aperture;
    record.focalLength = photo.focalLength;
    record.shutterSpeed = photo.shutterSpeed;
    record.camera = photo.camera;
    record.lens = photo.lens;
    record.cameraSettingsKnown = photo.cameraSettingsKnown;
    return record;
}

// True when two records agree on every fact a predicate can test.
[[nodiscard]] inline bool SameMetadataFacts(const PhotoItem& left, const PhotoItem& right) noexcept {
    return left.rating == right.rating && left.colorLabel == right.colorLabel &&
        left.pickState == right.pickState && left.ratingKnowledge == right.ratingKnowledge &&
        left.colorLabelKnowledge == right.colorLabelKnowledge &&
        left.pickStateKnowledge == right.pickStateKnowledge &&
        left.captureTimeSource == right.captureTimeSource && left.captureTime == right.captureTime &&
        left.userCommentKnown == right.userCommentKnown && left.userComment == right.userComment &&
        left.name == right.name && left.extension == right.extension && left.modified == right.modified &&
        left.iso == right.iso && left.aperture == right.aperture && left.focalLength == right.focalLength &&
        left.shutterSpeed == right.shutterSpeed && left.camera == right.camera && left.lens == right.lens &&
        left.cameraSettingsKnown == right.cameraSettingsKnown;
}

} // namespace quicksift::core
