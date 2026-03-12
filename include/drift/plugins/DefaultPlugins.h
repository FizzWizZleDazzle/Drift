#pragma once

#include <drift/Plugin.h>
#include <drift/plugins/InputPlugin.h>
#include <drift/plugins/RendererPlugin.h>
#include <drift/plugins/AudioPlugin.h>
#include <drift/plugins/PhysicsPlugin.h>
#include <drift/plugins/SpritePlugin.h>
#include <drift/plugins/FontPlugin.h>
#include <drift/plugins/ParticlePlugin.h>
#include <drift/plugins/TilemapPlugin.h>
#include <drift/plugins/UIPlugin.h>

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
