// drift: runs Candela games.
//
//     drift run [folder | script] [--config <path>] [--set section.key=value]...
//     drift build --web [folder] [-o <out folder>]
//     drift check [folder | script] [--json]
//
// The engine runs as usual; one exclusive system hands each frame to the
// Candela host (bindings/candela/host), which calls the game's systems. The
// functions below are the host's view of the engine: entities, engine
// components as JSON shaped like the `drift` module's structs, and the
// services its resources reach (input, assets, sound, window, effects).

#include <drift/App.hpp>
#include <drift/AssetServer.hpp>
#include <drift/CollisionBridge.hpp>
#include <drift/Hierarchy.hpp>
#include <drift/gameplay/ProceduralAnimation.hpp>
#include <drift/math/Vector.hpp>
#include <drift/Log.hpp>
#include <drift/Reflect.hpp>
#include <drift/Scene.hpp>
#include <drift/PhysicsSyncSystem.hpp>
#include <drift/components/AudioSource.hpp>
#include <drift/components/Behavior.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/Light2D.hpp>
#include <drift/components/SpriteAnimator.hpp>
#include <drift/components/TrailRenderer.hpp>
#include <drift/resources/FontResource.hpp>
#include <drift/resources/ParticleSystemResource.hpp>
#include <drift/resources/SpriteResource.hpp>
#include <drift/components/CameraController.hpp>
#include <drift/components/Name.hpp>
#include <drift/components/ParticleEmitter.hpp>
#include <drift/gameplay/SaveData.hpp>
#include <drift/gameplay/Tween.hpp>
#include <drift/plugins/SavePlugin.hpp>
#include <drift/components/Physics.hpp>
#include <drift/components/Shape.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Text.hpp>
#include <drift/components/Tilemap.hpp>
#include <drift/gameplay/Pathfinding.hpp>
#include <drift/gameplay/Steering.hpp>
#include <drift/math/Noise.hpp>
#include <drift/plugins/RandomPlugin.hpp>
#include <drift/procgen/Generators.hpp>
#include <drift/procgen/TilemapFill.hpp>
#include <drift/procgen/Wfc.hpp>
#include <drift/resources/TilemapResource.hpp>
#include <drift/effects/PostEffects.hpp>
#include <drift/gameplay/InputActions.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/plugins/LightingPlugin.hpp>
#include <drift/resources/AudioResource.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/PhysicsResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>

#include "msgpack.hpp"
#include "services.hpp"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#else
#include "web_pack.hpp"
#endif

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <span>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace drift;
using nlohmann::json;

extern "C" {
int32_t drift_host_load(const char* entry, const char* module_dir);
int32_t drift_host_reload(const char* entry, const char* module_dir);
const char* drift_host_error();
const char* drift_host_check(const char* entry, const char* module_dir, const char* toml);
void drift_host_contact(int32_t kind, uint64_t a, uint64_t b, float px, float py, float nx, float ny);
void drift_host_frame(double dt, double elapsed, int64_t frame, double scale, double real_dt);
void drift_host_unload();
int32_t drift_host_is_component(const char* name);
void drift_host_scene_component(uint64_t e, const char* name, const char* value, const char* ids);
}

