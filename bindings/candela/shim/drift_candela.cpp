// drift_candela: the C side of the candela `drift` package.
//
// candela's foreign interface carries int64, double, null-terminated
// strings, packed int/float arrays and structs of those, and nothing that
// could hold a pointer or call back into the script. The frame loop lives in
// the package's candela code (App.run()), which calls two functions here
// once per frame:
//
//   dc_sync_begin()  waits for the simulation started last frame;
//                    the script's component changes are then written into
//                    the world (dc_create, dc_set, dc_move_many, ...).
//   dc_sync_end()    renders and presents the frame, polls input, copies
//                    collision events and the component values the script
//                    reads (the snapshot), then starts the next frame's
//                    simulation on a background thread and returns, so the
//                    simulation runs while the script's systems do.
//
// While the simulation runs the script must not touch the world. Reads of
// engine-driven components come from the snapshot (dc_pull); calls that
// need the live world (physics queries, audio, scenes, ...) wait for the
// simulation first (ensureJoined). Resources that are mainThreadOnly() are
// never used by the background simulation, so textures, fonts, drawing and
// post effects need no wait.
//
// Entities cross as small int handles from a table here, never reused, so a
// stale handle never names a new entity. Engine handles (textures, sounds,
// fonts, ...) are already 32-bit and cross as they are. 0 is never a valid
// handle. Returned strings live in one static buffer until the next call.

#include <drift/App.hpp>
#include <drift/AssetServer.hpp>
#include <drift/CollisionBridge.hpp>
#include <drift/Commands.hpp>
#include <drift/PhysicsSyncSystem.hpp>
#include <drift/Reflect.hpp>
#include <drift/Scene.hpp>
#include <drift/components/AudioSource.hpp>
#include <drift/components/Behavior.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/CameraController.hpp>
#include <drift/components/GlobalTransform.hpp>
#include <drift/components/Hierarchy.hpp>
#include <drift/components/Light2D.hpp>
#include <drift/components/Name.hpp>
#include <drift/components/ParticleEmitter.hpp>
#include <drift/components/Physics.hpp>
#include <drift/components/Shape.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/SpriteAnimator.hpp>
#include <drift/components/Text.hpp>
#include <drift/components/Tilemap.hpp>
#include <drift/effects/PostEffects.hpp>
#include <drift/gameplay/Easing.hpp>
#include <drift/gameplay/InputActions.hpp>
#include <drift/gameplay/SaveData.hpp>
#include <drift/gameplay/Tween.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/plugins/LightingPlugin.hpp>
#include <drift/resources/AudioResource.hpp>
#include <drift/resources/FontResource.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/PhysicsResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/SpriteResource.hpp>
#include <drift/resources/TilemapResource.hpp>
#include <drift/resources/Time.hpp>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif

using namespace drift;

// ---- Structs that cross the boundary (doubles, int64_t, nested) ----------

// candela 0.1.6 passes its int as int64_t, in arguments, returns and
// struct fields alike.
using cint = int64_t;

struct DV2 { double x, y; };
struct DRect { double x, y, w, h; };
struct DColor { double r, g, b, a; };
struct DCell { cint x, y; };

struct DTextStyle {
    double size;
    DColor color;
    cint align, valign;
    double wrap, lineSpacing;
    double outline;
    DColor outlineColor;
    DV2 shadow;
    DColor shadowColor;
    cint rich;
};

struct DSprite {
    DRect src;               // w == 0: whole texture
    DV2 scale;
    double rotation;
    DV2 origin;
    DColor tint;
    cint flip;
    cint blend;
};

struct DPlay {
    double volume, pan, pitch;
    cint loop;
    cint bus;
    double fadeIn;
    cint priority;
};

struct DHit {
    cint hit;
    DV2 point, normal;
    double fraction;
};

// ---- Conversions ---------------------------------------------------------

namespace {

Vec2 v2(DV2 v) { return {static_cast<float>(v.x), static_cast<float>(v.y)}; }
DV2 dv(Vec2 v) { return {v.x, v.y}; }
Rect rect(DRect r) { return {float(r.x), float(r.y), float(r.w), float(r.h)}; }
DRect drect(Rect r) { return {r.x, r.y, r.w, r.h}; }

uint8_t channel(double c) {
    if (!(c > 0.0)) return 0;
    if (c >= 1.0) return 255;
    return static_cast<uint8_t>(std::lround(c * 255.0));
}
Color color(DColor c) { return {channel(c.r), channel(c.g), channel(c.b), channel(c.a)}; }
ColorF colorf(DColor c) { return {float(c.r), float(c.g), float(c.b), float(c.a)}; }
DColor dcolor(Color c) { return {c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0}; }

// Engine handles are 32-bit ids; a candela int holds them unchanged.
cint bits(uint32_t v) { return cint(v); }
uint32_t ubits(cint v) { return uint32_t(v); }

template <typename H> H handle(cint v) { return H{uint32_t(v)}; }

// ---- Component kinds ------------------------------------------------------
//
// Every engine component the candela side mirrors has a kind number and a
// fixed layout of doubles (see the codec functions below and the matching
// encode/decode functions in src/main.cdl).

enum Kind : int32_t {
    KTransform = 0, KVelocity, KSprite, KShape, KText, KCamera, KCameraFollow,
    KRigidBody, KBoxCollider, KCircleCollider, KEmitter, KPointLight, KSpotLight,
    KOccluder, KAudioSource, KTilemap, KAnimator, KLifetime, KOscillate, KFollow,
    KName, KTween, KindCount
};

// Doubles per entity that dc_pull returns for the kinds the engine changes.
int32_t pullStride(int32_t kind) {
    switch (kind) {
        case KTransform: return 5;   // x, y, rotation, scale x, scale y
        case KVelocity: return 3;    // x, y, angular
        case KSprite: return 8;      // frame x, y, w, h, color r, g, b, a
        case KCamera: return 1;      // zoom
        case KAnimator: return 4;    // clip, frame, finished, playing
        case KLifetime: return 1;    // seconds left
    }
    return 0;
}

// ---- State ---------------------------------------------------------------

struct Burst {
    EntityId entity;
    double despawnAt;
};

struct Snapshot {
    std::unordered_map<int32_t, std::array<double, 8>> rows;
};

struct State {
    App* app = nullptr;
    bool pipelined = true;
    bool simInFlight = false;
    std::string error;
    std::string text;               // returned strings live here until the next call

    // Entity handles: never reused, so a stale handle never names a new entity.
    std::unordered_map<int32_t, EntityId> ids;
    std::unordered_map<EntityId, int32_t> handles;
    int32_t nextHandle = 1;
    std::vector<int32_t> dead;      // handles whose entity went away engine-side

    // Values of engine-driven components as they were when the simulation
    // started, for the handles the script read last frame or wrote.
    Snapshot snaps[KindCount];
    std::unordered_set<int32_t> interest[KindCount];
    std::unordered_set<int32_t> nextInterest[KindCount];

    // Collision and sensor events of the last simulation, as handle pairs.
    std::vector<int32_t> events[4];

    // Frame values cached at dc_sync_end, so reading them never races the
    // simulation.
    double dt = 0.0, elapsed = 0.0;

    // Pipeline timing (performance-counter ticks) of the last frame.
    uint64_t returned = 0;          // dc_sync_end handed control to the script
    uint64_t scriptDone = 0;        // dc_sync_begin was called
    uint64_t joined = 0;            // the simulation had finished
    uint64_t syncStart = 0, syncEnd = 0;
    uint64_t renderTicks = 0;
    SimulationTiming sim;
    double stats[8] = {};

    float drawZ = 0.f;

    std::unordered_map<int32_t, TweenId> tweens;          // Tween component per handle
    std::unordered_set<int32_t> autoVelocity;            // Velocity2D added for a body

    std::vector<std::unique_ptr<SaveData>> saves;

    std::vector<SceneInstance> scenes;   // spawned instances; handle = index + 1
    std::vector<const MapObject*> objects;
    TilemapHandle objectsMap;

    std::vector<Burst> bursts;

    // Effects added through this binding, by kind, so a game finds its fade.
    EffectId fade;
};

State g;

template <typename T> T* res() { return g.app ? g.app->getResource<T>() : nullptr; }
World* world() { return g.app ? &g.app->world() : nullptr; }

double ms(uint64_t ticks) { return double(ticks) * 1000.0 / double(SDL_GetPerformanceFrequency()); }

// Waits for the background simulation, so the caller may touch the world.
void ensureJoined() {
    if (!g.app || !g.simInFlight) return;
    g.app->finishSimulation();
    g.simInFlight = false;
    g.joined = SDL_GetPerformanceCounter();
}

int32_t handleOf(EntityId id) {
    if (id == InvalidEntityId) return 0;
    auto it = g.handles.find(id);
    if (it != g.handles.end()) return it->second;
    int32_t h = g.nextHandle++;
    g.ids[h] = id;
    g.handles[id] = h;
    return h;
}

EntityId idOf(int32_t h) {
    World* w = world();
    if (!w) return InvalidEntityId;
    auto it = g.ids.find(h);
    if (it == g.ids.end()) return InvalidEntityId;
    if (!w->isAlive(it->second)) return InvalidEntityId;
    return it->second;
}

void forget(int32_t h) {
    auto it = g.ids.find(h);
    if (it == g.ids.end()) return;
    g.handles.erase(it->second);
    g.ids.erase(it);
}

template <typename T> ComponentId cid() {
    World& w = *world();
    auto& reg = w.componentRegistry();
    if (!reg.has<T>()) w.registerComponent<T>(typeid(T).name());
    return reg.get<T>();
}

template <typename T> T* get(EntityId e) {
    if (e == InvalidEntityId) return nullptr;
    World& w = *world();
    ComponentId c = cid<T>();
    if (!w.hasComponent(e, c)) return nullptr;
    return w.getMut<T>(e, c);
}

template <typename T> void put(EntityId e, const T& value) {
    world()->set<T>(e, cid<T>(), value);
}

template <typename T> void removeComponent(EntityId e) {
    if (e == InvalidEntityId) return;
    world()->removeComponent(e, cid<T>());
}

const char* keep(std::string s) {
    g.text = std::move(s);
    return g.text.c_str();
}

int32_t fail(const std::string& message) {
    g.error = message;
    return 0;
}

const PhysicsBody* bodyOf(EntityId e) {
    auto* bridge = res<PhysicsBridge>();
    if (!bridge) return nullptr;
    auto it = bridge->entityToBody.find(e);
    return it == bridge->entityToBody.end() ? nullptr : &it->second;
}

// Moves a body Box2D owns to follow a Transform2D the script set.
void teleportBody(EntityId e, Vec2 position, float rotation) {
    auto* physics = res<PhysicsResource>();
    const PhysicsBody* b = bodyOf(e);
    if (!b || !physics) return;
    Vec2 offset{};
    if (auto* box = get<BoxCollider2D>(e)) offset = box->offset;
    physics->setBodyPosition(*b, position + offset);
    physics->setBodyRotation(*b, rotation);
}

// Emitter layout (31 doubles): rate, lifetime min, max, speed min, max,
// angle min, max, size start, end, color start rgba, color end rgba,
// gravity x, y, drag, max particles, blend, texture, shape, shape radius,
// shape extents x, y, duration, looping, z, playing.
constexpr int32_t EmitterSize = 31;

EmitterConfig emitterConfig(const double* d) {
    EmitterConfig c;
    c.spawnRate = float(d[0]);
    c.lifetime = {float(d[1]), float(d[2])};
    c.speed = {float(d[3]), float(d[4])};
    c.angle = {float(d[5]), float(d[6])};
    c.sizeStart = {float(d[7]), float(d[7])};
    c.sizeEnd = {float(d[8]), float(d[8])};
    c.colorStart = {float(d[9]), float(d[10]), float(d[11]), float(d[12])};
    c.colorEnd = {float(d[13]), float(d[14]), float(d[15]), float(d[16])};
    c.gravity = {float(d[17]), float(d[18])};
    c.drag = float(d[19]);
    c.maxParticles = d[20] > 0 ? uint32_t(d[20]) : 256;
    c.blendMode = static_cast<BlendMode>(int(d[21]));
    c.texture = handle<TextureHandle>(cint(d[22]));
    if (!c.texture.valid()) {
        if (auto* r = res<RendererResource>()) c.texture = r->whiteTexture();
    }
    c.shape = static_cast<EmissionShape>(int(d[23]));
    c.shapeRadius = float(d[24]);
    c.shapeExtents = {float(d[25]), float(d[26])};
    c.duration = float(d[27]);
    c.looping = d[28] != 0;
    c.zOrder = float(d[29]);
    return c;
}

TextStyle textStyle(const DTextStyle& d) {
    TextStyle s;
    s.size = float(d.size);
    s.color = color(d.color);
    s.align = static_cast<TextAlign>(d.align);
    s.valign = static_cast<TextVAlign>(d.valign);
    s.wrapWidth = float(d.wrap);
    s.lineSpacing = float(d.lineSpacing);
    s.outlineWidth = float(d.outline);
    s.outlineColor = color(d.outlineColor);
    s.shadowOffset = v2(d.shadow);
    s.shadowColor = color(d.shadowColor);
    s.richText = d.rich != 0;
    s.zOrder = g.drawZ;
    return s;
}

void reapBursts() {
    World* w = world();
    if (!w) return;
    double now = g.elapsed;
    auto& b = g.bursts;
    for (size_t i = 0; i < b.size();) {
        if (now >= b[i].despawnAt) {
            if (w->isAlive(b[i].entity)) w->destroyEntity(b[i].entity);
            b[i] = b.back();
            b.pop_back();
        } else {
            ++i;
        }
    }
}

void closeApp() {
    if (!g.app) return;
    ensureJoined();
    for (auto& s : g.saves) {
        if (s && s->dirty()) s->flush();
    }
    delete g.app;
    g.app = nullptr;
    g.ids.clear();
    g.handles.clear();
    g.dead.clear();
    for (auto& s : g.snaps) s.rows.clear();
    for (auto& s : g.interest) s.clear();
    for (auto& s : g.nextInterest) s.clear();
    for (auto& e : g.events) e.clear();
    g.tweens.clear();
    g.autoVelocity.clear();
    g.scenes.clear();
    g.objects.clear();
    g.bursts.clear();
    g.fade = {};
}

} // namespace

