# UI

The `UIResource` provides an immediate-mode UI system built on Dear ImGui. It renders panels, buttons, labels, sliders, images, progress bars, and nine-slice sprites. The UI is drawn in screen space on top of all game rendering.

**Header:** `#include <drift/resources/UIResource.hpp>`

**Plugin:** `UIPlugin` (included in `DefaultPlugins`)

## Immediate-Mode Pattern

The UI is immediate-mode: you call widget functions every frame and they return interaction results immediately. There is no retained widget tree. If you stop calling a widget, it disappears.

```cpp
void drawMenu(ResMut<UIResource> ui, ResMut<GameState> state) {
    ui->panelBegin(Rect{100, 100, 300, 400});

    ui->label("Main Menu", Vec2{120, 120});

    if (ui->button("Play", Rect{120, 160, 260, 40})) {
        state->screen = Screen::Game;
    }

    if (ui->button("Settings", Rect{120, 210, 260, 40})) {
        state->screen = Screen::Settings;
    }

    if (ui->button("Quit", Rect{120, 260, 260, 40})) {
        state->shouldQuit = true;
    }

    ui->panelEnd();
}
```

## Panels

Panels are rectangular containers that group widgets visually. All widgets drawn between `panelBegin` and `panelEnd` are clipped to the panel area.

```cpp
void panelBegin(Rect rect, PanelFlags flags = PanelFlags::None);
void panelEnd();
```

`PanelFlags` control panel appearance:

```cpp
enum class PanelFlags {
    None       = 0,
    NoBg       = 1 << 0,   // transparent background
    Border     = 1 << 1,   // draw a border
    Scrollable = 1 << 2,   // enable scrolling for overflow content
};
```

Flags can be combined with `|`:

```cpp
ui->panelBegin(Rect{10, 10, 200, 400}, PanelFlags::Border | PanelFlags::Scrollable);
```

## Widgets

### Button

```cpp
bool button(const char* label, Rect rect);
```

Returns `true` on the frame the button is clicked. The button renders with the current theme's button colors and rounding.

```cpp
if (ui->button("Start Game", Rect{100, 200, 200, 50})) {
    // button was clicked this frame
}
```

### Label

```cpp
void label(const char* text, Vec2 pos);
```

Draws static text at the given screen position using the theme's text color.

```cpp
ui->label("Level 3", Vec2{20, 20});
```

### Slider

```cpp
bool slider(const char* label, float* value, float minVal, float maxVal);
```

A horizontal slider that modifies `value` in place. Returns `true` when the value changes.

```cpp
static float volume = 0.5f;
if (ui->slider("Volume", &volume, 0.f, 1.f)) {
    audio->setMusicVolume(volume);
}
```

### Progress Bar

```cpp
void progressBar(float fraction, Rect rect);
```

Draws a horizontal bar filled to `fraction` (0.0 to 1.0).

```cpp
float hpPercent = (float)state->hp / (float)state->maxHp;
ui->progressBar(hpPercent, Rect{20, 60, 200, 20});
```

### Image

```cpp
void image(TextureHandle texture, Rect rect, Rect srcRect);
```

Draws a texture in screen space at the specified rect. The `srcRect` selects a sub-region of the texture (use `{0, 0, w, h}` for the full texture).

```cpp
ui->image(iconTexture, Rect{20, 20, 48, 48}, Rect{0, 0, 48, 48});
```

### Nine-Slice

```cpp
void nineSlice(TextureHandle texture, Rect rect, Rect borders);
```

Draws a texture using nine-slice scaling. The `borders` rect defines the inset from each edge (left, top, right, bottom) that should not be stretched. The corners stay fixed, edges stretch in one direction, and the center fills the remaining area.

```cpp
// borders: 8px inset on all sides
ui->nineSlice(panelTexture, Rect{50, 50, 400, 300}, Rect{8, 8, 8, 8});
```

