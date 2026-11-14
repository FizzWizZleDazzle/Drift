#pragma once

#include <drift/Types.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace drift {

void log(LogLevel level, const char* fmt, ...);

// A warning or error the log kept for dev tools. `id` counts every kept
// line since the program started, so a reader can tell which lines are new.
struct LogLine {
    uint64_t id = 0;
    LogLevel level = LogLevel::Warn;
    std::string text;
    double seconds = 0.0;   // logClock() when it was logged
};

// Seconds on the log's clock: steady time since the first call.
double logClock();

// The latest warnings and errors, oldest first: at most 64.
std::vector<LogLine> recentProblems();

} // namespace drift

#define DRIFT_LOG_TRACE(...) ::drift::log(::drift::LogLevel::Trace, __VA_ARGS__)
#define DRIFT_LOG_DEBUG(...) ::drift::log(::drift::LogLevel::Debug, __VA_ARGS__)
#define DRIFT_LOG_INFO(...)  ::drift::log(::drift::LogLevel::Info,  __VA_ARGS__)
#define DRIFT_LOG_WARN(...)  ::drift::log(::drift::LogLevel::Warn,  __VA_ARGS__)
#define DRIFT_LOG_ERROR(...) ::drift::log(::drift::LogLevel::Error, __VA_ARGS__)
