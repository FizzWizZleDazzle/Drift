// =============================================================================
// Drift Engine - Gameplay Toolkit Example
// =============================================================================
// A small top-down grid game built from the toolkit units:
//   - InputActions: named actions and a 2D axis for keyboard, mouse, gamepad;
//     press R, then any key or button, to rebind "go" (saved to disk).
//   - A* pathfinding on a NavGrid generated from simplex noise, with mud
//     cells that cost more to cross.
//   - Tweens: the agent walks the path as a tween sequence; walls and coins
//     pop in with Ease::OutBack; coins bob with a yoyo loop.
//   - Timers: a repeating Timer component spawns a coin on each
//     TimerFinished event.
//   - SpatialIndex: the agent picks up coins found with a radius query.
//   - YSort: walls, coins and the agent overlap correctly by depth.
//   - SaveData: best score and bindings persist between runs.
// All textures are generated in code.
// =============================================================================

#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/Query.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/plugins/SavePlugin.hpp>
#include <drift/resources/FontResource.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>
#include <drift/gameplay/Pathfinding.hpp>
#include <drift/math/Noise.hpp>
#include <drift/math/Random.hpp>
#include <drift/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace drift;

static constexpr int kCols = 24;
static constexpr int kRows = 14;
static constexpr float kCell = 44.f;

struct Agent {};
struct Coin {};

struct Demo : public Resource {
    NavGrid grid{kCols, kRows, kCell};
    std::unordered_map<int, EntityId> walls;   // cell index -> wall entity
    std::unordered_map<int, EntityId> coins;   // cell index -> coin entity
    std::unordered_set<EntityId> dying;        // coins playing their pickup tween
    std::vector<GridPoint> path;
    GridPoint cursor{kCols / 2, kRows / 2};
    EntityId agent = InvalidEntityId;
    Cooldown cursorRepeat{0.12f};
    Vec2 lastMouse{};
    bool rebinding = false;
    int score = 0;
    int best = 0;
    Rng rng{2024};
    TextureHandle block, ball, coin;
    FontHandle font;
};

static int cellIndex(GridPoint p) { return p.y * kCols + p.x; }

// ---- Procedural textures ----

static TextureHandle makeBlock(RendererResource& r) {
    const int w = 40, h = 56;
    std::vector<uint8_t> px(w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            bool top = y < 16;
            bool edge = x == 0 || x == w - 1 || y == 0 || y == h - 1 || y == 16;
            uint8_t v = edge ? 40 : (top ? 190 : 120);
            uint8_t* p = &px[(y * w + x) * 4];
            p[0] = v; p[1] = static_cast<uint8_t>(v * 0.85f); p[2] = static_cast<uint8_t>(v * 0.7f); p[3] = 255;
        }
    return r.createTextureFromPixels(px.data(), w, h);
}

static TextureHandle makeDisc(RendererResource& r, int n, Color inner, Color outer) {
    std::vector<uint8_t> px(n * n * 4);
    float c = (n - 1) * 0.5f;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float d = distance({float(x), float(y)}, {c, c}) / c;
            Color col = lerp(inner, outer, clamp01(d));
            uint8_t* p = &px[(y * n + x) * 4];
            p[0] = col.r; p[1] = col.g; p[2] = col.b;
            p[3] = d <= 1.f ? 255 : 0;
        }
    return r.createTextureFromPixels(px.data(), n, n, {TextureFilter::Linear});
}

// ---- Helpers ----

static Vec2 cellCenter(const Demo& d, GridPoint p) { return d.grid.cellToWorld(p); }

static void spawnWall(Demo& d, Tweens& tweens, Commands& cmd, GridPoint p, float delay) {
    Sprite s{.texture = d.block, .origin = {20.f, 36.f}};
    EntityId e = cmd.spawn().insert(Transform2D{cellCenter(d, p), 0.f, {0.f, 0.f}}, s,
                                    YSort{.baseZ = 1.f}).id();
    d.walls[cellIndex(p)] = e;
    d.grid.setBlocked(p);
    tweens.on(e).delay(delay).scaleTo({1.f, 1.f}, 0.35f, Ease::OutBack).start();
}

