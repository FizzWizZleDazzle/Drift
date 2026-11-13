# Values and Enums

Plain structs and functions the `drift` module provides. `import "drift" { * };` brings them all in; a name you declare yourself shadows the imported one.

## Structs

| Type | Fields and methods |
|------|--------------------|
| `Vec2` | `x`, `y`. `a + b`, `a - b`, `-a`, `a * s`, `a / s` with a float `s`; `dot`, `length`, `length_squared`, `distance`, `normalized`, `lerp(o, t)`, `angle()` (radians from +x toward +y), `rotated(radians)`, `perp()` (a quarter turn), `clamp_length(max)`. Built with `vec2(x, y)`, or `from_angle(radians)` for a unit vector |
| `Rect` | `x`, `y`, `w`, `h` (top-left and size). `contains(p)`, `overlaps(r)`, `center()`. Built with `rect(x, y, w, h)` |
| `Color` | `r`, `g`, `b`, `a`, from 0.0 to 1.0. `alpha(a)` returns a copy with a new alpha, `lerp(o, t)` blends. Built with `rgb(r, g, b)` or `rgba(r, g, b, a)` from 0 to 255 |
| `Entity` | An entity's id. Compare with `==`. `Entity {}` is no entity. `alive()`: whether it exists, false once a despawn has been applied |
| `Cell` | `x`, `y`, ints: a tile or grid position. Built with `cell(x, y)` |
| `Rng` | A seeded random generator; see [Procedural Generation](../guides/procedural-generation.md#random-numbers) |
| `WeightedTable<T>` | Items rolled by weight: `add(item, weight)`, `roll(rng)`. Built with `weighted_table<T>()` |
| `Noise` | A noise recipe: `seed`, `kind`, `frequency`, `octaves`, `lacunarity`, `gain`, `warp`, `warp_frequency`. `sample(x, y)`, `samples(from, w, h, step)`, `grid(w, h, threshold, above, below)` |
| `Grid` | `width`, `height`, `cells`. `get`, `set`, `count`, `cells_of`, region tools; see [Grids](../guides/procedural-generation.md#grids) |
| `GridRect`, `Dungeon` | A room (`x`, `y`, `w`, `h`, `center()`, `contains(c)`) and a dungeon's `grid` and `rooms` |
| `CaveOptions`, `DungeonOptions`, `WalkOptions`, `WfcOptions` | Generator settings; every field has a default |
| `FlowField` | `direction(c)`, `direction_at(pos, cell_size)`, `step(c)`, `cost(c)` |
| `Burst` | `time`, `count`, `cycles` (0 forever), `interval`: particles at once, for `ParticleEmitter.bursts` |
| `PlayOptions` | `volume` (1), `pan` (-1 left to 1 right), `pitch` (1), `looping`, `bus` (the sfx bus when unset), `fade_in` (seconds), `priority` (higher keeps its voice when all are busy). For `Sound.play_with` |
| `Timer`, `Cooldown` | See [Timers and Save Data](../guides/timers-and-saves.md) |
| `Image` | Pixels to change from code; see [Image](resources.md#image) |
| `Shader`, `Material` | A fragment shader, and a shader with its parameters and textures; from `Assets` |
| `Spritesheet` | A texture cut into equal frames: `texture`, `frame_w`, `frame_h`, `columns`, `rows`. `frame(col, row)`, `frame_at(i)` (left to right, top to bottom from 0), `frames(first, count)`, `count()`. Built with `assets.spritesheet(texture, frame_w, frame_h)` |
| `GlobalTransform` | `position`, `rotation`, `scale`: a transform in world space, from `world_transform(e)` |
| `Spring`, `Spring2`, `AngleSpring` | Damped springs for a float, a `Vec2` and a rotation: `value`, `velocity`, `update(target, stiffness, damping, dt)`; `Spring` and `Spring2` also `smooth(target, seconds, dt)`. See [Procedural Animation](../guides/procedural-animation.md#springs) |
| `TwoBone` | A two-bone limb's pose: `joint`, `end`, `upper`, `lower` (world rotations), `reached` |
| `IkChain` | A FABRIK chain: `joints`, `lengths`, `min_angles`, `max_angles`, `base_angle`, `iterations`, `tolerance`. `solve(root, target)`, `limit_all(max_bend)`, `end()`, `angle(i)`, `len()` |
| `Rope` | A verlet chain: `points`, `previous`, `segment`, `gravity`, `damping`, `iterations`, `stiffness`, `ground`. `update(anchor, dt)`, `pin(target)`, `release()`, `push(v)`, `angle(i)`, `len()` |
| `Foot`, `Gait` | A stepping foot (`position`, `planted()`, `update(rest, gait, can_step, dt)`) and how it steps (`distance`, `duration`, `height`, `lead`, `up`) |
| `Texture`, `Sound`, `Font`, `Map`, `Scene` | Assets from `Assets` |

## Post effect settings

The structs `Effects.add` takes; every field has a default.

| Struct | Fields |
|--------|--------|
| `Bloom` | `threshold` (0.7), `soft_knee` (0.5), `intensity` (1), `radius` (1), `levels` (5, 1 to 8), `tint` |
| `ColorGrading` | `brightness`, `contrast`, `saturation` (1 each), `tint`, `lut` (a lookup `Texture`: N slices of N x N side by side, red across a slice, green down, blue picking the slice), `lut_strength` |
| `Blur` | `radius` (1), `iterations` (2, 1 to 8) |
| `Pixelate` | `size` (6, in scene pixels) |
| `Grayscale`, `Sepia` | `amount` (1) |
| `Vignette` | `intensity` (0.6), `radius` (0.75), `softness` (0.45), `color` (black) |
| `Crt` | `curvature` (0.15), `scanlines` (0.35), `scanline_spacing` (3), `chromatic_aberration` (1.5), `vignette` (0.3) |

## Functions

| Function | Returns |
|----------|---------|
| `texture(path)` | The `Texture` of an image file (PNG, JPG, BMP, TGA, GIF), loaded once per path |
| `sound(path)` | The `Sound` in a WAV, OGG or MP3 file, loaded once per path |
| `music(path)` | The same for a long track: it decodes while it plays instead of all at load |
| `at(x, y)` | A `Transform` at a position |
| `plus(bundle, more)` | The bundle with more components after it: `plus(enemy(), [Boss {}])`. A component given twice is the later one |
| `clamp(x, lo, hi)`, `min(a, b)`, `max(a, b)` | Floats held between bounds, the smaller, the larger |
| `tween()` | An empty `Tween` to chain steps onto |
| `ease(e, t)` | Eased progress for `t` from 0 to 1 |
| `hud_layer()` | The screen-space layer, drawn last without a camera |
| `pixel(c)`, `color_of(px)` | A `Color` as one `int`, 0xRRGGBBAA, and back, for `Image.pixels()` |
| `layer_mask(layers: int[])` | A `Camera.layer_mask` drawing those world layers: `layer_mask([0, 1])` |
| `from_angle(radians)` | The unit `Vec2` at that angle |
| `sin(x)`, `cos(x)`, `atan2(y, x)` | Trigonometry in radians |
| `wrap_angle(a)`, `angle_diff(from, to)`, `lerp_angle(a, b, t)`, `rotate_towards(current, target, max_step)`, `look_at(from, to)` | Angle helpers that take the short way round; see [Procedural Animation](../guides/procedural-animation.md#angles) |
| `lerp(a, b, t)`, `move_towards(current, target, max_step)`, `damp(current, target, rate, dt)`, `damp2(...)`, `smoothstep(lo, hi, x)` | Number helpers |
| `sine_wave(t, frequency, amplitude, phase)`, `cosine_wave(...)`, `wobble(t, frequency, amplitude, seed)`, `wobble2(...)` | Oscillators and smooth random motion |
| `squash(amount)`, `stretch(velocity, per_speed, max_stretch)` | Area-keeping scales for squash and stretch |
| `spring(v)`, `spring2(v)`, `angle_spring(v)` | Springs at rest at a value |
| `two_bone_ik(root, target, upper_len, lower_len, bend)` | A `TwoBone` pose |
| `ik_chain(root, count, length, direction)`, `rope(start, count, segment, direction)`, `foot(at)` | A straight IK chain, a rope, a planted foot |
| `parent_of(e)` | The entity's parent; `Entity {}` for a root |
| `children_of(e)`, `descendants_of(e)` | The direct children in the order they were added; everything below, depth first |
| `world_transform(e)` | The entity's `GlobalTransform`: its place after every parent |
| `to_world(e, p)`, `to_local(e, p)` | A point in the entity's space (where a child at `p` would be) to the world, and a world point to the entity's space |
| `timer(seconds)`, `repeating_timer(seconds)`, `cooldown(seconds)` | Timers |
| `log(message)`, `warn(message)` | Write to the engine log |
| `rng(seed)`, `rng_stream(seed, stream)`, `seed_from(text)` | Random generators and seeds |
| `noise(seed)`, `grid(w, h, value)`, `cell(x, y)`, `weighted_table<T>()` | |
| `cave(rng, w, h, o)`, `dungeon(rng, w, h, o)`, `random_walk(rng, w, h, o)`, `wfc(rng, sample, w, h, o)` | Level generators; see [Procedural Generation](../guides/procedural-generation.md#generators) |
| `poisson_disk(rng, area, min_distance)`, `poisson_on_grid(rng, g, value, min_distance)` | Evenly spread points |
| `find_path(g, start, goal, blocked, diagonal)`, `flow_field(g, goals, blocked, diagonal)` | Pathfinding over a grid |

## Constants

| Constant | Value |
|----------|-------|
| `FLOOR`, `WALL`, `DOOR` | 0, 1, 2: the cell values the generators write |
| `ALL_CATEGORIES` | 65535: every collider category, the default `mask` of colliders and physics queries |
| `ALL_LAYERS` | Every world layer, 0 to 30: the default `layer_mask` of cameras and lights |
| `PI`, `TAU` | Half a turn and a whole turn, in radians |

## Enums

| Enum | Values |
|------|--------|
| `Schedule` | `Startup`, `Update` |
| `ScaleMode` | `Letterbox`, `Stretch`, `Integer`, `Expand` |
| `Cursor` | `Default`, `Pointer`, `Text`, `Crosshair`, `Move`, `Wait`, `NotAllowed` |
| `Blend` | `Alpha`, `Additive`, `Multiply`, `Screen`, `Premultiplied`, `Opaque` |
| `Filter` | `Nearest`, `Linear` |
| `Align` | `Left`, `Center`, `Right` |
| `VAlign` | `Top`, `Middle`, `Bottom` |
| `BodyKind` | `Static`, `Dynamic`, `Kinematic` |
| `EmitShape` | `Point`, `Circle`, `Ring`, `Box`, `Line` |
| `OccluderShape` | `Box`, `Circle`, `Polygon`, `FromCollider` |
| `NoiseType` | `Value`, `Perlin`, `Simplex` |
| `Ease` | `Linear`, and `In`, `Out` and `InOut` of `Sine`, `Quad`, `Cubic`, `Quart`, `Quint`, `Expo`, `Circ`, `Back`, `Elastic`, `Bounce` (`InOutSine`, ...), and `Spring` |
| `Key` | The letters, `Num0` to `Num9`, `F1` to `F12`, `Return`, `Escape`, `Backspace`, `Tab`, `Space`, `Left`, `Right`, `Up`, `Down`, `LCtrl`, `LShift`, `LAlt`, `RCtrl`, `RShift`, `RAlt` |
| `MouseButton` | `Left`, `Middle`, `Right`, `X1`, `X2` |
| `GamepadButton` | `South`, `East`, `West`, `North`, `Back`, `Start`, `Guide`, `LeftStick`, `RightStick`, `LeftShoulder`, `RightShoulder`, `DpadUp`, `DpadDown`, `DpadLeft`, `DpadRight` |
| `GamepadAxis` | `LeftX`, `LeftY`, `RightX`, `RightY`, `LeftTrigger`, `RightTrigger` |
