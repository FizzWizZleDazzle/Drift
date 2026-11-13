// =============================================================================
// Lighting
// =============================================================================
// A dark stone floor lit by moving colored lights. Walls, pillars and a
// crate block the light and cast hard shadows; one wall takes its shape
// from its physics collider. Every post effect in the library can be
// toggled from the keyboard. All textures are generated in code.
//
// Controls: mouse moves the white light, 1-8 toggle post effects,
//           F flashes the screen, G fades to black and back,
//           L toggles lighting, F12 saves a screenshot.
// =============================================================================

#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/Query.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/Light2D.hpp>
#include <drift/components/Physics.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/effects/PostEffects.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/plugins/LightingPlugin.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>
#include <drift/resources/UIResource.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace drift;

// Moves an entity along a flattened circle around `center`.
struct EllipseOrbit {
    Vec2 center;
    float radius = 100.f;
    float speed = 1.f;
    float phase = 0.f;
};

struct Sweep { float base = 0.f; float range = 0.6f; float speed = 0.8f; };
struct Spin { float speed = 0.5f; };
struct MouseLight {};

struct Demo : Resource {
    DRIFT_RESOURCE(Demo)
    struct Toggle { Key key; const char* name; EffectId id; };
    std::vector<Toggle> toggles;
    EffectId fade;
    bool faded = false;
};

// ---- Procedural textures ----------------------------------------------------

static uint32_t hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
    return x;
}

// 64x64 stone tiles with dark mortar lines; tiles with Repeat wrap.
static TextureHandle makeFloor(RendererResource& r) {
    const int n = 64;
    std::vector<uint8_t> px(n * n * 4);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            bool mortar = (x % 32) < 2 || (y % 32) < 2;
            float noise = (hash(static_cast<uint32_t>(y * n + x)) & 255) / 255.f;
            float v = mortar ? 0.25f : 0.62f + noise * 0.12f;
            uint8_t* p = &px[(y * n + x) * 4];
            p[0] = static_cast<uint8_t>(v * 200); p[1] = static_cast<uint8_t>(v * 205);
            p[2] = static_cast<uint8_t>(v * 215); p[3] = 255;
        }
    return r.createTextureFromPixels(px.data(), n, n, {TextureFilter::Nearest, TextureWrap::Repeat});
}

// 32x32 brick block for walls.
static TextureHandle makeBrick(RendererResource& r) {
    const int n = 32;
    std::vector<uint8_t> px(n * n * 4);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            int row = y / 8;
            int bx = (x + (row % 2) * 8) % 16;
            bool mortar = (y % 8) == 0 || bx == 0;
            float noise = (hash(static_cast<uint32_t>(y * 97 + x * 13)) & 63) / 255.f;
            uint8_t* p = &px[(y * n + x) * 4];
            p[0] = mortar ? 70 : static_cast<uint8_t>(150 + noise * 200);
            p[1] = mortar ? 64 : static_cast<uint8_t>(80 + noise * 100);
            p[2] = mortar ? 60 : static_cast<uint8_t>(60 + noise * 80);
            p[3] = 255;
        }
    return r.createTextureFromPixels(px.data(), n, n);
}

// Filled disc with a soft rim; `glow` makes a bright falloff for light bulbs.
static TextureHandle makeDisc(RendererResource& r, int n, bool glow) {
    std::vector<uint8_t> px(n * n * 4);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float dx = (x + 0.5f) / n * 2.f - 1.f, dy = (y + 0.5f) / n * 2.f - 1.f;
            float d = std::sqrt(dx * dx + dy * dy);
            uint8_t* p = &px[(y * n + x) * 4];
            if (glow) {
                float f = std::max(0.f, 1.f - d);
                p[0] = p[1] = p[2] = 255;
                p[3] = static_cast<uint8_t>(std::min(1.f, f * f * 2.f) * 255.f);
            } else {
                float shade = 0.55f + 0.45f * (1.f - d);
                p[0] = static_cast<uint8_t>(120 * shade); p[1] = static_cast<uint8_t>(130 * shade);
                p[2] = static_cast<uint8_t>(150 * shade); p[3] = d < 1.f ? 255 : 0;
            }
        }
    return r.createTextureFromPixels(px.data(), n, n, {TextureFilter::Linear});
}

// ---- Scene ------------------------------------------------------------------

static void spawnWall(Commands& cmd, TextureHandle brick, Vec2 pos, Vec2 half, float rot = 0.f) {
    Sprite s{.texture = brick, .origin = {16.f, 16.f}};
    cmd.spawn().insert(Transform2D{pos, rot, {half.x / 16.f, half.y / 16.f}}, s,
                       LightOccluder2D::box({16.f, 16.f}));
}

