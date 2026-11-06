# Candela API Reference

The candela `drift` package by area. `world` is the `World` a system receives, `e` an `Entity`. `Vec2`, `Rect` and `Color` are structs. See [Candela](overview.md) for how a game fits together and how the frame runs.

## Values

| Function | Returns |
|----------|---------|
| `vec2(x, y)` | `Vec2`. Methods: `add`, `sub`, `mul(s)`, `dot`, `length`, `distance`, `normalized`, `lerp(o, t)` |
| `rect(x, y, w, h)` | `Rect` (top-left and size). Methods: `contains(p)`, `overlaps(r)`, `center()` |
| `rgb(r, g, b)`, `rgba(r, g, b, a)` | `Color` from 0..255 ints. `Color` fields are 0.0..1.0; `c.alpha(a)` returns a copy with a new alpha |
| `Cell { x, y }` | A tile cell, ints |
| `ease(Ease, t)` | Eased progress for `t` in 0..1 |
| `hud_layer()` | `31`, the screen-space layer drawn last without a camera |
| `last_error()` | The message of the most recent failed native call |

Enums: `ScaleMode` (Letterbox, Stretch, Integer, Expand), `Blend` (Alpha, Additive, Multiply, Premultiplied, Screen, Opaque), `Filter` (Nearest, Linear), `Align` (Left, Center, Right), `VAlign` (Top, Middle, Bottom), `BodyKind` (Static, Dynamic, Kinematic), `EmitShape` (Point, Circle, Ring, Box, Line), `OccluderShape` (Box, Circle, FromCollider), `Ease` (Linear, In/Out/InOut of Sine, Quad, Cubic, Quart, Quint, Expo, Circ, Back, Elastic, Bounce, and Spring), `Key`, `MouseButton`, `GamepadButton`, `GamepadAxis`, `Binding`.

`Key` has the letters, `Num0`..`Num9`, `F1`..`F12`, `Return`, `Escape`, `Backspace`, `Tab`, `Space`, the arrows (`Left`, `Right`, `Up`, `Down`) and the modifiers (`LCtrl`, `LShift`, `LAlt`, `RCtrl`, `RShift`, `RAlt`). `Key::Code(n)` names any other key by its SDL scancode.

## App

| Function | Does |
|----------|------|
| `config(title, width, height)` | A `Config` with defaults for the rest. Fields: `title`, `width`, `height` (the logical resolution), `fullscreen`, `vsync`, `resizable`, `scale_mode`, `lighting`, `renderer` (`""`, `"gl"` or `"webgpu"`), `pipelined` (default `true`: simulate while systems run), `threads` (engine threads; 0 one per core, 1 none) |
| `app(cfg)` | A new `App` |
| `.startup(fn(World))` | A system that runs once before the first frame |
| `.update(fn(World, float))` | A system that runs every frame with the frame time; several run in the order added |
| `.on_collision<A, B>(fn(World, Entity, Entity))` | Called when an entity with `A` starts touching one with `B`; the arguments follow the declared order |
| `.on_sensor<A, B>(fn(World, Entity, Entity))` | The same for an entity entering a sensor collider, either way round |
| `.run()` | Opens the window and runs until the game quits. Raises `drift_start_failed` |

## World

### Entities and components

