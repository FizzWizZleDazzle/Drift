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
- `anchor` is the point of the image placed at the position, from (0, 0) top-left to (1, 1) bottom-right. The default (0.5, 0.5) centers it. It is also the pivot: the sprite turns and scales around it.
- `color` tints the image and its `a` sets the opacity: `color: rgb(255, 255, 255).alpha(0.5)` draws it half transparent. `flip_x` and `flip_y` mirror it, `blend` picks how it mixes with what is behind it (`Blend::Additive` for glows).
- `Transform.rotation` (radians, clockwise on screen) and `Transform.scale` turn and size the sprite around its anchor. A negative scale mirrors it as well.

To turn a limb around its joint rather than its middle, put the anchor on the joint. For an arm image 64 pixels wide whose shoulder is at its left edge, halfway down:

```rust
cmd.spawn([
    Transform { position: vec2(200.0, 150.0), rotation: 0.6, .. },
    Sprite { image: texture("assets/arm.png"), anchor: vec2(0.0, 0.5), .. },
]);
```

For a joint at pixel (jx, jy) of a w x h image, the anchor is `vec2(jx / w, jy / h)`. With a `frame`, count from the frame's top-left and use its size.

## Shapes

`Rectangle` and `Circle` draw filled shapes, or outlines when `outline` is above zero:

```rust
cmd.spawn([at(100.0, 100.0), Rectangle { size: vec2(64.0, 16.0), color: rgb(80, 200, 120), corner_radius: 4.0, .. }]);
cmd.spawn([at(200.0, 100.0), Circle { radius: 20.0, color: rgb(255, 255, 255), outline: 2.0, .. }]);
```

An entity draws one of `Sprite`, `Rectangle` or `Circle`, plus any `Text`. For a shape made of several parts, spawn several entities and give the parts a `Parent`.

## Parts that move together

A `Parent` makes an entity's `Transform` count from another entity: its position, rotation and scale are relative to the parent, so when the parent moves, turns or scales, the child goes with it. Chains of parents make a rig, such as a body, an upper arm, a forearm and a hand. `cmd.spawn_child(parent, bundle)` spawns one:

```rust
@component struct Joint { speed: float = 1.0 }

@system(schedule: Schedule::Startup)
fn build_arm(cmd: Commands) {
    let arm = texture("assets/arm.png");   // 64 x 12, shoulder at the left edge
    let body = cmd.spawn([at(200.0, 240.0), Rectangle { size: vec2(40.0, 60.0), .. }]);
    let upper = cmd.spawn_child(body, [at(15.0, -20.0), Sprite { image: arm, anchor: vec2(0.0, 0.5), .. }, Joint { speed: 1.0 }]);
    let fore = cmd.spawn_child(upper, [at(64.0, 0.0), Sprite { image: arm, anchor: vec2(0.0, 0.5), .. }, Joint { speed: 2.0 }]);
    cmd.spawn_child(fore, [at(64.0, 0.0), Circle { radius: 8.0, .. }]);   // the hand, at the forearm's end
}

@system
fn swing(j: Joint, t: Transform, time: Time) {
    t.rotation = 0.6 * sin(time.elapsed * j.speed);
}
```

Each part's position is where its joint sits on the parent, in the parent's own units: the forearm is at (64, 0) because the upper arm is 64 pixels long. Setting the root's `scale` to (-1, 1) mirrors the whole rig, children's rotations included, which turns a character around.

- `GlobalTransform` is a part's place in the world after every parent. Query it like any component; assign to it to put a part at a world position, and the engine works out the local `Transform`.
- `world_transform(e)`, `to_world(e, local)` and `to_local(e, world)` convert between the world and an entity's space at any time, from the engine's current transforms. A system's own changes land when it returns, so these see them from the next system on.
- `parent_of(e)`, `children_of(e)` and `descendants_of(e)` walk the tree.
- `cmd.set_parent(child, parent)` and `cmd.set_parent_in_place(child, parent)` move an entity under another, the second keeping it where it is on screen; `cmd.detach(child)` makes it a root again where it is.
- Despawning an entity despawns everything below it.

The engine composes the transforms after the Update systems and before drawing, every frame.

## Draw order and layers

Every drawable has a `layer` and a `z`. Higher layers draw over lower ones; within a layer, higher `z` draws on top. World layers are 0 to 30, and cameras draw the layers in their `layer_mask`, every world layer (`ALL_LAYERS`) by default. `layer_mask([0, 3])` builds a mask, for example for a minimap camera that leaves out the decoration on layer 2.

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

`CameraBounds` stops the camera at the edges of a level, so the view never shows what lies past them:

```rust
cmd.spawn([at(0.0, 0.0), Camera { .. }, CameraFollow { target: player, .. },
           CameraBounds { area: rect(0.0, 0.0, 3200.0, 720.0) }]);
```

`Camera2D` converts between screen and world positions through the main camera, for example to aim at the mouse, and `visible()` gives the world rect on screen. The `_in` versions, such as `screen_to_world_in(camera, p)`, do the same through any camera entity. `Effects` shakes a camera with `shake(camera, intensity)`.

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

## Textures from code

An `Image` is pixels you change from code: fill it, draw shapes and other images into it, set single pixels, then show it with `to_texture()`. Keep the texture and call `texture.update_rect(image, r)` after each change; it uploads only that part, so painting every frame stays cheap in the browser.

```rust
let img = assets.create_image(128, 128, rgb(0, 0, 0));
img.gradient(rgb(40, 0, 80), rgb(255, 120, 0), true);
img.circle(vec2(64.0, 64.0), 24.0, rgba(255, 255, 255, 200));
cmd.spawn([at(200.0, 200.0), Sprite { image: img.to_texture(), .. }]);
```

A render target's `to_image()` reads back what its camera drew, and `save_png` or `download` keeps it. See [Image](../reference/resources.md#image) for every method.

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

`Window` changes the mode and size while the game runs, and its `width` and `height` report the size in effect. It also sets the title, the background color, fullscreen and the mouse cursor. The background color is `clear_color` in `[render]`.

## Shaders

A sprite with a `material` draws through a shader of your own: a hit flash, a dissolve, water. Write the fragment stage in GLSL, the `main` of a shader that sets `o_color`; the engine adds the inputs `v_uv`, `v_color`, `v_world`, the textures `u_texture0` to `u_texture3`, and `material.params[0]` to `[7]`. The browser and desktop OpenGL run GLSL; add a `.wgsl` file beside it for the WebGPU backend.

```rust
const FLASH: string = "void main() {
    vec4 c = texture(u_texture0, v_uv) * v_color;
    o_color = vec4(mix(c.rgb, material.params[0].rgb, material.params[0].a), c.a);
}";

@system(schedule: Schedule::Startup)
fn setup(cmd: Commands, assets: Assets) {
    let flash = assets.create_material(assets.create_shader("", FLASH));
    flash.set_color(0, rgba(255, 255, 255, 180));
    cmd.spawn([at(200.0, 200.0), Sprite { image: texture("assets/enemy.png"), material: flash, .. }]);
}
```

`assets.shader("shaders/flash")` loads `shaders/flash.frag.glsl` and `shaders/flash.wgsl` instead. A material can also run over the whole picture as a post effect with `effects.add_material(material, order)`; the picture arrives as `u_texture0`.

See [Engine Components](../reference/components.md#drawing) for every field.
