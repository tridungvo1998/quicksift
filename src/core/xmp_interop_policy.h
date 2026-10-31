// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
// OWNER: How QuickSift's rating / pick / reject model maps onto XMP fields that
// Lightroom Classic and Capture One read (portable; checked by
// scripts/checks/xmp_interop_checks.cpp). Every reader (sidecar text, Exiv2, the
// fast JPEG XMP scan) and every writer (sidecar text, Exiv2) goes through here.
//
// On disk (0.9.6.0 and later):
//   xmp:Rating        -1 when rejected (Lightroom's and Capture One's reject),
//                     otherwise the star rating 0..5
//   quicksift:Pick    -1 rejected, 0 none, 1 picked (QuickSift's own field)
//   quicksift:Rating  the star rating 0..5, kept while rejected so that
//                     un-rejecting restores the stars
//   xmp:Label         the colour label; optionally also set for picks
//                     (Settings -> "Mark picks with a label")
//
// Reading:
//   xmp:Rating == -1                        -> rejected; stars = quicksift:Rating (or 0)
//   xmp:Rating 0..5, quicksift:Pick == -1:
//     with quicksift:Rating present         -> another app un-rejected it: not rejected
//     without quicksift:Rating (pre-0.9.6)  -> rejected (legacy QuickSift file)
//   otherwise                               -> stars = xmp:Rating, pick = quicksift:Pick
#pragma once

#include "core/metadata_state.h"

#include <optional>

namespace quicksift::core::xmp_interop {

inline constexpr int kLightroomRejectRating = -1;
// xmp:Rating is parsed with this lower bound so -1 is a value, not "custom".
inline constexpr int kXmpRatingMinimum = -1;
inline constexpr int kXmpRatingMaximum = 5;

// `rating` is xmp:Rating parsed in [-1, 5], `pick` is quicksift:Pick, and
// `storedStars` is quicksift:Rating (nullopt when absent or not 0..5). Both
// values are rewritten in place into QuickSift's model (rating 0..5, pick -1..1).
inline void DecodeRatingAndPick(MetadataFieldValue& rating, MetadataFieldValue& pick,
    std::optional<int> storedStars) {
    if (rating.IsKnown() && rating.value == kLightroomRejectRating) {
        pick = KnownMetadataValue(-1);
        rating = KnownMetadataValue(storedStars.value_or(0));
        return;
    }
    if (rating.IsKnown() && pick.IsKnown() && pick.value == -1 && storedStars) {
        // quicksift:Rating proves a 0.9.6+ writer, which always writes xmp:Rating=-1
        // with a reject. A non-negative xmp:Rating means another app un-rejected it.
        pick = KnownMetadataValue(0);
    }
}

// Values to write when the rating or the pick changes.
struct EncodedRatingPick {
    bool writeXmpRating = false;   // false: leave xmp:Rating untouched
    int xmpRating = 0;
    bool writePick = false;        // false: leave quicksift:Pick untouched
    int pick = 0;
    bool writeStoredStars = false; // false: leave quicksift:Rating untouched
    int storedStars = 0;
};

// `rating` / `pick` are the model values after the edit; nullopt when the value
// is not a plain known one (a custom on-disk value QuickSift must not overwrite).
// A reject whose stars are not known leaves xmp:Rating alone (documented edge).
[[nodiscard]] inline EncodedRatingPick EncodeRatingAndPick(std::optional<int> rating,
    std::optional<int> pick) noexcept {
    EncodedRatingPick out;
    if (pick && *pick >= -1 && *pick <= 1) {
        out.writePick = true;
        out.pick = *pick;
    }
    if (rating && *rating >= 0 && *rating <= 5) {
        out.writeXmpRating = true;
        out.xmpRating = (pick && *pick == -1) ? kLightroomRejectRating : *rating;
        out.writeStoredStars = true;
        out.storedStars = *rating;
    }
    return out;
}

[[nodiscard]] inline std::optional<int> KnownValue(const MetadataFieldValue& value) noexcept {
    if (value.IsKnown()) return value.value;
    return std::nullopt;
}

// Pick -> colour label companion ("Mark picks with a label"). `mapping` is
// the chosen colour (0 = off). Returns the label to write together with the pick
// edit, or nullopt to leave the label alone:
//   * picking a photo that has no label gives it the mapping colour;
//   * un-picking (or rejecting) a photo whose label is the mapping colour clears it.
// Other labels are never touched, so a label the user chose is never replaced.
[[nodiscard]] inline std::optional<int> PickCompanionLabel(int mapping, int previousPick,
    int newPick, const MetadataFieldValue& currentLabel) noexcept {
    if (mapping <= 0 || !currentLabel.IsKnown() || previousPick == newPick) return std::nullopt;
    if (newPick == 1 && currentLabel.value == 0) return mapping;
    if (previousPick == 1 && newPick != 1 && currentLabel.value == mapping) return 0;
    return std::nullopt;
}

} // namespace quicksift::core::xmp_interop
