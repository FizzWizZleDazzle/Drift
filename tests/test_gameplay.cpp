#include "Test.hpp"

#include <drift/World.hpp>
#include <drift/Events.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/gameplay/Easing.hpp>
#include <drift/gameplay/Tween.hpp>
#include <drift/gameplay/Timer.hpp>
#include <drift/gameplay/InputActions.hpp>
#include <drift/gameplay/SaveData.hpp>
#include <drift/gameplay/SpatialHash.hpp>
#include <drift/gameplay/Pathfinding.hpp>
#include <drift/math/Vector.hpp>
#include <drift/math/Intersect.hpp>
#include <drift/math/Random.hpp>
#include <drift/math/Noise.hpp>
#include <drift/resources/InputResource.hpp>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <numeric>
#include <set>
#include <string>

using namespace drift;

// ---------------------------------------------------------------------------
// Easing
// ---------------------------------------------------------------------------

TEST(easing_endpoints) {
    for (int i = 0; i < static_cast<int>(Ease::Count_); ++i) {
        Ease e = static_cast<Ease>(i);
        CHECK_NEAR(ease(e, 0.f), 0.f, 1e-6);
        CHECK_NEAR(ease(e, 1.f), 1.f, 1e-6);
        // Continuous at the ends: no jump just inside the range (the Expo
        // curves have the standard 2^-10 step; Circ is steep near its ends).
        CHECK_NEAR(ease(e, 1e-6f), 0.f, 2e-3);
        CHECK_NEAR(ease(e, 1.f - 1e-6f), 1.f, 2e-3);
        // Clamped outside [0, 1].
        CHECK(ease(e, -1.f) == 0.f);
        CHECK(ease(e, 2.f) == 1.f);
    }
}

TEST(easing_shapes) {
    CHECK_NEAR(ease(Ease::Linear, 0.25f), 0.25f, 1e-6);
    CHECK_NEAR(ease(Ease::InOutQuad, 0.5f), 0.5f, 1e-5);
    CHECK_NEAR(ease(Ease::InOutCubic, 0.5f), 0.5f, 1e-5);
    CHECK(ease(Ease::InQuad, 0.5f) < 0.5f);
    CHECK(ease(Ease::OutQuad, 0.5f) > 0.5f);
    CHECK(ease(Ease::InBack, 0.2f) < 0.f);   // anticipation
    float maxOutBack = 0.f, maxSpring = 0.f;
    for (int i = 0; i <= 100; ++i) {
        maxOutBack = std::max(maxOutBack, ease(Ease::OutBack, i / 100.f));
        maxSpring = std::max(maxSpring, ease(Ease::Spring, i / 100.f));
    }
    CHECK(maxOutBack > 1.05f);
    CHECK(maxSpring > 1.05f);
    for (int i = 0; i <= 100; ++i) {
        float b = ease(Ease::OutBounce, i / 100.f);
        CHECK(b >= -1e-6f && b <= 1.f + 1e-6f);
    }
}

// ---------------------------------------------------------------------------
// Vector helpers
// ---------------------------------------------------------------------------

TEST(vector_helpers) {
    CHECK_NEAR(cross({1, 0}, {0, 1}), 1.f, 1e-6);
    CHECK(approx(perp({1, 0}), {0, 1}));
    CHECK(approx(rotate({1, 0}, Pi * 0.5f), {0, 1}, 1e-5f));
    CHECK_NEAR(angle({0, 1}), Pi * 0.5f, 1e-6);
    CHECK_NEAR(angleBetween({1, 0}, {0, -1}), -Pi * 0.5f, 1e-6);
    CHECK_NEAR(distance({0, 0}, {3, 4}), 5.f, 1e-6);
    CHECK(approx(lerp(Vec2{0, 0}, Vec2{10, 20}, 0.5f), {5, 10}));
    CHECK(approx(clampLength({30, 40}, 5.f), {3, 4}, 1e-5f));
    CHECK(approx(moveTowards(Vec2{0, 0}, Vec2{10, 0}, 3.f), {3, 0}));
    CHECK(approx(moveTowards(Vec2{0, 0}, Vec2{1, 0}, 3.f), {1, 0}));
    CHECK_NEAR(moveTowards(5.f, 0.f, 2.f), 3.f, 1e-6);
    CHECK_NEAR(wrapAngle(3.f * Pi), Pi, 1e-5);
    CHECK_NEAR(lerpAngle(Pi - 0.1f, -Pi + 0.1f, 0.5f), Pi, 1e-5);
    CHECK_NEAR(remap(5.f, 0.f, 10.f, 100.f, 200.f), 150.f, 1e-4);

    // smoothDamp converges without overshoot.
    float x = 0.f, v = 0.f;
    for (int i = 0; i < 300; ++i) {
        x = smoothDamp(x, 10.f, v, 0.3f, 1.f / 60.f);
        CHECK(x <= 10.f + 1e-4f);
    }
    CHECK_NEAR(x, 10.f, 1e-2);
}

