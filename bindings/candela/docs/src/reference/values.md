# Values and Enums

Plain structs and functions the `drift` module provides. `import "drift" { * };` brings them all in; a name you declare yourself shadows the imported one.

## Structs

| Type | Fields and methods |
|------|--------------------|
| `Vec2` | `x`, `y`. `a + b`, `a - b`, `-a`, `a * s`, `a / s` with a float `s`; `dot`, `length`, `distance`, `normalized`, `lerp(o, t)`, `angle()`, `rotated(radians)`. Built with `vec2(x, y)` |
| `Rect` | `x`, `y`, `w`, `h` (top-left and size). `contains(p)`, `overlaps(r)`, `center()`. Built with `rect(x, y, w, h)` |
| `Color` | `r`, `g`, `b`, `a`, from 0.0 to 1.0. `alpha(a)` returns a copy with a new alpha, `lerp(o, t)` blends. Built with `rgb(r, g, b)` or `rgba(r, g, b, a)` from 0 to 255 |
| `Entity` | An entity's id. Compare with `==`. `Entity {}` is no entity |
| `Cell` | `x`, `y`, ints: a tile position |
| `PlayOptions` | `volume` (1), `pan` (-1 left to 1 right), `pitch` (1), `looping`, `bus` (the sfx bus when unset), `fade_in` (seconds), `priority` (higher keeps its voice when all are busy). For `Sound.play_with` |
| `Timer`, `Cooldown` | See [Timers and Save Data](../guides/timers-and-saves.md) |
| `Texture`, `Sound`, `Font`, `Spritesheet`, `Map`, `Scene` | Assets from `Assets` |

## Functions

| Function | Returns |
|----------|---------|
| `texture(path)` | The `Texture` of an image file (PNG, JPG, BMP, TGA, GIF), loaded once per path |
| `sound(path)` | The `Sound` in a WAV, OGG or MP3 file, loaded once per path |
| `at(x, y)` | A `Transform` at a position |
| `clamp(x, lo, hi)`, `min(a, b)`, `max(a, b)` | Floats held between bounds, the smaller, the larger |
| `tween()` | An empty `Tween` to chain steps onto |
| `ease(e, t)` | Eased progress for `t` from 0 to 1 |
| `hud_layer()` | The screen-space layer, drawn last without a camera |
| `timer(seconds)`, `repeating_timer(seconds)`, `cooldown(seconds)` | Timers |
| `log(message)`, `warn(message)` | Write to the engine log |

## Enums

| Enum | Values |
|------|--------|
| `Schedule` | `Startup`, `Update` |
| `ScaleMode` | `Letterbox`, `Stretch`, `Integer`, `Expand` |
| `Blend` | `Alpha`, `Additive`, `Multiply`, `Screen`, `Premultiplied`, `Opaque` |
| `Filter` | `Nearest`, `Linear` |
| `Align` | `Left`, `Center`, `Right` |
| `VAlign` | `Top`, `Middle`, `Bottom` |
| `BodyKind` | `Static`, `Dynamic`, `Kinematic` |
| `EmitShape` | `Point`, `Circle`, `Ring`, `Box`, `Line` |
| `OccluderShape` | `Box`, `Circle`, `FromCollider` |
| `Ease` | `Linear`, and `In`, `Out` and `InOut` of `Sine`, `Quad`, `Cubic`, `Quart`, `Quint`, `Expo`, `Circ`, `Back`, `Elastic`, `Bounce` (`InOutSine`, ...), and `Spring` |
| `Key` | The letters, `Num0` to `Num9`, `F1` to `F12`, `Return`, `Escape`, `Backspace`, `Tab`, `Space`, `Left`, `Right`, `Up`, `Down`, `LCtrl`, `LShift`, `LAlt`, `RCtrl`, `RShift`, `RAlt` |
| `MouseButton` | `Left`, `Middle`, `Right`, `X1`, `X2` |
| `GamepadButton` | `South`, `East`, `West`, `North`, `Back`, `Start`, `Guide`, `LeftStick`, `RightStick`, `LeftShoulder`, `RightShoulder`, `DpadUp`, `DpadDown`, `DpadLeft`, `DpadRight` |
| `GamepadAxis` | `LeftX`, `LeftY`, `RightX`, `RightY`, `LeftTrigger`, `RightTrigger` |
