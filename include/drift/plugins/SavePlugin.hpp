#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/gameplay/SaveData.hpp>

#include <string>
#include <utility>

namespace drift {

// Adds a SaveData resource opened on the per-user save directory. The
// directory is named after [project] organization and name in drift.toml,
// unless the constructor names them:
//   app.addPlugin<SavePlugin>();
//   app.addPlugin(new SavePlugin("MyStudio", "MyGame"));
class SavePlugin : public Plugin {
public:
    SavePlugin() = default;
    SavePlugin(std::string org, std::string app, std::string slot = "save")
        : org_(std::move(org)), app_(std::move(app)), slot_(std::move(slot)), named_(true) {}

    void build(App& app) override {
        if (!named_) {
            org_ = app.config().project.organization;
            app_ = app.config().project.name;
        }
        app.addResource<SaveData>(org_, app_, slot_);
    }
    DRIFT_PLUGIN(SavePlugin)

private:
    std::string org_, app_, slot_ = "save";
    bool named_ = false;
};

} // namespace drift
