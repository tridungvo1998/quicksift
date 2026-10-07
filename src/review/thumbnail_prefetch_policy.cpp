// OWNER: Velocity/direction row planning; reject non-finite geometry before integer conversion.
// CODE GUIDE: See CODE_GUIDE.md -> "Scrolling thumbnails".

#include "review/thumbnail_prefetch_policy.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace quicksift::review::prefetch {
namespace {

constexpr float kMaximumTrackedVelocity = 1.0e9f;
constexpr int kMaximumColumnsPerPlan = 256;
constexpr int kMaximumRowsPerPlan = 256;
constexpr std::size_t kMaximumThumbnailRequests = 65'536;
// Decode-admission quiet window: after this without a new scroll sample, treat as Settled.
constexpr auto kScrollLoadQuietWindow = std::chrono::milliseconds(120);
constexpr float kFlingVelocityPixelsPerSecond = 950.0f;
constexpr float kCoastVelocityPixelsPerSecond = 280.0f;

int SafeFloorRow(double pixelOffset, double rowHeight, int maximumRow) noexcept {
    if (maximumRow <= 0 || !std::isfinite(pixelOffset) || !std::isfinite(rowHeight) ||
        rowHeight <= 0.0 || pixelOffset <= 0.0) {
        return 0;
    }
    const double row = std::floor(pixelOffset / rowHeight);
    if (!std::isfinite(row) || row >= static_cast<double>(maximumRow)) return maximumRow;
    return row <= 0.0 ? 0 : static_cast<int>(row);
}

std::size_t SaturatingProduct(std::size_t left, std::size_t right) noexcept {
    if (left == 0 || right == 0) return 0;
    if (left > std::numeric_limits<std::size_t>::max() / right)
        return std::numeric_limits<std::size_t>::max();
    return left * right;
}

} // namespace

unsigned ConcurrentThumbnailDecodeCap(int thumbnailSizeDip) noexcept {
    // Larger cells decode more pixels and must not keep the same concurrency as tiny cells
    // during a fling/coast. Caps are conservative on purpose: UI smoothness wins.
    const int size = std::max(1, thumbnailSizeDip);
    if (size >= 360) return 1u;
    if (size >= 240) return 2u;
    if (size >= 150) return 3u;
    if (size >= 100) return 4u;
    return 6u;
}

ScrollLoadBudget ComputeScrollLoadBudget(float velocityPixelsPerSecond,
    int thumbnailSizeDip, int visibleCellEstimate) noexcept {
    ScrollLoadBudget budget;
    const float speed = std::isfinite(velocityPixelsPerSecond) ?
        std::abs(velocityPixelsPerSecond) : 0.0f;
    const unsigned sizeCap = ConcurrentThumbnailDecodeCap(thumbnailSizeDip);
    const int visibleHint = std::max(0, visibleCellEstimate);

    if (speed >= kFlingVelocityPixelsPerSecond) {
        budget.phase = ScrollMotionPhase::Fling;
        budget.loadMode = 0;
        budget.maxVisibleEnqueue = 0;
        budget.maxPredictiveRows = 0;
        budget.maxConcurrentDecodes = 0;
        budget.allowPredictive = false;
        budget.placeholdersOnly = true;
        return budget;
    }

    if (speed >= kCoastVelocityPixelsPerSecond) {
        budget.phase = ScrollMotionPhase::Coasting;
        // Moderate motion: keep a tiny rolling visible window. Large cells get mode 1
        // (single concurrent decode); smaller cells may use mode 2 with a size-scaled cap.
        const unsigned coastCap = std::max(1u, sizeCap / 2u);
        budget.maxConcurrentDecodes = coastCap;
        budget.loadMode = coastCap <= 1u ? 1 : 2;
        budget.maxVisibleEnqueue = static_cast<int>(std::max(1u, coastCap));
        if (visibleHint > 0)
            budget.maxVisibleEnqueue = std::min(budget.maxVisibleEnqueue, visibleHint);
        budget.maxPredictiveRows = 0;
        budget.allowPredictive = false;
        budget.placeholdersOnly = false;
        return budget;
    }

    budget.phase = ScrollMotionPhase::Settled;
    budget.loadMode = 2;
    budget.maxConcurrentDecodes = sizeCap;
    budget.maxVisibleEnqueue = visibleHint > 0 ?
        std::max(visibleHint, static_cast<int>(sizeCap) * 4) :
        static_cast<int>(sizeCap) * 8;
    budget.maxPredictiveRows = 24;
    budget.allowPredictive = true;
    budget.placeholdersOnly = false;
    return budget;
}

