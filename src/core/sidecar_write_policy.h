// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#pragma once
// XMP sidecar write plan (portable; checked by scripts/checks/sidecar_write_checks.cpp).
//
// Sidecar writes (the default for RAW) used to read the photo's metadata from
// EVERY source twice per edit (before and after): the sidecar, then a full Exiv2
// parse of the RAW and a Windows Shell property-store query when the sidecar did
// not answer every field. That is hundreds of ms to seconds per rating on large
// RAWs. Now:
//  * the sidecar text already in memory answers the "before" values; the full
//    read runs only when the sidecar does not answer every field;
//  * every Known value the sidecar lacks is written along with the edit
//    (rating and pick always, the label when set), so the next read and write of
//    that RAW are sidecar-only;
//  * verification re-reads only the sidecar.
#include "core/metadata_state.h"

#include <vector>

namespace quicksift::core::sidecar_write {

// QuickSift-written sidecars always carry xmp:Rating and quicksift:Pick, so for
// them an absent xmp:Label means "no label" (not "ask the RAW").
inline void CompleteQuickSiftSidecar(PhotoMetadataValues& values) {
    if (values.rating.IsAuthoritative() && values.pickState.IsAuthoritative() &&
        values.colorLabel.knowledge == MetadataKnowledge::Unknown) {
        values.colorLabel = KnownMetadataValue(0);
    }
}

[[nodiscard]] inline bool NeedsFullRead(const PhotoMetadataValues& sidecar) noexcept {
    return !(sidecar.rating.IsAuthoritative() && sidecar.colorLabel.IsAuthoritative() &&
        sidecar.pickState.IsAuthoritative());
}

struct FieldWrite {
    MetadataFieldId field = MetadataFieldId::Rating;
    int value = 0;
};

[[nodiscard]] inline std::vector<FieldWrite> PlanWrites(const PhotoMetadataValues& sidecar,
    const PhotoMetadataValues& before, const MetadataPatch& patch) {
    std::vector<FieldWrite> writes;
    const auto plan = [&](MetadataFieldId field, const MetadataPatchField& edit, bool writeZero) {
        if (edit.active) { writes.push_back({ field, edit.desired }); return; }
        if (MetadataFieldFor(sidecar, field).IsAuthoritative()) return; // already there
        const MetadataFieldValue known = MetadataFieldFor(before, field);
        if (known.knowledge != MetadataKnowledge::Known) return;    // never invent values
        if (known.value == 0 && !writeZero) return;
        writes.push_back({ field, known.value });
    };
    plan(MetadataFieldId::Rating, patch.rating, true);
    plan(MetadataFieldId::PickState, patch.pickState, true);
    plan(MetadataFieldId::ColorLabel, patch.colorLabel, false);
    return writes;
}

} // namespace quicksift::core::sidecar_write
