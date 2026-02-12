#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>

namespace lancast {

// Thread-safe MPSC queue using mutex + condition variable.
// Used for encode->network and network->decode paths.
template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;
    explicit ThreadSafeQueue(size_t max_size) : max_size_(max_size) {}

    void push(T item) {
        {
            std::lock_guard lock(mutex_);
            if (max_size_ > 0 && queue_.size() >= max_size_) {
                queue_.pop(); // drop oldest
            }
            queue_.push(std::move(item));
        }
        cv_.notify_one();
    }

    std::optional<T> try_pop() {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    // Blocks until an item is available or timeout expires.
    std::optional<T> wait_pop(std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        std::unique_lock lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [this] { return !queue_.empty() || closed_; })) {
            return std::nullopt;
        }
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    bool is_closed() const {
        std::lock_guard lock(mutex_);
        return closed_ && queue_.empty();
    }

    size_t size() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard lock(mutex_);
        return queue_.empty();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    size_t max_size_ = 0;
    bool closed_ = false;
};

} // namespace lancast
