// CODE GUIDE: See CODE_GUIDE.md -> "Scrolling thumbnails".
// OWNER: Portable viewport prediction contract; no Win32 or decoder side effects belong here.

#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

namespace quicksift::review::prefetch {

enum class PrefetchTier { Visible, Predictive, Background };

// Settled = full decode priority; Coasting = throttle; Fling = UI/placeholders only.
enum class ScrollMotionPhase { Settled, Coasting, Fling };

struct ThumbnailRequest {
    std::size_t index = 0;
    PrefetchTier tier = PrefetchTier::Background;
};

struct ThumbnailViewport {
    std::size_t itemCount = 0;
    int columns = 1;
    float scrollOffset = 0.0f;
    float viewportHeight = 1.0f;
    float rowHeight = 1.0f;
    int baseOverscanRows = 1;
    int maximumPredictiveRows = 8;
    float predictionHorizonSeconds = 0.35f;
};

struct ThumbnailPrefetchPlan {
    int visibleFirstRow = 0;
    int visibleLastRow = -1;
    int scheduledFirstRow = 0;
    int scheduledLastRow = -1;
    int direction = 0;
    float velocityRowsPerSecond = 0.0f;
    std::vector<ThumbnailRequest> requests;
};

// Velocity- and size-aware decode admission for Thumbnail View scrolling.
// Large cells cost more decode work per cell, so concurrent/enqueue caps shrink.
struct ScrollLoadBudget {
    ScrollMotionPhase phase = ScrollMotionPhase::Settled;
    // Maps onto Worker thumbnail drag/load mode: 0 = cache-only, 1 = minimal, 2 = moderate.
    int loadMode = 2;
    int maxVisibleEnqueue = 64;
    int maxPredictiveRows = 24;
    unsigned maxConcurrentDecodes = 8;
    bool allowPredictive = true;
    bool placeholdersOnly = false;
};

// Derive a scroll-load budget from tracked scroll velocity and thumbnail cell size.
// velocityPixelsPerSecond should already be quiet-window decayed (see PrefetchPlanner).
[[nodiscard]] ScrollLoadBudget ComputeScrollLoadBudget(float velocityPixelsPerSecond,
    int thumbnailSizeDip, int visibleCellEstimate = 0) noexcept;

// Concurrent thumbnail decode cap for a given cell size while motion is active.

// Virtualized photo-grid scroll contract:
// - Paint placeholders/cache immediately (never decode on the paint path during motion)
// - Coalesce scroll input to one viewport publish + layout/paint per frame
// - Fling: publish/cancel only; Coast: size-capped enqueue; Settled: full after debounce
// - Successive flings must not accumulate a decode backlog
[[nodiscard]] inline constexpr int ScrollFrameIntervalMilliseconds() noexcept { return 16; }
[[nodiscard]] inline constexpr int ScrollSettleDebounceMilliseconds() noexcept { return 140; }
// Cancel in-flight off-viewport thumbnail work immediately while flinging (no grace).
[[nodiscard]] inline constexpr bool FlingCancelsInFlightImmediately() noexcept { return true; }

[[nodiscard]] unsigned ConcurrentThumbnailDecodeCap(int thumbnailSizeDip) noexcept;

class PrefetchPlanner final {
public:
    using Clock = std::chrono::steady_clock;

    void ObserveScroll(float offset, Clock::time_point timestamp = Clock::now()) noexcept;
    [[nodiscard]] ThumbnailPrefetchPlan PlanThumbnails(const ThumbnailViewport& viewport,
        Clock::time_point timestamp = Clock::now()) const;
    void Reset(float offset = 0.0f, Clock::time_point timestamp = Clock::now()) noexcept;

    [[nodiscard]] float VelocityPixelsPerSecond() const noexcept { return velocityPixelsPerSecond_; }
    // Velocity used for decode admission: decays to zero shortly after the last
    // ObserveScroll so wheel flings do not keep the grid in Fling mode forever.
    [[nodiscard]] float EffectiveVelocityPixelsPerSecond(
        Clock::time_point timestamp = Clock::now()) const noexcept;
    [[nodiscard]] int Direction() const noexcept { return direction_; }
    [[nodiscard]] Clock::time_point LastScrollTimestamp() const noexcept { return lastTimestamp_; }

private:
    float lastOffset_ = 0.0f;
    Clock::time_point lastTimestamp_{};
    float velocityPixelsPerSecond_ = 0.0f;
    int direction_ = 0;
    bool initialized_ = false;
};

} // namespace quicksift::review::prefetch
