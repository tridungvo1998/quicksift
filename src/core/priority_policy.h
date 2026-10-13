// OWNER: The explicit work-priority model (P0..P5), the process-wide foreground-demand
// signal that lower tiers poll between work units, and the pure admission/yield and
// starvation decisions. Portable and deterministic (checked by
// scripts/checks/priority_policy_checks.cpp).
//
// Tiers (lower number = higher priority):
//   P0 Ui            UI thread. Never blocks: no disk I/O, only brief queue handoffs,
//                    bounded per-frame drains.
//   P1 MetadataWrite rating / pick / label / comment writes from user interaction
//                    (import / batch apply run at P1 but yield to interactive P1 edits
//                    at file boundaries).
//   P2 ViewLoad      Single / Compare foreground decode (current image, compare panes).
//   P3 SearchRead    capture-time / comment reads while the search box has a query.
//   P4 VisibleThumb  visible thumbnails.
//   P5 Background    prefetch / margin thumbnails, face analysis, sharpness, cache
//                    maintenance, folder metadata scans, ratings export reads.
//
// Why P1 (writes) above P2 (view loads): a write is short (one sidecar or one verified
// image copy), user-initiated and changes durable state; delaying it risks losing a
// cull on exit and leaves the UI showing an optimistic value longer. It preempts the
// view lane only for the same path (that decode is cancelled and re-requested) and on
// the shared metadata library / disk queue, which costs the view a few milliseconds.
// The view lane keeps its own thread and decode budget, so writes never starve it.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace quicksift::core::priority {

enum class Tier : std::uint8_t {
    Ui = 0,
    MetadataWrite = 1,
    ViewLoad = 2,
    SearchRead = 3,
    VisibleThumb = 4,
    Background = 5,
};
inline constexpr std::size_t kTierCount = 6;

[[nodiscard]] std::wstring_view TierName(Tier tier) noexcept;

// Point-in-time view of foreground demand (plain values; see ForegroundDemand).
struct DemandSnapshot {
    unsigned metadataWritesPending = 0; // P1 batches queued or running
    bool interactiveWritePending = false; // P1 interactive edit waiting (outranks import/batch)
    unsigned viewForeground = 0;        // P2 foreground view decodes queued or running
    bool searchActive = false;          // P3 search box has an applied query
    bool searchFocused = false;         // P3 search box has keyboard focus
    bool thumbnailViewportActive = false; // P4 grid window published
};

// Highest-priority tier that currently has demand (Background when none).
[[nodiscard]] Tier HighestDemand(const DemandSnapshot& demand) noexcept;

// True when work of `tier` should pause at its next cooperative checkpoint because a
// higher tier has demand. P0/P1 never yield; P2 yields only to P1 writes on the same
// path (handled by the path gate, not here), so it returns false.
[[nodiscard]] bool ShouldYield(Tier tier, const DemandSnapshot& demand) noexcept;

// Thumbnail pool admission while Single/Compare is decoding the image on screen:
// at most `kPoolCapDuringViewLoad` thumbnail decodes, and no predictive / face /
// idle / analysis work until the foreground view decode finishes.
inline constexpr unsigned kPoolCapDuringViewLoad = 1;
enum class PoolLane : std::uint8_t { Interactive, Visible, Predictive, Face, Idle, Analysis };
[[nodiscard]] bool PoolLaneAdmitted(PoolLane lane, bool viewForegroundActive,
    unsigned activeThumbnailJobs) noexcept;

// Search hydration (P3) batch size: smaller while the user is interacting with the
// field so a pending write/view request waits at most one small batch.
[[nodiscard]] std::size_t SearchHydrateBatch(const DemandSnapshot& demand) noexcept;

// Starvation thresholds: a queued unit of `tier` waiting longer than this is logged
// (rate-limited) as a starvation warning. P0 has no queue.
[[nodiscard]] std::chrono::milliseconds StarvationThreshold(Tier tier) noexcept;

// Per-tier "waiting since" bookkeeping (portable, single-owner; no locking).
class StarvationTracker {
public:
    using Clock = std::chrono::steady_clock;
    // Work of `tier` is queued but not being served.
    void NoteWaiting(Tier tier, Clock::time_point now) noexcept;
    // Work of `tier` was served (or its queue drained).
    void NoteServed(Tier tier) noexcept;
    // Returns true once per wait episode when the wait exceeds the threshold.
    [[nodiscard]] bool TakeStarvationWarning(Tier tier, Clock::time_point now,
        std::chrono::milliseconds* waited = nullptr) noexcept;

private:
    std::array<Clock::time_point, kTierCount> waitingSince_{};
    std::array<bool, kTierCount> warned_{};
};

// Process-wide foreground demand. Writers: the UI thread and the transaction /
// view lanes; readers: any background worker between work units. All members are
// atomics; reading is wait-free and never takes a lock.
class ForegroundDemand {
public:
    void SetMetadataWritesPending(unsigned count) noexcept { writes_.store(count, std::memory_order_release); }
    void SetInteractiveWritePending(bool pending) noexcept { interactiveWrite_.store(pending, std::memory_order_release); }
    void SetViewForeground(unsigned count) noexcept { view_.store(count, std::memory_order_release); }
    void SetSearch(bool active, bool focused) noexcept {
        searchActive_.store(active, std::memory_order_release);
        searchFocused_.store(focused, std::memory_order_release);
    }
    void SetThumbnailViewportActive(bool active) noexcept { viewport_.store(active, std::memory_order_release); }
    [[nodiscard]] DemandSnapshot Snapshot() const noexcept;
    // Bumped whenever demand rises; workers may wait on it with a timeout.
    [[nodiscard]] std::uint64_t Epoch() const noexcept { return epoch_.load(std::memory_order_acquire); }
    void Touch() noexcept { epoch_.fetch_add(1, std::memory_order_acq_rel); }

private:
    std::atomic<unsigned> writes_{ 0 };
    std::atomic<bool> interactiveWrite_{ false };
    std::atomic<unsigned> view_{ 0 };
    std::atomic<bool> searchActive_{ false };
    std::atomic<bool> searchFocused_{ false };
    std::atomic<bool> viewport_{ false };
    std::atomic<std::uint64_t> epoch_{ 0 };
};

// The single process instance (function-local static; constant-initialized atomics).
[[nodiscard]] ForegroundDemand& Demand() noexcept;

} // namespace quicksift::core::priority