// =========================================================================
// App and frame loop
// =========================================================================

namespace {

int32_t knownHandle(EntityId e) {
    auto it = g.handles.find(e);
    return it == g.handles.end() ? 0 : it->second;
}

void collectEvents() {
    for (auto& e : g.events) e.clear();
    auto* cb = res<CollisionBridge>();
    if (!cb) return;
    auto add = [](int k, EntityId a, EntityId b) {
        int32_t ha = knownHandle(a), hb = knownHandle(b);
        if (!ha || !hb) return;
        g.events[k].push_back(ha);
        g.events[k].push_back(hb);
    };
    for (int32_t i = 0; i < cb->collisionStartCount(); ++i) add(0, cb->collisionStartA(i), cb->collisionStartB(i));
    for (int32_t i = 0; i < cb->collisionEndCount(); ++i) add(1, cb->collisionEndA(i), cb->collisionEndB(i));
    for (int32_t i = 0; i < cb->sensorStartCount(); ++i) add(2, cb->sensorStartA(i), cb->sensorStartB(i));
    for (int32_t i = 0; i < cb->sensorEndCount(); ++i) add(3, cb->sensorEndA(i), cb->sensorEndB(i));
}

// Handles whose entity is gone (a Lifetime ran out, a parent was despawned).
void collectDead() {
    g.dead.clear();
    World* w = world();
    for (auto it = g.ids.begin(); it != g.ids.end();) {
        if (!w->isAlive(it->second)) {
            g.dead.push_back(it->first);
            g.handles.erase(it->second);
            it = g.ids.erase(it);
        } else {
            ++it;
        }
    }
}

bool readPull(int32_t kind, EntityId e, double* out);

void takeSnapshot() {
    for (int32_t k = 0; k < KindCount; ++k) {
        auto& snap = g.snaps[k];
        snap.rows.clear();
        if (!g.pipelined) { g.interest[k].clear(); continue; }
        for (int32_t h : g.interest[k]) {
            EntityId e = idOf(h);
            if (e == InvalidEntityId) continue;
            std::array<double, 8> row{};
            if (readPull(k, e, row.data())) snap.rows.emplace(h, row);
        }
        g.interest[k].clear();
    }
}

} // namespace

EXPORT const char* dc_error() { return g.error.c_str(); }

EXPORT cint dc_start(const char* title, cint width, cint height,
                     cint fullscreen, cint vsync, cint resizable,
                     cint scaleMode, cint lighting, const char* renderer,
                     cint pipelined, cint threads) {
    if (g.app) return fail("drift is already running");
    g.app = new App();
    Config& cfg = g.app->config();
    cfg.window.title = title ? title : "Drift";
    cfg.window.width = int32_t(width);
    cfg.window.height = int32_t(height);
    cfg.window.fullscreen = fullscreen != 0;
    cfg.window.resizable = resizable != 0;
    cfg.window.scaleMode = static_cast<ScaleMode>(scaleMode);
    cfg.render.vsync = vsync != 0;
    cfg.render.backend = renderer ? renderer : "";
    cfg.app.threads = int32_t(threads);
    g.app->addPlugins<DefaultPlugins>();
    if (lighting) g.app->addPlugin<LightingPlugin>();
    if (!g.app->start()) {
        delete g.app;
        g.app = nullptr;
        return fail("could not open a window and graphics device");
    }
    g.pipelined = pipelined != 0;
    return 1;
}

// Waits for the simulation the last dc_sync_end started. After this the
// script's component changes go into the world.
EXPORT cint dc_sync_begin() {
    if (!g.app) return 0;
    g.scriptDone = SDL_GetPerformanceCounter();
    bool wasInFlight = g.simInFlight;
    ensureJoined();
    if (!wasInFlight && g.joined < g.returned) g.joined = g.scriptDone;
    g.sim = g.app->lastSimulation();
    double script = ms(g.scriptDone - g.returned);
    double sim = g.sim.end > g.sim.start ? ms(g.sim.end - g.sim.start) : 0.0;
    double overlap = 0.0;
    if (g.sim.async && g.sim.end > g.sim.start) {
        uint64_t a = std::max(g.sim.start, g.returned);
        uint64_t b = std::min(g.sim.end, g.scriptDone);
        if (b > a) overlap = ms(b - a);
    }
    g.stats[0] = script;
    g.stats[1] = sim;
    g.stats[2] = overlap;
    g.stats[3] = g.joined > g.scriptDone ? ms(g.joined - g.scriptDone) : 0.0;
    g.stats[5] = double(g.sim.deferredUs) / 1000.0;
    g.stats[7] = g.sim.async ? 1.0 : 0.0;
    return 1;
}

// Draws and presents the frame, starts the next one and its simulation.
// Returns 0 once the app has quit; the window is closed by then.
EXPORT cint dc_sync_end() {
    if (!g.app) return 0;
    ensureJoined();
    uint64_t r0 = SDL_GetPerformanceCounter();
    g.app->endFrame();
    g.renderTicks = SDL_GetPerformanceCounter() - r0;
    g.stats[4] = ms(g.renderTicks);
    if (!g.app->pollFrame()) {
        closeApp();
        return 0;
    }
    if (auto* t = res<Time>()) {
        g.dt = t->delta;
        g.elapsed = t->elapsed;
    }
    g.drawZ = 0.f;
    reapBursts();
    if (!g.pipelined) g.app->simulate();
    collectEvents();
    collectDead();
    takeSnapshot();
    if (g.pipelined) {
        g.app->simulateAsync();
        g.simInFlight = true;
    }
    g.returned = SDL_GetPerformanceCounter();
    g.stats[6] = g.scriptDone ? ms(g.returned - g.scriptDone) : 0.0;
    return 1;
}

// Timing of the last frame in milliseconds: 0 script systems, 1 engine
// simulation, 2 the two overlapping, 3 waiting for the simulation after the
// script finished, 4 rendering and presenting, 5 systems that had to wait
// for the main thread, 6 the serial part of the frame (writing changes,
// rendering, polling, snapshot), 7 1.0 when the simulation ran in the
// background.
EXPORT double dc_stat(cint which) {
    return which >= 0 && which < 8 ? g.stats[which] : 0.0;
}

EXPORT cint dc_quit() { if (g.app) g.app->quit(); return 0; }

EXPORT double dc_dt() { return g.dt; }
EXPORT double dc_time() { return g.elapsed; }

EXPORT cint dc_set_title(const char* title) {
    if (!g.app) return 0;
    if (auto* w = static_cast<SDL_Window*>(g.app->sdlWindow())) SDL_SetWindowTitle(w, title);
    return 0;
}

EXPORT cint dc_set_clear_color(DColor c) {
    if (auto* r = res<RendererResource>()) r->setClearColor(colorf(c));
    return 0;
}

EXPORT cint dc_set_scale_mode(cint mode) {
    if (auto* r = res<RendererResource>()) r->setScaleMode(static_cast<ScaleMode>(mode));
    return 0;
}
EXPORT cint dc_set_logical_size(cint w, cint h) {
    if (auto* r = res<RendererResource>()) r->setLogicalSize(w, h);
    return 0;
}
EXPORT DV2 dc_logical_size() {
    auto* r = res<RendererResource>();
    return r ? DV2{r->logicalWidth(), r->logicalHeight()} : DV2{0, 0};
}

EXPORT cint dc_screenshot(const char* path) {
    if (auto* r = res<RendererResource>()) r->captureScreenshot(path);
    return 0;
}
EXPORT const char* dc_backend() {
    auto* r = res<RendererResource>();
    return keep(r ? r->backendName() : "");
}
EXPORT cint dc_draw_calls() { auto* r = res<RendererResource>(); return r ? r->drawCalls() : 0; }

// =========================================================================
// Input
// =========================================================================

EXPORT cint dc_key_down(cint key) {
    auto* in = res<InputResource>();
    return in && in->keyHeld(static_cast<Key>(key)) ? 1 : 0;
}
EXPORT cint dc_key_pressed(cint key) {
    auto* in = res<InputResource>();
    return in && in->keyPressed(static_cast<Key>(key)) ? 1 : 0;
}
EXPORT cint dc_key_released(cint key) {
    auto* in = res<InputResource>();
    return in && in->keyReleased(static_cast<Key>(key)) ? 1 : 0;
}
EXPORT DV2 dc_mouse_position() {
    auto* in = res<InputResource>();
    return in ? dv(in->mousePosition()) : DV2{0, 0};
}
EXPORT DV2 dc_mouse_delta() {
    auto* in = res<InputResource>();
    return in ? dv(in->mouseDelta()) : DV2{0, 0};
}
EXPORT double dc_mouse_wheel() {
    auto* in = res<InputResource>();
    return in ? in->mouseWheelDelta() : 0.0;
}
EXPORT cint dc_mouse_down(cint b) {
    auto* in = res<InputResource>();
    return in && in->mouseButtonHeld(static_cast<MouseButton>(b)) ? 1 : 0;
}
EXPORT cint dc_mouse_pressed(cint b) {
    auto* in = res<InputResource>();
    return in && in->mouseButtonPressed(static_cast<MouseButton>(b)) ? 1 : 0;
}
EXPORT cint dc_mouse_released(cint b) {
    auto* in = res<InputResource>();
    return in && in->mouseButtonReleased(static_cast<MouseButton>(b)) ? 1 : 0;
}
EXPORT cint dc_gamepad_connected(cint i) {
    auto* in = res<InputResource>();
    return in && in->gamepadConnected(i) ? 1 : 0;
}
EXPORT double dc_gamepad_axis(cint i, cint axis) {
    auto* in = res<InputResource>();
    return in ? in->gamepadAxis(i, axis) : 0.0;
}
EXPORT cint dc_gamepad_down(cint i, cint b) {
    auto* in = res<InputResource>();
    return in && in->gamepadButtonHeld(i, b) ? 1 : 0;
}
EXPORT cint dc_gamepad_pressed(cint i, cint b) {
    auto* in = res<InputResource>();
    return in && in->gamepadButtonPressed(i, b) ? 1 : 0;
}
EXPORT cint dc_gamepad_released(cint i, cint b) {
    auto* in = res<InputResource>();
    return in && in->gamepadButtonReleased(i, b) ? 1 : 0;
}

// ---- Synthetic input, for automated tests and demo playback ----
// Queued as SDL events, so they arrive with the next dc_frame like real input.

