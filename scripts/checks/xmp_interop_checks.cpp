// OWNER: Portable checks for the Lightroom / Capture One rating-pick mapping
// (core/xmp_interop_policy.h): decode, encode, round trip and the pick label.
#include "core/xmp_interop_policy.h"

#include <cstdio>
#include <optional>

namespace {
int g_checks = 0;
int g_failures = 0;
void Check(bool condition, const char* name) {
    ++g_checks;
    if (!condition) { ++g_failures; std::printf("FAIL: %s\n", name); }
}
using namespace quicksift::core;
using namespace quicksift::core::xmp_interop;

struct Decoded { int rating; int pick; };
Decoded Decode(int xmpRating, std::optional<int> pick, std::optional<int> stored) {
    MetadataFieldValue r = KnownMetadataValue(xmpRating);
    MetadataFieldValue p = pick ? KnownMetadataValue(*pick) : KnownMetadataValue(0);
    DecodeRatingAndPick(r, p, stored);
    return { r.value, p.value };
}

// Simulates writing then reading back through the on-disk fields.
Decoded RoundTrip(int stars, int pick) {
    const EncodedRatingPick e = EncodeRatingAndPick(stars, pick);
    return Decode(e.xmpRating, e.pick, e.storedStars);
}
} // namespace

int main() {
    // Encode.
    const auto reject = EncodeRatingAndPick(3, -1);
    Check(reject.writeXmpRating && reject.xmpRating == -1, "reject writes xmp:Rating=-1");
    Check(reject.writeStoredStars && reject.storedStars == 3, "reject keeps stars in quicksift:Rating");
    Check(reject.writePick && reject.pick == -1, "reject keeps quicksift:Pick=-1");
    const auto pick = EncodeRatingAndPick(4, 1);
    Check(pick.xmpRating == 4 && pick.pick == 1 && pick.storedStars == 4, "pick writes stars as xmp:Rating");
    const auto unknownStars = EncodeRatingAndPick(std::nullopt, -1);
    Check(!unknownStars.writeXmpRating && !unknownStars.writeStoredStars && unknownStars.writePick,
        "reject with unknown stars leaves xmp:Rating alone");
    const auto unknownPick = EncodeRatingAndPick(2, std::nullopt);
    Check(unknownPick.writeXmpRating && unknownPick.xmpRating == 2 && !unknownPick.writePick,
        "unknown pick leaves quicksift:Pick alone");

    // Decode.
    Check(Decode(-1, std::nullopt, std::nullopt).pick == -1, "Lightroom/C1 -1 reads as reject");
    Check(Decode(-1, std::nullopt, std::nullopt).rating == 0, "Lightroom reject has 0 stars");
    Check(Decode(-1, 0, 5).rating == 5 && Decode(-1, 0, 5).pick == -1, "-1 wins over quicksift:Pick=0");
    Check(Decode(3, -1, std::nullopt).pick == -1 && Decode(3, -1, std::nullopt).rating == 3,
        "legacy QuickSift reject stays rejected");
    Check(Decode(2, -1, 4).pick == 0 && Decode(2, -1, 4).rating == 2, "un-rejected elsewhere is not rejected");
    Check(Decode(5, 1, 5).pick == 1 && Decode(5, 1, 5).rating == 5, "pick + stars read back");
    Check(Decode(0, 0, std::nullopt).pick == 0 && Decode(0, 0, std::nullopt).rating == 0, "empty file");

    // Round trip: stars survive reject -> unreject.
    for (int stars = 0; stars <= 5; ++stars) {
        for (int p = -1; p <= 1; ++p) {
            const Decoded d = RoundTrip(stars, p);
            Check(d.rating == stars && d.pick == p, "round trip keeps stars and pick");
        }
    }

    // Pick -> colour label.
    const MetadataFieldValue none = KnownMetadataValue(0);
    const MetadataFieldValue green = KnownMetadataValue(3);
    const MetadataFieldValue red = KnownMetadataValue(1);
    Check(!PickCompanionLabel(0, 0, 1, none), "mapping off -> label untouched");
    Check(PickCompanionLabel(3, 0, 1, none) == std::optional<int>(3), "pick adds the mapping label");
    Check(!PickCompanionLabel(3, 0, 1, red), "pick never replaces a user label");
    Check(PickCompanionLabel(3, 1, 0, green) == std::optional<int>(0), "unpick clears the mapping label");
    Check(PickCompanionLabel(3, 1, -1, green) == std::optional<int>(0), "reject clears the mapping label");
    Check(!PickCompanionLabel(3, 1, 0, red), "unpick keeps another label");
    Check(!PickCompanionLabel(3, 1, 1, none), "no pick change -> nothing");
    MetadataFieldValue unknownLabel;
    Check(!PickCompanionLabel(3, 0, 1, unknownLabel), "unknown label -> untouched");

    std::printf("xmp_interop_checks: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
