// CODE GUIDE: Serialized latest-wins persistence. Writer never reads UI-owned state.
#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
namespace quicksift::core {
struct TextSnapshot { std::wstring path, document; bool operator==(const TextSnapshot&) const = default; };
class SnapshotWriter {
public:
    using Write = std::function<bool(const TextSnapshot&)>;
    explicit SnapshotWriter(Write write) : write_(std::move(write)) {}
    ~SnapshotWriter() { Stop(); }
    std::uint64_t Submit(TextSnapshot snapshot) {
        std::lock_guard lock(mutex_);
        if (stopping_) return 0;
        if (!thread_.joinable()) thread_ = std::thread([this] { Run(); });
        pending_ = std::move(snapshot);
        const auto ticket = ++submitted_;
        cv_.notify_all();
        return ticket;
    }
    bool Drain(std::uint64_t ticket) {
        if (!ticket) return false;
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [&] { return completed_ >= ticket; });
        return success_;
    }
    void Stop() noexcept {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join(); // drain pending final snapshot, never abandon settings
    }
private:
    void Run() noexcept {
        std::optional<TextSnapshot> lastSaved;
        for (;;) {
            TextSnapshot snapshot;
            std::uint64_t ticket;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [&] { return stopping_ || pending_.has_value(); });
                if (!pending_) return;
                snapshot = std::move(*pending_); pending_.reset(); ticket = submitted_;
            }
            bool success = lastSaved && *lastSaved == snapshot;
            if (!success) {
                // A final drain observes failure; a subsequent Submit retries the same content.
                try { success = write_(snapshot); } catch (...) { success = false; }
                if (success) lastSaved = std::move(snapshot);
            }
            { std::lock_guard lock(mutex_); completed_ = ticket; success_ = success; }
            cv_.notify_all();
        }
    }
    Write write_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    std::optional<TextSnapshot> pending_;
    std::uint64_t submitted_ = 0, completed_ = 0;
    bool stopping_ = false, success_ = true;
};
}
