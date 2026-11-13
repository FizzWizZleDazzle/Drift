# Errors

## Compile errors

The candela compiler checks the game before it runs: types, attributes and their arguments. Drift then checks the systems:

| Error | Meaning |
|-------|---------|
| `drift_unknown_parameter` | A system parameter's type is not a component, query row, tuple list, lookup, resource, state, message, contact, `Commands` or engine resource |
| `drift_mixed_singular` | A system takes a single component and a single event, or two single events; it runs once per entity or once per event, not both |
| `drift_unknown_condition` | `run_if:` names a function Drift cannot call |
| `drift_condition_parameter` | A condition takes something other than resources, engine resources and queries |
| `drift_not_a_component` | A query row, tuple or lookup names a struct that is not a `@component` or engine component |
| `drift_order_cycle` | `after:` and `before:` make a loop; the message lists it |
| `drift_settings_without_defaults` | A `@settings` struct has a field without a default |

## Errors while running

An error a system raises and does not catch stops that system for the frame: the changes it made are discarded, its commands are not applied, and the log shows the error with the script line. Other systems keep running. In dev builds the system stays off until its script is saved again; in release builds it runs again next frame.

Engine calls raise these kinds, which a script can catch with `try` / `catch "kind"`:

| Kind | Raised by |
|------|-----------|
| `drift_load_failed` | A file that does not exist or does not load, including a data file that is not valid JSON, TOML or CSV |
| `drift_bad_data` | `parse_json` of text that is not JSON |
| `drift_bad_color` | `hex` of text that is not a color |
| `drift_save_failed` | Save data that cannot be read or written |
| `drift_missing_resource` | `cmd.remove_resource` of a resource that is not there |
| `drift_no_such_layer` | A tilemap layer name the map does not have |
| `drift_bad_binding` | An input binding that names nothing |
