# Candela API Reference

Every function of the candela `drift` package, by area. `app` is the `App` returned by `start`, `e` an `Entity`. Types in signatures are candela types; `Vec2`, `Rect` and `Color` are structs passed by value. See [Candela](overview.md) for setup and conventions.

## Values

| Function | Returns |
|----------|---------|
| `vec2(x, y)` | `Vec2`. Methods: `add`, `sub`, `mul(s)`, `dot`, `length`, `distance`, `normalized`, `lerp(o, t)` |
| `rect(x, y, w, h)` | `Rect` (top-left and size). Methods: `contains(p)`, `overlaps(r)`, `center()` |
| `rgb(r, g, b)`, `rgba(r, g, b, a)` | `Color` from 0..255 ints. `Color` fields are 0.0..1.0; `c.alpha(a)` returns a copy with a new alpha |
| `Cell { x, y }` | A tile cell, ints |
| `ease(Ease, t)` | Eased progress for `t` in 0..1 |
| `hud_layer()` | `31`, the screen-space layer drawn last without a camera |
| `last_error()` | The message of the most recent failed call |

Enums: `ScaleMode` (Letterbox, Stretch, Integer, Expand), `Blend` (Alpha, Additive, Multiply, Premultiplied, Screen, Opaque), `Filter` (Nearest, Linear), `Align` (Left, Center, Right), `VAlign` (Top, Middle, Bottom), `BodyKind` (Static, Dynamic, Kinematic), `EmitShape` (Point, Circle, Ring, Box, Line), `Ease` (Linear, In/Out/InOut of Sine, Quad, Cubic, Quart, Quint, Expo, Circ, Back, Elastic, Bounce, and Spring), `Key`, `MouseButton`, `GamepadButton`, `GamepadAxis`, `Binding`.

`Key` has the letters, `Num0`..`Num9`, `F1`..`F12`, `Return`, `Escape`, `Backspace`, `Tab`, `Space`, the arrows (`Left`, `Right`, `Up`, `Down`) and the modifiers (`LCtrl`, `LShift`, `LAlt`, `RCtrl`, `RShift`, `RAlt`). `Key::Code(n)` names any other key by its SDL scancode.

## App and frame loop

| Function | Does |
|----------|------|
| `config(title, width, height)` | A `Config`: windowed, resizable, vsync, letterboxed, no lighting. Fields: `title`, `width`, `height` (the logical resolution), `fullscreen`, `vsync`, `resizable`, `scale_mode`, `lighting`, `renderer` (`""`, `"gl"` or `"webgpu"`) |
| `start(cfg)` | Opens the window and starts the engine. Raises `drift_start_failed` |
| `app.frame()` | Presents the previous frame and runs the next; `false` once the app has quit |
| `app.quit()` | Ends the loop: the next `frame()` returns `false` |
| `app.close()` | Shuts the engine down now (only needed after leaving the loop early) |
| `app.dt()`, `app.time()`, `app.frame_count()` | Seconds of the last frame, seconds since start, frames so far |
| `app.size()` | The logical resolution as a `Vec2` |
| `app.set_logical_size(w, h)`, `app.set_scale_mode(ScaleMode)` | Change how the game maps onto the window |
| `app.set_clear_color(c)`, `app.set_title(s)` | Background color, window title |
| `app.screenshot(path)` | Saves the next presented frame as a PNG |
| `app.backend()`, `app.draw_calls()` | The graphics backend name; draw calls in the last frame |

## Input

