// Procedural generation: golden values for the seeded generators (the same
// numbers must come out on every platform, the web build included), and the
// properties each generator promises.

#include "Test.hpp"

#include <drift/gameplay/Pathfinding.hpp>
#include <drift/gameplay/Steering.hpp>
#include <drift/math/Noise.hpp>
#include <drift/math/Random.hpp>
#include <drift/plugins/RandomPlugin.hpp>
#include <drift/procgen/Generators.hpp>
#include <drift/procgen/Grid.hpp>
#include <drift/procgen/WeightedTable.hpp>
#include <drift/procgen/Wfc.hpp>

#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace drift;

namespace {

uint64_t hashGrid(const Grid& g) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int32_t v : g.cells) {
        h ^= static_cast<uint32_t>(v);
        h *= 0x100000001b3ULL;
    }
    return h;
}

bool allConnected(const Grid& g, int32_t value) { return findRegions(g, value).size() == 1; }

} // namespace

// ---- Golden values ----

TEST(procgen_rng_golden) {
    Rng r(42);
    CHECK(r.nextU32() == 1898997482u);
    CHECK(r.nextU32() == 1014631766u);
    CHECK(r.nextU32() == 4096008554u);
    CHECK(r.nextU32() == 633901381u);

    Rng f(7);
    CHECK(f.next01() == 0.823437572f);
    CHECK(f.range(-5.f, 5.f) == -2.31106806f);
    CHECK(f.range(10.f, 20.f) == 16.7585068f);
    CHECK(f.range(1, 6) == 5);
    CHECK(f.range(1, 6) == 1);
    CHECK(f.range(-100, 100) == 55);

    CHECK(Rng::hash("caves") == 0xd3d8483b1e6e9371ull);
}

TEST(procgen_noise_golden) {
    Noise n(1234);
    CHECK(n.value(3.7f, 1.2f) == -0.039930094f);
    CHECK(n.perlin(3.7f, 1.2f) == -0.132178307f);
    CHECK(n.simplex(3.7f, 1.2f) == 0.284303397f);
    CHECK(n.fbm(NoiseType::Perlin, 0.37f, 2.11f, 5) == 0.321074277f);
    NoiseOptions o;
    o.frequency = 0.05f;
    o.octaves = 4;
    o.warp = 8.f;
    CHECK(n.sample(o, 13.f, 27.f) == -0.551405251f);
}

TEST(procgen_generators_golden) {
    Rng c(99);
    Grid cave = generateCave(c, 40, 24);
    CHECK(hashGrid(cave) == 0x627cb7cfd970fe88ull);
    CHECK(cave.count(GridFloor) == 531);

    Rng d(5);
    Dungeon dungeon = generateDungeon(d, 48, 30);
    CHECK(hashGrid(dungeon.grid) == 0xb2e62da41c206ca8ull);
    CHECK(dungeon.rooms.size() == 19);
    CHECK(dungeon.grid.count(GridDoor) == 27);

    Rng w(3);
    Grid walk = randomWalk(w, 40, 24);
    CHECK(hashGrid(walk) == 0xb3fb22d77478d94dull);
    CHECK(walk.count(GridFloor) == 334);

    Rng p(11);
    auto pts = poissonDisk(p, {0, 0, 100, 100}, 10.f);
    REQUIRE(pts.size() == 66);
    CHECK(pts[0].x == 88.6623688f);
    CHECK(pts[0].y == 36.141552f);
    CHECK(pts.back().x == 35.5087624f);
    CHECK(pts.back().y == 86.1790924f);

    Grid sample(6, 6, 0);
    sample.fillRect(0, 0, 6, 1, 1);
    sample.fillRect(0, 5, 6, 1, 1);
    sample.fillRect(0, 0, 1, 6, 1);
    sample.fillRect(5, 0, 1, 6, 1);
    sample.set(2, 2, 2);
    Rng wr(8);
    Grid wfc = generateWfc(wr, WfcRules::fromSample(sample), 20, 12);
    CHECK(wfc.width == 20);
    CHECK(hashGrid(wfc) == 0xd97f131ea6eacb11ull);

    WeightedTable<std::string> loot;
    loot.add("coin", 70).add("potion", 25).add("sword", 5);
    Rng lr(1);
    std::vector<std::string> rolls = loot.roll(lr, 6);
    std::vector<std::string> expected = {"potion", "coin", "sword", "coin", "coin", "potion"};
    CHECK(rolls == expected);
}

