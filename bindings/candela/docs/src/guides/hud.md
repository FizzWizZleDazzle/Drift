# HUD and Menus

`drift/ui` is a set of HUD and menu widgets: labels bound to your resources, health and progress bars, icon rows, buttons, toggles, sliders, toasts and typing dialog boxes. Import it next to `drift`, and its systems run the widgets for you:

```rust
import "drift" { * };
import "drift/ui" { * };

@resource
struct Game { score: int = 0, hp: float = 100.0, max_hp: float = 100.0, lives: int = 3 }

@system(schedule: Schedule::Startup)
fn hud(cmd: Commands, theme: Theme) {
    cmd.spawn(plus(label(theme, "", anchor(TOP_LEFT, 16.0, 12.0)), [bind_counter("Game.score", "Score {}", 200.0)]));
    spawn_bar(cmd, theme, anchor(TOP_LEFT, 16.0, 48.0), vec2(220.0, 18.0),
              Bar { path: "Game.hp", max_path: "Game.max_hp", .. });
    cmd.spawn(plus(icon_row(anchor(TOP_RIGHT, -110.0, 14.0), 3, 3), [IconRow { path: "Game.lives", .. }]));
}
```

The score counts up when it changes, the bar follows `Game.hp` with a trail that shows the damage taken, and the hearts follow `Game.lives`, without a system of your own.

Every widget draws on `hud_layer()`: in screen pixels, after the game and its post effects, and not through a camera, so screen shake and zoom leave it alone. A game that does not import `drift/ui` carries none of it.

## Anchors

An `Anchor` keeps an entity at a point of the window plus an offset in pixels, and keeps it there when the window changes size:

```rust
anchor(TOP_RIGHT, -16.0, 16.0)     // 16 pixels in from the top-right corner
Anchor { to: CENTER, offset: vec2(0.0, -80.0) }
```

The points are `TOP_LEFT`, `TOP`, `TOP_RIGHT`, `LEFT`, `CENTER`, `RIGHT`, `BOTTOM_LEFT`, `BOTTOM` and `BOTTOM_RIGHT`, or any `Vec2` from (0, 0) at the top-left to (1, 1) at the bottom-right. Widgets also use the point as their pivot: a label anchored at `TOP_RIGHT` is right-aligned, so it grows to the left.

Positions are in the window's logical pixels. When `drift.toml` keeps a fixed logical size and the page scales it to fit, the HUD scales with the game and stays the same size relative to it; clicks and taps arrive in the same logical pixels.

Phones put notches and rounded corners over the edges. `safe_margin` in the theme keeps every anchored widget that many pixels inside each edge.

For several widgets in a line, `row`, `column` and `grid_layout` return the offsets:

```rust
let spots = column(vec2(0.0, -80.0), 56.0, 3);   // three offsets 56 pixels apart
button(cmd, theme, Anchor { to: CENTER, offset: spots[0] }, "play", "Play", 0);
button(cmd, theme, Anchor { to: CENTER, offset: spots[1] }, "options", "Options", 1);
button(cmd, theme, Anchor { to: CENTER, offset: spots[2] }, "quit", "Quit", 2);
```

## Themes

`Theme` is the look widgets start from: font, text size and color, outline and shadow, panel, button and bar colors, padding, corner radius, widget width and the safe margin. It is a settings resource, so `drift.toml` can set it:

```toml
[theme]
text_size = 26.0
corner_radius = 10.0
accent = { r = 1.0, g = 0.6, b = 0.2, a = 1.0 }
safe_margin = { x = 12.0, y = 12.0 }
```

A widget copies the theme when it is made. For one widget that looks different, pass a changed copy: `theme.sized(40.0)`, `theme.colored(rgb(255, 210, 60))` or `theme.accented(rgb(240, 80, 90))`. To restyle a widget after it is made, change its components (`Text`, `Rectangle`, `Bar`) from a system.

## Labels and bindings

`label(theme, text, anchor)` is a bundle: a `Text` in the theme's style with an `Anchor`. Add components to it with `plus`.

`Bind` fills a label from a field of one of your resources. `path` names the resource and the field, with dots for nested structs, and `format` puts the value where `{}` is:

```rust
cmd.spawn(plus(label(theme, "", anchor(TOP_LEFT, 16.0, 12.0)), [bind("Game.level", "Level {}")]));
cmd.spawn(plus(label(theme, "", anchor(TOP_LEFT, 16.0, 40.0)), [bind_counter("Game.coins", "{} coins", 50.0)]));
```

`bind_counter` counts toward a new number at the given speed, or faster for a large change, instead of jumping. `decimals` sets how many decimals a number shows. `ClockBind` shows a field holding seconds as `m:ss`, or `m:ss.d` with `tenths`, for timers:

```rust
cmd.spawn(plus(label(theme.sized(30.0), "", anchor(TOP, 0.0, 12.0)), [ClockBind { path: "Game.time_left", .. }]));
```

`clock(seconds, tenths)` makes the same text yourself. `resource_number(path)` and `resource_string(path)` read a field by path from any system.

## Panels, bars and icon rows

`panel(theme, anchor, size)` is a rounded rectangle in the theme's panel color, to put behind other widgets.

`spawn_bar(cmd, theme, anchor, size, bar)` makes a bar and returns its entity. A `Bar` fills to `value / max`; set those from a system, or bind them with `path` and `max_path`. The fill slides to a new value (`smooth`, per second; 0 jumps), and after a drop a trail in `trail_color` holds for `trail_delay` seconds, then shrinks to the fill. Colors left unset come from the theme.

