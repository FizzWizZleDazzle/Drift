// =============================================================================
// Procgen: walk around generated levels
// =============================================================================
// Four generators build the level, keys 1-4 switch between them and R rolls
// a new seed:
//
//   1 Cave      cellular automata, only the largest cavern kept
//   2 Dungeon   BSP rooms joined by corridors, with doors
//   3 Tunnels   random walkers carving with a 2x2 brush
//   4 Ruins     wave function collapse over rules learned from a small sample
//
// Each level goes into a tilemap made in code (floor variants picked by
// noise), whose wall layer becomes static colliders. Poisson-disk points
// over the floor get coins, gems or slimes from a weighted table; slimes
// chase the player along a flow field and keep apart with steering.
//
// Move with WASD or the arrow keys. F12 saves a screenshot. Run with the
// argument `tour` to switch generator every few seconds.
// =============================================================================

#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/PhysicsSyncSystem.hpp>
#include <drift/Query.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/CameraController.hpp>
#include <drift/components/Physics.hpp>
#include <drift/components/Shape.hpp>
#include <drift/components/Tilemap.hpp>
#include <drift/gameplay/Pathfinding.hpp>
#include <drift/gameplay/Steering.hpp>
#include <drift/math/Noise.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/plugins/RandomPlugin.hpp>
#include <drift/procgen/Generators.hpp>
#include <drift/procgen/TilemapFill.hpp>
#include <drift/procgen/WeightedTable.hpp>
#include <drift/procgen/Wfc.hpp>
#include <drift/resources/FontResource.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/PhysicsResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/TilemapResource.hpp>
#include <drift/resources/Time.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace drift;

namespace {

constexpr int32_t MapW = 72, MapH = 48;
constexpr float Cell = 16.f;
constexpr float PlayerSpeed = 130.f, SlimeSpeed = 55.f;

// Tile ids in the generated tileset, left to right.
constexpr uint32_t TileFloorA = 1, TileFloorB = 2, TileWall = 3, TileDoor = 4;

enum class Mode { Cave, Dungeon, Tunnels, Ruins };
const char* modeNames[] = {"Cave", "Dungeon", "Tunnels", "Ruins"};

enum class Spawn { Coin, Gem, Slime };

struct Player {};
struct Slime {};
struct Pickup { int32_t value = 1; };
struct LevelThing {};   // despawned with the level

struct Game : public Resource {
    DRIFT_RESOURCE(Game)
    TextureHandle tileset;
    TilemapHandle map;
    EntityId mapEntity = InvalidEntityId;
    EntityId player = InvalidEntityId;
    EntityId camera = InvalidEntityId;
    Mode mode = Mode::Cave;
    uint64_t seed = 1;
    bool regenerate = true;
    Grid grid;
    NavGrid nav;
    FlowField field;
    GridPoint fieldGoal = {-1, -1};
    Vec2 start;
    int32_t score = 0;
    bool tour = false;
    float tourTime = 0.f;
};

// A 64x16 tileset: two floor variants, a wall and a door, drawn from noise.
TextureHandle makeTileset(RendererResource& r) {
    const int w = 64, h = 16;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 255);
    Noise grain(7);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int tile = x / 16, lx = x % 16, ly = y;
            float n = grain.fbm(NoiseType::Value, x * 0.5f, y * 0.5f, 3) * 0.5f + 0.5f;
            uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
            float rr = 0, gg = 0, bb = 0;
            if (tile == 0 || tile == 1) {             // floor
                float base = tile == 0 ? 52.f : 60.f;
                rr = base + 18.f * n;
                gg = base * 0.85f + 14.f * n;
                bb = base * 0.75f + 10.f * n;
                if (tile == 1 && (lx == 4 || ly == 11) ) { rr -= 10; gg -= 10; bb -= 10; }
            } else if (tile == 2) {                   // wall: bricks with a lit top edge
                bool mortar = ly % 5 == 4 || (lx + (ly / 5) * 4) % 8 == 0;
                rr = mortar ? 70.f : 120.f + 30.f * n;
                gg = mortar ? 66.f : 96.f + 22.f * n;
                bb = mortar ? 80.f : 104.f + 20.f * n;
                if (ly < 2) { rr += 40; gg += 40; bb += 40; }
            } else {                                  // door: planks
                bool seam = lx % 4 == 0;
                rr = seam ? 70.f : 140.f + 20.f * n;
                gg = seam ? 44.f : 92.f + 14.f * n;
                bb = seam ? 24.f : 48.f + 10.f * n;
            }
            p[0] = static_cast<uint8_t>(std::fmin(rr, 255.f));
            p[1] = static_cast<uint8_t>(std::fmin(gg, 255.f));
            p[2] = static_cast<uint8_t>(std::fmin(bb, 255.f));
        }
    return r.createTextureFromPixels(px.data(), w, h);
}