// ---- Rng additions ----

TEST(procgen_rng_state_and_streams) {
    Rng a(1234);
    for (int i = 0; i < 5; ++i) a.nextU32();
    Rng b = Rng::fromState(a.state(), a.increment());
    for (int i = 0; i < 20; ++i) CHECK(a.nextU32() == b.nextU32());

    Rng s1(9), s2(9);
    Rng c1 = s1.split(), c2 = s2.split();
    CHECK(c1.nextU32() == c2.nextU32());
    CHECK(s1.nextU32() == s2.nextU32());

    Random random(77);
    Rng x = random.stream("loot"), y = random.stream("loot"), z = random.stream("level");
    uint32_t xv = x.nextU32();
    CHECK(xv == y.nextU32());
    CHECK(xv != z.nextU32());
    // Using the shared generator does not move the streams.
    random.rng.nextU32();
    CHECK(random.stream("loot").nextU32() == xv);
}

// ---- Noise ----

TEST(procgen_noise_options) {
    Noise n(5);
    NoiseOptions plain;
    plain.type = NoiseType::Perlin;
    plain.frequency = 0.1f;
    plain.octaves = 3;
    // No warp: the same as fbm at the scaled point.
    CHECK(n.sample(plain, 12.f, 7.f) == n.fbm(NoiseType::Perlin, 1.2f, 0.7f, 3));
    NoiseOptions warped = plain;
    warped.warp = 10.f;
    int differ = 0;
    for (int i = 0; i < 20; ++i) {
        float x = float(i) * 3.3f, y = float(i) * 1.7f;
        float v = n.sample(warped, x, y);
        CHECK(v >= -1.f && v <= 1.f);
        if (v != n.sample(plain, x, y)) ++differ;
    }
    CHECK(differ > 15);
    float wx = 4.f, wy = 4.f;
    n.warp(wx, wy, 0.f);
    CHECK(wx == 4.f && wy == 4.f);
    n.warp(wx, wy, 3.f, 0.2f);
    CHECK(std::fabs(wx - 4.f) <= 3.f && std::fabs(wy - 4.f) <= 3.f);

    Grid g = gridFromNoise(5, 30, 20, plain, 0.f);
    CHECK(g.count(GridWall) > 0);
    CHECK(g.count(GridFloor) > 0);
    CHECK(g == gridFromNoise(5, 30, 20, plain, 0.f));
}

// ---- Grids and regions ----

TEST(procgen_grid_regions) {
    Grid g(10, 6, GridWall);
    g.fillRect(1, 1, 3, 3, GridFloor);   // 9 cells
    g.fillRect(6, 1, 3, 2, GridFloor);   // 6 cells
    g.set(8, 4, GridFloor);              // 1 cell
    CHECK(g.get(-1, 0) == GridWall);
    CHECK(g.get(-1, 0, 7) == 7);
    auto regions = findRegions(g, GridFloor);
    REQUIRE(regions.size() == 3);
    CHECK(regions[0].size() == 9);
    CHECK(regions[1].size() == 6);
    CHECK(regions[2].size() == 1);

    Grid a = g;
    CHECK(removeSmallRegions(a, GridFloor, 2, GridWall) == 1);
    CHECK(a.count(GridFloor) == 15);
    Grid b = g;
    keepLargestRegion(b, GridFloor, GridWall);
    CHECK(b.count(GridFloor) == 9);
    Grid c = g;
    CHECK(connectRegions(c, GridFloor) == 2);
    CHECK(allConnected(c, GridFloor));

    Grid reach = floodFill(g, {2, 2});
    CHECK(reach.count(1) == 9);

    NavGrid nav = navGridFrom(g, {GridWall}, 16.f);
    CHECK(nav.walkable({2, 2}));
    CHECK(!nav.walkable({0, 0}));
    CHECK(nav.cellSize == 16.f);
}

