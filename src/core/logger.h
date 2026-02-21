#pragma once

#include <string_view>
#include <cstdio>
#include <cstdarg>

namespace lancast {

enum class LogLevel { Debug, Info, Warn, Error };

class Logger {
public:
    static void set_level(LogLevel level);
    static LogLevel level();

    static void log(LogLevel level, const char* tag, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 3, 4)))
#endif
        ;
};

#define LOG_DEBUG(tag, ...) ::lancast::Logger::log(::lancast::LogLevel::Debug, tag, __VA_ARGS__)
#define LOG_INFO(tag, ...)  ::lancast::Logger::log(::lancast::LogLevel::Info, tag, __VA_ARGS__)
#define LOG_WARN(tag, ...)  ::lancast::Logger::log(::lancast::LogLevel::Warn, tag, __VA_ARGS__)
#define LOG_ERROR(tag, ...) ::lancast::Logger::log(::lancast::LogLevel::Error, tag, __VA_ARGS__)

} // namespace lancast
