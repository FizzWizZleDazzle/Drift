#pragma once

// Built-in dev overlay panels (DRIFT_DEV builds).

#include <drift/resources/UIResource.hpp>

namespace drift {

// Entity list, hierarchy tree and reflected component editing.
UIResource::DevPanel makeInspectorPanel();
// Frame time graph, renderer counters and per-system timings.
UIResource::DevPanel makePerformancePanel();
// The latest warnings and errors from the log.
UIResource::DevPanel makeLogPanel();

} // namespace drift