| Function | Does |
|----------|------|
| `app.key_down(k)`, `app.key_pressed(k)`, `app.key_released(k)` | Held now, went down this frame, went up this frame |
| `app.mouse_position()` | Logical screen position |
| `app.mouse_world()` | World position under the mouse, through the main camera |
| `app.mouse_delta()`, `app.mouse_wheel()` | Movement and wheel this frame |
| `app.mouse_down(b)`, `app.mouse_pressed(b)`, `app.mouse_released(b)` | Mouse buttons |
| `app.gamepad_connected(pad)`, `app.gamepad_axis(pad, axis)` | Pads 0..3; sticks read -1..1 (Y down), triggers 0..1 |
| `app.gamepad_down(pad, b)`, `app.gamepad_pressed(pad, b)`, `app.gamepad_released(pad, b)` | Gamepad buttons (Xbox layout: `South` is A) |
| `app.send_key(k, down)`, `app.send_mouse_move(pos)`, `app.send_mouse_button(pos, b, down)` | Queue input as if from the device; it arrives with the next `frame()`. For automated tests and demo playback |

### Input actions

Actions name what the player does; any number of bindings feed each one. A `Binding` is `Binding::Key(Key)`, `Mouse(MouseButton)`, `Button(GamepadButton)`, `AxisPositive(GamepadAxis)`, `AxisNegative(GamepadAxis)` or `Unbound`.

| Function | Does |
|----------|------|
| `app.bind_action(name, b)` | Adds a binding to an action. Raises `drift_bad_binding` for `Unbound` |
| `app.bind_axis(name, negative, positive)` | A 1D axis, -1..1 |
| `app.bind_axis2d(name, left, right, up, down)` | A 2D axis; up is -Y. Calling it again adds another set |
| `app.bind_stick(name, x_axis, y_axis)` | A whole stick as a 2D axis |
| `app.action_pressed(name)`, `action_held`, `action_released`, `action_value` | Action state; axes count as held past the press threshold |
| `app.axis(name)`, `app.axis2d(name)` | Axis values |
| `app.bindings(name, slot)` | The bindings in a slot as text (`"key:Space"`). Slot 0 for actions; 0/1 negative/positive for axes; 0..3 left, right, up, down for 2D axes |
| `app.rebind(name, slot, index, b)`, `app.rebind_text(name, slot, index, text)` | Replace one binding |
| `app.capture_binding()` | The first input that went down this frame, as binding text, or `""`; for "press a key" menus |
| `app.bindings_json()`, `app.load_bindings_json(text)` | Save and restore every binding |
| `app.has_action(name)`, `app.remove_action(name)`, `app.set_action_tuning(dead_zone, press_threshold, gamepad)` | Housekeeping |

## Entities

| Function | Does |
|----------|------|
| `app.spawn()`, `app.spawn_at(pos)` | A new entity with a transform |
| `app.spawn_sprite(tex, pos)` | An entity drawing `tex` centered on `pos` |
| `app.spawn_rect(pos, size, c)` | A solid rectangle centered on `pos` |
| `app.spawn_text(text, pos, size, c)` | Text with the built-in font, top-left at `pos` |
| `app.spawn_camera(pos)` | An active camera looking at `pos` |
| `app.find(name)` | First entity with that name (not alive if none) |
| `app.query(components)` | Every entity with the named components, as a flecs query expression: `"Sprite"`, `"RigidBody2D, Velocity2D"`, `"Text, !Sprite"`. Raises `drift_bad_query` |
| `e.alive()`, `e.despawn()`, `e.equals(o)` | Despawning also removes children |
| `e.set_name(s)`, `e.name()` | The `Name` component |
| `e.has(component)`, `e.remove(component)` | By component name |
| `e.component_json(component)`, `e.set_component_json(component, json)` | Any reflected component as scene JSON; setting merges into the current value. Raises `drift_bad_component` |
| `e.set_parent(p)`, `e.parent()` | Parenting: the child's transform becomes relative to the parent |

### Transform

`e.set_position(p)`, `e.position()`, `e.world_position()` (after parents), `e.move_by(d)`, `e.set_rotation(radians)`, `e.rotation()`, `e.set_scale(s)`, `e.scale()`. Setting the position of a physics body teleports it.

### Sprite