| Function | Does |
|----------|------|
| `world.spawn()` | A new entity, as an `EntityMut`: chain `.insert(component)`, then `.id()` for the `Entity` |
| `world.entity(e)` | An `EntityMut` for an existing entity |
| `world.insert(e, c)` | Adds or replaces a component |
| `world.get<T>(e)` | The component. Raises `drift_missing_component` |
| `world.has<T>(e)`, `world.remove<T>(e)` | |
| `world.despawn(e)` | Removes the entity and its children; gone from queries at once, from the engine at the end of the frame |
| `world.alive(e)` | |
| `world.query<A>()`, `world.query2<A, B>()`, `world.query3<A, B, C>()` | Rows (`Row`, `Row2`, `Row3`) with `entity` and the components as `a`, `b`, `c`. A list, so spawning and despawning while looping is safe |
| `world.entities<A>()`, `world.count<A>()` | The entities with an `A`; how many |
| `world.par_each<T>(fn(World, Entity, T))`, `world.par_each2<A, B>(...)` | Calls the function for each match. Sequential: candela runs one script thread |
| `world.set_parent(child, parent)` | The child's transform becomes relative to the parent |
| `world.find(name)` | The first entity with that `Name`, or one that is not alive |
| `world.world_position(e)` | Position after parents, from the engine |
| `world.component_json(e, name)`, `world.set_component_json(e, name, json)` | Any reflected engine component as scene JSON, for components the package does not mirror. Raises `drift_bad_component` |
| `world.defer(fn())` | Runs a function at the end of the frame, after spawned entities exist in the engine |

### Resources, time and the window

| Function | Does |
|----------|------|
| `world.insert_resource(r)`, `world.resource<R>()`, `world.has_resource<R>()`, `world.remove_resource<R>()` | One value per type. `resource` raises `drift_missing_resource` |
| `world.dt`, `world.frame`, `world.time()` | Frame time, frames so far, seconds since start |
| `world.input()` | The `Input` (below) |
| `world.quit()` | Ends the game after this frame |
| `world.size()`, `world.set_logical_size(w, h)`, `world.set_scale_mode(ScaleMode)` | The logical resolution and how it maps onto the window |
| `world.set_clear_color(c)`, `world.set_title(s)` | |
| `world.screenshot(path)` | Saves the next presented frame as a PNG |
| `world.backend()`, `world.draw_calls()` | The graphics backend name; draw calls in the last frame |
| `world.stats()` | `FrameStats` of the last frame in milliseconds: `script`, `simulation`, `overlap` (the two at the same time), `wait`, `render`, `serial`, and `background` (the simulation ran on its own thread) |
| `world.screen_to_world(p)`, `world.world_to_screen(p)` | Through the main camera |

## Engine components

Build each with the fields you need and `..` for the defaults. The engine changes some fields itself (marked *engine*); systems read the current value when they query the component.

| Component | Fields |
|-----------|--------|
| `Transform` | `position`, `rotation` (radians), `scale` (1, 1). *Engine*: physics, `Velocity`, behaviors, tweens move it. `at(x, y)` builds one |
| `Velocity` | `linear` (pixels per second), `angular` (radians per second). Moves the entity in the engine; on a dynamic `RigidBody` it is the body's velocity (*engine*) |
| `Sprite` | `image` (a file path, loaded and cached), `texture` (wins over `image`), `frame` (part of the image; `w` 0 for all), `anchor` (0.5, 0.5: the point on the position), `color` (*engine* for tweens), `flip_x`, `flip_y`, `z`, `layer`, `visible`, `blend`. `frame` is *engine* under an `Animator` |
| `Rectangle` | `size`, `color`, `outline` (0 filled), `corner_radius`, `anchor`, `z`, `layer`, `visible` |
| `Circle` | `radius`, `color`, `outline`, `z`, `layer`, `visible` |
| `Text` | `value`, `size`, `color`, `align`, `valign`, `wrap`, `line_spacing`, `outline`, `outline_color`, `font`, `z`, `layer`, `visible`. Up to 511 bytes |
| `Camera` | `zoom` (*engine* for tweens), `rotation`, `active`, `order`, `viewport` (normalized, for split screen) |
| `CameraFollow` | `target`, `smoothing`, `offset`, `dead_zone` |
| `RigidBody` | `kind` (Dynamic), `fixed_rotation`, `gravity_scale`, `linear_damping`, `angular_damping`. Add a collider too |
| `BoxCollider`, `CircleCollider` | `half_size` or `radius`, `offset`, `sensor`, `density`, `friction`, `restitution`, `category`, `mask`. Without a `RigidBody` the collider is static |
| `ParticleEmitter` | `rate`, `lifetime_min`/`max`, `speed_min`/`max`, `angle_min`/`max`, `size_start`, `size_end`, `color_start`, `color_end`, `gravity`, `drag`, `max_particles`, `blend`, `texture`, `shape`, `shape_radius`, `shape_extents`, `duration`, `looping`, `z`, `playing` |
| `PointLight` | `color`, `intensity`, `radius`, `falloff`, `shadows`. Needs `Config.lighting` |
| `SpotLight` | as `PointLight`, plus `direction`, `angle`, `softness` |
| `LightOccluder` | `shape`, `half_size`, `radius` |
| `AudioSource` | `sound`, `bus`, `volume`, `pitch`, `looping`, `autoplay`, `spatial`, `min_distance`, `max_distance` |
| `Tilemap` | `map` (from `world.load_tilemap`), `z`, `layer`, `colliders` |
| `Animator` | `clips` (`AnimationClip { frames: Rect[], frame_seconds, looping }`), `clip` (set it to switch), `playing`; *engine*: `frame`, `finished` |
| `Tween` | Built with `tween()` and `move_to`, `move_by`, `scale_to`, `rotate_to`, `rotate_by`, `fade_to`, `tint_to`, `zoom_to` (each `(target, seconds, Ease)`), `delay(seconds)`, `repeat(times)` or `yoyo(times)` (-1 forever). Inserting a changed tween restarts it |
| `Lifetime` | `seconds`; the entity despawns when it runs out (*engine*: the time left) |
| `Oscillate` | `amplitude`, `frequency`, `phase`; swings the entity on top of other motion |
| `Follow` | `target`, `speed`, `distance`, `offset` |
| `Name` | `value`, for `world.find` and scene files |