// ---------------------------------------------------------------------------
// Intersections
// ---------------------------------------------------------------------------

TEST(intersections) {
    Rect a{0, 0, 10, 10}, b{5, 5, 10, 10}, c{20, 20, 1, 1};
    CHECK(overlaps(a, b));
    CHECK(!overlaps(a, c));
    auto i = intersection(a, b);
    REQUIRE(i.has_value());
    CHECK_NEAR(i->w, 5.f, 1e-6);
    CHECK(approx(penetration(Rect{8, 0, 10, 10}, Rect{0, 0, 10, 10}), {2, 0}));

    CHECK(overlaps(Circle{{0, 0}, 1}, Circle{{1.5f, 0}, 1}));
    CHECK(!overlaps(Circle{{0, 0}, 1}, Circle{{2.5f, 0}, 1}));
    CHECK(overlaps(Circle{{12, 5}, 2.5f}, a));
    CHECK(!overlaps(Circle{{13, 13}, 2.f}, a));   // corner: distance sqrt(18) > 2

    auto p = intersect(Segment{{0, 0}, {10, 10}}, Segment{{0, 10}, {10, 0}});
    REQUIRE(p.has_value());
    CHECK(approx(*p, {5, 5}, 1e-5f));
    CHECK(!intersect(Segment{{0, 0}, {1, 0}}, Segment{{0, 1}, {1, 1}}).has_value());
    CHECK(!intersect(Segment{{0, 0}, {1, 1}}, Segment{{2, 0}, {3, -5}}).has_value());

    auto hr = raycast({-5, 5}, {1, 0}, a);
    REQUIRE(hr.has_value());
    CHECK_NEAR(hr->t, 5.f, 1e-5);
    CHECK(approx(hr->normal, {-1, 0}));
    CHECK(!raycast({-5, 5}, {-1, 0}, a).has_value());
    CHECK(!raycast({-5, 5}, {1, 0}, a, 4.f).has_value());

    auto hc = raycast({-10, 0}, {1, 0}, Circle{{0, 0}, 2});
    REQUIRE(hc.has_value());
    CHECK_NEAR(hc->t, 8.f, 1e-5);
    CHECK(approx(hc->normal, {-1, 0}, 1e-5f));

    CHECK(overlaps(Segment{{-1, 5}, {11, 5}}, a));
    CHECK(!overlaps(Segment{{-1, 15}, {11, 15}}, a));
    CHECK(overlaps(Segment{{-3, 0}, {3, 0}}, Circle{{0, 0.5f}, 1}));
    CHECK(approx(closestPoint(Segment{{0, 0}, {10, 0}}, {4, 7}), {4, 0}));
}

// ---------------------------------------------------------------------------
// RNG and noise
// ---------------------------------------------------------------------------

TEST(rng_determinism) {
    Rng a(1234), b(1234), c(999);
    bool differs = false;
    for (int i = 0; i < 1000; ++i) {
        uint32_t x = a.nextU32();
        CHECK(x == b.nextU32());
        differs = differs || x != c.nextU32();
    }
    CHECK(differs);

    // Same seed after reseed restarts the sequence.
    Rng r(42);
    uint32_t first = r.nextU32();
    r.reseed(42);
    CHECK(r.nextU32() == first);
}

