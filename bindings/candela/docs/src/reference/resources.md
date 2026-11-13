<!-- Written by scripts/candela-reference.py from the drift module and
     bindings/candela/docs/api-notes.md. Edit those, not this page. -->

# Engine Resources

The engine's resources reach a system as parameters, the same way as your own. Their fields are this frame's values, and their methods act on the engine at once.

## Time

`dt` is the frame time times `scale`; `real_dt` ignores the scale, for menus and tools that keep running while the game is slowed or frozen.

```rust
struct Time {
    dt: float = 0.0,
    elapsed: float = 0.0,
    frame: int = 0,
    scale: float = 1.0,
    real_dt: float = 0.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `dt` | `float` | `0.0` | Seconds since the last frame, times `scale`; multiply speeds by it |
| `elapsed` | `float` | `0.0` | Seconds since the game started |
| `frame` | `int` | `0` | Frames so far |
| `scale` | `float` | `1.0` | The game's speed: 1 normal, 0.5 half, 0 frozen |
| `real_dt` | `float` | `0.0` | Seconds since the last frame, ignoring `scale` |

```rust
@system
fn spin(fan: (Fan, Transform), time: Time) {
    let (_, t) = fan;
    t.rotation += 3.0 * time.dt;
}
```

### Time.set_scale

```rust
fn set_scale(self, s: float)
```

Game speed from the next frame on: 1.0 normal, 0.5 half, 0.0 frozen. Physics, tweens, particles and every system's `dt` follow it.

```rust
time.set_scale(0.25);   // slow motion
```

## Input

The keyboard, mouse, gamepads, touch and input actions. An action is a name, such as `"jump"`, that any mix of keys, buttons and sticks can drive; write game logic against actions. Bind actions in the `[input]` section of `drift.toml` or from code. Reading an action the game never declared, such as a misspelt `"jmup"`, raises `drift_unknown_action`, which lists the actions there are. See [Input](../guides/input.md).

```rust
struct Input {
    id: int = 0,
}
```

```rust
@system
fn controls(input: Input, window: Window) {
    if input.action_pressed("pause") {
        log("paused at " + string(input.mouse_world().x));
    }
}
```

### Input.key_down

```rust
fn key_down(self, k: Key) -> bool
```

Whether the key is held.

```rust
if input.key_down(Key::LShift) { speed = speed * 2.0; }
```

### Input.key_pressed

```rust
fn key_pressed(self, k: Key) -> bool
```

Whether the key went down this frame.

```rust
if input.key_pressed(Key::F1) { show_help = !show_help; }
```

### Input.key_released

```rust
fn key_released(self, k: Key) -> bool
```

Whether the key went up this frame.

```rust
if input.key_released(Key::Space) { charge = 0.0; }
```

### Input.mouse_down

```rust
fn mouse_down(self, b: MouseButton) -> bool
```

Whether the mouse button is held.

```rust
if input.mouse_down(MouseButton::Left) { paint(input.mouse_world()); }
```

### Input.mouse_pressed

```rust
fn mouse_pressed(self, b: MouseButton) -> bool
```

Whether the mouse button went down this frame.

```rust
if input.mouse_pressed(MouseButton::Right) { place_flag(input.mouse_world()); }
```

### Input.mouse_released

```rust
fn mouse_released(self, b: MouseButton) -> bool
```

Whether the mouse button went up this frame.

```rust
if input.mouse_released(MouseButton::Left) { drop_item(); }
```

### Input.mouse_position

```rust
fn mouse_position(self) -> Vec2
```

The mouse in logical screen pixels, letterbox bars accounted for.

```rust
let p = input.mouse_position();
```

### Input.mouse_world

```rust
fn mouse_world(self) -> Vec2
```

The mouse in world coordinates, through the main camera.

```rust
let aim = (input.mouse_world() - t.position).normalized();
```

### Input.mouse_delta

```rust
fn mouse_delta(self) -> Vec2
```

How far the mouse moved this frame.

```rust
if input.mouse_delta().length() > 0.0 { show_cursor = true; }
```

### Input.gamepad_down

```rust
fn gamepad_down(self, pad: int, b: GamepadButton) -> bool
```

Whether a gamepad button is held. Pads are numbered 0 to 3 and use the Xbox layout: `South` is A.

```rust
if input.gamepad_down(0, GamepadButton::South) { charge += time.dt; }
```

### Input.gamepad_pressed

```rust
fn gamepad_pressed(self, pad: int, b: GamepadButton) -> bool
```

Whether a gamepad button went down this frame.

```rust
if input.gamepad_pressed(0, GamepadButton::Start) { phase.set(Phase::Paused); }
```

### Input.gamepad_released

```rust
fn gamepad_released(self, pad: int, b: GamepadButton) -> bool
```

Whether a gamepad button went up this frame.

```rust
if input.gamepad_released(0, GamepadButton::South) { jump_held = false; }
```

### Input.gamepad_axis

```rust
fn gamepad_axis(self, pad: int, a: GamepadAxis) -> float
```

A stick axis from -1 to 1 (Y down), or a trigger from 0 to 1.

```rust
let x = input.gamepad_axis(0, GamepadAxis::LeftX);
```

### Input.bind_key

```rust
fn bind_key(self, action: string, k: Key)
```

Adds a key to an action.

```rust
input.bind_key("jump", Key::Space);
```

### Input.bind_mouse

```rust
fn bind_mouse(self, action: string, b: MouseButton)
```

Adds a mouse button to an action.

```rust
input.bind_mouse("fire", MouseButton::Left);
```

### Input.bind_button

```rust
fn bind_button(self, action: string, b: GamepadButton)
```

Adds a gamepad button to an action.

```rust
input.bind_button("jump", GamepadButton::South);
```

### Input.bind_axis2d

```rust
fn bind_axis2d(self, name: string, left: Key, right: Key, up: Key, down: Key)
```

A 2D axis from four keys; up is -Y, and diagonal key input has length 1. Calling it again for the same name adds another set of keys.

```rust
input.bind_axis2d("move", Key::A, Key::D, Key::W, Key::S);
input.bind_axis2d("move", Key::Left, Key::Right, Key::Up, Key::Down);
```

### Input.bind_stick

```rust
fn bind_stick(self, name: string, x: GamepadAxis, y: GamepadAxis)
```

A gamepad stick as a 2D axis.

```rust
input.bind_stick("move", GamepadAxis::LeftX, GamepadAxis::LeftY);
```

### Input.action_pressed

```rust
fn action_pressed(self, name: string) -> bool
```

Whether the action went down this frame.

```rust
fn jumped(input: Input) -> bool => input.action_pressed("jump");
```

### Input.action_held

```rust
fn action_held(self, name: string) -> bool
```

Whether the action is held.

```rust
emitter.playing = input.action_held("thrust");
```

### Input.action_released

```rust
fn action_released(self, name: string) -> bool
```

Whether the action went up this frame.

```rust
if input.action_released("jump") && v.linear.y < 0.0 { v.linear = vec2(v.linear.x, v.linear.y * 0.5); }
```

### Input.action_value

```rust
fn action_value(self, name: string) -> float
```

The action's analog value from 0 to 1, for triggers.

```rust
let throttle = input.action_value("accelerate");
```

### Input.axis2d

```rust
fn axis2d(self, name: string) -> Vec2
```

A 2D axis bound with `bind_axis2d` or `bind_stick`.

```rust
v.linear = input.axis2d("move") * 200.0;
```

### Input.mouse_wheel

```rust
fn mouse_wheel(self) -> float
```

The mouse wheel this frame: positive away from the player.

```rust
camera.zoom = clamp(camera.zoom + input.mouse_wheel() * 0.1, 0.5, 3.0);
```

### Input.gamepad_connected

```rust
fn gamepad_connected(self, pad: int) -> bool
```

Whether pad `pad` is connected.

```rust
if !input.gamepad_connected(0) { hint.value = "Plug in a gamepad or use the keyboard"; }
```

### Input.bind

```rust
fn bind(self, action: string, binding: string)
```

Adds a binding in text form, as drift.toml writes them: "key:Space", "mouse:Left", "button:South", "axis:+LeftX".

```rust
input.bind("jump", "key:W");
input.bind("throttle", "axis:+RightTrigger");
```

### Input.bind_axis

```rust
fn bind_axis(self, name: string, negative: Key, positive: Key)
```

A 1D axis from two keys, -1 to 1.

```rust
input.bind_axis("steer", Key::Left, Key::Right);
```

### Input.axis

```rust
fn axis(self, name: string) -> float
```

A 1D axis bound with `bind_axis`, from -1 to 1.

```rust
v.angular = input.axis("steer") * 3.0;
```

### Input.bindings

```rust
fn bindings(self, name: string, slot: int) -> string[]
```

Rebinding. Actions have slot 0; a 1D axis has 0 (negative) and 1 (positive); a 2D axis 0 to 3 for left, right, up and down.

```rust
let shown = input.bindings("jump", 0).join(", ");   // "key:Space, button:South"
```

### Input.rebind

```rust
fn rebind(self, name: string, slot: int, index: int, binding: string)
```

Replaces binding `index` of a slot, or adds one when `index` is past the end.

```rust
input.rebind("jump", 0, 0, "key:W");
```

### Input.capture_binding

```rust
fn capture_binding(self) -> string
```

The first key, button or stick that went down this frame, as text; "" when none did.

```rust
let pressed = input.capture_binding();
if pressed != "" {
    input.rebind(menu.waiting_for, 0, 0, pressed);
}
```

### Input.set_action_tuning

```rust
fn set_action_tuning(self, dead_zone: float, press_threshold: float, pad: int)
```

Stick dead zone, the analog value that counts as held, and the pad actions read (-1 for any).

```rust
input.set_action_tuning(0.2, 0.5, -1);
```

### Input.bindings_json

```rust
fn bindings_json(self) -> string
```

Every binding as JSON text, to keep in save data.

```rust
save.set_string("bindings", input.bindings_json());
save.flush();
```

### Input.load_bindings_json

```rust
fn load_bindings_json(self, text: string) -> int
```

Restores bindings saved with bindings_json() for names that exist; returns how many it changed.

```rust
input.load_bindings_json(save.get_string("bindings", ""));
```

### Input.send_key

```rust
fn send_key(self, k: Key, down: bool)
```

Input as if from the device; it arrives at the start of next frame.

```rust
input.send_key(Key::Right, true);   // a demo holds Right from next frame
```

### Input.send_mouse_move

```rust
fn send_mouse_move(self, pos: Vec2)
```

Moves the mouse, as if from the device, next frame.

```rust
input.send_mouse_move(vec2(100.0, 100.0));
```

### Input.send_mouse_button

```rust
fn send_mouse_button(self, pos: Vec2, b: MouseButton, down: bool)
```

Presses or releases a mouse button at a position, as if from the device, next frame.

```rust
input.send_mouse_button(vec2(100.0, 100.0), MouseButton::Left, true);
```

### Input.start_text_input

```rust
fn start_text_input(self)
```

Text typed this frame while text input is on, for name entry and chat. Starting it brings up the on-screen keyboard where there is one.

```rust
input.start_text_input();
```

### Input.stop_text_input

```rust
fn stop_text_input(self)
```

Turns text input off.

```rust
input.stop_text_input();
```

### Input.text

```rust
fn text(self) -> string
```

The text typed this frame while text input is on.

```rust
form.name = form.name + input.text();
```

### Input.touches

```rust
fn touches(self) -> Touch[]
```

Fingers on a touch screen. The first finger also drives the mouse.

```rust
for t in input.touches() {
    if t.pressed {
        effects.burst(t.position, ParticleEmitter { .. }, 10);
    }
}
```

## Touch

A finger on a touch screen, in logical screen coordinates.

```rust
struct Touch {
    id: int = 0,
    position: Vec2 = Vec2 { .. },
    start: Vec2 = Vec2 { .. },
    delta: Vec2 = Vec2 { .. },
    pressed: bool = false,
    released: bool = false,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `int` | `0` | Stays the same while the finger is down |
| `position` | `Vec2` | `Vec2 { .. }` | Where the finger is, in logical screen pixels |
| `start` | `Vec2` | `Vec2 { .. }` | Where it went down |
| `delta` | `Vec2` | `Vec2 { .. }` | How far it moved this frame |
| `pressed` | `bool` | `false` | It went down this frame |
| `released` | `bool` | `false` | It went up this frame; the touch is gone next frame |

```rust
for t in input.touches() {
    if t.released && (t.position - t.start).length() > 50.0 {
        log("swipe");
    }
}
```

## Window

The game's window, or the canvas on a web page.

```rust
struct Window {
    width: float = 0.0,
    height: float = 0.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `width` | `float` | `0.0` | Logical width in pixels |
| `height` | `float` | `0.0` | Logical height in pixels |

```rust
@system
fn center(title: (Title, Transform), window: Window) {
    let (_, t) = title;
    t.position = vec2(window.width * 0.5, 80.0);
}
```

### Window.quit

```rust
fn quit(self)
```

Ends the game after this frame. In the browser the page stops running the game.

```rust
if input.key_pressed(Key::Escape) { window.quit(); }
```

### Window.lock_pointer

```rust
fn lock_pointer(self, on: bool)
```

Hides the cursor and keeps it in the window; Input.mouse_delta() still reports movement. For mouse-look and aiming. In a browser it needs a tap, click or key first; asked for without one, it waits for the next.

### Window.pixel_ratio

```rust
fn pixel_ratio(self) -> float
```

Screen pixels per window unit: 2 on most phones, 1 on most monitors.

### Window.op

```rust
fn op(self, name: string, nums: float[], text: string) -> float[]
```

### Window.set_title

```rust
fn set_title(self, title: string)
```

### Window.set_clear_color

```rust
fn set_clear_color(self, c: Color)
```

### Window.set_scale_mode

```rust
fn set_scale_mode(self, m: ScaleMode)
```

### Window.set_logical_size

```rust
fn set_logical_size(self, w: int, h: int)
```

The logical resolution the game draws at.

### Window.size

```rust
fn size(self) -> Vec2
```

The window's own size in pixels; in the browser, the canvas.

### Window.set_size

```rust
fn set_size(self, w: int, h: int)
```

### Window.fullscreen

```rust
fn fullscreen(self) -> bool
```

### Window.set_fullscreen

```rust
fn set_fullscreen(self, on: bool)
```

Browsers allow fullscreen only right after a tap, click or key; asked for at another time, it waits for the next one.

### Window.show_cursor

```rust
fn show_cursor(self, visible: bool)
```

### Window.set_cursor

```rust
fn set_cursor(self, c: Cursor)
```

### Window.draw_calls

```rust
fn draw_calls(self) -> int
```

Draw calls and sprites in the last frame.

### Window.sprite_count

```rust
fn sprite_count(self) -> int
```

### Window.backend

```rust
fn backend(self) -> string
```

"gl" or "webgpu".

### Window.download

```rust
fn download(self, path: string)
```

Offers a file the game wrote, such as a screenshot, as a download in the browser. A screenshot asked for this frame is offered once it is written.

```rust
window.download("shot.png");
```

### Window.screenshot

```rust
fn screenshot(self, path: string)
```

Saves the next presented frame as a PNG.

```rust
if input.key_pressed(Key::F12) {
    window.screenshot("shot.png");
    window.download("shot.png");
}
```

## Assets

Makes textures, sounds, images and maps from data, and loads the kinds of file that `texture(path)` and `sound(path)` do not. Loading the same path again returns the same asset. See [Assets](../concepts/assets.md).

```rust
struct Assets {
    id: int = 0,
}
```

```rust
@system(schedule: Schedule::Startup)
fn load(cmd: Commands, assets: Assets) {
    let map = assets.tilemap("assets/level1.tmj");
    cmd.spawn([at(0.0, 0.0), Tilemap { map: map, .. }]);
}
```

### Assets.create_texture

```rust
fn create_texture(self, w: int, h: int, pixels: Color[]) -> Texture
```

A texture from colors, row by row from the top-left.

```rust
let pixels: Color[] = [];
for i in range(16 * 16) {
    pixels.push(rgb(255, 255, 255));
}
let white = assets.create_texture(16, 16, pixels);
```

### Assets.create_sound

```rust
fn create_sound(self, samples: float[], rate: int) -> Sound
```

A mono sound from samples between -1 and 1 at `rate` samples per second, for blips made in code.

```rust
let samples: float[] = [];
for i in range(2205) {
    samples.push(sin(float(i) * 0.12) * 0.3);
}
let beep = assets.create_sound(samples, 22050);
```

### Assets.tilemap

```rust
fn tilemap(self, path: string) -> Map
```

A Tiled map (.tmj, .json) or the first level of an LDtk project.

```rust
let map = assets.tilemap("assets/level1.tmj");
```

### Assets.ldtk_level

```rust
fn ldtk_level(self, path: string, level: string) -> Map
```

One level of an LDtk project, by its identifier.

```rust
let map = assets.ldtk_level("assets/world.ldtk", "Level_2");
```

### Assets.create_tilemap

```rust
fn create_tilemap(self, width: int, height: int, tile_size: int, tileset: Texture, layers: string[]) -> Map
```

An empty map of tile layers over one tileset image, for levels made in code. Tile ids count from 1 at the image's top-left, left to right.

```rust
let map = assets.create_tilemap(40, 25, 16, art.tiles, ["Ground", "Walls"]);
```

### Assets.render_target

```rust
fn render_target(self, w: int, h: int) -> Texture
```

A texture cameras can draw into and sprites can show.

```rust
let minimap = assets.render_target(256, 160);
cmd.spawn([at(0.0, 0.0), Camera { zoom: 0.2, order: -1, target: minimap, clear: true, .. }]);
cmd.spawn([at(1140.0, 100.0), Sprite { image: minimap, layer: hud_layer(), .. }]);
```

### Assets.create_image

```rust
fn create_image(self, w: int, h: int, fill: Color) -> Image
```

An image of `w` by `h` pixels filled with one color, to draw into from code.

```rust
let canvas = assets.create_image(64, 64, rgba(0, 0, 0, 0));
```

### Assets.image

```rust
fn image(self, path: string) -> Image
```

An image file's pixels, to change before making a texture of them.

```rust
let img = assets.image("assets/hero.png");
img.flip_x();
let mirrored = img.to_texture();
```

### Assets.spritesheet

```rust
fn spritesheet(self, t: Texture, frame_w: float, frame_h: float) -> Spritesheet
```

Cuts a texture into a grid of frames `frame_w` by `frame_h` pixels.

```rust
let sheet = assets.spritesheet(texture("assets/hero.png"), 16.0, 16.0);
cmd.spawn([at(100.0, 100.0), Sprite { image: sheet.texture, frame: sheet.frame(0, 0), .. }]);
```

### Assets.font

```rust
fn font(self, path: string, size: float) -> Font
```

A TrueType, OpenType or BMFont font. `size` is the size a Text with size 0 uses.

### Assets.shader

```rust
fn shader(self, path: string) -> Shader
```

`path` without an extension finds path.wgsl and path.frag.glsl.

### Assets.create_shader

```rust
fn create_shader(self, wgsl: string, glsl: string) -> Shader
```

A shader from source: the body of `fn fragment` in WGSL, the `main` of a GLSL fragment shader, or both. Empty for the one you do not have.

### Assets.create_material

```rust
fn create_material(self, s: Shader) -> Material
```

### Assets.aseprite

```rust
fn aseprite(self, path: string) -> Spritesheet
```

An Aseprite file (.ase, .aseprite): its frames side by side in one texture, its frame durations and its tags.

### Assets.scene

```rust
fn scene(self, path: string) -> Scene
```

A scene file, loaded once per path.

## Effects

Screen flashes and shakes, particle bursts and emitter control. See [Particles and Effects](../guides/effects.md).

```rust
struct Effects {
    id: int = 0,
}
```

```rust
@system
fn crash(_: Contact<Player, Rock>, effects: Effects) {
    effects.flash(rgb(255, 60, 60), 0.3);
}
```

### Effects.flash

```rust
fn flash(self, c: Color, seconds: float)
```

Covers the screen in a color that fades back to the picture over `seconds`.

```rust
effects.flash(rgb(255, 255, 255), 0.2);
```

### Effects.shake

```rust
fn shake(self, camera: Entity, intensity: float)
```

Shakes a camera entity; the shake dies away by itself.

```rust
effects.shake(camera, 8.0);
```

### Effects.burst

```rust
fn burst(self, pos: Vec2, emitter: ParticleEmitter, count: int)
```

A one-off burst of `count` particles that cleans itself up.

```rust
effects.burst(t.position, ParticleEmitter {
    speed_min: 80.0, speed_max: 220.0, lifetime_max: 0.6,
    color_start: rgb(255, 240, 200), color_end: rgba(255, 80, 0, 0), ..
}, 40);
```

### Effects.add_material

```rust
fn add_material(self, m: Material, order: int) -> Effect
```

A post effect running a material's shader over the picture, which arrives as its texture0.

### Effects.restart

```rust
fn restart(self, emitter: Entity)
```

Starts an emitter's run over: its bursts fire again and a finished one emits anew.

```rust
effects.restart(fountain);
```

### Effects.add

```rust
fn add(self, settings: any) -> Effect
```

A post effect from its settings: Bloom, ColorGrading, Blur, Pixelate, Grayscale, Sepia, Vignette or Crt. Each kind has its own place in the chain; add_at picks one.

```rust
let grade = effects.add(ColorGrading { saturation: 0.6, brightness: 0.9, .. });
```

### Effects.add_at

```rust
fn add_at(self, settings: any, order: int) -> Effect
```

The same, at a place of your choosing in the chain; effects run from the lowest order up.

```rust
effects.add_at(Vignette { intensity: 0.8, .. }, 100);
```

### Effects.bloom

```rust
fn bloom(self, threshold: float, intensity: float) -> Effect
```

Adds a `Bloom` with a threshold and an intensity.

```rust
let glow = effects.bloom(0.7, 1.2);
```

### Effects.vignette

```rust
fn vignette(self, intensity: float, radius: float) -> Effect
```

Adds a `Vignette`: darker edges.

```rust
effects.vignette(0.6, 0.75);
```

### Effects.crt

```rust
fn crt(self, curvature: float, scanlines: float) -> Effect
```

Adds a `Crt`: a curved screen with scanlines.

```rust
effects.crt(0.15, 0.35);
```

### Effects.grayscale

```rust
fn grayscale(self, amount: float) -> Effect
```

Adds a `Grayscale`: color to gray, 0 to 1.

```rust
let dead = effects.grayscale(1.0);
```

### Effects.sepia

```rust
fn sepia(self, amount: float) -> Effect
```

Adds a `Sepia`: color to brown, 0 to 1.

```rust
effects.sepia(0.8);
```

### Effects.pixelate

```rust
fn pixelate(self, size: float) -> Effect
```

Adds a `Pixelate`: blocks of `size` pixels.

```rust
effects.pixelate(4.0);
```

### Effects.blur

```rust
fn blur(self, radius: float, iterations: int) -> Effect
```

Adds a `Blur`, for behind a pause menu.

```rust
let soft = effects.blur(2.0, 2);
```

### Effects.fade_to

```rust
fn fade_to(self, c: Color, amount: float, seconds: float)
```

Moves the screen `amount` of the way to `c` over `seconds` and holds it there; amount 0 fades back.

```rust
effects.fade_to(rgb(0, 0, 0), 1.0, 0.5);   // to black over half a second
effects.fade_to(rgb(0, 0, 0), 0.0, 0.5);   // and back
```

### Effects.set_ambient

```rust
fn set_ambient(self, c: Color)
```

Light everywhere before any light adds to it, when lighting is on.

```rust
effects.set_ambient(rgb(40, 40, 70));   // night
```

### Effects.set_lighting

```rust
fn set_lighting(self, on: bool)
```

Switches lighting on and off while the game runs.

```rust
effects.set_lighting(false);
```

### Effects.set_tween_speed

```rust
fn set_tween_speed(self, s: float)
```

Multiplies time for every tween; 0 freezes them.

```rust
effects.set_tween_speed(0.0);   // freeze every tween
```

## Effect

A post effect running on the picture.

```rust
struct Effect {
    id: int = 0,
}
```

```rust
@resource
struct Looks { crt: Effect }

@system
fn toggle_crt(input: Input, looks: Looks) {
    if input.key_pressed(Key::F2) {
        looks.crt.set_enabled(!looks.crt.enabled());
    }
}
```

### Effect.set

```rust
fn set(self, settings: any)
```

New settings of the same kind as the effect was added with.

```rust
glow.set(Bloom { threshold: 0.5, intensity: 2.5, .. });
```

### Effect.set_enabled

```rust
fn set_enabled(self, on: bool)
```

Switches the effect on or off.

```rust
looks.crt.set_enabled(false);
```

### Effect.enabled

```rust
fn enabled(self) -> bool
```

Whether the effect is on.

```rust
if looks.crt.enabled() { log("crt on"); }
```

### Effect.set_order

```rust
fn set_order(self, order: int)
```

Effects run from the lowest order up.

```rust
glow.set_order(10);
```

### Effect.remove

```rust
fn remove(self)
```

Removes the effect from the chain.

```rust
looks.crt.remove();
```

## Bloom

Bright parts bleed light around them.

```rust
struct Bloom {
    threshold: float = 0.7,
    soft_knee: float = 0.5,
    intensity: float = 1.0,
    radius: float = 1.0,
    levels: int = 5,
    tint: Color = Color { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `threshold` | `float` | `0.7` | brightness where it starts, 0 to 1 |
| `soft_knee` | `float` | `0.5` | 0 cuts off hard, 1 eases in |
| `intensity` | `float` | `1.0` |  |
| `radius` | `float` | `1.0` |  |
| `levels` | `int` | `5` | 1 to 8; more spreads wider |
| `tint` | `Color` | `Color { .. }` |  |

```rust
effects.add(Bloom { threshold: 0.5, intensity: 1.4, levels: 6, tint: rgb(255, 230, 200), .. });
```

## ColorGrading

Brightness, contrast, saturation and tint, and an optional lookup table: a strip of N slices of N x N, red across a slice, green down it, blue picking the slice.

```rust
struct ColorGrading {
    brightness: float = 1.0,
    contrast: float = 1.0,
    saturation: float = 1.0,
    tint: Color = Color { .. },
    lut: Texture = Texture { .. },
    lut_strength: float = 1.0,
}
```

```rust
effects.add(ColorGrading { saturation: 0.6, contrast: 1.1, .. });
```

## Blur

```rust
struct Blur {
    radius: float = 1.0,
    iterations: int = 2,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `radius` | `float` | `1.0` | Spread in pixels |
| `iterations` | `int` | `2` | Passes; more is smoother |

```rust
effects.add(Blur { radius: 2.0, iterations: 3 });
```

## Pixelate

```rust
struct Pixelate {
    size: float = 6.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `size` | `float` | `6.0` | Block size in pixels |

```rust
effects.add(Pixelate { size: 6.0 });
```

## Grayscale

```rust
struct Grayscale {
    amount: float = 1.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `amount` | `float` | `1.0` | 0 leaves the colors, 1 is fully gray |

```rust
effects.add(Grayscale { amount: 1.0 });
```

## Sepia

```rust
struct Sepia {
    amount: float = 1.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `amount` | `float` | `1.0` | 0 leaves the colors, 1 is fully brown |

```rust
effects.add(Sepia { amount: 0.7 });
```

## Vignette

```rust
struct Vignette {
    intensity: float = 0.6,
    radius: float = 0.75,
    softness: float = 0.45,
    color: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: 1.0 },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `intensity` | `float` | `0.6` |  |
| `radius` | `float` | `0.75` | distance from the center where it is full |
| `softness` | `float` | `0.45` | it starts this far inside `radius` |
| `color` | `Color` | `Color { r: 0.0, g: 0.0, b: 0.0, a: 1.0 }` |  |

```rust
effects.add(Vignette { intensity: 0.8, color: rgb(40, 0, 0), .. });
```

## Crt

```rust
struct Crt {
    curvature: float = 0.15,
    scanlines: float = 0.35,
    scanline_spacing: float = 3.0,
    chromatic_aberration: float = 1.5,
    vignette: float = 0.3,
}
```

```rust
effects.add(Crt { curvature: 0.2, scanlines: 0.5, chromatic_aberration: 2.0, .. });
```

## Save

A key-value store kept between runs. On the desktop it is a JSON file in the user's data folder, named after `organization` and `name` in `[project]`; in the browser it lives in the page's storage. Values change in memory, and `flush()` writes them. See [Saving](../guides/saving.md).

```rust
struct Save {
    name: string = "",
}
```

```rust
@system(on_enter: Phase::Over)
fn keep_best(game: Game, save: Save) {
    if game.score > save.get_int("best", 0) {
        save.set_int("best", game.score);
        save.flush();
    }
}
```

### Save.get_int

```rust
fn get_int(self, key: string, fallback: int) -> int
```

The int under `key`, or `fallback` when there is none or it holds another type.

```rust
let best = save.get_int("best", 0);
```

### Save.set_int

```rust
fn set_int(self, key: string, v: int)
```

Stores an int under `key`.

```rust
save.set_int("level", 3);
```

### Save.get_float

```rust
fn get_float(self, key: string, fallback: float) -> float
```

The float under `key`, or `fallback`.

```rust
let volume = save.get_float("volume", 0.8);
```

### Save.set_float

```rust
fn set_float(self, key: string, v: float)
```

Stores a float under `key`.

```rust
save.set_float("best_time", 42.5);
```

### Save.get_string

```rust
fn get_string(self, key: string, fallback: string) -> string
```

The string under `key`, or `fallback`.

```rust
let name = save.get_string("name", "Player");
```

### Save.set_string

```rust
fn set_string(self, key: string, v: string)
```

Stores a string under `key`.

```rust
save.set_string("name", "Ada");
```

### Save.get_bool

```rust
fn get_bool(self, key: string, fallback: bool) -> bool
```

The bool under `key`, or `fallback`.

```rust
let music_on = save.get_bool("music", true);
```

### Save.set_bool

```rust
fn set_bool(self, key: string, v: bool)
```

Stores a bool under `key`.

```rust
save.set_bool("music", false);
```

### Save.has

```rust
fn has(self, key: string) -> bool
```

Whether a value is stored under `key`.

```rust
if !save.has("tutorial_done") { phase.set(Phase::Tutorial); }
```

### Save.erase

```rust
fn erase(self, key: string)
```

Removes the value under `key`.

```rust
save.erase("checkpoint");
```

### Save.clear

```rust
fn clear(self)
```

Removes every key.

```rust
save.clear();
save.flush();
```

### Save.flush

```rust
fn flush(self)
```

Writes the slot to disk.

```rust
save.set_int("coins", wallet.coins);
save.flush();
```

### Save.slot

```rust
fn slot(self, name: string) -> Save
```

Another slot in the same directory, such as "slot2".

```rust
let slot = save.slot("slot2");
slot.set_int("level", 4);
slot.flush();
```

### Save.get_value

```rust
fn get_value<T>(self, key: string, fallback: T) -> T
```

A value of any type stored as JSON under `key`, or `fallback`.

### Save.set_value

```rust
fn set_value(self, key: string, value: any)
```

## Camera2D

Converting between screen and world positions.

```rust
struct Camera2D {
    id: int = 0,
}
```

```rust
@system
fn hover(input: Input, cams: Camera2D, draw: Draw) {
    let p = cams.screen_to_world(input.mouse_position());
    draw.circle_lines(p, 10.0, rgb(255, 255, 255), 1.0);
}
```

### Camera2D.screen_to_world

```rust
fn screen_to_world(self, p: Vec2) -> Vec2
```

Through the main camera.

```rust
let world = cams.screen_to_world(vec2(0.0, 0.0));   // the world point at the top-left
```

### Camera2D.world_to_screen

```rust
fn world_to_screen(self, p: Vec2) -> Vec2
```

A world point to logical screen pixels, through the main camera.

```rust
let on_screen = cams.world_to_screen(boss_position);
```

### Camera2D.visible

```rust
fn visible(self) -> Rect
```

The world rect the main camera shows.

```rust
let view = cams.visible();
if !view.contains(t.position) { cmd.despawn(e); }
```

### Camera2D.screen_to_world_in

```rust
fn screen_to_world_in(self, camera: Entity, p: Vec2) -> Vec2
```

The same through a camera entity of your own.

```rust
let p = cams.screen_to_world_in(right_camera, input.mouse_position());
```

### Camera2D.world_to_screen_in

```rust
fn world_to_screen_in(self, camera: Entity, p: Vec2) -> Vec2
```

A world point to screen pixels through a camera entity of your own.

```rust
let p = cams.world_to_screen_in(minimap_camera, player_pos);
```

### Camera2D.visible_in

```rust
fn visible_in(self, camera: Entity) -> Rect
```

The world rect a camera entity of your own shows.

```rust
let shown = cams.visible_in(minimap_camera);
```

## Draw

For debugging, previews and anything with no entity of its own. See [UI and Score](../guides/ui-and-score.md#drawing-without-entities).

```rust
struct Draw {
    layer: int = 0,
    z: float = 1000.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `layer` | `int` | `0` | The layer the next draws go on |
| `z` | `float` | `1000.0` | The z the next draws use |

```rust
@system
fn show_aim(players: (Player, Transform)[], input: Input, draw: Draw) {
    for (_, t) in players {
        draw.line(t.position, input.mouse_world(), rgb(255, 80, 80), 2.0);
    }
    draw.hud();
    draw.text("aim", vec2(8.0, 8.0), 16.0, rgb(255, 255, 255));
}
```

### Draw.hud

```rust
fn hud(self)
```

Screen coordinates on the HUD layer, drawn after post effects.

```rust
draw.hud();
draw.rect(rect(10.0, 10.0, 100.0, 8.0), rgb(200, 40, 40));   // a health bar on the screen
```

### Draw.world

```rust
fn world(self)
```

World coordinates on layer 0.

```rust
draw.world();
```

### Draw.on_layer

```rust
fn on_layer(self, layer: int)
```

Draws the next shapes on this render layer.

```rust
draw.on_layer(2);
```

### Draw.at_z

```rust
fn at_z(self, z: float)
```

Draws the next shapes at this z.

```rust
draw.at_z(-1.0);   // under the entities of layer 0
```

### Draw.rect

```rust
fn rect(self, r: Rect, c: Color)
```

A filled rectangle.

```rust
draw.rect(rect(0.0, 0.0, 64.0, 32.0), rgba(0, 0, 0, 160));
```

### Draw.rect_lines

```rust
fn rect_lines(self, r: Rect, c: Color, thickness: float)
```

A rectangle's outline.

```rust
draw.rect_lines(rect(x, y, w, h), rgb(0, 255, 0), 1.0);
```

### Draw.rounded_rect

```rust
fn rounded_rect(self, r: Rect, radius: float, c: Color)
```

A filled rectangle with rounded corners.

```rust
draw.rounded_rect(rect(20.0, 20.0, 200.0, 60.0), 8.0, rgba(20, 20, 30, 220));
```

### Draw.circle

```rust
fn circle(self, center: Vec2, radius: float, c: Color)
```

A filled circle.

```rust
draw.circle(t.position, 4.0, rgb(255, 255, 0));
```

### Draw.circle_lines

```rust
fn circle_lines(self, center: Vec2, radius: float, c: Color, thickness: float)
```

A circle's outline.

```rust
draw.circle_lines(t.position, 48.0, rgba(255, 255, 255, 80), 1.0);
```

### Draw.arc

```rust
fn arc(self, center: Vec2, radius: float, start: float, end: float, c: Color, thickness: float)
```

From `start` to `end` radians, clockwise from +x.

```rust
draw.arc(t.position, 20.0, -PI / 2.0, -PI / 2.0 + TAU * reload.fraction(), rgb(255, 255, 255), 3.0);
```

### Draw.line

```rust
fn line(self, a: Vec2, b: Vec2, c: Color, thickness: float)
```

A line from `a` to `b`.

```rust
draw.line(a, b, rgb(255, 80, 80), 2.0);
```

### Draw.polyline

```rust
fn polyline(self, points: Vec2[], c: Color, thickness: float, closed: bool)
```

Lines through the points in order; `closed` joins the last to the first.

```rust
draw.polyline(path_points, rgb(80, 200, 255), 2.0, false);
```

### Draw.triangle

```rust
fn triangle(self, a: Vec2, b: Vec2, d: Vec2, c: Color)
```

A filled triangle.

```rust
draw.triangle(vec2(0.0, 0.0), vec2(20.0, 10.0), vec2(0.0, 20.0), rgb(255, 255, 255));
```

### Draw.polygon

```rust
fn polygon(self, points: Vec2[], c: Color)
```

A filled polygon, convex or not.

```rust
draw.polygon([vec2(0.0, 0.0), vec2(40.0, 0.0), vec2(50.0, 30.0), vec2(10.0, 40.0)], rgb(90, 160, 90));
```

### Draw.text

```rust
fn text(self, s: string, pos: Vec2, size: float, c: Color)
```

Text in the built-in font, its top-left at `pos`.

```rust
draw.text("fps " + string(int(1.0 / time.dt)), vec2(8.0, 8.0), 16.0, rgb(255, 255, 255));
```

### Draw.text_styled

```rust
fn text_styled(self, s: string, pos: Vec2, style: Text)
```

Text styled like a Text component: font, size, alignment, outline and the rest. Its `value`, `layer`, `z` and `visible` are not used.

```rust
draw.text_styled("Paused", vec2(480.0, 270.0), Text { size: 48.0, align: Align::Center, outline: 3.0, .. });
```

### Draw.texture

```rust
fn texture(self, t: Texture, pos: Vec2)
```

An image centered at `pos`.

```rust
draw.texture(art.cursor, input.mouse_world());
```

### Draw.sprite

```rust
fn sprite(self, s: Sprite, t: Transform)
```

A sprite as an entity would draw it, without the entity.

```rust
draw.sprite(Sprite { image: art.ghost, color: rgba(255, 255, 255, 100), .. }, at(x, y));
```

### Draw.nine_slice

```rust
fn nine_slice(self, t: Texture, dest: Rect, frame: Rect, border: Rect, c: Color)
```

An image cut in nine: the corners keep their size, the edges and middle stretch over `dest`. `border` holds the left, top, right and bottom margins of `frame` (zero size for the whole image) as x, y, w, h.

```rust
draw.hud();
draw.nine_slice(art.panel, rect(20.0, 300.0, 400.0, 120.0), rect(0.0, 0.0, 0.0, 0.0), rect(8.0, 8.0, 8.0, 8.0), rgb(255, 255, 255));
```

### Draw.push_clip

```rust
fn push_clip(self, r: Rect)
```

Draws after this only inside `r`, in screen pixels, until pop_clip().

```rust
draw.push_clip(rect(20.0, 20.0, 200.0, 100.0));
draw.text(long_story, vec2(20.0, 20.0 - scroll), 16.0, rgb(255, 255, 255));
draw.pop_clip();
```

### Draw.pop_clip

```rust
fn pop_clip(self)
```

Ends the clip `push_clip` started.

```rust
draw.pop_clip();
```

## Physics

Gravity, queries about colliders, and pushes on bodies. See [Collisions and Physics](../guides/collisions.md).

```rust
struct Physics {
    id: int = 0,
}
```

```rust
@system
fn line_of_sight(guard: (Guard, Transform), physics: Physics,
                 players: (Entity, Player, Transform)[]) {
    let (_, eye) = guard;
    for (player, _, t) in players {
        let to = t.position - eye.position;
        let ray = physics.raycast(eye.position, to.normalized(), to.length());
        if ray.hit && ray.entity == player {
            log("spotted");
        }
    }
}
```

### Physics.gravity

```rust
fn gravity(self) -> Vec2
```

World gravity in pixels per second squared. `[physics] gravity` in `drift.toml` sets where it starts.

```rust
let g = physics.gravity();
```

### Physics.set_gravity

```rust
fn set_gravity(self, g: Vec2)
```

Changes world gravity.

```rust
physics.set_gravity(vec2(0.0, -900.0));   // upside down
```

### Physics.raycast

```rust
fn raycast(self, origin: Vec2, direction: Vec2, distance: float) -> RayHit
```

The first collider along the ray. Sensors count.

```rust
let ray = physics.raycast(t.position, vec2(0.0, 1.0), 20.0);
let on_ground = ray.hit;
```

### Physics.raycast_mask

```rust
fn raycast_mask(self, origin: Vec2, direction: Vec2, distance: float, mask: int) -> RayHit
```

The same, seeing only colliders whose category is in `mask`.

```rust
let ray = physics.raycast_mask(eye, dir, 400.0, WALLS);   // WALLS: a category bit of your own
```

### Physics.overlap_point

```rust
fn overlap_point(self, p: Vec2) -> Entity[]
```

Entities whose colliders contain the point.

```rust
for e in physics.overlap_point(input.mouse_world()) {
    cmd.insert(e, Selected {});
}
```

### Physics.overlap_rect

```rust
fn overlap_rect(self, r: Rect) -> Entity[]
```

Entities whose colliders' bounding boxes touch the rect.

```rust
let inside = physics.overlap_rect(rect(x, y, 64.0, 64.0));
```

### Physics.overlap_circle

```rust
fn overlap_circle(self, center: Vec2, radius: float) -> Entity[]
```

Entities whose colliders touch the circle.

```rust
for e in physics.overlap_circle(blast, 80.0) {
    physics.apply_impulse(e, vec2(0.0, -300.0));
}
```

### Physics.overlap_circle_mask

```rust
fn overlap_circle_mask(self, center: Vec2, radius: float, mask: int) -> Entity[]
```

Entities whose colliders, of a category in `mask`, touch the circle.

```rust
let enemies_near = physics.overlap_circle_mask(t.position, 120.0, ENEMIES);
```

### Physics.apply_impulse

```rust
fn apply_impulse(self, e: Entity, impulse: Vec2)
```

Pushes on a dynamic body: an impulse changes its velocity at once (a jump, a hit), a force acts over this step (thrust, wind).

```rust
physics.apply_impulse(player, vec2(0.0, -350.0));   // a jump
```

### Physics.apply_force

```rust
fn apply_force(self, e: Entity, force: Vec2)
```

```rust
physics.apply_force(ship, from_angle(t.rotation) * 400.0);   // thrust while held
```

### Physics.apply_torque

```rust
fn apply_torque(self, e: Entity, torque: float)
```

Turns a dynamic body over this step.

```rust
physics.apply_torque(wheel, 50.0);
```

### Physics.apply_angular_impulse

```rust
fn apply_angular_impulse(self, e: Entity, impulse: float)
```

Changes a dynamic body's spin at once.

```rust
physics.apply_angular_impulse(crate_entity, 5.0);
```

### Physics.set_debug_draw

```rust
fn set_debug_draw(self, on: bool)
```

Draws every collider's outline over the game.

```rust
if input.key_pressed(Key::F3) { physics.set_debug_draw(true); }
```

### Physics.rebuild_shapes

```rust
fn rebuild_shapes(self, t: Texture, pixels: Image)
```

Makes every TextureCollider showing the texture again next frame, from `pixels`: call it after changing the image and writing it into the texture, such as carving a hole in terrain with Image.fill_circle and Texture.update. It traces the whole image, so do it when the pixels change, not every frame. With Image {} the texture is read back from the GPU instead, which costs more.

```rust
ground_image.fill_circle(hit, 40.0, rgba(0, 0, 0, 0));   // a crater
ground.update(ground_image);
physics.rebuild_shapes(ground, ground_image);
```

## texture_outlines

```rust
fn texture_outlines(t: Texture, frame: Rect, c: TextureCollider) -> Vec2[][]
```

The outlines a TextureCollider with settings `c` traces in `frame` of the texture (Rect {} for all of it), in pixels from the frame's top-left corner, to draw or check. Solid outlines wind clockwise on screen and holes the other way. Worked out once and kept, like the collider's.

```rust
for points in texture_outlines(rock, Rect { .. }, TextureCollider { .. }) {
    log(string(points.len()) + " points");
}
```

## texture_pieces

```rust
fn texture_pieces(t: Texture, frame: Rect, c: TextureCollider) -> Vec2[][]
```

The convex pieces, of at most 8 points, that cover those outlines: the shapes a dynamic body gets.

```rust
let pieces = texture_pieces(rock, Rect { .. }, TextureCollider { .. });
```

## RayHit

What a ray hit; `hit` false when it hit nothing.

```rust
struct RayHit {
    hit: bool = false,
    entity: Entity = Entity { .. },
    point: Vec2 = Vec2 { .. },
    normal: Vec2 = Vec2 { .. },
    fraction: float = 0.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `hit` | `bool` | `false` | Whether the ray hit anything |
| `entity` | `Entity` | `Entity { .. }` | The entity it hit |
| `point` | `Vec2` | `Vec2 { .. }` | Where, in the world |
| `normal` | `Vec2` | `Vec2 { .. }` | The surface direction at the hit |
| `fraction` | `float` | `0.0` | How far along the ray, 0 to 1 |

```rust
let ray = physics.raycast(origin, dir, 300.0);
if ray.hit {
    draw.circle(ray.point, 3.0, rgb(255, 0, 0));
}
```

## Random

The game's seed from drift.toml [random], and a generator per purpose.

```rust
struct Random {
    id: int = 0,
}
```

```rust
@system(schedule: Schedule::Startup)
fn setup(random: Random, level: Level) {
    log("seed " + string(random.seed()));
    level.rng = random.stream("levels");
}
```

### Random.seed

```rust
fn seed(self) -> int
```

The seed in use. With `seed = 0` in `[random]`, a new one is picked each run and logged.

```rust
hud.value = "Seed " + string(random.seed());
```

### Random.stream

```rust
fn stream(self, name: string) -> Rng
```

The same name gives the same generator for the same seed.

```rust
let loot = random.stream("loot");
let levels = random.stream("levels");   // independent of loot
```

## Audio

The mixer: one music track at a time on the music bus, playlists, and the buses every sound plays on. See [Sound and Music](../guides/sound.md).

```rust
struct Audio {
    id: int = 0,
}
```

```rust
@resource
struct Tracks { theme: Sound = music("assets/theme.ogg") }

@system(schedule: Schedule::Startup)
fn start_music(tracks: Tracks, audio: Audio) {
    audio.play_music(tracks.theme, 0.0, true);
}
```

### Audio.play_music

```rust
fn play_music(self, track: Sound, crossfade: float, looping: bool)
```

One music track at a time on the music bus; another crossfades over `crossfade` seconds.

```rust
audio.play_music(tracks.boss, 2.0, true);   // crossfade over two seconds
```

### Audio.play_playlist

```rust
fn play_playlist(self, tracks: Sound[], crossfade: float, looping: bool, shuffle: bool)
```

Tracks one after another, `crossfade` seconds apart (0 is gapless).

```rust
audio.play_playlist([tracks.a, tracks.b, tracks.c], 0.0, true, true);
```

### Audio.next_track

```rust
fn next_track(self)
```

Skips to the next track of the playlist.

```rust
audio.next_track();
```

### Audio.stop_music

```rust
fn stop_music(self, fade: float)
```

Stops the music, fading out over `fade` seconds.

```rust
audio.stop_music(1.0);
```

### Audio.pause_music

```rust
fn pause_music(self, fade: float)
```

Pauses the music, fading out over `fade` seconds.

```rust
audio.pause_music(0.3);
```

### Audio.resume_music

```rust
fn resume_music(self, fade: float)
```

Resumes paused music, fading in over `fade` seconds.

```rust
audio.resume_music(0.3);
```

### Audio.music_playing

```rust
fn music_playing(self) -> bool
```

Whether music is playing.

```rust
if !audio.music_playing() { audio.play_music(tracks.theme, 0.0, true); }
```

### Audio.master_bus

```rust
fn master_bus(self) -> Bus
```

The bus every other bus sits under.

```rust
audio.master_bus().set_volume(0.5, 0.0);
```

### Audio.music_bus

```rust
fn music_bus(self) -> Bus
```

The bus music plays on.

```rust
audio.music_bus().set_volume(0.3, 0.5);   // duck the music under a dialog
```

### Audio.sfx_bus

```rust
fn sfx_bus(self) -> Bus
```

The bus sounds play on unless told otherwise.

```rust
audio.sfx_bus().set_paused(true);   // pause the game's sounds, keep the music
```

### Audio.bus

```rust
fn bus(self, name: string, parent: Bus) -> Bus
```

The bus of that name under `parent` (Bus {} for master), made the first time it is asked for.

```rust
let voice = audio.bus("voice", Bus { .. });
sfx.line.play_with(PlayOptions { bus: voice, .. });
```

### Audio.has_device

```rust
fn has_device(self) -> bool
```

false when no output device opened; sounds then advance silently.

```rust
if !audio.has_device() { log("no sound"); }
```

## Systems

Turns systems on and off by function name, from the next system on: a paused game turns off its gameplay systems and keeps its menu running. A name that is not one of the game's systems raises drift_unknown_system, which lists the systems there are.

```rust
struct Systems {
    id: int = 0,
}
```

```rust
@system(on_enter: Phase::Paused)
fn freeze(systems: Systems) {
    systems.disable("spawn_enemies");
}
```

### Systems.enable

```rust
fn enable(self, system_name: string)
```

Turns a system on by its function name. A name that is not one of the game's systems raises `drift_unknown_system`, which lists them.

```rust
systems.enable("spawn_enemies");
```

### Systems.disable

```rust
fn disable(self, system_name: string)
```

Turns a system off by its function name. A name that is not one of the game's systems raises `drift_unknown_system`.

```rust
systems.disable("spawn_enemies");
```

### Systems.enabled

```rust
fn enabled(self, system_name: string) -> bool
```

Whether a system is on. A name that is not one of the game's systems raises `drift_unknown_system`.

```rust
if !systems.enabled("autosave") { log("autosave is off"); }
```

## ResourceField

One field of a resource, for widgets and tools that read it every frame: the `drift/ui` bindings and bars, and the debug overlay. Make one with `resource_field<T>(path)`; the compiler checks the type, and the field name is checked when it is made.

```rust
struct ResourceField {
    resource: string = "",
    field: string = "",
}
```

```rust
@resource
struct Game { score: int = 0, player: PlayerStats = PlayerStats { .. } }

let score = resource_field<Game>("score");
let hp = resource_field<Game>("player.hp");    // a field of a struct field
log(score.label() + " is " + score.text());   // "Game.score is 120"
```

### ResourceField.is_set

```rust
fn is_set(self) -> bool
```

Whether it names a field; `ResourceField {}` names none.

### ResourceField.check

```rust
fn check(self)
```

Raises drift_not_a_resource or drift_no_such_field unless it names a field of a resource.

### ResourceField.label

```rust
fn label(self) -> string
```

"Game.score".

### ResourceField.text

```rust
fn text(self) -> string
```

The field's value as text: numbers and bools as written, strings as they are, and `""` while the resource does not exist.

### ResourceField.number

```rust
fn number(self) -> float
```

The field's value as a number: `true` is 1.0, and anything that is not a number is 0.0.

## resource_field

```rust
fn resource_field<T>(path: string) -> ResourceField
```

The field `path` of the resource `T`. `T` must be a struct marked `@resource` or `@settings`, or this raises `drift_not_a_resource`; the path must name a field of it, or this raises `drift_no_such_field`, which lists the fields the type has. Made in a resource's field default, it is checked when the game loads.

```rust
spawn_bar(cmd, theme, anchor(TOP_LEFT, 16.0, 48.0), vec2(220.0, 18.0),
          Bar { value_from: resource_field<Game>("hp"), max_from: resource_field<Game>("max_hp"), .. });
```

## ScaleMode

```rust
enum ScaleMode { Letterbox, Stretch, Integer, Expand }
```

## Cursor

```rust
enum Cursor { Default, Pointer, Text, Crosshair, Move, Wait, NotAllowed }
```
