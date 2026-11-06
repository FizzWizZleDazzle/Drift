// =============================================================================
// Drift Engine - Scene Editor Example
// =============================================================================
// Loads a JSON scene, edits it in the dev inspector (F1), saves it (F5), and
// spawns a prefab with per-instance overrides (P). A game component (Spin)
// opts into reflection so the inspector and scene files know it.
// =============================================================================

#include <drift/App.hpp>
#include <drift/Query.hpp>
#include <drift/Reflect.hpp>
#include <drift/Scene.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/PhysicsResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>

#ifdef DRIFT_DEV
#include <drift/resources/UIResource.hpp>
#include "imgui.h"
#endif

#include <filesystem>
#include <string>

using namespace drift;

// A game component: rotates its entity (and, through the hierarchy, its children).
struct Spin {
    float speed = 1.f;   // radians per second
};

struct Editor : public Resource {
    DRIFT_RESOURCE(Editor)
    std::string savePath = "level.json";
    SceneHandle coin;
    uint32_t rng = 12345;
    int coinsSpawned = 0;
};

static void spinSystem(Res<Time> time, QueryMut<Transform2D, Spin> spinners) {
    spinners.iter([&](Transform2D& t, Spin& s) { t.rotation += s.speed * time->delta; });
}

static bool keyboardFree() {
#ifdef DRIFT_DEV
    return !ImGui::GetIO().WantTextInput;   // not while typing in a field
#else
    return true;
#endif
}

static void spawnCoin(App& app, Vec2 at) {
    auto* editor = app.getResource<Editor>();
    auto* scenes = app.getResource<SceneResource>();
    editor->rng = editor->rng * 1664525u + 1013904223u;
    int hue = static_cast<int>((editor->rng >> 8) % 3);
    const int tints[3][3] = {{255, 255, 255}, {255, 160, 160}, {160, 255, 180}};

    SceneSpawnOptions opts;
    opts.offset = at;
    opts.overrides = {
        {"Coin", {{"Sprite", {{"tint", {tints[hue][0], tints[hue][1], tints[hue][2], 255}}}}}},
    };
    scenes->spawnNow(editor->coin, opts);
    editor->coinsSpawned++;
}

class EditorPlugin : public Plugin {
public:
    void build(App& app) override {
        // Opt the game component into reflection: scene files and the
        // inspector now read and write it like any engine component.
        reflect::component<Spin>(app.world(), "Spin")
            .field("speed", &Spin::speed);

        app.addResource<Editor>();
        app.update<spinSystem>("spin");

        // Scene work touches the world directly, so it runs in exclusive systems.
        app.addSystem("load_level", Phase::Startup, [](App& a) {
            auto* scenes = a.getResource<SceneResource>();
            auto* editor = a.getResource<Editor>();
            const char* path = std::filesystem::exists(editor->savePath)
                                   ? editor->savePath.c_str() : "assets/level.json";
            SceneHandle level = scenes->load(path);
            SceneInstance inst = scenes->spawnNow(level);
            DRIFT_LOG_INFO("Loaded '%s': %zu entities", path, inst.entities.size());
            editor->coin = scenes->load("assets/coin.json");

            a.getResource<PhysicsResource>()->setDebugDraw({.enabled = true});
#ifdef DRIFT_DEV
            a.getResource<UIResource>()->devToggle();
#endif
        });

        app.addSystem("editor_keys", Phase::Update, [](App& a) {
            auto* input = a.getResource<InputResource>();
            auto* editor = a.getResource<Editor>();
            if (!keyboardFree()) return;
            if (input->keyPressed(Key::F5)) {
                a.getResource<SceneResource>()->save(editor->savePath.c_str());
            }
            if (input->keyPressed(Key::P)) {
                auto* renderer = a.getResource<RendererResource>();
                spawnCoin(a, renderer->screenToWorld(input->mousePosition()));
            }
        });
    }

#ifdef DRIFT_DEV
    void finish(App& app) override {
        app.getResource<UIResource>()->addDevPanel("Scene Editor", [](App& a) {
            auto* editor = a.getResource<Editor>();
            ImGui::TextWrapped("F1 toggles the overlay. Select entities in the Inspector "
                               "and edit their components.");
            ImGui::Text("F5  save to %s", editor->savePath.c_str());
            ImGui::Text("P   spawn a coin prefab at the mouse");
            if (ImGui::Button("Save")) a.getResource<SceneResource>()->save(editor->savePath.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Spawn coin")) spawnCoin(a, {0.f, -250.f});
            ImGui::Text("Coins spawned: %d", editor->coinsSpawned);
        });
    }
#endif

    DRIFT_PLUGIN(EditorPlugin)
};

int main() {
    App app;
    app.setConfig({.title = "Drift - Scene Editor", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.addPlugin<EditorPlugin>();
    return app.run();
}