TEST(rng_ranges) {
    Rng r(7);
    std::set<int> seen;
    for (int i = 0; i < 2000; ++i) {
        int v = r.range(-3, 3);
        CHECK(v >= -3 && v <= 3);
        seen.insert(v);
        float f = r.range(2.f, 5.f);
        CHECK(f >= 2.f && f < 5.f);
        float u = r.next01();
        CHECK(u >= 0.f && u < 1.f);
        CHECK(r.insideCircle(3.f).length() <= 3.f + 1e-4f);
    }
    CHECK(seen.size() == 7);

    // Weighted pick: index 1 has zero weight and must never be chosen;
    // index 2 should win about 3x as often as index 0.
    const float weights[] = {1.f, 0.f, 3.f};
    int counts[3] = {};
    for (int i = 0; i < 20000; ++i) {
        int k = r.weightedIndex(weights);
        REQUIRE(k >= 0 && k < 3);
        counts[k]++;
    }
    CHECK(counts[1] == 0);
    float ratio = float(counts[2]) / float(counts[0]);
    CHECK(ratio > 2.7f && ratio < 3.3f);
    const float none[] = {0.f, -1.f};
    CHECK(r.weightedIndex(none) == -1);

    // Shuffle is a permutation and is deterministic per seed.
    std::array<int, 20> v1{}, v2{};
    std::iota(v1.begin(), v1.end(), 0);
    v2 = v1;
    Rng s1(5), s2(5);
    s1.shuffle(std::span<int>(v1));
    s2.shuffle(std::span<int>(v2));
    CHECK(v1 == v2);
    std::array<int, 20> sorted = v1;
    std::sort(sorted.begin(), sorted.end());
    std::array<int, 20> ident{};
    std::iota(ident.begin(), ident.end(), 0);
    CHECK(sorted == ident);
    CHECK(v1 != ident);
}

TEST(noise_range_and_determinism) {
    Noise n1(3), n2(3), n3(4);
    bool differs = false;
    float lo = 1.f, hi = -1.f;
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            float fx = x * 0.37f, fy = y * 0.41f;
            for (NoiseType t : {NoiseType::Value, NoiseType::Perlin, NoiseType::Simplex}) {
                float a = n1.sample(t, fx, fy);
                CHECK(a == n2.sample(t, fx, fy));
                CHECK(a >= -1.f && a <= 1.f);
                differs = differs || a != n3.sample(t, fx, fy);
                lo = std::min(lo, a);
                hi = std::max(hi, a);
            }
            float f = n1.fbm(NoiseType::Simplex, fx, fy, 5);
            CHECK(f >= -1.f && f <= 1.f);
        }
    }
    CHECK(differs);
    CHECK(hi - lo > 1.f);   // spans a useful range
    // Perlin is zero on lattice points.
    CHECK_NEAR(n1.perlin(3.f, 7.f), 0.f, 1e-6);
}

// ---------------------------------------------------------------------------
// Pathfinding
// ---------------------------------------------------------------------------

static bool contiguous(const std::vector<GridPoint>& path, bool allowDiagonal) {
    for (size_t i = 1; i < path.size(); ++i) {
        int dx = std::abs(path[i].x - path[i - 1].x), dy = std::abs(path[i].y - path[i - 1].y);
        if (dx > 1 || dy > 1 || (dx == 0 && dy == 0)) return false;
        if (!allowDiagonal && dx + dy != 1) return false;
    }
    return true;
}

TEST(astar_open_grid) {
    NavGrid grid(10, 10);
    auto p4 = findPath(grid, {0, 0}, {5, 3}, {.diagonal = DiagonalMode::Never});
    REQUIRE(!p4.empty());
    CHECK(p4.front() == (GridPoint{0, 0}));
    CHECK(p4.back() == (GridPoint{5, 3}));
    CHECK(p4.size() == 9);   // manhattan 8 steps
    CHECK(contiguous(p4, false));

    auto p8 = findPath(grid, {0, 0}, {5, 3});
    CHECK(p8.size() == 6);   // chebyshev 5 steps
    CHECK(contiguous(p8, true));
    CHECK_NEAR(pathCost(grid, p8), 3 * 1.41421356f + 2.f, 1e-4);

    CHECK(findPath(grid, {2, 2}, {2, 2}).size() == 1);
}

