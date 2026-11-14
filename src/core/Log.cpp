#include <drift/Log.hpp>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>

namespace drift {

static const char* levelStrings[] = {
    "[TRACE]",
    "[DEBUG]",
    "[INFO] ",
    "[WARN] ",
    "[ERROR]",
};

namespace {

constexpr size_t kKept = 64;

struct Problems {
    std::mutex lock;
    std::deque<LogLine> lines;
    uint64_t next = 1;
};

Problems& problems() {
    static Problems p;
    return p;
}

} // namespace

void log(LogLevel level, const char* fmt, ...) {
    int lvl = static_cast<int>(level);
    if (lvl < 0) lvl = 0;
    if (lvl > 4) lvl = 4;

    va_list args, again;
    va_start(args, fmt);
    va_copy(again, args);
    int n = std::vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    std::string text(n > 0 ? static_cast<size_t>(n) : 0, '\0');
    if (n > 0) std::vsnprintf(text.data(), text.size() + 1, fmt, again);
    va_end(again);

    std::fprintf(stderr, "%s %s\n", levelStrings[lvl], text.c_str());

    if (level >= LogLevel::Warn) {
        Problems& p = problems();
        std::lock_guard<std::mutex> guard(p.lock);
        p.lines.push_back({p.next++, level, std::move(text), logClock()});
        if (p.lines.size() > kKept) p.lines.pop_front();
    }
}

double logClock() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

std::vector<LogLine> recentProblems() {
    Problems& p = problems();
    std::lock_guard<std::mutex> guard(p.lock);
    return {p.lines.begin(), p.lines.end()};
}

} // namespace drift