// ---- Generators: properties ----

TEST(procgen_cave_properties) {
    for (uint64_t seed = 1; seed <= 5; ++seed) {
        Rng rng(seed);
        Grid cave = generateCave(rng, 60, 40);
        CHECK(cave.width == 60 && cave.height == 40);
        for (int32_t x = 0; x < 60; ++x) CHECK(cave.get(x, 0) == GridWall && cave.get(x, 39) == GridWall);
        for (int32_t y = 0; y < 40; ++y) CHECK(cave.get(0, y) == GridWall && cave.get(59, y) == GridWall);
        CHECK(allConnected(cave, GridFloor));
        CHECK(cave.count(GridFloor) > 600);
        Rng again(seed);
        CHECK(generateCave(again, 60, 40) == cave);
    }
    CaveOptions open;
    open.fill = 0.f;
    open.steps = 0;
    Rng rng(1);
    CHECK(generateCave(rng, 10, 10, open).count(GridFloor) == 64);
}

TEST(procgen_dungeon_properties) {
    for (uint64_t seed = 1; seed <= 8; ++seed) {
        Rng rng(seed);
        DungeonOptions o;
        o.minRoom = 3;
        o.maxRoom = 8;
        Dungeon d = generateDungeon(rng, 64, 40, o);
        REQUIRE(d.rooms.size() >= 4);
        for (size_t i = 0; i < d.rooms.size(); ++i) {
            const GridRect& r = d.rooms[i];
            CHECK(r.w >= 3 && r.h >= 3 && r.w <= 8 && r.h <= 8);
            CHECK(r.x >= 1 && r.y >= 1 && r.x + r.w <= 63 && r.y + r.h <= 39);
            for (size_t j = i + 1; j < d.rooms.size(); ++j) {
                const GridRect& s = d.rooms[j];
                bool apart = r.x + r.w <= s.x || s.x + s.w <= r.x || r.y + r.h <= s.y || s.y + s.h <= r.y;
                CHECK(apart);
            }
        }
        // Every room is reachable once doors count as floor.
        Grid walk = d.grid;
        for (int32_t& v : walk.cells) if (v == GridDoor) v = GridFloor;
        Grid reach = floodFill(walk, d.rooms[0].center());
        for (const GridRect& r : d.rooms) CHECK(reach.get(r.center()) == 1);
        // Doors sit between two walls.
        for (int32_t y = 0; y < 40; ++y)
            for (int32_t x = 0; x < 64; ++x) {
                if (d.grid.get(x, y) != GridDoor) continue;
                bool across = (d.grid.get(x - 1, y) == GridWall && d.grid.get(x + 1, y) == GridWall) ||
                              (d.grid.get(x, y - 1) == GridWall && d.grid.get(x, y + 1) == GridWall);
                CHECK(across);
            }
    }
}

TEST(procgen_walk_properties) {
    Rng rng(4);
    WalkOptions o;
    o.coverage = 0.3f;
    o.walkers = 3;
    o.brush = 2;
    Grid g = randomWalk(rng, 50, 30, o);
    CHECK(g.count(GridFloor) >= size_t(0.3f * 48 * 28));
    CHECK(allConnected(g, GridFloor));
    for (int32_t x = 0; x < 50; ++x) CHECK(g.get(x, 0) == GridWall && g.get(x, 29) == GridWall);
}