TEST(astar_walls_and_weights) {
    // Wall at x = 5 with a gap at y = 9.
    NavGrid grid(10, 10);
    for (int y = 0; y < 9; ++y) grid.setBlocked({5, y});
    auto p = findPath(grid, {0, 0}, {9, 0});
    REQUIRE(!p.empty());
    for (auto c : p) CHECK(grid.walkable(c));
    bool throughGap = std::any_of(p.begin(), p.end(), [](GridPoint c) { return c == GridPoint{5, 9}; });
    CHECK(throughGap);

    // Fully closed: no path.
    grid.setBlocked({5, 9});
    CHECK(findPath(grid, {0, 0}, {9, 0}).empty());
    // Blocked goal: no path.
    CHECK(findPath(grid, {0, 0}, {5, 5}).empty());
    // Out of bounds: no path.
    CHECK(findPath(grid, {0, 0}, {50, 0}).empty());

    // Weights: a straight corridor through expensive cells loses to a detour.
    NavGrid w(7, 3);
    for (int x = 1; x < 6; ++x) w.setCost({x, 1}, 10.f);
    auto pw = findPath(w, {0, 1}, {6, 1}, {.diagonal = DiagonalMode::Never});
    REQUIRE(!pw.empty());
    bool usedMud = std::any_of(pw.begin(), pw.end(), [](GridPoint c) { return c.y == 1 && c.x > 0 && c.x < 6; });
    CHECK(!usedMud);
    CHECK_NEAR(pathCost(w, pw), 8.f, 1e-5);
}

TEST(astar_diagonal_rules) {
    // Two blocked cells touching at a corner: (1,0) and (0,1).
    NavGrid grid(3, 3);
    grid.setBlocked({1, 0});
    grid.setBlocked({0, 1});
    // Squeezing from (0,0) to (1,1) diagonally passes between them.
    CHECK(findPath(grid, {0, 0}, {1, 1}, {.diagonal = DiagonalMode::Always}).size() == 2);
    CHECK(findPath(grid, {0, 0}, {1, 1}, {.diagonal = DiagonalMode::NoCornerCutting}).empty());
    CHECK(findPath(grid, {0, 0}, {1, 1}, {.diagonal = DiagonalMode::AllowOneCorner}).empty());

    NavGrid one(3, 3);
    one.setBlocked({1, 0});
    CHECK(findPath(one, {0, 0}, {1, 1}, {.diagonal = DiagonalMode::AllowOneCorner}).size() == 2);
    CHECK(findPath(one, {0, 0}, {1, 1}, {.diagonal = DiagonalMode::NoCornerCutting}).size() == 3);

    NavGrid conv(4, 4, 32.f, {100.f, 0.f});
    CHECK(conv.worldToCell({100.f + 70.f, 5.f}) == (GridPoint{2, 0}));
    CHECK(approx(conv.cellToWorld({1, 1}), {100.f + 48.f, 48.f}));
}

// ---------------------------------------------------------------------------
// Spatial hash
// ---------------------------------------------------------------------------

TEST(spatial_hash_queries) {
    SpatialHash hash(10.f);
    Rng rng(11);
    std::vector<std::pair<EntityId, Rect>> items;
    for (EntityId id = 1; id <= 300; ++id) {
        Rect r{rng.range(-200.f, 200.f), rng.range(-200.f, 200.f), rng.range(0.f, 35.f), rng.range(0.f, 35.f)};
        items.push_back({id, r});
        hash.insert(id, r);
    }
    // Compare against brute force for random areas; also checks no duplicates.
    for (int q = 0; q < 50; ++q) {
        Rect area{rng.range(-220.f, 200.f), rng.range(-220.f, 200.f), rng.range(1.f, 80.f), rng.range(1.f, 80.f)};
        auto got = hash.queryRect(area);
        std::set<EntityId> unique(got.begin(), got.end());
        CHECK(unique.size() == got.size());
        std::set<EntityId> expect;
        for (auto& [id, r] : items) {
            if (r.x <= area.x + area.w && r.x + r.w >= area.x && r.y <= area.y + area.h && r.y + r.h >= area.y)
                expect.insert(id);
        }
        CHECK(unique == expect);

        Vec2 c{rng.range(-200.f, 200.f), rng.range(-200.f, 200.f)};
        float radius = rng.range(1.f, 60.f);
        auto near = hash.queryRadius(c, radius);
        std::set<EntityId> nearSet(near.begin(), near.end()), nearExpect;
        CHECK(nearSet.size() == near.size());
        for (auto& [id, r] : items) if (overlaps(Circle{c, radius}, r)) nearExpect.insert(id);
        CHECK(nearSet == nearExpect);
    }

    hash.clear();
    hash.insert(1, Vec2{0, 0});
    hash.insert(2, Vec2{5, 0});
    hash.insert(3, Vec2{50, 0});
    CHECK(hash.nearest({4, 0}, 100.f) == 2);
    CHECK(hash.nearest({4, 0}, 100.f, 2) == 1);
    CHECK(hash.nearest({200, 0}, 10.f) == InvalidEntityId);
}