static void setup(ResMut<RendererResource> r, ResMut<PostEffects> fx, ResMut<LightingSettings> lighting,
                  ResMut<Demo> demo, Commands& cmd) {
    r->setClearColor({0.f, 0.f, 0.f, 1.f});
    lighting->ambient = {0.07f, 0.07f, 0.12f, 1.f};

    TextureHandle floor = makeFloor(*r);
    TextureHandle brick = makeBrick(*r);
    TextureHandle pillar = makeDisc(*r, 64, false);
    TextureHandle bulb = makeDisc(*r, 32, true);

    cmd.spawn().insert(Transform2D{}, Camera{.active = true});

    // Floor: one sprite, the texture repeats across it.
    cmd.spawn().insert(Transform2D{{-1200.f, -800.f}},
                       Sprite{.texture = floor, .srcRect = {0.f, 0.f, 2400.f, 1600.f}, .zOrder = -10.f});

    // Walls and pillars.
    spawnWall(cmd, brick, {-380.f, -150.f}, {120.f, 16.f});
    spawnWall(cmd, brick, {-260.f, -60.f}, {16.f, 90.f});
    spawnWall(cmd, brick, {360.f, 180.f}, {16.f, 110.f});
    spawnWall(cmd, brick, {120.f, -210.f}, {90.f, 16.f}, 0.4f);
    for (Vec2 p : {Vec2{-140.f, 170.f}, Vec2{200.f, 20.f}, Vec2{-450.f, 160.f}, Vec2{480.f, -150.f}}) {
        cmd.spawn().insert(Transform2D{p, 0.f, {0.6f, 0.6f}}, Sprite{.texture = pillar, .origin = {32.f, 32.f}},
                           LightOccluder2D::circle(32.f));
    }

    // A rotating triangular crate (polygon occluder).
    const Vec2 tri[3] = {{0.f, -40.f}, {36.f, 24.f}, {-36.f, 24.f}};
    {
        LightOccluder2D occ = LightOccluder2D::polygon(tri, 3);
        cmd.spawn().insert(Transform2D{{-30.f, -40.f}}, occ, Spin{0.6f});
    }

    // A wall whose shadow shape comes from its physics collider.
    cmd.spawn().insert(Transform2D{{100.f, 250.f}, 0.f, {5.f, 1.f}},
                       Sprite{.texture = brick, .origin = {16.f, 16.f}},
                       RigidBody2D{.type = BodyType::Static},
                       BoxCollider2D{.halfSize = {80.f, 16.f}},
                       LightOccluder2D::fromCollider());

    // Orbiting colored lights, each with a bright bulb sprite.
    struct L { ColorF color; float radius, orbit, speed, phase; };
    const L lights[] = {
        {{1.f, 0.55f, 0.2f}, 340.f, 230.f, 0.5f, 0.f},
        {{0.2f, 0.9f, 0.8f}, 300.f, 330.f, -0.35f, 2.f},
        {{0.7f, 0.35f, 1.f}, 280.f, 140.f, 0.8f, 4.f},
    };
    for (const L& l : lights) {
        cmd.spawn().insert(Transform2D{},
                           PointLight2D{.color = l.color, .intensity = 1.4f, .radius = l.radius},
                           EllipseOrbit{{0.f, 0.f}, l.orbit, l.speed, l.phase},
                           Sprite{.texture = bulb, .origin = {16.f, 16.f},
                                  .tint = Color(255, 255, 255), .zOrder = 5.f, .blend = BlendMode::Additive});
    }

    // A sweeping spot light from the top edge.
    cmd.spawn().insert(Transform2D{{0.f, -330.f}},
                       SpotLight2D{.color = {1.f, 0.95f, 0.7f}, .intensity = 1.6f, .radius = 650.f,
                                   .direction = 1.5708f, .angle = 0.7f, .softness = 0.35f},
                       Sweep{0.f, 0.7f, 0.6f},
                       Sprite{.texture = bulb, .origin = {16.f, 16.f}, .zOrder = 5.f,
                              .blend = BlendMode::Additive});

    // White light under the mouse, no shadows.
    cmd.spawn().insert(Transform2D{{300.f, -60.f}},
                       PointLight2D{.color = {1.f, 1.f, 0.95f}, .intensity = 0.9f, .radius = 180.f,
                                    .castShadows = false},
                       MouseLight{},
                       Sprite{.texture = bulb, .origin = {16.f, 16.f}, .zOrder = 5.f,
                              .blend = BlendMode::Additive});

    // Every post effect, added disabled; number keys toggle them.
    TextureHandle warmLut = PostEffects::createColorLut(*r, 16, [](ColorF c) {
        return ColorF{std::pow(c.r, 0.8f), c.g, std::pow(c.b, 1.3f) * 0.85f, 1.f};
    });
    auto disabled = [&](EffectId id) { fx->setEnabled(id, false); return id; };
    demo->toggles = {
        {Key::Num1, "Bloom", disabled(fx->add(Bloom{.threshold = 0.5f, .intensity = 2.f}))},
        {Key::Num2, "Vignette", disabled(fx->add(Vignette{}))},
        {Key::Num3, "CRT", disabled(fx->add(Crt{}))},
        {Key::Num4, "Color grading", disabled(fx->add(ColorGrading{.contrast = 1.15f, .saturation = 1.3f,
                                                                   .lut = warmLut}))},
        {Key::Num5, "Pixelate", disabled(fx->add(Pixelate{}))},
        {Key::Num6, "Grayscale", disabled(fx->add(Grayscale{}))},
        {Key::Num7, "Sepia", disabled(fx->add(Sepia{}))},
        {Key::Num8, "Blur", disabled(fx->add(Blur{}))},
    };
    demo->fade = fx->add(ScreenFade{});
}

