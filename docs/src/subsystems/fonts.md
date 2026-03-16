# Fonts

The `FontResource` loads TrueType fonts and draws text to the screen. It uses `stb_truetype` for glyph rasterization and renders text as textured quads through the renderer.

**Header:** `#include <drift/resources/FontResource.hpp>`

**Plugin:** `FontPlugin` (included in `DefaultPlugins`)

## Loading Fonts

```cpp
FontHandle loadFont(const char* path, float sizePx);
void       destroyFont(FontHandle font);
```

`loadFont` loads a `.ttf` or `.otf` file and rasterizes it at the given pixel size. Each size requires a separate `FontHandle` -- if you need the same font at 16px and 32px, load it twice.

```cpp
struct Fonts : public Resource {
    DRIFT_RESOURCE(Fonts)
    FontHandle body;
    FontHandle heading;
    FontHandle small;
};

void loadFonts(ResMut<FontResource> fonts, ResMut<Fonts> f) {
    f->body    = fonts->loadFont("assets/fonts/roboto.ttf", 24.f);
    f->heading = fonts->loadFont("assets/fonts/roboto.ttf", 48.f);
    f->small   = fonts->loadFont("assets/fonts/roboto.ttf", 14.f);
}
```

## Drawing Text

```cpp
void drawText(FontHandle font, const char* text, Vec2 position,
              Color color = Color::white(), float scale = 1.f);
```

Draws text at the given world-space position. The position is the top-left corner of the first character. Text is rendered in world space and moves with the camera.

```cpp
void drawScore(ResMut<FontResource> fonts, Res<Fonts> f, Res<GameState> state) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Score: %d", state->score);
    fonts->drawText(f->body, buf, Vec2{20.f, 20.f}, Color::white());
}
```

The `scale` parameter multiplies the rasterized size. A font loaded at 24px drawn with `scale = 2.0` will appear as 48px. For best visual quality, load the font at the target size rather than scaling up.

## Measuring Text

```cpp
Vec2 measureText(FontHandle font, const char* text, float scale = 1.f) const;
```

Returns the bounding box size (width, height) of the rendered text without drawing it. Use this for centering, layout, and UI calculations.

```cpp
Vec2 size = fonts->measureText(f->heading, "Game Over");
// size.x = width in pixels, size.y = height in pixels
```

### Centering Text Manually

```cpp
void drawCenteredText(FontResource& fonts, FontHandle font,
                      const char* text, Vec2 center, Color color) {
    Vec2 size = fonts.measureText(font, text);
    Vec2 pos = {center.x - size.x * 0.5f, center.y - size.y * 0.5f};
    fonts.drawText(font, text, pos, color);
}
```

## Complete Example

A HUD that displays score, health, and a timer:

```cpp
struct HUD : public Resource {
    DRIFT_RESOURCE(HUD)
    FontHandle font;
    FontHandle titleFont;
};

void setupHUD(ResMut<FontResource> fonts, ResMut<HUD> hud) {
    hud->font      = fonts->loadFont("assets/fonts/monospace.ttf", 20.f);
    hud->titleFont = fonts->loadFont("assets/fonts/monospace.ttf", 40.f);
}

void drawHUD(ResMut<FontResource> fonts, Res<HUD> hud,
             Res<Time> time, Res<GameState> state,
             Res<RendererResource> renderer) {
    float screenW = renderer->logicalWidth();

    // Score (top-left)
    char scoreBuf[64];
    snprintf(scoreBuf, sizeof(scoreBuf), "Score: %d", state->score);
    fonts->drawText(hud->font, scoreBuf, Vec2{10.f, 10.f}, Color::white());

    // Health bar label (top-left, below score)
    char healthBuf[32];
    snprintf(healthBuf, sizeof(healthBuf), "HP: %d / %d", state->hp, state->maxHp);
    fonts->drawText(hud->font, healthBuf, Vec2{10.f, 36.f}, Color::green());

    // Timer (top-right)
    int seconds = (int)time->elapsed;
    int minutes = seconds / 60;
    seconds %= 60;
    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", minutes, seconds);
    Vec2 timerSize = fonts->measureText(hud->font, timeBuf);
    fonts->drawText(hud->font, timeBuf,
                    Vec2{screenW - timerSize.x - 10.f, 10.f},
                    Color::white());

    // Game Over text (centered)
    if (state->gameOver) {
        const char* msg = "GAME OVER";
        Vec2 size = fonts->measureText(hud->titleFont, msg);
        float screenH = renderer->logicalHeight();
        fonts->drawText(hud->titleFont, msg,
                        Vec2{(screenW - size.x) * 0.5f, (screenH - size.y) * 0.5f},
                        Color::red());
    }
}
```