TEST(procgen_poisson_properties) {
    Rng rng(21);
    Rect area{-50, 20, 300, 200};
    auto pts = poissonDisk(rng, area, 12.f);
    CHECK(pts.size() > 150);
    for (size_t i = 0; i < pts.size(); ++i) {
        CHECK(pts[i].x >= area.x && pts[i].y >= area.y && pts[i].x < area.x + area.w && pts[i].y < area.y + area.h);
        for (size_t j = i + 1; j < pts.size(); ++j) {
            float dx = pts[i].x - pts[j].x, dy = pts[i].y - pts[j].y;
            CHECK(dx * dx + dy * dy >= 144.f);
        }
    }
    // A mask: only the left half.
    auto left = poissonDisk(rng, area, 12.f, 30, [&](Vec2 p) { return p.x < area.x + area.w * 0.5f; });
    for (Vec2 p : left) CHECK(p.x < area.x + area.w * 0.5f);

    Rng d(2);
    Dungeon dungeon = generateDungeon(d, 50, 30);
    auto onFloor = poissonDiskOnGrid(rng, dungeon.grid, GridFloor, 3.f);
    CHECK(onFloor.size() > 20);
    std::set<size_t> roomsHit;
    for (Vec2 p : onFloor) {
        GridPoint c{int32_t(std::floor(p.x)), int32_t(std::floor(p.y))};
        CHECK(dungeon.grid.get(c) == GridFloor);
        for (size_t r = 0; r < dungeon.rooms.size(); ++r)
            if (dungeon.rooms[r].contains(c)) roomsHit.insert(r);
    }
    CHECK(roomsHit.size() == dungeon.rooms.size());
}

TEST(procgen_wfc_rules) {
    // 0 = water, 1 = sand, 2 = grass: water never touches grass.
    WfcRules rules;
    rules.addTile(0, 2.f);
    rules.addTile(1, 1.f);
    rules.addTile(2, 2.f);
    rules.allowAll(0, 0);
    rules.allowAll(1, 1);
    rules.allowAll(2, 2);
    rules.allowAll(0, 1);
    rules.allowAll(1, 2);
    CHECK(rules.allowed(0, 1, WfcDirection::Up));
    CHECK(!rules.allowed(0, 2, WfcDirection::Right));
    Rng rng(6);
    Grid g = generateWfc(rng, rules, 30, 20);
    REQUIRE(g.width == 30);
    for (int32_t y = 0; y < 20; ++y)
        for (int32_t x = 0; x < 30; ++x) {
            int32_t v = g.get(x, y);
            if (x + 1 < 30) CHECK(rules.allowed(v, g.get(x + 1, y), WfcDirection::Right));
            if (y + 1 < 20) CHECK(rules.allowed(v, g.get(x, y + 1), WfcDirection::Down));
        }
    CHECK(g.count(0) > 0 && g.count(2) > 0);

    // A preset pins cells; -1 leaves them free.
    WfcOptions o;
    o.preset = Grid(30, 20, -1);
    o.preset.set(0, 0, 0);
    o.preset.set(29, 19, 2);
    Grid p = generateWfc(rng, rules, 30, 20, o);
    REQUIRE(p.width == 30);
    CHECK(p.get(0, 0) == 0);
    CHECK(p.get(29, 19) == 2);

    // Water next to grass with no sand between cannot be completed.
    WfcOptions impossible;
    impossible.attempts = 2;
    impossible.preset = Grid(2, 1, -1);
    impossible.preset.set(0, 0, 0);
    impossible.preset.set(1, 0, 2);
    CHECK(generateWfc(rng, rules, 2, 1, impossible).width == 0);

    // Learned from a sample: every pair in the output appears in the sample.
    Grid sample(5, 5, 1);
    sample.fillRect(1, 1, 3, 3, 0);
    WfcRules learned = WfcRules::fromSample(sample);
    CHECK(learned.tileCount() == 2);
    CHECK(learned.weights()[learned.indexOf(1)] == 16.f);
    Grid out = generateWfc(rng, learned, 16, 16);
    CHECK(out.width == 16);
}

