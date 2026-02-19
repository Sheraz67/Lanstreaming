#pragma once

// Polyfill for std::jthread and std::stop_token on platforms where
// libc++ doesn't provide them (Apple Clang).

#if __has_include(<stop_token>) && !defined(__APPLE__)
// Platform supports std::jthread natively (GCC/libstdc++)
#include <thread>
#include <stop_token>

namespace lancast {
using jthread = std::jthread;
using stop_token = std::stop_token;
} // namespace lancast

#else
// Polyfill using std::thread + std::atomic<bool>

#include <thread>
#include <atomic>
#include <functional>
#include <utility>

namespace lancast {

class stop_token {
public:
    stop_token() : flag_(nullptr) {}
    explicit stop_token(std::atomic<bool>* flag) : flag_(flag) {}

    bool stop_requested() const noexcept {
        return flag_ && flag_->load(std::memory_order_relaxed);
    }

private:
    std::atomic<bool>* flag_;
};

class jthread {
public:
    jthread() noexcept = default;

    template <typename F>
    explicit jthread(F&& f)
        : stop_flag_(new std::atomic<bool>(false))
        , thread_(std::forward<F>(f), stop_token(stop_flag_.get()))
    {}

    ~jthread() {
        if (joinable()) {
            request_stop();
            join();
        }
    }

    jthread(const jthread&) = delete;
    jthread& operator=(const jthread&) = delete;

    jthread(jthread&& other) noexcept
        : stop_flag_(std::move(other.stop_flag_))
        , thread_(std::move(other.thread_))
    {}

    jthread& operator=(jthread&& other) noexcept {
        if (joinable()) {
            request_stop();
            join();
        }
        stop_flag_ = std::move(other.stop_flag_);
        thread_ = std::move(other.thread_);
        return *this;
    }

    void request_stop() noexcept {
        if (stop_flag_) stop_flag_->store(true, std::memory_order_relaxed);
    }

    bool joinable() const noexcept { return thread_.joinable(); }
    void join() { thread_.join(); }

    stop_token get_stop_token() const noexcept {
        return stop_token(stop_flag_.get());
    }

private:
    std::unique_ptr<std::atomic<bool>> stop_flag_;
    std::thread thread_;
};

} // namespace lancast

#endif