// ---------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------

TEST(timers_and_cooldowns) {
    Timer once = Timer::once(1.f);
    CHECK(!once.tick(0.6f));
    CHECK(once.tick(0.6f));
    CHECK(once.finished());
    CHECK(!once.tick(1.f));   // fires only once
    once.reset();
    CHECK(!once.finished());

    Timer rep = Timer::repeating(0.5f);
    CHECK(!rep.tick(0.4f));
    CHECK(rep.tick(0.2f));
    CHECK(rep.timesFinished == 1);
    CHECK(rep.tick(1.0f));
    CHECK(rep.timesFinished == 2);
    CHECK_NEAR(rep.elapsed, 0.1f, 1e-5);

    Cooldown cd(2.f);
    CHECK(cd.tryUse());
    CHECK(!cd.tryUse());
    cd.tick(1.5f);
    CHECK(!cd.ready());
    cd.tick(0.5f);
    CHECK(cd.tryUse());
}

// ---------------------------------------------------------------------------
// Tweens
// ---------------------------------------------------------------------------

namespace {

EntityId spawn(World& w, Vec2 pos) {
    EntityId e = w.createEntity();
    w.set(e, w.componentRegistry().get<Transform2D>(), Transform2D{pos});
    w.set(e, w.componentRegistry().get<Sprite>(), Sprite{});
    return e;
}

const Transform2D& transformOf(World& w, EntityId e) {
    return *w.get<Transform2D>(e, w.componentRegistry().get<Transform2D>());
}

} // namespace

TEST(tween_move_and_finish_event) {
    World world;
    Tweens tweens;
    Events<TweenFinished> finished;
    EntityId e = spawn(world, {0, 0});

    TweenId id = tweens.on(e).moveTo({100, 0}, 1.f, Ease::Linear).start();
    CHECK(tweens.isActive(id));
    tweens.update(0.25f, world, &finished);
    CHECK_NEAR(transformOf(world, e).position.x, 25.f, 1e-3);
    tweens.update(0.5f, world, &finished);
    CHECK_NEAR(transformOf(world, e).position.x, 75.f, 1e-3);
    tweens.update(0.5f, world, &finished);
    CHECK_NEAR(transformOf(world, e).position.x, 100.f, 1e-4);
    CHECK(!tweens.isActive(id));
    finished.update();
    REQUIRE(finished.count() == 1);
    CHECK(finished.events()[0].id == id);
    CHECK(finished.events()[0].entity == e);
}

TEST(tween_sequence_delay_call_relative) {
    World world;
    Tweens tweens;
    EntityId e = spawn(world, {10, 0});
    std::vector<std::string> log;
    float custom = -1.f;

    tweens.on(e)
        .moveBy({10, 0}, 1.f, Ease::Linear)
        .call([&] { log.push_back("mid"); })
        .delay(0.5f)
        .value(0.f, 8.f, 1.f, Ease::Linear, [&](float v) { custom = v; })
        .fadeTo(0.f, 0.5f)
        .start();

    tweens.update(1.0f, world, nullptr);   // first step completes, call runs
    CHECK_NEAR(transformOf(world, e).position.x, 20.f, 1e-4);
    CHECK(log.size() == 1);
    tweens.update(0.25f, world, nullptr);  // in the delay
    CHECK(custom < 0.f);
    tweens.update(0.75f, world, nullptr);  // delay done + half the value step
    CHECK_NEAR(custom, 4.f, 1e-3);
    tweens.update(1.0f, world, nullptr);   // value done, fade done
    CHECK_NEAR(custom, 8.f, 1e-4);
    const Sprite* s = world.get<Sprite>(e, world.componentRegistry().get<Sprite>());
    CHECK(s->tint.a == 0);
    CHECK(tweens.activeCount() == 0);
}

