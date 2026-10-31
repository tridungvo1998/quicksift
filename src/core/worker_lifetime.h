// OWNER: Shutdown token for app-owned worker threads (ratings, batch, search,
// deferred startup).
//
// Header-only and portable (no windows.h) so lifecycle_checks can cover it.
// The UI thread owns one Gate. Every worker is started with a Token issued by
// that gate and may only touch the UI (post a message) through
// Token::RunIfAlive. Gate::Revoke() is called on the UI thread at the start of
// teardown (normal close and Force quit both reach WM_DESTROY): it sets the stop
// flag and then takes the exclusive lock, so once Revoke() returns no RunIfAlive
// body is running and none will run again. The owner then joins every worker
// before any member they use is destroyed.
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>

namespace quicksift::core::worker_lifetime {

namespace detail {
struct State {
    std::shared_mutex mutex;
    bool alive = true;               // guarded by mutex
    std::atomic<bool> stop{ false }; // cheap poll for worker loops
};
} // namespace detail

class Token {
public:
    Token() = default;

    // A default (never issued) token is permanently stopped and dead.
    [[nodiscard]] bool Valid() const noexcept { return static_cast<bool>(state_); }
    [[nodiscard]] bool StopRequested() const noexcept {
        return !state_ || state_->stop.load(std::memory_order_acquire);
    }

    // Runs `action` only while the owner is alive, holding off Revoke() until it
    // returns. Keep `action` short and non-blocking (a PostMessage), and never
    // call back into the owner's UI-thread code from it. Returns whether it ran.
    template <class Action>
    bool RunIfAlive(Action&& action) const noexcept {
        if (!state_) return false;
        try {
            std::shared_lock lock(state_->mutex);
            if (!state_->alive) return false;
            action();
            return true;
        } catch (...) {
            return false; // a lock failure or a throwing action never escapes a worker
        }
    }

private:
    friend class Gate;
    explicit Token(std::shared_ptr<detail::State> state) noexcept : state_(std::move(state)) {}
    std::shared_ptr<detail::State> state_;
};

class Gate {
public:
    Gate() : state_(std::make_shared<detail::State>()) {}
    Gate(const Gate&) = delete;
    Gate& operator=(const Gate&) = delete;
    ~Gate() { Revoke(); }

    [[nodiscard]] Token Issue() const noexcept { return Token(state_); }

    // Workers see StopRequested() at their next poll; posts are still allowed.
    void RequestStop() noexcept { state_->stop.store(true, std::memory_order_release); }

    // After this returns no RunIfAlive action is running or will ever run.
    // Idempotent. Never throws (a lock failure degrades to "stop requested").
    void Revoke() noexcept {
        RequestStop();
        try {
            std::unique_lock lock(state_->mutex);
            state_->alive = false;
        } catch (...) {
        }
    }

    [[nodiscard]] bool Revoked() const noexcept {
        try {
            std::shared_lock lock(state_->mutex);
            return !state_->alive;
        } catch (...) {
            return true;
        }
    }

    // Tokens still held by workers (diagnostics only).
    [[nodiscard]] long Outstanding() const noexcept { return state_.use_count() - 1; }

private:
    std::shared_ptr<detail::State> state_;
};

} // namespace quicksift::core::worker_lifetime
