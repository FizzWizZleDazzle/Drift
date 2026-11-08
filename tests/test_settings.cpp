#include "Test.hpp"

#include <drift/App.hpp>
#include <drift/Settings.hpp>
#include <drift/plugins/LightingPlugin.hpp>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace drift;

namespace {

struct Rules : Resource {
    DRIFT_RESOURCE(Rules)
    int32_t lives = 3;
    float speed = 1.f;
    std::string mode = "default";
    Vec2 spawn = {0.f, 0.f};

    static void describe(reflect::TypeBuilder<Rules>& t) {
        t.field("lives", &Rules::lives)
         .field("speed", &Rules::speed)
         .field("mode", &Rules::mode)
         .field("spawn", &Rules::spawn);
    }
};

std::string writeToml(const char* name, const std::string& text) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path) << text;
    return path.string();
}

// An App reading `file`, with `argv` as its command line.
struct Fixture {
    App app;
    explicit Fixture(const std::string& file, std::vector<std::string> args = {}) {
        args.insert(args.begin(), "game");
        args.insert(args.begin() + 1, "--config=" + file);
        std::vector<char*> argv;
        for (auto& a : args) argv.push_back(a.data());
        app.args(static_cast<int>(argv.size()), argv.data());
    }
};

} // namespace

TEST(settings_fill_engine_sections) {
    std::string f = writeToml("drift_settings_engine.toml", R"(
[project]
name = "Coin Dash"
organization = "Studio"

[window]
width = 320
height = 180
scale_mode = "integer"

[render]
backend = "webgpu"
clear_color = [0.5, 0.25, 0, 1]

[app]
threads = 1
)");
    Fixture fx(f);
    const Config& c = fx.app.config();
    CHECK(c.project.name == "Coin Dash");
    CHECK(c.project.organization == "Studio");
    CHECK(c.title() == "Coin Dash");   // no window.title: the project name
    CHECK(c.window.width == 320);
    CHECK(c.window.height == 180);
    CHECK(c.window.scaleMode == ScaleMode::Integer);   // any case
    CHECK(c.window.resizable);                          // untouched default
    CHECK(c.render.backend == "webgpu");
    CHECK_NEAR(c.render.clearColor.g, 0.25f, 1e-6);
    CHECK(c.app.threads == 1);
}

TEST(settings_bind_plugin_and_game_sections) {
    std::string f = writeToml("drift_settings_sections.toml", R"(
[rules]
lives = 5
mode = "hard"
spawn = [10, 20]

[lighting]
ambient = [0.2, 0.2, 0.3]
)");
    Fixture fx(f);
    Rules& r = fx.app.settings<Rules>("rules");
    CHECK(r.lives == 5);
    CHECK(r.mode == "hard");
    CHECK_NEAR(r.spawn.y, 20.f, 1e-6);
    CHECK_NEAR(r.speed, 1.f, 1e-6);
    CHECK(&fx.app.settings<Rules>("rules") == &r);   // one resource
    CHECK(fx.app.getResource<Rules>() == &r);

    LightingSettings& l = fx.app.settings<LightingSettings>("lighting");
    CHECK_NEAR(l.ambient.b, 0.3f, 1e-6);
    CHECK_NEAR(l.ambient.a, 1.f, 1e-6);   // three channels keep alpha
}

TEST(settings_wrong_types_keep_the_value) {
    std::string f = writeToml("drift_settings_types.toml", R"(
[window]
width = "wide"
scale_mode = "sideways"
height = 200
)");
    Fixture fx(f);
    CHECK(fx.app.config().window.width == 1280);
    CHECK(fx.app.config().window.scaleMode == ScaleMode::Letterbox);
    CHECK(fx.app.config().window.height == 200);
}

TEST(settings_precedence_file_code_env_cli) {
    std::string f = writeToml("drift_settings_order.toml", R"(
[rules]
lives = 5
speed = 2.0
mode = "file"
)");
    SDL_setenv_unsafe("DRIFT_RULES_SPEED", "4.5", 1);
    SDL_setenv_unsafe("DRIFT_RULES_MODE", "env", 1);
    Fixture fx(f, {"--set", "rules.mode=\"cli\""});
    Rules& r = fx.app.settings<Rules>("rules");
    CHECK(r.lives == 5);                 // file
    CHECK_NEAR(r.speed, 4.5f, 1e-6);     // environment over the file
    CHECK(r.mode == "cli");              // --set over the environment

    r.lives = 9;                         // code over the file
    r.speed = 1.f;                       // code under the environment
    fx.app.settingsStore().applyOverrides();   // what start() does after plugins build
    CHECK(r.lives == 9);
    CHECK_NEAR(r.speed, 4.5f, 1e-6);
    SDL_unsetenv_unsafe("DRIFT_RULES_SPEED");
    SDL_unsetenv_unsafe("DRIFT_RULES_MODE");
}

TEST(settings_read_any_value_by_path) {
    std::string f = writeToml("drift_settings_paths.toml", R"(
[level.spawn]
rate = 0.5
count = 12
boss = true
name = "cave"
)");
    Fixture fx(f, {"--set", "level.spawn.count=20"});
    Settings& s = fx.app.settingsStore();
    CHECK(s.has("level.spawn.rate"));
    CHECK(!s.has("level.spawn.missing"));
    CHECK_NEAR(s.getFloat("level.spawn.rate"), 0.5, 1e-9);
    CHECK(s.getInt("level.spawn.count") == 20);
    CHECK(s.getBool("level.spawn.boss"));
    CHECK(s.getString("level.spawn.name") == "cave");
    CHECK(s.getInt("level.spawn.name", -1) == -1);   // wrong type: the fallback
    CHECK(s.getString("nothing.here", "x") == "x");
}

TEST(settings_reload_applies_changed_keys) {
    std::string f = writeToml("drift_settings_reload.toml", R"(
[rules]
lives = 5
speed = 2.0

[window]
width = 640
)");
    Fixture fx(f);
    Rules& r = fx.app.settings<Rules>("rules");
    std::vector<std::string> seen;
    fx.app.onSettingsChanged("rules", [&](const std::vector<std::string>& keys) { seen = keys; });

    r.mode = "set in code";   // not in the file: a reload leaves it
    writeToml("drift_settings_reload.toml", R"(
[rules]
lives = 7

[window]
width = 800
)");
    CHECK(fx.app.settingsStore().reload());
    CHECK(r.lives == 7);
    CHECK_NEAR(r.speed, 1.f, 1e-6);       // removed from the file: the default
    CHECK(r.mode == "set in code");
    CHECK(seen.size() == 2);
    CHECK(fx.app.config().window.width == 640);   // takes effect on restart

    writeToml("drift_settings_reload.toml", "[rules\nlives = 1\n");
    CHECK(!fx.app.settingsStore().reload());     // parse error: values stay
    CHECK(r.lives == 7);
}

TEST(settings_without_a_file_use_defaults) {
    App app;   // the test directory has no drift.toml
    Rules& r = app.settings<Rules>("rules");
    CHECK(r.lives == 3);
    CHECK(app.config().window.width == 1280);
}
