#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/Query.hpp>
#include <drift/gameplay/Timer.hpp>
#include <drift/resources/Time.hpp>

namespace drift {

inline void timerTickSystem(Res<Time> time, QueryMut<Timer> timers, EventWriter<TimerFinished> finished) {
    float dt = time->delta;
    timers.iterWithEntity([&](EntityId e, Timer& t) {
        if (!t.tick(dt)) return;
        for (uint32_t i = 0; i < t.timesFinished; ++i) finished.send({e, t.tag});
    });
}

// Ticks every entity's Timer in PreUpdate, so Update systems see
// justFinished() in the same frame. TimerFinished events, like all events,
// are readable the frame after they are sent.
class TimerPlugin : public Plugin {
public:
    void build(App& app) override {
        app.world().registerComponent<Timer>("Timer");
        app.initEvent<TimerFinished>();
        app.addSystem<timerTickSystem>("timer_tick", Phase::PreUpdate);
    }
    DRIFT_PLUGIN(TimerPlugin)
};

} // namespace drift
