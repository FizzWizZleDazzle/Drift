// =============================================================================
// Text
// =============================================================================
// Text with the built-in font and a bitmap font: alignment, word wrapping,
// sizes, UTF-8 (Latin, Greek, Cyrillic), outline and shadow, inline color
// tags, Text entities in world space and on the screen layer.
//
// Controls: arrow keys move the camera, Q/E zoom, W changes the wrap width,
//           F12 saves a screenshot.
// =============================================================================

#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/Query.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/Text.hpp>
#include <drift/plugins/DefaultPlugins.hpp>
#include <drift/resources/FontResource.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>

#include <cmath>
#include <cstdio>

using namespace drift;

struct Demo : Resource {
    DRIFT_RESOURCE(Demo)
    FontHandle pixel;
    EntityId camera = InvalidEntityId;
    EntityId spinner = InvalidEntityId;
    EntityId counter = InvalidEntityId;
    float wrapWidth = 200.f;
};

static const char* kParagraph =
    "Word wrapping breaks lines at spaces so each one fits the width; "
    "a word longer than the line, like Rindfleischetikettierungsueberwachungsaufgabenuebertragungsgesetz, "
    "is split where it must.";

static void setup(ResMut<FontResource> fonts, ResMut<Demo> demo, Commands& cmd) {
    // A 5x7 pixel font in AngelCode BMFont format. Lowercase maps to
    // uppercase; characters it lacks come from the default font.
    demo->pixel = fonts->loadFont("assets/pixel5x7.fnt", 0.f);

    demo->camera = cmd.spawn().insert(Transform2D{.position = {640.f, 360.f}},
                                      Camera{.active = true}).id();

    // Screen-layer title: a Text entity, centered, with outline and shadow.
    cmd.spawn().insert(Transform2D{.position = {640.f, 14.f}},
                       Text{.text = "Drift text", .size = 44.f,
                            .color = Color(255, 214, 120), .align = TextAlign::Center,
                            .outlineWidth = 3.f, .outlineColor = Color(60, 30, 10),
                            .shadowOffset = {3.f, 4.f}, .layer = ScreenLayer});

    // World-space text that rotates (see spin) and moves with the camera.
    demo->spinner = cmd.spawn().insert(
        Transform2D{.position = {1060.f, 610.f}},
        Text{.text = "world space", .size = 28.f, .color = Color(140, 220, 255),
             .align = TextAlign::Center, .valign = TextVAlign::Middle,
             .outlineWidth = 2.f, .outlineColor = Color(10, 30, 60)}).id();

    // Text whose string changes every frame (see tick).
    demo->counter = cmd.spawn().insert(
        Transform2D{.position = {1260.f, 700.f}},
        Text{.size = 16.f, .color = Color(200, 200, 210), .align = TextAlign::Right,
             .valign = TextVAlign::Bottom, .layer = ScreenLayer}).id();
}

static void spin(Res<Time> time, Res<Demo> demo, QueryMut<Transform2D, Text> texts) {
    float e = static_cast<float>(time->elapsed);
    texts.iterWithEntity([&](EntityId id, Transform2D& t, Text& text) {
        if (id == demo->spinner) {
            t.rotation = std::sin(e * 0.8f) * 0.5f;
            float s = 1.f + 0.1f * std::sin(e * 2.f);
            t.scale = {s, s};
        } else if (id == demo->counter) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.0f fps, %.1f s", time->delta > 0 ? 1.f / time->delta : 0.f,
                          time->elapsed);
            text.set(buf);
        }
    });
}

static void controls(Res<InputResource> input, Res<Time> time, ResMut<RendererResource> r,
                     ResMut<Demo> demo, QueryMut<Transform2D, Camera> cams) {
    float dt = time->delta;
    cams.iter([&](Transform2D& t, Camera& c) {
        if (input->keyHeld(Key::Left))  t.position.x -= 300.f * dt;
        if (input->keyHeld(Key::Right)) t.position.x += 300.f * dt;
        if (input->keyHeld(Key::Up))    t.position.y -= 300.f * dt;
        if (input->keyHeld(Key::Down))  t.position.y += 300.f * dt;
        if (input->keyHeld(Key::Q)) c.zoom = std::max(0.25f, c.zoom - dt);
        if (input->keyHeld(Key::E)) c.zoom = std::min(4.f, c.zoom + dt);
    });
    if (input->keyPressed(Key::W)) demo->wrapWidth = demo->wrapWidth > 230.f ? 200.f : 260.f;
    if (input->keyPressed(Key::F12)) r->captureScreenshot("text.png");
}

