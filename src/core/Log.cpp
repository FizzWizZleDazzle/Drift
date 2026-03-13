#include <drift/Log.hpp>
#include <cstdarg>
#include <cstdio>

namespace drift {

static const char* levelStrings[] = {
    "[TRACE]",
    "[DEBUG]",
    "[INFO] ",
    "[WARN] ",
    "[ERROR]",
};

void log(LogLevel level, const char* fmt, ...) {
    int lvl = static_cast<int>(level);
    if (lvl < 0) lvl = 0;
    if (lvl > 4) lvl = 4;

    std::fprintf(stderr, "%s ", levelStrings[lvl]);

    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);

    std::fputc('\n', stderr);
}

} // namespace drift
