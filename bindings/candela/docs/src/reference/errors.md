# Errors

## When the game loads

The Candela compiler checks the game before it runs: syntax, types, attributes and their arguments. Drift then checks the systems and stops with one of these:

| Error | Meaning |
|-------|---------|
| `drift_unknown_parameter` | A system or condition parameter's type is not a component, query row, tuple list, lookup, resource, state, message, collision, `Commands` or engine resource |
| `drift_mixed_singular` | A system takes a single component and a single event, or two single events; it runs once per entity or once per event, not both |
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
| `drift_load_failed` | A map, a music track, a data file or an image file that does not exist or does not load (a data file also when it is not valid JSON, TOML or CSV), and an image read back from a texture that is not a render target |
| `drift_save_failed` | Save data or an image that cannot be written, such as when the browser refuses storage |
| `drift_no_such_layer` | `Map.layer` with a name the map does not have |
| `drift_bad_data` | `parse_json` of text that is not JSON |
| `drift_bad_color` | `hex` of text that is not a color |
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
