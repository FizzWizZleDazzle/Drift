// drift_candela: the C side of the candela `drift` package.
//
// candela's foreign interface carries int32, double, null-terminated
// strings, packed int/float arrays and structs of those, and nothing that
// could hold a pointer or call back into the script. So the candela program
// owns the main loop and this file owns one drift::App: dc_frame() closes the
// previous frame (render and present) and opens the next one (events,
// update, physics), and every other call reads or changes the world in
// between, from the same thread.
//
// Entities are 64-bit flecs ids; they cross as small int handles from a
// table here. Engine handles (textures, sounds, fonts, ...) are already
// 32-bit and cross as they are. 0 is never a valid handle. Strings returned
// to candela are copied out by the caller before the next call, so one
// static buffer serves every string-returning function.

#include <drift/App.hpp>
#include <drift/AssetServer.hpp>
#include <drift/CollisionBridge.hpp>
#include <drift/Commands.hpp>
#include <drift/PhysicsSyncSystem.hpp>
#include <drift/Reflect.hpp>
#include <drift/Scene.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/CameraController.hpp>
#include <drift/components/Hierarchy.hpp>
#include <drift/components/Light2D.hpp>
#include <drift/components/Name.hpp>
#include <drift/components/ParticleEmitter.hpp>
#include <drift/components/Physics.hpp>
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
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif

using namespace drift;

// ---- Structs that cross the boundary (doubles, int32_t, nested) ----------

// candela 0.1.6 passes its int as int64_t, in arguments, returns and
// struct fields alike.
using cint = int64_t;

struct DV2 { double x, y; };
struct DRect { double x, y, w, h; };
struct DColor { double r, g, b, a; };
struct DCell { cint x, y; };

struct DCollider {
    DV2 offset;
    double density, friction, restitution;
    cint sensor, category, mask;
};

struct DBody {
    cint kind;            // BodyType
    cint fixedRotation;
    double gravityScale, linearDamping, angularDamping;
};

struct DEmitter {
    double rate;
    double lifetimeMin, lifetimeMax;
    double speedMin, speedMax;
    double angleMin, angleMax;
    double sizeStart, sizeEnd;
    DColor colorStart, colorEnd;
    DV2 gravity;
    double drag;
    cint maxParticles;
    cint blend;           // BlendMode
    cint texture;         // 0 = white square
    cint shape;           // EmissionShape
    double shapeRadius;
    DV2 shapeExtents;
    double duration;         // -1 = forever
    cint looping;
    double z;
    cint layer;
};

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
DColor dcolorf(ColorF c) { return {c.r, c.g, c.b, c.a}; }

// Engine handles are 32-bit ids; a candela int holds them unchanged.
cint bits(uint32_t v) { return cint(v); }
uint32_t ubits(cint v) { return uint32_t(v); }

template <typename H> H handle(cint v) { return H{uint32_t(v)}; }

// ---- State ---------------------------------------------------------------

struct Burst {
    EntityId entity;
    double despawnAt;
};

struct State {
    App* app = nullptr;
    bool frameOpen = false;
    std::string error;
    std::string text;               // returned strings live here until the next call

    // Entity handles: never reused, so a stale handle never names a new entity.
    std::unordered_map<int32_t, EntityId> ids;
    std::unordered_map<EntityId, int32_t> handles;
    int32_t nextHandle = 1;

    float drawZ = 0.f;
    std::vector<int32_t> query;     // last dc_query result

