// CODE GUIDE: Conservative bounded space-query reuse for disposable derivatives only.
#pragma once
#include <chrono>
#include <cstdint>
namespace quicksift::core {
class DerivativeSpaceBudget {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::uint64_t kBatchBytes = 16ull * 1024 * 1024;
    static constexpr std::uint64_t kFileAllowance = 64ull * 1024;
    bool NeedsRefresh(std::uint64_t bytes, std::uint64_t minimum, Clock::time_point now) const noexcept {
        if (!valid_ || now - observed_ >= std::chrono::milliseconds(250) || charged_ >= kBatchBytes ||
            available_ <= minimum || available_ - minimum <= kBatchBytes) return true;
        const auto remaining = kBatchBytes - charged_;
        return remaining <= kFileAllowance || bytes >= remaining - kFileAllowance;
    }
    void Observe(std::uint64_t available, Clock::time_point now) noexcept {
        available_ = available; observed_ = now; charged_ = 0; valid_ = true;
    }
    bool Reserve(std::uint64_t bytes, std::uint64_t minimum) noexcept {
        if (!valid_ || available_ <= minimum || bytes > available_ - minimum ||
            kFileAllowance > available_ - minimum - bytes) return false;
        const auto charge = bytes + kFileAllowance; // bounded above by available_ - minimum
        available_ -= charge;
        charged_ = charge > kBatchBytes - (charged_ < kBatchBytes ? charged_ : kBatchBytes) ? kBatchBytes : charged_ + charge;
        return true;
    }
    void Invalidate() noexcept { valid_ = false; }
private:
    bool valid_ = false;
    std::uint64_t available_ = 0, charged_ = 0;
    Clock::time_point observed_{};
};
}
