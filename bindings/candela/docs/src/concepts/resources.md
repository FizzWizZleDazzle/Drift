# Resources and Settings

A resource is one value for the whole game, not attached to any entity: the score, the current level, the state of a menu, tuning values. Systems take resources as parameters, the same way they take queries.

## Your own resources

```rust
@resource
struct Score { value: int = 0, best: int = 0 }

@system
fn count(_: Overlap<Player, Coin>, score: Score) {
    score.value += 1;
}
```

A resource whose fields all have defaults exists from the start. Defaults may call functions, so a resource can load its own assets with `texture(path)` and `sound(path)`:

```rust
@resource
struct Art {
    coin: Texture = texture("assets/coin.png"),
    hit: Sound = sound("assets/hit.wav"),
}
```

A file that does not load stops the game at load with the path and the error.

A resource with a field that has no default exists once a system inserts it with `cmd.insert_resource(...)`. A system that takes a resource that does not exist yet is skipped that frame, so inserting and removing a resource switches a group of systems on and off.

## Asset handles

A `Texture`, `Sound`, `Map`, `Effect` or other asset handle names something the engine owns. Its methods act on the engine at once: `art.hit.play()` plays the sound, `effect.set_enabled(false)` turns the effect off.

## Constants

Numbers that never change, such as the size of a sprite or the height of the ground, are constants:

```rust
const GROUND_Y: float = 400.0;
```

Values you tune while playing belong in settings instead.

## Engine resources

The engine's resources are parameters too: `Time`, `Input`, `Window`, `Assets`, `Audio`, `Physics`, `Effects`, `Camera2D`, `Save`, `Draw` and `Systems`. Their methods act on the engine at once. See [Engine Resources](../reference/resources.md).

## Settings

Tuning values belong in `drift.toml`, where you change them without touching code. A `@settings` struct is a resource filled from one section of it:

```toml
[enemies]
speed = 140.0
spawn_every = 2.5
max_alive = 12
```

```rust
@settings(section: "enemies")
struct EnemySettings { speed: float = 120.0, spawn_every: float = 3.0, max_alive: int = 10 }

@system
fn spawn(cfg: EnemySettings, ...) { ... cfg.max_alive ... }
```

Every field needs a default, used when the file leaves the key out. Values come from the default, then `drift.toml`, then a system that changes the resource, then `DRIFT_ENEMIES_SPEED`-style environment variables and `--set enemies.speed=200` on the command line, each over the one before. A value of the wrong type is logged with its line, and the field keeps its value.

While the game runs, saving `drift.toml` applies the keys that changed in the file, so you tune a value and see it at once.

`drift.toml` also holds the engine's own sections: `[project]`, `[window]`, `[render]`, `[app]`, `[physics]`, `[audio]`, `[lighting]` and `[hot_reload]`. See [Projects and the drift Command](../getting-started/projects.md#drifttoml).
