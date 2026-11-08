# Text

The `Text` component draws a string at its entity's position, so labels, damage numbers, signs and HUD lines are entities like everything else.

## Showing text

```rust
@drift::system(schedule: drift::Schedule::Startup)
fn setup(cmd: Commands) {
    // A title on the HUD, centered at the top.
    cmd.spawn([at(640.0, 20.0), Text { value: "Level 1", size: 40.0, align: Align::Center,
                                       outline: 2.0, layer: hud_layer(), .. }]);
    // A sign in the world, seen through the camera.
    cmd.spawn([at(300.0, 180.0), Text { value: "SHOP", color: rgb(255, 220, 120),
                                        align: Align::Center, valign: VAlign::Middle, .. }]);
}
```

`align` and `valign` choose which point of the text sits on the position; it is also the point the text rotates around. On `hud_layer()` the position is in screen pixels; on world layers it is in world units.

A `Text` holds up to 511 bytes. For a longer page of dialog, split it across several entities.

## Changing the string

Query the text and assign `value`, with `Text` in `writes:`:

```rust
@drift::component
struct ScoreLabel {}

@drift::system(writes: [Text])
fn show_score(score: Score, labels: (ScoreLabel, Text)[]) {
    for row in labels {
        row.1.value = "Score " + string(score.value);
    }
}
```

## Style

| Field | Effect |
|-------|--------|
| `size` | Pixel height. Raise it for bigger text instead of scaling the `Transform`, so the glyphs stay sharp |
| `color` | Text color |
| `wrap` | Wraps lines at this width; 0 for no wrapping |
| `line_spacing` | Multiplies the distance between lines |
| `outline`, `outline_color` | An outline of this width around each glyph |
| `shadow_offset`, `shadow_color` | A drop shadow; a zero offset turns it off |
| `rich_text` | Reads inline color tags |

With `rich_text: true`, `[color=#rrggbb]` or `[color=#rrggbbaa]` switches the color until the matching `[/color]`. Tags nest, and `[[` writes a literal `[`:

```rust
Text { value: "Press [color=#ffd060]E[/color] to open", rich_text: true, layer: hud_layer(), .. }
```

`Transform.scale` stretches the drawn glyphs, which suits short animations such as a pop-in.

## Fonts

Text uses the built-in font unless `font` names another. `drift::Assets` loads TrueType, OpenType and BMFont files:

```rust
@drift::resource
struct Fonts { title: Font }

@drift::system(schedule: drift::Schedule::Startup)
fn load_fonts(cmd: Commands, assets: drift::Assets) {
    cmd.insert_resource(Fonts { title: assets.font("assets/fonts/title.ttf", 48) });
}
```

TrueType and OpenType fonts stay smooth at any `size`. BMFont files suit pixel-art fonts drawn at their own size.

See [Engine Components](../reference/components.md#drawing) for every field.
