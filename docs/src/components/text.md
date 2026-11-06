# Text

The `Text` component draws a string at the entity's transform, the way `Sprite` draws a texture. Use it for labels, damage numbers, signs in the world, and HUD text that lives as long as an entity. `FontPlugin` (part of `DefaultPlugins`) draws every visible `Text` entity each frame.

**Header:** `#include <drift/components/Text.hpp>`

## Definition

```cpp
struct Text {
    static constexpr size_t Capacity = 512;

    char       text[Capacity] = {};
    FontHandle font;                         // empty = the built-in font
    float      size = 0.f;                   // pixel height; 0 = the font's load size
    Color      color = Color::white();
    TextAlign  align = TextAlign::Left;
    TextVAlign valign = TextVAlign::Top;
    float      wrapWidth = 0.f;              // 0 = no wrapping
    float      lineSpacing = 1.f;
    float      outlineWidth = 0.f;           // 0 = no outline
    Color      outlineColor = Color::black();
    Vec2       shadowOffset = {};            // {0,0} = no shadow
    Color      shadowColor = {0, 0, 0, 160};
    bool       richText = false;
    float      zOrder = 0.f;
    uint8_t    layer = 0;                    // 0..30 world layers, ScreenLayer for HUD text
    bool       visible = true;

    void set(std::string_view s);
    std::string_view str() const;
    TextStyle style() const;
};
```

The fields match [`TextStyle`](../subsystems/fonts.md#textstyle); see the fonts page for what each one does. `layer` defaults to world layer 0, like `Sprite`.

## Usage

```cpp
void setup(Commands& cmd) {
    // HUD title, centered at the top of the screen.
    cmd.spawn().insert(
        Transform2D{.position = {640.f, 20.f}},
        Text{.text = "Level 1", .size = 40.f, .align = TextAlign::Center,
             .outlineWidth = 2.f, .layer = ScreenLayer});

    // A sign in the world: moves with the camera, centered on its position.
    cmd.spawn().insert(
        Transform2D{.position = {300.f, 180.f}},
        Text{.text = "SHOP", .color = Color(255, 220, 120),
             .align = TextAlign::Center, .valign = TextVAlign::Middle});
}
```

Position, rotation, and scale come from `Transform2D`, or from `GlobalTransform2D` when the entity is a child in a [hierarchy](hierarchy.md), so a label can follow its parent. `align` and `valign` choose which point of the text sits on the position, which is also the point it rotates around.

On `ScreenLayer` the position is in logical screen pixels; on world layers it is in world units and the text is seen through the cameras.

## Changing the string

The string is stored inside the component, so components stay plain data. Assign a literal in an initializer, or call `set` at runtime:

```cpp
void updateScore(Res<GameState> state, QueryMut<Text, With<ScoreLabel>> labels) {
    labels.iter([&](Text& t) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Score: %d", state->score);
        t.set(buf);
    });
}
```

`set` keeps up to `Capacity - 1` bytes and cuts longer input at a UTF-8 character boundary. For longer text, such as a page of dialog, call `FontResource::drawText` from a render system instead.

## Scaling

`Transform2D::scale` stretches the rendered glyphs, which is right for animation (a pulse, a pop-in). For text that is simply bigger, raise `size` instead so the glyphs are rasterized at that size and stay sharp.