static void animate(Res<Time> time, Res<InputResource> input, Res<RendererResource> r,
                    QueryMut<Transform2D, EllipseOrbit> orbits, QueryMut<Transform2D, Sweep> sweeps,
                    QueryMut<Transform2D, Spin> spins, QueryMut<Transform2D, MouseLight> mouse) {
    float t = static_cast<float>(time->elapsed);
    orbits.iter([&](Transform2D& tr, EllipseOrbit& o) {
        float a = o.phase + t * o.speed;
        tr.position = o.center + Vec2{std::cos(a), std::sin(a) * 0.7f} * o.radius;
    });
    sweeps.iter([&](Transform2D& tr, Sweep& s) { tr.rotation = s.base + std::sin(t * s.speed) * s.range; });
    spins.iter([&](Transform2D& tr, Spin& s) { tr.rotation += s.speed * time->delta; });
    Vec2 m = r->screenToWorld(input->mousePosition());
    mouse.iter([&](Transform2D& tr, MouseLight&) { tr.position = m; });
}

// The spinning crate has no sprite; draw its occluder polygon.
static void drawCrates(ResMut<RendererResource> r, Query<Transform2D, LightOccluder2D, Spin> crates) {
    crates.iter([&](const Transform2D& t, const LightOccluder2D& o, const Spin&) {
        float c = std::cos(t.rotation), s = std::sin(t.rotation);
        Vec2 pts[LightOccluder2D::kMaxPoints];
        for (int i = 0; i < o.pointCount; ++i) {
            Vec2 p = o.points[i];
            pts[i] = t.position + Vec2{p.x * c - p.y * s, p.x * s + p.y * c};
        }
        r->drawPolygonFilled(pts, o.pointCount, Color(150, 110, 70));
        r->drawPolyline(pts, o.pointCount, Color(90, 60, 35), 4.f, true, 0.1f);
    });
}

static void controls(Res<InputResource> input, ResMut<PostEffects> fx, ResMut<LightingSettings> lighting,
                     ResMut<Demo> demo, ResMut<RendererResource> r) {
    for (auto& t : demo->toggles) {
        if (input->keyPressed(t.key)) fx->setEnabled(t.id, !fx->enabled(t.id));
    }
    if (input->keyPressed(Key::F)) fx->flash(demo->fade, {1.f, 1.f, 1.f, 1.f}, 0.4f);
    if (input->keyPressed(Key::G)) {
        demo->faded = !demo->faded;
        fx->get<ScreenFade>(demo->fade)->color = {0.f, 0.f, 0.f, 1.f};
        fx->fadeTo(demo->fade, demo->faded ? 1.f : 0.f, 0.8f);
    }
    if (input->keyPressed(Key::L)) lighting->enabled = !lighting->enabled;
    if (input->keyPressed(Key::F12)) r->captureScreenshot("lighting.png");
}

static void hud(ResMut<UIResource> ui, Res<PostEffects> fx, Res<Demo> demo, Res<LightingSettings> lighting) {
    std::string text = "Mouse: white light   F: flash   G: fade   F12: screenshot\n";
    text += std::string("L  [") + (lighting->enabled ? "x" : " ") + "] Lighting\n";
    for (const auto& t : demo->toggles) {
        char line[64];
        std::snprintf(line, sizeof(line), "%d  [%s] %s\n", static_cast<int>(t.key) - static_cast<int>(Key::Num1) + 1,
                      fx->enabled(t.id) ? "x" : " ", t.name);
        text += line;
    }
    ui->label(text.c_str(), {16.f, 12.f});
}

int main(int argc, char** argv) {
    App app;
    app.args(argc, argv);   // window and project settings: drift.toml
    app.addPlugins<DefaultPlugins>();
    app.addPlugin<LightingPlugin>();
    app.addResource<Demo>();
    app.startup<setup>();
    app.update<animate>("animate");
    app.update<controls>("controls");
    app.update<hud>("hud");
    app.render<drawCrates>("draw_crates");
    return app.run();
}