// ---- Systems ----

static void setup(ResMut<RendererResource> r, ResMut<FontResource> fonts, ResMut<Demo> d,
                  ResMut<InputActions> actions, ResMut<SaveData> save, ResMut<Tweens> tweens,
                  Commands& cmd) {
    r->setClearColor({0.08f, 0.09f, 0.11f, 1.f});
    d->block = makeBlock(*r);
    d->ball = makeDisc(*r, 28, Color(150, 230, 255), Color(30, 110, 220));
    d->coin = makeDisc(*r, 20, Color(255, 250, 190), Color(230, 160, 20));
    d->font = fonts->loadFont("../render_showcase/assets/fonts/DejaVuSansMono.ttf", 18.f);

    // Default bindings; a saved set from an earlier run replaces them.
    actions->bindAxis2D("move", InputBinding::key(Key::A), InputBinding::key(Key::D),
                        InputBinding::key(Key::W), InputBinding::key(Key::S));
    actions->bindAxis2D("move", InputBinding::key(Key::Left), InputBinding::key(Key::Right),
                        InputBinding::key(Key::Up), InputBinding::key(Key::Down));
    actions->bindStick("move", GamepadAxis::LeftX, GamepadAxis::LeftY);
    actions->bindAction("go", InputBinding::key(Key::Space));
    actions->bindAction("go", InputBinding::mouse(MouseButton::Left));
    actions->bindAction("go", InputBinding::button(GamepadButton::South));
    actions->bindAction("wall", InputBinding::key(Key::E));
    actions->bindAction("wall", InputBinding::mouse(MouseButton::Right));
    actions->bindAction("wall", InputBinding::button(GamepadButton::West));
    actions->bindAction("rebind", InputBinding::key(Key::R));
    actions->bindAction("rebind", InputBinding::button(GamepadButton::North));
    if (save->has("bindings")) actions->fromJson(save->json()["bindings"]);
    d->best = save->get("best", 0);

    // Terrain from noise: high values are walls, a middle band is mud.
    Noise noise(7);
    GridPoint start{2, kRows / 2};
    for (int y = 0; y < kRows; ++y)
        for (int x = 0; x < kCols; ++x) {
            GridPoint p{x, y};
            float n = noise.fbm(NoiseType::Simplex, x * 0.18f, y * 0.18f, 3);
            if (distance(Vec2(float(x), float(y)), Vec2(float(start.x), float(start.y))) < 2.5f) continue;
            if (n > 0.28f) spawnWall(*d, *tweens, cmd, p, 0.02f * (x + y));
            else if (n > 0.08f) d->grid.setCost(p, 4.f);
        }

    Vec2 mid{kCols * kCell * 0.5f, kRows * kCell * 0.5f};
    cmd.spawn().insert(Transform2D{mid + Vec2{0.f, -20.f}}, Camera{.zoom = 1.f, .active = true});

    d->agent = cmd.spawn().insert(Transform2D{cellCenter(*d, start)},
                                  Sprite{.texture = d->ball, .origin = {14.f, 22.f}},
                                  YSort{.baseZ = 1.f}, Agent{}).id();

    // Coin spawner: a repeating timer; each TimerFinished spawns a coin.
    cmd.spawn().insert(Timer::repeating(1.2f, /*tag=*/1));
}

