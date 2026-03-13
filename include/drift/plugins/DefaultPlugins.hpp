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
