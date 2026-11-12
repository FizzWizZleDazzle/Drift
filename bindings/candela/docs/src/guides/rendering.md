# Sprites, Shapes and Cameras

Everything you see is a component: put a `Sprite`, `Rectangle` or `Circle` on an entity with a `Transform`, and the engine draws it every frame. There is no draw call to write.

## Sprites

A sprite draws a texture at its entity's position. `image` holds the `Texture`; `texture(path)` loads one from a file once and hands back the same texture for the same path:

```rust
@system(schedule: Schedule::Startup)
fn setup(cmd: Commands) {
    cmd.spawn([at(320.0, 180.0), Sprite { image: texture("assets/ship.png"), .. }]);
}
```

`Assets.create_texture(w, h, pixels)` makes a texture from colors instead, and a render target is a texture too. Textures sample with `Filter::Nearest` by default, which keeps pixel art crisp; call `set_filter(Filter::Linear)` for smooth scaling.

- `frame` draws part of the image, as a `Rect` in pixels. A zero-size rect draws all of it. Spritesheets hand out these rects; see [Animation](animation.md).
- `anchor` is the point of the image placed at the position, from (0, 0) top-left to (1, 1) bottom-right. The default (0.5, 0.5) centers it.
- `color` tints the image, `flip_x` and `flip_y` mirror it, `blend` picks how it mixes with what is behind it (`Blend::Additive` for glows).
- `Transform.rotation` and `Transform.scale` turn and size the sprite around its anchor.

## Shapes

`Rectangle` and `Circle` draw filled shapes, or outlines when `outline` is above zero:

```rust
cmd.spawn([at(100.0, 100.0), Rectangle { size: vec2(64.0, 16.0), color: rgb(80, 200, 120), corner_radius: 4.0, .. }]);
cmd.spawn([at(200.0, 100.0), Circle { radius: 20.0, color: rgb(255, 255, 255), outline: 2.0, .. }]);
```

An entity draws one of `Sprite`, `Rectangle` or `Circle`, plus any `Text`. For a shape made of several parts, spawn several entities and give the parts a `Parent`.

## Draw order and layers

Every drawable has a `layer` and a `z`. Higher layers draw over lower ones; within a layer, higher `z` draws on top. World layers are 0 to 30, and cameras draw the layers in their `layer_mask`.

`hud_layer()` is the screen layer. It draws last, after post effects, in screen coordinates without a camera: (0, 0) is the top-left of the window's logical area. Put scores, menus and health bars there:

```rust
cmd.spawn([at(16.0, 16.0), Text { value: "Lives 3", size: 24.0, layer: hud_layer(), .. }]);
```

Set `visible: false` to hide a drawable without removing it.

## Cameras

Without a `Camera` entity, world position (0, 0) is the top-left of the screen and one world unit is one logical pixel. A `Camera` entity looks at the world from its `Transform`, centered on its position:

```rust
cmd.spawn([at(0.0, 0.0), Camera { zoom: 2.0, .. }]);
```

`CameraFollow` keeps a camera on another entity. `smoothing` sets how quickly it catches up, and the camera stays still while the target is inside `dead_zone`:

```rust
let player = cmd.spawn([at(0.0, 0.0), Sprite { image: texture("assets/hero.png"), .. }]);
cmd.spawn([at(0.0, 0.0), Camera { .. }, CameraFollow { target: player, smoothing: 5.0, .. }]);
```

`Camera2D` converts between screen and world positions through the main camera, for example to aim at the mouse. `Effects` shakes a camera with `shake(camera, intensity)`.

### Split screen

Give each camera a `viewport`, a rect from (0, 0) to (1, 1) of the window, and an `order` to draw in:

```rust
cmd.spawn([at(0.0, 0.0), Camera { viewport: rect(0.0, 0.0, 0.5, 1.0), order: 0, .. }, CameraFollow { target: one, .. }]);
cmd.spawn([at(0.0, 0.0), Camera { viewport: rect(0.5, 0.0, 0.5, 1.0), order: 1, .. }, CameraFollow { target: two, .. }]);
```

The first active camera that draws to the window is the main camera.

### Render targets

A camera can draw into a texture instead of the window. Make the texture with `assets.render_target(w, h)`, set it as the camera's `target`, then show it with a sprite, for example as a minimap on the HUD:

```rust
@system(schedule: Schedule::Startup)
fn minimap(cmd: Commands, assets: Assets) {
    let map = assets.render_target(256, 160);
    cmd.spawn([at(0.0, 0.0), Camera { zoom: 0.2, order: -1, target: map, clear: true, .. }]);
    cmd.spawn([at(1140.0, 100.0), Sprite { image: map, layer: hud_layer(), .. }]);
}
```

Cameras with a lower `order` draw first, so the minimap is ready before the main camera's frame uses it.

## Resolution and scale modes

The game draws at the logical resolution `width` and `height` in the `[window]` section of `drift.toml`. `scale_mode` decides how that fits the window:

```toml
[window]
width = 320
height = 180
scale_mode = "integer"
```

| Mode | Result |
|------|--------|
| `Letterbox` | Keeps the aspect ratio; bars fill the rest. The default |
| `Stretch` | Fills the window, distorting the aspect ratio |
| `Integer` | Pixel perfect: scales by whole numbers |
| `Expand` | Keeps pixels square and grows the logical size, so wider screens see more |

`Window` changes the mode and size while the game runs, and its `width` and `height` report the size in effect. The background color is `clear_color` in `[render]`.

See [Engine Components](../reference/components.md#drawing) for every field.