namespace {

App* g_app = nullptr;
bool g_loadFailed = false;   // the game did not compile; drift exits with 1
std::vector<uint8_t> g_answer;   // answers handed to the host stay alive until the next call
EffectId g_fade;
// Tweens run by the engine's Tweens resource, keyed by entity, with the
// candela value they came from.
struct RunningTween {
    TweenId id = InvalidTweenId;
    std::string value;   // the value's MessagePack bytes
};
std::unordered_map<EntityId, RunningTween> g_tweens;

World& world() { return g_app->world(); }

template <typename T> T* res() { return g_app ? g_app->getResource<T>() : nullptr; }

template <typename T> ComponentId cid() {
    auto& reg = world().componentRegistry();
    if (!reg.has<T>()) world().registerComponent<T>(typeid(T).name());
    return reg.get<T>();
}

template <typename T> const T* get(EntityId e) {
    if (!world().isAlive(e) || !world().hasComponent(e, cid<T>())) return nullptr;
    return static_cast<const T*>(world().getComponent(e, cid<T>()));
}

template <typename T> void put(EntityId e, const T& v) { world().set<T>(e, cid<T>(), v); }

// Values cross to and from the host as MessagePack.
const uint8_t* keep(const json& j, size_t* len) {
    g_answer.clear();
    json::to_msgpack(j, g_answer);
    *len = g_answer.size();
    return g_answer.data();
}

json unpack(const uint8_t* data, size_t len) {
    if (!data || len == 0) return json();
    return json::from_msgpack(data, data + len, true, false);
}

// ---- Value shapes of the drift module ----
//
// Engine components go to the host written straight into MessagePack (Pack)
// and come back as a view of its bytes (Msg); engine functions still take
// and answer nlohmann values. The readers below take either.

using candela::Msg;
using candela::Pack;

// A field of a value, or null when it has none.
const json& field(const json& j, const char* key) {
    static const json none;
    if (!j.is_object()) return none;
    auto it = j.find(key);
    return it != j.end() ? *it : none;
}
Msg field(const Msg& j, const char* key) { return j[key]; }

// Iterates the elements of a list.
template <typename F> void forEach(const json& j, F&& fn) {
    if (j.is_array()) for (const auto& x : j) fn(x);
}
template <typename F> void forEach(const Msg& j, F&& fn) { j.each(fn); }

void vec(Pack& p, Vec2 v) { p.object([&] { p.kv("x", v.x); p.kv("y", v.y); }); }
json vec(Vec2 v) { return {{"x", v.x}, {"y", v.y}}; }
template <typename J> Vec2 vec(const J& j, Vec2 fallback = {}) {
    if (!j.is_object()) return fallback;
    return {j.value("x", fallback.x), j.value("y", fallback.y)};
}
void color(Pack& p, Color c) {
    p.object([&] { p.kv("r", c.r / 255.0); p.kv("g", c.g / 255.0); p.kv("b", c.b / 255.0); p.kv("a", c.a / 255.0); });
}
json color(Color c) { return {{"r", c.r / 255.0}, {"g", c.g / 255.0}, {"b", c.b / 255.0}, {"a", c.a / 255.0}}; }
uint8_t channel(double v) { return uint8_t(std::clamp(v, 0.0, 1.0) * 255.0 + 0.5); }
template <typename J> Color color(const J& j, Color fallback = Color::white()) {
    if (!j.is_object()) return fallback;
    return {channel(j.value("r", 1.0)), channel(j.value("g", 1.0)), channel(j.value("b", 1.0)), channel(j.value("a", 1.0))};
}
void rectJ(Pack& p, Rect r) { p.object([&] { p.kv("x", r.x); p.kv("y", r.y); p.kv("w", r.w); p.kv("h", r.h); }); }
json rectJ(Rect r) { return {{"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h}}; }
template <typename J> Rect rectJ(const J& j) {
    if (!j.is_object()) return {};
    return {j.value("x", 0.f), j.value("y", 0.f), j.value("w", 0.f), j.value("h", 0.f)};
}
template <typename J> uint8_t layer(const J& j) { return uint8_t(std::clamp(j.value("layer", 0), 0, 31)); }

const char* blendNames[] = {"Alpha", "Additive", "Multiply", "Premultiplied", "Screen", "Opaque"};
template <typename J> BlendMode blend(const J& j) {
    std::string s = j.is_string() ? j.template get<std::string>() : "Alpha";
    for (int i = 0; i < 6; ++i) if (s == blendNames[i]) return static_cast<BlendMode>(i);
    return BlendMode::Alpha;
}
template <typename E, typename J> E pick(const J& j, std::initializer_list<const char*> names) {
    std::string s = j.is_string() ? j.template get<std::string>() : "";
    int i = 0;
    for (const char* n : names) {
        if (s == n) return static_cast<E>(i);
        ++i;
    }
    return static_cast<E>(0);
}
template <typename E> const char* name(E v, std::initializer_list<const char*> names) {
    int i = 0;
    for (const char* n : names) if (i++ == int(v)) return n;
    return *names.begin();
}

Vec2 textureSize(TextureHandle t) {
    auto* r = res<RendererResource>();
    return r && t.valid() ? r->textureSize(t) : Vec2{};
}

// Moves a body Box2D owns to a Transform the script set.
void teleport(EntityId e, Vec2 position, float rotation) {
    auto* bridge = res<PhysicsBridge>();
    auto* physics = res<PhysicsResource>();
    if (!bridge || !physics) return;
    auto it = bridge->entityToBody.find(e);
    if (it == bridge->entityToBody.end()) return;
    Vec2 offset{};
    if (auto* box = get<BoxCollider2D>(e)) offset = box->offset;
    physics->setBodyPosition(it->second, position + offset);
    physics->setBodyRotation(it->second, rotation);
}

// ---- Hierarchy and procedural animation ----
//
// Parent and GlobalTransform components, the hierarchy queries, and the
// procedural animation solvers (include/drift/math/ProcAnim.hpp) the drift
// module's animation helpers call.

void transformJ(Pack& p, Vec2 position, float rotation, Vec2 scale) {
    p.object([&] {
        p.key("position");
        vec(p, position);
        p.kv("rotation", rotation);
        p.key("scale");
        vec(p, scale);
    });
}

bool hasParent(EntityId e) { return get<Parent>(e) != nullptr; }
void readParent(EntityId e, Pack& p) {
    p.object([&] {
        p.id("entity", get<Parent>(e)->entity);
        p.kv("keep_world", false);
    });
}
void writeParent(EntityId e, const Msg& j) {
    Msg pe = j["entity"];
    EntityId p = pe.is_object() ? pe.value("id", EntityId(0)) : InvalidEntityId;
    if (!setParent(world(), e, p, j.value("keep_world", false))) {
        DRIFT_LOG_WARN("Candela: cannot parent entity %llu to %llu (dead, a loop, or %d children already)",
                       (unsigned long long)e, (unsigned long long)p, MaxChildren);
    }
}
void removeParentKeepingPlace(EntityId e) { removeParent(world(), e, true); }

void readGlobal(EntityId e, Pack& p) {
    GlobalTransform2D g = worldTransformOf(world(), e);
    transformJ(p, g.position, g.rotation, g.scale);
}
// Assigning a GlobalTransform moves the entity there in world space.
void writeGlobal(EntityId e, const Msg& j) {
    GlobalTransform2D g{vec(j["position"]), j.value("rotation", 0.f), vec(j["scale"], {1, 1})};
    put(e, localTransformFor(world(), parentOf(world(), e), g));
}

// ---- Engine functions read straight from MessagePack ----
//
// The functions a game may call for every entity every frame (hierarchy
// queries and the procedural animation solvers) read their arguments from
// the host's bytes and write their answer the same way, without building
// nlohmann values; drift_bridge_call looks them up before the others.

// The arguments of a call: a list, each element found once.
struct Args {
    std::vector<Msg> items;
    explicit Args(const Msg& list) {
        items.reserve(list.size());
        list.each([&](const Msg& m) { items.push_back(m); });
    }
    Msg operator[](size_t i) const { return i < items.size() ? items[i] : Msg{}; }
    float f(size_t i) const { return (*this)[i].get<float>(); }
    int n(size_t i) const { return (*this)[i].get<int>(); }
    EntityId e(size_t i) const { return (*this)[i].get<EntityId>(); }
};

std::vector<Vec2> pointsOf(const Msg& flat) {
    std::vector<Vec2> pts;
    pts.reserve(flat.size() / 2);
    float x = 0.f;
    size_t i = 0;
    flat.each([&](const Msg& m) {
        if (i++ % 2 == 0) x = m.get<float>();
        else pts.push_back({x, m.get<float>()});
    });
    return pts;
}
std::vector<float> floatsOf(const Msg& j) {
    std::vector<float> v;
    v.reserve(j.size());
    j.each([&](const Msg& m) { v.push_back(m.get<float>()); });
    return v;
}
void pushPoints(Pack& out, const std::vector<Vec2>& pts) {
    for (Vec2 p : pts) {
        out.val(p.x);
        out.val(p.y);
    }
}
void floats(Pack& out, std::initializer_list<float> v) {
    out.array(v.size());
    for (float x : v) out.val(x);
}

using FastService = void (*)(const Args&, Pack&);

const std::unordered_map<std::string_view, FastService>& fastServices() {
    static const std::unordered_map<std::string_view, FastService> table = {
        // Hierarchy queries.
        {"hier_parent", [](const Args& a, Pack& out) { out.val(parentOf(world(), a.e(0))); }},
        {"hier_children", [](const Args& a, Pack& out) { out.list(childrenOf(world(), a.e(0))); }},
        {"hier_descendants", [](const Args& a, Pack& out) { out.list(descendantsOf(world(), a.e(0))); }},
        {"hier_world",
         [](const Args& a, Pack& out) {
             GlobalTransform2D g = worldTransformOf(world(), a.e(0));
             floats(out, {g.position.x, g.position.y, g.rotation, g.scale.x, g.scale.y});
         }},
        {"hier_to_world",
         [](const Args& a, Pack& out) {
             Vec2 r = localToWorld(world(), a.e(0), {a.f(1), a.f(2)});
             floats(out, {r.x, r.y});
         }},
        {"hier_to_local",
         [](const Args& a, Pack& out) {
             Vec2 r = worldToLocal(world(), a.e(0), {a.f(1), a.f(2)});
             floats(out, {r.x, r.y});
         }},
        // The solvers of drift/gameplay/ProceduralAnimation.hpp; state
        // crosses as flat float lists and comes back the same way.
        {"anim_spring",
         [](const Args& a, Pack& out) {
             // kind (0 float, 1 angle, 2 critically damped), value, velocity, target, stiffness or smooth time, damping, dt
             float v = a.f(1), vel = a.f(2);
             int kind = a.n(0);
             if (kind == 0) springStep(v, vel, a.f(3), a.f(4), a.f(5), a.f(6));
             else if (kind == 1) angleSpringStep(v, vel, a.f(3), a.f(4), a.f(5), a.f(6));
             else v = smoothDamp(v, a.f(3), vel, a.f(4), a.f(6));
             floats(out, {v, vel});
         }},
        {"anim_spring2",
         [](const Args& a, Pack& out) {
             // kind (0 spring, 2 critically damped), value, velocity, target (x, y each), stiffness or smooth time, damping, dt
             Vec2 v{a.f(1), a.f(2)}, vel{a.f(3), a.f(4)}, t{a.f(5), a.f(6)};
             if (a.n(0) == 0) springStep(v, vel, t, a.f(7), a.f(8), a.f(9));
             else v = smoothDamp(v, t, vel, a.f(7), a.f(9));
             floats(out, {v.x, v.y, vel.x, vel.y});
         }},
        {"anim_two_bone",
         [](const Args& a, Pack& out) {
             TwoBonePose p = twoBoneIk({a.f(0), a.f(1)}, {a.f(2), a.f(3)}, a.f(4), a.f(5), a.f(6));
             floats(out, {p.joint.x, p.joint.y, p.end.x, p.end.y, p.upperAngle, p.lowerAngle, p.reached ? 1.f : 0.f});
         }},
        {"anim_fabrik",
         [](const Args& a, Pack& out) {
             // joints, lengths, target x, y, iterations, tolerance, base angle, min angles, max angles
             std::vector<Vec2> joints = pointsOf(a[0]);
             // The options only view the limits: they live here.
             std::vector<float> lengths = floatsOf(a[1]), lo = floatsOf(a[7]), hi = floatsOf(a[8]);
             FabrikOptions o;
             o.iterations = a.n(4);
             o.tolerance = a.f(5);
             o.baseAngle = a.f(6);
             o.minAngle = lo;
             o.maxAngle = hi;
             bool reached = solveFabrik(joints, lengths, {a.f(2), a.f(3)}, o);
             out.array(1 + joints.size() * 2);
             out.val(reached ? 1.f : 0.f);
             pushPoints(out, joints);
         }},
        {"anim_verlet",
         [](const Args& a, Pack& out) {
             // points, previous, anchor x, y, segment, dt, gravity x, y, damping, iterations, stiffness, ground, pin end, end x, y
             std::vector<Vec2> pts = pointsOf(a[0]), prev = pointsOf(a[1]);
             prev.resize(pts.size());
             VerletOptions o;
             o.gravity = {a.f(6), a.f(7)};
             o.damping = a.f(8);
             o.iterations = a.n(9);
             o.stiffness = a.f(10);
             o.ground = a.f(11);
             o.pinEnd = a[12].get<bool>();
             o.endTarget = {a.f(13), a.f(14)};
             stepVerlet(pts, prev, {a.f(2), a.f(3)}, a.f(4), a.f(5), o);
             out.array(pts.size() * 2 + prev.size() * 2);
             pushPoints(out, pts);
             pushPoints(out, prev);
         }},
        {"anim_foot",
         [](const Args& a, Pack& out) {
             // position, from, to, progress, rest, distance, duration, height, lead, up, dt, can step
             Foot ft;
             ft.position = {a.f(0), a.f(1)};
             ft.from = {a.f(2), a.f(3)};
             ft.to = {a.f(4), a.f(5)};
             ft.progress = a.f(6);
             StepOptions o;
             o.distance = a.f(9);
             o.duration = a.f(10);
             o.height = a.f(11);
             o.lead = a.f(12);
             o.up = {a.f(13), a.f(14)};
             bool landed = ft.update({a.f(7), a.f(8)}, o, a.f(15), a[16].get<bool>());
             floats(out, {ft.position.x, ft.position.y, ft.from.x, ft.from.y, ft.to.x, ft.to.y, ft.progress,
                          landed ? 1.f : 0.f});
         }},
    };
    return table;
}

// ---- Engine components ----

struct Codec {
    const char* name;
    bool (*has)(EntityId);
    void (*read)(EntityId, Pack&);
    void (*write)(EntityId, const Msg&);
    void (*remove)(EntityId);
    ComponentId (*id)();
};

bool isShape(EntityId e, ShapeKind k) {
    const Shape2D* s = get<Shape2D>(e);
    return s && s->kind == k;
}

void readShape(EntityId e, Pack& p) {
    const Shape2D& s = *get<Shape2D>(e);
    p.object([&] {
        p.key("color");
        color(p, s.color);
        p.kv("outline", s.outline);
        p.kv("z", s.zOrder);
        p.kv("layer", s.layer);
        p.kv("visible", s.visible);
        if (s.kind == ShapeKind::Rectangle) {
            p.key("size");
            vec(p, s.size);
            p.kv("corner_radius", s.cornerRadius);
            p.key("pivot");
            vec(p, s.anchor);
        } else {
            p.kv("radius", s.radius);
        }
    });
}

void writeShape(EntityId e, const Msg& j, ShapeKind kind) {
    Shape2D s;
    s.kind = kind;
    s.size = vec(j["size"], {32, 32});
    s.radius = j.value("radius", 16.f);
    s.color = color(j["color"]);
    s.outline = j.value("outline", 0.f);
    s.cornerRadius = j.value("corner_radius", 0.f);
    s.anchor = vec(j["pivot"], {0.5f, 0.5f});
    s.zOrder = j.value("z", 0.f);
    s.layer = layer(j);
    s.visible = j.value("visible", true);
    put(e, s);
}

template <typename J> ColorF colorF(const J& j, ColorF fallback) {
    if (!j.is_object()) return fallback;
    return {j.value("r", 1.f), j.value("g", 1.f), j.value("b", 1.f), j.value("a", 1.f)};
}
void colorF(Pack& p, ColorF c) { p.object([&] { p.kv("r", c.r); p.kv("g", c.g); p.kv("b", c.b); p.kv("a", c.a); }); }

const char* shapeNames[] = {"Point", "Circle", "Ring", "Box", "Line"};

uint32_t idOf(const json& j) { return j.is_object() ? j.value("id", 0u) : 0u; }
uint32_t idOf(const Msg& j) { return j.value("id", 0u); }

template <typename J> EmitterConfig emitterConfig(const J& j) {
    EmitterConfig c;
    c.spawnRate = j.value("rate", 20.f);
    c.lifetime = {j.value("lifetime_min", 0.5f), j.value("lifetime_max", 1.5f)};
    c.speed = {j.value("speed_min", 20.f), j.value("speed_max", 60.f)};
    c.angle = {j.value("angle_min", 0.f), j.value("angle_max", 6.2831853f)};
    float s0 = j.value("size_start", 6.f), s1 = j.value("size_end", 1.f);
    c.sizeStart = {s0, s0};
    c.sizeEnd = {s1, s1};
    c.colorStart = colorF(field(j, "color_start"), {1, 1, 1, 1});
    c.colorEnd = colorF(field(j, "color_end"), {1, 1, 1, 0});
    c.gravity = vec(field(j, "gravity"));
    c.drag = j.value("drag", 0.f);
    c.maxParticles = std::max(1, j.value("max_particles", 256));
    c.blendMode = blend(field(j, "blend"));
    c.texture = TextureHandle{idOf(field(j, "texture"))};
    if (!c.texture.valid()) {
        if (auto* r = res<RendererResource>()) c.texture = r->whiteTexture();
    }
    c.shape = pick<EmissionShape>(field(j, "shape"), {"Point", "Circle", "Ring", "Box", "Line"});
    c.shapeRadius = j.value("shape_radius", 0.f);
    c.shapeExtents = vec(field(j, "shape_extents"));
    c.duration = j.value("duration", -1.f);
    c.looping = j.value("looping", true);
    c.zOrder = j.value("z", 10.f);
    // A `_max` of 0 is the same as the minimum.
    float s0max = j.value("size_start_max", 0.f), s1max = j.value("size_end_max", 0.f);
    if (s0max > 0.f) c.sizeStart.max = s0max;
    if (s1max > 0.f) c.sizeEnd.max = s1max;
    c.srcRect = rectJ(field(j, "frame"));
    c.initialRotation = {j.value("rotation_min", 0.f), j.value("rotation_max", 0.f)};
    c.angularVelocity = {j.value("spin_min", 0.f), j.value("spin_max", 0.f)};
    c.velocityInheritance = j.value("inherit_velocity", 0.f);
    c.space = j.value("local", false) ? ParticleSpace::Local : ParticleSpace::World;
    c.preWarmTime = j.value("prewarm", 0.f);
    Rect bounds = rectJ(field(j, "bounds"));
    if (bounds.w > 0.f && bounds.h > 0.f) {
        c.boundsMin = {bounds.x, bounds.y};
        c.boundsMax = {bounds.x + bounds.w, bounds.y + bounds.h};
    }
    c.seed = static_cast<uint32_t>(j.value("seed", int64_t(0)));
    forEach(field(j, "bursts"), [&](const auto& b) {
        c.bursts.push_back(BurstEntry{b.value("time", 0.f), b.value("count", 10), b.value("cycles", 1), b.value("interval", 0.f)});
    });
    return c;
}

void emitterJson(Pack& p, const ParticleEmitter& pe) {
    const EmitterConfig& c = pe.config;
    auto* r = res<RendererResource>();
    uint32_t tex = r && c.texture.id == r->whiteTexture().id ? 0u : c.texture.id;
    p.object([&] {
        p.kv("rate", c.spawnRate);
        p.kv("lifetime_min", c.lifetime.min);
        p.kv("lifetime_max", c.lifetime.max);
        p.kv("speed_min", c.speed.min);
        p.kv("speed_max", c.speed.max);
        p.kv("angle_min", c.angle.min);
        p.kv("angle_max", c.angle.max);
        p.kv("size_start", c.sizeStart.min);
        p.kv("size_end", c.sizeEnd.min);
        p.key("color_start");
        colorF(p, c.colorStart);
        p.key("color_end");
        colorF(p, c.colorEnd);
        p.key("gravity");
        vec(p, c.gravity);
        p.kv("drag", c.drag);
        p.kv("max_particles", c.maxParticles);
        p.kv("blend", blendNames[int(c.blendMode)]);
        p.id("texture", tex);
        p.kv("shape", shapeNames[int(c.shape)]);
        p.kv("shape_radius", c.shapeRadius);
        p.key("shape_extents");
        vec(p, c.shapeExtents);
        p.kv("duration", c.duration);
        p.kv("looping", c.looping);
        p.kv("z", c.zOrder);
        p.kv("playing", pe.playing);
        p.key("frame");
        rectJ(p, c.srcRect);
        p.kv("size_start_max", c.sizeStart.max == c.sizeStart.min ? 0.f : c.sizeStart.max);
        p.kv("size_end_max", c.sizeEnd.max == c.sizeEnd.min ? 0.f : c.sizeEnd.max);
        p.kv("rotation_min", c.initialRotation.min);
        p.kv("rotation_max", c.initialRotation.max);
        p.kv("spin_min", c.angularVelocity.min);
        p.kv("spin_max", c.angularVelocity.max);
        p.kv("inherit_velocity", c.velocityInheritance);
        p.kv("local", c.space == ParticleSpace::Local);
        p.kv("prewarm", c.preWarmTime);
        p.key("bounds");
        rectJ(p, c.boundsMin.x == c.boundsMax.x && c.boundsMin.y == c.boundsMax.y
                     ? Rect{}
                     : Rect{c.boundsMin.x, c.boundsMin.y, c.boundsMax.x - c.boundsMin.x, c.boundsMax.y - c.boundsMin.y});
        p.kv("seed", c.seed);
        p.key("bursts");
        p.array(c.bursts.size());
        for (const BurstEntry& b : c.bursts) {
            p.object([&] {
                p.kv("time", b.time);
                p.kv("count", b.count);
                p.kv("cycles", b.cycles);
                p.kv("interval", b.interval);
            });
        }
    });
}

const char* easeNames[] = {
    "Linear", "InSine", "OutSine", "InOutSine", "InQuad", "OutQuad", "InOutQuad",
    "InCubic", "OutCubic", "InOutCubic", "InQuart", "OutQuart", "InOutQuart",
    "InQuint", "OutQuint", "InOutQuint", "InExpo", "OutExpo", "InOutExpo",
    "InCirc", "OutCirc", "InOutCirc", "InBack", "OutBack", "InOutBack",
    "InElastic", "OutElastic", "InOutElastic", "InBounce", "OutBounce", "InOutBounce", "Spring"};

Ease easeOf(const Msg& j) {
    std::string n = j.is_string() ? j.get<std::string>() : "Linear";
    for (int i = 0; i < int(Ease::Count_); ++i) {
        if (n == easeNames[i]) return static_cast<Ease>(i);
    }
    return Ease::Linear;
}

void stopTween(EntityId e) {
    auto it = g_tweens.find(e);
    if (it == g_tweens.end()) return;
    if (auto* tw = res<Tweens>()) tw->stop(it->second.id, false);
    g_tweens.erase(it);
}

// Starts the tween a candela Tween value describes.
void startTween(EntityId e, const Msg& j) {
    // The same value written back while it runs leaves it running; once it
    // has finished, inserting it again plays it again.
    auto* tw = res<Tweens>();
    auto it = g_tweens.find(e);
    std::string_view bytes = j.bytes();
    if (it != g_tweens.end() && it->second.value == bytes && tw && tw->isActive(it->second.id)) return;
    stopTween(e);
    if (!tw) return;
    TweenBuilder b = tw->on(e);
    j["steps"].each([&](const Msg& st) {
        float a = st.value("a", 0.f), bb = st.value("b", 0.f), secs = st.value("seconds", 0.f);
        Ease ez = easeOf(st["ease"]);
        switch (st.value("kind", 0)) {
            case 0: b.moveTo({a, bb}, secs, ez); break;
            case 1: b.moveBy({a, bb}, secs, ez); break;
            case 2: b.scaleTo({a, bb}, secs, ez); break;
            case 3: b.rotateTo(a, secs, ez); break;
            case 4: b.rotateBy(a, secs, ez); break;
            case 5: b.fadeTo(a, secs, ez); break;
            case 6: b.zoomTo(a, secs, ez); break;
            case 7: b.tintTo(Color{channel(a), channel(bb), channel(st.value("c", 0.0)), channel(st.value("d", 1.0))}, secs, ez); break;
            case 8: b.delay(secs); break;
        }
    });
    int loops = j.value("loops", 0);
    if (loops != 0) b.loop(loops, j.value("mirror", false) ? TweenLoop::Yoyo : TweenLoop::Restart);
    g_tweens[e] = {b.start(), std::string(bytes)};
}

// A collider's category and mask bits; out of range values keep the defaults.
CollisionFilter filterOf(const Msg& j) {
    CollisionFilter f;
    int64_t cat = j.value("category", int64_t(1)), mask = j.value("mask", int64_t(0xFFFF));
    f.category = cat >= 0 && cat <= 0xFFFFFFFFll ? uint32_t(cat) : 1u;
    f.mask = mask >= 0 && mask <= 0xFFFFFFFFll ? uint32_t(mask) : 0xFFFFu;
    return f;
}
// The settings every collider has.
template <typename C> void colliderJ(Pack& p, const C& c) {
    p.kv("sensor", c.isSensor);
    p.kv("density", c.density);
    p.kv("friction", c.friction);
    p.kv("restitution", c.restitution);
    p.kv("category", c.filter.category);
    p.kv("mask", c.filter.mask);
}
template <typename C> void colliderOf(const Msg& j, C& c) {
    c.isSensor = j.value("sensor", false);
    c.density = j.value("density", 1.f);
    c.friction = j.value("friction", 0.3f);
    c.restitution = j.value("restitution", 0.f);
    c.filter = filterOf(j);
}

const char* jointKinds[] = {"Distance", "Revolute", "Prismatic", "Weld"};

const char* textAligns[] = {"Left", "Center", "Right"};
const char* textVAligns[] = {"Top", "Middle", "Bottom"};

std::vector<Codec> presentationCodecs();
std::vector<Codec> effectCodecs();
std::vector<Codec> animationCodecs();

template <typename T> void removeIt(EntityId e) { world().removeComponent(e, cid<T>()); }
template <typename T> ComponentId idOfC() { return cid<T>(); }
template <typename T> bool hasIt(EntityId e) { return get<T>(e) != nullptr; }

const std::vector<Codec>& codecs() {
    static const std::vector<Codec> list = [] {
    std::vector<Codec> l = {
        {"Transform", hasIt<Transform2D>,
         [](EntityId e, Pack& p) {
             const Transform2D& t = *get<Transform2D>(e);
             transformJ(p, t.position, t.rotation, t.scale);
         },
         [](EntityId e, const Msg& j) {
             Transform2D t;
             t.position = vec(j["position"]);
             t.rotation = j.value("rotation", 0.f);
             t.scale = vec(j["scale"], {1, 1});
             put(e, t);
             teleport(e, t.position, t.rotation);
         },
         removeIt<Transform2D>, idOfC<Transform2D>},
        {"Sprite", hasIt<Sprite>,
         [](EntityId e, Pack& p) {
             const Sprite& s = *get<Sprite>(e);
             Vec2 size = s.srcRect.w > 0 ? Vec2{s.srcRect.w, s.srcRect.h} : textureSize(s.texture);
             p.object([&] {
                 p.id("image", s.texture.id);
                 p.key("frame");
                 rectJ(p, s.srcRect);
                 p.key("pivot");
                 vec(p, Vec2{size.x > 0 ? s.origin.x / size.x : 0.5f, size.y > 0 ? s.origin.y / size.y : 0.5f});
                 p.key("color");
                 color(p, s.tint);
                 p.kv("flip_x", (int(s.flip) & int(Flip::H)) != 0);
                 p.kv("flip_y", (int(s.flip) & int(Flip::V)) != 0);
                 p.kv("z", s.zOrder);
                 p.kv("layer", s.layer);
                 p.kv("visible", s.visible);
                 p.kv("blend", blendNames[int(s.blend)]);
                 p.id("material", s.material.id);
             });
         },
         [](EntityId e, const Msg& j) {
             Sprite s;
             s.material = MaterialHandle{idOf(j["material"])};
             s.texture = TextureHandle{idOf(j["image"])};
             s.srcRect = rectJ(j["frame"]);
             Vec2 size = s.srcRect.w > 0 ? Vec2{s.srcRect.w, s.srcRect.h} : textureSize(s.texture);
             Vec2 pivot = vec(j["pivot"], {0.5f, 0.5f});
             s.origin = {pivot.x * size.x, pivot.y * size.y};
             s.tint = color(j["color"]);
             int flip = (j.value("flip_x", false) ? int(Flip::H) : 0) | (j.value("flip_y", false) ? int(Flip::V) : 0);
             s.flip = static_cast<Flip>(flip);
             s.zOrder = j.value("z", 0.f);
             s.layer = layer(j);
             s.visible = j.value("visible", true);
             s.blend = blend(j["blend"]);
             put(e, s);
         },
         removeIt<Sprite>, idOfC<Sprite>},
        {"Rectangle",
         [](EntityId e) { return isShape(e, ShapeKind::Rectangle); },
         readShape,
         [](EntityId e, const Msg& j) { writeShape(e, j, ShapeKind::Rectangle); },
         removeIt<Shape2D>, idOfC<Shape2D>},
        {"Circle",
         [](EntityId e) { return isShape(e, ShapeKind::Circle); },
         readShape,
         [](EntityId e, const Msg& j) { writeShape(e, j, ShapeKind::Circle); },
         removeIt<Shape2D>, idOfC<Shape2D>},
        {"Text", hasIt<Text>,
         [](EntityId e, Pack& p) {
             const Text& t = *get<Text>(e);
             p.object([&] {
                 p.kv("value", std::string_view(t.str()));
                 p.kv("size", t.size);
                 p.key("color");
                 color(p, t.color);
                 p.kv("align", textAligns[int(t.align)]);
                 p.kv("valign", textVAligns[int(t.valign)]);
                 p.kv("wrap_width", t.wrapWidth);
                 p.kv("line_spacing", t.lineSpacing);
                 p.kv("outline", t.outlineWidth);
                 p.key("outline_color");
                 color(p, t.outlineColor);
                 p.key("shadow_offset");
                 vec(p, t.shadowOffset);
                 p.key("shadow_color");
                 color(p, t.shadowColor);
                 p.kv("rich_text", t.richText);
                 p.id("font", t.font.id);
                 p.kv("z", t.zOrder);
                 p.kv("layer", t.layer);
                 p.kv("visible", t.visible);
             });
         },
         [](EntityId e, const Msg& j) {
             Text t;
             t.set(j.value("value", ""));
             t.size = j.value("size", 24.f);
             t.color = color(j["color"]);
             t.align = pick<TextAlign>(j["align"], {"Left", "Center", "Right"});
             t.valign = pick<TextVAlign>(j["valign"], {"Top", "Middle", "Bottom"});
             t.wrapWidth = j.value("wrap_width", 0.f);
             t.lineSpacing = j.value("line_spacing", 1.f);
             t.outlineWidth = j.value("outline", 0.f);
             t.outlineColor = color(j["outline_color"], Color::black());
             t.shadowOffset = vec(j["shadow_offset"]);
             t.shadowColor = color(j["shadow_color"], Color{0, 0, 0, 160});
             t.richText = j.value("rich_text", false);
             t.font = FontHandle{idOf(j["font"])};
             t.zOrder = j.value("z", 0.f);
             t.layer = layer(j);
             t.visible = j.value("visible", true);
             put(e, t);
         },
         removeIt<Text>, idOfC<Text>},
        {"Velocity", hasIt<Velocity2D>,
         [](EntityId e, Pack& p) {
             const Velocity2D& v = *get<Velocity2D>(e);
             p.object([&] {
                 p.key("linear");
                 vec(p, v.linear);
                 p.kv("angular", v.angular);
             });
         },
         [](EntityId e, const Msg& j) { put(e, Velocity2D{vec(j["linear"]), j.value("angular", 0.f)}); },
         removeIt<Velocity2D>, idOfC<Velocity2D>},
        {"RigidBody", hasIt<RigidBody2D>,
         [](EntityId e, Pack& p) {
             const RigidBody2D& b = *get<RigidBody2D>(e);
             p.object([&] {
                 p.kv("kind", name(b.type, {"Static", "Dynamic", "Kinematic"}));
                 p.kv("fixed_rotation", b.fixedRotation);
                 p.kv("gravity_scale", b.gravityScale);
                 p.kv("linear_damping", b.linearDamping);
                 p.kv("angular_damping", b.angularDamping);
             });
         },
         [](EntityId e, const Msg& j) {
             RigidBody2D b;
             b.type = pick<BodyType>(j["kind"], {"Static", "Dynamic", "Kinematic"});
             b.fixedRotation = j.value("fixed_rotation", false);
             b.gravityScale = j.value("gravity_scale", 1.f);
             b.linearDamping = j.value("linear_damping", 0.f);
             b.angularDamping = j.value("angular_damping", 0.f);
             put(e, b);
         },
         removeIt<RigidBody2D>, idOfC<RigidBody2D>},
        {"BoxCollider", hasIt<BoxCollider2D>,
         [](EntityId e, Pack& p) {
             const BoxCollider2D& c = *get<BoxCollider2D>(e);
             p.object([&] {
                 p.key("half_size");
                 vec(p, c.halfSize);
                 p.key("offset");
                 vec(p, c.offset);
                 colliderJ(p, c);
             });
         },
         [](EntityId e, const Msg& j) {
             BoxCollider2D c;
             c.halfSize = vec(j["half_size"], {16, 16});
             c.offset = vec(j["offset"]);
             colliderOf(j, c);
             put(e, c);
         },
         removeIt<BoxCollider2D>, idOfC<BoxCollider2D>},
        {"CircleCollider", hasIt<CircleCollider2D>,
         [](EntityId e, Pack& p) {
             const CircleCollider2D& c = *get<CircleCollider2D>(e);
             p.object([&] {
                 p.kv("radius", c.radius);
                 p.key("offset");
                 vec(p, c.offset);
                 colliderJ(p, c);
             });
         },
         [](EntityId e, const Msg& j) {
             CircleCollider2D c;
             c.radius = j.value("radius", 16.f);
             c.offset = vec(j["offset"]);
             colliderOf(j, c);
             put(e, c);
         },
         removeIt<CircleCollider2D>, idOfC<CircleCollider2D>},
        {"TextureCollider", hasIt<TextureCollider2D>,
         [](EntityId e, Pack& p) {
             const TextureCollider2D& c = *get<TextureCollider2D>(e);
             p.object([&] {
                 p.kv("threshold", c.threshold);
                 p.kv("tolerance", c.tolerance);
                 p.kv("min_area", c.minArea);
                 p.kv("follow_sprite", c.followSprite);
                 colliderJ(p, c);
             });
         },
         [](EntityId e, const Msg& j) {
             TextureCollider2D c;
             c.threshold = j.value("threshold", 0.5f);
             c.tolerance = j.value("tolerance", 1.f);
             c.minArea = j.value("min_area", 4.f);
             c.followSprite = j.value("follow_sprite", false);
             colliderOf(j, c);
             put(e, c);
         },
         removeIt<TextureCollider2D>, idOfC<TextureCollider2D>},
        {"Joint", hasIt<Joint2D>,
         [](EntityId e, Pack& p) {
             const Joint2D& j = *get<Joint2D>(e);
             p.object([&] {
                 p.kv("kind", jointKinds[int(j.type)]);
                 p.id("target", j.target);
                 p.key("anchor");
                 vec(p, j.anchor);
                 p.key("target_anchor");
                 vec(p, j.targetAnchor);
                 p.kv("length", j.length);
                 p.kv("min_length", j.minLength);
                 p.kv("max_length", j.maxLength);
                 p.kv("stiffness", j.stiffness);
                 p.kv("damping", j.damping);
                 p.kv("limit", j.limit);
                 p.kv("lower", j.lower);
                 p.kv("upper", j.upper);
                 p.kv("motor", j.motor);
                 p.kv("motor_speed", j.motorSpeed);
                 p.kv("max_motor_force", j.maxMotorForce);
                 p.key("axis");
                 vec(p, j.axis);
                 p.kv("collide_connected", j.collideConnected);
             });
         },
         [](EntityId e, const Msg& j) {
             Joint2D d;
             d.type = pick<JointType>(j["kind"], {"Distance", "Revolute", "Prismatic", "Weld"});
             d.target = j["target"].value("id", EntityId(0));
             if (d.target == 0) d.target = InvalidEntityId;
             d.anchor = vec(j["anchor"]);
             d.targetAnchor = vec(j["target_anchor"]);
             d.length = j.value("length", 0.f);
             d.minLength = j.value("min_length", 0.f);
             d.maxLength = j.value("max_length", 0.f);
             d.stiffness = j.value("stiffness", 0.f);
             d.damping = j.value("damping", 0.f);
             d.limit = j.value("limit", false);
             d.lower = j.value("lower", 0.f);
             d.upper = j.value("upper", 0.f);
             d.motor = j.value("motor", false);
             d.motorSpeed = j.value("motor_speed", 0.f);
             d.maxMotorForce = j.value("max_motor_force", 0.f);
             d.axis = vec(j["axis"], {1, 0});
             d.collideConnected = j.value("collide_connected", false);
             put(e, d);
         },
         removeIt<Joint2D>, idOfC<Joint2D>},
        {"AudioSource", hasIt<AudioSource>,
         [](EntityId e, Pack& p) {
             const AudioSource& a = *get<AudioSource>(e);
             p.object([&] {
                 p.id("sound", a.sound.id);
                 p.id("bus", a.bus.id);
                 p.kv("volume", a.volume);
                 p.kv("pitch", a.pitch);
                 p.kv("looping", a.loop);
                 p.kv("autoplay", a.autoplay);
                 p.kv("spatial", a.spatial);
                 p.kv("min_distance", a.minDistance);
                 p.kv("max_distance", a.maxDistance);
                 p.kv("rolloff", a.rolloff == Rolloff::Inverse ? "Inverse" : "Linear");
                 p.kv("priority", a.priority);
                 p.kv("play", a.playRequested);
             });
         },
         [](EntityId e, const Msg& j) {
             AudioSource a;
             const AudioSource* cur = get<AudioSource>(e);
             if (cur) a = *cur;   // keeps the playback the engine started
             a.sound = SoundHandle{idOf(j["sound"])};
             a.bus = BusHandle{idOf(j["bus"])};
             a.volume = j.value("volume", 1.f);
             a.pitch = j.value("pitch", 1.f);
             a.loop = j.value("looping", false);
             a.autoplay = j.value("autoplay", true);
             a.spatial = j.value("spatial", false);
             a.minDistance = j.value("min_distance", 64.f);
             a.maxDistance = j.value("max_distance", 1024.f);
             a.rolloff = pick<Rolloff>(j["rolloff"], {"Linear", "Inverse"});
             a.priority = j.value("priority", 0);
             a.playRequested = j.value("play", false) || (cur && cur->sound != a.sound);
             put(e, a);
         },
         removeIt<AudioSource>, idOfC<AudioSource>},
        {"SpatialBounds", hasIt<SpatialBounds>,
         [](EntityId e, Pack& p) {
             const SpatialBounds& b = *get<SpatialBounds>(e);
             p.object([&] {
                 p.key("half_size");
                 vec(p, b.halfSize);
                 p.key("offset");
                 vec(p, b.offset);
             });
         },
         [](EntityId e, const Msg& j) { put(e, SpatialBounds{vec(j["half_size"]), vec(j["offset"])}); },
         removeIt<SpatialBounds>, idOfC<SpatialBounds>},
        {"AudioListener", hasIt<AudioListener>,
         [](EntityId e, Pack& p) { p.object([&] { p.kv("active", get<AudioListener>(e)->active); }); },
         [](EntityId e, const Msg& j) { put(e, AudioListener{j.value("active", true)}); },
         removeIt<AudioListener>, idOfC<AudioListener>},
        {"Lifetime", hasIt<Lifetime>,
         [](EntityId e, Pack& p) { p.object([&] { p.kv("seconds", get<Lifetime>(e)->seconds); }); },
         [](EntityId e, const Msg& j) { put(e, Lifetime{j.value("seconds", 1.f)}); },
         removeIt<Lifetime>, idOfC<Lifetime>},
        {"Oscillate", hasIt<Oscillate>,
         [](EntityId e, Pack& p) {
             const Oscillate& o = *get<Oscillate>(e);
             p.object([&] {
                 p.key("amplitude");
                 vec(p, o.amplitude);
                 p.kv("frequency", o.frequency);
                 p.kv("phase", o.phase);
             });
         },
         [](EntityId e, const Msg& j) {
             Oscillate o;
             if (const Oscillate* cur = get<Oscillate>(e)) o = *cur;
             o.amplitude = vec(j["amplitude"], {0, 8});
             o.frequency = j.value("frequency", 1.f);
             o.phase = j.value("phase", 0.f);
             put(e, o);
         },
         removeIt<Oscillate>, idOfC<Oscillate>},
        {"Follow", hasIt<Follow>,
         [](EntityId e, Pack& p) {
             const Follow& f = *get<Follow>(e);
             p.object([&] {
                 p.id("target", f.target == InvalidEntityId ? 0 : f.target);
                 p.kv("speed", f.speed);
                 p.kv("distance", f.distance);
                 p.key("offset");
                 vec(p, f.offset);
             });
         },
         [](EntityId e, const Msg& j) {
             Follow f;
             EntityId t = j["target"].value("id", EntityId(0));
             f.target = t == 0 ? InvalidEntityId : t;
             f.speed = j.value("speed", 100.f);
             f.distance = j.value("distance", 0.f);
             f.offset = vec(j["offset"]);
             put(e, f);
         },
         removeIt<Follow>, idOfC<Follow>},
        {"Orbit", hasIt<Orbit>,
         [](EntityId e, Pack& p) {
             const Orbit& o = *get<Orbit>(e);
             p.object([&] {
                 p.id("center", o.center == InvalidEntityId ? 0 : o.center);
                 p.key("point");
                 vec(p, o.point);
                 p.kv("radius", o.radius);
                 p.kv("speed", o.speed);
                 p.kv("angle", o.angle);
             });
         },
         [](EntityId e, const Msg& j) {
             Orbit o;
             EntityId c = j["center"].value("id", EntityId(0));
             o.center = c == 0 ? InvalidEntityId : c;
             o.point = vec(j["point"]);
             o.radius = j.value("radius", 64.f);
             o.speed = j.value("speed", 1.f);
             o.angle = j.value("angle", 0.f);
             put(e, o);
         },
         removeIt<Orbit>, idOfC<Orbit>},
        {"ParticleEmitter", hasIt<ParticleEmitter>,
         [](EntityId e, Pack& p) { emitterJson(p, *get<ParticleEmitter>(e)); },
         [](EntityId e, const Msg& j) {
             ParticleEmitter pe;
             pe.config = emitterConfig(j);
             pe.playing = j.value("playing", true);
             put(e, pe);
         },
         removeIt<ParticleEmitter>, idOfC<ParticleEmitter>},
        {"Tween",
         [](EntityId e) { return g_tweens.count(e) > 0; },
         [](EntityId e, Pack& p) { p.raw(g_tweens.at(e).value); },
         [](EntityId e, const Msg& j) { startTween(e, j); },
         [](EntityId e) { stopTween(e); },
         idOfC<Transform2D>},
        {"Name", hasIt<Name>,
         [](EntityId e, Pack& p) { p.object([&] { p.kv("value", std::string_view(get<Name>(e)->value)); }); },
         [](EntityId e, const Msg& j) {
             Name n;
             std::string v = j.value("value", "");
             std::snprintf(n.value, sizeof(n.value), "%s", v.c_str());
             put(e, n);
         },
         removeIt<Name>, idOfC<Name>},
        {"Tilemap", hasIt<Tilemap>,
         [](EntityId e, Pack& p) {
             const Tilemap& t = *get<Tilemap>(e);
             p.object([&] {
                 p.id("map", t.map.id);
                 p.kv("z", t.zOrder);
                 p.kv("layer", t.renderLayer);
                 p.kv("colliders", t.colliders);
                 p.kv("visible", t.visible);
             });
         },
         [](EntityId e, const Msg& j) {
             Tilemap t;
             if (const Tilemap* cur = get<Tilemap>(e)) t = *cur;
             t.map = TilemapHandle{idOf(j["map"])};
             t.zOrder = j.value("z", 0.f);
             t.renderLayer = layer(j);
             t.colliders = j.value("colliders", true);
             t.visible = j.value("visible", true);
             put(e, t);
         },
         removeIt<Tilemap>, idOfC<Tilemap>},
        {"Parent", hasParent, readParent, writeParent, removeParentKeepingPlace, idOfC<Parent>},
        {"GlobalTransform", hasIt<Transform2D>, readGlobal, writeGlobal, [](EntityId) {}, idOfC<Transform2D>},
    };
    for (Codec& c : presentationCodecs()) l.push_back(c);
    for (Codec& c : effectCodecs()) l.push_back(c);
    for (Codec& c : animationCodecs()) l.push_back(c);
    return l;
    }();
    return list;
}

// The drift module names a struct `Sprite`, or `drift::Sprite` when a game
// declares its own of that name.
const Codec* codec(const char* component) {
    static const std::unordered_map<std::string_view, const Codec*> byName = [] {
        std::unordered_map<std::string_view, const Codec*> m;
        for (const auto& c : codecs()) m.emplace(c.name, &c);
        return m;
    }();
    std::string_view n = component ? component : "";
    if (n.substr(0, 7) == "drift::") n.remove_prefix(7);
    auto it = byName.find(n);
    return it != byName.end() ? it->second : nullptr;
}

// ---- Random numbers, noise, generators, pathfinding and tilemaps ----

// A candela Rng is the engine's generator state as (hi, lo, inc).
Rng rngOf(const json& a) {
    uint64_t hi = static_cast<uint32_t>(a[0].get<int64_t>()), lo = static_cast<uint32_t>(a[1].get<int64_t>());
    return Rng::fromState((hi << 32) | lo, static_cast<uint64_t>(a[2].get<int64_t>()));
}
void pushState(json& out, const Rng& r) {
    out.push_back(static_cast<int64_t>(r.state() >> 32));
    out.push_back(static_cast<int64_t>(r.state() & 0xffffffffu));
}
json stateOf(const Rng& r) {
    json out = json::array();
    pushState(out, r);
    out.push_back(static_cast<int64_t>(r.increment()));
    return out;
}

Grid gridOf(const json& w, const json& h, const json& cells) {
    Grid g(w.get<int32_t>(), h.get<int32_t>());
    for (size_t i = 0; i < g.cells.size() && i < cells.size(); ++i) g.cells[i] = cells[i].get<int32_t>();
    return g;
}
// [hi, lo] of the generator, then the grid's width, height and cells.
json gridAnswer(const Rng& r, const Grid& g) {
    json out = json::array();
    pushState(out, r);
    out.push_back(g.width);
    out.push_back(g.height);
    for (int32_t v : g.cells) out.push_back(v);
    return out;
}
std::vector<int32_t> ints(const json& j) {
    std::vector<int32_t> v;
    for (const auto& x : j) v.push_back(x.get<int32_t>());
    return v;
}

// Noise fields by seed; a field builds its permutation table once.
const Noise& noiseFor(int64_t seed) {
    static std::unordered_map<int64_t, Noise> cache;
    auto it = cache.find(seed);
    if (it == cache.end()) it = cache.emplace(seed, Noise(static_cast<uint32_t>(seed))).first;
    return it->second;
}
NoiseOptions noiseOptions(const json& a) {
    NoiseOptions o;
    o.type = static_cast<NoiseType>(std::clamp(a[1].get<int>(), 0, 2));
    o.frequency = a[2].get<float>();
    o.octaves = a[3].get<int>();
    o.lacunarity = a[4].get<float>();
    o.gain = a[5].get<float>();
    o.warp = a[6].get<float>();
    o.warpFrequency = a[7].get<float>();
    return o;
}

TilemapHandle mapOf(const json& j) { return TilemapHandle{j.get<uint32_t>()}; }

bool procgen(const std::string& f, const json& a, json& out) {
    if (f == "rng_seed") {
        uint64_t stream = static_cast<uint64_t>(a[1].get<int64_t>());
        out = stateOf(stream ? Rng(static_cast<uint64_t>(a[0].get<int64_t>()), stream)
                             : Rng(static_cast<uint64_t>(a[0].get<int64_t>())));
        return true;
    }
    if (f == "rng_int") {
        Rng r = rngOf(a);
        std::string op = a[3].get<std::string>();
        int64_t x = a[4].get<int64_t>(), y = a[5].get<int64_t>();
        json vals = json::array();
        if (op == "next_int") vals.push_back(r.nextU32());
        else if (op == "below") vals.push_back(r.below(static_cast<uint32_t>(std::max<int64_t>(0, x))));
        else if (op == "range") vals.push_back(r.range(static_cast<int32_t>(x), static_cast<int32_t>(y)));
        else if (op == "shuffle") {
            std::vector<int32_t> order(static_cast<size_t>(std::max<int64_t>(0, x)));
            for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int32_t>(i);
            r.shuffle(std::span<int32_t>(order));
            for (int32_t i : order) vals.push_back(i);
        } else if (op == "split") {
            vals = stateOf(r.split());
        }
        out = json::array();
        pushState(out, r);
        for (auto& v : vals) out.push_back(v);
        return true;
    }
    if (f == "rng_float") {
        Rng r = rngOf(a);
        std::string op = a[3].get<std::string>();
        float x = a[4].get<float>(), y = a[5].get<float>();
        json vals = json::array();
        if (op == "next_float") vals.push_back(r.next01());
        else if (op == "range") vals.push_back(r.range(x, y));
        else if (op == "chance") vals.push_back(r.chance(x) ? 1.0 : 0.0);
        else if (op == "sign") vals.push_back(r.sign());
        else if (op == "angle") vals.push_back(r.angle());
        else if (op == "unit_vector" || op == "inside_circle") {
            Vec2 v = op == "unit_vector" ? r.unitVector() : r.insideCircle(x);
            vals.push_back(v.x);
            vals.push_back(v.y);
        } else if (op == "normal") vals.push_back(r.normal(x, y));
        out = json::array();
        pushState(out, r);
        for (auto& v : vals) out.push_back(v);
        return true;
    }
    if (f == "rng_weighted") {
        Rng r = rngOf(a);
        std::vector<float> w;
        for (const auto& x : a[3]) w.push_back(x.get<float>());
        int32_t i = r.weightedIndex(w);
        out = json::array();
        pushState(out, r);
        out.push_back(i);
        return true;
    }
    if (f == "hash_text") {
        out = static_cast<int64_t>(Rng::hash(a[0].get<std::string>()));
        return true;
    }
    if (f == "random_seed" || f == "random_stream") {
        auto* random = res<Random>();
        if (f == "random_seed") out = random ? static_cast<int64_t>(random->seed()) : int64_t(0);
        else out = stateOf(random ? random->stream(a[0].get<std::string>()) : Rng());
        return true;
    }
    if (f == "noise_sample") {
        out = noiseFor(a[0].get<int64_t>()).sample(noiseOptions(a), a[8].get<float>(), a[9].get<float>());
        return true;
    }
    if (f == "noise_grid") {
        const Noise& n = noiseFor(a[0].get<int64_t>());
        NoiseOptions o = noiseOptions(a);
        float x0 = a[8].get<float>(), y0 = a[9].get<float>(), step = a[12].get<float>();
        int w = a[10].get<int>(), h = a[11].get<int>();
        out = json::array();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) out.push_back(n.sample(o, x0 + x * step, y0 + y * step));
        return true;
    }
    if (f == "noise_cells") {
        Grid g = gridFromNoise(static_cast<uint32_t>(a[0].get<int64_t>()), a[8].get<int32_t>(), a[9].get<int32_t>(),
                               noiseOptions(a), a[10].get<float>(), a[11].get<int32_t>(), a[12].get<int32_t>());
        out = g.cells;
        return true;
    }
    if (f == "gen_cave") {
        Rng r = rngOf(a);
        CaveOptions o;
        o.fill = a[5].get<float>();
        o.steps = a[6].get<int32_t>();
        o.birth = a[7].get<int32_t>();
        o.survive = a[8].get<int32_t>();
        o.solidEdges = a[9].get<bool>();
        o.keepLargest = a[10].get<bool>();
        Grid g = generateCave(r, a[3].get<int32_t>(), a[4].get<int32_t>(), o);
        out = gridAnswer(r, g);
        return true;
    }
    if (f == "gen_dungeon") {
        Rng r = rngOf(a);
        DungeonOptions o;
        o.minRoom = a[5].get<int32_t>();
        o.maxRoom = a[6].get<int32_t>();
        o.padding = a[7].get<int32_t>();
        o.corridorWidth = a[8].get<int32_t>();
        o.extraCorridors = a[9].get<int32_t>();
        o.doors = a[10].get<bool>();
        Dungeon d = generateDungeon(r, a[3].get<int32_t>(), a[4].get<int32_t>(), o);
        out = gridAnswer(r, d.grid);
        out.push_back(d.rooms.size());
        for (const GridRect& room : d.rooms) {
            out.push_back(room.x);
            out.push_back(room.y);
            out.push_back(room.w);
            out.push_back(room.h);
        }
        return true;
    }
    if (f == "gen_walk") {
        Rng r = rngOf(a);
        WalkOptions o;
        o.coverage = a[5].get<float>();
        o.walkers = a[6].get<int32_t>();
        o.turnChance = a[7].get<float>();
        o.brush = a[8].get<int32_t>();
        o.start = {a[9].get<int32_t>(), a[10].get<int32_t>()};
        Grid g = randomWalk(r, a[3].get<int32_t>(), a[4].get<int32_t>(), o);
        out = gridAnswer(r, g);
        return true;
    }
    if (f == "gen_poisson" || f == "gen_poisson_grid") {
        Rng r = rngOf(a);
        std::vector<Vec2> pts;
        if (f == "gen_poisson") {
            Rect area{a[3].get<float>(), a[4].get<float>(), a[5].get<float>(), a[6].get<float>()};
            pts = poissonDisk(r, area, a[7].get<float>(), a[8].get<int32_t>());
        } else {
            Grid g = gridOf(a[3], a[4], a[5]);
            pts = poissonDiskOnGrid(r, g, a[6].get<int32_t>(), a[7].get<float>(), a[8].get<int32_t>());
        }
        out = json::array();
        pushState(out, r);
        for (Vec2 p : pts) {
            out.push_back(p.x);
            out.push_back(p.y);
        }
        return true;
    }
    if (f == "gen_wfc") {
        Rng r = rngOf(a);
        Grid sample = gridOf(a[3], a[4], a[5]);
        int32_t w = a[6].get<int32_t>(), h = a[7].get<int32_t>();
        WfcOptions o;
        o.attempts = a[8].get<int32_t>();
        o.periodic = a[9].get<bool>();
        if (a[10].size() == static_cast<size_t>(w) * static_cast<size_t>(h)) o.preset = gridOf(json(w), json(h), a[10]);
        Grid g = generateWfc(r, WfcRules::fromSample(sample, o.periodic), w, h, o);
        out = gridAnswer(r, g);
        return true;
    }
    if (f == "grid_op") {
        Grid g = gridOf(a[0], a[1], a[2]);
        std::string op = a[3].get<std::string>();
        int32_t x = a[4].get<int32_t>(), y = a[5].get<int32_t>(), z = a[6].get<int32_t>();
        if (op == "keep_largest") keepLargestRegion(g, x, y);
        else if (op == "remove_small") removeSmallRegions(g, x, static_cast<size_t>(std::max(0, y)), z);
        else if (op == "connect") connectRegions(g, x, y);
        else if (op == "flood") g = floodFill(g, {x, y});
        else if (op == "regions") {
            out = json::array();
            for (const auto& region : findRegions(g, x)) out.push_back(region.size());
            return true;
        }
        out = g.cells;
        return true;
    }
    if (f == "path_find") {
        Grid g = gridOf(a[0], a[1], a[2]);
        NavGrid nav = navGridFrom(g, ints(a[3]));
        PathOptions o;
        o.diagonal = a[8].get<bool>() ? DiagonalMode::NoCornerCutting : DiagonalMode::Never;
        out = json::array();
        for (GridPoint p : findPath(nav, {a[4].get<int32_t>(), a[5].get<int32_t>()},
                                    {a[6].get<int32_t>(), a[7].get<int32_t>()}, o)) {
            out.push_back(p.x);
            out.push_back(p.y);
        }
        return true;
    }
    if (f == "flow_field") {
        Grid g = gridOf(a[0], a[1], a[2]);
        NavGrid nav = navGridFrom(g, ints(a[3]));
        std::vector<GridPoint> goals;
        for (size_t i = 0; i + 1 < a[4].size(); i += 2) goals.push_back({a[4][i].get<int32_t>(), a[4][i + 1].get<int32_t>()});
        FlowField field(nav, goals, a[5].get<bool>() ? DiagonalMode::NoCornerCutting : DiagonalMode::Never);
        out = json::array();
        for (int32_t y = 0; y < g.height; ++y)
            for (int32_t x = 0; x < g.width; ++x) {
                GridPoint n = field.next({x, y});
                out.push_back(n == GridPoint{x, y} ? -1 : n.y * g.width + n.x);
            }
        for (int32_t y = 0; y < g.height; ++y)
            for (int32_t x = 0; x < g.width; ++x) {
                float c = field.cost({x, y});
                out.push_back(std::isfinite(c) ? c : -1.f);
            }
        return true;
    }
    if (f.rfind("map_", 0) != 0) return false;
    auto* maps = res<TilemapResource>();
    if (!maps) {
        out = nullptr;
        return true;
    }
    if (f == "map_load") {
        std::string level = a[1].get<std::string>();
        out = maps->loadTilemap(a[0].get<std::string>().c_str(), level.empty() ? nullptr : level.c_str()).id;
    } else if (f == "map_create") {
        TilemapDesc d;
        d.width = a[0].get<int32_t>();
        d.height = a[1].get<int32_t>();
        d.tileWidth = a[2].get<int32_t>();
        d.tileHeight = a[3].get<int32_t>();
        d.tileset = TextureHandle{a[4].get<uint32_t>()};
        d.layers.clear();
        for (const auto& n : a[5]) d.layers.push_back(n.get<std::string>());
        if (d.layers.empty()) d.layers.push_back("Tiles");
        out = maps->createTilemap(d).id;
    } else if (f == "map_info") {
        TilemapHandle m = mapOf(a[0]);
        Rect b = maps->bounds(m);
        Vec2 o = maps->origin(m);
        out = {maps->mapWidth(m), maps->mapHeight(m), maps->tileWidth(m), maps->tileHeight(m),
               o.x + b.x, o.y + b.y, b.w, b.h, maps->layerCount(m)};
    } else if (f == "map_layer") {
        out = maps->findLayer(mapOf(a[0]), a[1].get<std::string>());
    } else if (f == "map_layer_name") {
        out = std::string(maps->layerName(mapOf(a[0]), a[1].get<int32_t>()));
    } else if (f == "map_layer_visible") {
        maps->setLayerVisible(mapOf(a[0]), a[1].get<int32_t>(), a[2].get<bool>());
        out = nullptr;
    } else if (f == "map_layer_solid") {
        maps->setLayerCollision(mapOf(a[0]), a[1].get<int32_t>(), a[2].get<bool>(), ints(a[3]));
        out = nullptr;
    } else if (f == "map_tile") {
        TilemapHandle m = mapOf(a[0]);
        int32_t l = a[1].get<int32_t>(), x = a[2].get<int32_t>(), y = a[3].get<int32_t>();
        out = a[4].get<int>() == 1 ? maps->getIntGrid(m, l, x, y) : static_cast<int32_t>(maps->getTile(m, l, x, y));
    } else if (f == "map_set_tile") {
        TilemapHandle m = mapOf(a[0]);
        int32_t l = a[1].get<int32_t>(), x = a[2].get<int32_t>(), y = a[3].get<int32_t>(), v = a[4].get<int32_t>();
        if (a[5].get<int>() == 1) maps->setIntGrid(m, l, x, y, v);
        else maps->setTile(m, l, x, y, static_cast<uint32_t>(v));
        out = nullptr;
    } else if (f == "map_tile_at") {
        out = static_cast<int32_t>(maps->tileAt(mapOf(a[0]), a[1].get<int32_t>(), {a[2].get<float>(), a[3].get<float>()}));
    } else if (f == "map_world_to_tile") {
        TileCoord c = maps->worldToTile(mapOf(a[0]), {a[1].get<float>(), a[2].get<float>()}, a[3].get<int32_t>());
        out = {c.x, c.y};
    } else if (f == "map_tile_to_world") {
        Vec2 p = maps->tileToWorld(mapOf(a[0]), {a[1].get<int32_t>(), a[2].get<int32_t>()}, a[3].get<int32_t>());
        out = {p.x, p.y};
    } else if (f == "map_fill") {
        Grid g = gridOf(a[2], a[3], a[4]);
        std::vector<uint32_t> tiles;
        for (const auto& t : a[5]) tiles.push_back(static_cast<uint32_t>(std::max(0, t.get<int32_t>())));
        fillTilemapLayer(*maps, mapOf(a[0]), a[1].get<int32_t>(), g, tiles, {a[6].get<int32_t>(), a[7].get<int32_t>()});
        out = nullptr;
    } else if (f == "map_read") {
        out = gridFromTilemapLayer(*maps, mapOf(a[0]), a[1].get<int32_t>()).cells;
    } else if (f == "map_destroy") {
        maps->destroyTilemap(mapOf(a[0]));
        out = nullptr;
    } else if (f == "map_property") {
        out = maps->properties(mapOf(a[0])).getString(a[1].get<std::string>());
    } else {
        return false;
    }
    return true;
}

// ---- Rendering and presentation ----
//
// Cameras as components, and the services behind Camera2D, Draw and the
// particle controls of Effects.

json entityJ(EntityId e) { return {{"id", e}}; }
EntityId entityOf(const json& j) { return j.is_object() ? j.value("id", EntityId(0)) : EntityId(0); }
uint32_t maskOf(const json& j) { return static_cast<uint32_t>(j.value("layer_mask", int64_t(AllWorldLayers))); }
float num(const json& a, size_t i, float fallback = 0.f) {
    return i < a.size() && a[i].is_number() ? a[i].get<float>() : fallback;
}

Camera cameraOf(const Msg& j) {
    Camera c;
    c.zoom = j.value("zoom", 1.f);
    c.rotation = j.value("rotation", 0.f);
    c.active = j.value("active", true);
    c.viewport = j.contains("viewport") ? rectJ(j["viewport"]) : Rect{0.f, 0.f, 1.f, 1.f};
    c.order = j.value("order", 0);
    c.layerMask = static_cast<uint32_t>(j.value("layer_mask", int64_t(AllWorldLayers)));
    c.target = TextureHandle{idOf(j["target"])};
    c.clear = j.value("clear", false);
    c.clearColor = colorF(j["clear_color"], {0.f, 0.f, 0.f, 1.f});
    return c;
}

// A renderer camera that sees what a Camera entity sees, for conversions:
// the main camera for entity 0 or one without a Camera.
CameraHandle viewOf(EntityId e) {
    auto* r = res<RendererResource>();
    if (!r) return {};
    const Camera* c = e ? get<Camera>(e) : nullptr;
    if (!c) return r->getDefaultCamera();
    static CameraHandle scratch;
    CameraDesc d;
    if (const auto* t = get<Transform2D>(e)) d.position = t->position;
    d.zoom = c->zoom;
    d.rotation = c->rotation;
    d.viewport = c->viewport;
    d.enabled = false;   // never drawn
    if (!scratch.valid()) scratch = r->createCamera(d);
    r->setCamera(scratch, d);
    return scratch;
}

std::vector<Codec> presentationCodecs() {
    return {
        {"Camera", hasIt<Camera>,
         [](EntityId e, Pack& p) {
             const Camera& c = *get<Camera>(e);
             p.object([&] {
                 p.kv("zoom", c.zoom);
                 p.kv("rotation", c.rotation);
                 p.kv("active", c.active);
                 p.key("viewport");
                 rectJ(p, c.viewport);
                 p.kv("order", c.order);
                 p.kv("layer_mask", c.layerMask);
                 p.id("target", c.target.id);
                 p.kv("clear", c.clear);
                 p.key("clear_color");
                 colorF(p, c.clearColor);
             });
         },
         [](EntityId e, const Msg& j) { put(e, cameraOf(j)); },
         removeIt<Camera>, idOfC<Camera>},
        {"CameraFollow", hasIt<CameraFollow>,
         [](EntityId e, Pack& p) {
             const CameraFollow& f = *get<CameraFollow>(e);
             p.object([&] {
                 p.id("target", f.target);
                 p.kv("smoothing", f.smoothing);
                 p.key("offset");
                 vec(p, f.offset);
                 p.key("dead_zone");
                 vec(p, f.deadZone);
             });
         },
         [](EntityId e, const Msg& j) {
             CameraFollow f;
             f.target = j["target"].value("id", EntityId(0));
             f.smoothing = j.value("smoothing", 5.f);
             f.offset = vec(j["offset"]);
             f.deadZone = vec(j["dead_zone"]);
             put(e, f);
         },
         removeIt<CameraFollow>, idOfC<CameraFollow>},
        {"CameraBounds", hasIt<CameraBounds>,
         [](EntityId e, Pack& p) {
             p.object([&] {
                 p.key("area");
                 rectJ(p, get<CameraBounds>(e)->area);
             });
         },
         [](EntityId e, const Msg& j) { put(e, CameraBounds{rectJ(j["area"])}); },
         removeIt<CameraBounds>, idOfC<CameraBounds>},
    };
}

// The style of a candela Text value, for drawing and measuring.
TextStyle textStyle(const json& j) {
    TextStyle t;
    t.size = j.value("size", 24.f);
    t.color = color(j.value("color", json()));
    t.align = pick<TextAlign>(j.value("align", json()), {"Left", "Center", "Right"});
    t.valign = pick<TextVAlign>(j.value("valign", json()), {"Top", "Middle", "Bottom"});
    t.wrapWidth = j.value("wrap_width", 0.f);
    t.lineSpacing = j.value("line_spacing", 1.f);
    t.outlineWidth = j.value("outline", 0.f);
    t.outlineColor = color(j.value("outline_color", json()), Color::black());
    t.shadowOffset = vec(j.value("shadow_offset", json()));
    t.shadowColor = color(j.value("shadow_color", json()), Color{0, 0, 0, 160});
    t.richText = j.value("rich_text", false);
    return t;
}

// Immediate draws on `layer`, leaving the renderer's own layer as it was.
template <typename F> void onLayer(RendererResource& r, int layer, F&& draw) {
    uint8_t before = r.layer();
    r.setLayer(static_cast<uint8_t>(std::clamp(layer, 0, 31)));
    draw();
    r.setLayer(before);
}

void drawShape(RendererResource& r, const std::string& kind, float z, const json& n, Color c) {
    auto p = [&](size_t i) { return Vec2{num(n, i), num(n, i + 1)}; };
    auto points = [&](size_t from) {
        std::vector<Vec2> out;
        for (size_t i = from; i + 1 < n.size(); i += 2) out.push_back(p(i));
        return out;
    };
    Rect rc{num(n, 0), num(n, 1), num(n, 2), num(n, 3)};
    if (kind == "rect") r.drawRectFilled(rc, c, z);
    else if (kind == "rect_lines") r.drawRect(rc, c, num(n, 4, 1.f), z);
    else if (kind == "rounded_rect") r.drawRoundedRectFilled(rc, num(n, 4), c, z);
    else if (kind == "circle") r.drawCircleFilled(p(0), num(n, 2), c, 0, z);
    else if (kind == "circle_lines") r.drawCircle(p(0), num(n, 2), c, num(n, 3, 1.f), 0, z);
    else if (kind == "arc") r.drawArc(p(0), num(n, 2), num(n, 3), num(n, 4), c, num(n, 5, 1.f), z);
    else if (kind == "line") r.drawLine(p(0), p(2), c, num(n, 4, 1.f), z);
    else if (kind == "triangle") r.drawTriangleFilled(p(0), p(2), p(4), c, z);
    else if (kind == "polyline") {
        std::vector<Vec2> pts = points(2);
        r.drawPolyline(pts.data(), static_cast<int32_t>(pts.size()), c, num(n, 0, 1.f), num(n, 1) > 0.5f, z);
    } else if (kind == "polygon") {
        std::vector<Vec2> pts = points(0);
        r.drawPolygonFilled(pts.data(), static_cast<int32_t>(pts.size()), c, z);
    } else {
        DRIFT_LOG_WARN("Candela: Draw has no shape '%s'", kind.c_str());
    }
}

bool presentation(const std::string& f, const json& a, json& out) {
    if (f == "camera_point" || f == "camera_view") {
        auto* r = res<RendererResource>();
        CameraHandle cam = viewOf(a[0].get<EntityId>());
        if (!r) {
            out = json::array();
        } else if (f == "camera_view") {
            Rect b = r->cameraWorldBounds(cam);
            out = {b.x, b.y, b.w, b.h};
        } else {
            Vec2 p{a[2].get<float>(), a[3].get<float>()};
            Vec2 v = a[1].get<int>() == 0 ? r->screenToWorld(cam, p) : r->worldToScreen(cam, p);
            out = {v.x, v.y};
        }
        return true;
    }
    if (f.rfind("draw_", 0) == 0) {
        out = nullptr;
        auto* r = res<RendererResource>();
        if (!r) return true;
        if (f == "draw_shape") {
            Color c = color(a[4].is_array() && a[4].size() == 4
                                ? json{{"r", a[4][0]}, {"g", a[4][1]}, {"b", a[4][2]}, {"a", a[4][3]}}
                                : json());
            onLayer(*r, a[1].get<int>(), [&] { drawShape(*r, a[0].get<std::string>(), a[2].get<float>(), a[3], c); });
        } else if (f == "draw_text") {
            // text, position, Text style, layer, z
            auto* fonts = res<FontResource>();
            if (!fonts) return true;
            TextStyle style = textStyle(a[2]);
            style.layer = static_cast<uint8_t>(std::clamp(a[3].get<int>(), 0, 31));
            style.zOrder = a[4].get<float>();
            fonts->drawText(FontHandle{idOf(a[2].value("font", json()))}, a[0].get<std::string>(), vec(a[1]), style);
        } else if (f == "draw_sprite") {
            // Sprite, Transform, layer, z
            const json& s = a[0];
            const json& t = a[1];
            SpriteDraw d;
            d.texture = TextureHandle{idOf(s.value("image", json()))};
            d.srcRect = rectJ(s.value("frame", json()));
            Vec2 size = d.srcRect.w > 0 ? Vec2{d.srcRect.w, d.srcRect.h} : textureSize(d.texture);
            Vec2 pivot = vec(s.value("pivot", json()), {0.5f, 0.5f});
            d.origin = {pivot.x * size.x, pivot.y * size.y};
            d.position = vec(t.value("position", json()));
            d.rotation = t.value("rotation", 0.f);
            d.scale = vec(t.value("scale", json()), {1, 1});
            d.tint = color(s.value("color", json()));
            d.flip = static_cast<Flip>((s.value("flip_x", false) ? int(Flip::H) : 0) | (s.value("flip_y", false) ? int(Flip::V) : 0));
            d.blend = blend(s.value("blend", json()));
            d.material = MaterialHandle{idOf(s.value("material", json()))};
            d.layer = static_cast<uint8_t>(std::clamp(a[2].get<int>(), 0, 31));
            d.zOrder = a[3].get<float>();
            if (d.texture.valid()) r->draw(d);
        } else if (f == "draw_nine") {
            TextureHandle tex{a[0].get<uint32_t>()};
            const json& n = a[1];
            Rect dest{num(n, 0), num(n, 1), num(n, 2), num(n, 3)};
            Rect src{num(n, 4), num(n, 5), num(n, 6), num(n, 7)};
            if (src.w <= 0.f || src.h <= 0.f) {
                Vec2 size = textureSize(tex);
                src = {0.f, 0.f, size.x, size.y};
            }
            Vec4 border{num(n, 8), num(n, 9), num(n, 10), num(n, 11)};
            const json& c = a[2];
            Color tint = color(json{{"r", num(c, 0, 1.f)}, {"g", num(c, 1, 1.f)}, {"b", num(c, 2, 1.f)}, {"a", num(c, 3, 1.f)}});
            onLayer(*r, a[3].get<int>(), [&] { r->drawNineSlice(tex, dest, src, border, tint, a[4].get<float>()); });
        } else if (f == "draw_clip") {
            if (a[0].size() == 4) r->pushClipRect({num(a[0], 0), num(a[0], 1), num(a[0], 2), num(a[0], 3)});
            else r->popClipRect();
        } else {
            return false;
        }
        return true;
    }
    if (f == "particles_restart") {
        // The emitter's run state goes; the particle system starts it again.
        if (auto* p = res<ParticleSystemResource>()) p->emitters.erase(a[0].get<EntityId>());
        out = nullptr;
        return true;
    }
    return false;
}

// ---- Images ----
//
// Pixel buffers the engine holds for scripts, by id: made, loaded or read
// back from a render target, changed, then shown through a texture.

std::unordered_map<int64_t, Image> g_images;
int64_t g_nextImage = 1;
std::vector<std::pair<std::string, int>> g_downloads;   // path, frames left to wait for it

Color colorOf(const json& c, Color fallback = {0, 0, 0, 0}) {
    if (!c.is_array() || c.size() < 4) return fallback;
    return {channel(c[0].get<double>()), channel(c[1].get<double>()), channel(c[2].get<double>()), channel(c[3].get<double>())};
}
json colorList(Color c) { return {c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0}; }
Rect rectOf(const json& r) { return {num(r, 0), num(r, 1), num(r, 2), num(r, 3)}; }
int64_t keepImage(Image img) {
    if (img.empty()) return 0;
    g_images[g_nextImage] = std::move(img);
    return g_nextImage++;
}
uint32_t packed(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

#ifdef __EMSCRIPTEN__
// Offers a file of the page's file system as a browser download.
EM_JS(void, drift_offer_download, (const char* path), {
    var p = UTF8ToString(path);
    var data = FS.readFile(p);
    var url = URL.createObjectURL(new Blob([data], {type: 'application/octet-stream'}));
    var a = document.createElement('a');
    a.href = url;
    a.download = p.split('/').pop();
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(function() { URL.revokeObjectURL(url); }, 10000);
});
#endif

// Downloads wait for their file: a screenshot is written at the end of the
// frame that asked for it.
void offerDownloads() {
    for (auto it = g_downloads.begin(); it != g_downloads.end();) {
        std::error_code ec;
        bool there = std::filesystem::is_regular_file(it->first, ec);
        if (there) {
#ifdef __EMSCRIPTEN__
            drift_offer_download(it->first.c_str());
#else
            DRIFT_LOG_INFO("Candela: %s is saved", it->first.c_str());
#endif
        } else if (--it->second > 0) {
            ++it;
            continue;
        } else {
            DRIFT_LOG_WARN("Candela: nothing to download at %s", it->first.c_str());
        }
        it = g_downloads.erase(it);
    }
}

bool images(const std::string& f, const json& a, json& out) {
    if (f.rfind("image_", 0) != 0 && f != "texture_update" && f != "download" && f != "window_screenshot" &&
        f != "render_target") return false;
    auto* r = res<RendererResource>();
    out = nullptr;
    if (f == "image_new") {
        out = keepImage(Image(a[0].get<int32_t>(), a[1].get<int32_t>(), colorOf(a[2])));
        return true;
    }
    if (f == "image_load") {
        out = keepImage(Image::load(a[0].get<std::string>().c_str()));
        return true;
    }
    if (f == "image_from_texture") {
        out = r ? keepImage(r->readTexture(TextureHandle{a[0].get<uint32_t>()})) : 0;
        return true;
    }
    if (f == "texture_update") {
        // texture, image, the part of the image (empty: all of it at 0, 0)
        auto it = g_images.find(a[1].get<int64_t>());
        if (!r || it == g_images.end()) return true;
        const Image& img = it->second;
        Rect part = a[2].size() == 4 ? rectOf(a[2]) : Rect{0.f, 0.f, float(img.width()), float(img.height())};
        int32_t x0 = std::max(0, int32_t(part.x)), y0 = std::max(0, int32_t(part.y));
        int32_t x1 = std::min(img.width(), int32_t(part.x + part.w)), y1 = std::min(img.height(), int32_t(part.y + part.h));
        if (x1 <= x0 || y1 <= y0) return true;
        TextureHandle tex{a[0].get<uint32_t>()};
        if (x0 == 0 && y0 == 0 && x1 == img.width() && y1 == img.height()) {
            r->updateTexture(tex, 0, 0, img.width(), img.height(), img.data());
        } else {
            Image sub = img.crop({float(x0), float(y0), float(x1 - x0), float(y1 - y0)});
            r->updateTexture(tex, x0, y0, sub.width(), sub.height(), sub.data());
        }
        return true;
    }
    if (f == "render_target") {
        out = r ? r->createRenderTarget(a[0].get<int>(), a[1].get<int>()).id : 0u;
        return true;
    }
    if (f == "window_screenshot") {
        if (r) r->captureScreenshot(a[0].get<std::string>().c_str());
        return true;
    }
    if (f == "download") {
        g_downloads.push_back({a[0].get<std::string>(), 3});
        return true;
    }
    auto it = g_images.find(a[0].get<int64_t>());
    if (it == g_images.end()) {
        if (f == "image_info" || f == "image_get" || f == "image_read") out = json::array();
        else if (f == "image_crop" || f == "image_texture") out = 0;
        else if (f == "image_save") out = false;
        return true;
    }
    Image& img = it->second;
    if (f == "image_info") out = {img.width(), img.height()};
    else if (f == "image_get") out = colorList(img.get(a[1].get<int32_t>(), a[2].get<int32_t>()));
    else if (f == "image_set") img.set(a[1].get<int32_t>(), a[2].get<int32_t>(), colorOf(a[3]));
    else if (f == "image_op") {
        // image, op, numbers, color, second color
        std::string op = a[1].get<std::string>();
        const json& n = a[2];
        Color c = colorOf(a[3]);
        if (op == "fill") img.fill(c);
        else if (op == "fill_rect") img.fillRect(rectOf(n), c);
        else if (op == "gradient") img.fillGradient(c, colorOf(a[4]), num(n, 0) > 0.5f);
        else if (op == "line") img.drawLine({num(n, 0), num(n, 1)}, {num(n, 2), num(n, 3)}, c, num(n, 4, 1.f));
        else if (op == "rect_lines") img.drawRect(rectOf(n), c, num(n, 4, 1.f));
        else if (op == "circle") img.drawCircle({num(n, 0), num(n, 1)}, num(n, 2), c);
        else if (op == "fill_circle") img.fillCircle({num(n, 0), num(n, 1)}, num(n, 2), c);
        else if (op == "circle_lines") img.drawCircleOutline({num(n, 0), num(n, 1)}, num(n, 2), c, num(n, 3, 1.f));
        else if (op == "flip_x") img.flipX();
        else if (op == "flip_y") img.flipY();
        else if (op == "noise") {
            // seed, type, frequency, octaves, lacunarity, gain, warp, warp
            // frequency: the noise at each pixel, from `c` at -1 to the
            // second color at 1.
            const Noise& noise = noiseFor(static_cast<int64_t>(num(n, 0)));
            NoiseOptions o = noiseOptions(n);
            Color hi = colorOf(a[4]);
            for (int32_t y = 0; y < img.height(); ++y)
                for (int32_t x = 0; x < img.width(); ++x) {
                    float t = std::clamp(noise.sample(o, float(x), float(y)) * 0.5f + 0.5f, 0.f, 1.f);
                    auto lerp = [t](uint8_t u, uint8_t v) { return uint8_t(std::lround(u + (v - u) * t)); };
                    img.set(x, y, {lerp(c.r, hi.r), lerp(c.g, hi.g), lerp(c.b, hi.b), lerp(c.a, hi.a)});
                }
        } else {
            DRIFT_LOG_WARN("Candela: Image has no '%s'", op.c_str());
        }
    } else if (f == "image_blit") {
        // destination, source, x, y, part of the source, blend
        auto src = g_images.find(a[1].get<int64_t>());
        if (src != g_images.end()) {
            img.blit(src->second, a[2].get<int32_t>(), a[3].get<int32_t>(), a[4].size() == 4 ? rectOf(a[4]) : Rect{},
                     a[5].get<bool>());
        }
    } else if (f == "image_crop") {
        Rect part = a[1].size() == 4 ? rectOf(a[1]) : Rect{0.f, 0.f, float(img.width()), float(img.height())};
        out = keepImage(img.crop(part));
    } else if (f == "image_read") {
        // Packed 0xRRGGBBAA pixels of a rect, row by row.
        Rect part = a[1].size() == 4 ? rectOf(a[1]) : Rect{0.f, 0.f, float(img.width()), float(img.height())};
        out = json::array();
        for (int32_t y = int32_t(part.y); y < int32_t(part.y + part.h); ++y)
            for (int32_t x = int32_t(part.x); x < int32_t(part.x + part.w); ++x) {
                Color c = img.get(x, y);
                uint8_t p[4] = {c.r, c.g, c.b, c.a};
                out.push_back(packed(p));
            }
    } else if (f == "image_write") {
        Rect part = a[1].size() == 4 ? rectOf(a[1]) : Rect{0.f, 0.f, float(img.width()), float(img.height())};
        const json& px = a[2];
        size_t i = 0;
        for (int32_t y = int32_t(part.y); y < int32_t(part.y + part.h); ++y)
            for (int32_t x = int32_t(part.x); x < int32_t(part.x + part.w) && i < px.size(); ++x, ++i) {
                uint32_t v = static_cast<uint32_t>(px[i].get<int64_t>());
                img.set(x, y, {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)});
            }
    } else if (f == "image_texture") {
        out = r ? r->createTextureFromPixels(img.data(), img.width(), img.height()).id : 0u;
    } else if (f == "image_save") {
        out = img.savePng(a[1].get<std::string>().c_str());
    } else if (f == "image_free") {
        g_images.erase(it);
    } else {
        return false;
    }
    return true;
}

// ---- Browser and platform ----
//
// The page around a web game (cmake/drift_web.js and the shell page) and the
// matching desktop calls: pointer lock, links, the clipboard, the page's
// query string, local time. Window's fullscreen goes through the page too.

#ifdef __EMSCRIPTEN__
EM_JS(void, web_set_fullscreen, (int on), { if (Module['driftSetFullscreen']) Module['driftSetFullscreen'](!!on); });
EM_JS(int, web_is_fullscreen, (), { return Module['driftIsFullscreen'] ? Module['driftIsFullscreen']() : 0; });
EM_JS(void, web_lock_pointer, (int on), { if (Module['driftLockPointer']) Module['driftLockPointer'](!!on); });
EM_JS(int, web_touch_screen, (), { return Module['driftTouchScreen'] ? 1 : 0; });
EM_JS(void, web_copy, (const char* text), {
    var t = UTF8ToString(text);
    var fallback = function () {
        var area = document.createElement('textarea');
        area.value = t;
        area.style.position = 'fixed';
        area.style.opacity = '0';
        document.body.appendChild(area);
        area.select();
        try { document.execCommand('copy'); } catch (e) {}
        area.remove();
        if (Module['canvas']) Module['canvas'].focus();
    };
    if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(t).catch(fallback);
    else fallback();
});
EM_JS(char*, web_pasted, (), { return stringToNewUTF8(Module['driftPasted'] || ''); });
EM_JS(char*, web_query, (const char* name), {
    var v = new URLSearchParams(window.location.search).get(UTF8ToString(name));
    return stringToNewUTF8(v === null ? '' : v);
});
EM_JS(void, web_show_error, (const char* text), {
    if (Module['driftError']) Module['driftError'](UTF8ToString(text));
});
// Hot reload on pages built with `drift build --web --dev` (drift-dev.js):
// the game runs ("" after a start or reload) or its new code failed (the
// error), and a change that needs the whole page loaded again.
EM_JS(void, web_dev_status, (const char* error), {
    if (Module['driftDevStatus']) Module['driftDevStatus'](UTF8ToString(error));
});
EM_JS(void, web_dev_reload_page, (const char* why), {
    if (Module['driftDevReload']) Module['driftDevReload'](UTF8ToString(why));
});

std::string takeString(char* s) {
    std::string out = s ? s : "";
    free(s);
    return out;
}
#endif

SDL_Window* sdlWindow();

bool platform(const std::string& f, const json& a, json& out) {
    if (f == "window_lock_pointer") {
#ifdef __EMSCRIPTEN__
        web_lock_pointer(a[0].get<bool>());
#else
        if (SDL_Window* w = sdlWindow()) SDL_SetWindowRelativeMouseMode(w, a[0].get<bool>());
#endif
        out = nullptr;
    } else if (f == "pixel_ratio") {
#ifdef __EMSCRIPTEN__
        out = emscripten_get_device_pixel_ratio();
#else
        SDL_Window* w = sdlWindow();
        out = w ? SDL_GetWindowDisplayScale(w) : 1.f;
#endif
    } else if (f == "open_url") {
        if (!SDL_OpenURL(a[0].get<std::string>().c_str())) DRIFT_LOG_WARN("open_url: %s", SDL_GetError());
        out = nullptr;
    } else if (f == "clipboard_set") {
#ifdef __EMSCRIPTEN__
        web_copy(a[0].get<std::string>().c_str());
#else
        SDL_SetClipboardText(a[0].get<std::string>().c_str());
#endif
        out = nullptr;
    } else if (f == "clipboard_get") {
#ifdef __EMSCRIPTEN__
        out = takeString(web_pasted());
#else
        char* text = SDL_GetClipboardText();
        out = std::string(text ? text : "");
        SDL_free(text);
#endif
    } else if (f == "page_query") {
#ifdef __EMSCRIPTEN__
        out = takeString(web_query(a[0].get<std::string>().c_str()));
#else
        out = "";
#endif
    } else if (f == "local_time") {
        // year, month (1-12), day, hour, minute, second, weekday (0 is
        // Sunday), seconds since 1970 UTC.
        std::time_t now = std::time(nullptr);
        std::tm t{};
#ifdef _WIN32
        localtime_s(&t, &now);
#else
        localtime_r(&now, &t);
#endif
        out = {t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, t.tm_wday, double(now)};
    } else if (f == "is_web") {
#ifdef __EMSCRIPTEN__
        out = true;
#else
        out = false;
#endif
    } else if (f == "is_touch") {
#ifdef __EMSCRIPTEN__
        out = web_touch_screen() != 0;
#else
        int n = 0;
        SDL_TouchID* touch = SDL_GetTouchDevices(&n);
        SDL_free(touch);
        out = n > 0;
#endif
    } else {
        return false;
    }
    return true;
}

// ---- Post effects, lights and trails ----

EffectId fade();

// Post effects scripts added, by the id their Effect holds: one of the
// PostEffects library, or a material run by the renderer.
struct ScriptEffect {
    EffectId library;
    MaterialHandle material;
    int32_t rendererId = 0;   // the material's post effect while enabled
    int32_t order = 0;
};
std::unordered_map<int64_t, ScriptEffect> g_effects;
int64_t g_nextEffect = 1;
constexpr int64_t kDefaultOrder = -2147483647;

// The settings of a post effect from a candela value whose `__type` names
// it. False for a struct that is not one.
bool effectSettings(const json& j, PostEffectParams& out) {
    std::string kind = j.value("__type", "");
    if (kind == "Bloom") {
        Bloom b;
        b.threshold = j.value("threshold", 0.7f);
        b.softKnee = j.value("soft_knee", 0.5f);
        b.intensity = j.value("intensity", 1.f);
        b.radius = j.value("radius", 1.f);
        b.levels = j.value("levels", 5);
        b.tint = colorF(j.value("tint", json()), {1.f, 1.f, 1.f, 1.f});
        out = b;
    } else if (kind == "ColorGrading") {
        ColorGrading g;
        g.brightness = j.value("brightness", 1.f);
        g.contrast = j.value("contrast", 1.f);
        g.saturation = j.value("saturation", 1.f);
        g.tint = colorF(j.value("tint", json()), {1.f, 1.f, 1.f, 1.f});
        g.lut = TextureHandle{idOf(j.value("lut", json()))};
        g.lutStrength = j.value("lut_strength", 1.f);
        out = g;
    } else if (kind == "Blur") {
        out = Blur{j.value("radius", 1.f), j.value("iterations", 2)};
    } else if (kind == "Pixelate") {
        out = Pixelate{j.value("size", 6.f)};
    } else if (kind == "Grayscale") {
        out = Grayscale{j.value("amount", 1.f)};
    } else if (kind == "Sepia") {
        out = Sepia{j.value("amount", 1.f)};
    } else if (kind == "Vignette") {
        Vignette v;
        v.intensity = j.value("intensity", 0.6f);
        v.radius = j.value("radius", 0.75f);
        v.softness = j.value("softness", 0.45f);
        v.color = colorF(j.value("color", json()), {0.f, 0.f, 0.f, 1.f});
        out = v;
    } else if (kind == "Crt") {
        Crt c;
        c.curvature = j.value("curvature", 0.15f);
        c.scanlines = j.value("scanlines", 0.35f);
        c.scanlineSpacing = j.value("scanline_spacing", 3.f);
        c.chromaticAberration = j.value("chromatic_aberration", 1.5f);
        c.vignette = j.value("vignette", 0.3f);
        out = c;
    } else {
        DRIFT_LOG_WARN("Candela: %s is not a post effect", kind.empty() ? "that value" : kind.c_str());
        return false;
    }
    return true;
}

void lightJ(Pack& p, ColorF color, float intensity, float radius, float falloff, bool shadows, uint32_t mask) {
    p.key("color");
    colorF(p, color);
    p.kv("intensity", intensity);
    p.kv("radius", radius);
    p.kv("falloff", falloff);
    p.kv("shadows", shadows);
    p.kv("layer_mask", mask);
}
uint32_t maskOf(const Msg& j) { return static_cast<uint32_t>(j.value("layer_mask", int64_t(AllWorldLayers))); }

std::vector<Codec> effectCodecs() {
    return {
        {"Trail", hasIt<TrailRenderer>,
         [](EntityId e, Pack& p) {
             const TrailRenderer& t = *get<TrailRenderer>(e);
             p.object([&] {
                 p.kv("width", t.width);
                 p.kv("lifetime", t.lifetime);
                 p.key("color_start");
                 colorF(p, t.colorStart);
                 p.key("color_end");
                 colorF(p, t.colorEnd);
                 p.kv("min_distance", t.minDistance);
                 p.kv("max_points", t.maxPoints);
                 p.kv("z", t.zOrder);
             });
         },
         [](EntityId e, const Msg& j) {
             TrailRenderer t;
             t.width = j.value("width", 4.f);
             t.lifetime = j.value("lifetime", 0.5f);
             t.colorStart = colorF(j["color_start"], {1, 1, 1, 1});
             t.colorEnd = colorF(j["color_end"], {1, 1, 1, 0});
             t.minDistance = j.value("min_distance", 2.f);
             t.maxPoints = std::max(2, j.value("max_points", 64));
             t.zOrder = j.value("z", -1.f);
             put(e, t);
         },
         removeIt<TrailRenderer>, idOfC<TrailRenderer>},
        {"PointLight", hasIt<PointLight2D>,
         [](EntityId e, Pack& p) {
             const PointLight2D& l = *get<PointLight2D>(e);
             p.object([&] { lightJ(p, l.color, l.intensity, l.radius, l.falloff, l.castShadows, l.layerMask); });
         },
         [](EntityId e, const Msg& j) {
             PointLight2D l;
             l.color = colorF(j["color"], {1, 1, 1, 1});
             l.intensity = j.value("intensity", 1.f);
             l.radius = j.value("radius", 200.f);
             l.falloff = j.value("falloff", 2.f);
             l.castShadows = j.value("shadows", true);
             l.layerMask = maskOf(j);
             put(e, l);
         },
         removeIt<PointLight2D>, idOfC<PointLight2D>},
        {"SpotLight", hasIt<SpotLight2D>,
         [](EntityId e, Pack& p) {
             const SpotLight2D& l = *get<SpotLight2D>(e);
             p.object([&] {
                 lightJ(p, l.color, l.intensity, l.radius, l.falloff, l.castShadows, l.layerMask);
                 p.kv("direction", l.direction);
                 p.kv("angle", l.angle);
                 p.kv("softness", l.softness);
             });
         },
         [](EntityId e, const Msg& j) {
             SpotLight2D l;
             l.color = colorF(j["color"], {1, 1, 1, 1});
             l.intensity = j.value("intensity", 1.f);
             l.radius = j.value("radius", 300.f);
             l.falloff = j.value("falloff", 2.f);
             l.direction = j.value("direction", 0.f);
             l.angle = j.value("angle", 0.8f);
             l.softness = j.value("softness", 0.2f);
             l.castShadows = j.value("shadows", true);
             l.layerMask = maskOf(j);
             put(e, l);
         },
         removeIt<SpotLight2D>, idOfC<SpotLight2D>},
        {"LightOccluder", hasIt<LightOccluder2D>,
         [](EntityId e, Pack& p) {
             const LightOccluder2D& o = *get<LightOccluder2D>(e);
             p.object([&] {
                 p.kv("shape", name(o.shape, {"Box", "Circle", "Polygon", "FromCollider"}));
                 p.key("half_size");
                 vec(p, o.halfSize);
                 p.kv("radius", o.radius);
                 p.key("points");
                 p.array(static_cast<size_t>(o.pointCount));
                 for (int i = 0; i < o.pointCount; ++i) vec(p, o.points[i]);
                 p.key("offset");
                 vec(p, o.offset);
                 p.kv("shadow_self", o.shadowSelf);
                 p.kv("layer_mask", o.layerMask);
             });
         },
         [](EntityId e, const Msg& j) {
             LightOccluder2D o;
             o.shape = pick<OccluderShape>(j["shape"], {"Box", "Circle", "Polygon", "FromCollider"});
             o.halfSize = vec(j["half_size"], {16.f, 16.f});
             o.radius = j.value("radius", 16.f);
             j["points"].each([&](const Msg& pt) {
                 if (o.pointCount < LightOccluder2D::kMaxPoints) o.points[o.pointCount++] = vec(pt);
             });
             o.offset = vec(j["offset"]);
             o.shadowSelf = j.value("shadow_self", false);
             o.layerMask = maskOf(j);
             put(e, o);
         },
         removeIt<LightOccluder2D>, idOfC<LightOccluder2D>},
    };
}

bool effects(const std::string& f, const json& a, json& out) {
    out = nullptr;
    auto* fx = res<PostEffects>();
    if (f == "effect_add") {
        // settings, order
        PostEffectParams params;
        out = 0;
        if (!fx || !effectSettings(a[0], params)) return true;
        int64_t order = a.size() > 1 && a[1].is_number() ? a[1].get<int64_t>() : kDefaultOrder;
        EffectId id = std::visit([&](auto& p) {
            return order == kDefaultOrder ? fx->add(p) : fx->add(p, static_cast<int32_t>(order));
        }, params);
        g_effects[g_nextEffect] = ScriptEffect{id, {}, 0, 0};
        out = g_nextEffect++;
        return true;
    }
    if (f == "effect_set") {
        // id, settings: the new settings of the same kind.
        auto it = g_effects.find(a[0].get<int64_t>());
        PostEffectParams params;
        if (!fx || it == g_effects.end() || !effectSettings(a[1], params)) return true;
        bool same = std::visit([&](auto& p) {
            using T = std::decay_t<decltype(p)>;
            T* cur = fx->get<T>(it->second.library);
            if (cur) *cur = p;
            return cur != nullptr;
        }, params);
        if (!same) DRIFT_LOG_WARN("Candela: Effect.set takes the kind of settings the effect was added with");
        return true;
    }
    if (f == "effect_op") {
        // id, op, value
        auto it = g_effects.find(a[0].get<int64_t>());
        std::string op = a[1].get<std::string>();
        float v = a[2].get<float>();
        out = 0.0;
        if (it == g_effects.end()) return true;
        ScriptEffect& e = it->second;
        auto* r = res<RendererResource>();
        bool isMaterial = e.material.valid();
        if (op == "enabled") {
            out = isMaterial ? (e.rendererId != 0 ? 1.0 : 0.0) : (fx && fx->enabled(e.library) ? 1.0 : 0.0);
        } else if (op == "set_enabled" || op == "set_order") {
            if (op == "set_order") e.order = static_cast<int32_t>(v);
            bool on = op == "set_enabled" ? v > 0.5f : (isMaterial ? e.rendererId != 0 : true);
            if (isMaterial && r) {
                // The renderer has no switch for a material post effect: it
                // is removed and added again.
                if (e.rendererId) r->removePostEffect(e.rendererId);
                e.rendererId = on ? r->addPostEffect(e.order, e.material) : 0;
            } else if (fx) {
                if (op == "set_enabled") fx->setEnabled(e.library, on);
                else fx->setOrder(e.library, e.order);
            }
        } else if (op == "remove") {
            if (isMaterial && r && e.rendererId) r->removePostEffect(e.rendererId);
            else if (!isMaterial && fx) fx->remove(e.library);
            g_effects.erase(it);
        }
        return true;
    }
    if (f == "effect_material") {
        auto* r = res<RendererResource>();
        out = 0;
        if (!r) return true;
        ScriptEffect e;
        e.material = MaterialHandle{a[0].get<uint32_t>()};
        e.order = a[1].get<int32_t>();
        e.rendererId = r->addPostEffect(e.order, e.material);
        g_effects[g_nextEffect] = e;
        out = g_nextEffect++;
        return true;
    }
    if (f == "fade_to") {
        // r, g, b, a, amount, seconds
        EffectId id = fade();
        if (fx && id.valid()) {
            if (auto* sf = fx->get<ScreenFade>(id)) {
                sf->color = ColorF{a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>()};
            }
            fx->fadeTo(id, a[4].get<float>(), a[5].get<float>());
        }
        return true;
    }
    if (f == "lighting") {
        // What lighting is like; with no [lighting] section there is none.
        auto* ls = res<LightingSettings>();
        std::string what = a[0].get<std::string>();
        const json& n = a[1];
        if (!ls) {
            static bool told = false;
            if (!told) DRIFT_LOG_WARN("Candela: lighting is off; add a [lighting] section to drift.toml");
            told = true;
        } else if (what == "ambient") {
            ls->ambient = ColorF{num(n, 0), num(n, 1), num(n, 2), num(n, 3, 1.f)};
        } else if (what == "enabled") {
            ls->enabled = num(n, 0) > 0.5f;
        }
        return true;
    }
    if (f == "tween_speed") {
        if (auto* tw = res<Tweens>()) tw->timeScale = std::max(0.f, a[0].get<float>());
        return true;
    }
    return false;
}

// ---- Window, fonts and shaders ----

SDL_Window* sdlWindow() { return g_app ? static_cast<SDL_Window*>(g_app->sdlWindow()) : nullptr; }

bool windowAndAssets(const std::string& f, const json& a, json& out) {
    out = nullptr;
    auto* r = res<RendererResource>();
    if (f == "window_op") {
        // op, numbers, text
        std::string op = a[0].get<std::string>();
        const json& n = a[1];
        SDL_Window* win = sdlWindow();
        out = json::array();
        if (op == "title") {
            g_app->config().window.title = a[2].get<std::string>();
            if (win) SDL_SetWindowTitle(win, g_app->config().title().c_str());
        } else if (op == "clear_color") {
            ColorF c{num(n, 0), num(n, 1), num(n, 2), num(n, 3, 1.f)};
            g_app->config().render.clearColor = c;
            if (r) r->setClearColor(c);
        } else if (op == "scale_mode") {
            auto m = static_cast<ScaleMode>(std::clamp(static_cast<int>(num(n, 0)), 0, 3));
            g_app->config().window.scaleMode = m;
            if (r) r->setScaleMode(m);
        } else if (op == "logical_size") {
            if (r && num(n, 0) >= 1.f && num(n, 1) >= 1.f) r->setLogicalSize(static_cast<int>(num(n, 0)), static_cast<int>(num(n, 1)));
        } else if (op == "size") {
            int w = 0, h = 0;
            if (win) SDL_GetWindowSize(win, &w, &h);
            out = {w, h};
        } else if (op == "set_size") {
            if (win) SDL_SetWindowSize(win, static_cast<int>(num(n, 0)), static_cast<int>(num(n, 1)));
        } else if (op == "fullscreen") {
#ifdef __EMSCRIPTEN__
            out = {web_is_fullscreen() ? 1.0 : 0.0};
#else
            out = {win && (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) ? 1.0 : 0.0};
#endif
        } else if (op == "set_fullscreen") {
            g_app->config().window.fullscreen = num(n, 0) > 0.5f;
#ifdef __EMSCRIPTEN__
            // The page's fullscreen, which waits for a gesture when it has to.
            web_set_fullscreen(g_app->config().window.fullscreen);
#else
            if (win) SDL_SetWindowFullscreen(win, g_app->config().window.fullscreen);
#endif
        } else if (op == "show_cursor") {
            if (num(n, 0) > 0.5f) SDL_ShowCursor();
            else SDL_HideCursor();
        } else if (op == "cursor") {
            static const SDL_SystemCursor kinds[] = {SDL_SYSTEM_CURSOR_DEFAULT, SDL_SYSTEM_CURSOR_POINTER,
                                                     SDL_SYSTEM_CURSOR_TEXT, SDL_SYSTEM_CURSOR_CROSSHAIR,
                                                     SDL_SYSTEM_CURSOR_MOVE, SDL_SYSTEM_CURSOR_WAIT,
                                                     SDL_SYSTEM_CURSOR_NOT_ALLOWED};
            static SDL_Cursor* made[7] = {};
            int k = std::clamp(static_cast<int>(num(n, 0)), 0, 6);
            if (!made[k]) made[k] = SDL_CreateSystemCursor(kinds[k]);
            if (made[k]) SDL_SetCursor(made[k]);
        } else if (op == "stats") {
            out = {r ? r->drawCalls() : 0, r ? r->spriteCount() : 0};
        } else {
            DRIFT_LOG_WARN("Candela: Window has no '%s'", op.c_str());
        }
        return true;
    }
    if (f == "window_backend") {
        out = std::string(r ? r->backendName() : "none");
        return true;
    }
    if (f == "font_load") {
        auto* assets = res<AssetServer>();
        out = assets ? assets->loadFont(a[0].get<std::string>().c_str(), static_cast<int>(a[1].get<float>())).id : 0u;
        return true;
    }
    if (f == "text_measure") {
        // text, Text style
        auto* fonts = res<FontResource>();
        Vec2 size = fonts ? fonts->measureText(FontHandle{idOf(a[1].value("font", json()))}, a[0].get<std::string>(),
                                               textStyle(a[1]))
                          : Vec2{};
        out = {size.x, size.y};
        return true;
    }
    if (f == "font_line_height") {
        auto* fonts = res<FontResource>();
        out = fonts ? fonts->lineHeight(FontHandle{a[0].get<uint32_t>()}, a[1].get<float>()) : 0.f;
        return true;
    }
    if (f == "font_fallback") {
        if (auto* fonts = res<FontResource>()) fonts->setFallbackFont(FontHandle{a[0].get<uint32_t>()}, FontHandle{a[1].get<uint32_t>()});
        return true;
    }
    if (f == "shader_load") {
        out = r ? r->loadShader(a[0].get<std::string>().c_str()).id : 0u;
        return true;
    }
    if (f == "shader_create") {
        ShaderDesc d;
        d.wgsl = a[0].get<std::string>();
        d.glslFragment = a[1].get<std::string>();
        out = r ? r->createShader(d).id : 0u;
        return true;
    }
    if (f == "material_create") {
        out = r ? r->createMaterial(ShaderHandle{a[0].get<uint32_t>()}).id : 0u;
        return true;
    }
    if (f == "material_param") {
        if (r) r->setMaterialParam(MaterialHandle{a[0].get<uint32_t>()}, a[1].get<int>(),
                                   Vec4{a[2].get<float>(), a[3].get<float>(), a[4].get<float>(), a[5].get<float>()});
        return true;
    }
    if (f == "material_texture") {
        if (r) r->setMaterialTexture(MaterialHandle{a[0].get<uint32_t>()}, a[1].get<int>(), TextureHandle{a[2].get<uint32_t>()});
        return true;
    }
    return false;
}

// Tells the player the game cannot start: on the web, on the page instead
// of a black canvas.
void showStartError(const char* text) {
#ifdef __EMSCRIPTEN__
    web_show_error(text);
#else
    (void)text;
#endif
}

// ---- Sprite animation ----
//
// Sprite sheets, the Animator component, and Aseprite files cut into frames
// and tags.

// The script's Spritesheet value for each sheet made here, by sheet id, so a
// clip read back carries the same sheet the script made it from.
struct SheetValue {
    uint32_t texture = 0;
    float frameW = 0.f, frameH = 0.f;
    int columns = 0, rows = 0;
};
std::unordered_map<uint32_t, SheetValue>& sheetValues() {
    static std::unordered_map<uint32_t, SheetValue> sheets;
    return sheets;
}
void sheetJ(Pack& p, SpritesheetHandle sheet) {
    auto it = sheetValues().find(sheet.id);
    p.object([&] {
        p.kv("id", sheet.id);
        if (it == sheetValues().end()) return;
        const SheetValue& v = it->second;
        p.id("texture", v.texture);
        p.kv("frame_w", v.frameW);
        p.kv("frame_h", v.frameH);
        p.kv("columns", v.columns);
        p.kv("rows", v.rows);
    });
}

void clipJ(Pack& p, const AnimationClip& c) {
    p.object([&] {
        p.key("sheet");
        sheetJ(p, c.sheet);
        p.key("frames");
        p.list(c.frames);
        p.key("seconds");
        p.list(c.seconds);
        p.kv("looping", c.looping);
    });
}
AnimationClip clipOf(const Msg& j) {
    AnimationClip c;
    c.sheet = SpritesheetHandle{idOf(j["sheet"])};
    j["frames"].each([&](const Msg& f) { c.frames.push_back(f.get<int32_t>()); });
    j["seconds"].each([&](const Msg& x) { c.seconds.push_back(x.get<float>()); });
    c.looping = j.value("looping", true);
    return c;
}
bool sameClip(const AnimationClip& a, const AnimationClip& b) {
    return a.sheet == b.sheet && a.frames == b.frames && a.seconds == b.seconds && a.looping == b.looping;
}

// Logs a clip name an Animator does not have, once per entity and name.
void reportUnknownClip(EntityId e, const SpriteAnimator& a, const std::string& name) {
    static std::unordered_set<std::string> reported;
    if (!reported.insert(std::to_string(e) + "/" + name).second) return;
    DRIFT_LOG_ERROR("drift_unknown_clip: %s (clips: %s)", name.c_str(), a.clipNames().c_str());
}

std::vector<Codec> animationCodecs() {
    return {
        {"Animator", hasIt<SpriteAnimator>,
         [](EntityId e, Pack& p) {
             const SpriteAnimator& a = *get<SpriteAnimator>(e);
             p.object([&] {
                 p.key("clips");
                 p.object([&] {
                     for (const auto& c : a.clips) {
                         p.key(c.name);
                         clipJ(p, c.clip);
                     }
                 });
                 p.kv("play", a.current);
                 p.kv("then", a.then);
                 p.kv("once", a.once);
                 p.kv("speed", a.speed);
                 p.kv("playing", a.playing);
                 p.kv("frame", a.frame);
                 p.kv("time", a.time());
                 p.kv("ended", a.ended);
             });
         },
         [](EntityId e, const Msg& j) {
             // The engine keeps the clip's place unless the script switched
             // clips (then it starts from the first frame), moved `frame`, or
             // moved `time`.
             SpriteAnimator a;
             j["clips"].items([&](std::string_view name, const Msg& c) { a.clips.push_back({std::string(name), clipOf(c)}); });
             a.current = j.value("play", std::string());
             a.then = j.value("then", std::string());
             a.once = j.value("once", false);
             a.speed = std::max(0.f, j.value("speed", 1.f));
             a.playing = j.value("playing", true);
             a.ended = j.value("ended", std::string());
             int frame = j.value("frame", 0);
             float time = j.value("time", 0.f);
             const SpriteAnimator* cur = get<SpriteAnimator>(e);
             const AnimationClip* now = a.find(a.current);
             const AnimationClip* before = cur ? cur->find(cur->current) : nullptr;
             bool samePlaying = cur && cur->current == a.current && now && before && sameClip(*now, *before);
             if (samePlaying && frame == cur->frame && time == cur->time()) {
                 a.frame = cur->frame;
                 a.frameTime = cur->frameTime;
             } else if (samePlaying && frame != cur->frame) {
                 a.frame = frame;
             } else if (time > 0.f) {
                 a.seek(time);
             } else {
                 a.frame = frame;
             }
             if (!a.current.empty() && !now) reportUnknownClip(e, a, a.current);
             if (!a.then.empty() && !a.find(a.then)) reportUnknownClip(e, a, a.then);
             if (now && !now->frames.empty()) {
                 a.frame = std::clamp(a.frame, 0, static_cast<int32_t>(now->frames.size()) - 1);
                 // Show the frame now, not after the first step.
                 const Sprite* s = get<Sprite>(e);
                 auto* sprites = res<SpriteResource>();
                 auto* renderer = res<RendererResource>();
                 if (s && sprites && renderer) {
                     Sprite shown = *s;
                     showAnimatorFrame(a, shown, *sprites, *renderer);
                     put(e, shown);
                 }
             }
             put(e, a);
         },
         removeIt<SpriteAnimator>, idOfC<SpriteAnimator>},
    };
}

// Clips that reached their end, as AnimationFinished messages.
void postFinishedAnimations(App& app) {
    auto* finished = app.getResource<Events<AnimationFinished>>();
    if (!finished) return;
    for (const AnimationFinished& f : *finished) {
        candela::postMessage("AnimationFinished", {{"entity", entityJ(f.entity)}, {"clip", f.clip}});
    }
}

bool animation(const std::string& f, const json& a, json& out) {
    if (f == "sheet_grid") {
        // texture, frame width, frame height -> sheet, columns, rows. The
        // same grid of the same texture is one sheet.
        static std::map<std::tuple<uint32_t, float, float>, SpritesheetHandle> made;
        auto* sprites = res<SpriteResource>();
        out = json::array();
        if (!sprites) return true;
        uint32_t tex = a[0].get<uint32_t>();
        float w = a[1].get<float>(), h = a[2].get<float>();
        auto key = std::make_tuple(tex, w, h);
        auto it = made.find(key);
        SpritesheetHandle sheet = it != made.end()
            ? it->second
            : sprites->createSpritesheet(TextureHandle{tex}, static_cast<int32_t>(w), static_cast<int32_t>(h));
        if (!sheet.valid()) return true;
        made[key] = sheet;
        int32_t count = sprites->frameCount(sheet);
        Rect last = sprites->frameAt(sheet, count - 1);
        int columns = static_cast<int>(std::lround(last.x / w)) + 1;
        int rows = static_cast<int>(std::lround(last.y / h)) + 1;
        sheetValues()[sheet.id] = {tex, w, h, columns, rows};
        out = {sheet.id, columns, rows};
        return true;
    }
    if (f == "sheet_aseprite") {
        // path -> sheet, texture, frame width, frame height, frames
        static std::unordered_map<std::string, SpritesheetHandle> loaded;
        auto* sprites = res<SpriteResource>();
        out = json::array();
        if (!sprites) return true;
        std::string path = a[0].get<std::string>();
        auto it = loaded.find(path);
        SpritesheetHandle sheet = it != loaded.end() ? it->second : sprites->loadAseprite(path.c_str());
        if (!sheet.valid()) return true;
        loaded[path] = sheet;
        Rect first = sprites->frameAt(sheet, 0);
        int32_t count = sprites->frameCount(sheet);
        uint32_t tex = sprites->getTexture(sheet).id;
        sheetValues()[sheet.id] = {tex, first.w, first.h, count, 1};
        out = {sheet.id, tex, first.w, first.h, count};
        return true;
    }
    if (f == "sheet_tag") {
        // sheet, tag -> looping, frame count, the frames, their seconds;
        // empty for a tag the sheet does not have
        auto* sprites = res<SpriteResource>();
        out = json::array();
        if (!sprites) return true;
        SpritesheetHandle sheet{a[0].get<uint32_t>()};
        std::string name = a[1].get<std::string>();
        auto names = sprites->tagNames(sheet);
        if (std::find(names.begin(), names.end(), name) == names.end()) return true;
        AnimationClip c = sprites->clipFromTag(sheet, name.c_str());
        out.push_back(c.looping ? 1.0 : 0.0);
        out.push_back(c.frames.size());
        for (int32_t i : c.frames) out.push_back(i);
        for (float s : c.seconds) out.push_back(s);
        return true;
    }
    if (f == "sheet_tags") {
        // sheet -> its tag names, comma separated
        std::string names;
        if (auto* sprites = res<SpriteResource>()) {
            for (const auto& n : sprites->tagNames(SpritesheetHandle{a[0].get<uint32_t>()})) {
                names += (names.empty() ? "" : ", ") + n;
            }
        }
        out = names.empty() ? std::string("none") : names;
        return true;
    }
    return false;
}

// ---- Services ----

int state(bool held, bool pressed, bool released, int which) {
    return which == 1 ? pressed : which == 2 ? released : held;
}

// A save slot: "" is the SavePlugin's slot; others open in the same
// directory the first time they are asked for.
SaveData* saveSlot(const std::string& name) {
    if (name.empty() || name == "save") return res<SaveData>();
    static std::unordered_map<std::string, std::unique_ptr<SaveData>> slots;
    auto& slot = slots[name];
    if (!slot) {
        const auto& project = g_app->config().project;
        slot = std::make_unique<SaveData>(project.organization, project.name, name);
    }
    return slot.get();
}

EffectId fade() {
    auto* fx = res<PostEffects>();
    if (!fx) return {};
    if (!g_fade.valid()) g_fade = fx->add(ScreenFade{});
    return g_fade;
}

// ---- Gameplay: input, physics, audio, scenes, app ----

// Logical screen coordinates back to window coordinates, for input sent
// as if from the device.
Vec2 logicalToWindow(Vec2 p) {
    auto* r = res<RendererResource>();
    if (!r) return p;
    Vec2 o = r->windowToLogical({0.f, 0.f});
    Vec2 one = r->windowToLogical({1.f, 1.f}) - o;
    return {one.x != 0.f ? (p.x - o.x) / one.x : p.x, one.y != 0.f ? (p.y - o.y) / one.y : p.y};
}

SDL_WindowID windowId() {
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    SDL_WindowID id = windows && count > 0 ? SDL_GetWindowID(windows[0]) : 0;
    SDL_free(windows);
    return id;
}

bool gameplayInput(const std::string& f, const json& a, json& out) {
    auto* in = res<InputResource>();
    auto* actions = res<InputActions>();
    if (f == "pad_connected") {
        out = in && in->gamepadConnected(a[0].get<int>());
    } else if (f == "bind_axis_keys") {
        auto key = [&](int i) { return InputBinding::key(static_cast<Key>(a[i].get<int>())); };
        if (actions) actions->bindAxis(a[0].get<std::string>(), key(1), key(2));
        out = nullptr;
    } else if (f == "action_axis") {
        std::string n = a[0].get<std::string>();
        if (actions && !actions->has(n)) {
            out = nullptr;
        } else {
            out = actions ? actions->axis(n) : 0.f;
        }
    } else if (f == "input_bindings") {
        out = json::array();
        std::string n = a[0].get<std::string>();
        if (actions && actions->has(n)) {
            for (const InputBinding& b : actions->bindings(n, a[1].get<int>())) out.push_back(b.toString());
        }
    } else if (f == "input_rebind") {
        InputBinding b = InputBinding::parse(a[3].get<std::string>());
        if (actions && b.valid()) {
            actions->rebind(a[0].get<std::string>(), a[1].get<int>(), static_cast<size_t>(std::max(0, a[2].get<int>())), b);
        }
        out = nullptr;
    } else if (f == "input_capture") {
        std::optional<InputBinding> b = actions && in ? actions->captureNext(*in) : std::nullopt;
        out = b ? b->toString() : std::string();
    } else if (f == "input_tuning") {
        if (actions) {
            actions->deadZone = a[0].get<float>();
            actions->pressThreshold = a[1].get<float>();
            actions->gamepad = a[2].get<int32_t>();
        }
        out = nullptr;
    } else if (f == "input_bindings_json") {
        out = actions ? actions->toJson().dump() : std::string("{}");
    } else if (f == "input_load_bindings") {
        json j = json::parse(a[0].get<std::string>(), nullptr, false);
        out = actions && !j.is_discarded() ? actions->fromJson(j) : 0;
    } else if (f == "input_send") {
        // Pushed as SDL events, so they arrive next frame through every
        // handler, actions included, like the device's own.
        SDL_Event e{};
        int kind = a[0].get<int>(), code = a[1].get<int>();
        bool down = a[2].get<bool>();
        Vec2 p = logicalToWindow({a[3].get<float>(), a[4].get<float>()});
        Uint64 now = SDL_GetTicksNS();
        if (kind == 0) {
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.timestamp = now;
            e.key.windowID = windowId();
            e.key.scancode = static_cast<SDL_Scancode>(code);
            e.key.down = down;
        } else if (kind == 1) {
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.timestamp = now;
            e.motion.windowID = windowId();
            e.motion.x = p.x;
            e.motion.y = p.y;
        } else {
            e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.timestamp = now;
            e.button.windowID = windowId();
            e.button.button = static_cast<Uint8>(code);
            e.button.down = down;
            e.button.clicks = 1;
            e.button.x = p.x;
            e.button.y = p.y;
        }
        SDL_PushEvent(&e);
        out = nullptr;
    } else if (f == "text_input") {
        int op = a[0].get<int>();
        if (in && op == 1) in->startTextInput();
        if (in && op == 2) in->stopTextInput();
        out = in && op == 0 ? in->textInput() : std::string();
    } else if (f == "touch_list") {
        out = json::array();
        if (in) {
            for (const Touch& t : in->touches()) {
                for (double v : std::initializer_list<double>{double(t.id), t.position.x, t.position.y, t.start.x, t.start.y, t.delta.x, t.delta.y,
                                t.pressed ? 1.f : 0.f, t.released ? 1.f : 0.f}) {
                    out.push_back(v);
                }
            }
        }
    } else {
        return false;
    }
    return true;
}

// The entity a shape belongs to; 0 for shapes the engine made itself.
EntityId shapeEntity(uint64_t packed) {
    auto* physics = res<PhysicsResource>();
    auto* bridge = res<PhysicsBridge>();
    if (!physics || !bridge || packed == 0) return 0;
    return entityOfShape(*bridge, *physics, packed);
}

CollisionFilter queryFilter(const json& mask) {
    CollisionFilter f;
    f.category = 0xFFFFFFFFu;
    int64_t m = mask.is_number_integer() ? mask.get<int64_t>() : 0xFFFF;
    f.mask = m >= 0 && m <= 0xFFFFFFFFll ? uint32_t(m) : 0xFFFFu;
    return f;
}

bool gameplayPhysics(const std::string& f, const json& a, json& out) {
    if (f.rfind("physics_", 0) != 0) return false;
    auto* physics = res<PhysicsResource>();
    auto* bridge = res<PhysicsBridge>();
    out = nullptr;
    if (!physics) {
        if (f == "physics_gravity" || f == "physics_raycast") out = json::array();
        if (f == "physics_overlap") out = json::array();
        return true;
    }
    if (f == "physics_gravity") {
        Vec2 g = physics->gravity();
        out = {g.x, g.y};
    } else if (f == "physics_set_gravity") {
        physics->setGravity({a[0].get<float>(), a[1].get<float>()});
    } else if (f == "physics_raycast") {
        Vec2 dir{a[2].get<float>(), a[3].get<float>()};
        float len = dir.length();
        RaycastHit h = len > 0.f ? physics->raycast({a[0].get<float>(), a[1].get<float>()}, dir * (1.f / len),
                                                    a[4].get<float>(), queryFilter(a[5]))
                                 : RaycastHit{};
        out = {h.hit ? 1.0 : 0.0, double(h.hit ? shapeEntity(h.shape) : 0), h.point.x, h.point.y,
               h.normal.x, h.normal.y, h.fraction};
    } else if (f == "physics_overlap") {
        // kind 0: point (x, y); 1: rect (x, y, w, h); 2: circle (x, y, radius).
        int kind = a[0].get<int>();
        float x = a[1].get<float>(), y = a[2].get<float>(), z = a[3].get<float>(), w = a[4].get<float>();
        CollisionFilter filter = queryFilter(a[5]);
        PhysicsShape shapes[256];
        int32_t n = kind == 0 ? physics->overlapPoint({x, y}, filter, shapes, 256)
                  : kind == 1 ? physics->overlapAABB({x, y, z, w}, filter, shapes, 256)
                              : physics->overlapCircle({x, y}, z, filter, shapes, 256);
        std::vector<EntityId> found;
        for (int32_t i = 0; i < n; ++i) {
            EntityId e = shapeEntity(shape_to_u64(shapes[i]));
            if (e != 0 && std::find(found.begin(), found.end(), e) == found.end()) found.push_back(e);
        }
        std::sort(found.begin(), found.end());
        out = found;
    } else if (f == "physics_push") {
        // kind 0: impulse, 1: force, 2: torque, 3: angular impulse.
        EntityId e = a[1].get<EntityId>();
        auto it = bridge ? bridge->entityToBody.find(e) : std::unordered_map<EntityId, PhysicsBody>::iterator{};
        if (bridge && it != bridge->entityToBody.end()) {
            Vec2 v{a[2].get<float>(), a[3].get<float>()};
            switch (a[0].get<int>()) {
                case 0: physics->applyImpulse(it->second, v); break;
                case 1: physics->applyForce(it->second, v); break;
                case 2: physics->applyTorque(it->second, v.x); break;
                case 3: physics->applyAngularImpulse(it->second, v.x); break;
            }
        }
    } else if (f == "physics_texture_shapes") {
        // texture, frame, threshold, tolerance, min area, pieces: each
        // outline as its point count, then x, y pairs.
        out = json::array();
        auto* cache = res<TextureShapeCache>();
        Rect frame = a[1].size() == 4 ? rectOf(a[1]) : Rect{};
        const TextureShapeCache::Shapes* shapes =
            cache ? cache->get(TextureHandle{a[0].get<uint32_t>()}, frame,
                               {a[2].get<float>(), a[3].get<float>(), a[4].get<float>()})
                  : nullptr;
        if (shapes) {
            for (const Outline& o : a[5].get<bool>() ? shapes->pieces() : shapes->outlines) {
                out.push_back(o.size());
                for (Vec2 p : o) {
                    out.push_back(p.x);
                    out.push_back(p.y);
                }
            }
        }
    } else if (f == "physics_rebuild_shapes") {
        // texture, image (0: read the texture back)
        auto img = g_images.find(a[1].get<int64_t>());
        if (auto* cache = res<TextureShapeCache>())
            cache->rebuild(TextureHandle{a[0].get<uint32_t>()}, img == g_images.end() ? nullptr : &img->second);
    } else if (f == "physics_debug") {
        PhysicsDebugDraw d = physics->debugDraw();
        d.enabled = a[0].get<bool>();
        physics->setDebugDraw(d);
    } else {
        return false;
    }
    return true;
}

bool gameplayAudio(const std::string& f, const json& a, json& out) {
    auto* audio = res<AudioResource>();
    if (f == "voice_play") {
        out = 0;
        if (!audio) return true;
        PlayParams p;
        p.bus = BusHandle{a[1].get<uint32_t>()};
        p.volume = a[2].get<float>();
        p.pan = a[3].get<float>();
        p.pitch = a[4].get<float>();
        p.loop = a[5].get<bool>();
        p.fadeIn = a[6].get<float>();
        p.priority = a[7].get<int>();
        out = audio->playSound(SoundHandle{a[0].get<uint32_t>()}, p).id;
    } else if (f == "voice_op") {
        out = 0.0;
        if (!audio) return true;
        PlayingSoundHandle v{a[0].get<uint32_t>()};
        float x = a[2].get<float>(), y = a[3].get<float>();
        switch (a[1].get<int>()) {
            case 0: audio->stopSound(v, x); break;
            case 1: audio->pauseSound(v, x); break;
            case 2: audio->resumeSound(v, x); break;
            case 3: audio->setSoundVolume(v, x, y); break;
            case 4: audio->setSoundPan(v, x); break;
            case 5: audio->setSoundPitch(v, x); break;
            case 6: audio->setSoundLooping(v, x > 0.5f); break;
            case 7: out = audio->isSoundPlaying(v) ? 1.0 : 0.0; break;
            case 8: out = audio->isSoundPaused(v) ? 1.0 : 0.0; break;
        }
    } else if (f == "music_load") {
        // Streamed, once per path.
        static std::unordered_map<std::string, uint32_t> loaded;
        std::string path = a[0].get<std::string>();
        auto it = loaded.find(path);
        if (it == loaded.end() || !audio) {
            uint32_t id = audio ? audio->loadSound(path.c_str(), LoadMode::Stream).id : 0u;
            if (id) loaded[path] = id;
            out = id;
        } else {
            out = it->second;
        }
    } else if (f == "sound_info") {
        out = 0.0;
        if (!audio) return true;
        SoundHandle s{a[0].get<uint32_t>()};
        if (a[1].get<int>() == 0) out = audio->soundDuration(s);
        else audio->setSoundMaxInstances(s, a[2].get<int>());
    } else if (f == "music_op") {
        out = 0.0;
        if (!audio) return true;
        float x = a[2].get<float>();
        switch (a[0].get<int>()) {
            case 0: {
                MusicParams p;
                p.crossfade = x;
                p.loop = a[3].get<bool>();
                audio->playMusic(SoundHandle{a[1].get<uint32_t>()}, p);
            } break;
            case 1: audio->stopMusic(x); break;
            case 2: audio->pauseMusic(x); break;
            case 3: audio->resumeMusic(x); break;
            case 4: out = audio->isMusicPlaying() ? 1.0 : 0.0; break;
            case 5: audio->nextTrack(); break;
            case 6: out = audio->hasDevice() ? 1.0 : 0.0; break;
        }
    } else if (f == "music_playlist") {
        out = nullptr;
        if (!audio) return true;
        std::vector<SoundHandle> tracks;
        for (const auto& t : a[0]) tracks.push_back(SoundHandle{t.get<uint32_t>()});
        PlaylistParams p;
        p.crossfade = a[1].get<float>();
        p.loop = a[2].get<bool>();
        p.shuffle = a[3].get<bool>();
        p.volume = a[4].get<float>();
        audio->playPlaylist(tracks, p);
    } else if (f == "audio_bus") {
        out = 0;
        if (!audio) return true;
        std::string name = a[0].get<std::string>();
        BusHandle b = name == "master" ? audio->masterBus()
                    : name == "music"  ? audio->musicBus()
                    : name == "sfx"    ? audio->sfxBus()
                                       : audio->createBus(name.c_str(), BusHandle{a[1].get<uint32_t>()});
        out = b.id;
    } else if (f == "bus_op") {
        out = 0.0;
        if (!audio) return true;
        BusHandle b{a[0].get<uint32_t>()};
        if (!b.valid()) b = audio->masterBus();
        float x = a[2].get<float>(), y = a[3].get<float>();
        switch (a[1].get<int>()) {
            case 0: audio->setBusVolume(b, x, y); break;
            case 1: out = audio->busVolume(b); break;
            case 2: audio->setBusMuted(b, x > 0.5f); break;
            case 3: out = audio->isBusMuted(b) ? 1.0 : 0.0; break;
            case 4: audio->setBusPaused(b, x > 0.5f); break;
            case 5: out = audio->isBusPaused(b) ? 1.0 : 0.0; break;
            case 6: audio->stopBus(b, x); break;
        }
    } else {
        return false;
    }
    return true;
}

// The objects of a kind ("" for all) with their layer and place in it.
template <typename Fn> void eachObject(TilemapResource& maps, TilemapHandle m, const std::string& kind, Fn&& fn) {
    int32_t layers = maps.layerCount(m);
    for (int32_t l = 0; l < layers; ++l) {
        const auto& objs = maps.objects(m, l);
        for (size_t i = 0; i < objs.size(); ++i) {
            if (kind.empty() || objs[i].type == kind) fn(l, int32_t(i), objs[i]);
        }
    }
}

bool gameplayTilemap(const std::string& f, const json& a, json& out) {
    if (f != "map_objects" && f != "map_object_names" && f != "map_object_property" && f != "map_tile_property" &&
        f != "map_autotile") {
        return false;
    }
    auto* maps = res<TilemapResource>();
    out = f == "map_objects" || f == "map_object_names" ? json::array() : json(std::string());
    if (!maps) return true;
    TilemapHandle m{a[0].get<uint32_t>()};
    if (f == "map_objects") {
        Vec2 o = maps->origin(m);
        eachObject(*maps, m, a[1].get<std::string>(), [&](int32_t l, int32_t i, const MapObject& ob) {
            Vec2 anchor = o + ob.anchor();
            for (double v : std::initializer_list<double>{double(l), double(i), double(ob.id), double(int(ob.shape)),
                                                          o.x + ob.position.x, o.y + ob.position.y, ob.size.x,
                                                          ob.size.y, anchor.x, anchor.y, ob.rotation,
                                                          double(ob.tile & 0x0FFFFFFFu), double(ob.points.size())}) {
                out.push_back(v);
            }
            for (Vec2 p : ob.points) {
                out.push_back(o.x + ob.position.x + p.x);
                out.push_back(o.y + ob.position.y + p.y);
            }
        });
    } else if (f == "map_object_names") {
        eachObject(*maps, m, a[1].get<std::string>(), [&](int32_t, int32_t, const MapObject& ob) {
            out.push_back(ob.name);
            out.push_back(ob.type);
        });
    } else if (f == "map_object_property") {
        int32_t l = a[1].get<int32_t>(), i = a[2].get<int32_t>();
        if (l >= 0 && l < maps->layerCount(m)) {
            const auto& objs = maps->objects(m, l);
            if (i >= 0 && size_t(i) < objs.size()) out = objs[size_t(i)].properties.getString(a[3].get<std::string>());
        }
    } else if (f == "map_tile_property") {
        if (const TilemapProperties* p = maps->tileProperties(m, a[1].get<uint32_t>())) {
            out = p->getString(a[2].get<std::string>());
        }
    } else {
        AutotileRules r;
        r.mode = a[3].get<int>() == 1 ? AutotileMode::Neighbours8 : AutotileMode::Neighbours4;
        r.terrain = ints(a[4]);
        for (const auto& t : a[5]) r.tiles.push_back(static_cast<uint32_t>(std::max<int64_t>(0, t.get<int64_t>())));
        r.fallback = static_cast<uint32_t>(std::max<int64_t>(0, a[6].get<int64_t>()));
        r.edgesAreTerrain = a[7].get<bool>();
        maps->autotile(m, a[1].get<int32_t>(), a[2].get<int32_t>(), r);
        out = nullptr;
    }
    return true;
}

bool gameplaySpatial(const std::string& f, const json& a, json& out) {
    if (f == "spatial_query") {
        out = json::array();
        auto* index = res<SpatialIndex>();
        if (!index) return true;
        float x = a[1].get<float>(), y = a[2].get<float>(), z = a[3].get<float>(), w = a[4].get<float>();
        std::vector<EntityId> found;
        switch (a[0].get<int>()) {
            case 0: index->queryRect({x, y, z, w}, found); break;
            case 1: index->queryRadius({x, y}, z, found); break;
            default: {
                EntityId e = index->nearest({x, y}, z, a[5].get<EntityId>() ? a[5].get<EntityId>() : InvalidEntityId);
                if (e != InvalidEntityId) found.push_back(e);
            }
        }
        out = found;
        return true;
    }
    if (f == "grid_fov" || f == "grid_los") {
        Grid g = gridOf(a[0], a[1], a[2]);
        std::vector<int32_t> opaqueValues = ints(a[3]);
        auto opaque = [&](GridPoint p) {
            return std::find(opaqueValues.begin(), opaqueValues.end(), g.get(p)) != opaqueValues.end();
        };
        if (f == "grid_los") {
            out = lineOfSight({a[4].get<int32_t>(), a[5].get<int32_t>()}, {a[6].get<int32_t>(), a[7].get<int32_t>()},
                              opaque);
            return true;
        }
        std::vector<int32_t> cells(g.cells.size(), 0);
        for (GridPoint p : fieldOfView(g.width, g.height, {a[4].get<int32_t>(), a[5].get<int32_t>()},
                                       a[6].get<int32_t>(), opaque)) {
            cells[size_t(p.y) * size_t(g.width) + size_t(p.x)] = 1;
        }
        out = cells;
        return true;
    }
    if (f == "grid_line") {
        out = json::array();
        for (GridPoint p : gridLine({a[0].get<int32_t>(), a[1].get<int32_t>()}, {a[2].get<int32_t>(), a[3].get<int32_t>()})) {
            out.push_back(p.x);
            out.push_back(p.y);
        }
        return true;
    }
    return false;
}

std::vector<std::string> g_sceneNames;   // the names of the last scene spawn

bool gameplayScenes(const std::string& f, const json& a, json& out) {
    if (f.rfind("scene_", 0) != 0) return false;
    auto* scenes = res<SceneResource>();
    if (!scenes) {
        out = f == "scene_load" ? json(0) : f == "scene_write" ? json(false) : json::array();
        return true;
    }
    if (f == "scene_load") {
        out = scenes->load(a[0].get<std::string>().c_str()).id;
    } else if (f == "scene_spawn") {
        SceneSpawnOptions o;
        o.offset = {a[1].get<float>(), a[2].get<float>()};
        EntityId parent = a[3].get<EntityId>();
        o.parent = parent ? parent : InvalidEntityId;
        for (size_t i = 0; i < a[4].size() && i < a[5].size(); ++i) {
            json patch = json::parse(a[5][i].get<std::string>(), nullptr, false);
            if (!patch.is_discarded()) o.overrides[a[4][i].get<std::string>()] = patch;
        }
        SceneInstance inst = scenes->spawnNow(SceneHandle{a[0].get<uint32_t>()}, o);
        out = json::array();
        out.push_back(inst.entities.size());
        for (EntityId e : inst.entities) out.push_back(e);
        out.push_back(inst.roots.size());
        for (EntityId e : inst.roots) out.push_back(e);
        g_sceneNames = inst.names;
    } else if (f == "scene_names") {
        out = g_sceneNames;
    } else if (f == "scene_serialize") {
        // Every entity with a reflected component and the ones the game
        // names (its own components), or one root and its children.
        SceneSaveOptions o;
        EntityId root = a[0].get<EntityId>();
        if (root) {
            o.entities.push_back(root);
        } else {
            std::unordered_set<EntityId> all;
            for (const reflect::ComponentInfo* info : reflect::components()) {
                if (ComponentId c = info->id(world())) {
                    for (EntityId e : world().entitiesWith(c)) all.insert(e);
                }
            }
            for (const auto& e : a[1]) {
                if (world().isAlive(e.get<EntityId>())) all.insert(e.get<EntityId>());
            }
            o.entities.assign(all.begin(), all.end());
            if (o.entities.empty()) o.filter = [](EntityId) { return false; };
        }
        std::vector<EntityId> order;
        json doc = scenes->serialize(o, &order);
        out = json{{"doc", doc}, {"order", order}};
    } else if (f == "scene_write") {
        std::string path = a[0].get<std::string>();
        json doc = json::parse(a[1].get<std::string>(), nullptr, false);
        std::FILE* file = doc.is_discarded() ? nullptr : std::fopen(path.c_str(), "wb");
        bool ok = file != nullptr;
        if (file) {
            std::string text = doc.dump(2) + "\n";
            ok = std::fwrite(text.data(), 1, text.size(), file) == text.size();
            ok = std::fclose(file) == 0 && ok;
        }
        out = ok;
    } else {
        return false;
    }
    return true;
}

// Scene files name the game's components by their struct names; the host
// takes those when a scene spawns or reloads.
void handScriptComponentsToHost() {
    auto* scenes = res<SceneResource>();
    if (!scenes) return;
    scenes->setForeignComponentHandler([](EntityId e, const std::string& name, const json& value,
                                          const std::unordered_map<int64_t, EntityId>& ids) {
        if (!drift_host_is_component(name.c_str())) return false;
        json map = json::object();
        for (const auto& [local, entity] : ids) map[std::to_string(local)] = entity;
        drift_host_scene_component(e, name.c_str(), value.dump().c_str(), map.dump().c_str());
        return true;
    });
}

bool gameplaySteering(const std::string& f, const json& a, json& out) {
    if (f != "steer") return false;
    std::string op = a[0].get<std::string>();
    std::vector<float> n;
    for (const auto& x : a[1]) n.push_back(x.get<float>());
    auto points = [](const json& flat) {
        std::vector<Vec2> v;
        for (size_t i = 0; i + 1 < flat.size(); i += 2) v.push_back({flat[i].get<float>(), flat[i + 1].get<float>()});
        return v;
    };
    std::vector<Vec2> pa = points(a[2]), pb = points(a[3]);
    auto at = [&](size_t i) { return i < n.size() ? n[i] : 0.f; };
    Vec2 pos{at(0), at(1)}, vel{at(2), at(3)}, r{};
    out = json::array();
    if (op == "truncate") r = steer::truncate(pos, at(2));
    else if (op == "seek") r = steer::seek(pos, vel, {at(4), at(5)}, at(6));
    else if (op == "flee") r = steer::flee(pos, vel, {at(4), at(5)}, at(6), at(7));
    else if (op == "arrive") r = steer::arrive(pos, vel, {at(4), at(5)}, at(6), at(7));
    else if (op == "pursue") r = steer::pursue(pos, vel, {at(4), at(5)}, {at(6), at(7)}, at(8));
    else if (op == "evade") r = steer::evade(pos, vel, {at(4), at(5)}, {at(6), at(7)}, at(8), at(9));
    else if (op == "separation") r = steer::separation(pos, pa, at(2), at(3));
    else if (op == "cohesion") r = steer::cohesion(pos, vel, pa, at(4), at(5));
    else if (op == "alignment") r = steer::alignment(pos, vel, pa, pb, at(4), at(5));
    else if (op == "wander") {
        // velocity, angle, max speed, distance, radius, jitter, then the
        // generator (hi, lo, inc); answers the force, angle and (hi, lo).
        float angle = at(2);
        if (a[4].size() < 3) return true;
        Rng rng = rngOf(a[4]);
        r = steer::wander({at(0), at(1)}, angle, rng, at(3), at(4), at(5), at(6));
        out = {r.x, r.y, angle, double(rng.state() >> 32), double(rng.state() & 0xffffffffu)};
        return true;
    } else if (op == "follow_path") {
        size_t index = size_t(std::max(0.f, at(4)));
        r = steer::followPath(pos, vel, pa, index, at(5), at(6), at(7));
        out = {r.x, r.y, double(index)};
        return true;
    }
    out = {r.x, r.y};
    return true;
}

bool gameplay(const std::string& f, const json& a, json& out) {
    return gameplayInput(f, a, out) || gameplayPhysics(f, a, out) || gameplayAudio(f, a, out) ||
           gameplayTilemap(f, a, out) || gameplaySpatial(f, a, out) || gameplayScenes(f, a, out) ||
           gameplaySteering(f, a, out);
}

bool dataAndNet(const std::string& f, const json& a, json& out) {
    return candela::dataService(*g_app, f, a, out) || candela::netService(f, a, out);
}

json service(const std::string& f, const json& a) {
    // The group of functions that answers each name, found the first time
    // it is called, so later calls skip the others' name checks.
    using Group = bool (*)(const std::string&, const json&, json&);
    static const Group groups[] = {procgen, presentation, images, gameplay, effects,
                                   windowAndAssets, platform, animation, dataAndNet};
    static std::unordered_map<std::string, int> route;
    json answer;
    auto known = route.find(f);
    if (known != route.end() && known->second >= 0 && groups[known->second](f, a, answer)) return answer;
    if (known == route.end() || known->second >= 0) {
        for (int i = 0; i < int(std::size(groups)); ++i) {
            if (groups[i](f, a, answer)) {
                route[f] = i;
                return answer;
            }
        }
        route[f] = -1;
    }
    if (f == "take_messages") {
        candela::netPoll();
        json sent(std::move(candela::outbox()));
        candela::outbox().clear();
        return sent;
    }
    auto* in = res<InputResource>();
    auto* actions = res<InputActions>();
    if (f == "key_state") {
        Key k = static_cast<Key>(a[0].get<int>());
        return in && state(in->keyHeld(k), in->keyPressed(k), in->keyReleased(k), a[1].get<int>());
    }
    if (f == "mouse_state") {
        auto b = static_cast<MouseButton>(a[0].get<int>());
        return in && state(in->mouseButtonHeld(b), in->mouseButtonPressed(b), in->mouseButtonReleased(b), a[1].get<int>());
    }
    if (f == "pad_state") {
        int p = a[0].get<int>(), b = a[1].get<int>();
        return in && state(in->gamepadButtonHeld(p, b), in->gamepadButtonPressed(p, b), in->gamepadButtonReleased(p, b), a[2].get<int>());
    }
    if (f == "pad_axis") return in ? in->gamepadAxis(a[0].get<int>(), a[1].get<int>()) : 0.0;
    if (f == "mouse_vec") {
        Vec2 v{};
        int which = a[0].get<int>();
        if (in && which == 3) {
            v = {in->mouseWheelDelta(), 0.f};
        } else if (in) {
            v = which == 2 ? in->mouseDelta() : in->mousePosition();
            if (which == 1) {
                if (auto* r = res<RendererResource>()) v = r->screenToWorld(v);
            }
        }
        return {v.x, v.y};
    }
    if (f == "bind") {
        InputBinding b = InputBinding::parse(a[1].get<std::string>());
        if (actions && b.valid()) actions->bindAction(a[0].get<std::string>(), b);
        return nullptr;
    }
    if (f == "bind_axis2d_keys") {
        auto key = [&](int i) { return InputBinding::parse("key:#" + std::to_string(a[i].get<int>())); };
        if (actions) actions->bindAxis2D(a[0].get<std::string>(), key(1), key(2), key(3), key(4));
        return nullptr;
    }
    if (f == "bind_stick") {
        if (actions) actions->bindStick(a[0].get<std::string>(), static_cast<GamepadAxis>(a[1].get<int>()),
                                        static_cast<GamepadAxis>(a[2].get<int>()));
        return nullptr;
    }
    // An action the game never declared answers null, and the host raises
    // drift_unknown_action naming the ones it has.
    if (f == "action_state") {
        std::string n = a[0].get<std::string>();
        if (!actions) return false;
        if (!actions->has(n)) return nullptr;
        int w = a[1].get<int>();
        return w == 1 ? actions->pressed(n) : w == 2 ? actions->released(n) : actions->held(n);
    }
    if (f == "action_value") {
        std::string n = a[0].get<std::string>();
        if (actions && !actions->has(n)) return nullptr;
        return actions ? actions->value(n) : 0.0;
    }
    if (f == "action_axis2d") {
        std::string n = a[0].get<std::string>();
        if (actions && !actions->has(n)) return nullptr;
        Vec2 v = actions ? actions->axis2D(n) : Vec2{};
        return {v.x, v.y};
    }
    if (f == "action_names") return actions ? json(actions->names()) : json::array();
    if (f == "texture_load") {
        auto* assets = res<AssetServer>();
        return assets ? assets->loadTexture(a[0].get<std::string>().c_str()).id : 0u;
    }
    if (f == "texture_create") {
        auto* r = res<RendererResource>();
        int w = a[0].get<int>(), h = a[1].get<int>();
        const json& px = a[2];
        if (!r || w <= 0 || h <= 0 || px.size() < size_t(w) * size_t(h) * 4) return 0;
        std::vector<uint8_t> bytes(px.size());
        for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = channel(px[i].get<double>());
        return r->createTextureFromPixels(bytes.data(), w, h).id;
    }
    if (f == "texture_size") {
        Vec2 s = textureSize(TextureHandle{a[0].get<uint32_t>()});
        return {s.x, s.y};
    }
    if (f == "texture_filter") {
        if (auto* r = res<RendererResource>()) {
            TextureOptions o;
            o.filter = a[1].get<int>() ? TextureFilter::Linear : TextureFilter::Nearest;
            r->setTextureOptions(TextureHandle{a[0].get<uint32_t>()}, o);
        }
        return nullptr;
    }
    if (f == "sound_load") {
        auto* assets = res<AssetServer>();
        return assets ? assets->loadSound(a[0].get<std::string>().c_str()).id : 0u;
    }
    if (f == "sound_create") {
        auto* audio = res<AudioResource>();
        const json& s = a[0];
        if (!audio || s.empty()) return 0;
        std::vector<float> samples(s.size());
        for (size_t i = 0; i < samples.size(); ++i) samples[i] = s[i].get<float>();
        return audio->createSound(samples.data(), samples.size(), 1, a[1].get<int>()).id;
    }
    if (f == "quit") {
        g_app->quit();
        return nullptr;
    }
    if (f == "window_size") {
        auto* r = res<RendererResource>();
        return r ? json{r->logicalWidth(), r->logicalHeight()} : json{0, 0};
    }
    if (f == "flash") {
        auto* fx = res<PostEffects>();
        EffectId id = fade();
        if (fx && id.valid()) {
            fx->flash(id, ColorF{a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>()},
                      a[4].get<float>());
        }
        return nullptr;
    }
    if (f == "shake") {
        EntityId e = a[0].get<EntityId>();
        if (world().isAlive(e)) {
            CameraShake s;
            if (const CameraShake* cur = get<CameraShake>(e)) s = *cur;
            s.trigger(a[1].get<float>());
            put(e, s);
        }
        return nullptr;
    }
    if (f == "burst") {
        // One emitter that fires `count` particles once and despawns itself
        // through a Lifetime once they are gone.
        EntityId e = world().createEntity();
        Transform2D t;
        t.position = vec(a[0]);
        put(e, t);
        int count = a[2].is_number() ? a[2].get<int>() : 10;
        ParticleEmitter pe;
        pe.config = emitterConfig(a[1]);
        pe.config.spawnRate = 0.f;
        pe.config.looping = false;
        pe.config.duration = 0.05f;
        pe.config.maxParticles = std::max(pe.config.maxParticles, count);
        pe.config.bursts.push_back(BurstEntry{0.f, count, 1, 0.f});
        put(e, pe);
        put(e, Lifetime{pe.config.lifetime.max + 0.2f});
        return nullptr;
    }
    if (f.rfind("save_", 0) == 0) {
        SaveData* save = saveSlot(a[0].get<std::string>());
        if (!save) return f == "save_flush" ? json(false) : json(nullptr);
        if (f == "save_get") {
            std::string k = a[1].get<std::string>();
            if (a[2].is_boolean()) return save->get<bool>(k, a[2].get<bool>());
            if (a[2].is_number_integer()) return save->get<int64_t>(k, a[2].get<int64_t>());
            if (a[2].is_number()) return save->get<double>(k, a[2].get<double>());
            return save->get<std::string>(k, a[2].get<std::string>());
        }
        if (f == "save_set") {
            std::string k = a[1].get<std::string>();
            if (a[2].is_boolean()) save->set(k, a[2].get<bool>());
            else if (a[2].is_number_integer()) save->set(k, a[2].get<int64_t>());
            else if (a[2].is_number()) save->set(k, a[2].get<double>());
            else save->set(k, a[2].get<std::string>());
            return nullptr;
        }
        if (f == "save_has") return save->has(a[1].get<std::string>());
        if (f == "save_erase") { save->erase(a[1].get<std::string>()); return nullptr; }
        if (f == "save_clear") { save->clear(); return nullptr; }
        if (f == "save_flush") return save->flush();
    }
    if (f == "entity_count") return int64_t(world().entitiesWith(cid<Transform2D>()).size());
    if (f == "entity_alive") return a[0].get<EntityId>() != 0 && world().isAlive(a[0].get<EntityId>());
    if (f == "set_time_scale") {
        if (auto* time = res<Time>()) time->scale = std::max(0.f, a[0].get<float>());
        return nullptr;
    }
    if (f == "settings_section") {
        std::string text = g_app->settingsStore().valueJson(a[0].get<std::string>().c_str());
        json out = json::parse(text.empty() ? "{}" : text, nullptr, false);
        return out.is_discarded() ? json::object() : out;
    }
    DRIFT_LOG_WARN("Candela: no engine function '%s'", f.c_str());
    return nullptr;
}

} // namespace

// ---- What the host calls ----

extern "C" {

void drift_bridge_log(int32_t level, const char* message) {
    if (level >= 2) DRIFT_LOG_ERROR("%s", message);
    else if (level == 1) DRIFT_LOG_WARN("%s", message);
    else DRIFT_LOG_INFO("%s", message);
}

uint64_t drift_bridge_new() { return world().createEntity(); }

void drift_bridge_despawn(uint64_t e) {
    if (!world().isAlive(e)) return;
    for (EntityId d : descendantsOf(world(), e)) stopTween(d);
    stopTween(e);
    despawnRecursive(world(), e);
}

int32_t drift_bridge_alive(uint64_t e) { return world().isAlive(e) ? 1 : 0; }

int32_t drift_bridge_is_engine(const char* component) { return codec(component) ? 1 : 0; }

int32_t drift_bridge_has(uint64_t e, const char* component) {
    const Codec* c = codec(component);
    return c && world().isAlive(e) && c->has(e) ? 1 : 0;
}

const uint8_t* drift_bridge_get(uint64_t e, const char* component, size_t* len) {
    const Codec* c = codec(component);
    if (!c || !world().isAlive(e) || !c->has(e)) return nullptr;
    g_answer.clear();
    Pack p(g_answer);
    c->read(e, p);
    *len = g_answer.size();
    return g_answer.data();
}

int32_t drift_bridge_set(uint64_t e, const char* component, const uint8_t* data, size_t len) {
    const Codec* c = codec(component);
    if (!c || !world().isAlive(e)) return 0;
    // A null value (or none) sets the component from its defaults, as an
    // empty map does.
    static const uint8_t empty[] = {0x80};
    Msg j(data, data + len);
    if (!data || len == 0 || j.is_null()) j = Msg(empty, empty + 1);
    else if (!j.skip()) return 0;
    c->write(e, j);
    return 1;
}

void drift_bridge_remove(uint64_t e, const char* component) {
    const Codec* c = codec(component);
    if (c && world().isAlive(e) && c->has(e)) c->remove(e);
}

const uint8_t* drift_bridge_list(const char* component, size_t* len) {
    const Codec* c = codec(component);
    std::vector<EntityId> ids;
    if (c) {
        ids = world().entitiesWith(c->id());
        std::sort(ids.begin(), ids.end());
        ids.erase(std::remove_if(ids.begin(), ids.end(), [&](EntityId e) { return !c->has(e); }), ids.end());
    }
    g_answer.clear();
    Pack p(g_answer);
    p.list(ids);
    *len = g_answer.size();
    return g_answer.data();
}

// Engine components that need another to work: everything drawn or
// simulated needs a Transform.
const char* drift_bridge_requires(const char* component) {
    const Codec* c = codec(component);
    if (!c || std::string(c->name) == "Transform" || std::string(c->name) == "Name" ||
        std::string(c->name) == "Tween") return "[]";
    return "[\"Transform\"]";
}

const uint8_t* drift_bridge_call(const char* function, const uint8_t* args, size_t argsLen, size_t* len) {
    const auto& fast = fastServices();
    auto it = function ? fast.find(function) : fast.end();
    if (it != fast.end()) {
        g_answer.clear();
        Pack out(g_answer);
        Msg list(args, args + argsLen);
        it->second(Args(list), out);
        *len = g_answer.size();
        return g_answer.data();
    }
    json a = unpack(args, argsLen);
    if (a.is_discarded() || a.is_null()) a = json::array();
    return keep(service(function ? function : "", a), len);
}

} // extern "C"

namespace {

// Hands each frame to the Candela host.
class CandelaPlugin : public Plugin {
public:
    CandelaPlugin(std::string entry, std::string moduleDir)
        : entry_(std::move(entry)), moduleDir_(std::move(moduleDir)) {}

    void build(App& app) override {
        app.addSystem("candela", Phase::Update, [this](App& a) { frame(a); });
        // Scripts and the data files the game read are assets: when hot
        // reload sees one change, the game's code is compiled again and
        // swapped in while it runs. drift.toml is tracked as a script too,
        // so @settings resources read their sections again.
        auto* assets = app.getResource<AssetServer>();
        auto reload = [this](uint32_t, const std::string&) { return reloadGame(); };
        assets->setReloader("script", reload);
        assets->setReloader("data", reload);
    }
    DRIFT_PLUGIN(CandelaPlugin)

private:
    void frame(App& a) {
        if (failed_) return;
        if (!loaded_) {
            if (!drift_host_load(entry_.c_str(), moduleDir_.c_str())) {
                DRIFT_LOG_ERROR("%s", drift_host_error());
                showStartError(drift_host_error());
                failed_ = true;
                g_loadFailed = true;
                a.quit();
                return;
            }
            loaded_ = true;
            handScriptComponentsToHost();
            trackScripts(a);
#ifdef __EMSCRIPTEN__
            web_dev_status("");
#endif
        }
        if (auto* cb = a.getResource<CollisionBridge>()) {
            for (int i = 0; i < cb->collisionStartCount(); ++i) {
                Vec2 p = cb->collisionStartPoint(i), n = cb->collisionStartNormal(i);
                drift_host_contact(0, cb->collisionStartA(i), cb->collisionStartB(i), p.x, p.y, n.x, n.y);
            }
            for (int i = 0; i < cb->collisionEndCount(); ++i) drift_host_contact(1, cb->collisionEndA(i), cb->collisionEndB(i), 0, 0, 0, 0);
            for (int i = 0; i < cb->sensorStartCount(); ++i) drift_host_contact(2, cb->sensorStartA(i), cb->sensorStartB(i), 0, 0, 0, 0);
            for (int i = 0; i < cb->sensorEndCount(); ++i) drift_host_contact(3, cb->sensorEndA(i), cb->sensorEndB(i), 0, 0, 0, 0);
        }
        postFinishedAnimations(a);
        offerDownloads();
        auto* time = a.getResource<Time>();
        drift_host_frame(time ? time->delta : 0.0, time ? time->elapsed : 0.0, time ? int64_t(time->frame) : 0,
                         time ? time->scale : 1.0, time ? time->realDelta : 0.0);
    }

    // Every .cdl file in the game's folder, and drift.toml, as "script"
    // assets. Hidden folders and build output are skipped, and in the
    // browser the runtime's own folders.
    void trackScripts(App& a) {
        auto* assets = a.getResource<AssetServer>();
        if (!assets) return;
        namespace fs = std::filesystem;
        fs::path folder = fs::path(entry_).parent_path();
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(folder.empty() ? fs::path(".") : folder, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            std::string name = it->path().filename().string();
            if (it->is_directory(ec)) {
                bool skip = name.rfind('.', 0) == 0 || name == "build" || name.rfind("build-", 0) == 0;
#ifdef __EMSCRIPTEN__
                skip = skip || name == "dev" || name == "proc" || name == "tmp" || name == "home" ||
                       name == "drift-module";
#endif
                if (skip) it.disable_recursion_pending();
                continue;
            }
            if (it->path().extension() != ".cdl") continue;
            std::string path = it->path().lexically_normal().generic_string();
            if (!assets->find("script", path.c_str())) assets->track("script", ++scriptIds_, path.c_str());
        }
        const std::string& toml = a.settingsStore().file();
        if (!toml.empty() && !assets->find("script", toml.c_str())) assets->track("script", ++scriptIds_, toml.c_str());
    }

    // Compiles the game again and swaps it in, once per frame however many
    // of its files changed. The old code keeps running when the new code
    // does not compile or the running game cannot move into it.
    bool reloadGame() {
        if (!loaded_ || !g_app) return false;
        auto* time = g_app->getResource<Time>();
        uint64_t frame = time ? uint64_t(time->frame) : 0;
        if (reloadedAt_ == frame) return reloadedOk_;
        reloadedAt_ = frame;
        int result = drift_host_reload(entry_.c_str(), moduleDir_.c_str());
        reloadedOk_ = result == 1;
        if (reloadedOk_) {
            trackScripts(*g_app);
#ifdef __EMSCRIPTEN__
            web_dev_status("");
#endif
            return true;
        }
        std::string why = drift_host_error();
        if (result == 0) {
            DRIFT_LOG_ERROR("Candela: the changed code does not compile; the game keeps running the code it had.\n%s",
                            why.c_str());
#ifdef __EMSCRIPTEN__
            web_dev_status(why.c_str());
#endif
        } else {
#ifdef __EMSCRIPTEN__
            DRIFT_LOG_WARN("Candela: the changed code cannot take over the running game: %s. Loading the page again.",
                           why.c_str());
            web_dev_reload_page(why.c_str());
#else
            DRIFT_LOG_WARN("Candela: the changed code cannot take over the running game: %s. "
                           "The game keeps running the code it had; start it again to run the new code.",
                           why.c_str());
#endif
        }
        return false;
    }

    std::string entry_;
    std::string moduleDir_;
    bool loaded_ = false;
    bool failed_ = false;
    uint32_t scriptIds_ = 0;
    uint64_t reloadedAt_ = UINT64_MAX;
    bool reloadedOk_ = false;
};

// Where the installed toolchain keeps what `drift` reads at run time:
// <prefix>/bin/drift beside <prefix>/lib/drift/<what>. Empty when the folder
// is not there, as in a build tree.
std::string installed(const char* what) {
#ifdef __EMSCRIPTEN__
    (void)what;
    return {};
#else
    const char* base = SDL_GetBasePath();
    if (!base) return {};
    std::filesystem::path p = (std::filesystem::path(base) / ".." / "lib" / "drift" / what).lexically_normal();
    std::error_code ec;
    return std::filesystem::is_directory(p, ec) ? p.string() : std::string();
#endif
}

// The `drift` module: DRIFT_CANDELA_MODULE, the installed copy, or the
// source tree this program was built from.
std::string moduleDir() {
    if (const char* env = SDL_getenv("DRIFT_CANDELA_MODULE")) return env;
#if defined(__EMSCRIPTEN__) && defined(DRIFT_NODE_CHECK)
    return DRIFT_CANDELA_MODULE_DIR;   // read from the disk through Node.js
#elif defined(__EMSCRIPTEN__)
    return "/drift-module";   // embedded in drift.wasm
#else
    std::string dir = installed("module");
    return dir.empty() ? DRIFT_CANDELA_MODULE_DIR : dir;
#endif
}

// The candela standard library this program was built with: the installed
// copy, or libs/ beside the program in a build tree. Exported as
// CANDELA_LIB_PATH, which candela reads for imports and native libraries, so
// a separately installed candela of another version is not used. A
// CANDELA_LIB_PATH already set wins. The browser build has the library built
// in.
void useStdLib() {
#ifndef __EMSCRIPTEN__
    if (SDL_getenv("CANDELA_LIB_PATH")) return;
    std::string dir = installed("libs");
    if (dir.empty()) {
        const char* base = SDL_GetBasePath();
        std::error_code ec;
        if (base && std::filesystem::is_directory(std::filesystem::path(base) / "libs" / "std", ec)) {
            dir = (std::filesystem::path(base) / "libs").string();
        }
    }
    if (!dir.empty()) SDL_setenv_unsafe("CANDELA_LIB_PATH", dir.c_str(), 1);
#endif
}

const char* usage =
    "drift: runs Candela games, checks them and builds them for the web.\n"
    "\n"
    "Usage:\n"
    "  drift run [folder | script] [--config <path>] [--set section.key=value]...\n"
    "  drift check [folder | script] [--json]\n"
    "  drift build --web [folder] [-o <out folder>] [--dev]\n"
    "  drift --version\n"
    "  drift --help\n"
    "\n"
    "run plays the game in a folder (default: this one) or starts at a script.\n"
    "check reports what loading the game would refuse or warn about, without running it.\n"
    "  --json prints the problems as JSON, for an editor.\n"
    "build --web writes a page to serve over HTTP (default: <folder>/build/web).\n"
    "  --dev makes a page that hot reloads the game when the page is written again.\n";

// The plugins every game runs with; `drift run` and `drift check` add the
// same ones, so a check reads drift.toml the way a run does.
void addGamePlugins(App& app) {
    app.addPlugins<DefaultPlugins>();
    app.addPlugin<SavePlugin>();   // the slot named after [project]
    if (app.settingsStore().has("lighting")) app.addPlugin<LightingPlugin>();
}

// drift check [folder | script] [--json]: the game folder is the one given,
// or for a script the nearest folder above it with a drift.toml.
int checkCommand(int argc, char** argv, int first) {
    namespace fs = std::filesystem;
    bool asJson = false;
    std::string target;
    for (int i = first; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--json") asJson = true;
        else if (arg.rfind("-", 0) == 0) {
            std::fprintf(stderr, "drift check: unknown option %s\n\n%s", arg.c_str(), usage);
            return 2;
        } else if (target.empty()) target = arg;
        else {
            std::fprintf(stderr, "drift check: one game at a time\n\n%s", usage);
            return 2;
        }
    }
    std::error_code ec;
    fs::path given = fs::absolute(target.empty() ? fs::path(".") : fs::path(target), ec);
    if (!fs::exists(given, ec)) {
        std::fprintf(stderr, "drift check: %s: no such game folder or script\n", given.string().c_str());
        return 2;
    }
    fs::path folder = fs::is_directory(given, ec) ? given : given.parent_path();
    for (fs::path d = folder;; d = d.parent_path()) {
        if (fs::is_regular_file(d / "drift.toml", ec)) {
            folder = d;
            break;
        }
        if (d == d.parent_path()) break;
    }
    fs::current_path(folder, ec);
    if (ec) {
        std::fprintf(stderr, "drift check: %s: %s\n", folder.string().c_str(), ec.message().c_str());
        return 2;
    }
    useStdLib();

    App app;
    g_app = &app;
    std::string entry = app.config().project.entry;
    if (entry.empty()) entry = "main.cdl";
    addGamePlugins(app);
    app.loadSettings();
    // [input], read the way the input actions read it.
    {
        InputActions probe;
        InputActionsPlugin::bindFromSettings(probe, app.settingsStore(), {});
    }

    const Settings& settings = app.settingsStore();
    json found = json::array();
    for (const auto& p : settings.problems()) {
        fs::path file = fs::absolute(p.file, ec);
        json d = {{"file", file.string()}, {"severity", "warning"}, {"code", "drift_bad_setting"},
                  {"message", p.message}};
        if (p.line > 0) d["line"] = p.line;
        found.push_back(d);
    }
    const char* host = drift_host_check(entry.c_str(), moduleDir().c_str(), settings.file().c_str());
    json fromHost = json::parse(host ? host : "[]", nullptr, false);
    if (fromHost.is_array()) {
        for (auto& d : fromHost) found.push_back(d);
    }
    g_app = nullptr;

    bool errors = false;
    for (const auto& d : found) errors = errors || d.value("severity", "") == "error";
    if (asJson) {
        std::printf("%s\n", found.dump(2).c_str());
    } else {
        for (const auto& d : found) {
            std::string where = d.value("file", "");
            if (d.contains("line")) where += ":" + std::to_string(d["line"].get<int>());
            if (d.contains("column")) where += ":" + std::to_string(d["column"].get<int>());
            std::printf("%s: %s: %s [%s]\n", where.c_str(), d.value("severity", "").c_str(),
                        d.value("message", "").c_str(), d.value("code", "").c_str());
        }
        if (found.empty()) std::printf("drift check: %s has no problems\n", folder.string().c_str());
    }
    return errors ? 1 : 0;
}

#ifndef __EMSCRIPTEN__
// The web runtime `drift build --web` copies: DRIFT_WEB_RUNTIME, or the
// installed copy.
std::string webRuntimeDir() {
    if (const char* env = SDL_getenv("DRIFT_WEB_RUNTIME")) return env;
    return installed("web");
}

// drift build --web [folder] [-o <out>] [--dev]
int buildCommand(int argc, char** argv, int first) {
    bool web = false, dev = false;
    std::string folder, out;
    for (int i = first; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--web") web = true;
        else if (arg == "--dev") dev = true;
        else if (arg == "-o" && i + 1 < argc) out = argv[++i];
        else if (arg.rfind("-", 0) == 0) {
            std::fprintf(stderr, "drift build: unknown option %s\n\n%s", arg.c_str(), usage);
            return 2;
        } else if (folder.empty()) folder = arg;
        else {
            std::fprintf(stderr, "drift build: one game folder at a time\n\n%s", usage);
            return 2;
        }
    }
    if (!web) {
        std::fprintf(stderr, "drift build: only --web is supported so far; it writes a web page for the game\n");
        return 2;
    }
    if (folder.empty()) folder = ".";
    if (out.empty()) out = (std::filesystem::path(folder) / "build" / "web").string();
    std::string runtime = webRuntimeDir();
    if (runtime.empty()) {
        std::fprintf(stderr, "drift build: no web runtime installed beside this drift; "
                             "set DRIFT_WEB_RUNTIME to a folder holding drift.html, drift.js and drift.wasm\n");
        return 1;
    }
    std::string error, summary;
    if (!drift::web::buildWebPage(folder, out, runtime, dev, error, summary)) {
        std::fprintf(stderr, "drift build: %s\n", error.c_str());
        return 1;
    }
    std::printf("%s\nServe the folder over HTTP and open index.html; browsers do not run it from file://.\n",
                summary.c_str());
    return 0;
}
#endif

} // namespace

int main(int argc, char** argv) {
    // drift [run] [folder | script] [options], drift build --web ...
    int first = 1;
    std::string command = "run";
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--help" || arg == "-h" || arg == "help") {
            std::printf("%s", usage);
            return 0;
        }
        if (arg == "--version" || arg == "-V") {
            std::printf("drift %s\n", DRIFT_VERSION);
            return 0;
        }
        if (arg == "run" || arg == "build" || arg == "check") {
            command = arg;
            first = 2;
        }
    }
    if (command == "check") return checkCommand(argc, argv, first);
#ifdef DRIFT_NODE_CHECK
    // The Node.js build runs checks only.
    std::fprintf(stderr, "drift_check.js runs checks: node drift_check.js check [folder | script] [--json]\n");
    return 2;
#endif
#ifndef __EMSCRIPTEN__
    if (command == "build") return buildCommand(argc, argv, first);
#endif