EXPORT cint dc_send_key(cint key, cint down) {
    if (!g.app) return 0;
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    ev.key.scancode = static_cast<SDL_Scancode>(key);
    ev.key.down = down != 0;
    ev.key.windowID = SDL_GetWindowID(static_cast<SDL_Window*>(g.app->sdlWindow()));
    return SDL_PushEvent(&ev) ? 1 : 0;
}
// Moves the mouse to a window pixel position; button 0 = move only.
EXPORT cint dc_send_mouse(DV2 windowPos, cint button, cint down) {
    if (!g.app) return 0;
    SDL_WindowID win = SDL_GetWindowID(static_cast<SDL_Window*>(g.app->sdlWindow()));
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.windowID = win;
    ev.motion.x = float(windowPos.x);
    ev.motion.y = float(windowPos.y);
    if (!SDL_PushEvent(&ev)) return 0;
    if (button <= 0) return 1;
    SDL_Event b{};
    b.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    b.button.windowID = win;
    b.button.button = uint8_t(button);
    b.button.down = down != 0;
    b.button.x = float(windowPos.x);
    b.button.y = float(windowPos.y);
    return SDL_PushEvent(&b) ? 1 : 0;
}

// ---- Input actions (bindings in InputBinding's text form) ----

namespace {
InputBinding binding(const char* text) { return InputBinding::parse(text ? text : ""); }
}

EXPORT cint dc_action_bind(const char* name, const char* b) {
    auto* a = res<InputActions>();
    InputBinding ib = binding(b);
    if (!a || !ib.valid()) return fail(std::string("not a binding: ") + (b ? b : ""));
    a->bindAction(name, ib);
    return 1;
}
EXPORT cint dc_action_bind_axis(const char* name, const char* neg, const char* pos) {
    auto* a = res<InputActions>();
    if (!a) return 0;
    a->bindAxis(name, binding(neg), binding(pos));
    return 1;
}
EXPORT cint dc_action_bind_axis2d(const char* name, const char* l, const char* r,
                                     const char* u, const char* d) {
    auto* a = res<InputActions>();
    if (!a) return 0;
    a->bindAxis2D(name, binding(l), binding(r), binding(u), binding(d));
    return 1;
}
EXPORT cint dc_action_bind_stick(const char* name, cint x, cint y) {
    auto* a = res<InputActions>();
    if (!a) return 0;
    a->bindStick(name, static_cast<GamepadAxis>(x), static_cast<GamepadAxis>(y));
    return 1;
}
EXPORT cint dc_action_pressed(const char* n) { auto* a = res<InputActions>(); return a && a->pressed(n) ? 1 : 0; }
EXPORT cint dc_action_held(const char* n) { auto* a = res<InputActions>(); return a && a->held(n) ? 1 : 0; }
EXPORT cint dc_action_released(const char* n) { auto* a = res<InputActions>(); return a && a->released(n) ? 1 : 0; }
EXPORT double dc_action_value(const char* n) { auto* a = res<InputActions>(); return a ? a->value(n) : 0.0; }
EXPORT double dc_action_axis(const char* n) { auto* a = res<InputActions>(); return a ? a->axis(n) : 0.0; }
EXPORT DV2 dc_action_axis2d(const char* n) {
    auto* a = res<InputActions>();
    return a ? dv(a->axis2D(n)) : DV2{0, 0};
}
EXPORT cint dc_action_has(const char* n) { auto* a = res<InputActions>(); return a && a->has(n) ? 1 : 0; }
EXPORT cint dc_action_remove(const char* n) { if (auto* a = res<InputActions>()) a->remove(n); return 0; }
EXPORT cint dc_action_rebind(const char* n, cint slot, cint index, const char* b) {
    auto* a = res<InputActions>();
    InputBinding ib = binding(b);
    if (!a || !ib.valid() || !a->has(n)) return 0;
    a->rebind(n, slot, size_t(std::max<cint>(index, 0)), ib);
    return 1;
}
EXPORT cint dc_action_binding_count(const char* n, cint slot) {
    auto* a = res<InputActions>();
    if (!a || !a->has(n)) return 0;
    return int32_t(a->bindings(n, slot).size());
}
EXPORT const char* dc_action_binding(const char* n, cint slot, cint index) {
    auto* a = res<InputActions>();
    if (!a || !a->has(n)) return keep("");
    const auto& list = a->bindings(n, slot);
    if (index < 0 || index >= int32_t(list.size())) return keep("");
    return keep(list[size_t(index)].toString());
}
EXPORT const char* dc_action_capture() {
    auto* a = res<InputActions>();
    auto* in = res<InputResource>();
    if (!a || !in) return keep("");
    auto b = a->captureNext(*in);
    return keep(b ? b->toString() : "");
}
EXPORT const char* dc_action_save() {
    auto* a = res<InputActions>();
    return keep(a ? a->toJson().dump() : "{}");
}
EXPORT cint dc_action_load(const char* text) {
    auto* a = res<InputActions>();
    if (!a) return 0;
    auto j = nlohmann::json::parse(text ? text : "", nullptr, false);
    if (j.is_discarded()) return -1;
    return a->fromJson(j);
}
EXPORT cint dc_action_settings(double deadZone, double pressThreshold, cint gamepad) {
    auto* a = res<InputActions>();
    if (!a) return 0;
    a->deadZone = float(deadZone);
    a->pressThreshold = float(pressThreshold);
    a->gamepad = gamepad;
    return 0;
}

// =========================================================================
// Textures and spritesheets
// =========================================================================

EXPORT cint dc_texture_load(const char* path) {
    auto* assets = res<AssetServer>();
    if (!assets) return fail("drift is not running");
    TextureHandle t = assets->loadTexture(path);
    if (!t.valid()) return fail(std::string("cannot load texture ") + path);
    return bits(t.id);
}

// rgba: 4 floats (0..1) per pixel, row by row from the top.
EXPORT cint dc_texture_create(cint w, cint h, const double* rgba, cint count) {
    auto* r = res<RendererResource>();
    if (!r) return fail("drift is not running");
    if (w <= 0 || h <= 0 || count < w * h * 4) return fail("pixel list is shorter than width * height * 4");
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < px.size(); ++i) px[i] = channel(rgba[i]);
    TextureHandle t = r->createTextureFromPixels(px.data(), w, h);
    if (!t.valid()) return fail("cannot create texture");
    return bits(t.id);
}
EXPORT cint dc_texture_render_target(cint w, cint h) {
    auto* r = res<RendererResource>();
    if (!r) return fail("drift is not running");
    TextureHandle t = r->createRenderTarget(w, h);
    if (!t.valid()) return fail("cannot create render target");
    return bits(t.id);
}
EXPORT cint dc_texture_white() {
    auto* r = res<RendererResource>();
    return r ? bits(r->whiteTexture().id) : 0;
}
EXPORT DV2 dc_texture_size(cint t) {
    auto* r = res<RendererResource>();
    return r ? dv(r->textureSize(handle<TextureHandle>(t))) : DV2{0, 0};
}
EXPORT cint dc_texture_options(cint t, cint filter, cint wrap) {
    auto* r = res<RendererResource>();
    if (!r) return 0;
    TextureOptions o;
    o.filter = static_cast<TextureFilter>(filter);
    o.wrap = static_cast<TextureWrap>(wrap);
    r->setTextureOptions(handle<TextureHandle>(t), o);
    return 0;
}
EXPORT cint dc_texture_destroy(cint t) {
    if (auto* r = res<RendererResource>()) r->destroyTexture(handle<TextureHandle>(t));
    return 0;
}

EXPORT cint dc_sheet_create(cint tex, cint fw, cint fh) {
    auto* s = res<SpriteResource>();
    if (!s) return fail("drift is not running");
    SpritesheetHandle h = s->createSpritesheet(handle<TextureHandle>(tex), fw, fh);
    if (!h.valid()) return fail("cannot create spritesheet");
    return bits(h.id);
}
EXPORT cint dc_sheet_load_aseprite(const char* path) {
    auto* s = res<SpriteResource>();
    if (!s) return fail("drift is not running");
    SpritesheetHandle h = s->loadAseprite(path);
    if (!h.valid()) return fail(std::string("cannot load ") + path);
    return bits(h.id);
}
EXPORT DRect dc_sheet_frame(cint sheet, cint col, cint row) {
    auto* s = res<SpriteResource>();
    return s ? drect(s->getFrame(handle<SpritesheetHandle>(sheet), col, row)) : DRect{0, 0, 0, 0};
}
// Frame by index, counting left to right, then top to bottom.
EXPORT DRect dc_sheet_frame_at(cint sheet, cint index) {
    auto* s = res<SpriteResource>();
    auto* r = res<RendererResource>();
    if (!s || !r) return {0, 0, 0, 0};
    auto h = handle<SpritesheetHandle>(sheet);
    Rect first = s->getFrame(h, 0, 0);
    Vec2 size = r->textureSize(s->getTexture(h));
    int cols = first.w > 0 ? std::max(1, int(size.x / first.w)) : 1;
    return drect(s->getFrame(h, index % cols, index / cols));
}
EXPORT cint dc_sheet_frame_count(cint sheet) {
    auto* s = res<SpriteResource>();
    return s ? s->frameCount(handle<SpritesheetHandle>(sheet)) : 0;
}
EXPORT cint dc_sheet_texture(cint sheet) {
    auto* s = res<SpriteResource>();
    return s ? bits(s->getTexture(handle<SpritesheetHandle>(sheet)).id) : 0;
}

// =========================================================================
// Immediate drawing (queued for this frame, drawn by the next dc_frame)
// =========================================================================

EXPORT cint dc_set_layer(cint layer) {
    if (auto* r = res<RendererResource>()) r->setLayer(uint8_t(std::clamp<cint>(layer, 0, 31)));
    return 0;
}
EXPORT cint dc_layer() { auto* r = res<RendererResource>(); return r ? r->layer() : 0; }
EXPORT cint dc_set_z(double z) { g.drawZ = float(z); return 0; }

EXPORT cint dc_draw_rect(DRect r, DColor c) {
    if (auto* rr = res<RendererResource>()) rr->drawRectFilled(rect(r), color(c), g.drawZ);
    return 0;
}
EXPORT cint dc_draw_rect_lines(DRect r, DColor c, double thickness) {
    if (auto* rr = res<RendererResource>()) rr->drawRect(rect(r), color(c), float(thickness), g.drawZ);
    return 0;
}
EXPORT cint dc_draw_rounded_rect(DRect r, double radius, DColor c) {
    if (auto* rr = res<RendererResource>()) rr->drawRoundedRectFilled(rect(r), float(radius), color(c), g.drawZ);
    return 0;
}
EXPORT cint dc_draw_circle(DV2 center, double radius, DColor c) {
    if (auto* rr = res<RendererResource>()) rr->drawCircleFilled(v2(center), float(radius), color(c), 0, g.drawZ);
    return 0;
}
EXPORT cint dc_draw_circle_lines(DV2 center, double radius, DColor c, double thickness) {
    if (auto* rr = res<RendererResource>())
        rr->drawCircle(v2(center), float(radius), color(c), float(thickness), 0, g.drawZ);
    return 0;
}
EXPORT cint dc_draw_arc(DV2 center, double radius, double from, double to, DColor c, double thickness) {
    if (auto* rr = res<RendererResource>())
        rr->drawArc(v2(center), float(radius), float(from), float(to), color(c), float(thickness), g.drawZ);
    return 0;
}
EXPORT cint dc_draw_line(DV2 a, DV2 b, DColor c, double thickness) {
    if (auto* rr = res<RendererResource>()) rr->drawLine(v2(a), v2(b), color(c), float(thickness), g.drawZ);
    return 0;
}
EXPORT cint dc_draw_triangle(DV2 a, DV2 b, DV2 c, DColor col) {
    if (auto* rr = res<RendererResource>()) rr->drawTriangleFilled(v2(a), v2(b), v2(c), color(col), g.drawZ);
    return 0;
}

namespace {
std::vector<Vec2> points(const double* xy, int32_t count) {
    std::vector<Vec2> out;
    for (int32_t i = 0; i + 1 < count; i += 2) out.push_back({float(xy[i]), float(xy[i + 1])});
    return out;
}
}

