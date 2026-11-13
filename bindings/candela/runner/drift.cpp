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
#include <drift/Log.hpp>
#include <drift/PhysicsSyncSystem.hpp>
#include <drift/components/Behavior.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/Light2D.hpp>
#include <drift/components/SpriteAnimator.hpp>
#include <drift/components/TrailRenderer.hpp>
#include <drift/resources/FontResource.hpp>
#include <drift/resources/ParticleSystemResource.hpp>
#include <drift/resources/SpriteResource.hpp>
#include <drift/resources/UIResource.hpp>
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

#ifndef __EMSCRIPTEN__
#include "web_pack.hpp"
#endif

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <span>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

using namespace drift;
using nlohmann::json;

extern "C" {
int32_t drift_host_load(const char* entry, const char* module_dir);
const char* drift_host_error();
void drift_host_contact(int32_t kind, uint64_t a, uint64_t b);
void drift_host_frame(double dt, double elapsed, int64_t frame);
void drift_host_unload();
}

namespace {

App* g_app = nullptr;
bool g_loadFailed = false;   // the game did not compile; drift exits with 1
std::string g_text;          // answers handed to the host stay alive until the next call
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

const char* keep(std::string s) {
    g_text = std::move(s);
    return g_text.c_str();
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

// Starts the tween a candela Tween value describes; the same value again
// leaves the running one alone.
void startTween(EntityId e, const json& j) {
    auto it = g_tweens.find(e);
    if (it != g_tweens.end() && it->second.value == j) return;
    stopTween(e);
    auto* tw = res<Tweens>();
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

const char* textAligns[] = {"Left", "Center", "Right"};
const char* textVAligns[] = {"Top", "Middle", "Bottom"};

std::vector<Codec> presentationCodecs();

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
                         {"density", c.density}, {"friction", c.friction}, {"restitution", c.restitution}};
         },
         [](EntityId e, const json& j) {
             BoxCollider2D c;
             c.halfSize = vec(j.value("half_size", json()), {16, 16});
             c.offset = vec(j.value("offset", json()));
             c.isSensor = j.value("sensor", false);
             c.density = j.value("density", 1.f);
             c.friction = j.value("friction", 0.3f);
             c.restitution = j.value("restitution", 0.f);
             put(e, c);
         },
         [](EntityId e) { world().removeComponent(e, cid<BoxCollider2D>()); },
         [] { return cid<BoxCollider2D>(); }},
        {"CircleCollider",
         [](EntityId e) { return get<CircleCollider2D>(e) != nullptr; },
         [](EntityId e) {
             const CircleCollider2D& c = *get<CircleCollider2D>(e);
             return json{{"radius", c.radius}, {"offset", vec(c.offset)}, {"sensor", c.isSensor},
                         {"density", c.density}, {"friction", c.friction}, {"restitution", c.restitution}};
         },
         [](EntityId e, const json& j) {
             CircleCollider2D c;
             c.radius = j.value("radius", 16.f);
             c.offset = vec(j.value("offset", json()));
             c.isSensor = j.value("sensor", false);
             c.density = j.value("density", 1.f);
             c.friction = j.value("friction", 0.3f);
             c.restitution = j.value("restitution", 0.f);
             put(e, c);
         },
         [](EntityId e) { world().removeComponent(e, cid<CircleCollider2D>()); },
         [] { return cid<CircleCollider2D>(); }},
        {"Lifetime",
         [](EntityId e) { return get<Lifetime>(e) != nullptr; },
         [](EntityId e) { return json{{"seconds", get<Lifetime>(e)->seconds}}; },
         [](EntityId e, const json& j) { put(e, Lifetime{j.value("seconds", 1.f)}); },
         [](EntityId e) { world().removeComponent(e, cid<Lifetime>()); },
         [] { return cid<Lifetime>(); }},
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
    };
    for (Codec& c : presentationCodecs()) l.push_back(c);
    return l;
    }();
    return list;
}