TEST(tween_loops_yoyo_and_stop) {
    World world;
    Tweens tweens;
    Events<TweenFinished> finished;
    EntityId e = spawn(world, {0, 0});

    // Yoyo twice: 0 -> 10 -> 0, then finished.
    TweenId id = tweens.on(e).moveTo({10, 0}, 1.f, Ease::Linear).yoyo(2).start();
    tweens.update(1.5f, world, &finished);
    CHECK_NEAR(transformOf(world, e).position.x, 5.f, 1e-3);
    tweens.update(0.6f, world, &finished);
    CHECK_NEAR(transformOf(world, e).position.x, 0.f, 1e-4);
    CHECK(!tweens.isActive(id));

    // Restart forever until stopped with complete = true.
    EntityId f = spawn(world, {0, 0});
    TweenId loop = tweens.on(f).rotateTo(2.f, 1.f, Ease::Linear).loop(-1).start();
    tweens.update(3.5f, world, &finished);
    CHECK_NEAR(transformOf(world, f).rotation, 1.f, 1e-3);
    CHECK(tweens.isActive(loop));
    tweens.stop(loop, true);
    finished.update();   // discard earlier events
    tweens.update(0.f, world, &finished);
    CHECK_NEAR(transformOf(world, f).rotation, 2.f, 1e-5);
    finished.update();
    CHECK(finished.count() == 1);

    // A despawned target ends its tween quietly.
    EntityId g = spawn(world, {0, 0});
    TweenId gone = tweens.on(g).moveTo({5, 5}, 1.f).start();
    world.destroyEntity(g);
    tweens.update(0.1f, world, &finished);
    CHECK(!tweens.isActive(gone));

    // Camera zoom.
    EntityId cam = world.createEntity();
    world.set(cam, world.componentRegistry().get<Camera>(), Camera{.zoom = 1.f});
    tweens.on(cam).zoomTo(3.f, 1.f, Ease::Linear).start();
    tweens.update(0.5f, world, nullptr);
    CHECK_NEAR(world.get<Camera>(cam, world.componentRegistry().get<Camera>())->zoom, 2.f, 1e-4);
}

TEST(tween_without_entity_pause_and_timescale) {
    World world;
    Tweens tweens;
    float v = 0.f;
    bool chained = false;
    float w = 0.f;
    TweenId id = tweens.sequence()
        .value(0.f, 10.f, 1.f, Ease::Linear, [&](float x) { v = x; })
        .call([&] {
            // Started from a callback: begins on the next update.
            tweens.sequence().value(0.f, 1.f, 1.f, Ease::Linear, [&](float x) { w = x; }).start();
            chained = true;
        })
        .start();
    tweens.update(0.5f, world, nullptr);
    CHECK_NEAR(v, 5.f, 1e-4);
    tweens.pause(id);
    tweens.update(0.5f, world, nullptr);
    CHECK_NEAR(v, 5.f, 1e-4);
    tweens.resume(id);
    tweens.timeScale = 0.5f;
    tweens.update(0.5f, world, nullptr);   // 0.25 s of tween time
    CHECK_NEAR(v, 7.5f, 1e-4);
    tweens.timeScale = 1.f;
    tweens.update(0.5f, world, nullptr);
    CHECK_NEAR(v, 10.f, 1e-4);
    CHECK(chained);
    CHECK(w == 0.f);
    tweens.update(0.5f, world, nullptr);
    CHECK_NEAR(w, 0.5f, 1e-4);

    // A target without the animated component ends quietly.
    EntityId bare = world.createEntity();
    world.set(bare, world.componentRegistry().get<Transform2D>(), Transform2D{});
    TweenId fade = tweens.on(bare).fadeTo(0.f, 1.f).start();
    tweens.update(0.1f, world, nullptr);
    CHECK(!tweens.isActive(fade));
}

// ---------------------------------------------------------------------------
// Save data
// ---------------------------------------------------------------------------

