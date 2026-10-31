// OWNER: Portable checks for the RAW/XMP sidecar write path (0.9.5.0):
// core/sidecar_write_policy.h (sidecar-first before values, materialised fields,
// QuickSift-sidecar completion) and core/metadata_gate_policy.h (the Exiv2 gate
// admits the writer thread's own reads: the RAW rating/pick self-deadlock).
//
//   g++ -std=c++20 -O2 -Isrc scripts/checks/sidecar_write_checks.cpp libQuickSiftCore.a

#include "core/metadata_gate_policy.h"
#include "core/sidecar_write_policy.h"

#include <cstdio>

namespace {
int gFailures = 0;
int gChecks = 0;
void Check(bool condition, const char* label) {
    ++gChecks;
    if (!condition) { ++gFailures; std::printf("FAIL: %s\n", label); }
}
} // namespace

int main() {
    using namespace quicksift::core;
    namespace sw = quicksift::core::sidecar_write;
    namespace gate = quicksift::core::metadata_gate;

    // Gate: a pending write blocks new background readers, never the writer's own reads.
    Check(!gate::Admit(false, false, 0, 1), "background reader waits behind a pending write");
    Check(gate::Admit(false, true, 0, 1), "writer thread's own read is admitted (no self-deadlock)");
    Check(gate::Admit(true, false, 0, 1), "write admitted when the gate is free");
    Check(!gate::Admit(true, true, 1, 1) && !gate::Admit(false, true, 1, 0), "one holder at a time");
    Check(gate::Admit(false, false, 0, 0), "reader admitted with no pending write");

    // No sidecar: a full read is needed once, and every Known value is materialised.
    const PhotoMetadataValues none{ UnknownMetadataValue(), UnknownMetadataValue(), UnknownMetadataValue() };
    Check(sw::NeedsFullRead(none), "missing sidecar needs one full read");
    const PhotoMetadataValues full{ KnownMetadataValue(3), KnownMetadataValue(2), KnownMetadataValue(0) };
    const MetadataPatch pick = MakeSingleFieldPatch(MetadataFieldId::PickState, 1);
    const auto writes = sw::PlanWrites(none, full, pick);
    bool hasPick = false, hasRating = false, hasLabel = false;
    for (const auto& w : writes) {
        if (w.field == MetadataFieldId::PickState) hasPick = w.value == 1;
        if (w.field == MetadataFieldId::Rating) hasRating = w.value == 3;
        if (w.field == MetadataFieldId::ColorLabel) hasLabel = w.value == 2;
    }
    Check(hasPick && hasRating && hasLabel && writes.size() == 3, "edit plus materialised rating and label");

    // After that write the sidecar answers everything: later edits are sidecar-only.
    PhotoMetadataValues written{ KnownMetadataValue(3), UnknownMetadataValue(), KnownMetadataValue(1) };
    sw::CompleteQuickSiftSidecar(written);
    Check(written.colorLabel.IsKnown() && written.colorLabel.value == 0,
        "QuickSift sidecar (rating + pick): absent label means no label");
    Check(!sw::NeedsFullRead(written), "complete sidecar: no Exiv2/Shell read of the RAW");

    // Foreign sidecar (Lightroom: rating only) is not completed and keeps the fallback.
    PhotoMetadataValues foreign{ KnownMetadataValue(4), UnknownMetadataValue(), UnknownMetadataValue() };
    sw::CompleteQuickSiftSidecar(foreign);
    Check(foreign.colorLabel.knowledge == MetadataKnowledge::Unknown && sw::NeedsFullRead(foreign),
        "foreign sidecar still consults the RAW");

    // Values are never invented: Unknown / Unsupported / unset-label are not written.
    const PhotoMetadataValues partial{ UnknownMetadataValue(), KnownMetadataValue(0), UnsupportedMetadataValue(L"x") };
    const auto minimal = sw::PlanWrites(none, partial, MakeSingleFieldPatch(MetadataFieldId::Rating, 5));
    Check(minimal.size() == 1 && minimal[0].field == MetadataFieldId::Rating && minimal[0].value == 5,
        "only the edit when nothing else is Known");
    // Fields already in the sidecar are left alone.
    const auto again = sw::PlanWrites(written, written, MakeSingleFieldPatch(MetadataFieldId::Rating, 1));
    Check(again.size() == 1, "complete sidecar: a rating edit writes one field");

    std::printf("sidecar_write_checks: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
