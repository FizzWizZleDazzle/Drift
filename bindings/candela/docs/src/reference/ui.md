<!-- Written by scripts/candela-reference.py from the doc comments in
     bindings/candela/drift/src. Edit those, not this page. -->

# HUD and Menus: drift/ui

The `drift/ui` module: labels, panels, bars, buttons, toggles, sliders, toasts and dialog
boxes on the screen layer. Import it beside `drift`:

```rust
import "drift" { * };
import "drift/ui" { * };
```

See [UI and Score](../guides/ui-and-score.md) for a walk through it.

## Theme

The look every widget starts from. Set it once in drift.toml [theme] or from a system; a
widget copies what it needs when it is made, so change one widget afterwards through its own
components.

```rust
@settings(section: "theme")
struct Theme {
    font: Font = Font { .. },
    text_size: float = 22.0,
    text_color: Color = Color { r: 0.93, g: 0.94, b: 0.97, a: 1.0 },
    outline: float = 0.0,
    outline_color: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: 1.0 },
    shadow_offset: Vec2 = Vec2 { x: 0.0, y: 2.0 },
    shadow_color: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: 0.55 },
    panel_color: Color = Color { r: 0.08, g: 0.09, b: 0.14, a: 0.86 },
    accent: Color = Color { r: 0.35, g: 0.67, b: 1.0, a: 1.0 },
    button_color: Color = Color { r: 0.18, g: 0.21, b: 0.32, a: 1.0 },
    button_hover: Color = Color { r: 0.26, g: 0.31, b: 0.46, a: 1.0 },
    button_pressed: Color = Color { r: 0.13, g: 0.15, b: 0.24, a: 1.0 },
    button_focus: Color = Color { r: 0.35, g: 0.67, b: 1.0, a: 1.0 },
    bar_back: Color = Color { r: 0.05, g: 0.06, b: 0.09, a: 0.9 },
    bar_fill: Color = Color { r: 0.36, g: 0.85, b: 0.45, a: 1.0 },
    bar_trail: Color = Color { r: 0.95, g: 0.85, b: 0.4, a: 1.0 },
    padding: Vec2 = Vec2 { x: 16.0, y: 10.0 },
    corner_radius: float = 6.0,
    widget_width: float = 220.0,
    safe_margin: Vec2 = Vec2 { x: 0.0, y: 0.0 },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `font` | `Font` | `Font { .. }` | The font of every label, button and toast; the default is the engine's built-in font. |
| `text_size` | `float` | `22.0` | Text height in pixels. |
| `text_color` | `Color` | `Color { r: 0.93, g: 0.94, b: 0.97, a: 1.0 }` | The color of text. |
| `outline` | `float` | `0.0` | Outline thickness around text in pixels; 0 draws none. |
| `outline_color` | `Color` | `Color { r: 0.0, g: 0.0, b: 0.0, a: 1.0 }` | The color of the text outline. |
| `shadow_offset` | `Vec2` | `Vec2 { x: 0.0, y: 2.0 }` | How far the text's drop shadow sits from the text, in pixels. |
| `shadow_color` | `Color` | `Color { r: 0.0, g: 0.0, b: 0.0, a: 0.55 }` | The color of the text's drop shadow; alpha 0 hides it. |
| `panel_color` | `Color` | `Color { r: 0.08, g: 0.09, b: 0.14, a: 0.86 }` | The fill of panels and dialog boxes. |
| `accent` | `Color` | `Color { r: 0.35, g: 0.67, b: 1.0, a: 1.0 }` | The mark of a toggle that is on and the knob of a slider. |
| `button_color` | `Color` | `Color { r: 0.18, g: 0.21, b: 0.32, a: 1.0 }` | The fill of a button, toggle or slider at rest. |
| `button_hover` | `Color` | `Color { r: 0.26, g: 0.31, b: 0.46, a: 1.0 }` | The fill while the mouse is over the widget or it has the focus. |
| `button_pressed` | `Color` | `Color { r: 0.13, g: 0.15, b: 0.24, a: 1.0 }` | The fill while the left mouse button is held down on the widget. |
| `button_focus` | `Color` | `Color { r: 0.35, g: 0.67, b: 1.0, a: 1.0 }` | The toggle mark and slider knob color while the widget has the keyboard or gamepad focus. |
| `bar_back` | `Color` | `Color { r: 0.05, g: 0.06, b: 0.09, a: 0.9 }` | The background of a bar; its alpha is the bar's full opacity. |
| `bar_fill` | `Color` | `Color { r: 0.36, g: 0.85, b: 0.45, a: 1.0 }` | The fill of a bar whose own `fill_color` is unset. |
| `bar_trail` | `Color` | `Color { r: 0.95, g: 0.85, b: 0.4, a: 1.0 }` | The damage trail of a bar whose own `trail_color` is unset. |
| `padding` | `Vec2` | `Vec2 { x: 16.0, y: 10.0 }` | Space between a widget's text and its edges, in pixels: x on the left and right, y above and below. |
| `corner_radius` | `float` | `6.0` | Corner radius of panels and widgets in pixels; bars use half of it. |
| `widget_width` | `float` | `220.0` | The narrowest a button, toggle or slider is; slider tracks add half. |
| `safe_margin` | `Vec2` | `Vec2 { x: 0.0, y: 0.0 }` | Kept clear on every side of the window: notches and rounded corners on phones. |

### Theme.sized

```rust
fn sized(self, size: float) -> Theme
```

A copy with another text size, for one widget.

### Theme.colored

```rust
fn colored(self, c: Color) -> Theme
```

A copy with another text color.

### Theme.accented

```rust
fn accented(self, c: Color) -> Theme
```

A copy with another accent: bar fill and focus color.

### Theme.copy

```rust
fn copy(self) -> Theme
```

A copy of the theme that shares no fields with it, to change for one widget.

```rust
let big = theme.copy();
big.text_size = 40.0;
big.outline = 2.0;
```

### Theme.text

```rust
fn text(self, value: string, pivot: Vec2) -> Text
```

Text in this theme. `pivot` picks the alignment, as in Anchor.

## TOP_LEFT

```rust
const TOP_LEFT: Vec2 = Vec2 { x: 0.0, y: 0.0 }
```

The top-left corner of the window, for Anchor.to. Window points run from (0, 0) at the
top-left to (1, 1) at the bottom-right.

## TOP

```rust
const TOP: Vec2 = Vec2 { x: 0.5, y: 0.0 }
```

The middle of the window's top edge, for Anchor.to.

## TOP_RIGHT

```rust
const TOP_RIGHT: Vec2 = Vec2 { x: 1.0, y: 0.0 }
```

The top-right corner of the window, for Anchor.to.

## LEFT

```rust
const LEFT: Vec2 = Vec2 { x: 0.0, y: 0.5 }
```

The middle of the window's left edge, for Anchor.to.

## CENTER

```rust
const CENTER: Vec2 = Vec2 { x: 0.5, y: 0.5 }
```

The middle of the window, for Anchor.to.

## RIGHT

```rust
const RIGHT: Vec2 = Vec2 { x: 1.0, y: 0.5 }
```

The middle of the window's right edge, for Anchor.to.

## BOTTOM_LEFT

```rust
const BOTTOM_LEFT: Vec2 = Vec2 { x: 0.0, y: 1.0 }
```

The bottom-left corner of the window, for Anchor.to.

## BOTTOM

```rust
const BOTTOM: Vec2 = Vec2 { x: 0.5, y: 1.0 }
```

The middle of the window's bottom edge, for Anchor.to.

## BOTTOM_RIGHT

```rust
const BOTTOM_RIGHT: Vec2 = Vec2 { x: 1.0, y: 1.0 }
```

The bottom-right corner of the window, for Anchor.to.

## Anchor

Keeps the entity at a point of the window plus `offset` pixels, inside the theme's safe
margin. Widgets made here also take `to` as their pivot, so a TOP_RIGHT widget hangs from
the top-right corner.

```rust
@component
struct Anchor {
    to: Vec2 = Vec2 { .. },
    offset: Vec2 = Vec2 { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `to` | `Vec2` | `Vec2 { .. }` | The point of the window, from (0, 0) at the top-left to (1, 1) at the bottom-right; TOP_LEFT and the other constants name the usual ones. |
| `offset` | `Vec2` | `Vec2 { .. }` | Pixels added to that point; positive x goes right, positive y goes down. |

## anchor

```rust
fn anchor(to: Vec2, x: float, y: float) -> Anchor
```

An Anchor at the window point `to`, moved `x` and `y` pixels from it.

```rust
cmd.spawn(label(theme, "Score 0", anchor(TOP_RIGHT, -16.0, 16.0)));
```

## ui_anchor

```rust
@system
fn ui_anchor(anchor: (Anchor, Transform), win: Window, theme: Theme)
```

A system the module adds: it runs in every game that imports the module.

Moves every anchored entity to its window point each frame, so widgets follow the window's
size.

## row

```rust
fn row(start: Vec2, gap: float, count: int) -> Vec2[]
```

Offsets for `count` widgets in a row, `gap` pixels apart, going right from `start`.

## column

```rust
fn column(start: Vec2, gap: float, count: int) -> Vec2[]
```

Offsets for `count` widgets in a column, `gap` pixels apart, going down from `start`.

## grid_layout

```rust
fn grid_layout(start: Vec2, cell: Vec2, columns: int, count: int) -> Vec2[]
```

Offsets for `count` widgets in rows of `columns`, `cell` apart.

## label

```rust
fn label(theme: Theme, value: string, at: Anchor) -> any[]
```

Text on the HUD.

## panel

```rust
fn panel(theme: Theme, at: Anchor, size: Vec2) -> any[]
```

A rounded panel behind other widgets, `size` pixels.

## Bind

Fills a Text from a field of a resource: `source` names it, made with
resource_field&lt;Game>("score"), and `format` is the text with {} where the value goes.
`count_speed` above 0 counts a number up or down to its new value at that many units per
second, with at least a quarter second per change, instead of jumping. `decimals` is how
many digits a number shows after the point.

```rust
@component
struct Bind {
    source: ResourceField = ResourceField { .. },
    format: string = "{}",
    decimals: int = 0,
    count_speed: float = 0.0,
    shown: float = 0.0,
    started: bool = false,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `source` | `ResourceField` | `ResourceField { .. }` | The resource field to show, made with resource_field&lt;T>(path). |
| `format` | `string` | `"{}"` | The text, with {} where the value goes. |
| `decimals` | `int` | `0` | Digits a number shows after the point. |
| `count_speed` | `float` | `0.0` | Units per second a number counts toward a new value; 0 jumps to it. |
| `shown` | `float` | `0.0` | The number on screen while it counts; Bind keeps it. |
| `started` | `bool` | `false` | Whether the first value has shown; Bind keeps it, so the first value does not count up from 0. |

## bind_text

```rust
fn bind_text<T>(path: string, format: string) -> Bind
```

A Bind showing the field `path` of the resource T: bind_text&lt;Game>("score", "Score {}"). A
type that is not a resource, or a field it does not have, raises drift_not_a_resource or
drift_no_such_field.

## bind_counter

```rust
fn bind_counter<T>(path: string, format: string, units_per_second: float) -> Bind
```

A Bind like `bind_text` that counts toward each new number at `units_per_second` or faster
instead of jumping to it.

## ui_bind

```rust
@system
fn ui_bind(bind: (Bind, Text), time: Time)
```

A system the module adds: it runs in every game that imports the module.

Writes each Bind's resource field into its Text every frame, counting numbers when
`count_speed` is above 0.

## ClockBind

A Text that shows a resource field holding seconds as a clock, "m:ss", or "m:ss.d" with
`tenths`: ClockBind { source: resource_field&lt;Game>("time_left"), .. }.

```rust
@component
struct ClockBind {
    source: ResourceField = ResourceField { .. },
    format: string = "{}",
    tenths: bool = false,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `source` | `ResourceField` | `ResourceField { .. }` | The resource field holding seconds. |
| `format` | `string` | `"{}"` | The text, with {} where the clock goes. |
| `tenths` | `bool` | `false` | Whether the clock shows a digit of tenths of a second. |

## ui_clock

```rust
@system
fn ui_clock(clock_bind: (ClockBind, Text))
```

A system the module adds: it runs in every game that imports the module.

Writes each ClockBind's seconds into its Text as a clock every frame.

## PartOf

A shape that belongs to a widget: it goes when the widget goes.

```rust
@component
struct PartOf {
    owner: Entity = Entity { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `owner` | `Entity` | `Entity { .. }` | The widget the part belongs to; when it is despawned, so is the part. |

## ui_parts

```rust
@system
fn ui_parts(part_of: (Entity, PartOf), cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Despawns every part whose owner is gone.

## Bar

A bar that fills to value / max: health, stamina, progress. The fill slides toward a new
value at `smooth` per second (0 jumps), and a trail in `trail_color` shows recent damage,
shrinking after `trail_delay` seconds. `value_from` and `max_from`, made with
resource_field&lt;Game>("hp"), fill value and max from resource fields every frame. `smooth` is
a rate per second (0 jumps); `trail_speed` is the share of the bar the trail shrinks per
second. Colors left unset come from the theme.

```rust
@component
struct Bar {
    value: float = 1.0,
    max: float = 1.0,
    value_from: ResourceField = ResourceField { .. },
    max_from: ResourceField = ResourceField { .. },
    fill_color: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: -1.0 },
    trail_color: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: -1.0 },
    smooth: float = 6.0,
    trail_delay: float = 0.4,
    trail_speed: float = 1.5,
    inset: float = 2.0,
    shown: float = -1.0,
    trail: float = -1.0,
    hold: float = 0.0,
    last: float = -1.0,
    alpha: float = 1.0,
    fill_part: Entity = Entity { .. },
    trail_part: Entity = Entity { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `value` | `float` | `1.0` | The current amount; the fill is value / max, clamped to 0..1. |
| `max` | `float` | `1.0` | The full amount; 0 or less shows an empty bar. |
| `value_from` | `ResourceField` | `ResourceField { .. }` | A resource field `value` is read from every frame, when set. |
| `max_from` | `ResourceField` | `ResourceField { .. }` | A resource field `max` is read from every frame, when set. |
| `fill_color` | `Color` | `Color { r: 0.0, g: 0.0, b: 0.0, a: -1.0 }` | The fill color; a negative alpha, the default, takes the theme's bar_fill. |
| `trail_color` | `Color` | `Color { r: 0.0, g: 0.0, b: 0.0, a: -1.0 }` | The trail color; a negative alpha, the default, takes the theme's bar_trail. |
| `smooth` | `float` | `6.0` | How fast the fill slides toward a new value, per second; 0 jumps. |
| `trail_delay` | `float` | `0.4` | Seconds the trail waits after a drop before it shrinks. |
| `trail_speed` | `float` | `1.5` | The share of the bar the trail shrinks per second. |
| `inset` | `float` | `2.0` | Pixels between the bar's edge and its fill. |
| `shown` | `float` | `-1.0` | The fill share on screen, 0 to 1; the bar keeps it, -1 until the first frame. |
| `trail` | `float` | `-1.0` | The trail share on screen, 0 to 1; the bar keeps it. |
| `hold` | `float` | `0.0` | Seconds the trail still waits; the bar keeps it. |
| `last` | `float` | `-1.0` | The fill share last frame, to notice drops; the bar keeps it. |
| `alpha` | `float` | `1.0` | The background alpha the bar was made with; fading the background below it fades the fill and trail too. |
| `fill_part` | `Entity` | `Entity { .. }` | The part entity that draws the fill; spawn_bar sets it. |
| `trail_part` | `Entity` | `Entity { .. }` | The part entity that draws the trail; spawn_bar sets it. |

## spawn_bar

```rust
fn spawn_bar(cmd: Commands, theme: Theme, at: Anchor, size: Vec2, bar: Bar) -> Entity
```

Spawns a bar `size` pixels at `at` and answers its entity: `spawn_bar(cmd, theme,
anchor(TOP_LEFT, 16.0, 48.0), vec2(220.0, 18.0), Bar { value_from:
resource_field<Game>("hp"), max_from: resource_field<Game>("max_hp"), .. })`. Change it
later through a system that takes its Bar.

## ui_bars

```rust
@system(after: [ui_anchor])
fn ui_bars(row: (Bar, Rectangle, Transform), time: Time, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Moves every bar's fill and trail toward its value and redraws them.

## IconRow

A row of `max` icons with the first `count` lit: hearts, ammo, lives. Icons are `image` when
it is set, circles otherwise. `count_from`, made with resource_field&lt;Game>("lives"), fills
count from a resource field.

```rust
@component
struct IconRow {
    count: int = 3,
    max: int = 3,
    count_from: ResourceField = ResourceField { .. },
    image: Texture = Texture { .. },
    size: float = 24.0,
    spacing: float = 30.0,
    full: Color = Color { r: 1.0, g: 0.35, b: 0.4, a: 1.0 },
    empty: Color = Color { r: 1.0, g: 1.0, b: 1.0, a: 0.18 },
    parts: Entity[] = [],
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `count` | `int` | `3` | How many icons are lit, from the left. |
| `max` | `int` | `3` | How many icons the row has. |
| `count_from` | `ResourceField` | `ResourceField { .. }` | A resource field `count` is read from every frame, when set. |
| `image` | `Texture` | `Texture { .. }` | The icon image, scaled to `size` pixels wide; no image draws circles. |
| `size` | `float` | `24.0` | The width of one icon in pixels. |
| `spacing` | `float` | `30.0` | Pixels from one icon's left edge to the next. |
| `full` | `Color` | `Color { r: 1.0, g: 0.35, b: 0.4, a: 1.0 }` | The tint of a lit icon. |
| `empty` | `Color` | `Color { r: 1.0, g: 1.0, b: 1.0, a: 0.18 }` | The tint of an unlit icon. |
| `parts` | `Entity[]` | `[]` | One part entity per icon; the row keeps them. |

## icon_row

```rust
fn icon_row(at: Anchor, count: int, max: int) -> any[]
```

The components of an IconRow at `at` with `count` of `max` icons lit, to spawn. The row
starts at the anchor and goes right.

```rust
cmd.spawn(icon_row(anchor(TOP_LEFT, 16.0, 16.0), 3, 5));
```

## ui_icon_rows

```rust
@system(after: [ui_anchor])
fn ui_icon_rows(icon_row: (Entity, IconRow, Transform), cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Keeps one part per icon of each IconRow and colors the first `count` of them as lit.

## ButtonPressed

Sent when a button is clicked, tapped, or activated with Return, Space or the gamepad's
South button while focused.

```rust
@message
struct ButtonPressed {
    id: string = "",
    button: Entity = Entity { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `string` | `""` | The id the button was made with. |
| `button` | `Entity` | `Entity { .. }` | The button's entity. |

## ToggleChanged

Sent when a toggle flips.

```rust
@message
struct ToggleChanged {
    id: string = "",
    on: bool = false,
    toggle: Entity = Entity { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `string` | `""` | The id the toggle was made with. |
| `on` | `bool` | `false` | Whether the toggle is now on. |
| `toggle` | `Entity` | `Entity { .. }` | The toggle's entity. |

## SliderChanged

Sent while a slider moves.

```rust
@message
struct SliderChanged {
    id: string = "",
    value: float = 0.0,
    slider: Entity = Entity { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `string` | `""` | The id the slider was made with. |
| `value` | `float` | `0.0` | The slider's new value, between its min and max. |
| `slider` | `Entity` | `Entity { .. }` | The slider's entity. |

## WidgetKind

Which of the three clickable widgets a Widget is; button(), toggle() and slider() set it.

```rust
enum WidgetKind { Button, Toggle, Slider }
```

| Variant | Meaning |
|---------|---------|
| `Button` | Sends ButtonPressed when clicked or activated. |
| `Toggle` | Flips `on` and sends ToggleChanged when clicked or activated. |
| `Slider` | Changes `value` while dragged, or with Left and Right while focused, and sends SliderChanged; Return, Space and the South button do nothing on it. |

## UiFocus

Which widget the keyboard or gamepad is on: the `order` of a Widget, or -1 before anyone
navigates. Up/Down (W/S, the d-pad, the left stick) move it; Left/Right change a focused
slider.

```rust
@resource
struct UiFocus {
    index: int = -1,
    held: float = 0.0,
    enabled: bool = true,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `index` | `int` | `-1` | The `order` of the focused widget, or -1 while none is. |
| `held` | `float` | `0.0` | Seconds until a held stick moves the focus again; UiFocus keeps it. |
| `enabled` | `bool` | `true` | Whether the keyboard and gamepad move and use the focus; false leaves only the mouse. |

## Widget

What a clickable widget is. Made by button(), toggle() and slider().

```rust
@component
struct Widget {
    id: string = "",
    kind: WidgetKind = WidgetKind::Button,
    order: int = 0,
    on: bool = false,
    value: float = 0.0,
    min: float = 0.0,
    max: float = 1.0,
    step: float = 0.1,
    hovered: bool = false,
    pressed: bool = false,
    dragging: bool = false,
    color: Color = Color { .. },
    hover: Color = Color { .. },
    down: Color = Color { .. },
    focus: Color = Color { .. },
    accent: Color = Color { .. },
    alpha: float = 1.0,
    label_part: Entity = Entity { .. },
    mark_part: Entity = Entity { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `string` | `""` | The id its messages carry. |
| `kind` | `WidgetKind` | `WidgetKind::Button` | Button, toggle or slider. |
| `order` | `int` | `0` | Its place in keyboard and gamepad navigation; lower comes first. |
| `on` | `bool` | `false` | Whether a toggle is on. |
| `value` | `float` | `0.0` | A slider's value. |
| `min` | `float` | `0.0` | A slider's lowest value. |
| `max` | `float` | `1.0` | A slider's highest value. |
| `step` | `float` | `0.1` | A twentieth of a slider's range; slider() sets it, and the module does not read it. |
| `hovered` | `bool` | `false` | Whether the mouse is over it this frame. |
| `pressed` | `bool` | `false` | Whether the left mouse button went down on it and is still held. |
| `dragging` | `bool` | `false` | Whether a slider's knob is being dragged. |
| `color` | `Color` | `Color { .. }` | The fill at rest, from the theme's button_color. |
| `hover` | `Color` | `Color { .. }` | The fill while hovered or focused, from the theme's button_hover. |
| `down` | `Color` | `Color { .. }` | The fill while pressed, from the theme's button_pressed. |
| `focus` | `Color` | `Color { .. }` | The mark or knob color while focused, from the theme's button_focus. |
| `accent` | `Color` | `Color { .. }` | The mark or knob color otherwise, from the theme's accent. |
| `alpha` | `float` | `1.0` | The background alpha it was made with; fading the background below it fades the parts too. |
| `label_part` | `Entity` | `Entity { .. }` | The part entity that draws its text. |
| `mark_part` | `Entity` | `Entity { .. }` | The part entity that draws a toggle's mark or a slider's knob. |

## button

```rust
fn button(cmd: Commands, theme: Theme, at: Anchor, id: string, text: string, order: int) -> Entity
```

A button with `text`; pressing it sends ButtonPressed with `id`. `order` is its place in
keyboard and gamepad navigation.

## toggle

```rust
fn toggle(cmd: Commands, theme: Theme, at: Anchor, id: string, text: string, on: bool, order: int) -> Entity
```

An on/off switch; flipping it sends ToggleChanged.

## slider

```rust
fn slider(cmd: Commands, theme: Theme, at: Anchor, id: string, text: string, value: float, min: float, max: float, order: int) -> Entity
```

A slider from `min` to `max`; moving it sends SliderChanged.

## ui_focus_nav

```rust
@system
fn ui_focus_nav(focus: UiFocus, input: Input, time: Time, widgets: (Widget, Rectangle)[])
```

A system the module adds: it runs in every game that imports the module.

Moves the focus between the visible widgets in `order`.

## ui_widgets

```rust
@system(after: [ui_anchor, ui_focus_nav])
fn ui_widgets(widget: (Entity, Widget, Rectangle, Transform), input: Input, focus: UiFocus, time: Time, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Handles the mouse, the focus and activation of every button, toggle and slider, sends their
messages and redraws them.

## Reveal

Fades and slides a widget in and out: set `shown` and it moves from `offset + slide`
(hidden) to `offset` (shown) over `seconds`, fading its Rectangle or Text. A hidden widget
stops drawing.

```rust
@component
struct Reveal {
    shown: bool = true,
    slide: Vec2 = Vec2 { .. },
    seconds: float = 0.25,
    t: float = -1.0,
    home: Vec2 = Vec2 { .. },
    alpha: float = -1.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `shown` | `bool` | `true` | Whether the widget is shown; change it to start the move. |
| `slide` | `Vec2` | `Vec2 { .. }` | Pixels the widget sits away from its anchor offset while hidden. |
| `seconds` | `float` | `0.25` | How long showing or hiding takes; 0 is at once. |
| `t` | `float` | `-1.0` | Progress from hidden (0) to shown (1); Reveal keeps it, -1 until the first frame. |
| `home` | `Vec2` | `Vec2 { .. }` | The anchor offset when shown, taken on the first frame; Reveal keeps it. |
| `alpha` | `float` | `-1.0` | The full alpha, taken from the widget on the first frame; Reveal keeps it. |

## reveal

```rust
fn reveal(shown: bool, slide: Vec2) -> Reveal
```

A Reveal that starts `shown` or hidden and sits `slide` pixels away while hidden.

```rust
cmd.spawn([Transform { .. }, theme.text("Paused", CENTER), anchor(CENTER, 0.0, 0.0),
           reveal(false, vec2(0.0, 40.0))]);
```

## ui_reveal_rects

```rust
@system(before: [ui_anchor])
fn ui_reveal_rects(reveal: (Reveal, Anchor, Rectangle), time: Time)
```

A system the module adds: it runs in every game that imports the module.

Slides and fades every Rectangle that has a Reveal.

## ui_reveal_texts

```rust
@system(before: [ui_anchor])
fn ui_reveal_texts(reveal: (Reveal, Anchor, Text), time: Time)
```

A system the module adds: it runs in every game that imports the module.

Slides and fades every Text that has a Reveal.

## Toast

A message that shows for a few seconds below the top of the window and goes; newer ones
stack under older ones.

```rust
@component
struct Toast {
    seconds: float = 2.5,
    age: float = 0.0,
    slot: float = -1.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `seconds` | `float` | `2.5` | How long it shows, fading in and out included. |
| `age` | `float` | `0.0` | Seconds it has shown; Toast keeps it. |
| `slot` | `float` | `-1.0` | Its place in the stack, sliding toward a whole number; Toast keeps it. |

## toast

```rust
fn toast(cmd: Commands, theme: Theme, message: string)
```

Shows `message` as a toast for 2.5 seconds, in the theme's text, 72 pixels below the top of
the window. It fades in over 0.2 seconds and out over the last 0.4.

```rust
toast(cmd, theme, "Checkpoint reached");
```

## ui_toasts

```rust
@system(before: [ui_anchor])
fn ui_toasts(toasts: (Entity, Toast, Anchor, Text)[], time: Time, theme: Theme, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Ages, stacks and fades every toast, and despawns the ones whose time is up.

## Typewriter

Reveals a Text's `full` string a few characters at a time. `finished` turns true once all of
it shows; finish() shows it at once.

```rust
@component
struct Typewriter {
    full: string = "",
    chars_per_second: float = 40.0,
    shown: float = 0.0,
    finished: bool = false,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `full` | `string` | `""` | The whole text to reveal. |
| `chars_per_second` | `float` | `40.0` | Characters revealed per second. |
| `shown` | `float` | `0.0` | How many characters show; Typewriter keeps it. |
| `finished` | `bool` | `false` | Whether all of `full` shows. |

### Typewriter.finish

```rust
fn finish(self)
```

Shows the whole text at once and marks it finished.

```rust
if input.key_pressed(Key::Space) { tw.finish(); }
```

## ui_typewriter

```rust
@system
fn ui_typewriter(typewriter: (Typewriter, Text), time: Time)
```

A system the module adds: it runs in every game that imports the module.

Reveals more of every Typewriter's text each frame.

## DialogClosed

Sent when the player closes a dialog box.

```rust
@message
struct DialogClosed {
    id: string = "",
    dialog: Entity = Entity { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `string` | `""` | The id the dialog box was made with. |
| `dialog` | `Entity` | `Entity { .. }` | The dialog box's entity, despawned as the message is sent. |

## DialogBox

A box along the bottom of the window that types out `pages` one after another. Return,
Space, a click or the South button finishes the page, then turns it; after the last page the
box closes and sends DialogClosed.

```rust
@component
struct DialogBox {
    id: string = "",
    pages: string[] = [],
    page: int = 0,
    text_part: Entity = Entity { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `string` | `""` | The id DialogClosed carries. |
| `pages` | `string[]` | `[]` | The text of each page, in order. |
| `page` | `int` | `0` | The page showing, from 0. |
| `text_part` | `Entity` | `Entity { .. }` | The part entity that types the text; dialog_box sets it. |

## dialog_box

```rust
fn dialog_box(cmd: Commands, theme: Theme, id: string, pages: string[]) -> Entity
```

Spawns a dialog box 720 pixels wide, 16 pixels above the bottom of the window, that types
out `pages` in the theme's text, and answers its entity.

```rust
dialog_box(cmd, theme, "intro", ["Welcome, traveller.", "The bridge is out. Find another way."]);
```

## ui_dialogs

```rust
@system(after: [ui_anchor])
fn ui_dialogs(dialog_box: (Entity, DialogBox, Rectangle, Transform), input: Input, writers: Lookup<Typewriter>, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Finishes or turns the page of every dialog box when the player presses Return, Space, the
left mouse button or the gamepad's South button, and closes it after the last page.

## FollowWorld

Keeps an anchored widget over an entity in the world: `offset` pixels from where the camera
shows the target. Floating names, prompts and health bars over heads. The widget hides while
the target is gone.

```rust
@component
struct FollowWorld {
    target: Entity = Entity { .. },
    offset: Vec2 = Vec2 { x: 0.0, y: -32.0 },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `target` | `Entity` | `Entity { .. }` | The entity to stay over; when it is gone the widget is despawned. |
| `offset` | `Vec2` | `Vec2 { x: 0.0, y: -32.0 }` | Pixels from the target's screen position; the default is 32 pixels above it. |

## screen_point_of

```rust
fn screen_point_of(e: Entity, camera: Camera2D) -> Vec2
```

Where the camera shows the entity `e`, in window pixels.

```rust
let p = screen_point_of(boss, camera);
```

## ui_follow_world

```rust
@system(before: [ui_anchor])
fn ui_follow_world(follow_world: (Entity, FollowWorld, Anchor), camera: Camera2D, win: Window, theme: Theme, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Points every FollowWorld widget's Anchor at its target's screen position, and despawns the
widget once the target is gone.

## OffscreenArrow

An arrow at the edge of the window pointing at `target` while the camera does not show it:
the way to the exit, a lost teammate, a boss.

```rust
@component
struct OffscreenArrow {
    target: Entity = Entity { .. },
    color: Color = Color { r: 1.0, g: 0.85, b: 0.3, a: 1.0 },
    size: float = 14.0,
    margin: float = 28.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `target` | `Entity` | `Entity { .. }` | The entity to point at; nothing draws while it is gone. |
| `color` | `Color` | `Color { r: 1.0, g: 0.85, b: 0.3, a: 1.0 }` | The arrow's color. |
| `size` | `float` | `14.0` | The arrow's length in pixels. |
| `margin` | `float` | `28.0` | Pixels between the arrow's tip and the window edge, inside the theme's safe margin. |

## ui_offscreen_arrows

```rust
@system
fn ui_offscreen_arrows(offscreen_arrow: (Entity, OffscreenArrow), camera: Camera2D, win: Window, theme: Theme, draw: Draw)
```

A system the module adds: it runs in every game that imports the module.

Draws an arrow at the window edge for every OffscreenArrow whose target is off the screen.

## Radar

A round radar on the HUD at the anchor: entities with a RadarBlip within `range` world units
of `center` show as dots.

```rust
@component
struct Radar {
    center: Entity = Entity { .. },
    range: float = 600.0,
    radius: float = 64.0,
    color: Color = Color { r: 0.05, g: 0.08, b: 0.12, a: 0.75 },
    ring: Color = Color { r: 0.35, g: 0.67, b: 1.0, a: 0.8 },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `center` | `Entity` | `Entity { .. }` | The entity at the middle of the radar, usually the player; no blips show while it is gone. |
| `range` | `float` | `600.0` | World units from the center to the radar's edge. |
| `radius` | `float` | `64.0` | The radar's radius on the screen, in pixels. |
| `color` | `Color` | `Color { r: 0.05, g: 0.08, b: 0.12, a: 0.75 }` | The radar's fill. |
| `ring` | `Color` | `Color { r: 0.35, g: 0.67, b: 1.0, a: 0.8 }` | The color of the radar's outline and its middle dot. |

## RadarBlip

Shows the entity on every Radar.

```rust
@component
struct RadarBlip {
    color: Color = Color { r: 1.0, g: 0.35, b: 0.4, a: 1.0 },
    size: float = 3.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `color` | `Color` | `Color { r: 1.0, g: 0.35, b: 0.4, a: 1.0 }` | The dot's color. |
| `size` | `float` | `3.0` | The dot's radius in pixels. |

## ui_radars

```rust
@system(after: [ui_anchor])
fn ui_radars(radar: (Radar, Transform), blips: (Entity, RadarBlip)[], draw: Draw)
```

A system the module adds: it runs in every game that imports the module.

Draws every Radar and the RadarBlip entities within its range.

## NineSlice

A panel drawn from an image cut in nine: the corners keep their size and the edges and
middle stretch, so one small image frames any size. `border` is the width of each edge in
the image's pixels; `frame` picks part of the image (zero for all of it).

```rust
@component
struct NineSlice {
    image: Texture = Texture { .. },
    size: Vec2 = Vec2 { x: 200.0, y: 100.0 },
    border: Edges = Edges { left: 8.0, top: 8.0, right: 8.0, bottom: 8.0 },
    frame: Rect = Rect { .. },
    color: Color = Color { .. },
    z: float = 0.0,
    visible: bool = true,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `image` | `Texture` | `Texture { .. }` | The image to cut. |
| `size` | `Vec2` | `Vec2 { x: 200.0, y: 100.0 }` | The panel's size in pixels; the Anchor's `to` is its pivot. |
| `border` | `Edges` | `Edges { left: 8.0, top: 8.0, right: 8.0, bottom: 8.0 }` | The left, top, right and bottom margins in the image, in image pixels. |
| `frame` | `Rect` | `Rect { .. }` | The part of the image to use, in image pixels; zero size uses all of it. |
| `color` | `Color` | `Color { .. }` | A tint; white, the default, leaves the image as it is. |
| `z` | `float` | `0.0` | Draw order on the HUD layer; higher draws on top. |
| `visible` | `bool` | `true` | Whether it draws. |

## nine_slice_panel

```rust
fn nine_slice_panel(image: Texture, border: float, at: Anchor, size: Vec2) -> any[]
```

The components of a NineSlice panel `size` pixels at `at`, with the same `border` in image
pixels on all four sides, to spawn.

```rust
cmd.spawn(nine_slice_panel(art.frame, 8.0, anchor(CENTER, 0.0, 0.0), vec2(320.0, 180.0)));
```

## ui_nine_slices

```rust
@system(after: [ui_anchor])
fn ui_nine_slices(nine_slice: (NineSlice, Anchor, Transform), draw: Draw)
```

A system the module adds: it runs in every game that imports the module.

Draws every visible NineSlice at its anchor.

## icon_label

```rust
fn icon_label(cmd: Commands, theme: Theme, icon: Texture, text: string, at: Anchor) -> Entity
```

An icon and text side by side, as one anchored line: coins, ammo, keys. Answers the text's
entity; the icon is a part of it.

## IconOf

Draws `icon` left of the entity's Text, `size` pixels tall.

```rust
@component
struct IconOf {
    icon: Texture = Texture { .. },
    size: float = 0.0,
    gap: float = 8.0,
    part: Entity = Entity { .. },
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `icon` | `Texture` | `Texture { .. }` | The image to draw. |
| `size` | `float` | `0.0` | The icon's height in pixels; 0 makes it 1.1 times the text size. |
| `gap` | `float` | `8.0` | Pixels between the icon and the text. |
| `part` | `Entity` | `Entity { .. }` | The part entity that draws the icon, made on the first frame; IconOf keeps it. |

## ui_icons

```rust
@system(after: [ui_anchor])
fn ui_icons(icon_of: (Entity, IconOf, Text, Transform), cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Places and draws the icon of every IconOf beside its Text.
