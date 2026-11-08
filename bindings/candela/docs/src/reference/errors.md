# Errors

## Compile errors

The candela compiler checks the game before it runs: types, attributes and their arguments. Drift then checks the systems:

| Error | Meaning |
|-------|---------|
| `drift_unknown_parameter` | A system parameter's type is not a query row, tuple list, lookup, resource, message list, contact list, `Commands` or engine resource |
| `drift_not_a_component` | A query row, tuple or lookup names a struct that is not a `@drift::component` or engine component |
| `drift_write_not_read` | `writes:` names a type the system has no parameter for |
| `drift_order_cycle` | `after:` and `before:` make a loop; the message lists it |
| `drift_settings_without_defaults` | A `@drift::settings` struct has a field without a default |

## Errors while running

An error a system raises and does not catch stops that system for the frame: the changes it made are discarded, its commands are not applied, and the log shows the error with the script line. Other systems keep running. In dev builds the system stays off until its script is saved again; in release builds it runs again next frame.

Engine calls raise these kinds, which a script can catch with `try` / `catch "kind"`:

| Kind | Raised by |
|------|-----------|
| `drift_load_failed` | A file that does not exist or does not load |
| `drift_save_failed` | Save data that cannot be read or written |
| `drift_missing_resource` | `cmd.remove_resource` of a resource that is not there |
| `drift_no_such_layer` | A tilemap layer name the map does not have |
| `drift_bad_binding` | An input binding that names nothing |
