// =============================================================================
// Tilemap: a Tiled map and two LDtk levels side by side
// =============================================================================
// The meadow (assets/meadow.tmj, Tiled, infinite) has a parallax sky image
// layer, a tinted parallax hill layer inside a group, animated water and
// lava, an image-collection tileset (trees, crates), flipped and rotated
// tiles, and platforms autotiled at startup from a hidden mask layer. The
// caves (assets/caves.ldtk, LDtk) continue to the right: IntGrid collision
// with auto-layer tiles, a Tiles layer, and entities with fields.
//
// Object spawners turn PlayerSpawn and Coin objects from both formats into
// entities. The player collides with colliders merged from the collision
// layers.
// =============================================================================

#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/Query.hpp>
#include <drift/Log.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/CameraController.hpp>
#include <drift/components/Physics.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Tilemap.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/PhysicsResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/TilemapResource.hpp>
#include <drift/resources/Time.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace drift;

namespace {

constexpr float PlayerW = 12.f, PlayerH = 14.f;
constexpr float Gravity = 700.f, RunSpeed = 95.f, JumpSpeed = -270.f;
constexpr float PlayerZ = 6.5f;   // between the meadow's Platforms (6) and FrontDecor (7) layers

struct Player {};
struct Coin { int32_t value = 1; };

struct Game : public Resource {
    DRIFT_RESOURCE(Game)
    TilemapHandle meadow;
    std::vector<TilemapHandle> caves;
    int32_t mask = -1, platforms = -1;
    AutotileRules platformRules;
    TextureHandle tiles, player;
    int32_t score = 0;
    bool autopilot = true;
    float jumpTimer = 0.f;
    float stuckTime = 0.f;
    float direction = 1.f;
    bool showColliders = false;
};

TextureHandle makePlayerTexture(RendererResource& r) {
    const int w = static_cast<int>(PlayerW), h = static_cast<int>(PlayerH);
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
            bool eye = y == 4 && (x == 4 || x == 8);
            bool body = x > 0 && x < w - 1;
            if (!body) continue;
            p[0] = eye ? 20 : (y < 8 ? 240 : 60);
            p[1] = eye ? 20 : (y < 8 ? 190 : 90);
            p[2] = eye ? 20 : (y < 8 ? 150 : 200);
            p[3] = 255;
        }
    }
    return r.createTextureFromPixels(px.data(), w, h);
}