// xy: x0, y0, x1, y1, ...; count is the number of floats.
EXPORT cint dc_draw_polygon(const double* xy, cint count, DColor c) {
    auto pts = points(xy, count);
    if (auto* rr = res<RendererResource>())
        rr->drawPolygonFilled(pts.data(), int32_t(pts.size()), color(c), g.drawZ);
    return 0;
}
EXPORT cint dc_draw_polyline(const double* xy, cint count, DColor c, double thickness, cint closed) {
    auto pts = points(xy, count);
    if (auto* rr = res<RendererResource>())
        rr->drawPolyline(pts.data(), int32_t(pts.size()), color(c), float(thickness), closed != 0, g.drawZ);
    return 0;
}

EXPORT cint dc_draw_texture(cint tex, DV2 pos, DSprite s) {
    auto* rr = res<RendererResource>();
    if (!rr) return 0;
    SpriteDraw d;
    d.texture = handle<TextureHandle>(tex);
    d.position = v2(pos);
    d.srcRect = s.src.w > 0 ? rect(s.src) : Rect{};
    d.scale = v2(s.scale);
    d.rotation = float(s.rotation);
    d.origin = v2(s.origin);
    d.tint = color(s.tint);
    d.flip = static_cast<Flip>(s.flip);
    d.blend = static_cast<BlendMode>(s.blend);
    d.zOrder = g.drawZ;
    d.layer = rr->layer();
    rr->draw(d);
    return 0;
}

EXPORT cint dc_draw_text(cint font, const char* text, DV2 pos, DTextStyle style) {
    if (auto* f = res<FontResource>()) f->drawText(handle<FontHandle>(font), text ? text : "", v2(pos), textStyle(style));
    return 0;
}
EXPORT DV2 dc_measure_text(cint font, const char* text, DTextStyle style) {
    auto* f = res<FontResource>();
    return f ? dv(f->measureText(handle<FontHandle>(font), text ? text : "", textStyle(style))) : DV2{0, 0};
}
EXPORT double dc_line_height(cint font, double size) {
    auto* f = res<FontResource>();
    return f ? f->lineHeight(handle<FontHandle>(font), float(size)) : 0.0;
}
EXPORT cint dc_font_load(const char* path, double size) {
    auto* f = res<FontResource>();
    if (!f) return fail("drift is not running");
    FontHandle h = f->loadFont(path, float(size));
    if (!h.valid()) return fail(std::string("cannot load font ") + path);
    return bits(h.id);
}

EXPORT cint dc_push_clip(DRect r) { if (auto* rr = res<RendererResource>()) rr->pushClipRect(rect(r)); return 0; }
EXPORT cint dc_pop_clip() { if (auto* rr = res<RendererResource>()) rr->popClipRect(); return 0; }

EXPORT DV2 dc_screen_to_world(DV2 p) {
    auto* rr = res<RendererResource>();
    return rr ? dv(rr->screenToWorld(v2(p))) : p;
}
EXPORT DV2 dc_world_to_screen(DV2 p) {
    auto* rr = res<RendererResource>();
    return rr ? dv(rr->worldToScreen(v2(p))) : p;
}

// =========================================================================
// Entities
// =========================================================================
//
// The script spawns entities by reserving a handle (no world access, so it
// can happen while the simulation runs) and creating them all at the next
// sync. Despawning, parenting and component writes happen at the sync too.

// A new handle for an entity dc_create makes later.
EXPORT cint dc_reserve() { return g.app ? g.nextHandle++ : 0; }

// Makes entities for reserved handles, each with a Transform2D.
EXPORT cint dc_create(const cint* hs, cint n) {
    World* w = world();
    if (!w) return 0;
    ensureJoined();
    for (cint i = 0; i < n; ++i) {
        int32_t h = int32_t(hs[i]);
        if (g.ids.count(h)) continue;
        EntityId e = w->createEntity();
        w->set<Transform2D>(e, cid<Transform2D>(), Transform2D{});
        g.ids[h] = e;
        g.handles[e] = h;
    }
    return 0;
}

// Removes the entity and its children.
EXPORT cint dc_despawn(cint h) {
    ensureJoined();
    EntityId e = idOf(int32_t(h));
    if (e != InvalidEntityId) {
        std::vector<EntityId> stack{e};
        std::vector<EntityId> order;
        while (!stack.empty()) {
            EntityId cur = stack.back();
            stack.pop_back();
            order.push_back(cur);
            if (auto* ch = get<Children>(cur)) {
                for (int i = 0; i < ch->count; ++i) {
                    if (world()->isAlive(ch->ids[i])) stack.push_back(ch->ids[i]);
                }
            }
        }
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            if (world()->isAlive(*it)) world()->destroyEntity(*it);
        }
    }
    forget(int32_t(h));
    g.tweens.erase(int32_t(h));
    g.autoVelocity.erase(int32_t(h));
    return 0;
}

// Handles whose entity went away in the engine since the last sync (a
// Lifetime ran out, a parent took it along).
EXPORT cint dc_dead_count() { return cint(g.dead.size()); }
EXPORT cint dc_dead(cint* out, cint n) {
    for (cint i = 0; i < n && i < cint(g.dead.size()); ++i) out[i] = g.dead[size_t(i)];
    return 0;
}

EXPORT cint dc_set_parent(cint child, cint parent) {
    ensureJoined();
    EntityId c = idOf(int32_t(child));
    EntityId p = idOf(int32_t(parent));
    if (c == InvalidEntityId) return 0;
    if (const Parent* old = get<Parent>(c)) {
        if (old->entity != InvalidEntityId && world()->isAlive(old->entity)) {
            Children ch{};
            if (const Children* cur = get<Children>(old->entity)) ch = *cur;
            ch.remove(c);
            put<Children>(old->entity, ch);
        }
    }
    if (p == InvalidEntityId) {
        world()->removeComponent(c, cid<Parent>());
        return 0;
    }
    put<Parent>(c, Parent{p});
    Children ch{};
    if (const Children* cur = get<Children>(p)) ch = *cur;
    ch.add(c);
    put<Children>(p, ch);
    return 0;
}

EXPORT DV2 dc_world_position(cint h) {
    ensureJoined();
    EntityId e = idOf(int32_t(h));
    if (e == InvalidEntityId) return {0, 0};
    if (const auto* gt = get<GlobalTransform2D>(e)) return dv(gt->position);
    const Transform2D* t = get<Transform2D>(e);
    return t ? dv(t->position) : DV2{0, 0};
}

// Any reflected component as JSON (the scene file form), for components
// the package does not mirror.
EXPORT const char* dc_component_json(cint h, const char* component) {
    ensureJoined();
    EntityId e = idOf(int32_t(h));
    World* w = world();
    const auto* info = component ? reflect::findComponent(component) : nullptr;
    if (e == InvalidEntityId || !w || !info) return keep("");
    ComponentId c = info->id(*w);
    if (!c || !w->hasComponent(e, c)) return keep("");
    reflect::JsonContext ctx;
    ctx.assets = res<AssetServer>();
    return keep(reflect::toJson(*info->info, w->getComponent(e, c), ctx).dump());
}
EXPORT cint dc_set_component_json(cint h, const char* component, const char* text) {
    ensureJoined();
    EntityId e = idOf(int32_t(h));
    World* w = world();
    const auto* info = component ? reflect::findComponent(component) : nullptr;
    if (e == InvalidEntityId || !w) return fail("not a live entity");
    if (!info) return fail(std::string("no reflected component named ") + (component ? component : ""));
    auto j = nlohmann::json::parse(text ? text : "", nullptr, false);
    if (j.is_discarded()) return fail("invalid JSON");
    ComponentId c = info->id(*w);
    if (!c) return fail(std::string(component) + " is not registered");
    reflect::Value value(*info->info, w->hasComponent(e, c) ? w->getComponent(e, c) : nullptr);
    reflect::JsonContext ctx;
    ctx.assets = res<AssetServer>();
    reflect::fromJson(*info->info, value.get(), j, ctx);
    w->setComponent(e, c, value.get(), info->info->size);
    return 1;
}

// =========================================================================
// Components
// =========================================================================
//
// Layouts, in doubles (bools are 0/1, enums their index, handles ints):
//
//   Transform       x, y, rotation, scale x, scale y
//   Velocity        x, y, angular
//   Sprite          texture, frame x, y, w, h, anchor x, y, color r, g, b, a,
//                   flip (1 x, 2 y), z, layer, visible, blend, mask; string image
//                   mask: 1 frame, 2 color, 4 the rest
//   Shape           kind, w, h, radius, color r, g, b, a, outline, corner,
//                   anchor x, y, z, layer, visible
//   Text            size, color r, g, b, a, align, valign, wrap, line spacing,
//                   outline, outline color r, g, b, a, z, layer, visible, font;
//                   string text
//   Camera          zoom, rotation, active, order, viewport x, y, w, h
//   CameraFollow    target, smoothing, offset x, y, dead zone x, y
//   RigidBody       kind, fixed rotation, gravity scale, linear damping,
//                   angular damping
//   BoxCollider     half x, y, offset x, y, sensor, density, friction,
//                   restitution, category, mask
//   CircleCollider  radius, offset x, y, sensor, density, friction,
//                   restitution, category, mask
//   ParticleEmitter see emitterConfig() above
//   PointLight      color r, g, b, intensity, radius, falloff, shadows
//   SpotLight       color r, g, b, intensity, radius, falloff, direction,
//                   angle, softness, shadows
//   LightOccluder   shape, half x, y, radius
//   AudioSource     sound, bus, volume, pitch, loop, autoplay, spatial,
//                   min distance, max distance
//   Tilemap         map, z, layer, colliders
//   Animator        clip, playing, clip count, then per clip: frame count,
//                   frame seconds, looping, x, y, w, h per frame
//   Lifetime        seconds
//   Oscillate       amplitude x, y, frequency, phase
//   Follow          target, speed, distance, offset x, y
//   Name            (none); string name
//   Tween           repeat, yoyo, step count, then per step: kind, a, b, c,
//                   d, seconds, ease