| Function | Does |
|----------|------|
| `e.set_sprite(tex)`, `e.texture()` | Adds or changes the sprite |
| `e.set_frame(src)`, `e.frame()` | Source rect within the texture (a spritesheet frame) |
| `e.set_origin(o)`, `e.center_origin()` | The source pixel placed on the position; also the rotation pivot. Default is the top-left |
| `e.set_tint(c)`, `e.tint()` | Color multiplier, alpha included |
| `e.set_flip(x, y)`, `e.set_z(z)`, `e.set_layer(layer)`, `e.set_visible(v)`, `e.set_blend(Blend)` | Draw settings; higher z draws on top within a layer |
| `e.remove_sprite()` | |

### Sprite animation

| Function | Does |
|----------|------|
| `e.add_animation(frames: Rect[], frame_seconds, looping)` | Adds a clip and returns its index; the first clip starts playing |
| `e.play_animation(i)`, `e.stop_animation()` | |
| `e.animation()`, `e.animation_frame()`, `e.animation_finished()` | Current clip and frame; whether a non-looping clip has reached its end |

### Text

`e.set_text(s)`, `e.text()`, `e.set_text_size(px)`, `e.set_text_color(c)`, `e.set_text_z(z)`, `e.set_text_layer(layer)`, `e.set_text_visible(v)`, and `e.set_text_style(font, style)` with a `TextStyle` from `text_style(size, color)` (fields: `size`, `color`, `align`, `valign`, `wrap_width`, `line_spacing`, `outline`, `outline_color`, `shadow`, `shadow_color`, `rich`). Text entities hold up to 511 bytes; draw longer text with `app.draw_text`.

### Camera

| Function | Does |
|----------|------|
| `e.add_camera()` | Makes `e` an active camera looking at its position |
| `e.set_zoom(z)`, `e.zoom()` | |
| `e.set_viewport(r)` | Normalized screen rect (0..1, top-left) for split screen |
| `e.set_camera_order(n)`, `e.set_camera_layers(mask)`, `e.set_camera_active(a)` | Draw order, the layers it draws (bit N = layer N) |
| `e.set_camera_target(tex)`, `e.set_camera_clear(clear, c)` | Render into a texture from `app.create_render_target` |
| `e.follow(target, smoothing)`, `e.follow_with(target, smoothing, offset, dead_zone)` | Smooth follow |
| `e.shake(intensity)` | Screen shake that dies away |
| `app.screen_to_world(p)`, `app.world_to_screen(p)` | Through the main camera |

### Physics

| Function | Does |
|----------|------|
| `e.add_body(body(BodyKind::Dynamic))` | A physics body. `Body` fields: `kind`, `fixed_rotation`, `gravity_scale`, `linear_damping`, `angular_damping`. An entity with a collider and no body is static |
| `e.add_box_collider(half_size, collider())`, `e.add_circle_collider(radius, collider())` | `Collider` fields: `offset`, `density`, `friction`, `restitution`, `sensor`, `category`, `mask` |
| `e.set_velocity(v)`, `e.velocity()`, `e.apply_impulse(i)`, `e.apply_force(f)` | Dynamic bodies |
| `e.remove_body()` | Removes the body and colliders |
| `app.set_gravity(g)`, `app.gravity()` | World gravity in pixels per second squared; default `(0, 9.8)` |
| `app.collisions()`, `app.collisions_ended()` | `Collision { a, b }` pairs from this frame. Methods: `involves(e)`, `other(e)` |
| `app.sensor_entered()`, `app.sensor_exited()` | `SensorEvent { sensor, visitor }` from this frame |
| `app.raycast(origin, direction, distance)` | `RayHit { hit, point, normal, fraction }`; sensors count as hits |

Add the body and collider in the same frame; their settings are read when the body is created at the end of that frame.

## Drawing

Immediate draws show in the current frame. They go on the current layer (0 by default; `hud_layer()` for screen space) at the current z; both reset every frame.