TEST(procgen_weighted_table) {
    WeightedTable<int> t;
    Rng empty(1);
    CHECK(t.roll(empty) == nullptr);
    t.add(1, 1.f).add(2, 0.f).add(3, 3.f);
    CHECK(t.totalWeight() == 4.f);
    Rng rng(3);
    int counts[4] = {};
    for (int i = 0; i < 4000; ++i) ++counts[*t.roll(rng)];
    CHECK(counts[2] == 0);
    CHECK(counts[1] > 800 && counts[1] < 1200);
    CHECK(counts[3] > 2800 && counts[3] < 3200);
    auto unique = t.rollUnique(rng, 5);
    CHECK(unique.size() == 2);   // the zero-weight entry never comes up
    CHECK(unique[0] != unique[1]);
}

// ---- Flow fields ----

TEST(procgen_flow_field) {
    NavGrid grid(8, 5, 10.f);
    for (int32_t y = 0; y < 4; ++y) grid.setBlocked({4, y});   // wall with a gap at the bottom
    FlowField field(grid, {{7, 0}}, DiagonalMode::Never);
    CHECK(field.cost({7, 0}) == 0.f);
    CHECK(field.cost({6, 0}) == 1.f);
    CHECK(!field.reachable({4, 0}));
    CHECK((field.next({7, 0}) == GridPoint{7, 0}));
    // From the left side the way leads down to the gap.
    GridPoint c{0, 0};
    int steps = 0;
    while (c != GridPoint{7, 0} && steps < 50) {
        c = field.next(c);
        ++steps;
    }
    CHECK((c == GridPoint{7, 0}));
    auto path = findPath(grid, {0, 0}, {7, 0}, PathOptions{.diagonal = DiagonalMode::Never});
    CHECK(float(steps) == field.cost({0, 0}));
    CHECK(pathCost(grid, path) == field.cost({0, 0}));
    Vec2 dir = field.direction(Vec2{5.f, 5.f});   // cell (0, 0)
    CHECK(dir.y == 1.f || dir.x == 1.f);

    // Mud costs more: the field goes around it when that is cheaper.
    NavGrid mud(5, 3);
    mud.setCost({2, 1}, 10.f);
    FlowField around(mud, {{4, 1}}, DiagonalMode::Never);
    CHECK((around.next({1, 1}) != GridPoint{2, 1}));
    CHECK(around.cost({0, 1}) == 6.f);

    FlowField bounded;
    bounded.build(grid, {{7, 0}}, DiagonalMode::Never, 3.f);
    CHECK(bounded.reachable({5, 0}));
    CHECK(!bounded.reachable({0, 0}));
}

// ---- Steering ----

TEST(procgen_steering) {
    Vec2 f = steer::seek({0, 0}, {0, 0}, {10, 0}, 5.f);
    CHECK(f.x == 5.f && f.y == 0.f);
    Vec2 a = steer::arrive({0, 0}, {0, 0}, {5, 0}, 10.f, 10.f);
    CHECK_NEAR(a.x, 5.f, 1e-5f);
    CHECK(steer::flee({0, 0}, {0, 0}, {100, 0}, 5.f, 10.f).x == 0.f);
    CHECK(steer::flee({0, 0}, {0, 0}, {5, 0}, 5.f, 10.f).x == -5.f);
    CHECK_NEAR(steer::truncate({30, 40}, 5.f).length(), 5.f, 1e-5f);
    const Vec2 crowd[] = {{0, 0}, {1, 0}, {0, 1}};
    Vec2 s = steer::separation({0, 0}, crowd, 5.f, 2.f);
    CHECK(s.x < 0.f && s.y < 0.f);
    CHECK_NEAR(s.length(), 2.f, 1e-5f);
    Vec2 coh = steer::cohesion({0, 0}, {0, 0}, crowd, 5.f, 2.f);
    CHECK(coh.x > 0.f && coh.y > 0.f);
    const Vec2 path[] = {{0, 0}, {10, 0}, {10, 10}};
    size_t index = 0;
    Vec2 step = steer::followPath({0, 0}, {0, 0}, path, index, 4.f, 1.f);
    CHECK(index == 1);
    CHECK(step.x == 4.f);
    Rng rng(1);
    float angle = 0.f;
    Vec2 w = steer::wander({1, 0}, angle, rng, 3.f);
    CHECK(w.length() <= 4.01f);
}
