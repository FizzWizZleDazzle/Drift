// App-level tests: the split frame (pollFrame, simulateAsync,
// finishSimulation), BehaviorPlugin and ShapePlugin. They open a real
// window, so ctest runs them with SDL's offscreen video driver.

#include "Test.hpp"

#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/Query.hpp>
#include <drift/components/Behavior.hpp>
#include <drift/components/Shape.hpp>
#include <drift/plugins/BehaviorPlugin.hpp>
#include <drift/plugins/RendererPlugin.hpp>
#include <drift/plugins/ShapePlugin.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>

#include <SDL3/SDL.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace drift;

namespace {

// Stands in for a resource that wraps thread-bound state.
struct BoundRes : Resource {
    bool mainThreadOnly() const override { return true; }
    std::thread::id ranOn;
};

struct Seq : Resource {
    std::vector<std::string> order;
};

struct SlowRes : Resource {
    std::thread::id ranOn;
};

struct OtherRes : Resource {
    std::thread::id ranOn;
};

void slowSystem(ResMut<SlowRes> r) {
    r->ranOn = std::this_thread::get_id();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
}

void boundSystem(ResMut<BoundRes> b, ResMut<Seq> s) {
    b->ranOn = std::this_thread::get_id();
    s->order.push_back("bound");
}

void afterBound(ResMut<Seq> s) { s->order.push_back("after"); }

void otherSystem(ResMut<OtherRes> o) { o->ranOn = std::this_thread::get_id(); }

Config testConfig() {
    Config cfg;
    cfg.title = "drift app tests";
    cfg.width = 320;
    cfg.height = 180;
    cfg.vsync = false;
    cfg.threadCount = 4;
    return cfg;
}

double seconds(uint64_t ticks) { return double(ticks) / double(SDL_GetPerformanceFrequency()); }

} // namespace

TEST(simulate_async_overlaps_the_caller) {
    App app;
    app.setConfig(testConfig());
    app.addResource<BoundRes>();
    app.addResource<Seq>();
    app.addResource<SlowRes>();
    app.addResource<OtherRes>();
    app.update<slowSystem>("slow");
    app.postUpdate<boundSystem>("bound");
    app.postUpdate<afterBound>("after_bound");
    app.postUpdate<otherSystem>("other");
    REQUIRE(app.start());

    REQUIRE(app.pollFrame());
    uint64_t t0 = SDL_GetPerformanceCounter();
    app.simulateAsync();
    // The caller's own work, while the simulation runs.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    uint64_t t1 = SDL_GetPerformanceCounter();
    app.finishSimulation();
    SimulationTiming sim = app.lastSimulation();
    app.endFrame();

    auto main = std::this_thread::get_id();
    CHECK(sim.async);
    CHECK(app.getResource<SlowRes>()->ranOn != main);
    CHECK(app.getResource<OtherRes>()->ranOn != main);
    // Systems on main-thread resources waited for finishSimulation().
    CHECK(app.getResource<BoundRes>()->ranOn == main);
    const auto& order = app.getResource<Seq>()->order;
    REQUIRE(order.size() == 2);
    CHECK(order[0] == "bound");
    CHECK(order[1] == "after");
    // The background part started before the caller finished its work and
    // ran for most of it.
    CHECK(sim.start < t1);
    CHECK(sim.end > t0);
    double overlap = seconds(std::min(sim.end, t1) - std::max(sim.start, t0));
    CHECK(overlap > 0.015);
}

TEST(simulate_runs_on_the_caller) {
    App app;
    app.setConfig(testConfig());
    app.addResource<BoundRes>();
    app.addResource<Seq>();
    app.addResource<SlowRes>();
    app.addResource<OtherRes>();
    app.postUpdate<boundSystem>("bound");
    app.postUpdate<afterBound>("after_bound");
    REQUIRE(app.start());
    REQUIRE(app.beginFrame());
    app.endFrame();
    CHECK(!app.lastSimulation().async);
    CHECK(app.getResource<BoundRes>()->ranOn == std::this_thread::get_id());
    CHECK(app.getResource<Seq>()->order.size() == 2);
}

namespace {
void step(App& app, int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    REQUIRE(app.beginFrame());
    app.endFrame();
}
} // namespace

TEST(behaviors_lifetime_oscillate_follow) {
    App app;
    app.setConfig(testConfig());
    app.addPlugin<BehaviorPlugin>();
    REQUIRE(app.start());
    World& w = app.world();
    ComponentId tId = w.componentRegistry().has<Transform2D>() ? w.componentRegistry().get<Transform2D>()
                                                               : w.registerComponent<Transform2D>("Transform2D");
    auto spawnAt = [&](Vec2 p) {
        EntityId e = w.createEntity();
        Transform2D t;
        t.position = p;
        w.set<Transform2D>(e, tId, t);
        return e;
    };
    auto reg = [&](auto value) {
        using T = decltype(value);
        return w.componentRegistry().get<T>();
    };
    EntityId dies = spawnAt({0, 0});
    w.set<Lifetime>(dies, reg(Lifetime{}), Lifetime{0.05f});
    EntityId target = spawnAt({300, 0});
    EntityId follower = spawnAt({0, 0});
    Follow f;
    f.target = target;
    f.speed = 10000.f;
    f.distance = 20.f;
    w.set<Follow>(follower, reg(Follow{}), f);
    EntityId swing = spawnAt({50, 50});
    Oscillate o;
    o.amplitude = {0.f, 10.f};
    o.frequency = 2.f;
    w.set<Oscillate>(swing, reg(Oscillate{}), o);

    step(app, 5);
    CHECK(w.isAlive(dies));
    for (int i = 0; i < 6; ++i) step(app, 20);
    CHECK(!w.isAlive(dies));
    const auto* ft = w.get<Transform2D>(follower, tId);
    REQUIRE(ft);
    CHECK_NEAR(ft->position.x, 280.f, 0.5f);
    const auto* st = w.get<Transform2D>(swing, tId);
    REQUIRE(st);
    CHECK(st->position.x == 50.f);
    CHECK(std::fabs(st->position.y - 50.f) <= 10.001f);
    CHECK(std::fabs(st->position.y - 50.f) > 0.01f);
}

TEST(shape_component_draws) {
    App app;
    app.setConfig(testConfig());
    app.addPlugin<RendererPlugin>();
    app.addPlugin<ShapePlugin>();
    REQUIRE(app.start());
    World& w = app.world();
    ComponentId tId = w.componentRegistry().get<Transform2D>();
    EntityId e = w.createEntity();
    w.set<Transform2D>(e, tId, Transform2D{});
    Shape2D s;
    s.kind = ShapeKind::Circle;
    s.radius = 20.f;
    w.set<Shape2D>(e, w.componentRegistry().get<Shape2D>(), s);
    // The forms the Shapes page shows.
    Commands& cmd = app.commands();
    cmd.spawn().insert(Transform2D{.position = {480.f, 530.f}},
                       Shape2D{.size = {960.f, 12.f}, .color = {60, 66, 96, 255}});
    cmd.spawn().insert(Transform2D{.position = {200.f, 200.f}},
                       Shape2D{.kind = ShapeKind::Circle, .radius = 40.f,
                               .color = {255, 210, 60, 255}, .outline = 3.f});
    REQUIRE(app.beginFrame());
    app.endFrame();
    // Draw calls are reported for the previous frame.
    REQUIRE(app.beginFrame());
    CHECK(app.getResource<RendererResource>()->drawCalls() > 0);
    app.endFrame();
}
