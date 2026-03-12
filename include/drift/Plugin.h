#pragma once

namespace drift {

class App;

// Plugin: registers resources, systems, and event handlers with the App.
class Plugin {
public:
    virtual ~Plugin() = default;
    virtual void build(App& app) = 0;
    virtual const char* getName() const = 0;
};

// PluginGroup: bundles multiple plugins together (e.g. DefaultPlugins).
class PluginGroup {
public:
    virtual ~PluginGroup() = default;
    virtual void build(App& app) = 0;
};

} // namespace drift

#ifndef SWIG
#define DRIFT_PLUGIN(Name) const char* getName() const override { return #Name; }
#endif