// Nine anchors around a box: each label is drawn with the alignment that
// puts it at its own anchor point (marked with a dot).
static void drawAlignment(RendererResource& r, FontResource& fonts) {
    Rect box = {40.f, 90.f, 360.f, 200.f};
    r.drawRect(box, Color(90, 90, 120), 1.f);
    const TextAlign ha[3] = {TextAlign::Left, TextAlign::Center, TextAlign::Right};
    const TextVAlign va[3] = {TextVAlign::Top, TextVAlign::Middle, TextVAlign::Bottom};
    const char* names[3][3] = {{"top left", "top center", "top right"},
                               {"middle left", "middle", "middle right"},
                               {"bottom left", "bottom center", "bottom right"}};
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            Vec2 p = {box.x + box.w * 0.5f * x, box.y + box.h * 0.5f * y};
            r.drawCircleFilled(p, 3.f, Color(255, 90, 90), 0, 1.f);
            fonts.drawText({}, names[y][x], p,
                           {.size = 16.f, .align = ha[x], .valign = va[y], .zOrder = 1.f});
        }
    }
}

// The same paragraph wrapped left, centered and right aligned. The frame
// is measureText's box, so it hugs what drawText covers.
static void drawWrapping(RendererResource& r, FontResource& fonts, float width) {
    const TextAlign aligns[3] = {TextAlign::Left, TextAlign::Center, TextAlign::Right};
    for (int i = 0; i < 3; ++i) {
        float left = 440.f + i * 280.f;
        TextStyle style{.size = 15.f, .color = Color(225, 225, 235), .align = aligns[i],
                        .wrapWidth = width, .lineSpacing = 1.1f, .zOrder = 1.f};
        Vec2 size = fonts.measureText({}, kParagraph, style);
        float anchorX = i == 0 ? left : i == 1 ? left + size.x * 0.5f : left + size.x;
        r.drawRectFilled({left - 6.f, 84.f, size.x + 12.f, size.y + 12.f}, Color(40, 44, 64));
        r.drawRect({left, 90.f, size.x, size.y}, Color(110, 110, 160), 1.f, 0.5f);
        fonts.drawText({}, kParagraph, {anchorX, 90.f}, style);
    }
}

