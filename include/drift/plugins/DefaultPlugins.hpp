#pragma once

#include <drift/Plugin.hpp>
#include <drift/plugins/InputPlugin.hpp>
#include <drift/plugins/RendererPlugin.hpp>
#include <drift/plugins/AudioPlugin.hpp>
#include <drift/plugins/PhysicsPlugin.hpp>
#include <drift/plugins/SpritePlugin.hpp>
#include <drift/plugins/FontPlugin.hpp>
#include <drift/plugins/ParticlePlugin.hpp>
#include <drift/plugins/TilemapPlugin.hpp>
#include <drift/plugins/UIPlugin.hpp>
#include <drift/plugins/HierarchyPlugin.hpp>
#include <drift/plugins/SpriteAnimationPlugin.hpp>
#include <drift/plugins/CameraPlugin.hpp>
#include <drift/plugins/TrailPlugin.hpp>

namespace drift {

class DefaultPlugins : public PluginGroup {
public:
    void build(App& app) override {
        app.addPlugin<InputPlugin>();
        app.addPlugin<RendererPlugin>();
        app.addPlugin<AudioPlugin>();
        app.addPlugin<PhysicsPlugin>();
        app.addPlugin<SpritePlugin>();
        app.addPlugin<FontPlugin>();
        app.addPlugin<ParticlePlugin>();
        app.addPlugin<TilemapPlugin>();
        app.addPlugin<UIPlugin>();
        app.addPlugin<HierarchyPlugin>();
        app.addPlugin<SpriteAnimationPlugin>();
        app.addPlugin<CameraPlugin>();
        app.addPlugin<TrailPlugin>();
    }
};

class MinimalPlugins : public PluginGroup {
public:
    void build(App& app) override {
        app.addPlugin<InputPlugin>();
        app.addPlugin<RendererPlugin>();
    }
};

} // namespace drift
