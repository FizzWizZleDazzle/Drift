// drift: runs Candela games.
//
//     drift run [script] [--config <path>] [--set section.key=value]...
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
#include <drift/components/CameraController.hpp>
#include <drift/components/Name.hpp>
#include <drift/components/Physics.hpp>
#include <drift/components/Shape.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Text.hpp>
#include <drift/effects/PostEffects.hpp>
#include <drift/gameplay/InputActions.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/plugins/LightingPlugin.hpp>
#include <drift/resources/AudioResource.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/PhysicsResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
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
std::string g_text;          // answers handed to the host stay alive until the next call
EffectId g_fade;

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

const char* textAligns[] = {"Left", "Center", "Right"};
const char* textVAligns[] = {"Top", "Middle", "Bottom"};

const std::vector<Codec>& codecs() {
    static const std::vector<Codec> list = {
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
                         {"blend", blendNames[int(s.blend)]}};
         },
         [](EntityId e, const json& j) {
             Sprite s;
             if (const Sprite* cur = get<Sprite>(e)) s.material = cur->material;
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
    };
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
    if (!c || std::string(c->name) == "Transform" || std::string(c->name) == "Name") return "[]";
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

std::string moduleDir() {
    if (const char* env = SDL_getenv("DRIFT_CANDELA_MODULE")) return env;
#ifdef __EMSCRIPTEN__
    return "/drift-module";
#else
    return DRIFT_CANDELA_MODULE_DIR;
#endif
}

} // namespace

int main(int argc, char** argv) {
    // drift run [script]: the first argument after `run` that is not an option.
    std::string entry;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "run") continue;
        if (arg == "--config" || arg == "--set") { ++i; continue; }
        if (arg.rfind("--", 0) == 0) continue;
        entry = arg;
        break;
    }

    App app;
    g_app = &app;
    app.args(argc, argv);
    if (entry.empty()) entry = app.config().project.entry;
    if (entry.empty()) entry = "main.cdl";

    app.addPlugins<DefaultPlugins>();
    if (app.settingsStore().has("lighting")) app.addPlugin<LightingPlugin>();
    app.addPlugin(new CandelaPlugin(entry, moduleDir()));
    int code = app.run();
    drift_host_unload();
    g_app = nullptr;
    return code;
}
