// =============================================================================
// Render showcase
// =============================================================================
// Shapes, nine-slice panels, a minimap camera rendering into a texture, a
// custom post effect, and a HUD on the screen layer. Everything is generated
// in code, so the example needs no asset files.
//
// Controls: arrow keys move the camera, Q/E zoom, 1-4 switch scale mode,
//           P toggles the post effect, L toggles the light map,
//           F12 saves a screenshot.
// =============================================================================

#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/Query.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/FontResource.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace drift;

// Vignette + scanlines. The same effect in WGSL (WebGPU) and GLSL (OpenGL),
// so it runs on every backend without a build step.
static const char* kPostWGSL = R"(
fn fragment(in: FragmentInput) -> vec4<f32> {
    let c = textureSample(texture0, sampler0, in.uv);
    let d = distance(in.uv, vec2<f32>(0.5, 0.5));
    let vignette = smoothstep(0.85, 0.35, d);
    let scan = 0.92 + 0.08 * sin(in.uv.y * frame.time.w * 3.14159);
    return vec4<f32>(c.rgb * vignette * mix(1.0, scan, material.params[0].x), 1.0);
}
)";

static const char* kPostGLSL = R"(
void main() {
    vec4 c = texture(u_texture0, v_uv);
    float d = distance(v_uv, vec2(0.5));
    float vignette = smoothstep(0.85, 0.35, d);
    float scan = 0.92 + 0.08 * sin(v_uv.y * frame.time.w * 3.14159);
    o_color = vec4(c.rgb * vignette * mix(1.0, scan, material.params[0].x), 1.0);
}
)";

struct Showcase : Resource {
    DRIFT_RESOURCE(Showcase)
    TextureHandle checker;
    TextureHandle panel;
    TextureHandle minimap;
    FontHandle font;
    MaterialHandle post;
    MaterialHandle wobble;
    int32_t postId = 0;
    EntityId mainCamera = InvalidEntityId;
    TextureHandle glow;
    TextureHandle lightMap;
    int32_t lightHook = 0;
    float time = 0.f;
};

static TextureHandle makeChecker(RendererResource& r) {
    const int n = 32;
    std::vector<uint8_t> px(n * n * 4);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            bool on = ((x / 8) + (y / 8)) % 2 == 0;
            uint8_t* p = &px[(y * n + x) * 4];
            p[0] = on ? 240 : 60; p[1] = on ? 180 : 70; p[2] = on ? 90 : 140; p[3] = 255;
        }
    return r.createTextureFromPixels(px.data(), n, n);
}

// 24x24 panel: 6px rounded border, translucent fill.
static TextureHandle makePanel(RendererResource& r) {
    const int n = 24;
    std::vector<uint8_t> px(n * n * 4);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float dx = std::max(0.f, std::max(5.5f - x, x - 17.5f));
            float dy = std::max(0.f, std::max(5.5f - y, y - 17.5f));
            float d = std::sqrt(dx * dx + dy * dy);
            bool border = (x < 3 || y < 3 || x > 20 || y > 20) && d < 6.f;
            bool inside = d < 6.f;
            uint8_t* p = &px[(y * n + x) * 4];
            p[0] = border ? 230 : 20; p[1] = border ? 200 : 24; p[2] = border ? 120 : 40;
            p[3] = inside ? (border ? 255 : 210) : 0;
        }
    return r.createTextureFromPixels(px.data(), n, n);
}

// Radial gradient used as a light.
static TextureHandle makeGlow(RendererResource& r) {
    const int n = 128;
    std::vector<uint8_t> px(n * n * 4);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float dx = (x + 0.5f) / n * 2.f - 1.f, dy = (y + 0.5f) / n * 2.f - 1.f;
            float f = std::max(0.f, 1.f - std::sqrt(dx * dx + dy * dy));
            uint8_t v = static_cast<uint8_t>(f * f * 255.f);
            uint8_t* p = &px[(y * n + x) * 4];
            p[0] = p[1] = p[2] = 255; p[3] = v;
        }
    return r.createTextureFromPixels(px.data(), n, n, {TextureFilter::Linear});
}