void PrefetchPlanner::ObserveScroll(float offset, Clock::time_point timestamp) noexcept {
    if (!std::isfinite(offset)) return;
    if (!initialized_) {
        Reset(offset, timestamp);
        return;
    }

    const float seconds = std::chrono::duration<float>(timestamp - lastTimestamp_).count();
    if (!std::isfinite(seconds) || seconds <= 0.0f || seconds > 1.5f) {
        Reset(offset, timestamp);
        return;
    }

    float instantaneous = (offset - lastOffset_) / seconds;
    if (!std::isfinite(instantaneous)) instantaneous = 0.0f;
    instantaneous = std::clamp(instantaneous, -kMaximumTrackedVelocity, kMaximumTrackedVelocity);
    const float weight = std::clamp(seconds * 10.0f, 0.22f, 0.72f);
    velocityPixelsPerSecond_ += (instantaneous - velocityPixelsPerSecond_) * weight;
    if (!std::isfinite(velocityPixelsPerSecond_)) velocityPixelsPerSecond_ = 0.0f;
    velocityPixelsPerSecond_ = std::clamp(velocityPixelsPerSecond_,
        -kMaximumTrackedVelocity, kMaximumTrackedVelocity);
    if (std::abs(velocityPixelsPerSecond_) < 45.0f) {
        direction_ = 0;
    } else {
        direction_ = velocityPixelsPerSecond_ > 0.0f ? 1 : -1;
    }
    lastOffset_ = offset;
    lastTimestamp_ = timestamp;
}

float PrefetchPlanner::EffectiveVelocityPixelsPerSecond(Clock::time_point timestamp) const noexcept {
    if (!initialized_ || !std::isfinite(velocityPixelsPerSecond_)) return 0.0f;
    if (timestamp < lastTimestamp_ || timestamp - lastTimestamp_ > kScrollLoadQuietWindow)
        return 0.0f;
    return velocityPixelsPerSecond_;
}

void PrefetchPlanner::Reset(float offset, Clock::time_point timestamp) noexcept {
    lastOffset_ = std::isfinite(offset) ? offset : 0.0f;
    lastTimestamp_ = timestamp;
    velocityPixelsPerSecond_ = 0.0f;
    direction_ = 0;
    initialized_ = true;
}