namespace {

TextureHandle spriteTexture(double tex, const char* image) {
    if (tex != 0) return handle<TextureHandle>(cint(tex));
    if (image && *image) {
        if (auto* assets = res<AssetServer>()) {
            TextureHandle t = assets->loadTexture(image);
            if (!t.valid()) g.error = std::string("cannot load texture ") + image;
            return t;
        }
        return {};
    }
    auto* r = res<RendererResource>();
    return r ? r->whiteTexture() : TextureHandle{};
}

Ease ease(int32_t e) { return static_cast<Ease>(std::clamp(e, 0, int32_t(Ease::Count_) - 1)); }

template <typename T> void fillCollider(T& c, const double* d) {
    c.offset = {float(d[0]), float(d[1])};
    c.isSensor = d[2] != 0;
    c.density = float(d[3]);
    c.friction = float(d[4]);
    c.restitution = float(d[5]);
    c.filter.category = uint32_t(d[6]);
    c.filter.mask = uint32_t(d[7]);
}

void startTween(int32_t h, EntityId e, const double* d, int32_t n) {
    auto* tw = res<Tweens>();
    if (!tw) return;
    auto old = g.tweens.find(h);
    if (old != g.tweens.end()) {
        tw->stop(old->second, false);
        g.tweens.erase(old);
    }
    if (n < 3) return;
    TweenBuilder b = tw->on(e);
    int32_t steps = int32_t(d[2]);
    for (int32_t i = 0; i < steps && 3 + i * 7 + 6 < n; ++i) {
        const double* s = d + 3 + i * 7;
        float secs = float(s[5]);
        Ease ez = ease(int32_t(s[6]));
        switch (int(s[0])) {
            case 0: b.moveTo({float(s[1]), float(s[2])}, secs, ez); break;
            case 1: b.moveBy({float(s[1]), float(s[2])}, secs, ez); break;
            case 2: b.scaleTo({float(s[1]), float(s[2])}, secs, ez); break;
            case 3: b.rotateTo(float(s[1]), secs, ez); break;
            case 4: b.rotateBy(float(s[1]), secs, ez); break;
            case 5: b.fadeTo(float(s[1]), secs, ez); break;
            case 6: b.zoomTo(float(s[1]), secs, ez); break;
            case 7: b.tintTo(color(DColor{s[1], s[2], s[3], s[4]}), secs, ez); break;
            case 8: b.delay(secs); break;
        }
    }
    int32_t times = int32_t(d[0]);
    if (times != 0) b.loop(times, d[1] != 0 ? TweenLoop::Yoyo : TweenLoop::Restart);
    g.tweens[h] = b.start();
}

void setAnimator(EntityId e, const double* d, int32_t n) {
    SpriteAnimator a{};
    const SpriteAnimator* cur = get<SpriteAnimator>(e);
    if (cur) a = *cur;
    std::vector<AnimationClip> clips;
    int32_t count = n >= 3 ? int32_t(d[2]) : 0;
    int32_t at = 3;
    for (int32_t c = 0; c < count && at + 3 <= n; ++c) {
        AnimationClip clip;
        int32_t frames = int32_t(d[at]);
        clip.frameDuration = float(d[at + 1]);
        clip.looping = d[at + 2] != 0;
        at += 3;
        for (int32_t f = 0; f < frames && at + 4 <= n; ++f, at += 4)
            clip.frames.push_back({float(d[at]), float(d[at + 1]), float(d[at + 2]), float(d[at + 3])});
        clips.push_back(std::move(clip));
    }
    bool same = cur && clips.size() == a.clips.size();
    for (size_t i = 0; same && i < clips.size(); ++i) {
        const auto& x = clips[i];
        const auto& y = a.clips[i];
        same = x.frameDuration == y.frameDuration && x.looping == y.looping && x.frames.size() == y.frames.size();
        for (size_t f = 0; same && f < x.frames.size(); ++f) {
            same = x.frames[f].x == y.frames[f].x && x.frames[f].y == y.frames[f].y &&
                   x.frames[f].w == y.frames[f].w && x.frames[f].h == y.frames[f].h;
        }
    }
    int32_t clip = int32_t(d[0]);
    bool playing = d[1] != 0;
    if (!same) {
        a.clips = std::move(clips);
        a.play(clip);
    } else if (clip != a.currentClip) {
        a.play(clip);
    }
    if (!playing) a.playing = false;
    else if (!a.playing && !a.isFinished()) a.playing = true;
    put<SpriteAnimator>(e, a);
    if (!get<Sprite>(e)) put<Sprite>(e, Sprite{});
}

// Writes one component from its layout.
void writeKind(int32_t kind, int32_t h, EntityId e, const double* d, int32_t n, const char* s) {
    switch (kind) {
        case KTransform: {
            if (n < 5) return;
            Transform2D t;
            if (const Transform2D* cur = get<Transform2D>(e)) t = *cur;
            t.position = {float(d[0]), float(d[1])};
            t.rotation = float(d[2]);
            t.scale = {float(d[3]), float(d[4])};
            put<Transform2D>(e, t);
            teleportBody(e, t.position, t.rotation);
            break;
        }
        case KVelocity: {
            if (n < 3) return;
            put<Velocity2D>(e, Velocity2D{{float(d[0]), float(d[1])}, float(d[2])});
            g.autoVelocity.erase(h);
            break;
        }
        case KSprite: {
            if (n < 17) return;
            const Sprite* cur = get<Sprite>(e);
            Sprite sp = cur ? *cur : Sprite{};
            int32_t mask = cur ? int32_t(d[16]) : 7;
            if (mask & 4) {
                sp.texture = spriteTexture(d[0], s);
                sp.flip = static_cast<Flip>(int(d[11]));
                sp.zOrder = float(d[12]);
                sp.layer = uint8_t(std::clamp(int(d[13]), 0, 31));
                sp.visible = d[14] != 0;
                sp.blend = static_cast<BlendMode>(int(d[15]));
            }
            if (mask & 1) sp.srcRect = d[3] > 0 ? Rect{float(d[1]), float(d[2]), float(d[3]), float(d[4])} : Rect{};
            if (mask & 2) sp.tint = color(DColor{d[7], d[8], d[9], d[10]});
            if (mask & 5) {
                Vec2 size = {sp.srcRect.w, sp.srcRect.h};
                if (size.x <= 0)
                    if (auto* r = res<RendererResource>()) size = r->textureSize(sp.texture);
                sp.origin = {float(d[5]) * size.x, float(d[6]) * size.y};
            }
            put<Sprite>(e, sp);
            break;
        }
        case KShape: {
            if (n < 15) return;
            Shape2D sh;
            sh.kind = d[0] != 0 ? ShapeKind::Circle : ShapeKind::Rectangle;
            sh.size = {float(d[1]), float(d[2])};
            sh.radius = float(d[3]);
            sh.color = color(DColor{d[4], d[5], d[6], d[7]});
            sh.outline = float(d[8]);
            sh.cornerRadius = float(d[9]);
            sh.anchor = {float(d[10]), float(d[11])};
            sh.zOrder = float(d[12]);
            sh.layer = uint8_t(std::clamp(int(d[13]), 0, 31));
            sh.visible = d[14] != 0;
            put<Shape2D>(e, sh);
            break;
        }
        case KText: {
            if (n < 19) return;
            Text t;
            t.set(s ? s : "");
            t.size = float(d[0]);
            t.color = color(DColor{d[1], d[2], d[3], d[4]});
            t.align = static_cast<TextAlign>(int(d[5]));
            t.valign = static_cast<TextVAlign>(int(d[6]));
            t.wrapWidth = float(d[7]);
            t.lineSpacing = float(d[8]);
            t.outlineWidth = float(d[9]);
            t.outlineColor = color(DColor{d[10], d[11], d[12], d[13]});
            t.zOrder = float(d[14]);
            t.layer = uint8_t(std::clamp(int(d[15]), 0, 31));
            t.visible = d[16] != 0;
            t.font = handle<FontHandle>(cint(d[17]));
            put<Text>(e, t);
            break;
        }
        case KCamera: {
            if (n < 8) return;
            Camera c;
            if (const Camera* cur = get<Camera>(e)) c = *cur;
            c.zoom = float(d[0]);
            c.rotation = float(d[1]);
            c.active = d[2] != 0;
            c.order = int32_t(d[3]);
            c.viewport = {float(d[4]), float(d[5]), float(d[6]), float(d[7])};
            put<Camera>(e, c);
            break;
        }
        case KCameraFollow: {
            if (n < 6) return;
            CameraFollow f;
            f.target = idOf(int32_t(d[0]));
            f.smoothing = float(d[1]);
            f.offset = {float(d[2]), float(d[3])};
            f.deadZone = {float(d[4]), float(d[5])};
            put<CameraFollow>(e, f);
            break;
        }
        case KRigidBody: {
            if (n < 5) return;
            RigidBody2D rb;
            rb.type = static_cast<BodyType>(int(d[0]));
            rb.fixedRotation = d[1] != 0;
            rb.gravityScale = float(d[2]);
            rb.linearDamping = float(d[3]);
            rb.angularDamping = float(d[4]);
            put<RigidBody2D>(e, rb);
            // Velocity2D marks a body Box2D moves; static and kinematic
            // bodies follow their Transform2D instead.
            if (rb.type == BodyType::Dynamic && !get<Velocity2D>(e)) {
                put<Velocity2D>(e, Velocity2D{});
                g.autoVelocity.insert(h);
            }
            break;
        }
        case KBoxCollider: {
            if (n < 10) return;
            BoxCollider2D c;
            c.halfSize = {float(d[0]), float(d[1])};
            fillCollider(c, d + 2);
            put<BoxCollider2D>(e, c);
            break;
        }
        case KCircleCollider: {
            if (n < 9) return;
            CircleCollider2D c;
            c.radius = float(d[0]);
            fillCollider(c, d + 1);
            put<CircleCollider2D>(e, c);
            break;
        }
        case KEmitter: {
            if (n < EmitterSize) return;
            ParticleEmitter pe;
            if (const ParticleEmitter* cur = get<ParticleEmitter>(e)) pe = *cur;
            pe.config = emitterConfig(d);
            pe.playing = d[30] != 0;
            put<ParticleEmitter>(e, pe);
            break;
        }
        case KPointLight: {
            if (n < 7) return;
            PointLight2D l;
            l.color = {float(d[0]), float(d[1]), float(d[2]), 1.f};
            l.intensity = float(d[3]);
            l.radius = float(d[4]);
            l.falloff = float(d[5]);
            l.castShadows = d[6] != 0;
            put<PointLight2D>(e, l);
            break;
        }
        case KSpotLight: {
            if (n < 10) return;
            SpotLight2D l;
            l.color = {float(d[0]), float(d[1]), float(d[2]), 1.f};
            l.intensity = float(d[3]);
            l.radius = float(d[4]);
            l.falloff = float(d[5]);
            l.direction = float(d[6]);
            l.angle = float(d[7]);
            l.softness = float(d[8]);
            l.castShadows = d[9] != 0;
            put<SpotLight2D>(e, l);
            break;
        }
        case KOccluder: {
            if (n < 4) return;
            LightOccluder2D o;
            o.shape = static_cast<OccluderShape>(int(d[0]));
            o.halfSize = {float(d[1]), float(d[2])};
            o.radius = float(d[3]);
            put<LightOccluder2D>(e, o);
            break;
        }
        case KAudioSource: {
            if (n < 9) return;
            AudioSource a;
            const AudioSource* cur = get<AudioSource>(e);
            if (cur) a = *cur;
            SoundHandle sound = handle<SoundHandle>(cint(d[0]));
            bool restart = cur && cur->sound.id != sound.id;
            a.sound = sound;
            a.bus = handle<BusHandle>(cint(d[1]));
            a.volume = float(d[2]);
            a.pitch = float(d[3]);
            a.loop = d[4] != 0;
            a.autoplay = d[5] != 0;
            a.spatial = d[6] != 0;
            a.minDistance = float(d[7]);
            a.maxDistance = float(d[8]);
            if (restart && a.autoplay) a.playRequested = true;
            put<AudioSource>(e, a);
            break;
        }
        case KTilemap: {
            if (n < 4) return;
            Tilemap t;
            if (const Tilemap* cur = get<Tilemap>(e)) t = *cur;
            t.map = handle<TilemapHandle>(cint(d[0]));
            t.zOrder = float(d[1]);
            t.renderLayer = uint8_t(std::clamp(int(d[2]), 0, 31));
            t.colliders = d[3] != 0;
            put<Tilemap>(e, t);
            break;
        }
        case KAnimator: setAnimator(e, d, n); break;
        case KLifetime: if (n >= 1) put<Lifetime>(e, Lifetime{float(d[0])}); break;
        case KOscillate: {
            if (n < 4) return;
            Oscillate o;
            if (const Oscillate* cur = get<Oscillate>(e)) o = *cur;
            o.amplitude = {float(d[0]), float(d[1])};
            o.frequency = float(d[2]);
            o.phase = float(d[3]);
            put<Oscillate>(e, o);
            break;
        }
        case KFollow: {
            if (n < 5) return;
            Follow f;
            f.target = idOf(int32_t(d[0]));
            f.speed = float(d[1]);
            f.distance = float(d[2]);
            f.offset = {float(d[3]), float(d[4])};
            put<Follow>(e, f);
            break;
        }
        case KName: put<Name>(e, Name(s ? s : "")); break;
        case KTween: startTween(h, e, d, n); break;
    }
}

void removeKind(int32_t kind, int32_t h, EntityId e) {
    switch (kind) {
        case KTransform: put<Transform2D>(e, Transform2D{}); break;   // every entity keeps one
        case KVelocity: removeComponent<Velocity2D>(e); break;
        case KSprite: removeComponent<Sprite>(e); break;
        case KShape: removeComponent<Shape2D>(e); break;
        case KText: removeComponent<Text>(e); break;
        case KCamera: removeComponent<Camera>(e); break;
        case KCameraFollow: removeComponent<CameraFollow>(e); break;
        case KRigidBody:
            removeComponent<RigidBody2D>(e);
            if (g.autoVelocity.erase(h)) removeComponent<Velocity2D>(e);
            break;
        case KBoxCollider: removeComponent<BoxCollider2D>(e); break;
        case KCircleCollider: removeComponent<CircleCollider2D>(e); break;
        case KEmitter: removeComponent<ParticleEmitter>(e); break;
        case KPointLight: removeComponent<PointLight2D>(e); break;
        case KSpotLight: removeComponent<SpotLight2D>(e); break;
        case KOccluder: removeComponent<LightOccluder2D>(e); break;
        case KAudioSource: {
            if (const AudioSource* a = get<AudioSource>(e)) {
                if (auto* audio = res<AudioResource>()) audio->stopSound(a->instance, 0.f);
            }
            removeComponent<AudioSource>(e);
            break;
        }
        case KTilemap: removeComponent<Tilemap>(e); break;
        case KAnimator: removeComponent<SpriteAnimator>(e); break;
        case KLifetime: removeComponent<Lifetime>(e); break;
        case KOscillate: removeComponent<Oscillate>(e); break;
        case KFollow: removeComponent<Follow>(e); break;
        case KName: removeComponent<Name>(e); break;
        case KTween: {
            auto it = g.tweens.find(h);
            if (it != g.tweens.end()) {
                if (auto* tw = res<Tweens>()) tw->stop(it->second, false);
                g.tweens.erase(it);
            }
            break;
        }
    }
}

bool readPull(int32_t kind, EntityId e, double* out) {
    switch (kind) {
        case KTransform: {
            const Transform2D* t = get<Transform2D>(e);
            if (!t) return false;
            out[0] = t->position.x; out[1] = t->position.y; out[2] = t->rotation;
            out[3] = t->scale.x; out[4] = t->scale.y;
            return true;
        }
        case KVelocity: {
            const Velocity2D* v = get<Velocity2D>(e);
            if (!v) return false;
            out[0] = v->linear.x; out[1] = v->linear.y; out[2] = v->angular;
            return true;
        }
        case KSprite: {
            const Sprite* s = get<Sprite>(e);
            if (!s) return false;
            out[0] = s->srcRect.x; out[1] = s->srcRect.y; out[2] = s->srcRect.w; out[3] = s->srcRect.h;
            DColor c = dcolor(s->tint);
            out[4] = c.r; out[5] = c.g; out[6] = c.b; out[7] = c.a;
            return true;
        }
        case KCamera: {
            const Camera* c = get<Camera>(e);
            if (!c) return false;
            out[0] = c->zoom;
            return true;
        }
        case KAnimator: {
            const SpriteAnimator* a = get<SpriteAnimator>(e);
            if (!a) return false;
            out[0] = a->currentClip; out[1] = a->currentFrame;
            out[2] = a->isFinished() ? 1 : 0; out[3] = a->playing ? 1 : 0;
            return true;
        }
        case KLifetime: {
            const Lifetime* l = get<Lifetime>(e);
            if (!l) return false;
            out[0] = l->seconds;
            return true;
        }
    }
    return false;
}

bool pullable(int32_t kind) { return pullStride(kind) > 0; }

constexpr double Missing = -1e300;

} // namespace