// Lights are drawn additively into a light map in world space, then the
// light map is multiplied over the camera's view.
static int32_t addLighting(RendererResource& r, Showcase* sc) {
    return r.addViewHook(0, [sc](RendererResource& rr, const ViewInfo& view) {
        if (view.camera != rr.getDefaultCamera()) return;   // main view only, not the minimap
        rr.beginPass(sc->lightMap, {.clear = true, .clearColor = {0.15f, 0.15f, 0.25f, 1.f},
                                    .viewProj = &view.viewProj});
        const Vec2 lights[3] = {{-300.f, -120.f}, {300.f, -120.f}, {0.f, 200.f}};
        const Color colors[3] = {Color(255, 160, 90), Color(255, 240, 160), Color(160, 120, 255)};
        for (int i = 0; i < 3; ++i) {
            float pulse = 1.f + 0.15f * std::sin(sc->time * 2.f + i);
            rr.draw({.texture = sc->glow, .position = lights[i], .scale = {4.f * pulse, 4.f * pulse},
                     .origin = {64.f, 64.f}, .tint = colors[i], .blend = BlendMode::Additive});
        }
        rr.endPass();
        rr.beginPass(view.target, {.viewportPx = view.viewportPx});
        rr.drawFullscreen(sc->lightMap, BlendMode::Multiply);
        rr.endPass();
    });
}

static void setup(ResMut<RendererResource> r, ResMut<FontResource> fonts,
                  ResMut<Showcase> sc, Commands& cmd) {
    r->setClearColor({0.09f, 0.10f, 0.14f, 1.f});
    sc->checker = makeChecker(*r);
    sc->panel = makePanel(*r);
    sc->minimap = r->createRenderTarget(256, 160, {TextureFilter::Linear});
    sc->font = fonts->loadFont("assets/fonts/DejaVuSansMono.ttf", 18.f);

    ShaderDesc sd;
    sd.wgsl = kPostWGSL;
    sd.glslFragment = kPostGLSL;
    ShaderHandle shader = r->createShader(sd);
    sc->post = r->createMaterial(shader);
    r->setMaterialParam(sc->post, 0, {1.f, 0.f, 0.f, 0.f});
    sc->postId = r->addPostEffect(0, sc->post);

    // A WGSL sprite shader translated at build time by drift_add_shaders
    // (see CMakeLists.txt). Sprites keep the default shader if it is absent.
    std::string shaderDir = std::string(SDL_GetBasePath()) + "shaders/render_showcase/wobble";
    if (ShaderHandle wobble = r->loadShader(shaderDir.c_str())) {
        sc->wobble = r->createMaterial(wobble);
        r->setMaterialParam(sc->wobble, 0, {1.f, 0.f, 0.f, 0.f});
    }

    sc->glow = makeGlow(*r);
    sc->lightMap = r->createRenderTarget(640, 360, {TextureFilter::Linear});
    sc->lightHook = addLighting(*r, sc.get());

    sc->mainCamera = cmd.spawn().insert(Transform2D{{0.f, 0.f}},
                                        Camera{.zoom = 1.f, .active = true}).id();
    // Minimap: same world, zoomed out, rendered into a texture.
    cmd.spawn().insert(Transform2D{{0.f, 0.f}},
                       Camera{.zoom = 0.2f, .active = true, .order = -1,
                              .target = sc->minimap, .clear = true,
                              .clearColor = {0.02f, 0.03f, 0.05f, 1.f}});

    // A grid of rotating checker sprites.
    for (int y = -4; y <= 4; ++y)
        for (int x = -6; x <= 6; ++x) {
            Sprite s;
            s.texture = sc->checker;
            s.origin = {16.f, 16.f};
            s.zOrder = -1.f;
            s.tint = Color(255, 255, 255, 200);
            s.material = sc->wobble;
            cmd.spawn().insert(Transform2D{{x * 160.f, y * 160.f}, 0.f, {1.5f, 1.5f}}, s);
        }
}

static void spin(Res<Time> time, QueryMut<Transform2D, Sprite> sprites) {
    sprites.iter([&](Transform2D& t, Sprite&) { t.rotation += time->delta * 0.5f; });
}

