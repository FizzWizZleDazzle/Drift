#include "drift/drift.h"

#include <stdarg.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// drift_log  --  simple levelled logging to stderr
// ---------------------------------------------------------------------------

static const char *level_strings[] = {
    "[TRACE]",
    "[DEBUG]",
    "[INFO] ",
    "[WARN] ",
    "[ERROR]",
};

void drift_log(DriftLogLevel level, const char *fmt, ...)
{
    // Clamp to valid range just in case
    int lvl = static_cast<int>(level);
    if (lvl < 0) lvl = 0;
    if (lvl > 4) lvl = 4;

    fprintf(stderr, "%s ", level_strings[lvl]);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
}
