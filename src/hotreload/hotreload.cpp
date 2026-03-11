// drift/src/hotreload/hotreload.cpp
// Hot-reload support for Drift 2D engine.
//
// This is a stub implementation.  The cr.h-based hot-reload is optional and
// currently disabled to unblock the build.  All public API functions are
// present so the linker is satisfied; they simply log that the feature is
// not available.

#include <drift/hotreload.h>

#include <cstdio>

// ---------------------------------------------------------------------------
// Public API -- stubs
// ---------------------------------------------------------------------------

DriftPlugin drift_plugin_load(const char* path)
{
    DriftPlugin handle{};
    std::fprintf(stderr,
                 "[drift] hotreload: not available (stub) -- cannot load '%s'\n",
                 path ? path : "(null)");
    return handle;
}

void drift_plugin_unload(DriftPlugin /*plugin*/)
{
    // no-op
}

bool drift_plugin_update(DriftPlugin /*plugin*/)
{
    return false;
}

bool drift_plugin_is_valid(DriftPlugin /*plugin*/)
{
    return false;
}
