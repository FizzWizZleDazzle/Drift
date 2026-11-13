# drift/ui

Everything `import "drift/ui" { * };` brings in. The [HUD and Menus](../guides/hud.md) guide shows them together.

## Resources

| Resource | Fields |
|----------|--------|
| `Theme` | Settings from `[theme]`: `font`, `text_size`, `text_color`, `outline`, `outline_color`, `shadow_offset`, `shadow_color`, `panel_color`, `accent`, `button_color`, `button_hover`, `button_pressed`, `button_focus`, `bar_back`, `bar_fill`, `bar_trail`, `padding`, `corner_radius`, `widget_width`, `safe_margin`. Methods: `sized(size)`, `colored(c)`, `accented(c)` and `copy()` return changed copies; `text(value, pivot)` returns a themed `Text` on the HUD |
| `UiFocus` | `index`: the `order` of the widget the keyboard or gamepad is on, -1 for none. `enabled`: false turns keyboard and gamepad navigation off |

## Components

| Component | Fields | Example |
|-----------|--------|---------|
| `Anchor` | `to` (a point of the window, (0, 0) top-left to (1, 1) bottom-right), `offset` (pixels) | `anchor(BOTTOM_RIGHT, -12.0, -10.0)` |
| `Bind` | `path` ("Resource.field"), `format` (`{}` is the value), `decimals`, `count_speed` (0 jumps) | `bind("Game.level", "Level {}")` |
| `ClockBind` | `path` (a field holding seconds), `format`, `tenths` | `ClockBind { path: "Game.time_left", .. }` |
| `Bar` | `value`, `max`, `path`, `max_path`, `fill_color`, `trail_color`, `smooth`, `trail_delay`, `trail_speed`, `inset` | `Bar { path: "Game.hp", max_path: "Game.max_hp", .. }` |
| `IconRow` | `count`, `max`, `path`, `image`, `size`, `spacing`, `full`, `empty` | `icon_row(anchor(TOP_RIGHT, -110.0, 14.0), 3, 3)` |
| `Widget` | `id`, `kind` (`WidgetKind::Button`, `Toggle`, `Slider`), `order`, `on`, `value`, `min`, `max`, `hovered`, `pressed` | Made by `button`, `toggle`, `slider` |
| `Reveal` | `shown`, `slide` (offset when hidden), `seconds` | `reveal(false, vec2(0.0, -40.0))` |
| `Toast` | `seconds`, `age` | Made by `toast` |
| `Typewriter` | `full`, `speed` (characters per second), `shown`, `finished`; `finish()` | `Typewriter { full: "Hello.", speed: 30.0, .. }` |
| `DialogBox` | `id`, `pages`, `page` | Made by `dialog_box` |
| `PartOf` | `owner`: the part goes when the owner goes | `PartOf { owner: e }` |

## Messages

| Message | Sent when |
|---------|-----------|
| `ButtonPressed { id, button }` | A button is clicked, tapped or pressed while focused |
| `ToggleChanged { id, on, toggle }` | A toggle flips |
| `SliderChanged { id, value, slider }` | A slider moves |
| `DialogClosed { id, dialog }` | The last page of a dialog box is turned |

## Functions

| Function | Returns |
|----------|---------|
| `anchor(to, x, y)` | An `Anchor` |
| `label(theme, text, anchor)` | A bundle: themed text at an anchor |
| `panel(theme, anchor, size)` | A bundle: a rounded panel |
| `icon_row(anchor, count, max)` | A bundle: a row of icons |
| `bind(path, format)`, `bind_counter(path, format, speed)` | A `Bind` |
| `spawn_bar(cmd, theme, anchor, size, bar)` | Spawns a bar; its entity |
| `button(cmd, theme, anchor, id, text, order)` | Spawns a button; its entity |
| `toggle(cmd, theme, anchor, id, text, on, order)` | Spawns a toggle; its entity |
| `slider(cmd, theme, anchor, id, text, value, min, max, order)` | Spawns a slider; its entity |
| `toast(cmd, theme, message)` | Shows a toast |
| `dialog_box(cmd, theme, id, pages)` | Spawns a dialog box; its entity |
| `row(start, gap, count)`, `column(start, gap, count)`, `grid_layout(start, cell, columns, count)` | Offsets for widgets in a line or a grid |
| `resource_number(path)`, `resource_string(path)` | A resource field by path, as a number (0 when it is not one) or as text |

## Constants

| Constant | Point |
|----------|-------|
| `TOP_LEFT`, `TOP`, `TOP_RIGHT` | (0, 0), (0.5, 0), (1, 0) |
| `LEFT`, `CENTER`, `RIGHT` | (0, 0.5), (0.5, 0.5), (1, 0.5) |
| `BOTTOM_LEFT`, `BOTTOM`, `BOTTOM_RIGHT` | (0, 1), (0.5, 1), (1, 1) |
