#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/gameplay/Tween.hpp>
#include <drift/resources/Time.hpp>

namespace drift {

// Adds the Tweens resource and advances it every frame in PostUpdate, after
// gameplay systems have moved things, so tweened values win for the frame.
class TweenPlugin : public Plugin {
public:
    void build(App& app) override {
        app.initResource<Tweens>();
        app.initEvent<TweenFinished>();
        app.addSystem("tween_update", Phase::PostUpdate, [](App& a) {
            auto* tweens = a.getResource<Tweens>();
            auto* time = a.getResource<Time>();
            tweens->update(time ? time->delta : 0.f, a.world(), a.getResource<Events<TweenFinished>>());
        });
    }
    DRIFT_PLUGIN(TweenPlugin)
};

} // namespace drift
