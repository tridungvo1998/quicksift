// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#pragma once
// Facts read by the background fact-hydration worker (search and filter menus)
// and the UI-thread merge rule that applies them. Portable, header-only.
#include "app_types.h"

#include <string>

namespace quicksift::core {

struct HydratedFacts {
    bool hasCapture = false;
    predicate::LocalDateTime capture{};
    predicate::CaptureTimeSource source = predicate::CaptureTimeSource::Unknown;
    bool hasComment = false;
    std::wstring comment;
    bool hasCamera = false;
    int iso = -1;
    double aperture = 0.0;
    double focalLength = 0.0;
    double shutterSpeed = 0.0;
    std::wstring camera;
    std::wstring lens;
    bool cameraKnown = false;
};

// The catalog store is the single authority: only facts it does not know yet are
// filled (a comment saved or settings read meanwhile win over the hydrated ones).
// Returns which filter-relevant facts changed so the caller can refilter.
struct HydratedFactsChange {
    bool capture = false;
    bool comment = false;
    bool camera = false;
};

inline HydratedFactsChange MergeHydratedFacts(PhotoItem& photo, const HydratedFacts& facts) {
    HydratedFactsChange change;
    if (facts.hasCapture && facts.source != predicate::CaptureTimeSource::Unknown &&
        photo.captureTimeSource == predicate::CaptureTimeSource::Unknown) {
        photo.captureTime = facts.capture;
        photo.captureTimeSource = facts.source;
        change.capture = true;
    }
    if (facts.hasComment && !photo.userCommentKnown) {
        photo.userComment = facts.comment;
        photo.userCommentKnown = true;
        change.comment = true;
    }
    if (facts.hasCamera && facts.cameraKnown && !photo.cameraSettingsKnown) {
        photo.iso = facts.iso;
        photo.aperture = facts.aperture;
        photo.focalLength = facts.focalLength;
        photo.shutterSpeed = facts.shutterSpeed;
        photo.camera = facts.camera;
        photo.lens = facts.lens;
        photo.cameraSettingsKnown = true;
        change.camera = true;
    }
    return change;
}

} // namespace quicksift::core