    // The first argument that is not an option is the game: a folder to play
    // from, or the script to start at.
    std::string entry;
    for (int i = first; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" || arg == "--set") { ++i; continue; }
        if (arg.rfind("--", 0) == 0) continue;
        entry = arg;
        break;
    }
#ifndef __EMSCRIPTEN__
    if (!entry.empty()) {
        std::error_code ec;
        if (std::filesystem::is_directory(entry, ec)) {
            std::filesystem::current_path(entry, ec);
            if (ec) {
                std::fprintf(stderr, "drift run: %s: %s\n", entry.c_str(), ec.message().c_str());
                return 1;
            }
            entry.clear();
        } else if (!std::filesystem::is_regular_file(entry, ec)) {
            std::fprintf(stderr, "drift run: %s: no such game folder or script\n\n%s", entry.c_str(), usage);
            return 1;
        }
    }
#endif
    useStdLib();

    App app;
    g_app = &app;
    app.args(argc, argv);
    if (entry.empty()) entry = app.config().project.entry;
    if (entry.empty()) entry = "main.cdl";

    addGamePlugins(app);
    app.addPlugin(new CandelaPlugin(entry, moduleDir()));
    int code = app.run();
    drift_host_unload();
    g_app = nullptr;
    return g_loadFailed ? 1 : code;
}
