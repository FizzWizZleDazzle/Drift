# Reloading While the Game Runs

`drift run` watches your scripts, `drift.toml` and the assets the game loaded. Save a change and the running game picks it up, without restarting and without losing its state.

## Scripts

When a `.cdl` file changes, Drift compiles the game again. If it compiles, the new systems replace the old ones from the next frame, and everything in the world stays: entities, their components, resources, and engine state such as physics bodies and playing sounds.

Data whose struct changed is carried across by field name:

- A field you added takes its default.
- A field you removed is dropped.
- A field whose type changed takes its default, and the log names it.
- A component or resource struct you deleted is removed from every entity.

Startup systems do not run again. To rebuild a level after changing how it is made, restart the game.

If the change does not compile, the error is printed and the game keeps running the last version that did. Fix the error and save again.

## Settings

Saving `drift.toml` applies the keys whose value changed, to the engine's sections and to your `@settings` structs. A few engine keys, such as the window size and the graphics backend, take effect on restart; the log says so.

## Assets

Images, scenes and tilemaps reload in place: everything showing them shows the new version.

## Turning it off

`drift run --no-reload` runs without watching. A game started from a built `.cdlb` file does not watch scripts, because it has none.
