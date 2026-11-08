# Resources and Settings

A resource is one value for the whole game, not attached to any entity: the score, the current level, the state of a menu, tuning values. Systems take resources as parameters, the same way they take queries.

## Your own resources

```rust
@drift::resource
struct Score { value: int = 0, best: int = 0 }

@drift::system(writes: [Score])
fn count(hits: drift::Overlap<Player, Coin>[], score: Score) {
    score.value += hits.len();
}
```

A resource whose fields all have defaults exists from the start. One with fields that need values, such as textures made at startup, exists once a system inserts it:

```rust
@drift::resource
struct Art { coin: Texture, hit: Sound }

@drift::system(schedule: drift::Schedule::Startup)
fn load(cmd: drift::Commands, assets: drift::Assets) {
    cmd.insert_resource(Art { coin: assets.texture("assets/coin.png"), hit: assets.sound("assets/hit.wav") });
}
```

A system that takes a resource that does not exist yet is skipped that frame. Inserting and removing a resource is how you switch a group of systems on and off.

## Asset handles

A `Texture`, `Sound`, `Map`, `Effect` or other asset handle names something the engine owns. Its methods act on the engine at once, so a system that calls `map.set_tile(...)` or `effect.set_enabled(false)` on a handle it holds in a resource does not list that resource in `writes:`. `writes:` is for changing the struct's own fields.

## Engine resources

The engine's resources are parameters too: `drift::Time`, `drift::Input`, `drift::Window`, `drift::Assets`, `drift::Audio`, `drift::Physics`, `drift::Effects`, `drift::Camera2D`, `drift::Save`, `drift::Draw` and `drift::Systems`. Their methods act on the engine at once. See [Engine Resources](../reference/resources.md).

## Settings

Tuning values belong in `drift.toml`, where you change them without touching code. A `@drift::settings` struct is a resource filled from one section of it:

```toml
[enemies]
speed = 140.0
spawn_every = 2.5
max_alive = 12
```

```rust
@drift::settings(section: "enemies")
struct EnemySettings { speed: float = 120.0, spawn_every: float = 3.0, max_alive: int = 10 }

@drift::system
fn spawn(cfg: EnemySettings, ...) { ... cfg.max_alive ... }
```

Every field needs a default, used when the file leaves the key out. Values come from the default, then `drift.toml`, then a system that changes the resource, then `DRIFT_ENEMIES_SPEED`-style environment variables and `--set enemies.speed=200` on the command line, each over the one before. A value of the wrong type is logged with its line, and the field keeps its value.

While the game runs, saving `drift.toml` applies the keys that changed in the file, so you tune a value and see it at once.

`drift.toml` also holds the engine's own sections: `[project]`, `[window]`, `[render]`, `[app]`, `[physics]`, `[audio]`, `[lighting]` and `[hot_reload]`. See [Projects and the drift Command](../getting-started/projects.md#drifttoml).