TEST(save_round_trip) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / ("drift_save_test_" + std::to_string(SDL_GetTicksNS()));
    fs::path file = dir / "nested" / "slot1.json";

    {
        SaveData save;
        CHECK(save.openFile(file));   // missing file = new, empty save
        CHECK(!save.has("highscore"));
        save.set("highscore", 1200);
        save.set("name", std::string("Ada"));
        save.set("volume", 0.75f);
        save.set("unlocked", std::vector<int>{1, 2, 5});
        save.json()["options"]["fullscreen"] = true;
        CHECK(save.dirty());
        CHECK(save.flush());
        CHECK(!save.dirty());
    }
    {
        SaveData save;
        CHECK(save.openFile(file));
        CHECK(save.get("highscore", 0) == 1200);
        CHECK(save.get<std::string>("name", "") == "Ada");
        CHECK_NEAR(save.get("volume", 0.f), 0.75f, 1e-6);
        CHECK((save.get("unlocked", std::vector<int>{}) == std::vector<int>{1, 2, 5}));
        CHECK(save.json()["options"]["fullscreen"].get<bool>());
        CHECK(save.get("missing", 7) == 7);
        CHECK(save.get("name", 3) == 3);   // wrong type falls back
        save.erase("name");
        CHECK(!save.has("name"));
        save.load();                        // discard unsaved change
        CHECK(save.has("name"));
    }
    CHECK(!fs::exists(file.string() + ".tmp"));

    // A corrupt file is reported and leaves an empty store.
    {
        std::FILE* fp = std::fopen(file.string().c_str(), "wb");
        std::fputs("{ not json", fp);
        std::fclose(fp);
        SaveData save;
        CHECK(!save.openFile(file));
        CHECK(!save.has("highscore"));
    }
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Input actions
// ---------------------------------------------------------------------------

namespace {

void keyEvent(InputResource& in, InputActions& a, SDL_Scancode sc, bool down) {
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    ev.key.scancode = sc;
    in.processEvent(ev);
    a.processEvent(ev);
}

void frame(InputResource& in, InputActions& a) {
    in.beginFrame();
    a.beginFrame();
}

} // namespace

TEST(input_binding_text) {
    for (InputBinding b : {InputBinding::key(Key::Space), InputBinding::key(Key::A),
                           InputBinding::key(static_cast<Key>(SDL_SCANCODE_COMMA)),
                           InputBinding::mouse(MouseButton::Right),
                           InputBinding::button(GamepadButton::DPadLeft),
                           InputBinding::axis(GamepadAxis::LeftY, false),
                           InputBinding::axis(GamepadAxis::RightTrigger)}) {
        std::string s = b.toString();
        CHECK(InputBinding::parse(s) == b);
    }
    CHECK(InputBinding::key(Key::Space).toString() == "key:Space");
    CHECK(InputBinding::axis(GamepadAxis::LeftX, false).toString() == "axis:-LeftX");
    CHECK(!InputBinding::parse("key:NoSuchKey").valid());
    CHECK(!InputBinding::parse("garbage").valid());
    CHECK(!InputBinding::parse("axis:LeftX").valid());
}

TEST(input_actions_keys) {
    InputResource input;
    InputActions actions;
    actions.setSource(&input);
    actions.bindAction("jump", InputBinding::key(Key::Space));
    actions.bindAction("jump", InputBinding::key(Key::W));
    actions.bindAxis("turn", InputBinding::key(Key::Left), InputBinding::key(Key::Right));
    actions.bindAxis2D("move", InputBinding::key(Key::A), InputBinding::key(Key::D),
                       InputBinding::key(Key::W), InputBinding::key(Key::S));

    frame(input, actions);
    keyEvent(input, actions, SDL_SCANCODE_SPACE, true);
    CHECK(actions.pressed("jump"));
    CHECK(actions.held("jump"));
    CHECK(!actions.released("jump"));

    frame(input, actions);   // still held, no new events
    CHECK(!actions.pressed("jump"));
    CHECK(actions.held("jump"));

    // A second binding of the same action going down does not re-press it.
    frame(input, actions);
    keyEvent(input, actions, SDL_SCANCODE_W, true);
    CHECK(!actions.pressed("jump"));
    keyEvent(input, actions, SDL_SCANCODE_SPACE, false);
    CHECK(actions.held("jump"));

    frame(input, actions);
    keyEvent(input, actions, SDL_SCANCODE_W, false);
    CHECK(actions.released("jump"));
    CHECK(!actions.held("jump"));

    frame(input, actions);
    keyEvent(input, actions, SDL_SCANCODE_RIGHT, true);
    CHECK_NEAR(actions.axis("turn"), 1.f, 1e-6);
    keyEvent(input, actions, SDL_SCANCODE_LEFT, true);
    CHECK_NEAR(actions.axis("turn"), 0.f, 1e-6);   // opposite keys cancel
    keyEvent(input, actions, SDL_SCANCODE_RIGHT, false);
    CHECK_NEAR(actions.axis("turn"), -1.f, 1e-6);

    // Diagonal 2D input is normalized to length 1; up is -Y.
    keyEvent(input, actions, SDL_SCANCODE_D, true);
    keyEvent(input, actions, SDL_SCANCODE_W, true);
    Vec2 m = actions.axis2D("move");
    CHECK_NEAR(m.length(), 1.f, 1e-5);
    CHECK(m.x > 0.f && m.y < 0.f);

    // Unknown names read as idle.
    CHECK(!actions.held("nope"));
    CHECK(actions.axis("nope") == 0.f);
}

