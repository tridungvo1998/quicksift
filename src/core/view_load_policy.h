// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#pragma once
// OWNER: Single/Compare view image-load decisions (portable, checked by
// view_load_checks). Round 21 (t29u) moved each rule that a load-stall fix
// depends on here, so the Windows-only worker/UI code and the checks share it.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>

namespace quicksift::core::view_load {

// Largest decode edge the view may request: the system profile's bucket cap,
// further limited by the Direct2D device's maximum bitmap edge (0 = unknown).
// A decode above the device limit made ID2D1RenderTarget::CreateBitmap fail, the
// result was dropped silently and paint re-requested the same decode forever.
[[nodiscard]] constexpr int EffectiveMaximumDecodeEdge(int profileMaximum,
    std::uint32_t deviceMaximum) noexcept {
    int cap = std::max(1, profileMaximum);
    if (deviceMaximum > 0 && deviceMaximum < static_cast<std::uint32_t>(cap))
        cap = static_cast<int>(deviceMaximum);
    return cap;
}

// The edge a view request can actually reach: the display demand, bounded by the
// native image edge (when known) and by the decode cap.
[[nodiscard]] constexpr int SatisfiableViewEdge(int desiredEdge, int nativeEdge,
    int maximumDecodeEdge) noexcept {
    int edge = std::max(1, desiredEdge);
    if (nativeEdge > 0) edge = std::min(edge, nativeEdge);
    if (maximumDecodeEdge > 0) edge = std::min(edge, maximumDecodeEdge);
    return std::max(1, edge);
}

// A full-quality view decode is (re)requested only while the display is not
// satisfied AND no full-quality (non-preview) result exists at the exact requested
// bucket. Once that exact result exists, the request has produced the best it can:
// LibRaw half-size / the decoded-pixel cap can keep it below the display edge, and
// re-requesting re-ran a full RAW demosaic on every paint (400 ms backstop).
[[nodiscard]] constexpr bool ShouldRequestFullViewDecode(bool displaySatisfied,
    bool exactFullQualityResultPresent) noexcept {
    return !displaySatisfied && !exactFullQualityResultPresent;
}

// Whole-image view-lane decodes are not tied to the pan/zoom interaction epoch.
// Their pixels stay valid across pan/zoom (a larger zoom target is a separate,
// coalesced request), but the epoch cancelled the running decode on every mouse
// move and the UI dropped a result that finished anyway, so a slow RAW never
// arrived while the user kept panning. Tiles keep the epoch.
[[nodiscard]] constexpr std::uint64_t ViewDecodeInteractionEpoch(bool viewLane,
    std::uint64_t requested) noexcept {
    return viewLane ? 0 : requested;
}

// True when the running view-lane job cannot deliver a result the UI will accept
// for a new foreground request of the same image (it belongs to an older folder
// generation or an older navigation epoch, e.g. A -> B -> A on fast key repeat).
// Such a job must not absorb the new request; the new one is queued behind it.
[[nodiscard]] constexpr bool RunningViewJobIsStaleFor(std::uint64_t runningGeneration,
    std::uint64_t runningEpoch, std::uint64_t requestGeneration,
    std::uint64_t requestEpoch) noexcept {
    if (runningGeneration != requestGeneration) return true;
    return runningEpoch != 0 && requestEpoch != 0 && runningEpoch < requestEpoch;
}

// Stall watchdog for the image on screen in Single View. Observe() is called on
// every paint with the current path and whether its image is settled (final
// quality, or terminally failed). It returns true once per stall interval while
// the same path stays unsettled, so the caller can log the load state.
class StallWatch {
public:
    using Clock = std::chrono::steady_clock;

    explicit StallWatch(std::chrono::milliseconds threshold = std::chrono::seconds(5),
        std::chrono::milliseconds repeat = std::chrono::seconds(15)) noexcept
        : threshold_(threshold), repeat_(repeat) {}

    bool Observe(const std::wstring& path, bool settled, Clock::time_point now) {
        if (settled || path.empty()) {
            path_.clear();
            reports_ = 0;
            return false;
        }
        if (path != path_) {
            path_ = path;
            since_ = now;
            reports_ = 0;
            return false;
        }
        const auto due = since_ + threshold_ + repeat_ * static_cast<int>(std::min(reports_, 1000u));
        if (now < due) return false;
        ++reports_;
        return true;
    }

    [[nodiscard]] std::chrono::milliseconds StalledFor(Clock::time_point now) const noexcept {
        if (path_.empty()) return std::chrono::milliseconds(0);
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - since_);
    }
    [[nodiscard]] unsigned Reports() const noexcept { return reports_; }

private:
    std::chrono::milliseconds threshold_;
    std::chrono::milliseconds repeat_;
    std::wstring path_;
    Clock::time_point since_{};
    unsigned reports_ = 0;
};

} // namespace quicksift::core::view_load
