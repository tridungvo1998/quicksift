// OWNER: Debug lock-order checker (0.9.4.0 polish). Header-only, portable.
//
// Every long-lived mutex that can be held while another one is taken has a rank in
// ARCHITECTURE.md "Lock order". A thread may only acquire locks in strictly
// increasing rank. In builds with QS_DEBUG_LOCK_ORDER defined, LockOrderScope keeps a
// per-thread stack of held ranks and reports an inversion through the installed
// handler (default: debug-break / abort); in all other builds it compiles to nothing,
// so release performance is unaffected.
//
// Usage (next to the existing lock):
//     std::lock_guard lock(mutex_);
//     QS_LOCK_ORDER(quicksift::core::lock_order::Rank::DatabaseMutex);
#pragma once

#include <cstdint>
#include <cstdlib>

namespace quicksift::core::lock_order {

// Lower ranks are acquired first. Gaps leave room for new locks.
enum class Rank : std::uint8_t {
    // TransactionCoordinator is UI-thread only and has no mutex.
    MetadataService = 20,        // MetadataTransactionService::mutex_ (never held over callbacks)
    FileService = 21,            // FileTransactionService::mutex_
    SearchMutex = 30,            // searchMutex_ (never held over I/O or posting)
    RatingsWorkerMutex = 31,     // ratingsWorkerMutex_
    BatchWorkerMutex = 32,       // batchWorkerMutex_
    WorkerBlocked = 40,          // Worker::blockedMutex_
    WorkerReadAheadGate = 41,    // Worker::readAheadGateMutex_
    WorkerDesired = 42,          // Worker::desiredMutex_
    WorkerQueue = 43,            // Worker::mutex_ (PopWork runs under it)
    CacheDatabase = 50,          // PersistentCache::databaseMutex_
    CacheDeferred = 51,          // PersistentCache::deferredMutex_
    LogStream = 60,              // diagnostic log streamMutex
    LogState = 61,               // diagnostic log state mutex (innermost: anyone may log)
};

using ViolationHandler = void (*)(Rank held, Rank acquiring) noexcept;

#if defined(QS_DEBUG_LOCK_ORDER)

namespace detail {
inline constexpr int kMaxDepth = 16;
struct ThreadStack {
    Rank ranks[kMaxDepth]{};
    int depth = 0;
};
inline ThreadStack& Stack() noexcept {
    thread_local ThreadStack stack;
    return stack;
}
inline void DefaultHandler(Rank, Rank) noexcept { std::abort(); }
inline ViolationHandler& Handler() noexcept {
    static ViolationHandler handler = &DefaultHandler;
    return handler;
}
} // namespace detail

inline void SetViolationHandler(ViolationHandler handler) noexcept {
    detail::Handler() = handler ? handler : &detail::DefaultHandler;
}

class LockOrderScope {
public:
    explicit LockOrderScope(Rank rank) noexcept {
        auto& stack = detail::Stack();
        if (stack.depth > 0 && static_cast<int>(stack.ranks[stack.depth - 1]) >= static_cast<int>(rank))
            detail::Handler()(stack.ranks[stack.depth - 1], rank);
        if (stack.depth < detail::kMaxDepth) stack.ranks[stack.depth] = rank;
        ++stack.depth;
    }
    ~LockOrderScope() { --detail::Stack().depth; }
    LockOrderScope(const LockOrderScope&) = delete;
    LockOrderScope& operator=(const LockOrderScope&) = delete;
};

[[nodiscard]] inline int HeldDepth() noexcept { return detail::Stack().depth; }

#define QS_LOCK_ORDER_CONCAT2(a, b) a##b
#define QS_LOCK_ORDER_CONCAT(a, b) QS_LOCK_ORDER_CONCAT2(a, b)
#define QS_LOCK_ORDER(rank) \
    ::quicksift::core::lock_order::LockOrderScope QS_LOCK_ORDER_CONCAT(qsLockOrder_, __LINE__)(rank)

#else

inline void SetViolationHandler(ViolationHandler) noexcept {}
[[nodiscard]] inline int HeldDepth() noexcept { return 0; }
#define QS_LOCK_ORDER(rank) static_cast<void>(0)

#endif

} // namespace quicksift::core::lock_order
