#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/gameplay/SaveData.hpp>

#include <string>
#include <utility>

namespace drift {

// Adds a SaveData resource opened on the per-user save directory for
// org/app. Not part of DefaultPlugins because it needs those names:
//   app.addPlugin(new SavePlugin("MyStudio", "MyGame"));
class SavePlugin : public Plugin {
public:
    SavePlugin(std::string org, std::string app, std::string slot = "save")
        : org_(std::move(org)), app_(std::move(app)), slot_(std::move(slot)) {}

    void build(App& app) override {
        app.addResource<SaveData>(org_, app_, slot_);
    }
    DRIFT_PLUGIN(SavePlugin)

private:
    std::string org_, app_, slot_;
};

} // namespace drift