```rust
let stamina = spawn_bar(cmd, theme.accented(rgb(90, 170, 255)), anchor(TOP_LEFT, 16.0, 72.0), vec2(160.0, 10.0),
                        Bar { path: "Player.stamina", max: 100.0, trail_delay: 0.0, .. });
```

`icon_row(anchor, count, max)` draws `max` icons with the first `count` lit: hearts, ammo, lives. Set `image` for a texture, or leave it for circles; `full` and `empty` color the lit and unlit ones, and `path` binds the count.

## Buttons, toggles and sliders

```rust
@system(schedule: Schedule::Startup)
fn menu(cmd: Commands, theme: Theme) {
    button(cmd, theme, anchor(CENTER, 0.0, -40.0), "play", "Play", 0);
    toggle(cmd, theme, anchor(CENTER, 0.0, 16.0), "music", "Music", true, 1);
    slider(cmd, theme, anchor(CENTER, 0.0, 72.0), "volume", "Volume", 0.8, 0.0, 1.0, 2);
}

@system
fn on_button(p: ButtonPressed, state: State<Screen>) {
    if p.id == "play" {
        state.set(Screen::Playing);
    }
}

@system
fn on_volume(s: SliderChanged, settings: Options) {
    settings.volume = s.value;
}
```

Each widget answers its entity and sends a message when used: `ButtonPressed { id, button }`, `ToggleChanged { id, on, toggle }` or `SliderChanged { id, value, slider }`.

They work with every input:

- Mouse and touch: hover, press and release over a button; drag a slider's knob.
- Keyboard and gamepad: Up and Down (W and S, the d-pad, the left stick) move the focus through the visible widgets in their `order`; Return, Space or the South button presses; Left and Right (A and D, the d-pad, the stick) move a focused slider.

`UiFocus` holds the focused `order` (-1 until someone navigates). Set `enabled` to false to turn keyboard and gamepad navigation off while the game uses those keys.

## Showing and hiding

`Reveal` fades and slides a label or panel in and out. Set `shown`, and it moves between its anchor offset (shown) and the offset plus `slide` (hidden) over `seconds`, fading as it goes; a hidden widget stops drawing.

```rust
cmd.spawn(plus(label(theme, "Paused", anchor(CENTER, 0.0, 0.0)), [reveal(false, vec2(0.0, -40.0))]));

@system
fn show_pause(rv: Reveal, _: PauseLabel, state: State<Screen>) {
    rv.shown = state.now == Screen::Paused;
}
```

Bars and buttons fade their parts along with their background `Rectangle`, so fading the background fades the whole widget.

Widget animations use the frame time before the time scale, so they keep moving while the game is paused or slowed.

## Toasts

`toast(cmd, theme, message)` shows a line of text under the top of the window for a few seconds. Newer toasts stack below older ones and the rest close up when one goes.

## Dialog boxes

`dialog_box(cmd, theme, id, pages)` opens a box along the bottom of the window that types its pages out. Return, Space, a click or the South button shows the rest of a page at once, then turns to the next; after the last page the box closes and sends `DialogClosed { id, dialog }`.

```rust
dialog_box(cmd, theme, "intro", ["The bridge is out.", "Find another way across."]);
```

`Typewriter` is the typing on its own: put it on any `Text` with the `full` string and a `speed` in characters per second; `finished` turns true when all of it shows.

## Over the world

`FollowWorld { target, offset }` keeps an anchored widget where the camera shows an entity, plus `offset` pixels: names over heads, "Press E" prompts, bars over enemies. The widget goes when the target goes.

```rust
cmd.spawn(plus(label(theme.sized(16.0), "Shopkeeper", anchor(TOP_LEFT, 0.0, 0.0)),
               [FollowWorld { target: npc, offset: vec2(0.0, -40.0) }]));
```

`OffscreenArrow { target }` draws an arrow at the edge of the window pointing at an entity while the camera does not show it; `color`, `size` and `margin` style it.

`Radar { center, range, radius }` on an anchored entity draws a round radar: every entity with a `RadarBlip { color, size }` within `range` world units of `center` shows as a dot.

```rust
cmd.spawn([Transform { .. }, anchor(BOTTOM_LEFT, 90.0, -90.0), Radar { center: player, range: 1200.0, .. }]);
cmd.spawn(plus(enemy(pos), [RadarBlip { .. }]));
```

## Images

`nine_slice_panel(image, border, anchor, size)` frames a panel with an image cut in nine: the corners keep their size and the edges and middle stretch, so one small image frames any size. `NineSlice` is the component, with `frame` for part of an image, `border` for each side, `color` and `z`.

`icon_label(cmd, theme, icon, text, anchor)` puts an icon before a line of text: coins, ammo, keys. Its `IconOf` component sets the icon's `size` and `gap`.

## Parts

A widget made of several shapes (a bar's fill and trail, a button's label, a slider's knob) keeps them as part entities with `PartOf`. Despawn the widget's entity and its parts go with it.

## Limitations

- Buttons, toggles and sliders are rounded rectangles; they do not take nine-slice images or icons yet.
- A HUD layout file that designers edit and that reloads while the game runs is not there yet; widgets are placed from code.
- `safe_margin` is a fixed number; the page does not read the phone's own safe-area insets.