## Input

`world.input()` returns an `Input`.

| Function | Does |
|----------|------|
| `key_down(k)`, `key_pressed(k)`, `key_released(k)` | Held now, went down this frame, went up this frame |
| `mouse_position()`, `mouse_world()` | Logical screen position; world position through the main camera |
| `mouse_delta()`, `mouse_wheel()` | Movement and wheel this frame |
| `mouse_down(b)`, `mouse_pressed(b)`, `mouse_released(b)` | Mouse buttons |
| `gamepad_connected(pad)`, `gamepad_axis(pad, axis)` | Pads 0..3; sticks read -1..1 (Y down), triggers 0..1 |
| `gamepad_down(pad, b)`, `gamepad_pressed(pad, b)`, `gamepad_released(pad, b)` | Gamepad buttons (Xbox layout: `South` is A) |
| `send_key(k, down)`, `send_mouse_move(pos)`, `send_mouse_button(pos, b, down)` | Queue input as if from the device; it arrives next frame. For automated tests and demo playback |

### Input actions

Actions name what the player does; any number of bindings feed each one. A `Binding` is `Binding::Key(Key)`, `Mouse(MouseButton)`, `Button(GamepadButton)`, `AxisPositive(GamepadAxis)`, `AxisNegative(GamepadAxis)` or `Unbound`. All are `Input` methods.

| Function | Does |
|----------|------|
| `bind_action(name, b)` | Adds a binding to an action. Raises `drift_bad_binding` for `Unbound` |
| `bind_axis(name, negative, positive)` | A 1D axis, -1..1 |
| `bind_axis2d(name, left, right, up, down)` | A 2D axis; up is -Y. Calling it again adds another set |
| `bind_stick(name, x_axis, y_axis)` | A whole stick as a 2D axis |
| `action_pressed(name)`, `action_held`, `action_released`, `action_value` | Action state; axes count as held past the press threshold |
| `axis(name)`, `axis2d(name)` | Axis values |
| `bindings(name, slot)` | The bindings in a slot as text (`"key:Space"`). Slot 0 for actions; 0/1 negative/positive for axes; 0..3 left, right, up, down for 2D axes |
| `rebind(name, slot, index, b)`, `rebind_text(name, slot, index, text)` | Replace one binding |
| `capture_binding()` | The first input that went down this frame, as binding text, or `""`; for "press a key" menus |
| `bindings_json()`, `load_bindings_json(text)` | Save and restore every binding |
| `has_action(name)`, `remove_action(name)`, `set_action_tuning(dead_zone, press_threshold, gamepad)` | Housekeeping |

