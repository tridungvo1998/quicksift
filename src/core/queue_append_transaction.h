// CODE GUIDE: Append-only queue preparation with rollback until catalog commit.
#pragma once
#include <cstddef>
namespace quicksift::core {
template<class Queue> class QueueAppendTransaction {
public:
    explicit QueueAppendTransaction(Queue& queue) noexcept : queue_(queue), size_(queue.size()) {}
    QueueAppendTransaction(const QueueAppendTransaction&) = delete;
    QueueAppendTransaction& operator=(const QueueAppendTransaction&) = delete;
    ~QueueAppendTransaction() { if (!committed_) while (queue_.size() > size_) queue_.pop_back(); }
    void Commit() noexcept { committed_ = true; }
private:
    Queue& queue_;
    std::size_t size_;
    bool committed_ = false;
};
// Keep first results small. Grow bounded batches to amortize full-prefix order commits.
inline std::size_t ScanBatchTarget(std::size_t discovered) noexcept {
    const auto target = discovered / 8;
    return target < 256 ? 256 : (target > 4096 ? 4096 : target);
}
}
