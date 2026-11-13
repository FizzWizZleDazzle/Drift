# Engine Resources

The engine's resources reach a system as parameters, like your own. Their fields are this frame's values; their methods act on the engine at once. The engine runs a system that takes one of these on its own, apart from engine work that uses the same resource.

## Time

| Field | Meaning |
|-------|---------|
| `dt` | Seconds since the last frame, times `scale` |
| `elapsed` | Seconds since the game started |
| `frame` | Frames so far |
| `scale` | How fast game time runs: 1 normal, 0.5 half speed, 0 frozen |
| `real_dt` | Seconds since the last frame whatever the scale, for menus and tools that run while the game is frozen |

`time.set_scale(s)` changes the speed from the next frame on. Physics, tweens, particles and every system's `dt` follow it: `set_scale(0.0)` pauses the game, a short `set_scale(0.05)` is a hit stop, and `set_scale(0.3)` is slow motion.

## Input

Device state and [input actions](../guides/input.md#actions).

| Method | Returns |
|--------|---------|
| `key_down(k)`, `key_pressed(k)`, `key_released(k)` | Held now, went down this frame, went up this frame. A tap shorter than a frame is both pressed and released that frame |
| `mouse_position()`, `mouse_world()` | Logical screen position; world position through the main camera |
| `mouse_delta()`, `mouse_wheel()` | Movement and wheel this frame |
| `mouse_down(b)`, `mouse_pressed(b)`, `mouse_released(b)` | Mouse buttons |
| `gamepad_connected(pad)`, `gamepad_axis(pad, axis)` | Pads 0 to 3; sticks read -1 to 1 (Y down), triggers 0 to 1 |
| `gamepad_down(pad, b)`, `gamepad_pressed(pad, b)`, `gamepad_released(pad, b)` | Gamepad buttons, Xbox layout: `South` is A |
| `bind_key(action, k)`, `bind_mouse(action, b)`, `bind_button(action, b)` | Add a binding to an action |
| `bind(action, text)` | Add a binding in text form, as `drift.toml` writes them: `"key:Space"`, `"mouse:Left"`, `"button:South"`, `"axis:+LeftX"` |
| `bind_axis(name, negative, positive)` | A 1D axis from two keys, -1 to 1 |
| `bind_axis2d(name, left, right, up, down)` | A 2D axis from four keys; up is -Y. Calling it again adds another set |
| `bind_stick(name, x_axis, y_axis)` | A gamepad stick as a 2D axis |
| `action_pressed(name)`, `action_held(name)`, `action_released(name)`, `action_value(name)` | Action state |
| `axis(name)`, `axis2d(name)` | Axis values |
| `bindings(name, slot)` | The bindings in a slot as text, such as `"key:Space"` or `"button:South"` |
| `rebind(name, slot, index, text)`, `capture_binding()` | Rebinding menus; see the guide |
| `set_action_tuning(dead_zone, press_threshold, pad)` | Stick dead zone, the analog value that counts as held, and the gamepad actions read (-1 for any) |
| `bindings_json()`, `load_bindings_json(text)` | Save and restore every binding |
| `send_key(k, down)`, `send_mouse_move(pos)`, `send_mouse_button(pos, b, down)` | Queue input as if from the device, for tests and demo playback; it arrives next frame |
| `start_text_input()`, `stop_text_input()`, `text()` | Typed text, for name entry and chat: `text()` is what was typed this frame. Starting text input brings up the on-screen keyboard on phones |
| `touches()` | `Touch[]`: the fingers on a touch screen, each with `id`, `position`, `start`, `delta`, `pressed` and `released` |

## Window

| Member | Meaning |
|--------|---------|
| `width`, `height` | The logical resolution this frame |
| `set_title(s)`, `set_clear_color(c)`, `set_scale_mode(m)`, `set_logical_size(w, h)` | Change the window while running; `drift.toml` sets where they start. In the browser the title is the page's |
| `size()`, `set_size(w, h)` | The window's own size in pixels; in the browser, the canvas |
| `fullscreen()`, `set_fullscreen(on)` | Fullscreen. Browsers allow it only from a key press or click, so call it from the system that reads one |
| `show_cursor(visible)`, `set_cursor(c)` | Hide the mouse cursor, or pick a `Cursor`: `Default`, `Pointer`, `Text`, `Crosshair`, `Move`, `Wait`, `NotAllowed` |
| `draw_calls()`, `sprite_count()`, `backend()` | Last frame's draw calls and sprites, and `"gl"` or `"webgpu"` |
| `quit()` | Ends the game after this frame |
| `screenshot(path)` | Saves the next presented frame as a PNG |
| `download(path)` | Offers a file the game wrote as a download in the browser, such as a screenshot (once it is written) |

```rust
if input.key_pressed(Key::F12) {
    window.screenshot("shot.png");
    window.download("shot.png");
}
if input.key_pressed(Key::F) {
    window.set_fullscreen(!window.fullscreen());
}
```

## Assets

Images and sounds load with the free functions `texture(path)`, `sound(path)` and `music(path)` (see [Values](values.md)), which work anywhere, field defaults included. `Assets` makes textures and sounds from data and loads everything else. Loading the same path again returns the same asset. A file that does not load raises `drift_load_failed`.

| Method | Returns |
|--------|---------|
| `create_texture(w, h, pixels: Color[])` | A `Texture` from colors, row by row from the top-left |
| `render_target(w, h)` | A `Texture` a `Camera` can draw into |
| `create_image(w, h, fill)`, `image(path)` | An `Image` to change from code: blank, or an image file's pixels. See [Image](#image) |
| `spritesheet(texture, frame_w, frame_h)`, `aseprite(path)` | A `Spritesheet`: `frame(col, row)`, `frame_at(i)`, `frames(first, count)`, `frame_count()`, `tag(name)` (the frames of an Aseprite tag), `texture` |
| `create_sound(samples: float[], rate)` | A mono `Sound` from samples in -1 to 1 |
| `font(path, size)` | A `Font`: TrueType, OpenType or BMFont. `Font {}` is the built-in font. `measure(s, size)` gives the size of a string, `line_height(size)` the distance between lines, `set_fallback(other)` where missing characters come from |
| `shader(path)`, `create_shader(wgsl, glsl)` | A `Shader`: from `path.wgsl` and `path.frag.glsl` beside each other, or from source. The browser runs the GLSL; see [Shaders](../guides/rendering.md#shaders) |
| `create_material(shader)` | A `Material` for `Sprite.material` or `Effects.add_material`: `set_param(i, x, y, z, w)` and `set_color(i, c)` set `material.params[i]` (0 to 7), `set_texture(slot, t)` sets extra textures 1 to 3 |
| `tilemap(path)`, `ldtk_level(path, level)` | A `Map` from Tiled or LDtk |
| `create_tilemap(width, height, tile_size, tileset, layers: string[])` | An empty `Map` of tile layers over one tileset image, for levels made in code |
| `scene(path)` | A `Scene` from a scene or prefab file |

`Texture` has `size()`, `set_filter(Filter)`, `update(image)` and `update_rect(image, r)` (write an image's pixels into it, cheap enough for every frame) and `to_image()` (a render target's pixels as its last frame left them); `Sound` has `play()` and `play_with(PlayOptions)`, which return a `Voice`, `duration()` and `set_max_instances(n)`.

### Image

Pixels in memory that a game changes from code and shows through a texture: generated art, paint programs, fog of war, minimaps. The engine holds the pixels; positions outside the image read as transparent and writes there are skipped. Unlike other assets, `assets.image(path)` reads the file again on every call, since each image is yours to change.

| Method | Does |
|--------|------|
| `width()`, `height()`, `get(x, y)`, `set(x, y, c)` | Size and single pixels |
| `fill(c)`, `fill_rect(r, c)`, `gradient(a, b, vertical)`, `noise(n: Noise, low, high)` | Fill areas; `noise` colors each pixel from `low` (noise -1) to `high` (1) |
| `line(a, b, c, thickness)`, `rect_lines(r, c, thickness)`, `circle(center, radius, c)`, `circle_lines(center, radius, c, thickness)` | Shapes, blended by alpha |
| `draw(src, x, y)`, `draw_part(src, frame, x, y)`, `paste(src, x, y)` | Another image on top, blended; `paste` copies its pixels as they are |
| `copy()`, `crop(r)`, `flip_x()`, `flip_y()` | New images, and mirroring in place |
| `pixels()`, `set_pixels(px)`, `read_rect(r)`, `write_rect(r, px)` | Many pixels at once as `int` 0xRRGGBBAA; `pixel(c)` and `color_of(px)` convert |
| `to_texture()` | A new texture of the pixels as they are now |
| `save_png(path)`, `download(path)` | Save; `download` also offers the file in the browser. A failed write raises `drift_save_failed` |
| `free()` | Lets the pixels go |

```rust
@resource
struct Canvas { image: Image = Image { .. }, texture: Texture = Texture { .. } }

@system(schedule: Schedule::Startup)
fn make(cmd: Commands, assets: Assets, canvas: Canvas) {
    canvas.image = assets.create_image(256, 256, rgb(20, 20, 30));
    canvas.image.noise(Noise { seed: 3, frequency: 0.04, octaves: 4, .. }, rgb(10, 30, 10), rgb(120, 200, 90));
    canvas.texture = canvas.image.to_texture();
    cmd.spawn([at(320.0, 240.0), Sprite { image: canvas.texture, .. }]);
}

@system
fn paint(input: Input, cam: Camera2D, canvas: Canvas) {
    if input.mouse_down(MouseButton::Left) {
        // The sprite is centered at (320, 240), so its top-left is (192, 112).
        let p = cam.screen_to_world(input.mouse_position()) - vec2(192.0, 112.0);
        canvas.image.circle(p, 6.0, rgb(255, 220, 80));
        canvas.texture.update_rect(canvas.image, rect(p.x - 7.0, p.y - 7.0, 14.0, 14.0));
    }
}
```

To draw engine shapes and sprites into a texture instead, point a camera at a render target and give it a layer of its own: the camera draws what is on that layer, `Draw.on_layer` adds immediate shapes there, and `to_image()` reads the result back.


### Map

A loaded Tiled map or LDtk level, or a map made with `create_tilemap`. Layers are numbered from 0 at the bottom; cells are `Cell { x, y }`.

| Method | Returns |
|--------|---------|
| `size()`, `tile_size()` | Size in cells, size of one cell in pixels, as `Cell` |
| `bounds()` | The map's world `Rect` where its `Tilemap` entity places it |
| `layer_count()`, `layer(name)`, `layer_name(i)` | Layers. `layer` raises `drift_no_such_layer` |
| `set_layer_visible(layer, v)`, `set_layer_solid(layer, solid, values: int[])` | Show or hide a layer; make it build colliders (`values` empty for any tile or IntGrid value) |
| `tile(layer, cell)`, `set_tile(layer, cell, id)` | Tile ids, 0 for empty |
| `int_grid(layer, cell)`, `set_int_grid(layer, cell, value)` | LDtk IntGrid values |
| `tile_at(layer, world_pos)` | The tile id under a world position |
| `world_to_tile(pos, layer)`, `tile_to_world(cell, layer)` | Coordinates; `tile_to_world` gives the cell's top-left |
| `fill(layer, grid, tiles: int[], at)`, `to_grid(layer)` | Write a [grid](../guides/procedural-generation.md#grids) into a tile layer through a tile id per value; read a layer back as a grid |
| `destroy()` | Free the map once no `Tilemap` entity shows it |
| `objects(kind)` | `MapObject[]` of a Tiled class or LDtk entity identifier; `""` for all |
| `property(name)`, `object_property(object, name)`, `tile_property(tile, name)` | Map or level properties, object properties and tileset tile properties, as strings; `""` when missing |
| `autotile(source, target, rules)` | Writes the `target` layer's tiles from the terrain in `source`, by which neighbours are terrain too; see [Tilemaps](../guides/tilemaps.md#autotiling) |

A `MapObject` has `name`, `kind`, `id` (the Tiled object id), `shape` (`ObjectShape`: `Rect`, `Point`, `Ellipse`, `Polygon`, `Polyline`, `Tile`, `Text`), `bounds` (world `Rect`), `anchor` (world position of its pivot), `rotation`, `tile` (for tile objects) and `points` (world positions of a polygon or polyline), plus the `layer` and `index` that identify it in its map.

### Scene

A loaded scene or prefab file. Spawn it through `Commands`:

| Command | Does |
|---------|------|
| `cmd.spawn_scene(scene, options)` | Spawns a copy and returns a `SceneInstance`. `SceneOptions` fields: `offset` (added to each root's position), `parent` (roots become its children), `overrides` (`{string: string}`: entity `Name` to a JSON patch of its components) |
| `cmd.save_scene(path)` | Writes every entity with a saved component to a scene file, after this system returns |
| `cmd.save_prefab(path, root)` | Writes `root` and its children |

A `SceneInstance` has `entities` and `roots` (`Entity[]`, in file order) and `find(name)`, which returns `Some(entity)` for the first with that `Name`. The entities exist for the systems that run after this one.

## Random

The game's seed from `drift.toml` `[random]`; see [Procedural Generation](../guides/procedural-generation.md#the-games-seed).

| Method | Returns |
|--------|---------|
| `seed()` | The seed in use; with `seed = 0` a new one is picked each run and logged |
| `stream(name)` | An `Rng` for one purpose: the same for the same seed and name, independent of other names |

## Audio

| Method | Does |
|--------|------|
| `play_music(sound, crossfade, looping)`, `stop_music(fade)`, `pause_music(fade)`, `resume_music(fade)`, `music_playing()` | One music track at a time |
| `play_playlist(tracks, crossfade, looping, shuffle)`, `next_track()` | Tracks one after another; `crossfade` 0 is gapless |
| `master_bus()`, `music_bus()`, `sfx_bus()`, `bus(name, parent)` | Mixer buses; `drift.toml [audio]` sets the built-in ones' starting volumes |
| `has_device()` | `false` when no output device opened; sounds still advance silently |

A `Bus` has `set_volume(v, fade)`, `volume()`, `set_muted(m)`, `muted()`, `set_paused(p)`, `paused()` and `stop(fade)`. A `Voice` from `Sound.play` has `stop(fade)`, `pause(fade)`, `resume(fade)`, `set_volume(v, fade)`, `set_pan(p)`, `set_pitch(p)`, `set_looping(b)`, `playing()` and `paused()`; after the sound ends its methods do nothing.

`PlayOptions` fields: `volume` (1), `pitch` (1; 2 is an octave up), `looping`, `pan` (-1 left to 1 right), `bus` (`Bus {}` for sfx), `fade_in` (seconds) and `priority`.

## Physics

| Method | Does |
|--------|------|
| `gravity()`, `set_gravity(g)` | World gravity in pixels per second squared; `drift.toml [physics]` sets where it starts |
| `raycast(origin, direction, distance)` | A `RayHit`: `hit`, `entity`, `point`, `normal`, `fraction`. Sensors count |
| `raycast_mask(origin, direction, distance, mask)` | The same, seeing only colliders whose `category` is in `mask` |
| `overlap_point(p)`, `overlap_circle(center, radius)` | `Entity[]` whose colliders contain the point or touch the circle |
| `overlap_circle_mask(center, radius, mask)` | The same for the categories in `mask` |
| `overlap_rect(r)` | `Entity[]` whose colliders' bounding boxes touch the rect |
| `apply_impulse(e, i)`, `apply_force(e, f)`, `apply_torque(e, t)`, `apply_angular_impulse(e, i)` | On dynamic bodies |
| `set_debug_draw(on)` | Outlines every collider over the game |

## Effects

| Method | Does |
|--------|------|
| `burst(pos, emitter, count)` | A one-off particle burst that cleans itself up |
| `restart(emitter)` | Starts an emitter entity's run over: its bursts fire again and a finished one emits anew |
| `shake(camera, intensity)` | Screen shake on a camera entity, dying away |
| `flash(color, seconds)`, `fade_to(color, amount, seconds)` | Screen flashes and fades |
| `add(settings)`, `add_at(settings, order)` | A post effect from its settings struct: `Bloom`, `ColorGrading`, `Blur`, `Pixelate`, `Grayscale`, `Sepia`, `Vignette` or `Crt` (see [Values](values.md#post-effect-settings)). Each kind has its own place in the chain; `add_at` picks one |
| `bloom(threshold, intensity)`, `vignette(intensity, radius)`, `crt(curvature, scanlines)`, `grayscale(amount)`, `sepia(amount)`, `pixelate(size)`, `blur(radius, iterations)` | The same with the most used settings and the rest at their defaults |
| `set_ambient(color)`, `set_lighting(on)` | Light everywhere before any light adds to it, and lighting on or off, when drift.toml has a `[lighting]` section |
| `add_material(material, order)` | A post effect of your own: the material's shader runs over the whole picture, which arrives as its `texture0` |
| `set_tween_speed(s)` | Multiplies time for every tween; 0 freezes them |

Every post effect call returns an `Effect`: `set(settings)` changes it (settings of the kind it was added with), `set_enabled(on)` and `enabled()` switch it, `set_order(order)` moves it in the chain, and `remove()` deletes it.

```rust
@resource
struct Looks { bloom: Effect = Effect { .. } }

@system(schedule: Schedule::Startup)
fn looks(effects: Effects, looks: Looks) {
    looks.bloom = effects.add(Bloom { threshold: 0.6, intensity: 1.2, levels: 6, .. });
    effects.add(ColorGrading { saturation: 1.2, contrast: 1.1, tint: rgb(255, 240, 220), .. });
}

@system
fn brighten(input: Input, looks: Looks) {
    if input.key_pressed(Key::B) {
        looks.bloom.set(Bloom { threshold: 0.4, intensity: 2.5, .. });
    }
}
```

## Camera2D

| Method | Returns |
|--------|---------|
| `screen_to_world(p)`, `world_to_screen(p)` | Through the main camera |
| `visible()` | The world rect the main camera shows |
| `screen_to_world_in(camera, p)`, `world_to_screen_in(camera, p)`, `visible_in(camera)` | The same through a camera entity, such as one half of a split screen |

```rust
@system
fn crosshair(input: Input, cam: Camera2D, marks: (Crosshair, Transform)[]) {
    for (_, t) in marks {
        t.position = cam.screen_to_world(input.mouse_position());
    }
}
```

## Save

The save slot `<user data>/<organization>/<name>/save.json`, named after `[project]` in `drift.toml`. A failed read or write raises `drift_save_failed`.

| Method | Does |
|--------|------|
| `get_int(key, fallback)`, `get_float`, `get_string`, `get_bool` | Read a value |
| `set_int(key, v)`, `set_float`, `set_string`, `set_bool` | Write a value |
| `has(key)`, `erase(key)`, `clear()` | |
| `flush()` | Write the slot to disk |
| `slot(name)` | Another slot in the same directory |

## Draw

Shapes, text and images for this frame only, for debugging and for overlays that change every frame. They draw in world coordinates on layer 0 at z 1000, over entities, until the system switches with `hud()`, `on_layer(l)` or `at_z(z)`; the next system starts from the defaults again.

| Method | Draws |
|--------|-------|
| `rect(r, c)`, `rect_lines(r, c, thickness)`, `rounded_rect(r, radius, c)` | Rectangles |
| `circle(center, radius, c)`, `circle_lines(center, radius, c, thickness)` | Circles |
| `arc(center, radius, start, end, c, thickness)` | Part of a circle, angles in radians from +x |
| `line(a, b, c, thickness)`, `polyline(points, c, thickness, closed)` | Lines |
| `triangle(a, b, c3, c)`, `polygon(points, c)` | Filled shapes; polygons may be concave |
| `text(s, pos, size, c)` | Built-in font, top-left at `pos` |
| `text_styled(s, pos, style: Text)` | Text styled by a `Text` value: font, size, alignment, wrap, outline, shadow, rich text |
| `texture(t, pos)`, `sprite(s: Sprite, t: Transform)` | An image centered at `pos`; a sprite as an entity would draw it |
| `nine_slice(t, dest, frame, border, c)` | An image cut in nine; `border` holds the left, top, right and bottom margins as `x`, `y`, `w`, `h` |
| `push_clip(r)`, `pop_clip()` | Draws in between show only inside `r`, in screen pixels |
| `hud()`, `world()`, `on_layer(l)`, `at_z(z)` | Where the draws that follow go |

```rust
@system
fn show_hitboxes(boxes: (BoxCollider, Transform)[], draw: Draw) {
    for (b, t) in boxes {
        let p = t.position - b.half_size;
        draw.rect_lines(rect(p.x, p.y, b.half_size.x * 2.0, b.half_size.y * 2.0), rgb(255, 80, 80), 1.0);
    }
    draw.hud();
    draw.text("boxes: " + string(boxes.len()), vec2(8.0, 8.0), 16.0, rgb(255, 255, 255));
}
```

## Systems

| Method | Does |
|--------|------|
| `enable(name)`, `disable(name)`, `enabled(name)` | Turn a system on or off by its function's name, such as `systems.disable("spawn_enemies")`; systems that run after this one see the change. A system that raised an error reads as disabled |