static void controls(Res<InputResource> input, Res<Time> time, ResMut<RendererResource> r,
                     ResMut<Showcase> sc, QueryMut<Transform2D, Camera> cams) {
    float speed = 400.f * time->delta;
    cams.iter([&](Transform2D& t, Camera& c) {
        if (c.target.valid()) return;
        if (input->keyHeld(Key::Left)) t.position.x -= speed / c.zoom;
        if (input->keyHeld(Key::Right)) t.position.x += speed / c.zoom;
        if (input->keyHeld(Key::Up)) t.position.y -= speed / c.zoom;
        if (input->keyHeld(Key::Down)) t.position.y += speed / c.zoom;
        if (input->keyHeld(Key::Q)) c.zoom *= 1.f - time->delta;
        if (input->keyHeld(Key::E)) c.zoom *= 1.f + time->delta;
    });
    if (input->keyPressed(Key::Num1)) r->setScaleMode(ScaleMode::Letterbox);
    if (input->keyPressed(Key::Num2)) r->setScaleMode(ScaleMode::Stretch);
    if (input->keyPressed(Key::Num3)) r->setScaleMode(ScaleMode::Integer);
    if (input->keyPressed(Key::Num4)) r->setScaleMode(ScaleMode::Expand);
    if (input->keyPressed(Key::P)) {
        if (sc->postId) { r->removePostEffect(sc->postId); sc->postId = 0; }
        else sc->postId = r->addPostEffect(0, sc->post);
    }
    if (input->keyPressed(Key::L)) {
        if (sc->lightHook) { r->removeViewHook(sc->lightHook); sc->lightHook = 0; }
        else sc->lightHook = addLighting(*r, sc.get());
    }
    if (input->keyPressed(Key::F12)) r->captureScreenshot("render_showcase.png");
    sc->time += time->delta;
}

static void drawWorld(ResMut<RendererResource> r, Res<Time> time, Res<InputResource> input) {
    float t = static_cast<float>(time->elapsed);

    // Shapes in world space.
    r->drawCircleFilled({-300.f, -120.f}, 60.f, Color(255, 120, 80));
    r->drawCircle({-300.f, -120.f}, 80.f + 8.f * std::sin(t * 3.f), Color(255, 220, 120), 4.f);
    r->drawArc({-300.f, -120.f}, 100.f, t, t + 2.f, Color(120, 220, 255), 6.f);
    r->drawRoundedRectFilled({-80.f, -180.f, 200.f, 120.f}, 24.f, Color(90, 200, 140));
    r->drawRect({-90.f, -190.f, 220.f, 140.f}, Color(255, 255, 255, 160), 2.f);

    Vec2 star[10];
    for (int i = 0; i < 10; ++i) {
        float a = t * 0.7f + i * 3.14159f / 5.f;
        float rad = (i % 2 == 0) ? 90.f : 40.f;
        star[i] = {300.f + std::cos(a) * rad, -120.f + std::sin(a) * rad};
    }
    r->drawPolygonFilled(star, 10, Color(250, 210, 70));
    r->drawPolyline(star, 10, Color(120, 60, 0), 3.f, true, 0.1f);

    Vec2 wave[64];
    for (int i = 0; i < 64; ++i) wave[i] = {-400.f + i * 13.f, 140.f + std::sin(t * 2.f + i * 0.3f) * 30.f};
    r->drawPolyline(wave, 64, Color(200, 120, 255), 5.f);
    r->drawTriangleFilled({-60.f, 260.f}, {60.f, 260.f}, {0.f, 180.f}, Color(255, 90, 160));

    // Mouse in world space.
    Vec2 mouseWorld = r->screenToWorld(input->mousePosition());
    r->drawCircle(mouseWorld, 12.f, Color::white(), 2.f, 0, 5.f);
}

static void drawHud(ResMut<RendererResource> r, ResMut<FontResource> fonts, Res<Showcase> sc,
                    Res<Time> time) {
    r->setLayer(ScreenLayer);
    float w = r->logicalWidth();

    r->drawNineSlice(sc->panel, {16.f, 16.f, 360.f, 116.f}, {}, {8.f, 8.f, 8.f, 8.f});
    if (sc->font.valid()) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "backend: %s\nfps: %.0f  draw calls: %d\n1-4 scale mode, P post, L lights, F12 shot",
                      r->backendName(), time->delta > 0 ? 1.f / time->delta : 0.f, r->drawCalls());
        fonts->drawText(sc->font, buf, {32.f, 30.f}, {.color = Color(240, 230, 210), .zOrder = 1.f});
    }

    // Minimap texture in the corner, framed by a nine-slice panel.
    Rect mm = {w - 16.f - 272.f, 16.f, 272.f, 176.f};
    r->drawNineSlice(sc->panel, mm, {}, {8.f, 8.f, 8.f, 8.f});
    SpriteDraw d;
    d.texture = sc->minimap;
    d.position = {mm.x + 8.f, mm.y + 8.f};
    d.layer = ScreenLayer;
    d.zOrder = 1.f;
    r->draw(d);
    r->setLayer(0);
}

int main() {
    App app;
    app.setConfig({.title = "Drift - render showcase", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.addResource<Showcase>();
    app.startup<setup>();
    app.update<spin>("spin");
    app.update<controls>("controls");
    app.render<drawWorld>("draw_world");
    app.render<drawHud>("draw_hud");
    return app.run();
}