// The drift module names a struct `Sprite`, or `drift::Sprite` when a game
// declares its own of that name.
const Codec* codec(const char* component) {
    std::string n = component ? component : "";
    if (n.rfind("drift::", 0) == 0) n = n.substr(7);
    for (const auto& c : codecs()) {
        if (n == c.name) return &c;
    }
    return nullptr;
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

// ---- Services ----

int state(bool held, bool pressed, bool released, int which) {
    return which == 1 ? pressed : which == 2 ? released : held;
}

EffectId fade() {
    auto* fx = res<PostEffects>();
    if (!fx) return {};
    if (!g_fade.valid()) g_fade = fx->add(ScreenFade{});
    return g_fade;
}

json service(const std::string& f, const json& a) {
    json answer;
    if (procgen(f, a, answer)) return answer;
    if (presentation(f, a, answer)) return answer;
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
        if (in) {
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
    if (f == "action_state") {
        std::string n = a[0].get<std::string>();
        if (!actions) return false;
        int w = a[1].get<int>();
        return w == 1 ? actions->pressed(n) : w == 2 ? actions->released(n) : actions->held(n);
    }
    if (f == "action_value") return actions ? actions->value(a[0].get<std::string>()) : 0.0;
    if (f == "action_axis2d") {
        Vec2 v = actions ? actions->axis2D(a[0].get<std::string>()) : Vec2{};
        return {v.x, v.y};
    }
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
    if (f == "sound_play") {
        if (auto* audio = res<AudioResource>()) {
            PlayParams p;
            p.volume = a[1].get<float>();
            p.pitch = a[2].get<float>();
            p.loop = a[3].get<bool>();
            audio->playSound(SoundHandle{a[0].get<uint32_t>()}, p);
        }
        return nullptr;
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
        auto* save = res<SaveData>();
        if (!save) return f == "save_flush" ? json(false) : json(nullptr);
        if (f == "save_get") {
            std::string k = a[0].get<std::string>();
            if (a[1].is_number_integer()) return save->get<int64_t>(k, a[1].get<int64_t>());
            if (a[1].is_number()) return save->get<double>(k, a[1].get<double>());
            return save->get<std::string>(k, a[1].get<std::string>());
        }
        if (f == "save_set") {
            std::string k = a[0].get<std::string>();
            if (a[1].is_number_integer()) save->set(k, a[1].get<int64_t>());
            else if (a[1].is_number()) save->set(k, a[1].get<double>());
            else save->set(k, a[1].get<std::string>());
            return nullptr;
        }
        if (f == "save_has") return save->has(a[0].get<std::string>());
        if (f == "save_erase") { save->erase(a[0].get<std::string>()); return nullptr; }
        if (f == "save_flush") return save->flush();
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
    stopTween(e);
    if (world().isAlive(e)) world().destroyEntity(e);
}

int32_t drift_bridge_alive(uint64_t e) { return world().isAlive(e) ? 1 : 0; }

int32_t drift_bridge_is_engine(const char* component) { return codec(component) ? 1 : 0; }

int32_t drift_bridge_has(uint64_t e, const char* component) {
    const Codec* c = codec(component);
    return c && world().isAlive(e) && c->has(e) ? 1 : 0;
}

const char* drift_bridge_get(uint64_t e, const char* component) {
    const Codec* c = codec(component);
    if (!c || !world().isAlive(e) || !c->has(e)) return nullptr;
    return keep(c->read(e).dump());
}

int32_t drift_bridge_set(uint64_t e, const char* component, const char* text) {
    const Codec* c = codec(component);
    if (!c || !world().isAlive(e)) return 0;
    json j = json::parse(text ? text : "{}", nullptr, false);
    if (j.is_discarded()) return 0;
    c->write(e, j);
    return 1;
}

void drift_bridge_remove(uint64_t e, const char* component) {
    const Codec* c = codec(component);
    if (c && world().isAlive(e) && c->has(e)) c->remove(e);
}

const char* drift_bridge_list(const char* component) {
    const Codec* c = codec(component);
    json out = json::array();
    if (c) {
        std::vector<EntityId> ids = world().entitiesWith(c->id());
        std::sort(ids.begin(), ids.end());
        for (EntityId e : ids) {
            if (c->has(e)) out.push_back(e);
        }
    }
    return keep(out.dump());
}

// Engine components that need another to work: everything drawn or
// simulated needs a Transform.
const char* drift_bridge_requires(const char* component) {
    const Codec* c = codec(component);
    if (!c || std::string(c->name) == "Transform" || std::string(c->name) == "Name" ||
        std::string(c->name) == "Tween") return "[]";
    return "[\"Transform\"]";
}

const char* drift_bridge_call(const char* function, const char* args) {
    json a = json::parse(args ? args : "[]", nullptr, false);
    if (a.is_discarded()) a = json::array();
    return keep(service(function ? function : "", a).dump());
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
                failed_ = true;
                g_loadFailed = true;
                a.quit();
                return;
            }
            loaded_ = true;
        }
        if (auto* cb = a.getResource<CollisionBridge>()) {
            for (int i = 0; i < cb->collisionStartCount(); ++i) drift_host_contact(0, cb->collisionStartA(i), cb->collisionStartB(i));
            for (int i = 0; i < cb->collisionEndCount(); ++i) drift_host_contact(1, cb->collisionEndA(i), cb->collisionEndB(i));
            for (int i = 0; i < cb->sensorStartCount(); ++i) drift_host_contact(2, cb->sensorStartA(i), cb->sensorStartB(i));
            for (int i = 0; i < cb->sensorEndCount(); ++i) drift_host_contact(3, cb->sensorEndA(i), cb->sensorEndB(i));
        }
        auto* time = a.getResource<Time>();
        drift_host_frame(time ? time->delta : 0.0, time ? time->elapsed : 0.0, time ? int64_t(time->frame) : 0);
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
