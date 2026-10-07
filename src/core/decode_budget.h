// CODE GUIDE: See CODE_GUIDE.md -> "Rules for asynchronous work".
// OWNER: Priority-aware decoded-memory admission with fixed safety limits; reservations release through RAII.

#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <mutex>

namespace quicksift::core {

enum class DecodePriority {
    Interactive,
    Visible,
    Predictive,
    Face,
    Idle,
    Analysis,
};

[[nodiscard]] constexpr int DecodePriorityRank(DecodePriority priority) noexcept {
    return static_cast<int>(priority);
}

class DecodeBudget;

class DecodeReservation final {
public:
    DecodeReservation(std::shared_ptr<DecodeBudget> owner, std::size_t bytes,
        bool oversized) noexcept;
    ~DecodeReservation();

    DecodeReservation(const DecodeReservation&) = delete;
    DecodeReservation& operator=(const DecodeReservation&) = delete;

    void ShrinkTo(std::size_t retainedBytes) noexcept;
    [[nodiscard]] std::size_t Bytes() const noexcept { return bytes_; }
    [[nodiscard]] bool Oversized() const noexcept { return oversized_; }

private:
    std::shared_ptr<DecodeBudget> owner_;
    std::size_t bytes_ = 0;
    bool oversized_ = false;
};

class DecodeBudget final : public std::enable_shared_from_this<DecodeBudget> {
public:
    explicit DecodeBudget(std::size_t byteLimit, unsigned maximumConcurrent = std::numeric_limits<unsigned>::max()) noexcept
        : byteLimit_(std::max<std::size_t>(1, byteLimit)),
          maximumConcurrent_(std::max(1u, maximumConcurrent)) {}

    [[nodiscard]] std::shared_ptr<DecodeReservation> Acquire(std::size_t bytes,
        DecodePriority priority = DecodePriority::Visible,
        std::function<bool()> cancelled = {});
    [[nodiscard]] std::shared_ptr<DecodeReservation> TryAcquire(std::size_t bytes,
        DecodePriority priority = DecodePriority::Idle) noexcept;

    void SetPolicy(std::size_t byteLimit, unsigned maximumConcurrent,
        bool allowOversized) noexcept;
    void NotifyWaiters() noexcept;
    void Stop() noexcept;

    [[nodiscard]] std::size_t BytesInFlightForTesting() const noexcept;
    [[nodiscard]] unsigned ActiveReservationsForTesting() const noexcept;
    [[nodiscard]] std::size_t PendingWaitersForTesting() const noexcept;
    [[nodiscard]] bool OversizedActive() const noexcept;
    [[nodiscard]] std::size_t Limit() const noexcept;

private:
    struct Waiter {
        std::size_t bytes = 0;
        DecodePriority priority = DecodePriority::Visible;
        std::uint64_t sequence = 0;
        std::function<bool()> cancelled;
    };

    friend class DecodeReservation;
    void ReleaseBytes(std::size_t bytes) noexcept;
    void ReleaseFinal(std::size_t bytes, bool oversized) noexcept;
    [[nodiscard]] bool IsCancelledLocked(const Waiter& waiter) const noexcept;
    [[nodiscard]] bool HasCapacityLocked(const Waiter& waiter) const noexcept;
    [[nodiscard]] const Waiter* HighestPriorityWaiterLocked() const noexcept;

    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::list<Waiter*> waiters_;
    std::size_t bytesInFlight_ = 0;
    std::size_t byteLimit_ = 1;
    unsigned activeReservations_ = 0;
    unsigned maximumConcurrent_ = 1;
    bool allowOversized_ = true;
    bool oversizedActive_ = false;
    std::uint64_t nextSequence_ = 1;
    std::atomic<bool> stopping_{ false };
};

} // namespace quicksift::core