// The sample the Ruins learn from: rooms, walls and gaps, drawn by hand.
Grid ruinsSample() {
    const char* rows[] = {
        "##########",
        "#....#...#",
        "#....#...#",
        "#.........",
        "#....#...#",
        "###.###.##",
        "#........#",
        "#..####..#",
        "#..#  #..#",
        "#..####..#",
    };
    Grid g(10, 10, GridFloor);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) g.set(x, y, rows[y][x] == '.' ? GridFloor : GridWall);
    return g;
}

Grid generate(Mode mode, Rng& rng) {
    switch (mode) {
        case Mode::Cave:
            return generateCave(rng, MapW, MapH);
        case Mode::Dungeon: {
            DungeonOptions o;
            o.minRoom = 5;
            o.maxRoom = 12;
            o.extraCorridors = 4;
            return generateDungeon(rng, MapW, MapH, o).grid;
        }
        case Mode::Tunnels: {
            WalkOptions o;
            o.coverage = 0.35f;
            o.walkers = 4;
            o.brush = 2;
            return randomWalk(rng, MapW, MapH, o);
        }
        case Mode::Ruins: {
            static const WfcRules rules = WfcRules::fromSample(ruinsSample(), true);
            WfcOptions o;
            o.preset = Grid(MapW, MapH, -1);
            for (int32_t x = 0; x < MapW; ++x) { o.preset.set(x, 0, GridWall); o.preset.set(x, MapH - 1, GridWall); }
            for (int32_t y = 0; y < MapH; ++y) { o.preset.set(0, y, GridWall); o.preset.set(MapW - 1, y, GridWall); }
            Grid g = generateWfc(rng, rules, MapW, MapH, o);
            if (g.width == 0) return generateCave(rng, MapW, MapH);   // every attempt contradicted
            keepLargestRegion(g, GridFloor, GridWall);
            return g;
        }
    }
    return {};
}

Vec2 cellCenter(Vec2 cell) { return {cell.x * Cell, cell.y * Cell}; }

// Box2D owns a moving body's position; move the body and the transform.
void teleport(PhysicsResource& physics, const PhysicsBridge& bridge, Commands& cmd, EntityId e, Vec2 to) {
    auto it = bridge.entityToBody.find(e);
    if (it != bridge.entityToBody.end()) {
        physics.setBodyPosition(it->second, to);
        physics.setBodyVelocity(it->second, {});
    }
    cmd.insert(e, Transform2D{.position = to});
}

void setup(ResMut<RendererResource> r, ResMut<Game> game, Res<Random> random, Commands& cmd) {
    r->setClearColor({0.05f, 0.05f, 0.08f, 1.f});
    game->tileset = makeTileset(*r);
    game->seed = random->seed();
    game->player = cmd.spawn().insert(
        Transform2D{}, Shape2D{.kind = ShapeKind::Circle, .radius = 6.f, .color = {255, 220, 90, 255}, .zOrder = 5.f},
        RigidBody2D{.type = BodyType::Dynamic, .fixedRotation = true, .gravityScale = 0.f},
        CircleCollider2D{.radius = 5.5f, .friction = 0.f}, Velocity2D{}, Player{});
    game->camera = cmd.spawn().insert(Transform2D{}, Camera{.zoom = 2.f, .active = true},
                                      CameraFollow{.target = game->player, .smoothing = 8.f}, CameraShake{});
}

void controls(Res<InputResource> input, Res<Time> time, ResMut<Game> game, ResMut<Random> random) {
    const Key modeKeys[] = {Key::Num1, Key::Num2, Key::Num3, Key::Num4};
    for (int i = 0; i < 4; ++i) {
        if (input->keyPressed(modeKeys[i])) {
            game->mode = static_cast<Mode>(i);
            game->regenerate = true;
        }
    }
    if (input->keyPressed(Key::R)) {
        game->seed = random->rng.nextU32() & 0x7fffffffu;
        game->regenerate = true;
    }
    if (game->tour) {
        game->tourTime += time->delta;
        if (game->tourTime > 4.f) {
            game->tourTime = 0.f;
            game->mode = static_cast<Mode>((static_cast<int>(game->mode) + 1) % 4);
            game->regenerate = true;
        }
    }
}

