<!-- Written by scripts/candela-reference.py from the drift module and
     bindings/candela/docs/api-notes.md. Edit those, not this page. -->

# HUD and Menus: drift/ui

The `drift/ui` module: labels, panels, bars, buttons, toggles, sliders, toasts and dialog boxes on the screen layer. Import it beside `drift`:

```rust
import "drift" { * };
import "drift/ui" { * };
```

See [UI and Score](../guides/ui-and-score.md) for a walk through it.

## Theme

The look every widget starts from. Set it once in drift.toml [theme] or from a system; a widget copies what it needs when it is made, so change one widget afterwards through its own components.

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

### Theme.text

```rust
fn text(self, value: string, pivot: Vec2) -> Text
```

Text in this theme. `pivot` picks the alignment, as in Anchor.

## TOP_LEFT

```rust
const TOP_LEFT: Vec2 = Vec2 { x: 0.0, y: 0.0 }
```

Points of the window, for Anchor.to: (0, 0) is the top-left, (1, 1) the bottom-right.

## TOP

```rust
const TOP: Vec2 = Vec2 { x: 0.5, y: 0.0 }
```

## TOP_RIGHT

```rust
const TOP_RIGHT: Vec2 = Vec2 { x: 1.0, y: 0.0 }
```

## LEFT

```rust
const LEFT: Vec2 = Vec2 { x: 0.0, y: 0.5 }
```

## CENTER

```rust
const CENTER: Vec2 = Vec2 { x: 0.5, y: 0.5 }
```

## RIGHT

```rust
const RIGHT: Vec2 = Vec2 { x: 1.0, y: 0.5 }
```

## BOTTOM_LEFT

```rust
const BOTTOM_LEFT: Vec2 = Vec2 { x: 0.0, y: 1.0 }
```

## BOTTOM

```rust
const BOTTOM: Vec2 = Vec2 { x: 0.5, y: 1.0 }
```

## BOTTOM_RIGHT

```rust
const BOTTOM_RIGHT: Vec2 = Vec2 { x: 1.0, y: 1.0 }
```

## Anchor

Keeps the entity at a point of the window plus `offset` pixels, inside the theme's safe margin. Widgets made here also take `to` as their pivot, so a TOP_RIGHT widget hangs from the top-right corner.

```rust
@component
struct Anchor {
    to: Vec2 = Vec2 { .. },
    offset: Vec2 = Vec2 { .. },
}
```

## anchor

```rust
fn anchor(to: Vec2, x: float, y: float) -> Anchor
```

## ui_anchor

```rust
@system
fn ui_anchor(anchor: (Anchor, Transform), win: Window, theme: Theme)
```

A system the module adds: it runs in every game that imports the module.

## row

```rust
fn row(start: Vec2, gap: float, count: int) -> Vec2[]
```

Offsets for `count` widgets in a row, `gap` pixels apart, going right from `start`.

## column

```rust
fn column(start: Vec2, gap: float, count: int) -> Vec2[]
```

The same going down.

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

Fills a Text from a field of a resource: `source` names it, made with resource_field&lt;Game>("score"), and `format` is the text with {} where the value goes. `count_speed` above 0 counts a number up or down to its new value at that many units per second, with at least a quarter second per change, instead of jumping. `decimals` is how many digits a number shows after the point.

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

## bind_text

```rust
fn bind_text<T>(path: string, format: string) -> Bind
```

A Bind showing the field `path` of the resource T: bind_text&lt;Game>("score", "Score {}"). A type that is not a resource, or a field it does not have, raises drift_not_a_resource or drift_no_such_field.

## bind_counter

```rust
fn bind_counter<T>(path: string, format: string, units_per_second: float) -> Bind
```

The same, counting toward a new number at `units_per_second` or faster.

## ui_bind

```rust
@system
fn ui_bind(bind: (Bind, Text), time: Time)
```

A system the module adds: it runs in every game that imports the module.

## ClockBind

A Text that shows a resource field holding seconds as a clock, "m:ss", or "m:ss.d" with `tenths`: ClockBind { source: resource_field&lt;Game>("time_left"), .. }.

```rust
@component
struct ClockBind {
    source: ResourceField = ResourceField { .. },
    format: string = "{}",
    tenths: bool = false,
}
```

