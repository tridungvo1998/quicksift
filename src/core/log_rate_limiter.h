// OWNER: Portable token-bucket style rate limiter for diagnostic events. Hot paths
// (search keystrokes, write retries, scheduler yields, thumbnail reliability) log
// through it so a storm can never flood the log writer or the disk queue.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>

namespace quicksift::core {

// Allows up to `burst` events per `window`; further events in the same window are
// counted as suppressed and reported by the next allowed event. Lock-free and safe
// to call from any thread; a lost race only lets one extra event through.
class LogRateLimiter {
public:
    constexpr LogRateLimiter(unsigned burst, std::chrono::milliseconds window) noexcept
        : burst_(burst == 0 ? 1 : burst), windowMs_(window.count() <= 0 ? 1 : window.count()) {}

    // `nowMs` is a monotonic millisecond clock (steady_clock in production).
    // Returns true when the event may be written; `suppressedOut` receives the number
    // of events dropped since the previous allowed event.
    bool Allow(std::int64_t nowMs, std::uint64_t* suppressedOut = nullptr) noexcept {
        std::int64_t start = windowStart_.load(std::memory_order_acquire);
        if (nowMs - start >= windowMs_ || nowMs < start) {
            if (windowStart_.compare_exchange_strong(start, nowMs, std::memory_order_acq_rel))
                count_.store(0, std::memory_order_release);
        }
        const unsigned used = count_.fetch_add(1, std::memory_order_acq_rel);
        if (used < burst_) {
            const std::uint64_t dropped = suppressed_.exchange(0, std::memory_order_acq_rel);
            if (suppressedOut) *suppressedOut = dropped;
            return true;
        }
        suppressed_.fetch_add(1, std::memory_order_acq_rel);
        return false;
    }

    bool Allow(std::uint64_t* suppressedOut = nullptr) noexcept {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        return Allow(static_cast<std::int64_t>(now), suppressedOut);
    }

private:
    unsigned burst_;
    std::int64_t windowMs_;
    std::atomic<std::int64_t> windowStart_{ std::numeric_limits<std::int64_t>::min() / 2 };
    std::atomic<unsigned> count_{ 0 };
    std::atomic<std::uint64_t> suppressed_{ 0 };
};

} // namespace quicksift::core
