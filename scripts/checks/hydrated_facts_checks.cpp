// OWNER: Portable checks for the UI-thread merge of background-hydrated facts
// (filter menus and search share one worker; the store stays the authority).
#include "core/hydrated_facts.h"

#include <cstdio>

namespace {
int g_checks = 0;
int g_failures = 0;
void Check(bool condition, const char* name) {
    ++g_checks;
    if (!condition) { ++g_failures; std::printf("FAIL: %s\n", name); }
}
using namespace quicksift::core;
namespace pred = quicksift::core::predicate;

HydratedFacts CameraFacts(int iso) {
    HydratedFacts facts;
    facts.hasCamera = true;
    facts.cameraKnown = true;
    facts.iso = iso;
    facts.aperture = 2.8;
    facts.camera = L"Canon EOS R5";
    return facts;
}
} // namespace

int main() {
    {
        PhotoItem photo;
        const auto change = MergeHydratedFacts(photo, CameraFacts(400));
        Check(change.camera && photo.cameraSettingsKnown && photo.iso == 400, "unknown camera is filled");
        Check(photo.camera == L"Canon EOS R5" && photo.aperture == 2.8, "camera fields copied");
        const auto again = MergeHydratedFacts(photo, CameraFacts(800));
        Check(!again.camera && photo.iso == 400, "known camera is never overwritten by a late result");
    }
    {
        PhotoItem photo;
        HydratedFacts facts = CameraFacts(100);
        facts.cameraKnown = false; // read finished but found nothing
        const auto change = MergeHydratedFacts(photo, facts);
        Check(!change.camera && !photo.cameraSettingsKnown && photo.iso == -1, "unknown result leaves photo untouched");
    }
    {
        PhotoItem photo;
        HydratedFacts facts;
        facts.hasCapture = true;
        facts.capture.year = 2026; facts.capture.month = 3; facts.capture.day = 14;
        facts.source = pred::CaptureTimeSource::Exif;
        Check(MergeHydratedFacts(photo, facts).capture && photo.captureTimeSource == pred::CaptureTimeSource::Exif,
            "unknown capture time filled");
        facts.capture.year = 1999;
        Check(!MergeHydratedFacts(photo, facts).capture && photo.captureTime.year == 2026, "known capture kept");
        PhotoItem other;
        HydratedFacts empty;
        empty.hasCapture = true; // source Unknown
        Check(!MergeHydratedFacts(other, empty).capture &&
            other.captureTimeSource == pred::CaptureTimeSource::Unknown, "Unknown-source capture ignored");
    }
    {
        PhotoItem photo;
        photo.userComment = L"user typed";
        photo.userCommentKnown = true;
        HydratedFacts facts;
        facts.hasComment = true;
        facts.comment = L"stale";
        Check(!MergeHydratedFacts(photo, facts).comment && photo.userComment == L"user typed", "user comment wins");
    }
    std::printf("hydrated_facts_checks: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