// Writes one component. `data` holds the kind's layout; `s` its string
// field, "" when it has none.
EXPORT cint dc_set(cint kind, cint h, const double* data, cint n, const char* s) {
    ensureJoined();
    EntityId e = idOf(int32_t(h));
    if (e == InvalidEntityId || kind < 0 || kind >= KindCount) return 0;
    writeKind(int32_t(kind), int32_t(h), e, data, int32_t(n), s);
    if (pullable(int32_t(kind))) g.interest[kind].insert(int32_t(h));
    return 1;
}

// Writes one component on many entities: `stride` doubles each.
EXPORT cint dc_set_many(cint kind, const cint* hs, cint n, const double* data, cint stride) {
    ensureJoined();
    if (kind < 0 || kind >= KindCount) return 0;
    for (cint i = 0; i < n; ++i) {
        EntityId e = idOf(int32_t(hs[i]));
        if (e == InvalidEntityId) continue;
        writeKind(int32_t(kind), int32_t(hs[i]), e, data + i * stride, int32_t(stride), "");
        if (pullable(int32_t(kind))) g.interest[kind].insert(int32_t(hs[i]));
    }
    return 0;
}

// Adds what the script changed to the Transform2D of many entities: five
// doubles each (x, y, rotation, scale x, scale y), so the change composes
// with what the simulation did to the same entity meanwhile.
EXPORT cint dc_move_many(const cint* hs, cint n, const double* delta) {
    ensureJoined();
    for (cint i = 0; i < n; ++i) {
        EntityId e = idOf(int32_t(hs[i]));
        if (e == InvalidEntityId) continue;
        const double* d = delta + i * 5;
        Transform2D t;
        if (const Transform2D* cur = get<Transform2D>(e)) t = *cur;
        t.position += {float(d[0]), float(d[1])};
        t.rotation += float(d[2]);
        t.scale += {float(d[3]), float(d[4])};
        put<Transform2D>(e, t);
        teleportBody(e, t.position, t.rotation);
        g.interest[KTransform].insert(int32_t(hs[i]));
    }
    return 0;
}

EXPORT cint dc_remove(cint kind, cint h) {
    ensureJoined();
    EntityId e = idOf(int32_t(h));
    if (e == InvalidEntityId || kind < 0 || kind >= KindCount) return 0;
    removeKind(int32_t(kind), int32_t(h), e);
    return 0;
}

// Reads the engine-driven fields of a kind for many entities into `out`
// (pullStride doubles each), from the snapshot taken when the simulation
// started. An entity without the component gets Missing (-1e300) in its
// first slot.
EXPORT cint dc_pull(cint kind, const cint* hs, cint n, double* out) {
    if (!g.app || kind < 0 || kind >= KindCount) return 0;
    int32_t stride = pullStride(int32_t(kind));
    if (!stride) return 0;
    auto& snap = g.snaps[kind];
    for (cint i = 0; i < n; ++i) {
        int32_t h = int32_t(hs[i]);
        double* o = out + i * stride;
        g.interest[kind].insert(h);
        auto it = snap.rows.find(h);
        if (it != snap.rows.end()) {
            std::copy(it->second.begin(), it->second.begin() + stride, o);
            continue;
        }
        ensureJoined();
        EntityId e = idOf(h);
        double row[8];
        if (e != InvalidEntityId && readPull(int32_t(kind), e, row)) std::copy(row, row + stride, o);
        else o[0] = Missing;
    }
    return 0;
}

// Collision and sensor events of the last simulation. kind: 0 collision
// started, 1 collision ended, 2 sensor entered (sensor, visitor), 3 sensor
// exited. Pairs of handles; the count is of pairs.
EXPORT cint dc_event_count(cint kind) {
    return kind >= 0 && kind < 4 ? cint(g.events[kind].size() / 2) : 0;
}
EXPORT cint dc_events(cint kind, cint* out, cint n) {
    if (kind < 0 || kind >= 4) return 0;
    const auto& ev = g.events[kind];
    for (cint i = 0; i < n && i < cint(ev.size()); ++i) out[i] = ev[size_t(i)];
    return 0;
}

// ---- Entities made by the engine (scenes) ----

// Bit N set: the entity has kind N, for the kinds dc_get reads.
EXPORT cint dc_kinds(cint h) {
    ensureJoined();
    EntityId e = idOf(int32_t(h));
    if (e == InvalidEntityId) return 0;
    cint m = 0;
    auto bit = [&](int k, bool has) { if (has) m |= cint(1) << k; };
    bit(KTransform, get<Transform2D>(e));
    bit(KVelocity, get<Velocity2D>(e) && !get<RigidBody2D>(e));
    bit(KSprite, get<Sprite>(e));
    bit(KShape, get<Shape2D>(e));
    bit(KText, get<Text>(e));
    bit(KCamera, get<Camera>(e));
    bit(KRigidBody, get<RigidBody2D>(e));
    bit(KBoxCollider, get<BoxCollider2D>(e));
    bit(KCircleCollider, get<CircleCollider2D>(e));
    bit(KPointLight, get<PointLight2D>(e));
    bit(KSpotLight, get<SpotLight2D>(e));
    bit(KOccluder, get<LightOccluder2D>(e));
    bit(KLifetime, get<Lifetime>(e));
    bit(KName, get<Name>(e));
    return m;
}

// Reads a whole component in its layout (the inverse of dc_set). Returns
// the number of doubles written, 0 when the entity lacks it.
EXPORT cint dc_get(cint kind, cint h, double* d, cint n) {
    ensureJoined();
    EntityId e = idOf(int32_t(h));
    if (e == InvalidEntityId) return 0;
    auto need = [&](cint k) { return n >= k; };
    switch (kind) {
        case KTransform: case KVelocity: case KLifetime: {
            double row[8];
            if (!readPull(int32_t(kind), e, row)) return 0;
            cint k = pullStride(int32_t(kind));
            if (!need(k)) return 0;
            std::copy(row, row + k, d);
            return k;
        }
        case KSprite: {
            const Sprite* s = get<Sprite>(e);
            if (!s || !need(17)) return 0;
            Vec2 size = {s->srcRect.w, s->srcRect.h};
            if (size.x <= 0)
                if (auto* r = res<RendererResource>()) size = r->textureSize(s->texture);
            DColor c = dcolor(s->tint);
            double v[17] = {double(s->texture.id), s->srcRect.x, s->srcRect.y, s->srcRect.w, s->srcRect.h,
                            size.x > 0 ? s->origin.x / size.x : 0.5, size.y > 0 ? s->origin.y / size.y : 0.5,
                            c.r, c.g, c.b, c.a, double(int(s->flip)), s->zOrder, double(s->layer),
                            s->visible ? 1.0 : 0.0, double(int(s->blend)), 7};
            std::copy(v, v + 17, d);
            return 17;
        }
        case KShape: {
            const Shape2D* s = get<Shape2D>(e);
            if (!s || !need(15)) return 0;
            DColor c = dcolor(s->color);
            double v[15] = {s->kind == ShapeKind::Circle ? 1.0 : 0.0, s->size.x, s->size.y, s->radius,
                            c.r, c.g, c.b, c.a, s->outline, s->cornerRadius, s->anchor.x, s->anchor.y,
                            s->zOrder, double(s->layer), s->visible ? 1.0 : 0.0};
            std::copy(v, v + 15, d);
            return 15;
        }
        case KText: {
            const Text* t = get<Text>(e);
            if (!t || !need(19)) return 0;
            DColor c = dcolor(t->color), o = dcolor(t->outlineColor);
            double v[19] = {t->size, c.r, c.g, c.b, c.a, double(int(t->align)), double(int(t->valign)),
                            t->wrapWidth, t->lineSpacing, t->outlineWidth, o.r, o.g, o.b, o.a,
                            t->zOrder, double(t->layer), t->visible ? 1.0 : 0.0, double(t->font.id), 0};
            std::copy(v, v + 19, d);
            return 19;
        }
        case KCamera: {
            const Camera* c = get<Camera>(e);
            if (!c || !need(8)) return 0;
            double v[8] = {c->zoom, c->rotation, c->active ? 1.0 : 0.0, double(c->order),
                           c->viewport.x, c->viewport.y, c->viewport.w, c->viewport.h};
            std::copy(v, v + 8, d);
            return 8;
        }
        case KRigidBody: {
            const RigidBody2D* b = get<RigidBody2D>(e);
            if (!b || !need(5)) return 0;
            double v[5] = {double(int(b->type)), b->fixedRotation ? 1.0 : 0.0, b->gravityScale,
                           b->linearDamping, b->angularDamping};
            std::copy(v, v + 5, d);
            return 5;
        }
        case KBoxCollider: {
            const BoxCollider2D* c = get<BoxCollider2D>(e);
            if (!c || !need(10)) return 0;
            double v[10] = {c->halfSize.x, c->halfSize.y, c->offset.x, c->offset.y, c->isSensor ? 1.0 : 0.0,
                            c->density, c->friction, c->restitution, double(c->filter.category), double(c->filter.mask)};
            std::copy(v, v + 10, d);
            return 10;
        }
        case KCircleCollider: {
            const CircleCollider2D* c = get<CircleCollider2D>(e);
            if (!c || !need(9)) return 0;
            double v[9] = {c->radius, c->offset.x, c->offset.y, c->isSensor ? 1.0 : 0.0,
                           c->density, c->friction, c->restitution, double(c->filter.category), double(c->filter.mask)};
            std::copy(v, v + 9, d);
            return 9;
        }
        case KPointLight: {
            const PointLight2D* l = get<PointLight2D>(e);
            if (!l || !need(7)) return 0;
            double v[7] = {l->color.r, l->color.g, l->color.b, l->intensity, l->radius, l->falloff,
                           l->castShadows ? 1.0 : 0.0};
            std::copy(v, v + 7, d);
            return 7;
        }
        case KSpotLight: {
            const SpotLight2D* l = get<SpotLight2D>(e);
            if (!l || !need(10)) return 0;
            double v[10] = {l->color.r, l->color.g, l->color.b, l->intensity, l->radius, l->falloff,
                            l->direction, l->angle, l->softness, l->castShadows ? 1.0 : 0.0};
            std::copy(v, v + 10, d);
            return 10;
        }
        case KName: return get<Name>(e) ? 1 : 0;   // the name is dc_get_string's
        case KOccluder: {
            const LightOccluder2D* o = get<LightOccluder2D>(e);
            if (!o || !need(4)) return 0;
            double v[4] = {double(int(o->shape)), o->halfSize.x, o->halfSize.y, o->radius};
            std::copy(v, v + 4, d);
            return 4;
        }
    }
    return 0;
}