    std::unordered_map<int32_t, std::unique_ptr<TweenBuilder>> tweenBuilders;
    int32_t nextBuilder = 1;

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

// Copy, edit, write back, so change ticks and hooks see the change.
template <typename T, typename Fn> void edit(int32_t h, Fn&& fn) {
    EntityId e = idOf(h);
    if (e == InvalidEntityId) return;
    T value{};
    if (const T* cur = get<T>(e)) value = *cur;
    fn(value);
    put<T>(e, value);
}

template <typename T> void removeComponent(int32_t h) {
    EntityId e = idOf(h);
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

void teleportBody(EntityId e, Vec2 position) {
    auto* bridge = res<PhysicsBridge>();
    auto* physics = res<PhysicsResource>();
    if (!bridge || !physics) return;
    auto it = bridge->entityToBody.find(e);
    if (it == bridge->entityToBody.end()) return;
    Vec2 offset{};
    if (auto* box = get<BoxCollider2D>(e)) offset = box->offset;
    physics->setBodyPosition(it->second, position + offset);
}

const PhysicsBody* bodyOf(EntityId e) {
    auto* bridge = res<PhysicsBridge>();
    if (!bridge) return nullptr;
    auto it = bridge->entityToBody.find(e);
    return it == bridge->entityToBody.end() ? nullptr : &it->second;
}

EmitterConfig emitterConfig(const DEmitter& d) {
    EmitterConfig c;
    c.texture = handle<TextureHandle>(d.texture);
    if (!c.texture.valid()) {
        if (auto* r = res<RendererResource>()) c.texture = r->whiteTexture();
    }
    c.spawnRate = float(d.rate);
    c.lifetime = {float(d.lifetimeMin), float(d.lifetimeMax)};
    c.speed = {float(d.speedMin), float(d.speedMax)};
    c.angle = {float(d.angleMin), float(d.angleMax)};
    c.sizeStart = {float(d.sizeStart), float(d.sizeStart)};
    c.sizeEnd = {float(d.sizeEnd), float(d.sizeEnd)};
    c.colorStart = colorf(d.colorStart);
    c.colorEnd = colorf(d.colorEnd);
    c.gravity = v2(d.gravity);
    c.drag = float(d.drag);
    c.maxParticles = d.maxParticles > 0 ? d.maxParticles : 256;
    c.blendMode = static_cast<BlendMode>(d.blend);
    c.shape = static_cast<EmissionShape>(d.shape);
    c.shapeRadius = float(d.shapeRadius);
    c.shapeExtents = v2(d.shapeExtents);
    c.duration = float(d.duration);
    c.looping = d.looping != 0;
    c.zOrder = float(d.z);
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

PhysicsBody* bodyFor(int32_t h) {
    EntityId e = idOf(h);
    if (e == InvalidEntityId) return nullptr;
    return const_cast<PhysicsBody*>(bodyOf(e));
}

void reapBursts() {
    auto* time = res<Time>();
    World* w = world();
    if (!time || !w) return;
    double now = time->elapsed;
    auto& b = g.bursts;
    for (size_t i = 0; i < b.size();) {
        if (now >= b[i].despawnAt) {
            if (w->isAlive(b[i].entity)) w->destroyEntity(b[i].entity);
            auto it = g.handles.find(b[i].entity);
            if (it != g.handles.end()) forget(it->second);
            b[i] = b.back();
            b.pop_back();
        } else {
            ++i;
        }
    }
}

void closeApp() {
    if (!g.app) return;
    if (g.frameOpen) g.app->endFrame();
    for (auto& s : g.saves) {
        if (s && s->dirty()) s->flush();
    }
    g.tweenBuilders.clear();
    delete g.app;
    g.app = nullptr;
    g.frameOpen = false;
    g.ids.clear();
    g.handles.clear();
    g.query.clear();
    g.scenes.clear();
    g.objects.clear();
    g.bursts.clear();
    g.fade = {};
}

} // namespace

// =========================================================================
// App and frame loop
// =========================================================================

EXPORT const char* dc_error() { return g.error.c_str(); }

EXPORT cint dc_start(const char* title, cint width, cint height,
                        cint fullscreen, cint vsync, cint resizable,
                        cint scaleMode, cint lighting, const char* renderer) {
    if (g.app) return fail("drift is already running");
    Config cfg;
    cfg.title = title ? title : "Drift";
    cfg.width = width;
    cfg.height = height;
    cfg.fullscreen = fullscreen != 0;
    cfg.vsync = vsync != 0;
    cfg.resizable = resizable != 0;
    cfg.scaleMode = static_cast<ScaleMode>(scaleMode);
    cfg.renderer = renderer ? renderer : "";

    g.app = new App();
    g.app->setConfig(cfg);
    g.app->addPlugins<DefaultPlugins>();
    if (lighting) g.app->addPlugin<LightingPlugin>();
    if (!g.app->start()) {
        delete g.app;
        g.app = nullptr;
        return fail("could not open a window and graphics device");
    }
    return 1;
}

// Ends the frame opened by the previous call, then opens the next one.
// Returns 0 when the app has quit; the window is closed by then.
EXPORT cint dc_frame() {
    if (!g.app) return 0;
    if (g.frameOpen) {
        g.app->endFrame();
        g.frameOpen = false;
    }
    if (!g.app->beginFrame()) {
        closeApp();
        return 0;
    }
    g.frameOpen = true;
    g.drawZ = 0.f;
    reapBursts();
    return 1;
}

EXPORT cint dc_quit() { if (g.app) g.app->quit(); return 0; }
EXPORT cint dc_close() { closeApp(); return 0; }
EXPORT cint dc_running() { return g.app && g.app->running() ? 1 : 0; }

EXPORT double dc_dt() { auto* t = res<Time>(); return t ? t->delta : 0.0; }
EXPORT double dc_time() { auto* t = res<Time>(); return t ? t->elapsed : 0.0; }
EXPORT cint dc_frame_count() { auto* t = res<Time>(); return t ? int32_t(t->frame) : 0; }

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
EXPORT cint dc_scale_mode() {
    auto* r = res<RendererResource>();
    return r ? int32_t(r->scaleMode()) : 0;
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

EXPORT cint dc_spawn() {
    World* w = world();
    if (!w) return 0;
    EntityId e = w->createEntity();
    w->set<Transform2D>(e, cid<Transform2D>(), Transform2D{});
    return handleOf(e);
}
EXPORT cint dc_despawn(cint h) {
    EntityId e = idOf(h);
    if (e != InvalidEntityId) {
        // Children go with their parent.
        if (auto* ch = get<Children>(e)) {
            Children copy = *ch;
            for (int i = 0; i < copy.count; ++i) {
                auto it = g.handles.find(copy.ids[i]);
                if (it != g.handles.end()) dc_despawn(it->second);
                else if (world()->isAlive(copy.ids[i])) world()->destroyEntity(copy.ids[i]);
            }
        }
        world()->destroyEntity(e);
    }
    forget(h);
    return 0;
}
EXPORT cint dc_alive(cint h) { return idOf(h) != InvalidEntityId ? 1 : 0; }

EXPORT cint dc_set_name(cint h, const char* name) {
    EntityId e = idOf(h);
    if (e != InvalidEntityId) put<Name>(e, Name(name));
    return 0;
}
EXPORT const char* dc_name(cint h) {
    EntityId e = idOf(h);
    const Name* n = e != InvalidEntityId ? get<Name>(e) : nullptr;
    return keep(n ? n->value : "");
}
EXPORT cint dc_find(const char* name) {
    World* w = world();
    if (!w || !name) return 0;
    for (EntityId e : w->entitiesWith(cid<Name>())) {
        const Name* n = get<Name>(e);
        if (n && std::strcmp(n->value, name) == 0) return handleOf(e);
    }
    return 0;
}

// Entities matching a flecs query expression over component names, such as
// "Sprite" or "RigidBody2D, Velocity2D". Returns the count; dc_query_at reads them.
EXPORT cint dc_query(const char* expr) {
    g.query.clear();
    World* w = world();
    if (!w || !expr || !*expr) return 0;
    QueryIter it = w->queryIter(expr);
    if (!it._internal) return -1;
    while (w->queryNext(&it)) {
        EntityId* ents = w->queryEntities(&it);
        for (int32_t i = 0; i < it.count; ++i) g.query.push_back(handleOf(ents[i]));
    }
    w->queryFini(&it);
    return int32_t(g.query.size());
}
EXPORT cint dc_query_at(cint i) {
    return i >= 0 && i < int32_t(g.query.size()) ? g.query[size_t(i)] : 0;
}

EXPORT cint dc_has(cint h, const char* component) {
    EntityId e = idOf(h);
    World* w = world();
    if (e == InvalidEntityId || !w) return 0;
    ComponentId c = w->componentRegistry().getByName(component);
    return c && w->hasComponent(e, c) ? 1 : 0;
}
EXPORT cint dc_remove(cint h, const char* component) {
    EntityId e = idOf(h);
    World* w = world();
    if (e == InvalidEntityId || !w) return 0;
    ComponentId c = w->componentRegistry().getByName(component);
    if (c) w->removeComponent(e, c);
    return 0;
}

// Any reflected component as JSON (the scene file form).
EXPORT const char* dc_component_json(cint h, const char* component) {
    EntityId e = idOf(h);
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
    EntityId e = idOf(h);
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

EXPORT cint dc_set_parent(cint child, cint parent) {
    EntityId c = idOf(child);
    EntityId p = idOf(parent);
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
EXPORT cint dc_parent(cint h) {
    EntityId e = idOf(h);
    const Parent* p = e != InvalidEntityId ? get<Parent>(e) : nullptr;
    return p ? handleOf(p->entity) : 0;
}

// ---- Transform ----

EXPORT cint dc_set_position(cint h, DV2 p) {
    edit<Transform2D>(h, [&](Transform2D& t) { t.position = v2(p); });
    EntityId e = idOf(h);
    if (e != InvalidEntityId) teleportBody(e, v2(p));
    return 0;
}
EXPORT DV2 dc_position(cint h) {
    EntityId e = idOf(h);
    const Transform2D* t = e != InvalidEntityId ? get<Transform2D>(e) : nullptr;
    return t ? dv(t->position) : DV2{0, 0};
}
EXPORT DV2 dc_world_position(cint h) {
    EntityId e = idOf(h);
    if (e == InvalidEntityId) return {0, 0};
    if (const auto* gt = get<GlobalTransform2D>(e)) return dv(gt->position);
    const Transform2D* t = get<Transform2D>(e);
    return t ? dv(t->position) : DV2{0, 0};
}
EXPORT cint dc_set_rotation(cint h, double r) {
    edit<Transform2D>(h, [&](Transform2D& t) { t.rotation = float(r); });
    EntityId e = idOf(h);
    if (const PhysicsBody* b = e != InvalidEntityId ? bodyOf(e) : nullptr)
        res<PhysicsResource>()->setBodyRotation(*b, float(r));
    return 0;
}
EXPORT double dc_rotation(cint h) {
    EntityId e = idOf(h);
    const Transform2D* t = e != InvalidEntityId ? get<Transform2D>(e) : nullptr;
    return t ? t->rotation : 0.0;
}
EXPORT cint dc_set_scale(cint h, DV2 s) {
    edit<Transform2D>(h, [&](Transform2D& t) { t.scale = v2(s); });
    return 0;
}
EXPORT DV2 dc_scale(cint h) {
    EntityId e = idOf(h);
    const Transform2D* t = e != InvalidEntityId ? get<Transform2D>(e) : nullptr;
    return t ? dv(t->scale) : DV2{1, 1};
}

// ---- Sprite ----

EXPORT cint dc_set_sprite(cint h, cint tex) {
    edit<Sprite>(h, [&](Sprite& s) { s.texture = handle<TextureHandle>(tex); });
    return 0;
}
EXPORT cint dc_sprite_texture(cint h) {
    EntityId e = idOf(h);
    const Sprite* s = e != InvalidEntityId ? get<Sprite>(e) : nullptr;
    return s ? bits(s->texture.id) : 0;
}
EXPORT cint dc_sprite_set_rect(cint h, DRect r) { edit<Sprite>(h, [&](Sprite& s) { s.srcRect = rect(r); }); return 0; }
EXPORT DRect dc_sprite_rect(cint h) {
    EntityId e = idOf(h);
    const Sprite* s = e != InvalidEntityId ? get<Sprite>(e) : nullptr;
    return s ? drect(s->srcRect) : DRect{0, 0, 0, 0};
}
EXPORT cint dc_sprite_set_origin(cint h, DV2 o) { edit<Sprite>(h, [&](Sprite& s) { s.origin = v2(o); }); return 0; }
EXPORT cint dc_sprite_set_tint(cint h, DColor c) { edit<Sprite>(h, [&](Sprite& s) { s.tint = color(c); }); return 0; }
EXPORT DColor dc_sprite_tint(cint h) {
    EntityId e = idOf(h);
    const Sprite* s = e != InvalidEntityId ? get<Sprite>(e) : nullptr;
    return s ? dcolor(s->tint) : DColor{1, 1, 1, 1};
}
EXPORT cint dc_sprite_set_flip(cint h, cint flip) { edit<Sprite>(h, [&](Sprite& s) { s.flip = static_cast<Flip>(flip); }); return 0; }
EXPORT cint dc_sprite_set_z(cint h, double z) { edit<Sprite>(h, [&](Sprite& s) { s.zOrder = float(z); }); return 0; }
EXPORT cint dc_sprite_set_layer(cint h, cint l) { edit<Sprite>(h, [&](Sprite& s) { s.layer = uint8_t(std::clamp<cint>(l, 0, 31)); }); return 0; }
EXPORT cint dc_sprite_set_visible(cint h, cint v) { edit<Sprite>(h, [&](Sprite& s) { s.visible = v != 0; }); return 0; }
EXPORT cint dc_sprite_set_blend(cint h, cint b) { edit<Sprite>(h, [&](Sprite& s) { s.blend = static_cast<BlendMode>(b); }); return 0; }
EXPORT cint dc_remove_sprite(cint h) { removeComponent<Sprite>(h); return 0; }

// ---- Sprite animation (SpriteAnimator clips of source rects) ----

// rects: x, y, w, h per frame; count is the number of floats.
EXPORT cint dc_anim_add_clip(cint h, const double* rects, cint count, double frameDuration, cint looping) {
    int32_t index = -1;
    edit<SpriteAnimator>(h, [&](SpriteAnimator& a) {
        AnimationClip clip;
        for (int32_t i = 0; i + 3 < count; i += 4)
            clip.frames.push_back({float(rects[i]), float(rects[i + 1]), float(rects[i + 2]), float(rects[i + 3])});
        clip.frameDuration = float(frameDuration);
        clip.looping = looping != 0;
        a.clips.push_back(std::move(clip));
        index = int32_t(a.clips.size()) - 1;
        if (a.clips.size() == 1) a.play(0);
    });
    EntityId e = idOf(h);
    if (e != InvalidEntityId && !get<Sprite>(e)) put<Sprite>(e, Sprite{});
    return index;
}
EXPORT cint dc_anim_play(cint h, cint clip) { edit<SpriteAnimator>(h, [&](SpriteAnimator& a) { a.play(clip); }); return 0; }
EXPORT cint dc_anim_stop(cint h) { edit<SpriteAnimator>(h, [&](SpriteAnimator& a) { a.stop(); }); return 0; }
EXPORT cint dc_anim_clip(cint h) {
    EntityId e = idOf(h);
    const SpriteAnimator* a = e != InvalidEntityId ? get<SpriteAnimator>(e) : nullptr;
    return a ? a->currentClip : -1;
}
EXPORT cint dc_anim_frame(cint h) {
    EntityId e = idOf(h);
    const SpriteAnimator* a = e != InvalidEntityId ? get<SpriteAnimator>(e) : nullptr;
    return a ? a->currentFrame : 0;
}
EXPORT cint dc_anim_finished(cint h) {
    EntityId e = idOf(h);
    const SpriteAnimator* a = e != InvalidEntityId ? get<SpriteAnimator>(e) : nullptr;
    return !a || a->isFinished() ? 1 : 0;
}

// ---- Text ----

EXPORT cint dc_set_text(cint h, const char* text) { edit<Text>(h, [&](Text& t) { t.set(text ? text : ""); }); return 0; }
EXPORT const char* dc_text(cint h) {
    EntityId e = idOf(h);
    const Text* t = e != InvalidEntityId ? get<Text>(e) : nullptr;
    return keep(t ? std::string(t->str()) : "");
}
EXPORT cint dc_text_style(cint h, cint font, DTextStyle s) {
    edit<Text>(h, [&](Text& t) {
        t.font = handle<FontHandle>(font);
        t.size = float(s.size);
        t.color = color(s.color);
        t.align = static_cast<TextAlign>(s.align);
        t.valign = static_cast<TextVAlign>(s.valign);
        t.wrapWidth = float(s.wrap);
        t.lineSpacing = float(s.lineSpacing);
        t.outlineWidth = float(s.outline);
        t.outlineColor = color(s.outlineColor);
        t.shadowOffset = v2(s.shadow);
        t.shadowColor = color(s.shadowColor);
        t.richText = s.rich != 0;
    });
    return 0;
}
EXPORT cint dc_text_set_color(cint h, DColor c) { edit<Text>(h, [&](Text& t) { t.color = color(c); }); return 0; }
EXPORT cint dc_text_set_size(cint h, double size) { edit<Text>(h, [&](Text& t) { t.size = float(size); }); return 0; }
EXPORT cint dc_text_set_z(cint h, double z) { edit<Text>(h, [&](Text& t) { t.zOrder = float(z); }); return 0; }
EXPORT cint dc_text_set_layer(cint h, cint l) { edit<Text>(h, [&](Text& t) { t.layer = uint8_t(std::clamp<cint>(l, 0, 31)); }); return 0; }
EXPORT cint dc_text_set_visible(cint h, cint v) { edit<Text>(h, [&](Text& t) { t.visible = v != 0; }); return 0; }

// ---- Camera ----

EXPORT cint dc_add_camera(cint h) { edit<Camera>(h, [&](Camera& c) { c.active = true; }); return 0; }
EXPORT cint dc_camera_set_zoom(cint h, double z) { edit<Camera>(h, [&](Camera& c) { c.zoom = float(z); }); return 0; }
EXPORT double dc_camera_zoom(cint h) {
    EntityId e = idOf(h);
    const Camera* c = e != InvalidEntityId ? get<Camera>(e) : nullptr;
    return c ? c->zoom : 1.0;
}
EXPORT cint dc_camera_set_active(cint h, cint a) { edit<Camera>(h, [&](Camera& c) { c.active = a != 0; }); return 0; }
EXPORT cint dc_camera_set_viewport(cint h, DRect r) { edit<Camera>(h, [&](Camera& c) { c.viewport = rect(r); }); return 0; }
EXPORT cint dc_camera_set_order(cint h, cint o) { edit<Camera>(h, [&](Camera& c) { c.order = o; }); return 0; }
EXPORT cint dc_camera_set_layers(cint h, cint mask) { edit<Camera>(h, [&](Camera& c) { c.layerMask = ubits(mask); }); return 0; }
EXPORT cint dc_camera_set_target(cint h, cint tex) { edit<Camera>(h, [&](Camera& c) { c.target = handle<TextureHandle>(tex); }); return 0; }
EXPORT cint dc_camera_set_clear(cint h, cint clear, DColor col) {
    edit<Camera>(h, [&](Camera& c) { c.clear = clear != 0; c.clearColor = colorf(col); });
    return 0;
}
EXPORT cint dc_camera_follow(cint h, cint target, double smoothing, DV2 offset, DV2 deadZone) {
    EntityId t = idOf(target);
    edit<CameraFollow>(h, [&](CameraFollow& f) {
        f.target = t;
        f.smoothing = float(smoothing);
        f.offset = v2(offset);
        f.deadZone = v2(deadZone);
    });
    return 0;
}
EXPORT cint dc_camera_shake(cint h, double intensity, double decay, double frequency) {
    edit<CameraShake>(h, [&](CameraShake& s) {
        s.decay = float(decay);
        s.frequency = float(frequency);
        s.trigger(float(intensity));
    });
    return 0;
}

// ---- Physics ----

EXPORT cint dc_add_body(cint h, DBody b) {
    EntityId e = idOf(h);
    if (e == InvalidEntityId) return 0;
    RigidBody2D rb;
    rb.type = static_cast<BodyType>(b.kind);
    rb.fixedRotation = b.fixedRotation != 0;
    rb.gravityScale = float(b.gravityScale);
    rb.linearDamping = float(b.linearDamping);
    rb.angularDamping = float(b.angularDamping);
    put<RigidBody2D>(e, rb);
    // Velocity2D marks a body Box2D moves; static and kinematic bodies
    // follow their Transform2D instead.
    if (rb.type == BodyType::Dynamic && !get<Velocity2D>(e)) put<Velocity2D>(e, Velocity2D{});
    return 0;
}

namespace {
template <typename T> void fillCollider(T& c, const DCollider& d) {
    c.offset = v2(d.offset);
    c.density = float(d.density);
    c.friction = float(d.friction);
    c.restitution = float(d.restitution);
    c.isSensor = d.sensor != 0;
    c.filter.category = ubits(d.category);
    c.filter.mask = ubits(d.mask);
}
}

EXPORT cint dc_add_box_collider(cint h, DV2 half, DCollider d) {
    EntityId e = idOf(h);
    if (e == InvalidEntityId) return 0;
    BoxCollider2D c;
    c.halfSize = v2(half);
    fillCollider(c, d);
    put<BoxCollider2D>(e, c);
    return 0;
}
EXPORT cint dc_add_circle_collider(cint h, double radius, DCollider d) {
    EntityId e = idOf(h);
    if (e == InvalidEntityId) return 0;
    CircleCollider2D c;
    c.radius = float(radius);
    fillCollider(c, d);
    put<CircleCollider2D>(e, c);
    return 0;
}
EXPORT cint dc_remove_body(cint h) {
    removeComponent<BoxCollider2D>(h);
    removeComponent<CircleCollider2D>(h);
    removeComponent<RigidBody2D>(h);
    removeComponent<Velocity2D>(h);
    return 0;
}
EXPORT cint dc_set_velocity(cint h, DV2 v) {
    edit<Velocity2D>(h, [&](Velocity2D& vel) { vel.linear = v2(v); });
    return 0;
}
EXPORT DV2 dc_velocity(cint h) {
    EntityId e = idOf(h);
    const Velocity2D* v = e != InvalidEntityId ? get<Velocity2D>(e) : nullptr;
    return v ? dv(v->linear) : DV2{0, 0};
}
EXPORT cint dc_apply_impulse(cint h, DV2 impulse) {
    PhysicsBody* b = bodyFor(h);
    auto* physics = res<PhysicsResource>();
    if (!b || !physics) return 0;
    physics->applyImpulse(*b, v2(impulse));
    // The ECS velocity is pushed back into Box2D before each step; keep it in sync.
    EntityId e = idOf(h);
    Vec2 vel = physics->getBodyVelocity(*b);
    if (get<Velocity2D>(e)) edit<Velocity2D>(h, [&](Velocity2D& v) { v.linear = vel; });
    return 0;
}
EXPORT cint dc_apply_force(cint h, DV2 force) {
    PhysicsBody* b = bodyFor(h);
    auto* physics = res<PhysicsResource>();
    if (b && physics) physics->applyForce(*b, v2(force));
    return 0;
}
EXPORT cint dc_set_gravity(DV2 g2) { if (auto* p = res<PhysicsResource>()) p->setGravity(v2(g2)); return 0; }
EXPORT DV2 dc_gravity() { auto* p = res<PhysicsResource>(); return p ? dv(p->gravity()) : DV2{0, 0}; }

// kind: 0 collision start, 1 collision end, 2 sensor start, 3 sensor end.
EXPORT cint dc_contact_count(cint kind) {
    auto* cb = res<CollisionBridge>();
    if (!cb) return 0;
    switch (kind) {
        case 0: return cb->collisionStartCount();
        case 1: return cb->collisionEndCount();
        case 2: return cb->sensorStartCount();
        case 3: return cb->sensorEndCount();
    }
    return 0;
}
EXPORT cint dc_contact_entity(cint kind, cint i, cint which) {
    auto* cb = res<CollisionBridge>();
    if (!cb) return 0;
    EntityId e = InvalidEntityId;
    switch (kind) {
        case 0: e = which ? cb->collisionStartB(i) : cb->collisionStartA(i); break;
        case 1: e = which ? cb->collisionEndB(i) : cb->collisionEndA(i); break;
        case 2: e = which ? cb->sensorStartB(i) : cb->sensorStartA(i); break;
        case 3: e = which ? cb->sensorEndB(i) : cb->sensorEndA(i); break;
    }
    return handleOf(e);
}
EXPORT DHit dc_raycast(DV2 origin, DV2 direction, double distance, cint mask) {
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

// ---- Particles ----

EXPORT cint dc_add_emitter(cint h, DEmitter d) {
    EntityId e = idOf(h);
    if (e == InvalidEntityId) return 0;
    ParticleEmitter pe;
    pe.config = emitterConfig(d);
    put<ParticleEmitter>(e, pe);
    return 0;
}
EXPORT cint dc_emitter_set_playing(cint h, cint playing) {
    edit<ParticleEmitter>(h, [&](ParticleEmitter& p) { p.playing = playing != 0; });
    return 0;
}
EXPORT cint dc_emitter_set_rate(cint h, double rate) {
    edit<ParticleEmitter>(h, [&](ParticleEmitter& p) { p.config.spawnRate = float(rate); });
    return 0;
}
// A one-off burst of `count` particles at `pos`, cleaned up when it fades.
EXPORT cint dc_burst(DV2 pos, DEmitter d, cint count) {
    World* w = world();
    if (!w) return 0;
    EntityId e = w->createEntity();
    Transform2D t;
    t.position = v2(pos);
    put<Transform2D>(e, t);
    ParticleEmitter pe;
    pe.config = emitterConfig(d);
    pe.config.spawnRate = 0.f;
    pe.config.looping = false;
    pe.config.duration = 0.05f;
    pe.config.maxParticles = std::max<uint32_t>(pe.config.maxParticles, uint32_t(count));
    pe.config.bursts.push_back(BurstEntry{0.f, int32_t(count), 1, 0.f});
    pe.oneShot = true;
    put<ParticleEmitter>(e, pe);
    auto* time = res<Time>();
    double now = time ? time->elapsed : 0.0;
    g.bursts.push_back({e, now + d.lifetimeMax + 0.2});
    return 0;
}

// ---- Lights (need Config.lighting) ----

EXPORT cint dc_add_point_light(cint h, DColor c, double intensity, double radius, double falloff, cint shadows) {
    EntityId e = idOf(h);
    if (e == InvalidEntityId) return 0;
    PointLight2D l;
    l.color = colorf(c);
    l.intensity = float(intensity);
    l.radius = float(radius);
    l.falloff = float(falloff);
    l.castShadows = shadows != 0;
    put<PointLight2D>(e, l);
    return 0;
}
EXPORT cint dc_add_spot_light(cint h, DColor c, double intensity, double radius, double falloff,
                              double direction, double angle, double softness, cint shadows) {
    EntityId e = idOf(h);
    if (e == InvalidEntityId) return 0;
    SpotLight2D l;
    l.color = colorf(c);
    l.intensity = float(intensity);
    l.radius = float(radius);
    l.falloff = float(falloff);
    l.direction = float(direction);
    l.angle = float(angle);
    l.softness = float(softness);
    l.castShadows = shadows != 0;
    put<SpotLight2D>(e, l);
    return 0;
}
EXPORT cint dc_light_set_color(cint h, DColor c, double intensity) {
    EntityId e = idOf(h);
    if (e == InvalidEntityId) return 0;
    if (get<PointLight2D>(e)) edit<PointLight2D>(h, [&](PointLight2D& l) { l.color = colorf(c); l.intensity = float(intensity); });
    if (get<SpotLight2D>(e)) edit<SpotLight2D>(h, [&](SpotLight2D& l) { l.color = colorf(c); l.intensity = float(intensity); });
    return 0;
}
// shape: 0 box (half size), 1 circle (radius = size.x), 3 the entity's collider.
EXPORT cint dc_add_occluder(cint h, cint shape, DV2 size) {
    EntityId e = idOf(h);
    if (e == InvalidEntityId) return 0;
    LightOccluder2D o;
    o.shape = static_cast<OccluderShape>(shape);
    o.halfSize = v2(size);
    o.radius = float(size.x);
    put<LightOccluder2D>(e, o);
    return 0;
}
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
// Tweens
// =========================================================================

EXPORT cint dc_tween_begin(cint h) {
    auto* t = res<Tweens>();
    if (!t) return 0;
    EntityId e = h ? idOf(h) : InvalidEntityId;
    int32_t id = g.nextBuilder++;
    g.tweenBuilders[id] = std::make_unique<TweenBuilder>(e != InvalidEntityId ? t->on(e) : t->sequence());
    return id;
}

namespace {
TweenBuilder* builder(int32_t b) {
    auto it = g.tweenBuilders.find(b);
    return it == g.tweenBuilders.end() ? nullptr : it->second.get();
}
Ease ease(int32_t e) { return static_cast<Ease>(std::clamp(e, 0, int32_t(Ease::Count_) - 1)); }
}

// step: 0 moveTo, 1 moveBy, 2 scaleTo, 3 rotateTo (v.x), 4 rotateBy (v.x),
// 5 fadeTo (v.x), 6 zoomTo (v.x), 7 delay (duration).
EXPORT cint dc_tween_step(cint b, cint step, DV2 v, double duration, cint e) {
    TweenBuilder* tb = builder(b);
    if (!tb) return 0;
    float d = float(duration);
    switch (step) {
        case 0: tb->moveTo(v2(v), d, ease(e)); break;
        case 1: tb->moveBy(v2(v), d, ease(e)); break;
        case 2: tb->scaleTo(v2(v), d, ease(e)); break;
        case 3: tb->rotateTo(float(v.x), d, ease(e)); break;
        case 4: tb->rotateBy(float(v.x), d, ease(e)); break;
        case 5: tb->fadeTo(float(v.x), d, ease(e)); break;
        case 6: tb->zoomTo(float(v.x), d, ease(e)); break;
        case 7: tb->delay(d); break;
    }
    return 0;
}
EXPORT cint dc_tween_tint(cint b, DColor c, double duration, cint e) {
    if (TweenBuilder* tb = builder(b)) tb->tintTo(color(c), float(duration), ease(e));
    return 0;
}
EXPORT cint dc_tween_loop(cint b, cint times, cint yoyo) {
    if (TweenBuilder* tb = builder(b)) tb->loop(times, yoyo ? TweenLoop::Yoyo : TweenLoop::Restart);
    return 0;
}
EXPORT cint dc_tween_start(cint b) {
    auto it = g.tweenBuilders.find(b);
    if (it == g.tweenBuilders.end()) return 0;
    TweenId id = it->second->start();
    g.tweenBuilders.erase(it);
    return bits(id);
}
EXPORT cint dc_tween_stop(cint id, cint complete) { if (auto* t = res<Tweens>()) t->stop(ubits(id), complete != 0); return 0; }
EXPORT cint dc_tween_pause(cint id) { if (auto* t = res<Tweens>()) t->pause(ubits(id)); return 0; }
EXPORT cint dc_tween_resume(cint id) { if (auto* t = res<Tweens>()) t->resume(ubits(id)); return 0; }
EXPORT cint dc_tween_active(cint id) { auto* t = res<Tweens>(); return t && t->isActive(ubits(id)) ? 1 : 0; }
EXPORT cint dc_tween_stop_all(cint h) {
    auto* t = res<Tweens>();
    EntityId e = idOf(h);
    if (t && e != InvalidEntityId) t->stopAll(e);
    return 0;
}
EXPORT cint dc_tween_time_scale(double s) { if (auto* t = res<Tweens>()) t->timeScale = float(s); return 0; }
EXPORT double dc_ease(cint e, double t) { return easing::apply(ease(e), float(t)); }

// =========================================================================
// Audio
// =========================================================================

EXPORT cint dc_sound_load(const char* path, cint stream) {
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
    auto* a = res<AudioResource>();
    if (!a) return fail("drift is not running");
    if (channels < 1 || channels > 2 || count < channels) return fail("a sound needs 1 or 2 channels and at least one frame");
    std::vector<float> f(samples, samples + count);
    SoundHandle s = a->createSound(f.data(), size_t(count / channels), channels, rate);
    if (!s.valid()) return fail("cannot create sound");
    return bits(s.id);
}
EXPORT double dc_sound_duration(cint s) { auto* a = res<AudioResource>(); return a ? a->soundDuration(handle<SoundHandle>(s)) : 0.0; }
EXPORT cint dc_sound_destroy(cint s) { if (auto* a = res<AudioResource>()) a->destroySound(handle<SoundHandle>(s)); return 0; }
EXPORT cint dc_sound_max_instances(cint s, cint n) { if (auto* a = res<AudioResource>()) a->setSoundMaxInstances(handle<SoundHandle>(s), n); return 0; }
EXPORT cint dc_sound_play(cint s, DPlay p) {
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
EXPORT cint dc_voice_stop(cint v, double fade) { if (auto* a = res<AudioResource>()) a->stopSound(handle<PlayingSoundHandle>(v), float(fade)); return 0; }
EXPORT cint dc_voice_pause(cint v, double fade) { if (auto* a = res<AudioResource>()) a->pauseSound(handle<PlayingSoundHandle>(v), float(fade)); return 0; }
EXPORT cint dc_voice_resume(cint v, double fade) { if (auto* a = res<AudioResource>()) a->resumeSound(handle<PlayingSoundHandle>(v), float(fade)); return 0; }
EXPORT cint dc_voice_set_volume(cint v, double vol, double fade) { if (auto* a = res<AudioResource>()) a->setSoundVolume(handle<PlayingSoundHandle>(v), float(vol), float(fade)); return 0; }
EXPORT cint dc_voice_set_pan(cint v, double pan) { if (auto* a = res<AudioResource>()) a->setSoundPan(handle<PlayingSoundHandle>(v), float(pan)); return 0; }
EXPORT cint dc_voice_set_pitch(cint v, double p) { if (auto* a = res<AudioResource>()) a->setSoundPitch(handle<PlayingSoundHandle>(v), float(p)); return 0; }
EXPORT cint dc_voice_playing(cint v) { auto* a = res<AudioResource>(); return a && a->isSoundPlaying(handle<PlayingSoundHandle>(v)) ? 1 : 0; }

EXPORT cint dc_bus(cint which) {
    auto* a = res<AudioResource>();
    if (!a) return 0;
    switch (which) {
        case 0: return bits(a->masterBus().id);
        case 1: return bits(a->musicBus().id);
        default: return bits(a->sfxBus().id);
    }
}
EXPORT cint dc_bus_create(const char* name, cint parent) {
    auto* a = res<AudioResource>();
    return a ? bits(a->createBus(name, handle<BusHandle>(parent)).id) : 0;
}
EXPORT cint dc_bus_set_volume(cint b, double v, double fade) { if (auto* a = res<AudioResource>()) a->setBusVolume(handle<BusHandle>(b), float(v), float(fade)); return 0; }
EXPORT double dc_bus_volume(cint b) { auto* a = res<AudioResource>(); return a ? a->busVolume(handle<BusHandle>(b)) : 0.0; }
EXPORT cint dc_bus_set_muted(cint b, cint m) { if (auto* a = res<AudioResource>()) a->setBusMuted(handle<BusHandle>(b), m != 0); return 0; }
EXPORT cint dc_bus_set_paused(cint b, cint p) { if (auto* a = res<AudioResource>()) a->setBusPaused(handle<BusHandle>(b), p != 0); return 0; }
EXPORT cint dc_bus_stop(cint b, double fade) { if (auto* a = res<AudioResource>()) a->stopBus(handle<BusHandle>(b), float(fade)); return 0; }

EXPORT cint dc_music_play(cint s, double crossfade, double volume, cint loop) {
    auto* a = res<AudioResource>();
    if (!a) return 0;
    MusicParams p;
    p.crossfade = float(crossfade);
    p.volume = float(volume);
    p.loop = loop != 0;
    return a->playMusic(handle<SoundHandle>(s), p) ? 1 : 0;
}
EXPORT cint dc_music_stop(double fade) { if (auto* a = res<AudioResource>()) a->stopMusic(float(fade)); return 0; }
EXPORT cint dc_music_pause(double fade) { if (auto* a = res<AudioResource>()) a->pauseMusic(float(fade)); return 0; }
EXPORT cint dc_music_resume(double fade) { if (auto* a = res<AudioResource>()) a->resumeMusic(float(fade)); return 0; }
EXPORT cint dc_music_playing() { auto* a = res<AudioResource>(); return a && a->isMusicPlaying() ? 1 : 0; }
EXPORT cint dc_audio_has_device() { auto* a = res<AudioResource>(); return a && a->hasDevice() ? 1 : 0; }

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
EXPORT cint dc_set_tilemap(cint h, cint map, double z, cint layer, cint colliders) {
    edit<Tilemap>(h, [&](Tilemap& t) {
        t.map = handle<TilemapHandle>(map);
        t.zOrder = float(z);
        t.renderLayer = uint8_t(std::clamp<cint>(layer, 0, 31));
        t.colliders = colliders != 0;
    });
    return 0;
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
    SceneSpawnOptions o;
    o.offset = v2(offset);
    o.parent = parent ? idOf(parent) : InvalidEntityId;
    if (overrides && *overrides) {
        o.overrides = nlohmann::json::parse(overrides, nullptr, false);
        if (o.overrides.is_discarded()) return fail("overrides are not valid JSON");
    }
    SceneInstance inst = s->spawnNow(handle<SceneHandle>(scene), o);
    g.scenes.push_back(std::move(inst));
    return int32_t(g.scenes.size());
}
namespace {
const SceneInstance* instance(int32_t i) { return i > 0 && i <= int32_t(g.scenes.size()) ? &g.scenes[size_t(i - 1)] : nullptr; }
}
EXPORT cint dc_scene_count(cint inst, cint roots) {
    auto* in = instance(inst);
    return in ? int32_t(roots ? in->roots.size() : in->entities.size()) : 0;
}
EXPORT cint dc_scene_entity(cint inst, cint roots, cint i) {
    auto* in = instance(inst);
    if (!in) return 0;
    const auto& list = roots ? in->roots : in->entities;
    return i >= 0 && i < int32_t(list.size()) ? handleOf(list[size_t(i)]) : 0;
}
EXPORT cint dc_scene_find(cint inst, const char* name) {
    auto* in = instance(inst);
    return in ? handleOf(in->find(name)) : 0;
}
EXPORT cint dc_scene_save(const char* path) {
    auto* s = res<SceneResource>();
    return s && s->save(path) ? 1 : 0;
}