void buildLevel(ResMut<Game> game, ResMut<TilemapResource> maps, ResMut<PhysicsResource> physics,
                Res<PhysicsBridge> bridge, Query<LevelThing> things, Commands& cmd) {
    if (!game->regenerate) return;
    game->regenerate = false;
    things.iterWithEntity([&](EntityId e, const LevelThing&) { cmd.despawn(e); });
    if (game->mapEntity != InvalidEntityId) cmd.despawn(game->mapEntity);
    if (game->map.valid()) maps->destroyTilemap(game->map);

    // One generator per mode, so switching modes keeps each level.
    Rng rng(game->seed, Rng::hash(modeNames[static_cast<int>(game->mode)]));
    game->grid = generate(game->mode, rng);
    const Grid& g = game->grid;

    TilemapDesc desc;
    desc.width = g.width;
    desc.height = g.height;
    desc.tileWidth = desc.tileHeight = static_cast<int32_t>(Cell);
    desc.tileset = game->tileset;
    desc.layers = {"Floor", "Walls"};
    game->map = maps->createTilemap(desc);

    // Floor: two variants in noise-shaped patches; doors on the floor layer.
    Noise patches(static_cast<uint32_t>(game->seed));
    Grid floor(g.width, g.height, 0);
    for (int32_t y = 0; y < g.height; ++y)
        for (int32_t x = 0; x < g.width; ++x) {
            int32_t v = g.get(x, y);
            if (v == GridDoor) floor.set(x, y, 3);
            else if (v == GridFloor) floor.set(x, y, patches.fbm(NoiseType::Simplex, x * 0.15f, y * 0.15f, 2) > 0.1f ? 2 : 1);
        }
    const uint32_t floorTiles[] = {0, TileFloorA, TileFloorB, TileDoor};
    fillTilemapLayer(*maps, game->map, 0, floor, floorTiles);
    const uint32_t wallTiles[] = {0, TileWall, 0};   // GridFloor, GridWall, GridDoor
    fillTilemapLayer(*maps, game->map, 1, g, wallTiles);
    maps->setLayerCollision(game->map, 1, true, {static_cast<int32_t>(TileWall)});
    game->mapEntity = cmd.spawn().insert(Transform2D{}, Tilemap{.map = game->map});

    game->nav = navGridFrom(g, {GridWall}, Cell);
    game->fieldGoal = {-1, -1};

    // Points spread over the floor; the first one is the start.
    std::vector<Vec2> spots = poissonDiskOnGrid(rng, g, GridFloor, 4.f);
    if (spots.empty()) return;
    game->start = cellCenter(spots[0]);
    teleport(*physics, *bridge, cmd, game->player, game->start);

    WeightedTable<Spawn> table;
    table.add(Spawn::Coin, 70.f).add(Spawn::Gem, 8.f).add(Spawn::Slime, 14.f);
    for (size_t i = 1; i < spots.size(); ++i) {
        Vec2 at = cellCenter(spots[i]);
        Spawn what = *table.roll(rng);
        if (what == Spawn::Slime && (at - game->start).length() < 12.f * Cell) what = Spawn::Coin;
        switch (what) {
            case Spawn::Coin:
                cmd.spawn().insert(Transform2D{.position = at},
                                   Shape2D{.kind = ShapeKind::Circle, .radius = 3.f, .color = {255, 200, 40, 255}, .zOrder = 3.f},
                                   Pickup{1}, LevelThing{});
                break;
            case Spawn::Gem:
                cmd.spawn().insert(Transform2D{.position = at, .rotation = 0.785f},
                                   Shape2D{.size = {7.f, 7.f}, .color = {90, 220, 255, 255}, .zOrder = 3.f},
                                   Pickup{5}, LevelThing{});
                break;
            case Spawn::Slime:
                cmd.spawn().insert(Transform2D{.position = at},
                                   Shape2D{.kind = ShapeKind::Circle, .radius = 5.f, .color = {110, 220, 90, 255}, .zOrder = 4.f},
                                   RigidBody2D{.type = BodyType::Dynamic, .fixedRotation = true, .gravityScale = 0.f},
                                   CircleCollider2D{.radius = 4.5f, .friction = 0.f}, Velocity2D{}, Slime{}, LevelThing{});
                break;
        }
    }
}

