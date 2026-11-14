# Settings and drift.toml

`drift.toml` in the game folder holds the game's settings: its name, the window, physics, input bindings, and your own tuning values. Change a number there and rebuild, without touching code.

```toml
[project]
name = "Coin Dash"
organization = "My Studio"   # with name, where save data lives
entry = "main.cdl"

[window]
width = 960                  # the logical resolution
height = 540
scale_mode = "letterbox"

[render]
clear_color = [0.07, 0.08, 0.12]

[physics]
gravity = [0, 0]

[input]
jump = ["key:Space", "button:South"]

[coin_dash]                  # your own section, read by a @settings struct
hazard_speed = 220.0
```

Every key has a default, so a file holding just `[project]` works. A key a section does not have, or a value of the wrong type, is logged when the game starts.

## Your own settings

A `@settings` struct is a [resource](resources.md) filled from one section:

```rust
@settings(section: "coin_dash")
struct Tuning { hazard_speed: float = 220.0, spawn_every: float = 4.0 }

@system
fn spawn(tuning: Tuning, time: Time, cmd: Commands) {
    // ... tuning.hazard_speed ...
}
```

Field names are the keys. Every field needs a default, used when the file leaves the key out. A system can also change the resource while the game runs, like any other.

## Engine sections

| Section | Keys |
|---------|------|
| `[project]` | `name`, `organization`, `version`, `entry` (the first script, default `main.cdl`), `assets` (the folder packed into the page, default `assets`), `description` and `icon` (an image path) for the web page |
| `[window]` | `title` (empty uses `project.name`), `width`, `height`, `fullscreen`, `resizable`, `high_dpi`, `scale_mode` |
| `[render]` | `backend` (`"gl"` or `"webgpu"`), `vsync`, `clear_color` |
| `[physics]` | `gravity` in pixels per second squared, `[x, y]` with +Y down; `pixels_per_meter`, the physics world's scale, default 32, read at start; see [Physics](../guides/collisions.md#gravity-and-scale) |
| `[audio]` | `master_volume`, `music_volume`, `sfx_volume`, 0 to 1 |
| `[lighting]` | `enabled`, `ambient`, `layer_mask`, `resolution_scale`. The section's presence turns lighting on; see [Particles and Effects](../guides/effects.md#lights) |
| `[input]` | One key per action, a list of bindings; see below |
| `[random]` | `seed` for the [`Random`](../reference/resources.md#random) resource; 0, the default, picks a new seed each run and logs it |
| `[theme]` | The look of the [`drift/ui`](../guides/ui-and-score.md) widgets |
| `[web]` | The web page: `orientation`, `background`, `fullscreen_button`, `touch_controls`, `touch_stick`, `touch_buttons`, `offline`; see [Publishing to the Web](../guides/publishing.md#the-page) |
| `[locale]` | `folder`, `language` (`"auto"` or a code), `fallback`; see [Localization](../guides/localization.md) |
| `[app]` | `threads` for the engine's scheduler: 0 picks for the machine |

Colors are `[r, g, b]` or `[r, g, b, a]` from 0 to 1; vectors are `[x, y]`; names, such as a scale mode, are strings in any case.

## Scale modes

The game draws at the logical `width` and `height`; `scale_mode` decides how that fits the window or the browser canvas:

| Mode | Result |
|------|--------|
| `letterbox` | Keeps the aspect ratio; bars fill the rest. The default |
| `stretch` | Fills the window, distorting the aspect ratio |
| `integer` | Pixel perfect: scales by whole numbers only |
| `expand` | Keeps pixels square and grows the logical size, so wider screens see more |

## Input bindings

Each key of `[input]` is an action, and its value lists bindings in text form:

```toml
[input]
jump = ["key:Space", "key:Up", "button:South"]
fire = ["mouse:Left", "button:RightShoulder"]
throttle = ["axis:+RightTrigger"]
```

| Binding | Means |
|---------|-------|
| `key:<name>` | A key by its name: `key:A`, `key:Space`, `key:Return`, `key:Escape`, `key:Left`, `key:Left Shift` |
| `mouse:<button>` | `Left`, `Middle`, `Right`, `X1`, `X2` |
| `button:<button>` | A gamepad button: `South`, `East`, `West`, `North`, `Back`, `Guide`, `Start`, `LeftStick`, `RightStick`, `LeftShoulder`, `RightShoulder`, `DPadUp`, `DPadDown`, `DPadLeft`, `DPadRight` |
| `axis:+<axis>`, `axis:-<axis>` | One direction of a stick or a trigger: `LeftX`, `LeftY`, `RightX`, `RightY`, `LeftTrigger`, `RightTrigger` |

Systems read actions with `input.action_pressed("jump")` and the like. Axes for movement are bound in code; see [Input](../guides/input.md).

## Overriding settings

Values apply in order: the default, then `drift.toml`, then your systems, then `DRIFT_<SECTION>_<KEY>` environment variables, then `--set section.key=value` on the command line, each over the one before. The last two are for `drift run` on the desktop: `drift run --set window.width=640` checks a smaller window without editing the file.
