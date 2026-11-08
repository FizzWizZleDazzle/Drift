# Engine Resources

The engine's resources reach a system as parameters, like your own. Their fields are this frame's values; their methods act on the engine at once. The engine runs a system that takes one of these on its own, apart from engine work that uses the same resource.

## Time

| Field | Meaning |
|-------|---------|
| `dt` | Seconds since the last frame |
| `elapsed` | Seconds since the game started |
| `frame` | Frames so far |

## Input

Device state and [input actions](../guides/input.md#actions).

| Method | Returns |
|--------|---------|
| `key_down(k)`, `key_pressed(k)`, `key_released(k)` | Held now, went down this frame, went up this frame |
| `mouse_position()`, `mouse_world()` | Logical screen position; world position through the main camera |
| `mouse_delta()`, `mouse_wheel()` | Movement and wheel this frame |
| `mouse_down(b)`, `mouse_pressed(b)`, `mouse_released(b)` | Mouse buttons |
| `gamepad_connected(pad)`, `gamepad_axis(pad, axis)` | Pads 0 to 3; sticks read -1 to 1 (Y down), triggers 0 to 1 |
| `gamepad_down(pad, b)`, `gamepad_pressed(pad, b)`, `gamepad_released(pad, b)` | Gamepad buttons, Xbox layout: `South` is A |
| `bind_key(action, k)`, `bind_mouse(action, b)`, `bind_button(action, b)` | Add a binding to an action |
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

## Window

| Member | Meaning |
|--------|---------|
| `width`, `height` | The logical resolution this frame |
| `set_title(s)`, `set_clear_color(c)`, `set_scale_mode(m)`, `set_logical_size(w, h)` | Change the window while running; `drift.toml` sets where they start |
| `quit()` | Ends the game after this frame |
| `screenshot(path)` | Saves the next presented frame as a PNG |

## Assets

Images and sounds load with the free functions `texture(path)` and `sound(path)` (see [Values](values.md)), which work anywhere, field defaults included. `Assets` makes textures and sounds from data and loads everything else. Loading the same path again returns the same asset. A file that does not load raises `drift_load_failed`.

| Method | Returns |
|--------|---------|
| `create_texture(w, h, pixels: Color[])` | A `Texture` from colors, row by row from the top-left |
| `render_target(w, h)` | A `Texture` a `Camera` can draw into |
| `spritesheet(texture, frame_w, frame_h)`, `aseprite(path)` | A `Spritesheet`: `frame(col, row)`, `frame_at(i)`, `frames(first, count)`, `frame_count()`, `tag(name)` (the frames of an Aseprite tag), `texture` |
| `music(path)` | A `Sound` that decodes while it plays, for long tracks |
| `create_sound(samples: float[], rate)` | A mono `Sound` from samples in -1 to 1 |
| `font(path, size)` | A `Font`: TrueType, OpenType or BMFont. `Font {}` is the built-in font |
| `tilemap(path)`, `ldtk_level(path, level)` | A `Map` from Tiled or LDtk |
| `scene(path)` | A `Scene` from a scene or prefab file |

`Texture` has `size()` and `set_filter(Filter)`; `Sound` has `play()`, `play_with(PlayOptions)` and `duration()`.

### Map

A loaded Tiled map or LDtk level. Layers are numbered from 0 at the bottom; cells are `Cell { x, y }`.

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
| `objects(kind)` | `MapObject[]` of a Tiled class or LDtk entity identifier; `""` for all |
| `property(name)`, `object_property(object, name)` | Map or level properties and object properties, as strings; `""` when missing |

A `MapObject` has `name`, `kind`, `bounds` (world `Rect`) and `anchor` (world position of its pivot).

### Scene

A loaded scene or prefab file. Spawn it through `Commands`:

| Command | Does |
|---------|------|
| `cmd.spawn_scene(scene, options)` | Spawns a copy and returns a `SceneInstance`. `SceneOptions` fields: `offset` (added to each root's position), `parent` (roots become its children), `overrides` (`{string: string}`: entity `Name` to a JSON patch of its components) |
| `cmd.save_scene(path)` | Writes every entity with a saved component to a scene file, after this system returns |
| `cmd.save_prefab(path, root)` | Writes `root` and its children |

A `SceneInstance` has `entities` and `roots` (`Entity[]`, in file order) and `find(name)`, which returns `Some(entity)` for the first with that `Name`. The entities exist for the systems that run after this one.

## Audio

| Method | Does |
|--------|------|
| `play_music(sound, crossfade, looping)`, `stop_music(fade)`, `pause_music(fade)`, `resume_music(fade)`, `music_playing()` | One music track at a time |
| `master_bus()`, `music_bus()`, `sfx_bus()`, `bus(name, parent)` | Mixer buses; `drift.toml [audio]` sets the built-in ones' starting volumes |
| `has_device()` | `false` when no output device opened; sounds still advance silently |

A `Bus` has `set_volume(v, fade)`, `volume()`, `set_muted(m)`, `set_paused(p)` and `stop(fade)`. A `Voice` from `Sound.play` has `stop(fade)`, `pause(fade)`, `resume(fade)`, `set_volume(v, fade)`, `set_pan(p)`, `set_pitch(p)` and `playing()`.

## Physics

| Method | Does |
|--------|------|
| `gravity()`, `set_gravity(g)` | World gravity in pixels per second squared; `drift.toml [physics]` sets where it starts |
| `raycast(origin, direction, distance)` | A `RayHit`: `hit`, `entity`, `point`, `normal`, `fraction`. Sensors count |
| `overlap_point(p)`, `overlap_rect(r)` | Entities whose colliders cover the point or touch the rect |
| `apply_impulse(e, i)`, `apply_force(e, f)` | On dynamic bodies |

## Effects

| Method | Does |
|--------|------|
| `burst(pos, emitter, count)` | A one-off particle burst that cleans itself up |
| `shake(camera, intensity)` | Screen shake on a camera entity, dying away |
| `flash(color, seconds)`, `fade_to(color, amount, seconds)` | Screen flashes and fades |
| `bloom(threshold, intensity)`, `vignette(intensity, radius)`, `crt(curvature, scanlines)`, `grayscale(amount)`, `sepia(amount)`, `pixelate(size)`, `blur(radius, iterations)` | Post effects; each returns an `Effect` with `set_enabled(on)`, `enabled()` and `remove()` |
| `set_ambient(color)` | Light everywhere, when lighting is on |
| `set_tween_speed(s)` | Multiplies time for every tween; 0 freezes them |

## Camera2D

| Method | Returns |
|--------|---------|
| `screen_to_world(p)`, `world_to_screen(p)` | Through the main camera |
| `visible()` | The world rect the main camera shows |

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

Shapes for this frame only, for debugging. They go on layer 0, or on the HUD after `hud()`.

| Method | Draws |
|--------|-------|
| `rect(r, c)`, `rect_lines(r, c, thickness)` | Rectangles |
| `circle(center, radius, c)`, `circle_lines(center, radius, c, thickness)` | Circles |
| `line(a, b, c, thickness)`, `polyline(points, c, thickness, closed)` | Lines |
| `text(s, pos, size, c)` | Built-in font, top-left at `pos` |
| `hud()`, `world()` | Switch between screen and world coordinates for the draws that follow |

## Systems

| Method | Does |
|--------|------|
| `enable(f)`, `disable(f)`, `enabled(f)` | Turn a system on or off by function; it takes effect next frame |