void movePlayer(Res<InputResource> input, QueryMut<Velocity2D, Player> players) {
    Vec2 dir{};
    if (input->keyHeld(Key::Left) || input->keyHeld(Key::A)) dir.x -= 1.f;
    if (input->keyHeld(Key::Right) || input->keyHeld(Key::D)) dir.x += 1.f;
    if (input->keyHeld(Key::Up) || input->keyHeld(Key::W)) dir.y -= 1.f;
    if (input->keyHeld(Key::Down) || input->keyHeld(Key::S)) dir.y += 1.f;
    players.iter([&](Velocity2D& v, Player&) { v.linear = dir.normalized() * PlayerSpeed; });
}

void chase(ResMut<Game> game, Query<Transform2D, Player> players, QueryMut<Transform2D, Velocity2D, Slime> slimes,
           QueryMut<CameraShake> shakes, ResMut<PhysicsResource> physics, Res<PhysicsBridge> bridge,
           Commands& cmd) {
    Vec2 playerPos{};
    players.iter([&](const Transform2D& t, const Player&) { playerPos = t.position; });
    // One flow field for every slime, rebuilt when the player changes cell.
    GridPoint goal = game->nav.worldToCell(playerPos);
    if (goal != game->fieldGoal && game->nav.walkable(goal)) {
        game->field.build(game->nav, {goal});
        game->fieldGoal = goal;
    }

    std::vector<Vec2> positions;
    slimes.iter([&](Transform2D& t, Velocity2D&, Slime&) { positions.push_back(t.position); });
    bool caught = false;
    slimes.iter([&](Transform2D& t, Velocity2D& v, Slime&) {
        Vec2 toPlayer = playerPos - t.position;
        if (toPlayer.length() < 11.f) caught = true;
        if (toPlayer.length() > 10.f * Cell) {   // asleep until the player comes near
            v.linear = {};
            return;
        }
        Vec2 flow = game->field.direction(t.position);
        if (game->nav.worldToCell(t.position) == goal) flow = toPlayer.normalized();
        Vec2 force = flow * SlimeSpeed - v.linear + steer::separation(t.position, positions, 14.f, SlimeSpeed) * 0.8f;
        v.linear = steer::truncate(v.linear + force * 0.2f, SlimeSpeed);
    });
    if (caught) {
        game->score /= 2;
        teleport(*physics, *bridge, cmd, game->player, game->start);
        shakes.iter([](CameraShake& s) { s.trigger(6.f); });
    }
}

void collect(ResMut<Game> game, Query<Transform2D, Player> players, Query<Transform2D, Pickup> pickups, Commands& cmd) {
    players.iter([&](const Transform2D& p, const Player&) {
        pickups.iterWithEntity([&](EntityId e, const Transform2D& t, const Pickup& pick) {
            if ((t.position - p.position).length() < 9.f) {
                game->score += pick.value;
                cmd.despawn(e);
            }
        });
    });
}

void drawHud(ResMut<RendererResource> r, ResMut<FontResource> fonts, Res<Game> game, Res<InputResource> input) {
    r->setLayer(ScreenLayer);
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "%s   seed %llu   score %d\n1 cave  2 dungeon  3 tunnels  4 ruins   R new seed   WASD move",
                  modeNames[static_cast<int>(game->mode)], static_cast<unsigned long long>(game->seed), game->score);
    fonts->drawText(fonts->defaultFont(), buf, {16.f, 12.f}, {.size = 18.f, .color = Color(235, 235, 220)});
    r->setLayer(0);
    if (input->keyPressed(Key::F12)) r->captureScreenshot("procgen.png");
}

} // namespace

int main(int argc, char** argv) {
    App app;
    app.args(argc, argv);   // window, project and [random] seed: drift.toml
    app.addPlugins<DefaultPlugins>();
    app.initResource<Game>();
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "tour") == 0) app.getResource<Game>()->tour = true;
    app.startup<setup>("setup");
    app.update<controls>("controls");
    app.update<buildLevel>("build_level").after("controls");
    app.update<movePlayer>("move_player");
    app.update<chase>("chase").after("build_level");
    app.update<collect>("collect");
    app.render<drawHud>("draw_hud");
    return app.run();
}
