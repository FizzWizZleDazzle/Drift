#pragma once

#include <drift/Types.h>

namespace drift {

void log(LogLevel level, const char* fmt, ...);

} // namespace drift

#define DRIFT_LOG_TRACE(...) ::drift::log(::drift::LogLevel::Trace, __VA_ARGS__)
#define DRIFT_LOG_DEBUG(...) ::drift::log(::drift::LogLevel::Debug, __VA_ARGS__)
#define DRIFT_LOG_INFO(...)  ::drift::log(::drift::LogLevel::Info,  __VA_ARGS__)
#define DRIFT_LOG_WARN(...)  ::drift::log(::drift::LogLevel::Warn,  __VA_ARGS__)
#define DRIFT_LOG_ERROR(...) ::drift::log(::drift::LogLevel::Error, __VA_ARGS__)
