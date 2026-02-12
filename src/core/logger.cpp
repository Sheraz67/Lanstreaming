#include "core/logger.h"
#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <atomic>

namespace lancast {

static std::atomic<LogLevel> g_log_level{LogLevel::Info};

void Logger::set_level(LogLevel level) {
    g_log_level.store(level, std::memory_order_relaxed);
}

LogLevel Logger::level() {
    return g_log_level.load(std::memory_order_relaxed);
}

void Logger::log(LogLevel level, const char* tag, const char* fmt, ...) {
    if (level < g_log_level.load(std::memory_order_relaxed)) return;

    static const char* level_names[] = {"DBG", "INF", "WRN", "ERR"};

    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    fprintf(stderr, "[%lld.%03lld] [%s] [%s] ",
            (long long)(ms / 1000), (long long)(ms % 1000),
            level_names[static_cast<int>(level)], tag);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
}

} // namespace lancast