ThumbnailPrefetchPlan PrefetchPlanner::PlanThumbnails(const ThumbnailViewport& viewport,
    Clock::time_point timestamp) const {
    ThumbnailPrefetchPlan plan;
    if (viewport.itemCount == 0) return plan;

    int effectiveDirection = direction_;
    float effectiveVelocity = velocityPixelsPerSecond_;
    if (!initialized_ || timestamp < lastTimestamp_ ||
        timestamp - lastTimestamp_ > std::chrono::milliseconds(900) ||
        !std::isfinite(effectiveVelocity)) {
        effectiveDirection = 0;
        effectiveVelocity = 0.0f;
    }

    const int columns = std::clamp(viewport.columns, 1, kMaximumColumnsPerPlan);
    const std::size_t columnCount = static_cast<std::size_t>(columns);
    const double rowHeight = std::isfinite(viewport.rowHeight) && viewport.rowHeight > 0.0f ?
        static_cast<double>(viewport.rowHeight) : 1.0;
    const double viewportHeight = std::isfinite(viewport.viewportHeight) && viewport.viewportHeight > 0.0f ?
        static_cast<double>(viewport.viewportHeight) : 1.0;
    const double scrollOffset = std::isfinite(viewport.scrollOffset) && viewport.scrollOffset > 0.0f ?
        static_cast<double>(viewport.scrollOffset) : 0.0;

    const std::size_t rows = viewport.itemCount / columnCount +
        (viewport.itemCount % columnCount == 0 ? 0u : 1u);
    const int rowCount = static_cast<int>(std::min<std::size_t>(rows,
        static_cast<std::size_t>(std::numeric_limits<int>::max())));
    if (rowCount <= 0) return plan;
    const int maximumRow = rowCount - 1;

    plan.visibleFirstRow = SafeFloorRow(scrollOffset, rowHeight, maximumRow);
    const double viewportEndExclusive = scrollOffset + viewportHeight;
    if (!std::isfinite(viewportEndExclusive)) {
        plan.visibleLastRow = maximumRow;
    } else {
        // A row is visible whenever its rectangle intersects the viewport.
        // Using ceil(end / rowHeight) - 1 includes a row even when only one pixel
        // of it is visible and avoids floating-point boundary loss.
        const double lastVisible = std::ceil(viewportEndExclusive / rowHeight) - 1.0;
        const double clampedLast = std::clamp(lastVisible, 0.0, static_cast<double>(maximumRow));
        plan.visibleLastRow = std::clamp(static_cast<int>(clampedLast),
            plan.visibleFirstRow, maximumRow);
    }
    plan.visibleLastRow = std::min(plan.visibleLastRow,
        plan.visibleFirstRow > maximumRow - (kMaximumRowsPerPlan - 1) ? maximumRow :
        plan.visibleFirstRow + (kMaximumRowsPerPlan - 1));
    plan.direction = effectiveDirection;
    const double velocityRows = static_cast<double>(effectiveVelocity) / rowHeight;
    plan.velocityRowsPerSecond = std::isfinite(velocityRows) ?
        static_cast<float>(std::clamp(velocityRows,
            -static_cast<double>(kMaximumTrackedVelocity), static_cast<double>(kMaximumTrackedVelocity))) : 0.0f;

    const int base = std::clamp(viewport.baseOverscanRows, 0, kMaximumRowsPerPlan);
    const int cap = std::clamp(std::max(base, viewport.maximumPredictiveRows),
        base, kMaximumRowsPerPlan);
    const double horizon = std::isfinite(viewport.predictionHorizonSeconds) ?
        std::clamp(static_cast<double>(viewport.predictionHorizonSeconds), 0.10, 1.50) : 0.35;
    const double rawVelocityLead = std::ceil(
        std::abs(static_cast<double>(plan.velocityRowsPerSecond)) * horizon);
    const int velocityLead = !std::isfinite(rawVelocityLead) || rawVelocityLead >= static_cast<double>(cap) ?
        cap : static_cast<int>(std::max(0.0, rawVelocityLead));
    const int lead = std::clamp(base > cap - velocityLead ? cap : base + velocityLead, base, cap);
    const int trail = effectiveDirection == 0 ? base : std::max(1, base / 2);

    if (effectiveDirection > 0) {
        plan.scheduledFirstRow = std::max(0, plan.visibleFirstRow - trail);
        plan.scheduledLastRow = std::min(maximumRow,
            plan.visibleLastRow > maximumRow - lead ? maximumRow : plan.visibleLastRow + lead);
    } else if (effectiveDirection < 0) {
        plan.scheduledFirstRow = std::max(0, plan.visibleFirstRow - lead);
        plan.scheduledLastRow = std::min(maximumRow,
            plan.visibleLastRow > maximumRow - trail ? maximumRow : plan.visibleLastRow + trail);
    } else {
        plan.scheduledFirstRow = std::max(0, plan.visibleFirstRow - base);
        plan.scheduledLastRow = std::min(maximumRow,
            plan.visibleLastRow > maximumRow - base ? maximumRow : plan.visibleLastRow + base);
    }

    const std::size_t scheduledRows = static_cast<std::size_t>(
        plan.scheduledLastRow - plan.scheduledFirstRow + 1);
    plan.requests.reserve(std::min({ viewport.itemCount,
        SaturatingProduct(scheduledRows, columnCount), kMaximumThumbnailRequests }));

    auto emitRow = [&](int row, PrefetchTier tier) {
        if (row < plan.scheduledFirstRow || row > plan.scheduledLastRow) return;
        const std::size_t rowIndex = static_cast<std::size_t>(row);
        if (rowIndex > viewport.itemCount / columnCount) return;
        const std::size_t first = rowIndex * columnCount;
        if (first >= viewport.itemCount) return;
        const std::size_t remaining = viewport.itemCount - first;
        const std::size_t last = first + std::min(remaining, columnCount);
        for (std::size_t index = first;
            index < last && plan.requests.size() < kMaximumThumbnailRequests; ++index) {
            plan.requests.push_back({ index, tier });
        }
    };

    for (int row = plan.visibleFirstRow; row <= plan.visibleLastRow; ++row)
        emitRow(row, PrefetchTier::Visible);

    const int maxDistance = std::max(plan.visibleFirstRow - plan.scheduledFirstRow,
        plan.scheduledLastRow - plan.visibleLastRow);
    for (int distance = 1; distance <= maxDistance; ++distance) {
        if (effectiveDirection >= 0 && plan.visibleLastRow <= maximumRow - distance)
            emitRow(plan.visibleLastRow + distance, PrefetchTier::Predictive);
        if (effectiveDirection <= 0 && plan.visibleFirstRow >= distance)
            emitRow(plan.visibleFirstRow - distance, PrefetchTier::Predictive);
        if (effectiveDirection > 0 && plan.visibleFirstRow >= distance)
            emitRow(plan.visibleFirstRow - distance, PrefetchTier::Background);
        if (effectiveDirection < 0 && plan.visibleLastRow <= maximumRow - distance)
            emitRow(plan.visibleLastRow + distance, PrefetchTier::Background);
    }

    return plan;
}

} // namespace quicksift::review::prefetch
