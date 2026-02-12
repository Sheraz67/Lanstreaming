#pragma once

#include <chrono>
#include <cstdint>

namespace lancast {

class Clock {
public:
    Clock() : start_(std::chrono::steady_clock::now()) {}

    // Returns microseconds since clock creation
    int64_t now_us() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(now - start_).count();
    }

    // Returns milliseconds since clock creation
    int64_t now_ms() const {
        return now_us() / 1000;
    }

    void reset() {
        start_ = std::chrono::steady_clock::now();
    }

private:
    std::chrono::steady_clock::time_point start_;
};

} // namespace lancast