## Physics

| Function | Does |
|----------|------|
| `world.set_gravity(g)`, `world.gravity()` | World gravity in pixels per second squared; default `(0, 9.8)` |
| `world.raycast(origin, direction, distance)` | `RayHit { hit, point, normal, fraction }`; sensors count as hits |
| `world.apply_impulse(e, i)`, `world.apply_force(e, f)` | Dynamic bodies |

Bodies and colliders are components; collisions and sensor overlaps reach the handlers given to `on_collision` and `on_sensor`. Add the body and its collider in the same frame: their settings are read when the body is created at the end of that frame. Physics queries wait for the frame's simulation step (see [the frame](overview.md#the-frame)).

## Textures, sheets and fonts

| Function | Does |
|----------|------|
| `world.load_texture(path)` | PNG and other stb_image formats; the same path returns the same texture. Raises `drift_load_failed` |
| `world.create_texture(w, h, pixels: Color[])` | From colors, row by row from the top-left |
| `world.create_render_target(w, h)`, `world.white_texture()` | |
| `tex.size()`, `tex.set_filter(Filter::Linear)`, `tex.destroy()` | Textures default to `Nearest` (crisp pixels) |
| `world.spritesheet(tex, frame_w, frame_h)`, `world.load_aseprite(path)` | A `Spritesheet` |
| `sheet.frame(col, row)`, `sheet.frame_at(i)`, `sheet.frames(first, count)`, `sheet.frame_count()`, `sheet.texture()` | Source rects for sprites and animation clips |
| `world.load_font(path, size)` | TrueType, OpenType or BMFont; `Font { id: 0 }` is the built-in font |
| `world.measure_text(text, size)`, `world.measure_text_with(font, text, style)`, `world.line_height(font, size)` | Text metrics; `text_style(size, color)` builds a `TextStyle` |

## Audio

| Function | Does |
|----------|------|
| `world.load_sound(path)`, `world.load_music(path)` | WAV, OGG, MP3; music decodes while it plays. Raises `drift_load_failed` |
| `world.create_sound(samples: float[], rate)` | A mono sound from samples in -1..1 |
| `snd.play()`, `snd.play_with(play_options())` | Returns a `Voice`. `PlayOptions`: `volume`, `pan`, `pitch`, `looping`, `bus`, `fade_in`, `priority` |
| `snd.duration()`, `snd.set_max_instances(n)`, `snd.destroy()` | |
| `voice.stop(fade)`, `pause(fade)`, `resume(fade)`, `set_volume(v, fade)`, `set_pan(p)`, `set_pitch(p)`, `playing()` | One playback |
| `world.master_bus()`, `world.music_bus()`, `world.sfx_bus()`, `world.create_bus(name, parent)` | Mixer buses |
| `bus.set_volume(v, fade)`, `volume()`, `set_muted(m)`, `set_paused(p)`, `stop(fade)` | Apply to everything on the bus and below |
| `world.play_music(snd, crossfade, looping)`, `stop_music(fade)`, `pause_music(fade)`, `resume_music(fade)`, `music_playing()` | One music track at a time |
| `world.audio_device()` | `false` when no output device opened; sounds still advance silently |

An `AudioSource` component plays a sound from an entity instead.

## Particles, lights and effects