static void controls(Res<InputActions> actions, Res<InputResource> input, Res<Time> time,
                     Res<RendererResource> r, ResMut<Demo> d, ResMut<Tweens> tweens,
                     Query<Transform2D, With<Agent>> agents, Commands& cmd) {
    if (d->rebinding) return;

    // Cursor: the 2D "move" axis steps it with key repeat; the mouse places it.
    d->cursorRepeat.tick(time->delta);
    Vec2 move = actions->axis2D("move");
    if (move.length() > 0.5f && d->cursorRepeat.tryUse()) {
        if (std::fabs(move.x) > 0.38f) d->cursor.x += move.x > 0 ? 1 : -1;
        if (std::fabs(move.y) > 0.38f) d->cursor.y += move.y > 0 ? 1 : -1;
    }
    if (!actions->held("move")) d->cursorRepeat.reset();
    Vec2 mouse = input->mousePosition();
    if (!approx(mouse, d->lastMouse)) {
        d->lastMouse = mouse;
        GridPoint m = d->grid.worldToCell(r->screenToWorld(mouse));
        if (d->grid.inBounds(m)) d->cursor = m;
    }
    d->cursor.x = std::clamp(d->cursor.x, 0, kCols - 1);
    d->cursor.y = std::clamp(d->cursor.y, 0, kRows - 1);

    Vec2 agentPos{};
    agents.iter([&](const Transform2D& t) { agentPos = t.position; });
    GridPoint here = d->grid.worldToCell(agentPos);

    if (actions->pressed("go")) {
        auto path = findPath(d->grid, here, d->cursor);
        if (!path.empty()) {
            d->path = path;
            tweens->stopAll(d->agent);
            auto walk = tweens->on(d->agent);
            for (size_t i = 1; i < path.size(); ++i) {
                bool diag = path[i].x != path[i - 1].x && path[i].y != path[i - 1].y;
                float t = 0.14f * d->grid.cost(path[i]) * (diag ? 1.414f : 1.f);
                walk.moveTo(cellCenter(*d, path[i]), t, Ease::Linear);
            }
            walk.call([dd = d.get()] { dd->path.clear(); });
            walk.start();
        }
    }

    if (actions->pressed("wall") && d->cursor != here) {
        int idx = cellIndex(d->cursor);
        auto it = d->walls.find(idx);
        if (it != d->walls.end()) {
            cmd.entity(it->second).despawn();
            d->walls.erase(it);
            d->grid.setBlocked(d->cursor, false);
        } else if (!d->coins.count(idx)) {
            spawnWall(*d, *tweens, cmd, d->cursor, 0.f);
        }
    }

    if (actions->pressed("rebind")) d->rebinding = true;
}

// Rebinding: the first input after R becomes the new primary "go" binding.
static void rebind(ResMut<InputActions> actions, Res<InputResource> input, ResMut<Demo> d,
                   ResMut<SaveData> save) {
    if (!d->rebinding || actions->pressed("rebind")) return;
    auto b = actions->captureNext(*input);
    if (!b) return;
    actions->rebind("go", 0, 0, *b);
    save->set("bindings", actions->toJson());
    save->flush();
    d->rebinding = false;
}

static void spawnCoins(EventReader<TimerFinished> timers, ResMut<Demo> d, ResMut<Tweens> tweens,
                       Commands& cmd) {
    for (const auto& ev : timers) {
        if (ev.tag != 1 || d->coins.size() >= 8) continue;
        for (int attempt = 0; attempt < 20; ++attempt) {
            GridPoint p{d->rng.range(0, kCols - 1), d->rng.range(0, kRows - 1)};
            int idx = cellIndex(p);
            if (!d->grid.walkable(p) || d->coins.count(idx)) continue;
            Vec2 pos = cellCenter(*d, p);
            EntityId e = cmd.spawn().insert(Transform2D{pos, 0.f, {0.f, 0.f}},
                                            Sprite{.texture = d->coin, .origin = {10.f, 14.f}},
                                            YSort{.baseZ = 1.f}, SpatialBounds{{10.f, 10.f}}, Coin{}).id();
            d->coins[idx] = e;
            tweens->on(e).scaleTo({1.f, 1.f}, 0.4f, Ease::OutBack).start();
            tweens->on(e).moveBy({0.f, -6.f}, 0.5f, Ease::InOutSine).yoyo().start();
            break;
        }
    }
}

