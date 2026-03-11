// =============================================================================
// Drift Engine - Hot Reload Demo
// =============================================================================
// Demonstrates: host executable that loads a game DLL via cr.h, reloads on
// recompile. The host owns all engine systems; the game DLL only provides
// game_load / game_update / game_unload.
// =============================================================================

#include <drift/drift.h>
#include <drift/renderer.h>
#include <drift/input.h>
#include <drift/audio.h>
#include <drift/physics.h>
#include <drift/ui.h>
#include <drift/ecs.h>
#include <drift/hotreload.h>

struct HostData {
    DriftWorld*  world;
    DriftCamera  camera;
    DriftPlugin  plugin;
    DriftState   state;
};

static void update(float dt, void* user_data) {
    HostData* host = static_cast<HostData*>(user_data);

    drift_input_update();

    if (drift_key_pressed(DRIFT_KEY_ESCAPE)) {
        drift_request_quit();
        return;
    }

    // Check for plugin reload
    if (host->plugin.id != 0) {
        host->state.dt = dt;
        drift_plugin_update(host->plugin);
    }

    // Physics
    drift_physics_step(dt);

    // Render
    drift_renderer_begin_frame();

#ifdef DRIFT_DEV
    drift_ui_begin_frame();
    if (drift_key_pressed(DRIFT_KEY_F1)) {
        drift_ui_dev_toggle();
    }
#endif

    // ECS progress runs all registered systems (including those from the game DLL)
    drift_ecs_progress(host->world, dt);

#ifdef DRIFT_DEV
    drift_ui_dev_render();
    drift_ui_end_frame();
    drift_ui_render();
#endif

    drift_renderer_end_frame();
    drift_renderer_present();
}

int main(int argc, char* argv[]) {
    DriftConfig config = drift_config_default();
    config.title = "Drift - Hot Reload Demo";

    if (drift_init(&config) != DRIFT_OK) {
        return 1;
    }

    drift_renderer_init();
    drift_input_init();
    drift_audio_init();
    drift_physics_init({0.0f, 0.0f});
    drift_ui_init();

    HostData host{};
    host.world = drift_ecs_world_create();
    host.camera = drift_camera_create();
    drift_camera_set_active(host.camera);

    // Setup DriftState for the plugin
    host.state.ecs_world = host.world;
    host.state.dt = 0.0f;

    // Load game plugin
    const char* plugin_path = (argc > 1) ? argv[1] : "./libgame.so";
    host.plugin = drift_plugin_load(plugin_path);
    if (!drift_plugin_is_valid(host.plugin)) {
        DRIFT_LOG_WARN("Failed to load game plugin '%s'. Running without hot reload.", plugin_path);
    } else {
        DRIFT_LOG_INFO("Game plugin loaded: %s", plugin_path);
    }

    drift_run(update, &host);

    // Cleanup
    if (drift_plugin_is_valid(host.plugin)) {
        drift_plugin_unload(host.plugin);
    }
    drift_ecs_world_destroy(host.world);
    drift_camera_destroy(host.camera);

    drift_ui_shutdown();
    drift_physics_shutdown();
    drift_audio_shutdown();
    drift_renderer_shutdown();
    drift_shutdown();

    return 0;
}
