# Cameras

A camera decides which part of the world is on screen. A game that fits on one screen needs none; a game with a world larger than the screen puts a camera on the player.

## The default view

Without a `Camera` entity, world position (0, 0) is the top-left of the screen and one world unit is one logical pixel, so a 960 by 540 game shows the world from (0, 0) to (960, 540). Every example in this book that fits on one screen works this way.

## A camera

A `Camera` entity looks at the world from its `Transform`, centered on its position. A camera at the middle of the screen shows what the default view shows; move it to scroll, and set `zoom` to magnify:

```rust
cmd.spawn([at(480.0, 270.0), Camera { zoom: 2.0, .. }]);
```

## Following the player

`CameraFollow` keeps a camera on another entity. `smoothing` sets how quickly it catches up, and the camera stays still while the target is inside `dead_zone`:

```rust
let player = cmd.spawn([at(0.0, 0.0), Sprite { image: texture("assets/hero.png"), .. }]);
cmd.spawn([at(0.0, 0.0), Camera { .. }, CameraFollow { target: player, smoothing: 5.0, .. }]);
```

`CameraBounds` stops the camera at the edges of a level, so the view never shows what lies past them:

```rust
cmd.spawn([at(0.0, 0.0), Camera { .. }, CameraFollow { target: player, .. },
           CameraBounds { area: rect(0.0, 0.0, 3200.0, 720.0) }]);
```

## Screen and world positions

`Input.mouse_world()` is the mouse through the main camera, which is what aiming and clicking on things need. `Camera2D` converts between screen and world positions through the main camera, for example to aim at the mouse, and `visible()` gives the world rect on screen. The `_in` versions, such as `screen_to_world_in(camera, p)`, do the same through any camera entity. Keep the camera's entity in a resource to shake it with `effects.shake(camera, intensity)` and to tween its zoom with `tween().zoom_to(...)`.

A HUD on `hud_layer()` does not move with the camera; see [UI and Score](ui-and-score.md).

## Split screen

Give each camera a `viewport`, a rect from (0, 0) to (1, 1) of the window, and an `order` to draw in:

```rust
cmd.spawn([at(0.0, 0.0), Camera { viewport: rect(0.0, 0.0, 0.5, 1.0), order: 0, .. }, CameraFollow { target: one, .. }]);
cmd.spawn([at(0.0, 0.0), Camera { viewport: rect(0.5, 0.0, 0.5, 1.0), order: 1, .. }, CameraFollow { target: two, .. }]);
```

The first active camera that draws to the window is the main camera.

## Render targets

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
| `letterbox` | Keeps the aspect ratio; bars fill the rest. The default |
| `stretch` | Fills the window, distorting the aspect ratio |
| `integer` | Pixel perfect: scales by whole numbers |
| `expand` | Keeps pixels square and grows the logical size, so wider screens see more |

`Window` changes the mode and size while the game runs, and its `width` and `height` report the size in effect. It also sets the title, the background color, fullscreen and the mouse cursor. The background color is `clear_color` in `[render]`.

See [`Camera`](../reference/components.md#camera) and [`Camera2D`](../reference/resources.md#camera2d) for every field and method.