## ui_clock

```rust
@system
fn ui_clock(clock_bind: (ClockBind, Text))
```

A system the module adds: it runs in every game that imports the module.

## PartOf

A shape that belongs to a widget: it goes when the widget goes.

```rust
@component
struct PartOf {
    owner: Entity = Entity { .. },
}
```

## ui_parts

```rust
@system
fn ui_parts(part_of: (Entity, PartOf), cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## Bar

A bar that fills to value / max: health, stamina, progress. The fill slides toward a new value at `smooth` per second (0 jumps), and a trail in `trail_color` shows recent damage, shrinking after `trail_delay` seconds. `value_from` and `max_from`, made with resource_field&lt;Game>("hp"), fill value and max from resource fields every frame. `smooth` is a rate per second (0 jumps); `trail_speed` is the share of the bar the trail shrinks per second. Colors left unset come from the theme.

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

## spawn_bar

```rust
fn spawn_bar(cmd: Commands, theme: Theme, at: Anchor, size: Vec2, bar: Bar) -> Entity
```

Spawns a bar `size` pixels at `at` and answers its entity: `spawn_bar(cmd, theme, anchor(TOP_LEFT, 16.0, 48.0), vec2(220.0, 18.0), Bar { value_from: resource_field<Game>("hp"), max_from: resource_field<Game>("max_hp"), .. })`. Change it later through a system that takes its Bar.

## ui_bars

```rust
@system(after: [ui_anchor])
fn ui_bars(row: (Bar, Rectangle, Transform), time: Time, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## IconRow

A row of `max` icons with the first `count` lit: hearts, ammo, lives. Icons are `image` when it is set, circles otherwise. `count_from`, made with resource_field&lt;Game>("lives"), fills count from a resource field.

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

## icon_row

```rust
fn icon_row(at: Anchor, count: int, max: int) -> any[]
```

## ui_icon_rows

```rust
@system(after: [ui_anchor])
fn ui_icon_rows(icon_row: (Entity, IconRow, Transform), cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## ButtonPressed

Sent when a button is clicked, tapped, or activated with Return, Space or the gamepad's South button while focused.

```rust
@message
struct ButtonPressed {
    id: string = "",
    button: Entity = Entity { .. },
}
```

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

## WidgetKind

```rust
enum WidgetKind { Button, Toggle, Slider }
```

## UiFocus

Which widget the keyboard or gamepad is on: the `order` of a Widget, or -1 before anyone navigates. Up/Down (W/S, the d-pad, the left stick) move it; Left/Right change a focused slider.

```rust
@resource
struct UiFocus {
    index: int = -1,
    held: float = 0.0,
    enabled: bool = true,
}
```

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

## button

```rust
fn button(cmd: Commands, theme: Theme, at: Anchor, id: string, text: string, order: int) -> Entity
```

A button with `text`; pressing it sends ButtonPressed with `id`. `order` is its place in keyboard and gamepad navigation.

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

## Reveal

Fades and slides a widget in and out: set `shown` and it moves from `offset + slide` (hidden) to `offset` (shown) over `seconds`, fading its Rectangle or Text. A hidden widget stops drawing.

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

## reveal

```rust
fn reveal(shown: bool, slide: Vec2) -> Reveal
```

## ui_reveal_rects

```rust
@system(before: [ui_anchor])
fn ui_reveal_rects(reveal: (Reveal, Anchor, Rectangle), time: Time)
```

A system the module adds: it runs in every game that imports the module.

## ui_reveal_texts

```rust
@system(before: [ui_anchor])
fn ui_reveal_texts(reveal: (Reveal, Anchor, Text), time: Time)
```

A system the module adds: it runs in every game that imports the module.

## Toast

A message that shows for a few seconds below the top of the window and goes; newer ones stack under older ones.

```rust
@component
struct Toast {
    seconds: float = 2.5,
    age: float = 0.0,
    slot: float = -1.0,
}
```

## toast

```rust
fn toast(cmd: Commands, theme: Theme, message: string)
```

## ui_toasts

```rust
@system(before: [ui_anchor])
fn ui_toasts(toasts: (Entity, Toast, Anchor, Text)[], time: Time, theme: Theme, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## Typewriter

Reveals a Text's `full` string a few characters at a time. `finished` turns true once all of it shows; finish() shows it at once.

```rust
@component
struct Typewriter {
    full: string = "",
    chars_per_second: float = 40.0,
    shown: float = 0.0,
    finished: bool = false,
}
```

### Typewriter.finish

```rust
fn finish(self)
```

## ui_typewriter

```rust
@system
fn ui_typewriter(typewriter: (Typewriter, Text), time: Time)
```

A system the module adds: it runs in every game that imports the module.

## DialogClosed

Sent when the player closes a dialog box.

```rust
@message
struct DialogClosed {
    id: string = "",
    dialog: Entity = Entity { .. },
}
```

## DialogBox

A box along the bottom of the window that types out `pages` one after another. Return, Space, a click or the South button finishes the page, then turns it; after the last page the box closes and sends DialogClosed.

```rust
@component
struct DialogBox {
    id: string = "",
    pages: string[] = [],
    page: int = 0,
    text_part: Entity = Entity { .. },
}
```

## dialog_box

```rust
fn dialog_box(cmd: Commands, theme: Theme, id: string, pages: string[]) -> Entity
```

## ui_dialogs

```rust
@system(after: [ui_anchor])
fn ui_dialogs(dialog_box: (Entity, DialogBox, Rectangle, Transform), input: Input, writers: Lookup<Typewriter>, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## FollowWorld

Keeps an anchored widget over an entity in the world: `offset` pixels from where the camera shows the target. Floating names, prompts and health bars over heads. The widget hides while the target is gone.

```rust
@component
struct FollowWorld {
    target: Entity = Entity { .. },
    offset: Vec2 = Vec2 { x: 0.0, y: -32.0 },
}
```

## screen_point_of

```rust
fn screen_point_of(e: Entity, camera: Camera2D) -> Vec2
```

## ui_follow_world

```rust
@system(before: [ui_anchor])
fn ui_follow_world(follow_world: (Entity, FollowWorld, Anchor), camera: Camera2D, win: Window, theme: Theme, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## OffscreenArrow

An arrow at the edge of the window pointing at `target` while the camera does not show it: the way to the exit, a lost teammate, a boss.

```rust
@component
struct OffscreenArrow {
    target: Entity = Entity { .. },
    color: Color = Color { r: 1.0, g: 0.85, b: 0.3, a: 1.0 },
    size: float = 14.0,
    margin: float = 28.0,
}
```

## ui_offscreen_arrows

```rust
@system
fn ui_offscreen_arrows(offscreen_arrow: (Entity, OffscreenArrow), camera: Camera2D, win: Window, theme: Theme, draw: Draw)
```

A system the module adds: it runs in every game that imports the module.

## Radar

A round radar on the HUD at the anchor: entities with a RadarBlip within `range` world units of `center` show as dots.

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

## RadarBlip

Shows the entity on every Radar.

```rust
@component
struct RadarBlip {
    color: Color = Color { r: 1.0, g: 0.35, b: 0.4, a: 1.0 },
    size: float = 3.0,
}
```

## ui_radars

```rust
@system(after: [ui_anchor])
fn ui_radars(radar: (Radar, Transform), blips: (Entity, RadarBlip)[], draw: Draw)
```

A system the module adds: it runs in every game that imports the module.

## NineSlice

A panel drawn from an image cut in nine: the corners keep their size and the edges and middle stretch, so one small image frames any size. `border` is the width of each edge in the image's pixels; `frame` picks part of the image (zero for all of it).

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

## nine_slice_panel

```rust
fn nine_slice_panel(image: Texture, border: float, at: Anchor, size: Vec2) -> any[]
```

## ui_nine_slices

```rust
@system(after: [ui_anchor])
fn ui_nine_slices(nine_slice: (NineSlice, Anchor, Transform), draw: Draw)
```

A system the module adds: it runs in every game that imports the module.

## icon_label

```rust
fn icon_label(cmd: Commands, theme: Theme, icon: Texture, text: string, at: Anchor) -> Entity
```

An icon and text side by side, as one anchored line: coins, ammo, keys. Answers the text's entity; the icon is a part of it.

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

## ui_icons

```rust
@system(after: [ui_anchor])
fn ui_icons(icon_of: (Entity, IconOf, Text, Transform), cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.