This is useful for dialog boxes, panels, and buttons that need to scale to different sizes without distorting their border artwork.

## Anchoring

```cpp
void setAnchor(Anchor anchor);
```

Sets the anchor point for subsequent widget positioning. This changes how widget coordinates are interpreted relative to the screen.

```cpp
enum class Anchor {
    TopLeft,      TopCenter,      TopRight,
    CenterLeft,   Center,         CenterRight,
    BottomLeft,   BottomCenter,   BottomRight,
};
```

```cpp
// Position widgets relative to bottom-center of the screen
ui->setAnchor(Anchor::BottomCenter);
ui->button("Action", Rect{-100, -80, 200, 50});
```

## Theming

The `UITheme` struct controls the visual style of all widgets:

```cpp
struct UITheme {
    ColorF primary;       // accent color
    ColorF secondary;     // secondary panels
    ColorF background;    // panel background
    ColorF text;          // text color
    ColorF button;        // button default
    ColorF buttonHover;   // button hovered
    ColorF buttonActive;  // button pressed
    float rounding;       // corner radius in pixels
    float padding;        // inner padding in pixels
};
```

Apply a theme with `setGameTheme`:

```cpp
void setupUI(ResMut<UIResource> ui) {
    UITheme theme;
    theme.primary     = ColorF{0.9f, 0.3f, 0.1f, 1.0f};  // orange accent
    theme.background  = ColorF{0.08f, 0.08f, 0.12f, 0.95f};
    theme.button      = ColorF{0.15f, 0.15f, 0.2f, 1.0f};
    theme.buttonHover = ColorF{0.25f, 0.2f, 0.15f, 1.0f};
    theme.text        = ColorF{1.0f, 0.95f, 0.9f, 1.0f};
    theme.rounding    = 6.f;
    theme.padding     = 12.f;
    ui->setGameTheme(theme);
}
```

### Custom Fonts

Push a custom font for subsequent widget rendering:

```cpp
void drawUI(ResMut<UIResource> ui, Res<Fonts> fonts) {
    ui->pushFont(fonts->heading);
    ui->label("Title", Vec2{100, 50});
    ui->popFont();

    // Back to default font
    ui->label("Body text", Vec2{100, 100});
}
```

## Dev Overlay

When built with `DRIFT_DEV`, the UI resource includes a developer overlay for inspecting engine internals:

```cpp
#ifdef DRIFT_DEV
void devToggle();
void devRender();
bool devIsVisible() const;
#endif
```

This is handled automatically by the engine in development builds. You do not typically need to call these yourself.

## Complete Example: Pause Menu

```cpp
void pauseMenu(ResMut<UIResource> ui, Res<InputResource> input,
               ResMut<AudioResource> audio, ResMut<GameState> state) {
    if (input->keyPressed(Key::Escape)) {
        state->paused = !state->paused;
        if (state->paused) audio->pauseMusic();
        else audio->resumeMusic();
    }

    if (!state->paused) return;

    float w = 300.f, h = 280.f;
    ui->setAnchor(Anchor::Center);
    ui->panelBegin(Rect{-w * 0.5f, -h * 0.5f, w, h}, PanelFlags::Border);

    ui->setAnchor(Anchor::TopLeft); // reset for panel-local coords
    ui->label("PAUSED", Vec2{120, 30});

    static float musicVol = 0.5f;
    if (ui->slider("Music", &musicVol, 0.f, 1.f)) {
        audio->setMusicVolume(musicVol);
    }

    static float sfxVol = 0.8f;
    ui->slider("SFX", &sfxVol, 0.f, 1.f);

    if (ui->button("Resume", Rect{50, 160, 200, 40})) {
        state->paused = false;
        audio->resumeMusic();
    }

    if (ui->button("Quit to Menu", Rect{50, 210, 200, 40})) {
        state->screen = Screen::MainMenu;
        state->paused = false;
    }

    ui->panelEnd();
}
```