| Function | Does |
|----------|------|
| `world.burst(pos, ParticleEmitter { .. }, count)` | A one-off burst that cleans itself up |
| `world.shake(camera, intensity)` | Screen shake on a camera entity that dies away |
| `world.set_ambient(c)` | Light everywhere. Raises `drift_lighting_off` without `Config.lighting` |
| `world.add_bloom(threshold, intensity)`, `add_vignette(intensity, radius)`, `add_crt(curvature, scanlines)`, `add_grayscale(a)`, `add_sepia(a)`, `add_pixelate(size)`, `add_blur(radius, iterations)` | Post effects; each returns an `Effect` with `set_enabled(on)` and `remove()` |
| `world.fade_to(c, amount, seconds)`, `world.flash(c, seconds)` | Screen fades and flashes |
| `world.set_tween_speed(s)` | Multiplies time for every tween; 0 freezes them |

## Timers and save data

| Function | Does |
|----------|------|
| `timer(seconds)`, `repeating_timer(seconds)` | A `Timer`: `tick(dt)` returns how many times it finished; `finished()`, `remaining()`, `fraction()`, `reset()`, `paused` |
| `cooldown(seconds)` | A `Cooldown`: `tick(dt)`, `ready()`, `try_use()`, `reset()` |
| `world.open_save(org, app, slot)`, `world.open_save_file(path)` | A `Save` in the per-user directory or at a path. Raises `drift_save_failed` |
| `save.set_int/float/string(key, v)`, `get_int/float/string(key, fallback)`, `has`, `erase`, `clear` | Values |
| `save.flush()`, `save.reload()`, `save.path()`, `save.json()` | `flush` writes to disk and raises `drift_save_failed` |

Timers are plain values: keep them in a resource or a component and tick them from a system.

## Tilemaps and scenes

| Function | Does |
|----------|------|
| `world.load_tilemap(path)`, `world.load_level(path, level)` | Tiled (`.tmj`/`.json`) or LDtk; a `TilemapAsset`. Show it with a `Tilemap` component. Raises `drift_load_failed` |
| `map.size()`, `map.tile_size()`, `map.bounds()`, `map.layer_count()`, `map.layer(name)`, `map.layer_name(i)` | `layer` raises `drift_no_such_layer` |
| `map.tile(layer, x, y)`, `map.set_tile(...)`, `map.int_grid(...)`, `map.set_int_grid(...)`, `map.tile_at(layer, world_pos)` | Tile and IntGrid values |
| `map.world_to_tile(p, layer)`, `map.tile_to_world(cell, layer)` | Coordinates, after the map is drawn |
| `map.set_layer_visible(layer, v)`, `map.set_layer_solid(layer, solid, values)` | Layer settings |
| `map.objects(kind)`, `map.object_property(o, name)`, `map.property(name)` | `MapObject { name, kind, bounds, anchor }` from object layers; spawn entities for them yourself |
| `world.load_scene(path)` | A `Scene` from a JSON scene or prefab. Raises `drift_load_failed` |
| `world.spawn_scene(scene, offset)`, `world.spawn_scene_with(scene, offset, parent, overrides)` | Spawns now and returns the entities, with their mirrored components; `overrides` is per-entity JSON |
| `world.save_scene(path)` | Every engine entity with a reflected component, as JSON |

## Debug drawing

Immediate shapes for this frame only, for debugging from a system; games draw with components. Draws go on the current layer (0 by default; `hud_layer()` for screen space) at the current z, both reset every frame.

| Function | Does |
|----------|------|
| `world.set_draw_layer(n)`, `world.set_draw_z(z)` | Layer and draw order for the draws that follow |
| `world.draw_rect(r, c)`, `world.draw_rect_lines(r, c, thickness)` | Rectangles |
| `world.draw_circle(center, radius, c)`, `world.draw_circle_lines(center, radius, c, thickness)` | Circles |
| `world.draw_line(a, b, c, thickness)`, `world.draw_polyline(points, c, thickness, closed)` | Lines |
| `world.draw_texture(tex, pos, sprite_options())` | `SpriteOptions` fields: `src`, `scale`, `rotation`, `origin`, `tint`, `flip_x`, `flip_y`, `blend` |
| `world.draw_text(text, pos, size, c)` | Built-in font, top-left at `pos` |
