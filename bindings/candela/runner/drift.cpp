// drift: runs Candela games.
//
//     drift run [folder | script] [--config <path>] [--set section.key=value]...
//     drift build --web [folder] [-o <out folder>]
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
const char* drift_host_error();
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
    nlohmann::json value;
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

json vec(Vec2 v) { return {{"x", v.x}, {"y", v.y}}; }
Vec2 vec(const json& j, Vec2 fallback = {}) {
    if (!j.is_object()) return fallback;
    return {j.value("x", fallback.x), j.value("y", fallback.y)};
}
json color(Color c) { return {{"r", c.r / 255.0}, {"g", c.g / 255.0}, {"b", c.b / 255.0}, {"a", c.a / 255.0}}; }
uint8_t channel(double v) { return uint8_t(std::clamp(v, 0.0, 1.0) * 255.0 + 0.5); }
Color color(const json& j, Color fallback = Color::white()) {
    if (!j.is_object()) return fallback;
    return {channel(j.value("r", 1.0)), channel(j.value("g", 1.0)), channel(j.value("b", 1.0)), channel(j.value("a", 1.0))};
}
json rectJ(Rect r) { return {{"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h}}; }
Rect rectJ(const json& j) {
    if (!j.is_object()) return {};
    return {j.value("x", 0.f), j.value("y", 0.f), j.value("w", 0.f), j.value("h", 0.f)};
}
uint8_t layer(const json& j) { return uint8_t(std::clamp(j.value("layer", 0), 0, 31)); }

const char* blendNames[] = {"Alpha", "Additive", "Multiply", "Premultiplied", "Screen", "Opaque"};
BlendMode blend(const json& j) {
    std::string s = j.is_string() ? j.get<std::string>() : "Alpha";
    for (int i = 0; i < 6; ++i) if (s == blendNames[i]) return static_cast<BlendMode>(i);
    return BlendMode::Alpha;
}
template <typename E> E pick(const json& j, std::initializer_list<const char*> names) {
    std::string s = j.is_string() ? j.get<std::string>() : "";
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

json transformJ(Vec2 position, float rotation, Vec2 scale) {
    return json{{"position", vec(position)}, {"rotation", rotation}, {"scale", vec(scale)}};
}
json idsJ(const std::vector<EntityId>& ids) {
    json out = json::array();
    for (EntityId e : ids) out.push_back(e);
    return out;
}

bool hasParent(EntityId e) { return get<Parent>(e) != nullptr; }
json readParent(EntityId e) { return json{{"entity", {{"id", get<Parent>(e)->entity}}}, {"keep_world", false}}; }
void writeParent(EntityId e, const json& j) {
    json pe = j.value("entity", json::object());
    EntityId p = pe.is_object() ? pe.value("id", EntityId(0)) : InvalidEntityId;
    if (!setParent(world(), e, p, j.value("keep_world", false))) {
        DRIFT_LOG_WARN("Candela: cannot parent entity %llu to %llu (dead, a loop, or %d children already)",
                       (unsigned long long)e, (unsigned long long)p, MaxChildren);
    }
}
void removeParentKeepingPlace(EntityId e) { removeParent(world(), e, true); }

json readGlobal(EntityId e) {
    GlobalTransform2D g = worldTransformOf(world(), e);
    return transformJ(g.position, g.rotation, g.scale);
}
// Assigning a GlobalTransform moves the entity there in world space.
void writeGlobal(EntityId e, const json& j) {
    GlobalTransform2D g{vec(j.value("position", json())), j.value("rotation", 0.f), vec(j.value("scale", json()), {1, 1})};
    put(e, localTransformFor(world(), parentOf(world(), e), g));
}

bool hierarchyService(const std::string& f, const json& a, json& out) {
    if (f == "hier_parent") {
        out = parentOf(world(), a[0].get<EntityId>());
        return true;
    }
    if (f == "hier_children") {
        out = idsJ(childrenOf(world(), a[0].get<EntityId>()));
        return true;
    }
    if (f == "hier_descendants") {
        out = idsJ(descendantsOf(world(), a[0].get<EntityId>()));
        return true;
    }
    if (f == "hier_world") {
        GlobalTransform2D g = worldTransformOf(world(), a[0].get<EntityId>());
        out = json::array({g.position.x, g.position.y, g.rotation, g.scale.x, g.scale.y});
        return true;
    }
    if (f == "hier_to_world" || f == "hier_to_local") {
        EntityId e = a[0].get<EntityId>();
        Vec2 p{a[1].get<float>(), a[2].get<float>()};
        Vec2 r = f == "hier_to_world" ? localToWorld(world(), e, p) : worldToLocal(world(), e, p);
        out = json::array({r.x, r.y});
        return true;
    }
    return false;
}

std::vector<Vec2> pointsOf(const json& flat) {
    std::vector<Vec2> pts(flat.size() / 2);
    for (size_t i = 0; i < pts.size(); ++i) pts[i] = {flat[i * 2].get<float>(), flat[i * 2 + 1].get<float>()};
    return pts;
}
std::vector<float> floatsOf(const json& j) {
    std::vector<float> v;
    for (const auto& x : j) v.push_back(x.get<float>());
    return v;
}
void pushPoints(json& out, const std::vector<Vec2>& pts) {
    for (Vec2 p : pts) {
        out.push_back(p.x);
        out.push_back(p.y);
    }
}

// The solvers of drift/gameplay/ProceduralAnimation.hpp; state crosses as
// flat float lists and comes back the same way.
bool animService(const std::string& f, const json& a, json& out) {
    auto F = [&](size_t i) { return a[i].get<float>(); };
    if (f == "anim_spring") {
        // kind (0 float, 1 angle, 2 critically damped), value, velocity, target, stiffness or smooth time, damping, dt
        float v = F(1), vel = F(2);
        int kind = a[0].get<int>();
        if (kind == 0) springStep(v, vel, F(3), F(4), F(5), F(6));
        else if (kind == 1) angleSpringStep(v, vel, F(3), F(4), F(5), F(6));
        else v = smoothDamp(v, F(3), vel, F(4), F(6));
        out = json::array({v, vel});
        return true;
    }
    if (f == "anim_spring2") {
        // kind (0 spring, 2 critically damped), value, velocity, target (x, y each), stiffness or smooth time, damping, dt
        Vec2 v{F(1), F(2)}, vel{F(3), F(4)}, t{F(5), F(6)};
        if (a[0].get<int>() == 0) springStep(v, vel, t, F(7), F(8), F(9));
        else v = smoothDamp(v, t, vel, F(7), F(9));
        out = json::array({v.x, v.y, vel.x, vel.y});
        return true;
    }
    if (f == "anim_two_bone") {
        TwoBonePose p = twoBoneIk({F(0), F(1)}, {F(2), F(3)}, F(4), F(5), F(6));
        out = json::array({p.joint.x, p.joint.y, p.end.x, p.end.y, p.upperAngle, p.lowerAngle, p.reached ? 1.f : 0.f});
        return true;
    }
    if (f == "anim_fabrik") {
        // joints, lengths, target x, y, iterations, tolerance, base angle, min angles, max angles
        std::vector<Vec2> joints = pointsOf(a[0]);
        std::vector<float> lengths = floatsOf(a[1]), lo = floatsOf(a[7]), hi = floatsOf(a[8]);
        FabrikOptions o;
        o.iterations = a[4].get<int>();
        o.tolerance = F(5);
        o.baseAngle = F(6);
        o.minAngle = lo;
        o.maxAngle = hi;
        bool reached = solveFabrik(joints, lengths, {F(2), F(3)}, o);
        out = json::array({reached ? 1.f : 0.f});
        pushPoints(out, joints);
        return true;
    }
    if (f == "anim_verlet") {
        // points, previous, anchor x, y, segment, dt, gravity x, y, damping, iterations, stiffness, ground, pin end, end x, y
        std::vector<Vec2> pts = pointsOf(a[0]), prev = pointsOf(a[1]);
        prev.resize(pts.size());
        VerletOptions o;
        o.gravity = {F(6), F(7)};
        o.damping = F(8);
        o.iterations = a[9].get<int>();
        o.stiffness = F(10);
        o.ground = F(11);
        o.pinEnd = a[12].get<bool>();
        o.endTarget = {F(13), F(14)};
        stepVerlet(pts, prev, {F(2), F(3)}, F(4), F(5), o);
        out = json::array();
        pushPoints(out, pts);
        pushPoints(out, prev);
        return true;
    }
    if (f == "anim_foot") {
        // position, from, to, progress, rest, distance, duration, height, lead, up, dt, can step
        Foot ft;
        ft.position = {F(0), F(1)};
        ft.from = {F(2), F(3)};
        ft.to = {F(4), F(5)};
        ft.progress = F(6);
        StepOptions o;
        o.distance = F(9);
        o.duration = F(10);
        o.height = F(11);
        o.lead = F(12);
        o.up = {F(13), F(14)};
        bool landed = ft.update({F(7), F(8)}, o, F(15), a[16].get<bool>());
        out = json::array({ft.position.x, ft.position.y, ft.from.x, ft.from.y, ft.to.x, ft.to.y, ft.progress,
                           landed ? 1.f : 0.f});
        return true;
    }
    return false;
}

// ---- Engine components ----

struct Codec {
    const char* name;
    bool (*has)(EntityId);
    json (*read)(EntityId);
    void (*write)(EntityId, const json&);
    void (*remove)(EntityId);
    ComponentId (*id)();
};

bool isShape(EntityId e, ShapeKind k) {
    const Shape2D* s = get<Shape2D>(e);
    return s && s->kind == k;
}

json readShape(EntityId e) {
    const Shape2D& s = *get<Shape2D>(e);
    json j = {{"color", color(s.color)}, {"outline", s.outline}, {"z", s.zOrder},
              {"layer", s.layer}, {"visible", s.visible}};
    if (s.kind == ShapeKind::Rectangle) {
        j["size"] = vec(s.size);
        j["corner_radius"] = s.cornerRadius;
        j["anchor"] = vec(s.anchor);
    } else {
        j["radius"] = s.radius;
    }
    return j;
}

void writeShape(EntityId e, const json& j, ShapeKind kind) {
    Shape2D s;
    s.kind = kind;
    s.size = vec(j.value("size", json()), {32, 32});
    s.radius = j.value("radius", 16.f);
    s.color = color(j.value("color", json()));
    s.outline = j.value("outline", 0.f);
    s.cornerRadius = j.value("corner_radius", 0.f);
    s.anchor = vec(j.value("anchor", json()), {0.5f, 0.5f});
    s.zOrder = j.value("z", 0.f);
    s.layer = layer(j);
    s.visible = j.value("visible", true);
    put(e, s);
}

ColorF colorF(const json& j, ColorF fallback) {
    if (!j.is_object()) return fallback;
    return {j.value("r", 1.f), j.value("g", 1.f), j.value("b", 1.f), j.value("a", 1.f)};
}
json colorF(ColorF c) { return {{"r", c.r}, {"g", c.g}, {"b", c.b}, {"a", c.a}}; }

const char* shapeNames[] = {"Point", "Circle", "Ring", "Box", "Line"};

EmitterConfig emitterConfig(const json& j) {
    EmitterConfig c;
    c.spawnRate = j.value("rate", 20.f);
    c.lifetime = {j.value("lifetime_min", 0.5f), j.value("lifetime_max", 1.5f)};
    c.speed = {j.value("speed_min", 20.f), j.value("speed_max", 60.f)};
    c.angle = {j.value("angle_min", 0.f), j.value("angle_max", 6.2831853f)};
    float s0 = j.value("size_start", 6.f), s1 = j.value("size_end", 1.f);
    c.sizeStart = {s0, s0};
    c.sizeEnd = {s1, s1};
    c.colorStart = colorF(j.value("color_start", json()), {1, 1, 1, 1});
    c.colorEnd = colorF(j.value("color_end", json()), {1, 1, 1, 0});
    c.gravity = vec(j.value("gravity", json()));
    c.drag = j.value("drag", 0.f);
    c.maxParticles = std::max(1, j.value("max_particles", 256));
    c.blendMode = blend(j.value("blend", json()));
    c.texture = TextureHandle{j.value("texture", json::object()).value("id", 0u)};
    if (!c.texture.valid()) {
        if (auto* r = res<RendererResource>()) c.texture = r->whiteTexture();
    }
    c.shape = pick<EmissionShape>(j.value("shape", json()), {"Point", "Circle", "Ring", "Box", "Line"});
    c.shapeRadius = j.value("shape_radius", 0.f);
    c.shapeExtents = vec(j.value("shape_extents", json()));
    c.duration = j.value("duration", -1.f);
    c.looping = j.value("looping", true);
    c.zOrder = j.value("z", 10.f);
    // A `_max` of 0 is the same as the minimum.
    float s0max = j.value("size_start_max", 0.f), s1max = j.value("size_end_max", 0.f);
    if (s0max > 0.f) c.sizeStart.max = s0max;
    if (s1max > 0.f) c.sizeEnd.max = s1max;
    c.srcRect = rectJ(j.value("frame", json()));
    c.initialRotation = {j.value("rotation_min", 0.f), j.value("rotation_max", 0.f)};
    c.angularVelocity = {j.value("spin_min", 0.f), j.value("spin_max", 0.f)};
    c.velocityInheritance = j.value("inherit_velocity", 0.f);
    c.space = j.value("local", false) ? ParticleSpace::Local : ParticleSpace::World;
    c.preWarmTime = j.value("prewarm", 0.f);
    Rect bounds = rectJ(j.value("bounds", json()));
    if (bounds.w > 0.f && bounds.h > 0.f) {
        c.boundsMin = {bounds.x, bounds.y};
        c.boundsMax = {bounds.x + bounds.w, bounds.y + bounds.h};
    }
    c.seed = static_cast<uint32_t>(j.value("seed", int64_t(0)));
    for (const auto& b : j.value("bursts", json::array())) {
        c.bursts.push_back(BurstEntry{b.value("time", 0.f), b.value("count", 10), b.value("cycles", 1), b.value("interval", 0.f)});
    }
    return c;
}

json emitterJson(const ParticleEmitter& pe) {
    const EmitterConfig& c = pe.config;
    auto* r = res<RendererResource>();
    uint32_t tex = r && c.texture.id == r->whiteTexture().id ? 0u : c.texture.id;
    json bursts = json::array();
    for (const BurstEntry& b : c.bursts) {
        bursts.push_back({{"time", b.time}, {"count", b.count}, {"cycles", b.cycles}, {"interval", b.interval}});
    }
    return json{{"rate", c.spawnRate}, {"lifetime_min", c.lifetime.min}, {"lifetime_max", c.lifetime.max},
            {"speed_min", c.speed.min}, {"speed_max", c.speed.max}, {"angle_min", c.angle.min},
            {"angle_max", c.angle.max}, {"size_start", c.sizeStart.min}, {"size_end", c.sizeEnd.min},
            {"color_start", colorF(c.colorStart)}, {"color_end", colorF(c.colorEnd)},
            {"gravity", vec(c.gravity)}, {"drag", c.drag}, {"max_particles", c.maxParticles},
            {"blend", blendNames[int(c.blendMode)]}, {"texture", {{"id", tex}}},
            {"shape", shapeNames[int(c.shape)]}, {"shape_radius", c.shapeRadius},
            {"shape_extents", vec(c.shapeExtents)}, {"duration", c.duration}, {"looping", c.looping},
            {"z", c.zOrder}, {"playing", pe.playing},
            {"frame", rectJ(c.srcRect)},
            {"size_start_max", c.sizeStart.max == c.sizeStart.min ? 0.f : c.sizeStart.max},
            {"size_end_max", c.sizeEnd.max == c.sizeEnd.min ? 0.f : c.sizeEnd.max},
            {"rotation_min", c.initialRotation.min}, {"rotation_max", c.initialRotation.max},
            {"spin_min", c.angularVelocity.min}, {"spin_max", c.angularVelocity.max},
            {"inherit_velocity", c.velocityInheritance}, {"local", c.space == ParticleSpace::Local},
            {"prewarm", c.preWarmTime},
            {"bounds", c.boundsMin.x == c.boundsMax.x && c.boundsMin.y == c.boundsMax.y ? rectJ(Rect{})
                                                  : rectJ(Rect{c.boundsMin.x, c.boundsMin.y, c.boundsMax.x - c.boundsMin.x,
                                                                c.boundsMax.y - c.boundsMin.y})},
            {"seed", c.seed}, {"bursts", bursts}};
}

const char* easeNames[] = {
    "Linear", "InSine", "OutSine", "InOutSine", "InQuad", "OutQuad", "InOutQuad",
    "InCubic", "OutCubic", "InOutCubic", "InQuart", "OutQuart", "InOutQuart",
    "InQuint", "OutQuint", "InOutQuint", "InExpo", "OutExpo", "InOutExpo",
    "InCirc", "OutCirc", "InOutCirc", "InBack", "OutBack", "InOutBack",
    "InElastic", "OutElastic", "InOutElastic", "InBounce", "OutBounce", "InOutBounce", "Spring"};

Ease easeOf(const json& j) {
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
void startTween(EntityId e, const json& j) {
    // The same value written back while it runs leaves it running; once it
    // has finished, inserting it again plays it again.
    auto* tw = res<Tweens>();
    auto it = g_tweens.find(e);
    if (it != g_tweens.end() && it->second.value == j && tw && tw->isActive(it->second.id)) return;
    stopTween(e);
    if (!tw) return;
    TweenBuilder b = tw->on(e);
    for (const auto& st : j.value("steps", json::array())) {
        float a = st.value("a", 0.f), bb = st.value("b", 0.f), secs = st.value("seconds", 0.f);
        Ease ez = easeOf(st.value("ease", json()));
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
    }
    int loops = j.value("loops", 0);
    if (loops != 0) b.loop(loops, j.value("mirror", false) ? TweenLoop::Yoyo : TweenLoop::Restart);
    g_tweens[e] = {b.start(), j};
}

// A collider's category and mask bits; out of range values keep the defaults.
CollisionFilter filterOf(const json& j) {
    CollisionFilter f;
    int64_t cat = j.value("category", int64_t(1)), mask = j.value("mask", int64_t(0xFFFF));
    f.category = cat >= 0 && cat <= 0xFFFFFFFFll ? uint32_t(cat) : 1u;
    f.mask = mask >= 0 && mask <= 0xFFFFFFFFll ? uint32_t(mask) : 0xFFFFu;
    return f;
}

const char* jointKinds[] = {"Distance", "Revolute", "Prismatic", "Weld"};

const char* textAligns[] = {"Left", "Center", "Right"};
const char* textVAligns[] = {"Top", "Middle", "Bottom"};

std::vector<Codec> presentationCodecs();
std::vector<Codec> effectCodecs();
std::vector<Codec> animationCodecs();

const std::vector<Codec>& codecs() {
    static const std::vector<Codec> list = [] {
    std::vector<Codec> l = {
        {"Transform",
         [](EntityId e) { return get<Transform2D>(e) != nullptr; },
         [](EntityId e) {
             const Transform2D& t = *get<Transform2D>(e);
             return json{{"position", vec(t.position)}, {"rotation", t.rotation}, {"scale", vec(t.scale)}};
         },
         [](EntityId e, const json& j) {
             Transform2D t;
             t.position = vec(j.value("position", json()));
             t.rotation = j.value("rotation", 0.f);
             t.scale = vec(j.value("scale", json()), {1, 1});
             put(e, t);
             teleport(e, t.position, t.rotation);
         },
         [](EntityId e) { world().removeComponent(e, cid<Transform2D>()); },
         [] { return cid<Transform2D>(); }},
        {"Sprite",
         [](EntityId e) { return get<Sprite>(e) != nullptr; },
         [](EntityId e) {
             const Sprite& s = *get<Sprite>(e);
             Vec2 size = s.srcRect.w > 0 ? Vec2{s.srcRect.w, s.srcRect.h} : textureSize(s.texture);
             return json{{"image", {{"id", s.texture.id}}},
                         {"frame", rectJ(s.srcRect)},
                         {"anchor", vec(Vec2{size.x > 0 ? s.origin.x / size.x : 0.5f, size.y > 0 ? s.origin.y / size.y : 0.5f})},
                         {"color", color(s.tint)},
                         {"flip_x", (int(s.flip) & int(Flip::H)) != 0},
                         {"flip_y", (int(s.flip) & int(Flip::V)) != 0},
                         {"z", s.zOrder},
                         {"layer", s.layer},
                         {"visible", s.visible},
                         {"blend", blendNames[int(s.blend)]},
                         {"material", {{"id", s.material.id}}}};
         },
         [](EntityId e, const json& j) {
             Sprite s;
             s.material = MaterialHandle{j.value("material", json::object()).value("id", 0u)};
             s.texture = TextureHandle{j.value("image", json::object()).value("id", 0u)};
             s.srcRect = rectJ(j.value("frame", json()));
             Vec2 size = s.srcRect.w > 0 ? Vec2{s.srcRect.w, s.srcRect.h} : textureSize(s.texture);
             Vec2 anchor = vec(j.value("anchor", json()), {0.5f, 0.5f});
             s.origin = {anchor.x * size.x, anchor.y * size.y};
             s.tint = color(j.value("color", json()));
             int flip = (j.value("flip_x", false) ? int(Flip::H) : 0) | (j.value("flip_y", false) ? int(Flip::V) : 0);
             s.flip = static_cast<Flip>(flip);
             s.zOrder = j.value("z", 0.f);
             s.layer = layer(j);
             s.visible = j.value("visible", true);
             s.blend = blend(j.value("blend", json()));
             put(e, s);
         },
         [](EntityId e) { world().removeComponent(e, cid<Sprite>()); },
         [] { return cid<Sprite>(); }},
        {"Rectangle",
         [](EntityId e) { return isShape(e, ShapeKind::Rectangle); },
         readShape,
         [](EntityId e, const json& j) { writeShape(e, j, ShapeKind::Rectangle); },
         [](EntityId e) { world().removeComponent(e, cid<Shape2D>()); },
         [] { return cid<Shape2D>(); }},
        {"Circle",
         [](EntityId e) { return isShape(e, ShapeKind::Circle); },
         readShape,
         [](EntityId e, const json& j) { writeShape(e, j, ShapeKind::Circle); },
         [](EntityId e) { world().removeComponent(e, cid<Shape2D>()); },
         [] { return cid<Shape2D>(); }},
        {"Text",
         [](EntityId e) { return get<Text>(e) != nullptr; },
         [](EntityId e) {
             const Text& t = *get<Text>(e);
             return json{{"value", std::string(t.str())}, {"size", t.size}, {"color", color(t.color)},
                         {"align", textAligns[int(t.align)]}, {"valign", textVAligns[int(t.valign)]},
                         {"wrap", t.wrapWidth}, {"line_spacing", t.lineSpacing},
                         {"outline", t.outlineWidth}, {"outline_color", color(t.outlineColor)},
                         {"shadow_offset", vec(t.shadowOffset)}, {"shadow_color", color(t.shadowColor)},
                         {"rich_text", t.richText}, {"font", {{"id", t.font.id}}},
                         {"z", t.zOrder}, {"layer", t.layer}, {"visible", t.visible}};
         },
         [](EntityId e, const json& j) {
             Text t;
             t.set(j.value("value", ""));
             t.size = j.value("size", 24.f);
             t.color = color(j.value("color", json()));
             t.align = pick<TextAlign>(j.value("align", json()), {"Left", "Center", "Right"});
             t.valign = pick<TextVAlign>(j.value("valign", json()), {"Top", "Middle", "Bottom"});
             t.wrapWidth = j.value("wrap", 0.f);
             t.lineSpacing = j.value("line_spacing", 1.f);
             t.outlineWidth = j.value("outline", 0.f);
             t.outlineColor = color(j.value("outline_color", json()), Color::black());
             t.shadowOffset = vec(j.value("shadow_offset", json()));
             t.shadowColor = color(j.value("shadow_color", json()), Color{0, 0, 0, 160});
             t.richText = j.value("rich_text", false);
             t.font = FontHandle{j.value("font", json::object()).value("id", 0u)};
             t.zOrder = j.value("z", 0.f);
             t.layer = layer(j);
             t.visible = j.value("visible", true);
             put(e, t);
         },
         [](EntityId e) { world().removeComponent(e, cid<Text>()); },
         [] { return cid<Text>(); }},
        {"Velocity",
         [](EntityId e) { return get<Velocity2D>(e) != nullptr; },
         [](EntityId e) {
             const Velocity2D& v = *get<Velocity2D>(e);
             return json{{"linear", vec(v.linear)}, {"angular", v.angular}};
         },
         [](EntityId e, const json& j) { put(e, Velocity2D{vec(j.value("linear", json())), j.value("angular", 0.f)}); },
         [](EntityId e) { world().removeComponent(e, cid<Velocity2D>()); },
         [] { return cid<Velocity2D>(); }},
        {"RigidBody",
         [](EntityId e) { return get<RigidBody2D>(e) != nullptr; },
         [](EntityId e) {
             const RigidBody2D& b = *get<RigidBody2D>(e);
             return json{{"kind", name(b.type, {"Static", "Dynamic", "Kinematic"})},
                         {"fixed_rotation", b.fixedRotation}, {"gravity_scale", b.gravityScale},
                         {"linear_damping", b.linearDamping}, {"angular_damping", b.angularDamping}};
         },
         [](EntityId e, const json& j) {
             RigidBody2D b;
             b.type = pick<BodyType>(j.value("kind", json()), {"Static", "Dynamic", "Kinematic"});
             b.fixedRotation = j.value("fixed_rotation", false);
             b.gravityScale = j.value("gravity_scale", 1.f);
             b.linearDamping = j.value("linear_damping", 0.f);
             b.angularDamping = j.value("angular_damping", 0.f);
             put(e, b);
         },
         [](EntityId e) { world().removeComponent(e, cid<RigidBody2D>()); },
         [] { return cid<RigidBody2D>(); }},
        {"BoxCollider",
         [](EntityId e) { return get<BoxCollider2D>(e) != nullptr; },
         [](EntityId e) {
             const BoxCollider2D& c = *get<BoxCollider2D>(e);
             return json{{"half_size", vec(c.halfSize)}, {"offset", vec(c.offset)}, {"sensor", c.isSensor},
                         {"density", c.density}, {"friction", c.friction}, {"restitution", c.restitution},
                         {"category", c.filter.category}, {"mask", c.filter.mask}};
         },
         [](EntityId e, const json& j) {
             BoxCollider2D c;
             c.halfSize = vec(j.value("half_size", json()), {16, 16});
             c.offset = vec(j.value("offset", json()));
             c.isSensor = j.value("sensor", false);
             c.density = j.value("density", 1.f);
             c.friction = j.value("friction", 0.3f);
             c.restitution = j.value("restitution", 0.f);
             c.filter = filterOf(j);
             put(e, c);
         },
         [](EntityId e) { world().removeComponent(e, cid<BoxCollider2D>()); },
         [] { return cid<BoxCollider2D>(); }},
        {"CircleCollider",
         [](EntityId e) { return get<CircleCollider2D>(e) != nullptr; },
         [](EntityId e) {
             const CircleCollider2D& c = *get<CircleCollider2D>(e);
             return json{{"radius", c.radius}, {"offset", vec(c.offset)}, {"sensor", c.isSensor},
                         {"density", c.density}, {"friction", c.friction}, {"restitution", c.restitution},
                         {"category", c.filter.category}, {"mask", c.filter.mask}};
         },
         [](EntityId e, const json& j) {
             CircleCollider2D c;
             c.radius = j.value("radius", 16.f);
             c.offset = vec(j.value("offset", json()));
             c.isSensor = j.value("sensor", false);
             c.density = j.value("density", 1.f);
             c.friction = j.value("friction", 0.3f);
             c.restitution = j.value("restitution", 0.f);
             c.filter = filterOf(j);
             put(e, c);
         },
         [](EntityId e) { world().removeComponent(e, cid<CircleCollider2D>()); },
         [] { return cid<CircleCollider2D>(); }},
        {"TextureCollider",
         [](EntityId e) { return get<TextureCollider2D>(e) != nullptr; },
         [](EntityId e) {
             const TextureCollider2D& c = *get<TextureCollider2D>(e);
             return json{{"threshold", c.threshold}, {"tolerance", c.tolerance}, {"min_area", c.minArea},
                         {"follow_sprite", c.followSprite}, {"sensor", c.isSensor}, {"density", c.density},
                         {"friction", c.friction}, {"restitution", c.restitution},
                         {"category", c.filter.category}, {"mask", c.filter.mask}};
         },
         [](EntityId e, const json& j) {
             TextureCollider2D c;
             c.threshold = j.value("threshold", 0.5f);
             c.tolerance = j.value("tolerance", 1.f);
             c.minArea = j.value("min_area", 4.f);
             c.followSprite = j.value("follow_sprite", false);
             c.isSensor = j.value("sensor", false);
             c.density = j.value("density", 1.f);
             c.friction = j.value("friction", 0.3f);
             c.restitution = j.value("restitution", 0.f);
             c.filter = filterOf(j);
             put(e, c);
         },
         [](EntityId e) { world().removeComponent(e, cid<TextureCollider2D>()); },
         [] { return cid<TextureCollider2D>(); }},
        {"Joint",
         [](EntityId e) { return get<Joint2D>(e) != nullptr; },
         [](EntityId e) {
             const Joint2D& j = *get<Joint2D>(e);
             return json{{"kind", jointKinds[int(j.type)]}, {"target", {{"id", j.target}}},
                         {"anchor", vec(j.anchor)}, {"target_anchor", vec(j.targetAnchor)},
                         {"length", j.length}, {"min_length", j.minLength}, {"max_length", j.maxLength},
                         {"stiffness", j.stiffness}, {"damping", j.damping}, {"limit", j.limit},
                         {"lower", j.lower}, {"upper", j.upper}, {"motor", j.motor},
                         {"motor_speed", j.motorSpeed}, {"max_motor_force", j.maxMotorForce},
                         {"axis", vec(j.axis)}, {"collide_connected", j.collideConnected}};
         },
         [](EntityId e, const json& j) {
             Joint2D d;
             d.type = pick<JointType>(j.value("kind", json()), {"Distance", "Revolute", "Prismatic", "Weld"});
             d.target = j.value("target", json::object()).value("id", EntityId(0));
             if (d.target == 0) d.target = InvalidEntityId;
             d.anchor = vec(j.value("anchor", json()));
             d.targetAnchor = vec(j.value("target_anchor", json()));
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
             d.axis = vec(j.value("axis", json()), {1, 0});
             d.collideConnected = j.value("collide_connected", false);
             put(e, d);
         },
         [](EntityId e) { world().removeComponent(e, cid<Joint2D>()); },
         [] { return cid<Joint2D>(); }},
        {"AudioSource",
         [](EntityId e) { return get<AudioSource>(e) != nullptr; },
         [](EntityId e) {
             const AudioSource& a = *get<AudioSource>(e);
             return json{{"sound", {{"id", a.sound.id}}}, {"bus", {{"id", a.bus.id}}}, {"volume", a.volume},
                         {"pitch", a.pitch}, {"looping", a.loop}, {"autoplay", a.autoplay},
                         {"spatial", a.spatial}, {"min_distance", a.minDistance},
                         {"max_distance", a.maxDistance}, {"rolloff", a.rolloff == Rolloff::Inverse ? "Inverse" : "Linear"},
                         {"priority", a.priority}, {"play", a.playRequested}};
         },
         [](EntityId e, const json& j) {
             AudioSource a;
             const AudioSource* cur = get<AudioSource>(e);
             if (cur) a = *cur;   // keeps the playback the engine started
             a.sound = SoundHandle{j.value("sound", json::object()).value("id", 0u)};
             a.bus = BusHandle{j.value("bus", json::object()).value("id", 0u)};
             a.volume = j.value("volume", 1.f);
             a.pitch = j.value("pitch", 1.f);
             a.loop = j.value("looping", false);
             a.autoplay = j.value("autoplay", true);
             a.spatial = j.value("spatial", false);
             a.minDistance = j.value("min_distance", 64.f);
             a.maxDistance = j.value("max_distance", 1024.f);
             a.rolloff = pick<Rolloff>(j.value("rolloff", json()), {"Linear", "Inverse"});
             a.priority = j.value("priority", 0);
             a.playRequested = j.value("play", false) || (cur && cur->sound != a.sound);
             put(e, a);
         },
         [](EntityId e) { world().removeComponent(e, cid<AudioSource>()); },
         [] { return cid<AudioSource>(); }},
        {"SpatialBounds",
         [](EntityId e) { return get<SpatialBounds>(e) != nullptr; },
         [](EntityId e) {
             const SpatialBounds& b = *get<SpatialBounds>(e);
             return json{{"half_size", vec(b.halfSize)}, {"offset", vec(b.offset)}};
         },
         [](EntityId e, const json& j) { put(e, SpatialBounds{vec(j.value("half_size", json())), vec(j.value("offset", json()))}); },
         [](EntityId e) { world().removeComponent(e, cid<SpatialBounds>()); },
         [] { return cid<SpatialBounds>(); }},
        {"AudioListener",
         [](EntityId e) { return get<AudioListener>(e) != nullptr; },
         [](EntityId e) { return json{{"active", get<AudioListener>(e)->active}}; },
         [](EntityId e, const json& j) { put(e, AudioListener{j.value("active", true)}); },
         [](EntityId e) { world().removeComponent(e, cid<AudioListener>()); },
         [] { return cid<AudioListener>(); }},
        {"Lifetime",
         [](EntityId e) { return get<Lifetime>(e) != nullptr; },
         [](EntityId e) { return json{{"seconds", get<Lifetime>(e)->seconds}}; },
         [](EntityId e, const json& j) { put(e, Lifetime{j.value("seconds", 1.f)}); },
         [](EntityId e) { world().removeComponent(e, cid<Lifetime>()); },
         [] { return cid<Lifetime>(); }},
        {"Oscillate",
         [](EntityId e) { return get<Oscillate>(e) != nullptr; },
         [](EntityId e) {
             const Oscillate& o = *get<Oscillate>(e);
             return json{{"amplitude", vec(o.amplitude)}, {"frequency", o.frequency}, {"phase", o.phase}};
         },
         [](EntityId e, const json& j) {
             Oscillate o;
             if (const Oscillate* cur = get<Oscillate>(e)) o = *cur;
             o.amplitude = vec(j.value("amplitude", json()), {0, 8});
             o.frequency = j.value("frequency", 1.f);
             o.phase = j.value("phase", 0.f);
             put(e, o);
         },
         [](EntityId e) { world().removeComponent(e, cid<Oscillate>()); },
         [] { return cid<Oscillate>(); }},
        {"Follow",
         [](EntityId e) { return get<Follow>(e) != nullptr; },
         [](EntityId e) {
             const Follow& f = *get<Follow>(e);
             return json{{"target", {{"id", f.target == InvalidEntityId ? 0 : f.target}}}, {"speed", f.speed},
                         {"distance", f.distance}, {"offset", vec(f.offset)}};
         },
         [](EntityId e, const json& j) {
             Follow f;
             EntityId t = j.value("target", json::object()).value("id", EntityId(0));
             f.target = t == 0 ? InvalidEntityId : t;
             f.speed = j.value("speed", 100.f);
             f.distance = j.value("distance", 0.f);
             f.offset = vec(j.value("offset", json()));
             put(e, f);
         },
         [](EntityId e) { world().removeComponent(e, cid<Follow>()); },
         [] { return cid<Follow>(); }},
        {"Orbit",
         [](EntityId e) { return get<Orbit>(e) != nullptr; },
         [](EntityId e) {
             const Orbit& o = *get<Orbit>(e);
             return json{{"center", {{"id", o.center == InvalidEntityId ? 0 : o.center}}}, {"point", vec(o.point)},
                         {"radius", o.radius}, {"speed", o.speed}, {"angle", o.angle}};
         },
         [](EntityId e, const json& j) {
             Orbit o;
             EntityId c = j.value("center", json::object()).value("id", EntityId(0));
             o.center = c == 0 ? InvalidEntityId : c;
             o.point = vec(j.value("point", json()));
             o.radius = j.value("radius", 64.f);
             o.speed = j.value("speed", 1.f);
             o.angle = j.value("angle", 0.f);
             put(e, o);
         },
         [](EntityId e) { world().removeComponent(e, cid<Orbit>()); },
         [] { return cid<Orbit>(); }},
        {"ParticleEmitter",
         [](EntityId e) { return get<ParticleEmitter>(e) != nullptr; },
         [](EntityId e) { return emitterJson(*get<ParticleEmitter>(e)); },
         [](EntityId e, const json& j) {
             ParticleEmitter pe;
             pe.config = emitterConfig(j);
             pe.playing = j.value("playing", true);
             put(e, pe);
         },
         [](EntityId e) { world().removeComponent(e, cid<ParticleEmitter>()); },
         [] { return cid<ParticleEmitter>(); }},
        {"Tween",
         [](EntityId e) { return g_tweens.count(e) > 0; },
         [](EntityId e) { return g_tweens.at(e).value; },
         [](EntityId e, const json& j) { startTween(e, j); },
         [](EntityId e) { stopTween(e); },
         [] { return cid<Transform2D>(); }},
        {"Name",
         [](EntityId e) { return get<Name>(e) != nullptr; },
         [](EntityId e) { return json{{"value", std::string(get<Name>(e)->value)}}; },
         [](EntityId e, const json& j) {
             Name n;
             std::string v = j.value("value", "");
             std::snprintf(n.value, sizeof(n.value), "%s", v.c_str());
             put(e, n);
         },
         [](EntityId e) { world().removeComponent(e, cid<Name>()); },
         [] { return cid<Name>(); }},
        {"Tilemap",
         [](EntityId e) { return get<Tilemap>(e) != nullptr; },
         [](EntityId e) {
             const Tilemap& t = *get<Tilemap>(e);
             return json{{"map", {{"id", t.map.id}}}, {"z", t.zOrder}, {"layer", t.renderLayer},
                         {"colliders", t.colliders}, {"visible", t.visible}};
         },
         [](EntityId e, const json& j) {
             Tilemap t;
             if (const Tilemap* cur = get<Tilemap>(e)) t = *cur;
             t.map = TilemapHandle{j.value("map", json::object()).value("id", 0u)};
             t.zOrder = j.value("z", 0.f);
             t.renderLayer = layer(j);
             t.colliders = j.value("colliders", true);
             t.visible = j.value("visible", true);
             put(e, t);
         },
         [](EntityId e) { world().removeComponent(e, cid<Tilemap>()); },
         [] { return cid<Tilemap>(); }},
        {"Parent", hasParent, readParent, writeParent, removeParentKeepingPlace, [] { return cid<Parent>(); }},
        {"GlobalTransform",
         [](EntityId e) { return get<Transform2D>(e) != nullptr; },
         readGlobal,
         writeGlobal,
         [](EntityId) {},
         [] { return cid<Transform2D>(); }},
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
uint32_t idOf(const json& j) { return j.is_object() ? j.value("id", 0u) : 0u; }
uint32_t maskOf(const json& j) { return static_cast<uint32_t>(j.value("layer_mask", int64_t(AllWorldLayers))); }
float num(const json& a, size_t i, float fallback = 0.f) {
    return i < a.size() && a[i].is_number() ? a[i].get<float>() : fallback;
}

Camera cameraOf(const json& j) {
    Camera c;
    c.zoom = j.value("zoom", 1.f);
    c.rotation = j.value("rotation", 0.f);
    c.active = j.value("active", true);
    c.viewport = j.contains("viewport") ? rectJ(j["viewport"]) : Rect{0.f, 0.f, 1.f, 1.f};
    c.order = j.value("order", 0);
    c.layerMask = maskOf(j);
    c.target = TextureHandle{idOf(j.value("target", json()))};
    c.clear = j.value("clear", false);
    c.clearColor = colorF(j.value("clear_color", json()), {0.f, 0.f, 0.f, 1.f});
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
        {"Camera",
         [](EntityId e) { return get<Camera>(e) != nullptr; },
         [](EntityId e) {
             const Camera& c = *get<Camera>(e);
             return json{{"zoom", c.zoom}, {"rotation", c.rotation}, {"active", c.active},
                         {"viewport", rectJ(c.viewport)}, {"order", c.order}, {"layer_mask", c.layerMask},
                         {"target", {{"id", c.target.id}}}, {"clear", c.clear}, {"clear_color", colorF(c.clearColor)}};
         },
         [](EntityId e, const json& j) { put(e, cameraOf(j)); },
         [](EntityId e) { world().removeComponent(e, cid<Camera>()); },
         [] { return cid<Camera>(); }},
        {"CameraFollow",
         [](EntityId e) { return get<CameraFollow>(e) != nullptr; },
         [](EntityId e) {
             const CameraFollow& f = *get<CameraFollow>(e);
             return json{{"target", entityJ(f.target)}, {"smoothing", f.smoothing}, {"offset", vec(f.offset)},
                         {"dead_zone", vec(f.deadZone)}};
         },
         [](EntityId e, const json& j) {
             CameraFollow f;
             f.target = entityOf(j.value("target", json()));
             f.smoothing = j.value("smoothing", 5.f);
             f.offset = vec(j.value("offset", json()));
             f.deadZone = vec(j.value("dead_zone", json()));
             put(e, f);
         },
         [](EntityId e) { world().removeComponent(e, cid<CameraFollow>()); },
         [] { return cid<CameraFollow>(); }},
        {"CameraBounds",
         [](EntityId e) { return get<CameraBounds>(e) != nullptr; },
         [](EntityId e) { return json{{"area", rectJ(get<CameraBounds>(e)->area)}}; },
         [](EntityId e, const json& j) { put(e, CameraBounds{rectJ(j.value("area", json()))}); },
         [](EntityId e) { world().removeComponent(e, cid<CameraBounds>()); },
         [] { return cid<CameraBounds>(); }},
    };
}

// The style of a candela Text value, for drawing and measuring.
TextStyle textStyle(const json& j) {
    TextStyle t;
    t.size = j.value("size", 24.f);
    t.color = color(j.value("color", json()));
    t.align = pick<TextAlign>(j.value("align", json()), {"Left", "Center", "Right"});
    t.valign = pick<TextVAlign>(j.value("valign", json()), {"Top", "Middle", "Bottom"});
    t.wrapWidth = j.value("wrap", 0.f);
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
            Vec2 anchor = vec(s.value("anchor", json()), {0.5f, 0.5f});
            d.origin = {anchor.x * size.x, anchor.y * size.y};
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

json lightJ(ColorF color, float intensity, float radius, float falloff, bool shadows, uint32_t mask) {
    return {{"color", colorF(color)}, {"intensity", intensity}, {"radius", radius}, {"falloff", falloff},
            {"shadows", shadows}, {"layer_mask", mask}};
}

std::vector<Codec> effectCodecs() {
    return {
        {"Trail",
         [](EntityId e) { return get<TrailRenderer>(e) != nullptr; },
         [](EntityId e) {
             const TrailRenderer& t = *get<TrailRenderer>(e);
             return json{{"width", t.width}, {"lifetime", t.lifetime}, {"color_start", colorF(t.colorStart)},
                         {"color_end", colorF(t.colorEnd)}, {"min_distance", t.minDistance},
                         {"max_points", t.maxPoints}, {"z", t.zOrder}};
         },
         [](EntityId e, const json& j) {
             TrailRenderer t;
             t.width = j.value("width", 4.f);
             t.lifetime = j.value("lifetime", 0.5f);
             t.colorStart = colorF(j.value("color_start", json()), {1, 1, 1, 1});
             t.colorEnd = colorF(j.value("color_end", json()), {1, 1, 1, 0});
             t.minDistance = j.value("min_distance", 2.f);
             t.maxPoints = std::max(2, j.value("max_points", 64));
             t.zOrder = j.value("z", -1.f);
             put(e, t);
         },
         [](EntityId e) { world().removeComponent(e, cid<TrailRenderer>()); },
         [] { return cid<TrailRenderer>(); }},
        {"PointLight",
         [](EntityId e) { return get<PointLight2D>(e) != nullptr; },
         [](EntityId e) {
             const PointLight2D& l = *get<PointLight2D>(e);
             return lightJ(l.color, l.intensity, l.radius, l.falloff, l.castShadows, l.layerMask);
         },
         [](EntityId e, const json& j) {
             PointLight2D l;
             l.color = colorF(j.value("color", json()), {1, 1, 1, 1});
             l.intensity = j.value("intensity", 1.f);
             l.radius = j.value("radius", 200.f);
             l.falloff = j.value("falloff", 2.f);
             l.castShadows = j.value("shadows", true);
             l.layerMask = maskOf(j);
             put(e, l);
         },
         [](EntityId e) { world().removeComponent(e, cid<PointLight2D>()); },
         [] { return cid<PointLight2D>(); }},
        {"SpotLight",
         [](EntityId e) { return get<SpotLight2D>(e) != nullptr; },
         [](EntityId e) {
             const SpotLight2D& l = *get<SpotLight2D>(e);
             json j = lightJ(l.color, l.intensity, l.radius, l.falloff, l.castShadows, l.layerMask);
             j["direction"] = l.direction;
             j["angle"] = l.angle;
             j["softness"] = l.softness;
             return j;
         },
         [](EntityId e, const json& j) {
             SpotLight2D l;
             l.color = colorF(j.value("color", json()), {1, 1, 1, 1});
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
         [](EntityId e) { world().removeComponent(e, cid<SpotLight2D>()); },
         [] { return cid<SpotLight2D>(); }},
        {"LightOccluder",
         [](EntityId e) { return get<LightOccluder2D>(e) != nullptr; },
         [](EntityId e) {
             const LightOccluder2D& o = *get<LightOccluder2D>(e);
             json points = json::array();
             for (int i = 0; i < o.pointCount; ++i) points.push_back(vec(o.points[i]));
             return json{{"shape", name(o.shape, {"Box", "Circle", "Polygon", "FromCollider"})},
                         {"half_size", vec(o.halfSize)}, {"radius", o.radius}, {"points", points},
                         {"offset", vec(o.offset)}, {"shadow_self", o.shadowSelf}, {"layer_mask", o.layerMask}};
         },
         [](EntityId e, const json& j) {
             LightOccluder2D o;
             o.shape = pick<OccluderShape>(j.value("shape", json()), {"Box", "Circle", "Polygon", "FromCollider"});
             o.halfSize = vec(j.value("half_size", json()), {16.f, 16.f});
             o.radius = j.value("radius", 16.f);
             for (const auto& p : j.value("points", json::array())) {
                 if (o.pointCount >= LightOccluder2D::kMaxPoints) break;
                 o.points[o.pointCount++] = vec(p);
             }
             o.offset = vec(j.value("offset", json()));
             o.shadowSelf = j.value("shadow_self", false);
             o.layerMask = maskOf(j);
             put(e, o);
         },
         [](EntityId e) { world().removeComponent(e, cid<LightOccluder2D>()); },
         [] { return cid<LightOccluder2D>(); }},
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
std::unordered_map<uint32_t, json>& sheetValues() {
    static std::unordered_map<uint32_t, json> sheets;
    return sheets;
}
json sheetJ(SpritesheetHandle sheet) {
    auto it = sheetValues().find(sheet.id);
    return it != sheetValues().end() ? it->second : json{{"id", sheet.id}};
}

json clipJ(const AnimationClip& c) {
    return {{"sheet", sheetJ(c.sheet)}, {"frames", c.frames}, {"seconds", c.seconds}, {"looping", c.looping}};
}
AnimationClip clipOf(const json& j) {
    AnimationClip c;
    c.sheet = SpritesheetHandle{idOf(j.value("sheet", json()))};
    for (const auto& f : j.value("frames", json::array())) c.frames.push_back(f.get<int32_t>());
    for (const auto& s : j.value("seconds", json::array())) c.seconds.push_back(s.get<float>());
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
        {"Animator",
         [](EntityId e) { return get<SpriteAnimator>(e) != nullptr; },
         [](EntityId e) {
             const SpriteAnimator& a = *get<SpriteAnimator>(e);
             json clips = json::object();
             for (const auto& c : a.clips) clips[c.name] = clipJ(c.clip);
             return json{{"clips", clips}, {"play", a.current}, {"then", a.then}, {"once", a.once},
                         {"speed", a.speed}, {"playing", a.playing}, {"frame", a.frame}, {"time", a.time()},
                         {"ended", a.ended}};
         },
         [](EntityId e, const json& j) {
             // The engine keeps the clip's place unless the script switched
             // clips (then it starts from the first frame), moved `frame`, or
             // moved `time`.
             SpriteAnimator a;
             json clips = j.value("clips", json::object());
             for (const auto& [name, c] : clips.items()) a.clips.push_back({name, clipOf(c)});
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
         [](EntityId e) { world().removeComponent(e, cid<SpriteAnimator>()); },
         [] { return cid<SpriteAnimator>(); }},
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
        sheetValues()[sheet.id] = {{"id", sheet.id}, {"texture", {{"id", tex}}}, {"frame_w", w}, {"frame_h", h},
                                   {"columns", columns}, {"rows", rows}};
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
        sheetValues()[sheet.id] = {{"id", sheet.id}, {"texture", {{"id", tex}}}, {"frame_w", first.w},
                                   {"frame_h", first.h}, {"columns", count}, {"rows", 1}};
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

json service(const std::string& f, const json& a) {
    json answer;
    if (procgen(f, a, answer)) return answer;
    if (presentation(f, a, answer)) return answer;
    if (images(f, a, answer)) return answer;
    if (hierarchyService(f, a, answer)) return answer;
    if (gameplay(f, a, answer)) return answer;
    if (effects(f, a, answer)) return answer;
    if (animService(f, a, answer)) return answer;
    if (windowAndAssets(f, a, answer)) return answer;
    if (platform(f, a, answer)) return answer;
    if (animation(f, a, answer)) return answer;
    if (candela::dataService(*g_app, f, a, answer) || candela::netService(f, a, answer)) return answer;
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
    return keep(c->read(e), len);
}

int32_t drift_bridge_set(uint64_t e, const char* component, const uint8_t* data, size_t len) {
    const Codec* c = codec(component);
    if (!c || !world().isAlive(e)) return 0;
    json j = unpack(data, len);
    if (j.is_discarded()) return 0;
    if (j.is_null()) j = json::object();
    c->write(e, j);
    return 1;
}

void drift_bridge_remove(uint64_t e, const char* component) {
    const Codec* c = codec(component);
    if (c && world().isAlive(e) && c->has(e)) c->remove(e);
}

const uint8_t* drift_bridge_list(const char* component, size_t* len) {
    const Codec* c = codec(component);
    json out = json::array();
    if (c) {
        std::vector<EntityId> ids = world().entitiesWith(c->id());
        std::sort(ids.begin(), ids.end());
        for (EntityId e : ids) {
            if (c->has(e)) out.push_back(e);
        }
    }
    return keep(out, len);
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

    std::string entry_;
    std::string moduleDir_;
    bool loaded_ = false;
    bool failed_ = false;
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
#ifdef __EMSCRIPTEN__
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
    "drift: runs Candela games and builds them for the web.\n"
    "\n"
    "Usage:\n"
    "  drift run [folder | script] [--config <path>] [--set section.key=value]...\n"
    "  drift build --web [folder] [-o <out folder>]\n"
    "  drift --version\n"
    "  drift --help\n"
    "\n"
    "run plays the game in a folder (default: this one) or starts at a script.\n"
    "build --web writes a page to serve over HTTP (default: <folder>/build/web).\n";

#ifndef __EMSCRIPTEN__
// The web runtime `drift build --web` copies: DRIFT_WEB_RUNTIME, or the
// installed copy.
std::string webRuntimeDir() {
    if (const char* env = SDL_getenv("DRIFT_WEB_RUNTIME")) return env;
    return installed("web");
}

// drift build --web [folder] [-o <out>]
int buildCommand(int argc, char** argv, int first) {
    bool web = false;
    std::string folder, out;
    for (int i = first; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--web") web = true;
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
    if (!drift::web::buildWebPage(folder, out, runtime, error, summary)) {
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
        if (arg == "run" || arg == "build") {
            command = arg;
            first = 2;
        }
    }
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

    app.addPlugins<DefaultPlugins>();
    app.addPlugin<SavePlugin>();   // the slot named after [project]
    if (app.settingsStore().has("lighting")) app.addPlugin<LightingPlugin>();
    app.addPlugin(new CandelaPlugin(entry, moduleDir()));
    int code = app.run();
    drift_host_unload();
    g_app = nullptr;
    return g_loadFailed ? 1 : code;
}