// The string field of a kind: Text text, Name name, Sprite image path.
EXPORT const char* dc_get_string(cint kind, cint h) {
    ensureJoined();
    EntityId e = idOf(int32_t(h));
    if (e == InvalidEntityId) return keep("");
    switch (kind) {
        case KText: { const Text* t = get<Text>(e); return keep(t ? std::string(t->str()) : ""); }
        case KName: { const Name* nm = get<Name>(e); return keep(nm ? nm->value : ""); }
        case KSprite: {
            const Sprite* s = get<Sprite>(e);
            auto* assets = res<AssetServer>();
            if (!s || !assets) return keep("");
            return keep(assets->pathOf(s->texture));
        }
    }
    return keep("");
}

// ---- Physics ----

EXPORT cint dc_apply_impulse(cint h, DV2 impulse) {
    ensureJoined();
    EntityId e = idOf(int32_t(h));
    const PhysicsBody* b = e != InvalidEntityId ? bodyOf(e) : nullptr;
    auto* physics = res<PhysicsResource>();
    if (!b || !physics) return 0;
    physics->applyImpulse(*b, v2(impulse));
    // The ECS velocity is pushed back into Box2D before each step; keep it in sync.
    Vec2 vel = physics->getBodyVelocity(*b);
    if (Velocity2D* v = get<Velocity2D>(e)) {
        Velocity2D nv = *v;
        nv.linear = vel;
        put<Velocity2D>(e, nv);
    }
    return 0;
}
EXPORT cint dc_apply_force(cint h, DV2 force) {
    ensureJoined();
    EntityId e = idOf(int32_t(h));
    const PhysicsBody* b = e != InvalidEntityId ? bodyOf(e) : nullptr;
    auto* physics = res<PhysicsResource>();
    if (b && physics) physics->applyForce(*b, v2(force));
    return 0;
}
EXPORT cint dc_set_gravity(DV2 g2) { ensureJoined(); if (auto* p = res<PhysicsResource>()) p->setGravity(v2(g2)); return 0; }
EXPORT DV2 dc_gravity() { ensureJoined(); auto* p = res<PhysicsResource>(); return p ? dv(p->gravity()) : DV2{0, 0}; }

EXPORT DHit dc_raycast(DV2 origin, DV2 direction, double distance, cint mask) {
    ensureJoined();
    DHit out{0, {0, 0}, {0, 0}, 0};
    auto* p = res<PhysicsResource>();
    if (!p) return out;
    CollisionFilter f;
    f.mask = ubits(mask);
    RaycastHit hit = p->raycast(v2(origin), v2(direction), float(distance), f);
    out.hit = hit.hit ? 1 : 0;
    out.point = dv(hit.point);
    out.normal = dv(hit.normal);
    out.fraction = hit.fraction;
    return out;
}

EXPORT cint dc_camera_shake(cint h, double intensity, double decay, double frequency) {
    ensureJoined();
    EntityId e = idOf(int32_t(h));
    if (e == InvalidEntityId) return 0;
    CameraShake s;
    if (const CameraShake* cur = get<CameraShake>(e)) s = *cur;
    s.decay = float(decay);
    s.frequency = float(frequency);
    s.trigger(float(intensity));
    put<CameraShake>(e, s);
    return 0;
}

// ---- Particles ----

// A one-off burst of `count` particles at `pos` with an emitter layout,
// cleaned up when it fades.
EXPORT cint dc_burst(DV2 pos, const double* d, cint n, cint count) {
    World* w = world();
    if (!w || n < EmitterSize) return 0;
    ensureJoined();
    EntityId e = w->createEntity();
    Transform2D t;
    t.position = v2(pos);
    put<Transform2D>(e, t);
    ParticleEmitter pe;
    pe.config = emitterConfig(d);
    pe.config.spawnRate = 0.f;
    pe.config.looping = false;
    pe.config.duration = 0.05f;
    pe.config.maxParticles = std::max<int32_t>(pe.config.maxParticles, int32_t(count));
    pe.config.bursts.push_back(BurstEntry{0.f, int32_t(count), 1, 0.f});
    pe.oneShot = true;
    put<ParticleEmitter>(e, pe);
    g.bursts.push_back({e, g.elapsed + d[2] + 0.2});
    return 0;
}

// ---- Lighting ----

EXPORT cint dc_set_ambient(DColor c) {
    auto* s = res<LightingSettings>();
    if (!s) return 0;
    s->ambient = colorf(c);
    return 1;
}

// =========================================================================
// Post effects
// =========================================================================

// kind: 0 bloom, 1 vignette, 2 crt, 3 grayscale, 4 sepia, 5 pixelate, 6 blur.
// a and b are the effect's two main parameters (see the binding docs).
EXPORT cint dc_effect_add(cint kind, double a, double b) {
    auto* fx = res<PostEffects>();
    if (!fx) return 0;
    EffectId id;
    switch (kind) {
        case 0: { Bloom e; e.threshold = float(a); e.intensity = float(b); id = fx->add(e); break; }
        case 1: { Vignette e; e.intensity = float(a); e.radius = float(b); id = fx->add(e); break; }
        case 2: { Crt e; e.curvature = float(a); e.scanlines = float(b); id = fx->add(e); break; }
        case 3: { Grayscale e; e.amount = float(a); id = fx->add(e); break; }
        case 4: { Sepia e; e.amount = float(a); id = fx->add(e); break; }
        case 5: { Pixelate e; e.pixelSize = float(a); id = fx->add(e); break; }
        case 6: { Blur e; e.radius = float(a); e.iterations = std::max(1, int(b)); id = fx->add(e); break; }
        default: return 0;
    }
    return id.id;
}
EXPORT cint dc_effect_set_enabled(cint id, cint on) { if (auto* fx = res<PostEffects>()) fx->setEnabled(EffectId{int32_t(id)}, on != 0); return 0; }
EXPORT cint dc_effect_remove(cint id) { if (auto* fx = res<PostEffects>()) fx->remove(EffectId{int32_t(id)}); return 0; }

namespace {
EffectId fadeEffect() {
    auto* fx = res<PostEffects>();
    if (!fx) return {};
    if (!g.fade.valid()) g.fade = fx->add(ScreenFade{});
    return g.fade;
}
}
EXPORT cint dc_fade_to(DColor c, double amount, double seconds) {
    auto* fx = res<PostEffects>();
    EffectId id = fadeEffect();
    if (!fx || !id.valid()) return 0;
    if (auto* f = fx->get<ScreenFade>(id)) f->color = colorf(c);
    fx->fadeTo(id, float(amount), float(seconds));
    return 0;
}
EXPORT cint dc_flash(DColor c, double seconds) {
    auto* fx = res<PostEffects>();
    EffectId id = fadeEffect();
    if (fx && id.valid()) fx->flash(id, colorf(c), float(seconds));
    return 0;
}

// =========================================================================
// Tweens (the Tween component is written through dc_set)
// =========================================================================

EXPORT cint dc_tween_time_scale(double s) { ensureJoined(); if (auto* t = res<Tweens>()) t->timeScale = float(s); return 0; }
EXPORT double dc_ease(cint e, double t) { return easing::apply(ease(int32_t(e)), float(t)); }

// =========================================================================
// Audio
// =========================================================================

EXPORT cint dc_sound_load(const char* path, cint stream) {
    ensureJoined();
    auto* a = res<AudioResource>();
    if (!a) return fail("drift is not running");
    SoundHandle s;
    if (stream) s = a->loadSound(path, LoadMode::Stream);
    else if (auto* assets = res<AssetServer>()) s = assets->loadSound(path);
    if (!s.valid()) return fail(std::string("cannot load sound ") + path);
    return bits(s.id);
}
// samples: interleaved when channels == 2; count is the number of floats.
EXPORT cint dc_sound_create(const double* samples, cint count, cint channels, cint rate) {
    ensureJoined();
    auto* a = res<AudioResource>();
    if (!a) return fail("drift is not running");
    if (channels < 1 || channels > 2 || count < channels) return fail("a sound needs 1 or 2 channels and at least one frame");
    std::vector<float> f(samples, samples + count);
    SoundHandle s = a->createSound(f.data(), size_t(count / channels), channels, rate);
    if (!s.valid()) return fail("cannot create sound");
    return bits(s.id);
}
EXPORT double dc_sound_duration(cint s) { ensureJoined(); auto* a = res<AudioResource>(); return a ? a->soundDuration(handle<SoundHandle>(s)) : 0.0; }
EXPORT cint dc_sound_destroy(cint s) { ensureJoined(); if (auto* a = res<AudioResource>()) a->destroySound(handle<SoundHandle>(s)); return 0; }
EXPORT cint dc_sound_max_instances(cint s, cint n) { ensureJoined(); if (auto* a = res<AudioResource>()) a->setSoundMaxInstances(handle<SoundHandle>(s), n); return 0; }
EXPORT cint dc_sound_play(cint s, DPlay p) {
    ensureJoined();
    auto* a = res<AudioResource>();
    if (!a) return 0;
    PlayParams pp;
    pp.bus = handle<BusHandle>(p.bus);
    pp.volume = float(p.volume);
    pp.pan = float(p.pan);
    pp.pitch = float(p.pitch);
    pp.loop = p.loop != 0;
    pp.fadeIn = float(p.fadeIn);
    pp.priority = p.priority;
    return bits(a->playSound(handle<SoundHandle>(s), pp).id);
}
EXPORT cint dc_voice_stop(cint v, double fade) { ensureJoined(); if (auto* a = res<AudioResource>()) a->stopSound(handle<PlayingSoundHandle>(v), float(fade)); return 0; }
EXPORT cint dc_voice_pause(cint v, double fade) { ensureJoined(); if (auto* a = res<AudioResource>()) a->pauseSound(handle<PlayingSoundHandle>(v), float(fade)); return 0; }
EXPORT cint dc_voice_resume(cint v, double fade) { ensureJoined(); if (auto* a = res<AudioResource>()) a->resumeSound(handle<PlayingSoundHandle>(v), float(fade)); return 0; }
EXPORT cint dc_voice_set_volume(cint v, double vol, double fade) { ensureJoined(); if (auto* a = res<AudioResource>()) a->setSoundVolume(handle<PlayingSoundHandle>(v), float(vol), float(fade)); return 0; }
EXPORT cint dc_voice_set_pan(cint v, double pan) { ensureJoined(); if (auto* a = res<AudioResource>()) a->setSoundPan(handle<PlayingSoundHandle>(v), float(pan)); return 0; }
EXPORT cint dc_voice_set_pitch(cint v, double p) { ensureJoined(); if (auto* a = res<AudioResource>()) a->setSoundPitch(handle<PlayingSoundHandle>(v), float(p)); return 0; }
EXPORT cint dc_voice_playing(cint v) { ensureJoined(); auto* a = res<AudioResource>(); return a && a->isSoundPlaying(handle<PlayingSoundHandle>(v)) ? 1 : 0; }

