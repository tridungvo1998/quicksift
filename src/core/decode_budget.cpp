// CODE GUIDE: See CODE_GUIDE.md -> "Rules for asynchronous work".
// OWNER: Priority-aware decoded-memory admission with fixed safety limits.

#include "decode_budget.h"

#include <chrono>

namespace quicksift::core {

DecodeReservation::DecodeReservation(std::shared_ptr<DecodeBudget> owner,
    std::size_t bytes, bool oversized) noexcept
    : owner_(std::move(owner)), bytes_(bytes), oversized_(oversized) {}

DecodeReservation::~DecodeReservation() {
    if (owner_) owner_->ReleaseFinal(bytes_, oversized_);
}

void DecodeReservation::ShrinkTo(std::size_t retainedBytes) noexcept {
    if (!owner_ || retainedBytes >= bytes_) return;
    const std::size_t released = bytes_ - retainedBytes;
    bytes_ = retainedBytes;
    owner_->ReleaseBytes(released);
}

bool DecodeBudget::IsCancelledLocked(const Waiter& waiter) const noexcept {
    if (stopping_.load(std::memory_order_acquire)) return true;
    if (!waiter.cancelled) return false;
    try {
        return waiter.cancelled();
    } catch (...) {
        return true;
    }
}

bool DecodeBudget::HasCapacityLocked(const Waiter& waiter) const noexcept {
    if (activeReservations_ >= maximumConcurrent_ || oversizedActive_) return false;
    if (waiter.bytes > byteLimit_) {
        return allowOversized_ && bytesInFlight_ == 0 && activeReservations_ == 0;
    }
    const std::size_t used = std::min(byteLimit_, bytesInFlight_);
    return waiter.bytes <= byteLimit_ - used;
}

const DecodeBudget::Waiter* DecodeBudget::HighestPriorityWaiterLocked() const noexcept {
    const Waiter* best = nullptr;
    for (const Waiter* waiter : waiters_) {
        if (!waiter || IsCancelledLocked(*waiter)) continue;
        if (!best || DecodePriorityRank(waiter->priority) < DecodePriorityRank(best->priority) ||
            (waiter->priority == best->priority && waiter->sequence < best->sequence)) {
            best = waiter;
        }
    }
    return best;
}

std::shared_ptr<DecodeReservation> DecodeBudget::Acquire(std::size_t bytes,
    DecodePriority priority, std::function<bool()> cancelled) {
    if (bytes == 0) return {};

    Waiter waiter;
    waiter.bytes = bytes;
    waiter.priority = priority;
    waiter.cancelled = std::move(cancelled);

    std::unique_lock lock(mutex_);
    waiter.sequence = nextSequence_++;
    waiters_.push_back(&waiter);
    const auto removeWaiter = [&] {
        const auto found = std::find(waiters_.begin(), waiters_.end(), &waiter);
        if (found != waiters_.end()) waiters_.erase(found);
    };

    for (;;) {
        if (IsCancelledLocked(waiter)) {
            removeWaiter();
            lock.unlock();
            available_.notify_all();
            return {};
        }
        if (HighestPriorityWaiterLocked() == &waiter && HasCapacityLocked(waiter)) break;
        available_.wait_for(lock, std::chrono::milliseconds(25));
    }

    removeWaiter();
    const bool oversized = bytes > byteLimit_;
    bytesInFlight_ += bytes;
    ++activeReservations_;
    if (oversized) oversizedActive_ = true;
    try {
        auto reservation = std::make_shared<DecodeReservation>(shared_from_this(), bytes, oversized);
        lock.unlock();
        available_.notify_all();
        return reservation;
    } catch (...) {
        bytesInFlight_ = bytes > bytesInFlight_ ? 0 : bytesInFlight_ - bytes;
        if (activeReservations_ != 0) --activeReservations_;
        if (oversized) oversizedActive_ = false;
        lock.unlock();
        available_.notify_all();
        throw;
    }
}

std::shared_ptr<DecodeReservation> DecodeBudget::TryAcquire(std::size_t bytes,
    DecodePriority priority) noexcept {
    if (bytes == 0) return {};

    std::lock_guard lock(mutex_);
    if (stopping_.load(std::memory_order_acquire) || !waiters_.empty()) return {};
    Waiter waiter{ bytes, priority, nextSequence_, {} };
    if (!HasCapacityLocked(waiter)) return {};

    const bool oversized = bytes > byteLimit_;
    bytesInFlight_ += bytes;
    ++activeReservations_;
    if (oversized) oversizedActive_ = true;
    try {
        return std::make_shared<DecodeReservation>(shared_from_this(), bytes, oversized);
    } catch (...) {
        bytesInFlight_ = bytes > bytesInFlight_ ? 0 : bytesInFlight_ - bytes;
        if (activeReservations_ != 0) --activeReservations_;
        if (oversized) oversizedActive_ = false;
        return {};
    }
}

void DecodeBudget::SetPolicy(std::size_t byteLimit, unsigned maximumConcurrent,
    bool allowOversized) noexcept {
    {
        std::lock_guard lock(mutex_);
        byteLimit_ = std::max<std::size_t>(1, byteLimit);
        maximumConcurrent_ = std::max(1u, maximumConcurrent);
        allowOversized_ = allowOversized;
    }
    available_.notify_all();
}

void DecodeBudget::NotifyWaiters() noexcept {
    available_.notify_all();
}

void DecodeBudget::Stop() noexcept {
    {
        std::lock_guard lock(mutex_);
        stopping_.store(true, std::memory_order_release);
    }
    available_.notify_all();
}

void DecodeBudget::ReleaseBytes(std::size_t bytes) noexcept {
    if (bytes == 0) return;
    {
        std::lock_guard lock(mutex_);
        bytesInFlight_ = bytes > bytesInFlight_ ? 0 : bytesInFlight_ - bytes;
    }
    available_.notify_all();
}

void DecodeBudget::ReleaseFinal(std::size_t bytes, bool oversized) noexcept {
    {
        std::lock_guard lock(mutex_);
        if (bytes != 0) bytesInFlight_ = bytes > bytesInFlight_ ? 0 : bytesInFlight_ - bytes;
        if (activeReservations_ != 0) --activeReservations_;
        if (oversized) oversizedActive_ = false;
    }
    available_.notify_all();
}

std::size_t DecodeBudget::BytesInFlightForTesting() const noexcept {
    std::lock_guard lock(mutex_);
    return bytesInFlight_;
}

unsigned DecodeBudget::ActiveReservationsForTesting() const noexcept {
    std::lock_guard lock(mutex_);
    return activeReservations_;
}

std::size_t DecodeBudget::PendingWaitersForTesting() const noexcept {
    std::lock_guard lock(mutex_);
    return waiters_.size();
}

bool DecodeBudget::OversizedActive() const noexcept {
    std::lock_guard lock(mutex_);
    return oversizedActive_;
}

std::size_t DecodeBudget::Limit() const noexcept {
    std::lock_guard lock(mutex_);
    return byteLimit_;
}

} // namespace quicksift::core