| Function | Does |
|----------|------|
| `app.set_layer(n)`, `app.set_z(z)` | Layer and draw order for the draws that follow |
| `app.draw_rect(r, c)`, `app.draw_rect_lines(r, c, thickness)`, `app.draw_rounded_rect(r, radius, c)` | Rectangles |
| `app.draw_circle(center, radius, c)`, `app.draw_circle_lines(...)`, `app.draw_arc(center, radius, from, to, c, thickness)` | Circles and arcs (radians) |
| `app.draw_line(a, b, c, thickness)`, `app.draw_triangle(a, b, c, col)` | |
| `app.draw_polygon(points: Vec2[], c)`, `app.draw_polyline(points, c, thickness, closed)` | Simple polygons, convex or not |
| `app.draw_texture(tex, pos)`, `app.draw_texture_with(tex, pos, sprite_options())` | `SpriteOptions` fields: `src`, `scale`, `rotation`, `origin`, `tint`, `flip_x`, `flip_y`, `blend` |
| `app.draw_text(text, pos, size, c)`, `app.draw_text_with(font, text, pos, style)` | UTF-8 text, top-left at `pos` by default |
| `app.measure_text(text, size)`, `app.measure_text_with(font, text, style)`, `app.line_height(font, size)` | Text metrics |
| `app.push_clip(r)`, `app.pop_clip()` | Clip draws to a screen rect |

## Textures, sheets and fonts

| Function | Does |
|----------|------|
| `app.load_texture(path)` | PNG and other stb_image formats; the same path returns the same texture. Raises `drift_load_failed` |
| `app.create_texture(w, h, pixels: Color[])` | From colors, row by row from the top-left |
| `app.create_render_target(w, h)`, `app.white_texture()` | |
| `tex.size()`, `tex.set_filter(Filter::Linear)`, `tex.destroy()` | Textures default to `Nearest` (crisp pixels) |
| `app.spritesheet(tex, frame_w, frame_h)`, `app.load_aseprite(path)` | A `Spritesheet` |
| `sheet.frame(col, row)`, `sheet.frame_at(i)`, `sheet.frames(first, count)`, `sheet.frame_count()`, `sheet.texture()` | Source rects for sprites and clips |
| `app.load_font(path, size)`, `app.default_font()` | TrueType, OpenType or BMFont |

## Audio

| Function | Does |
|----------|------|
| `app.load_sound(path)`, `app.load_music(path)` | WAV, OGG, MP3; music decodes while it plays. Raises `drift_load_failed` |
| `app.create_sound(samples: float[], rate)` | A mono sound from samples in -1..1 |
| `snd.play()`, `snd.play_with(play_options())` | Returns a `Voice`. `PlayOptions`: `volume`, `pan`, `pitch`, `looping`, `bus`, `fade_in`, `priority` |
| `snd.duration()`, `snd.set_max_instances(n)`, `snd.destroy()` | |
| `voice.stop(fade)`, `pause(fade)`, `resume(fade)`, `set_volume(v, fade)`, `set_pan(p)`, `set_pitch(p)`, `playing()` | One playback |
| `app.master_bus()`, `app.music_bus()`, `app.sfx_bus()`, `app.create_bus(name, parent)` | Mixer buses |
| `bus.set_volume(v, fade)`, `volume()`, `set_muted(m)`, `set_paused(p)`, `stop(fade)` | Apply to everything on the bus and below |
| `app.play_music(snd, crossfade, looping)`, `stop_music(fade)`, `pause_music(fade)`, `resume_music(fade)`, `music_playing()` | One music track at a time |
| `app.audio_device()` | `false` when no output device opened; sounds still advance silently |

## Particles, lights and effects