EXPORT cint dc_bus(cint which) {
    ensureJoined();
    auto* a = res<AudioResource>();
    if (!a) return 0;
    switch (which) {
        case 0: return bits(a->masterBus().id);
        case 1: return bits(a->musicBus().id);
        default: return bits(a->sfxBus().id);
    }
}
EXPORT cint dc_bus_create(const char* name, cint parent) {
    ensureJoined();
    auto* a = res<AudioResource>();
    return a ? bits(a->createBus(name, handle<BusHandle>(parent)).id) : 0;
}
EXPORT cint dc_bus_set_volume(cint b, double v, double fade) { ensureJoined(); if (auto* a = res<AudioResource>()) a->setBusVolume(handle<BusHandle>(b), float(v), float(fade)); return 0; }
EXPORT double dc_bus_volume(cint b) { ensureJoined(); auto* a = res<AudioResource>(); return a ? a->busVolume(handle<BusHandle>(b)) : 0.0; }
EXPORT cint dc_bus_set_muted(cint b, cint m) { ensureJoined(); if (auto* a = res<AudioResource>()) a->setBusMuted(handle<BusHandle>(b), m != 0); return 0; }
EXPORT cint dc_bus_set_paused(cint b, cint p) { ensureJoined(); if (auto* a = res<AudioResource>()) a->setBusPaused(handle<BusHandle>(b), p != 0); return 0; }
EXPORT cint dc_bus_stop(cint b, double fade) { ensureJoined(); if (auto* a = res<AudioResource>()) a->stopBus(handle<BusHandle>(b), float(fade)); return 0; }

EXPORT cint dc_music_play(cint s, double crossfade, double volume, cint loop) {
    ensureJoined();
    auto* a = res<AudioResource>();
    if (!a) return 0;
    MusicParams p;
    p.crossfade = float(crossfade);
    p.volume = float(volume);
    p.loop = loop != 0;
    return a->playMusic(handle<SoundHandle>(s), p) ? 1 : 0;
}
EXPORT cint dc_music_stop(double fade) { ensureJoined(); if (auto* a = res<AudioResource>()) a->stopMusic(float(fade)); return 0; }
EXPORT cint dc_music_pause(double fade) { ensureJoined(); if (auto* a = res<AudioResource>()) a->pauseMusic(float(fade)); return 0; }
EXPORT cint dc_music_resume(double fade) { ensureJoined(); if (auto* a = res<AudioResource>()) a->resumeMusic(float(fade)); return 0; }
EXPORT cint dc_music_playing() { ensureJoined(); auto* a = res<AudioResource>(); return a && a->isMusicPlaying() ? 1 : 0; }
EXPORT cint dc_audio_has_device() { ensureJoined(); auto* a = res<AudioResource>(); return a && a->hasDevice() ? 1 : 0; }

// =========================================================================
// Save data
// =========================================================================

namespace {
SaveData* save(int32_t h) {
    if (h <= 0 || h > int32_t(g.saves.size())) return nullptr;
    return g.saves[size_t(h - 1)].get();
}
int32_t addSave(std::unique_ptr<SaveData> s) {
    g.saves.push_back(std::move(s));
    return int32_t(g.saves.size());
}
}

EXPORT cint dc_save_open(const char* org, const char* app, const char* slot) {
    auto s = std::make_unique<SaveData>();
    if (!s->open(org, app, slot)) return fail(std::string("cannot open save slot ") + slot);
    return addSave(std::move(s));
}
EXPORT cint dc_save_open_file(const char* path) {
    auto s = std::make_unique<SaveData>();
    if (!s->openFile(path)) return fail(std::string("cannot open save file ") + path);
    return addSave(std::move(s));
}
EXPORT cint dc_save_set_int(cint h, const char* k, cint v) { if (auto* s = save(h)) s->set(k, v); return 0; }
EXPORT cint dc_save_set_float(cint h, const char* k, double v) { if (auto* s = save(h)) s->set(k, v); return 0; }
EXPORT cint dc_save_set_string(cint h, const char* k, const char* v) { if (auto* s = save(h)) s->set(k, std::string(v)); return 0; }
EXPORT cint dc_save_get_int(cint h, const char* k, cint fallback) {
    auto* s = save(h);
    return s ? s->get<int32_t>(k, fallback) : fallback;
}
EXPORT double dc_save_get_float(cint h, const char* k, double fallback) {
    auto* s = save(h);
    return s ? s->get<double>(k, fallback) : fallback;
}
EXPORT const char* dc_save_get_string(cint h, const char* k, const char* fallback) {
    auto* s = save(h);
    return keep(s ? s->get<std::string>(k, fallback) : std::string(fallback));
}
EXPORT cint dc_save_has(cint h, const char* k) { auto* s = save(h); return s && s->has(k) ? 1 : 0; }
EXPORT cint dc_save_erase(cint h, const char* k) { if (auto* s = save(h)) s->erase(k); return 0; }
EXPORT cint dc_save_clear(cint h) { if (auto* s = save(h)) s->clear(); return 0; }
EXPORT cint dc_save_flush(cint h) { auto* s = save(h); return s && s->flush() ? 1 : 0; }
EXPORT cint dc_save_load(cint h) { auto* s = save(h); return s && s->load() ? 1 : 0; }
EXPORT const char* dc_save_path(cint h) { auto* s = save(h); return keep(s ? s->path().string() : ""); }
EXPORT const char* dc_save_json(cint h) { auto* s = save(h); return keep(s ? s->json().dump() : "{}"); }

// =========================================================================
// Tilemaps
// =========================================================================

EXPORT cint dc_tilemap_load(const char* path, const char* level) {
    auto* t = res<TilemapResource>();
    if (!t) return fail("drift is not running");
    TilemapHandle m = t->loadTilemap(path, level && *level ? level : nullptr);
    if (!m.valid()) return fail(std::string("cannot load tilemap ") + path);
    return bits(m.id);
}
namespace { TilemapResource* maps() { return res<TilemapResource>(); } }
EXPORT DCell dc_tilemap_size(cint m) {
    auto* t = maps();
    auto h = handle<TilemapHandle>(m);
    return t ? DCell{t->mapWidth(h), t->mapHeight(h)} : DCell{0, 0};
}
EXPORT DCell dc_tilemap_tile_size(cint m) {
    auto* t = maps();
    auto h = handle<TilemapHandle>(m);
    return t ? DCell{t->tileWidth(h), t->tileHeight(h)} : DCell{0, 0};
}
EXPORT DRect dc_tilemap_bounds(cint m) { auto* t = maps(); return t ? drect(t->bounds(handle<TilemapHandle>(m))) : DRect{0, 0, 0, 0}; }
EXPORT cint dc_tilemap_layer_count(cint m) { auto* t = maps(); return t ? t->layerCount(handle<TilemapHandle>(m)) : 0; }
EXPORT cint dc_tilemap_find_layer(cint m, const char* name) { auto* t = maps(); return t ? t->findLayer(handle<TilemapHandle>(m), name) : -1; }
EXPORT const char* dc_tilemap_layer_name(cint m, cint l) {
    auto* t = maps();
    const char* n = t ? t->layerName(handle<TilemapHandle>(m), l) : nullptr;
    return keep(n ? n : "");
}
EXPORT cint dc_tilemap_layer_visible(cint m, cint l, cint v) { if (auto* t = maps()) t->setLayerVisible(handle<TilemapHandle>(m), l, v != 0); return 0; }
EXPORT cint dc_tilemap_layer_collision(cint m, cint l, cint solid, const cint* values, cint count) {
    auto* t = maps();
    if (!t) return 0;
    std::vector<int32_t> v(values, values + std::max<cint>(count, 0));
    t->setLayerCollision(handle<TilemapHandle>(m), l, solid != 0, v);
    return 0;
}
EXPORT cint dc_tilemap_tile(cint m, cint l, cint x, cint y) { auto* t = maps(); return t ? bits(t->getTile(handle<TilemapHandle>(m), l, x, y)) : 0; }
EXPORT cint dc_tilemap_set_tile(cint m, cint l, cint x, cint y, cint tile) { if (auto* t = maps()) t->setTile(handle<TilemapHandle>(m), l, x, y, ubits(tile)); return 0; }
EXPORT cint dc_tilemap_int_grid(cint m, cint l, cint x, cint y) { auto* t = maps(); return t ? t->getIntGrid(handle<TilemapHandle>(m), l, x, y) : 0; }
EXPORT cint dc_tilemap_set_int_grid(cint m, cint l, cint x, cint y, cint v) { if (auto* t = maps()) t->setIntGrid(handle<TilemapHandle>(m), l, x, y, v); return 0; }
EXPORT DCell dc_tilemap_world_to_tile(cint m, DV2 p, cint layer) {
    auto* t = maps();
    if (!t) return {0, 0};
    TileCoord c = t->worldToTile(handle<TilemapHandle>(m), v2(p), layer);
    return {c.x, c.y};
}
EXPORT DV2 dc_tilemap_tile_to_world(cint m, DCell c, cint layer) {
    auto* t = maps();
    return t ? dv(t->tileToWorld(handle<TilemapHandle>(m), TileCoord{int32_t(c.x), int32_t(c.y)}, layer)) : DV2{0, 0};
}
EXPORT cint dc_tilemap_tile_at(cint m, cint l, DV2 p) { auto* t = maps(); return t ? bits(t->tileAt(handle<TilemapHandle>(m), l, v2(p))) : 0; }
EXPORT const char* dc_tilemap_property(cint m, const char* name) {
    auto* t = maps();
    return keep(t ? t->properties(handle<TilemapHandle>(m)).getString(name) : "");
}

// Objects of a type ("" = all), cached for the dc_tilemap_object_* readers.
EXPORT cint dc_tilemap_objects(cint m, const char* type) {
    auto* t = maps();
    g.objects.clear();
    if (!t) return 0;
    g.objectsMap = handle<TilemapHandle>(m);
    std::string_view ty = type ? type : "";
    if (!ty.empty()) {
        g.objects = t->findObjects(g.objectsMap, ty);
    } else {
        for (int32_t l = 0; l < t->layerCount(g.objectsMap); ++l)
            for (const auto& o : t->objects(g.objectsMap, l)) g.objects.push_back(&o);
    }
    return int32_t(g.objects.size());
}
namespace {
const MapObject* object(int32_t i) { return i >= 0 && i < int32_t(g.objects.size()) ? g.objects[size_t(i)] : nullptr; }
}
EXPORT const char* dc_tilemap_object_name(cint i) { auto* o = object(i); return keep(o ? o->name : ""); }
EXPORT const char* dc_tilemap_object_type(cint i) { auto* o = object(i); return keep(o ? o->type : ""); }
EXPORT DRect dc_tilemap_object_rect(cint i) {
    auto* o = object(i);
    return o ? DRect{o->position.x, o->position.y, o->size.x, o->size.y} : DRect{0, 0, 0, 0};
}
EXPORT DV2 dc_tilemap_object_anchor(cint i) { auto* o = object(i); return o ? dv(o->anchor()) : DV2{0, 0}; }
EXPORT const char* dc_tilemap_object_property(cint i, const char* name) {
    auto* o = object(i);
    return keep(o ? o->properties.getString(name) : "");
}

// =========================================================================
// Scenes and prefabs
// =========================================================================

EXPORT cint dc_scene_load(const char* path) {
    auto* s = res<SceneResource>();
    if (!s) return fail("drift is not running");
    SceneHandle h = s->load(path);
    if (!h.valid()) return fail(std::string("cannot load scene ") + path);
    return bits(h.id);
}
EXPORT cint dc_scene_spawn(cint scene, DV2 offset, cint parent, const char* overrides) {
    auto* s = res<SceneResource>();
    if (!s) return fail("drift is not running");
    ensureJoined();
    SceneSpawnOptions o;
    o.offset = v2(offset);
    o.parent = parent ? idOf(int32_t(parent)) : InvalidEntityId;
    if (overrides && *overrides) {
        o.overrides = nlohmann::json::parse(overrides, nullptr, false);
        if (o.overrides.is_discarded()) return fail("overrides are not valid JSON");
    }
    SceneInstance inst = s->spawnNow(handle<SceneHandle>(scene), o);
    g.scenes.push_back(std::move(inst));
    return cint(g.scenes.size());
}
namespace {
const SceneInstance* instance(cint i) { return i > 0 && i <= cint(g.scenes.size()) ? &g.scenes[size_t(i - 1)] : nullptr; }
}
EXPORT cint dc_scene_count(cint inst, cint roots) {
    auto* in = instance(inst);
    return in ? cint(roots ? in->roots.size() : in->entities.size()) : 0;
}
EXPORT cint dc_scene_entity(cint inst, cint roots, cint i) {
    auto* in = instance(inst);
    if (!in) return 0;
    const auto& list = roots ? in->roots : in->entities;
    return i >= 0 && i < cint(list.size()) ? handleOf(list[size_t(i)]) : 0;
}
EXPORT cint dc_scene_save(const char* path) {
    ensureJoined();
    auto* s = res<SceneResource>();
    return s && s->save(path) ? 1 : 0;
}