void setup(ResMut<TilemapResource> maps, ResMut<RendererResource> renderer, ResMut<Game> game, Commands& cmd) {
    renderer->setClearColor({0.12f, 0.14f, 0.22f, 1.f});
    game->player = makePlayerTexture(*renderer);
    game->tiles = renderer->loadTexture("assets/tiles.png");

    // The meadow: platforms are autotiled from the hidden PlatformMask layer
    // (tile ids 9..24 are the 16 neighbour variants).
    game->meadow = maps->loadTilemap("assets/meadow.tmj");
    game->mask = maps->findLayer(game->meadow, "PlatformMask");
    game->platforms = maps->findLayer(game->meadow, "Platforms");
    game->platformRules.mode = AutotileMode::Neighbours4;
    for (uint32_t m = 0; m < 16; ++m) game->platformRules.tiles.push_back(9 + m);
    maps->autotile(game->meadow, game->mask, game->platforms, game->platformRules);
    DRIFT_LOG_INFO("meadow '%s': %d layers, title '%s'", maps->mapName(game->meadow),
                   maps->layerCount(game->meadow),
                   maps->properties(game->meadow).getString("title").c_str());
    cmd.spawn().insert(Transform2D{}, Tilemap{.map = game->meadow});

    // The caves start at the meadow's right edge; each level keeps its
    // LDtk world position on top of the entity position. Their layers start
    // at zOrder 4 so the meadow's parallax background stays behind them.
    Rect meadow = maps->bounds(game->meadow);
    game->caves = maps->loadLevels("assets/caves.ldtk");
    for (TilemapHandle level : game->caves) {
        DRIFT_LOG_INFO("LDtk level '%s' at %.0f,%.0f ambient %s", maps->mapName(level),
                       maps->worldOffset(level).x, maps->worldOffset(level).y,
                       maps->properties(level).getString("ambient").c_str());
        cmd.spawn().insert(Transform2D{.position = {meadow.x + meadow.w, 0.f}}, Tilemap{.map = level, .zOrder = 4.f});
    }

    // Object spawners: the same Coin spawner serves Tiled objects and LDtk
    // entities.
    Game* g = game.get();
    maps->addObjectSpawner("PlayerSpawn", [g](const MapObject&, const ObjectSpawnContext& ctx) {
        Vec2 pos = {ctx.anchor.x - PlayerW * 0.5f, ctx.anchor.y - PlayerH};
        EntityId player = ctx.commands.spawn().insert(
            Transform2D{.position = pos},
            Sprite{.texture = g->player, .zOrder = PlayerZ},
            RigidBody2D{.type = BodyType::Dynamic, .fixedRotation = true, .gravityScale = 0.f},
            BoxCollider2D{.halfSize = {PlayerW * 0.5f - 1.f, PlayerH * 0.5f}, .offset = {PlayerW * 0.5f, PlayerH * 0.5f},
                          .friction = 0.f},
            Velocity2D{}, Player{});
        ctx.commands.spawn().insert(Transform2D{.position = pos}, Camera{.zoom = 3.f, .active = true},
                                    CameraFollow{.target = player, .smoothing = 6.f, .offset = {0.f, -20.f}});
    });
    maps->addObjectSpawner("Coin", [g](const MapObject& o, const ObjectSpawnContext& ctx) {
        ctx.commands.spawn().insert(Transform2D{.position = ctx.position},
                                    Sprite{.texture = g->tiles, .srcRect = {112.f, 48.f, 16.f, 16.f}, .zOrder = PlayerZ - 0.1f},
                                    Coin{o.properties.getInt("value", 1)});
    });
    maps->addObjectSpawner("Sign", [](const MapObject& o, const ObjectSpawnContext& ctx) {
        DRIFT_LOG_INFO("sign at %.0f,%.0f: %s", ctx.position.x, ctx.position.y,
                       o.properties.getString("message").c_str());
    });
}

void playerControl(Res<InputResource> input, Res<Time> time, Res<PhysicsResource> physics, ResMut<Game> game,
                   QueryMut<Transform2D, Velocity2D, Player> players) {
    float dt = time->delta;
    for (Key k : {Key::Left, Key::Right, Key::Up, Key::Space, Key::A, Key::D, Key::W}) {
        if (input->keyPressed(k)) game->autopilot = false;
    }
    if (input->keyPressed(Key::Tab)) game->autopilot = !game->autopilot;

    players.iter([&](Transform2D& t, Velocity2D& v, Player&) {
        // Grounded when a short ray from either foot hits something. The
        // rays start inside the player's own box, which they ignore.
        bool grounded = false;
        for (float fx : {2.f, PlayerW - 2.f}) {
            RaycastHit hit = physics->raycast({t.position.x + fx, t.position.y + PlayerH - 1.f}, {0.f, 1.f}, 3.f);
            grounded = grounded || hit.hit;
        }

        float dir = 0.f;
        bool jump = false;
        if (game->autopilot) {
            // Run back and forth across both maps, hopping now and then and
            // turning around at walls too high to jump.
            bool blocked = std::fabs(v.linear.x) < 5.f;
            game->stuckTime = blocked ? game->stuckTime + dt : 0.f;
            if (game->stuckTime > 0.8f || t.position.x < -180.f) {
                game->direction = t.position.x < -180.f ? 1.f : -game->direction;
                game->stuckTime = 0.f;
            }
            dir = game->direction;
            game->jumpTimer -= dt;
            if (grounded && (game->jumpTimer <= 0.f || (blocked && game->stuckTime > 0.1f))) {
                jump = true;
                game->jumpTimer = 0.9f + static_cast<float>(std::rand() % 100) / 100.f;
            }
        } else {
            if (input->keyHeld(Key::Left) || input->keyHeld(Key::A)) dir -= 1.f;
            if (input->keyHeld(Key::Right) || input->keyHeld(Key::D)) dir += 1.f;
            jump = grounded && (input->keyPressed(Key::Up) || input->keyPressed(Key::Space) || input->keyPressed(Key::W));
        }

        v.linear.x = dir * RunSpeed;
        v.linear.y = std::min(v.linear.y + Gravity * dt, 400.f);
        if (jump) v.linear.y = JumpSpeed;
    });
}