| Function | Does |
|----------|------|
| `e.add_emitter(emitter())` | A particle emitter at the entity. `Emitter` fields: `rate`, `lifetime_min`/`max`, `speed_min`/`max`, `angle_min`/`max`, `size_start`, `size_end`, `color_start`, `color_end`, `gravity`, `drag`, `max_particles`, `blend`, `texture`, `shape`, `shape_radius`, `shape_extents`, `duration`, `looping`, `z` |
| `e.set_emitting(on)`, `e.set_emit_rate(r)` | |
| `app.burst(pos, emitter(), count)` | A one-off burst that cleans itself up |
| `e.add_point_light(c, intensity, radius)`, `e.add_spot_light(c, intensity, radius, direction, angle)`, `e.set_light_color(c, intensity)` | Lights; need `Config.lighting = true` |
| `e.add_occluder_box(half_size)`, `e.add_occluder_circle(r)`, `e.add_occluder_from_collider()` | Cast shadows |
| `app.set_ambient(c)` | Light everywhere. Raises `drift_lighting_off` without lighting |
| `app.add_bloom(threshold, intensity)`, `add_vignette(intensity, radius)`, `add_crt(curvature, scanlines)`, `add_grayscale(a)`, `add_sepia(a)`, `add_pixelate(size)`, `add_blur(radius, iterations)` | Post effects; each returns an `Effect` with `set_enabled(on)` and `remove()` |
| `app.fade_to(c, amount, seconds)`, `app.flash(c, seconds)` | Screen fades and flashes |

## Tweens, timers and save data

| Function | Does |
|----------|------|
| `app.tween(e)` | A `TweenBuilder` for `e`; chain `move_to`, `move_by`, `scale_to`, `rotate_to`, `rotate_by`, `fade_to`, `tint_to`, `zoom_to` (each `(target, seconds, Ease)`), `delay(seconds)`, `repeat(times)` or `yoyo(times)` (-1 forever), then `start()` |
| `app.sequence()` | A tween with no entity, for delays |
| `tween.active()`, `stop(complete)`, `pause()`, `resume()`, `e.stop_tweens()`, `app.set_tween_speed(s)` | A finished tween is no longer active |
| `timer(seconds)`, `repeating_timer(seconds)` | A `Timer`; `t.tick(dt)` returns how many times it finished. Also `finished()`, `remaining()`, `fraction()`, `reset()` |
| `cooldown(seconds)` | A `Cooldown`: `tick(dt)`, `ready()`, `try_use()`, `reset()` |
| `app.open_save(org, app_name, slot)`, `app.open_save_file(path)` | A `Save` in the per-user directory or at a path. Raises `drift_save_failed` |
| `save.set_int/float/string(key, v)`, `get_int/float/string(key, fallback)`, `has`, `erase`, `clear`, `flush()`, `reload()`, `path()`, `json()` | `flush()` writes to disk |

Timers and cooldowns are plain candela values: keep them in your game state and tick them with `app.dt()`.

## Tilemaps and scenes

| Function | Does |
|----------|------|
| `app.load_tilemap(path)`, `app.load_level(path, level)` | A Tiled map (`.tmj`/`.json`) or an LDtk level. Raises `drift_load_failed` |
| `app.spawn_tilemap(map, pos)`, `e.set_tilemap(map, z, layer, colliders)` | Draw the map with its top-left at the entity, with static colliders from its collision layers |
| `map.size()`, `map.tile_size()`, `map.bounds()`, `map.property(name)` | Map info |
| `map.layer(name)`, `map.layer_count()`, `map.layer_name(i)`, `map.set_layer_visible(i, v)`, `map.set_layer_solid(i, solid, values)` | Layers. `layer` raises `drift_no_such_layer` |
| `map.tile(layer, x, y)`, `map.set_tile(...)`, `map.int_grid(layer, x, y)`, `map.set_int_grid(...)` | Tile ids (0 = empty) and LDtk IntGrid values; infinite Tiled maps use negative cells too |
| `map.world_to_tile(p, layer)`, `map.tile_to_world(cell, layer)`, `map.tile_at(layer, p)` | From where the map was last drawn |
| `map.objects(kind)`, `map.object_property(o, name)` | `MapObject { name, kind, bounds, anchor }`, `""` for every object |
| `app.load_scene(path)` | A JSON scene or prefab |
| `scene.spawn(offset)`, `scene.spawn_with(offset, parent, overrides_json)` | A `SceneInstance`: `entities()`, `roots()`, `find(name)` |
| `app.save_scene(path)` | Saves every entity with a reflected component |
