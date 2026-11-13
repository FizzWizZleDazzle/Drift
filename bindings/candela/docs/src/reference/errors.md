# Errors

## When the game loads

The Candela compiler checks the game before it runs: syntax, types, attributes and their arguments. Drift then checks the systems and stops with one of these:

| Error | Meaning |
|-------|---------|
| `drift_unknown_parameter` | A system or condition parameter's type is not a row, query row, tuple list, lookup, resource, state, message, collision, `Commands` or engine resource, or a row holds something that is not a component or `Entity` |
| `drift_loose_components` | A system takes components or an `Entity` as separate parameters. A system that runs once per entity takes them as one row; the message shows the tuple to write and the `let` that takes it apart |
| `drift_two_rows` | A system takes two rows. It runs once per entity of one row; read the others with a list such as `(Entity, Enemy)[]` |
| `drift_row_and_event` | A system takes a row and a single collision or message. It runs once per entity or once per event, not both; take the events as a list or read the entity with a `Lookup` |
| `drift_two_events` | A system takes two single collisions or messages. It runs once per event of one; take the other as a list |
| `drift_unknown_condition` | `run_if:` names a function Drift cannot find |
| `drift_condition_parameter` | A condition takes something other than resources, engine resources and queries |
| `drift_order_cycle` | `after:` and `before:` make a loop; the message lists the systems in it |
| `drift_settings_without_defaults` | A `@settings` struct has a field without a default |

On the desktop the error prints in the terminal; in the browser it is in the developer console.

## While the game runs

An error a system raises and does not catch stops that system: the changes it made in that call are dropped, its commands are not applied, and the log shows the error. The system stays off for the rest of the run; the other systems keep running.

Engine calls raise errors you can catch with `try` and `catch e`. The text of the error starts with its kind and goes on with what failed, such as `drift_load_failed: assets/level2.tmj`:

| Kind | Raised by |
|------|-----------|
| `drift_load_failed` | A map, a music track, a data file or an image file that does not exist or does not load (a data file also when it is not valid JSON, TOML or CSV), an image read back from a texture that is not a render target, and a sprite sheet cut from a texture with no pixels or with a frame size of 0 |
| `drift_save_failed` | Save data or an image that cannot be written, such as when the browser refuses storage |
| `drift_no_such_layer` | `Map.layer` with a name the map does not have |
| `drift_bad_data` | `parse_json` of text that is not JSON |
| `drift_bad_color` | `hex` of text that is not a color |
| `drift_bad_clip` | `clip` with no frames, a frame outside the sheet, a duration list whose length is neither 1 nor the number of frames, or a sheet not made by `assets.spritesheet` or `assets.aseprite` |
| `host_fn_error` | An image or sound file that `texture` or `sound` cannot load |

```rust
try {
    let map = assets.tilemap(level_path);
    cmd.spawn([at(0.0, 0.0), Tilemap { map: map, .. }]);
} catch e {
    if e.starts_with("drift_load_failed") {
        warn("no level at " + level_path);
    }
}
```

Because the text goes on past the kind, `catch "drift_load_failed"` with a quoted kind does not match these errors; catch them with a name and test the start.

A file that a resource's field default loads, such as `ship: Texture = texture("assets/ship.png")`, is loaded when the game starts, and an error there stops the game before the first frame.

## Names that do not exist

A name written as text, such as a system name or an input action, cannot be checked by the compiler. Drift checks it when it is used and raises an error that names what is wrong and lists what there is, instead of quietly doing nothing:

| Kind | Raised by |
|------|-----------|
| `drift_unknown_system` | `Systems.enable`, `disable` or `enabled` with a name that is not one of the game's systems; the message lists them |
| `drift_unknown_action` | Reading an input action the game never declared in `[input]` or with an `Input.bind` method; the message lists the actions |
| `drift_unknown_clip` | `Animator.play` or `play_once` with a name the animator has no clip for, and `Spritesheet.clip` with a tag the Aseprite file does not have; the message lists the names there are. An unknown name written into `Animator.play` logs the same error |
| `drift_not_a_resource` | `resource_field<T>`, `bind_text<T>`, `bind_counter<T>` or `Debug.watch<T>` with a type that is not a `@resource` or `@settings` struct; the message says what the type is |
| `drift_no_such_field` | The same with a field the resource does not have; the message lists its fields |

These are mistakes in the game rather than failures to recover from, so they stop the system that made them, and the log shows the message.

## Names that changed

Names that read as one thing and did another were renamed, and an old name is now a compile error naming the function or field. The new names:

| Old | New |
|-----|-----|
| `debug.watch("Game.score")` | `debug.watch<Game>("score")` |
| `bind("Game.score", "{}")` | `bind_text<Game>("score", "{}")` |
| `bind_counter("Game.score", "{}", 200.0)` | `bind_counter<Game>("score", "{}", 200.0)` |
| `Bind { path: "Game.score" }`, `ClockBind { path: ... }` | `Bind { source: resource_field<Game>("score") }`, `ClockBind { source: ... }` |
| `Bar { path, max_path }` | `Bar { value_from, max_from }`, each a `resource_field<Game>("hp")` |
| `IconRow { path }` | `IconRow { count_from }` |
| `resource_number(path)`, `resource_string(path)` | `resource_field<Game>("score").number()`, `.text()` |
| `clock(seconds, false)`, `clock(seconds, true)` | `format_clock(seconds)`, `format_clock_tenths(seconds)` |
| `fill(pattern, values)` | `format_named(pattern, values)` |