static void collect(Res<SpatialIndex> index, Query<Transform2D, With<Agent>> agents, ResMut<Demo> d,
                    ResMut<Tweens> tweens, ResMut<SaveData> save) {
    Vec2 agentPos{};
    agents.iter([&](const Transform2D& t) { agentPos = t.position; });
    for (EntityId e : index->queryRadius(agentPos, 14.f)) {
        if (d->dying.count(e)) continue;
        for (auto it = d->coins.begin(); it != d->coins.end(); ++it) {
            if (it->second != e) continue;
            d->coins.erase(it);
            d->dying.insert(e);
            tweens->stopAll(e);
            tweens->on(e).scaleTo({1.8f, 1.8f}, 0.15f, Ease::OutQuad).fadeTo(0.f, 0.2f).start();
            if (++d->score > d->best) {
                d->best = d->score;
                save->set("best", d->best);
                save->flush();
            }
            break;
        }
    }
}

// Pickup tweens end with a TweenFinished event; despawn the coin then.
static void reap(EventReader<TweenFinished> done, ResMut<Demo> d, Commands& cmd) {
    for (const auto& ev : done) {
        if (d->dying.erase(ev.entity)) cmd.entity(ev.entity).despawn();
    }
}

static void drawGrid(ResMut<RendererResource> r, Res<Demo> d, Res<Time> time) {
    for (int y = 0; y < kRows; ++y)
        for (int x = 0; x < kCols; ++x) {
            Rect cell{x * kCell, y * kCell, kCell - 1.f, kCell - 1.f};
            float c = d->grid.cost({x, y});
            Color col = c > 1.f ? Color(92, 70, 48) : (((x + y) & 1) ? Color(46, 52, 60) : Color(52, 58, 67));
            r->drawRectFilled(cell, col, -2.f);
        }
    if (d->path.size() > 1) {
        std::vector<Vec2> pts;
        for (auto p : d->path) pts.push_back(cellCenter(*d, p));
        r->drawPolyline(pts.data(), static_cast<int32_t>(pts.size()), Color(120, 220, 255, 150), 4.f, false, -1.f);
    }
    float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(time->elapsed) * 6.f);
    Color cur = d->rebinding ? Color(255, 120, 120) : Color(255, 255, 255, static_cast<uint8_t>(140 + 100 * pulse));
    r->drawRect({d->cursor.x * kCell, d->cursor.y * kCell, kCell - 1.f, kCell - 1.f}, cur, 3.f, 5.f);
}

static void drawHud(ResMut<RendererResource> r, ResMut<FontResource> fonts, Res<Demo> d,
                    Res<InputActions> actions) {
    if (!d->font.valid()) return;
    r->setLayer(ScreenLayer);
    char buf[256];
    std::string go;
    for (const auto& b : actions->bindings("go")) go += (go.empty() ? "" : ", ") + b.toString();
    if (d->rebinding) {
        std::snprintf(buf, sizeof(buf), "Press any key or button to bind \"go\"...");
    } else {
        std::snprintf(buf, sizeof(buf),
                      "coins: %d   best: %d\nmove cursor: WASD / arrows / stick / mouse\n"
                      "go: %s\nwall: E / right click   rebind go: R",
                      d->score, d->best, go.c_str());
    }
    fonts->drawText(d->font, buf, {16.f, 12.f}, Color(235, 235, 220));
    r->setLayer(0);
}

int main() {
    App app;
    app.setConfig({.title = "Drift - gameplay toolkit", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.addPlugin(new SavePlugin("Drift", "toolkit-example"));
    app.addResource<Demo>();
    app.startup<setup>();
    app.update<controls>("controls");
    app.update<rebind>("rebind");
    app.update<spawnCoins>("spawn_coins");
    app.update<collect>("collect");
    app.update<reap>("reap");
    app.render<drawGrid>("draw_grid");
    app.render<drawHud>("draw_hud");
    return app.run();
}
