#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/AssetServer.hpp>
#include <drift/Scene.hpp>

#ifdef DRIFT_DEV
#include <drift/resources/UIResource.hpp>
#include "imgui.h"
#include <array>
#include <cstdio>
#include <memory>
#endif

namespace drift {

// Adds SceneResource (save, load and spawn JSON scenes and prefabs) and
// lets the AssetServer reload scene files in place.
class ScenePlugin : public Plugin {
public:
    void build(App& app) override {
        auto* scenes = app.addResource<SceneResource>(app);
        app.getResource<AssetServer>()->setReloader("scene",
            [scenes](uint32_t id, const std::string&) { return scenes->reload(SceneHandle{id}); });
    }

#ifdef DRIFT_DEV
    void finish(App& app) override {
        auto* ui = app.getResource<UIResource>();
        if (!ui) return;
        auto path = std::make_shared<std::array<char, 256>>();
        std::snprintf(path->data(), path->size(), "scene.json");
        ui->addDevPanel("Scene", [path](App& a) {
            auto* scenes = a.getResource<SceneResource>();
            if (!scenes) return;
            ImGui::InputText("File", path->data(), path->size());
            if (ImGui::Button("Save world")) scenes->save(path->data());
            ImGui::SameLine();
            if (ImGui::Button("Spawn file")) {
                SceneHandle h = scenes->load(path->data());
                if (h) scenes->spawnNow(h);
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload file")) {
                if (auto* assets = a.getResource<AssetServer>()) assets->reload(path->data());
            }
        });
    }
#endif

    DRIFT_PLUGIN(ScenePlugin)
};

} // namespace drift