static void drawSamples(RendererResource& r, FontResource& fonts, Res<Demo> demo) {
    // Sizes, each rasterized at its own size.
    float x = 40.f;
    const float sizes[] = {10.f, 14.f, 18.f, 24.f, 32.f, 48.f, 64.f};
    for (float s : sizes) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%gpx", s);
        fonts.drawText({}, buf, {x, 360.f}, {.size = s, .valign = TextVAlign::Bottom, .zOrder = 1.f});
        x += fonts.measureText({}, buf, {.size = s}).x + 18.f;
    }

    // UTF-8 in several scripts.
    const char* lines =
        "Fran\u00e7ais : \u00e0 la cr\u00e8me br\u00fbl\u00e9e, na\u00efve co\u00efncidence\n"
        "Deutsch : Gr\u00f6\u00dfe, Stra\u00dfe, \u00fcber \u00c4rger\n"
        "Polski : Za\u017c\u00f3\u0142\u0107 g\u0119\u015bl\u0105 ja\u017a\u0144\n"
        "\u010ce\u0161tina : P\u0159\u00edli\u0161 \u017elu\u0165ou\u010dk\u00fd k\u016f\u0148\n"
        "\u0420\u0443\u0441\u0441\u043a\u0438\u0439 : \u0421\u044a\u0435\u0448\u044c \u0436\u0435 "
        "\u0435\u0449\u0451 \u044d\u0442\u0438\u0445 \u043c\u044f\u0433\u043a\u0438\u0445 "
        "\u0444\u0440\u0430\u043d\u0446\u0443\u0437\u0441\u043a\u0438\u0445 \u0431\u0443\u043b\u043e\u043a\n"
        "\u0395\u03bb\u03bb\u03b7\u03bd\u03b9\u03ba\u03ac : \u039e\u03b5\u03c3\u03ba\u03b5\u03c0\u03ac\u03b6\u03c9 "
        "\u03c4\u03b7\u03bd \u03c8\u03c5\u03c7\u03bf\u03c6\u03b8\u03cc\u03c1\u03b1 "
        "\u03b2\u03b4\u03b5\u03bb\u03c5\u03b3\u03bc\u03af\u03b1\n"
        "Kerning : AVATAR, Toyota, WAVE, \u201cquotes\u201d \u2026 \u20ac 42";
    fonts.drawText({}, lines, {40.f, 384.f}, {.size = 19.f, .lineSpacing = 1.15f, .zOrder = 1.f});

    // Outline, shadow, and inline color tags, on a lighter panel so the
    // shadow shows.
    r.drawRoundedRectFilled({28.f, 592.f, 560.f, 60.f}, 8.f, Color(70, 80, 110));
    fonts.drawText({}, "Outline", {40.f, 600.f},
                   {.size = 40.f, .color = Color::white(), .outlineWidth = 3.f,
                    .outlineColor = Color(200, 40, 80), .zOrder = 1.f});
    fonts.drawText({}, "Shadow", {230.f, 600.f},
                   {.size = 40.f, .color = Color(250, 250, 200), .shadowOffset = {4.f, 4.f},
                    .shadowColor = Color(0, 0, 0, 200), .zOrder = 1.f});
    fonts.drawText({}, "Both", {420.f, 600.f},
                   {.size = 40.f, .color = Color(120, 255, 160), .outlineWidth = 2.f,
                    .outlineColor = Color(0, 60, 30), .shadowOffset = {3.f, 5.f}, .zOrder = 1.f});
    fonts.drawText({}, "Tags: [color=#ff6060]red[/color], [color=#60ff60]green[/color], "
                       "[color=#6090ff]blue [color=#ffffff80]and faded[/color][/color], [[literal]",
                   {40.f, 660.f}, {.size = 18.f, .richText = true, .zOrder = 1.f});

    // Bitmap font at whole multiples of its size, then with an outline.
    if (demo->pixel.valid()) {
        fonts.drawText(demo->pixel, "Pixel font 5x7: Hello, World! 0123456789", {720.f, 384.f},
                       {.size = 16.f, .color = Color(255, 230, 140), .zOrder = 1.f});
        fonts.drawText(demo->pixel, "Bitmap x3", {720.f, 412.f},
                       {.size = 24.f, .color = Color(140, 255, 200), .zOrder = 1.f});
        fonts.drawText(demo->pixel, "Outlined x4", {720.f, 450.f},
                       {.size = 32.f, .color = Color::white(), .outlineWidth = 4.f,
                        .outlineColor = Color(90, 40, 140), .shadowOffset = {4.f, 4.f}, .zOrder = 1.f});
        fonts.drawText(demo->pixel, "Fallback: \u00c9t\u00e9 \u0416\u0438\u0437\u043d\u044c", {720.f, 500.f},
                       {.size = 16.f, .color = Color(255, 200, 200), .zOrder = 1.f});
    }
}

static void drawHud(ResMut<RendererResource> r, ResMut<FontResource> fonts, Res<Demo> demo) {
    r->setLayer(ScreenLayer);
    drawAlignment(*r, *fonts);
    drawWrapping(*r, *fonts, demo->wrapWidth);
    drawSamples(*r, *fonts, demo);
    fonts->drawText({}, "Arrows move the camera, Q/E zoom, W wrap width, F12 screenshot",
                    {20.f, 700.f}, {.size = 14.f, .color = Color(150, 150, 170),
                                     .valign = TextVAlign::Bottom});
    r->setLayer(0);
}

static void drawWorld(ResMut<RendererResource> r) {
    // A grid under the world-space text so camera movement is visible.
    for (int i = 0; i <= 32; ++i) {
        float p = i * 40.f;
        r->drawLine({p, 0.f}, {p, 720.f}, Color(30, 34, 48), 1.f, -1.f);
        if (p <= 720.f) r->drawLine({0.f, p}, {1280.f, p}, Color(30, 34, 48), 1.f, -1.f);
    }
}

int main() {
    App app;
    app.setConfig({.title = "Drift - text", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.addResource<Demo>();
    app.startup<setup>();
    app.update<spin>("spin");
    app.update<controls>("controls");
    app.render<drawWorld>("draw_world");
    app.render<drawHud>("draw_hud");
    return app.run();
}
