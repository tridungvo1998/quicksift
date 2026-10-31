// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#pragma once
// Metadata read lane scheduling (portable; checked by metadata_progress_checks).
//
// Metadata (rating/pick/label) reads stay below decode and user work, but must
// make guaranteed progress. Before 0.9.5.0 the lane did not start a read while
// ANY visual job was active or queued - including Idle/Predictive thumbnail
// prefetch and background face analysis - so a large folder with continuous
// background decode (or a leaked active counter) left "metadata pending"
// forever. Now only foreground visual work makes it yield, and an aging rule
// starts one read after kMinShareAge of waiting even under foreground load.
#include <chrono>
#include <cstdint>

namespace quicksift::core::metadata_lane {

inline constexpr std::chrono::milliseconds kMinShareAge{ 750 };
// Pending metadata writes outrank reads, but a stuck write lane must not
// starve reads forever either.
inline constexpr std::chrono::milliseconds kWriteYieldCap{ 10000 };

struct LaneInputs {
    bool foregroundVisualActive = false; // Interactive/Visible or view-lane decode running
    bool foregroundVisualQueued = false;
    bool writePending = false;           // exclusive path write or Exiv2 write preference
    std::chrono::milliseconds waited{ 0 }; // since the lane first yielded with work queued
};

[[nodiscard]] constexpr bool ShouldStartRead(const LaneInputs& in) noexcept {
    if (in.writePending) return in.waited >= kWriteYieldCap;
    if (in.foregroundVisualActive || in.foregroundVisualQueued) return in.waited >= kMinShareAge;
    return true;
}

// Which running visual jobs count as "foreground" for the metadata lane.
// Background face analysis and Idle/Predictive prefetch do not.
[[nodiscard]] constexpr bool IsForegroundVisual(bool faceJob, bool viewLane, bool viewPrefetch,
    bool interactiveOrVisiblePriority) noexcept {
    if (faceJob) return false;
    if (viewLane) return !viewPrefetch;
    return interactiveOrVisiblePriority;
}

} // namespace quicksift::core::metadata_lane