void collectCoins(Query<Transform2D, Player> players, Query<Transform2D, Coin> coins, ResMut<Game> game,
                  Commands& cmd) {
    players.iter([&](const Transform2D& p, const Player&) {
        Rect pr{p.position.x, p.position.y, PlayerW, PlayerH};
        coins.iterWithEntity([&](EntityId e, const Transform2D& c, const Coin& coin) {
            if (pr.overlaps({c.position.x + 3.f, c.position.y + 3.f, 10.f, 10.f})) {
                game->score += coin.value;
                cmd.despawn(e);
            }
        });
    });
}

// Left click toggles a platform cell: the mask changes, the platforms are
// re-autotiled around it, and the colliders rebuild on their own.
void editPlatforms(Res<InputResource> input, Res<RendererResource> renderer, ResMut<TilemapResource> maps,
                   ResMut<Game> game) {
    if (input->keyPressed(Key::F1)) game->showColliders = !game->showColliders;
    if (!input->mouseButtonPressed(MouseButton::Left)) return;
    Vec2 world = renderer->screenToWorld(input->mousePosition());
    TileCoord c = maps->worldToTile(game->meadow, world, game->mask);
    uint32_t cur = maps->getTile(game->meadow, game->mask, c.x, c.y);
    maps->setTile(game->meadow, game->mask, c.x, c.y, cur ? 0u : 1u);
    maps->autotile(game->meadow, game->mask, game->platforms, game->platformRules, c, c);
}

void drawOverlay(ResMut<RendererResource> r, Res<TilemapResource> maps, Res<Game> game, Res<InputResource> input) {
    // LDtk levels carry a background color; fill each level behind its layers.
    for (TilemapHandle level : game->caves) {
        Vec2 o = maps->origin(level);
        Rect b = maps->bounds(level);
        r->drawRectFilled({o.x + b.x, o.y + b.y, b.w, b.h}, maps->backgroundColor(level), 3.9f);
    }

    if (game->showColliders) {
        auto outline = [&](TilemapHandle map) {
            Vec2 o = maps->origin(map);
            for (const Rect& rc : maps->collisionRects(map)) {
                r->drawRect({o.x + rc.x, o.y + rc.y, rc.w, rc.h}, Color(255, 60, 60, 220), 0.5f, 50.f);
            }
        };
        outline(game->meadow);
        for (TilemapHandle level : game->caves) outline(level);
    }

    // Score: one coin icon per point.
    r->setLayer(ScreenLayer);
    for (int32_t i = 0; i < game->score; ++i) {
        r->drawSprite(game->tiles, {12.f + i * 20.f, 12.f}, {112.f, 48.f, 16.f, 16.f}, Vec2{1.25f, 1.25f});
    }
    r->setLayer(0);
    if (input->keyPressed(Key::F12)) r->captureScreenshot("tilemap.png");
}

} // namespace

int main(int argc, char** argv) {
    App app;
    app.setConfig({.title = "Drift - Tilemap", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.initResource<Game>();
    if (argc > 1) app.getResource<Game>()->showColliders = std::atoi(argv[1]) != 0;
    app.startup<setup>("setup");
    app.update<playerControl>("player_control");
    app.update<collectCoins>("collect_coins");
    app.update<editPlatforms>("edit_platforms");
    app.render<drawOverlay>("draw_overlay");
    return app.run();
}