TEST(input_actions_rebind_and_json) {
    InputResource input;
    InputActions actions;
    actions.setSource(&input);
    actions.bindAction("fire", InputBinding::key(Key::Space));
    actions.bindAction("fire", InputBinding::mouse(MouseButton::Left));
    actions.bindStick("move", GamepadAxis::LeftX, GamepadAxis::LeftY);

    // Capture the next key for rebinding.
    frame(input, actions);
    keyEvent(input, actions, SDL_SCANCODE_F, true);
    auto captured = actions.captureNext(input);
    REQUIRE(captured.has_value());
    CHECK(*captured == InputBinding::key(Key::F));
    actions.rebind("fire", 0, 0, *captured);
    CHECK(actions.held("fire"));   // re-evaluated immediately
    CHECK(actions.bindings("fire")[0] == InputBinding::key(Key::F));

    nlohmann::json saved = actions.toJson();
    CHECK(saved["fire"][0][0] == "key:F");
    CHECK(saved["fire"][0][1] == "mouse:Left");
    CHECK(saved["move"].size() == 4);

    // A fresh map with defaults takes the saved bindings back.
    InputActions fresh;
    fresh.bindAction("fire", InputBinding::key(Key::Space));
    fresh.bindStick("move", GamepadAxis::RightX, GamepadAxis::RightY);
    nlohmann::json withJunk = saved;
    withJunk["unknown_action"] = nlohmann::json::array({nlohmann::json::array({"key:Q"})});
    CHECK(fresh.fromJson(withJunk) == 2);
    CHECK(fresh.bindings("fire").size() == 2);
    CHECK(fresh.bindings("fire")[0] == InputBinding::key(Key::F));
    CHECK(fresh.bindings("move", AxisSlot::Up)[0] == InputBinding::axis(GamepadAxis::LeftY, false));
    CHECK(!fresh.has("unknown_action"));

    // Slot count mismatch is ignored.
    nlohmann::json bad = {{"fire", nlohmann::json::array({nlohmann::json::array(), nlohmann::json::array()})}};
    CHECK(fresh.fromJson(bad) == 0);
}

// Gamepad bindings, through an SDL virtual gamepad.
TEST(input_actions_gamepad) {
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        std::printf("  skipped: SDL_Init(GAMEPAD) failed: %s\n", SDL_GetError());
        return;
    }
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "drift test pad";
    SDL_JoystickID jid = SDL_AttachVirtualJoystick(&desc);
    REQUIRE(jid != 0);
    SDL_Joystick* joy = SDL_OpenJoystick(jid);
    REQUIRE(joy != nullptr);

    InputResource input;
    InputActions actions;
    actions.setSource(&input);
    actions.bindAction("jump", InputBinding::button(GamepadButton::South));
    actions.bindAction("shoot", InputBinding::axis(GamepadAxis::RightTrigger));
    actions.bindStick("move", GamepadAxis::LeftX, GamepadAxis::LeftY);

    auto pump = [&] {
        SDL_UpdateJoysticks();
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            input.processEvent(ev);
            actions.processEvent(ev);
        }
    };

    frame(input, actions);
    pump();   // delivers GAMEPAD_ADDED
    REQUIRE(input.gamepadConnected(0));

    frame(input, actions);
    SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_SOUTH, true);
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTX, 32767);
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    pump();
    CHECK(actions.pressed("jump"));
    CHECK(actions.held("shoot"));
    CHECK_NEAR(actions.axis2D("move").x, 1.f, 1e-3);

    // Inside the dead zone the stick reads zero.
    frame(input, actions);
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTX, 3000);   // about 0.09
    SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_SOUTH, false);
    pump();
    CHECK(actions.axis2D("move").x == 0.f);
    CHECK(actions.released("jump"));

    SDL_CloseJoystick(joy);
    SDL_DetachVirtualJoystick(jid);
    SDL_Quit();
}
