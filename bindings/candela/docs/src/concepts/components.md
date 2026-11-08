# Components

A component is a piece of data attached to an entity: where it is, what it looks like, how fast it moves, how much health it has. An entity is an id with components on it, and nothing else. Games describe their things by the components they combine.

## Your own components

Mark a struct with `@drift::component`:

```rust
@drift::component
struct Health { hp: int = 100, max: int = 100 }

@drift::component
struct Enemy {}
```

A component with no fields, like `Enemy`, is a tag: it marks entities so queries can find them.

Give fields defaults where you can. A spawn bundle written as `Health { .. }` then takes them all, and the scene editor creates the component with them.

Drift stores your components with the entity, inside the engine. That is what lets the inspector show them, scene files save them, and a script reload keep them. A field may be any type a candela struct holds: numbers, strings, lists, maps, other structs and enums, and engine values such as `Vec2`, `Color`, `Entity` and `Texture`.

## Engine components

The engine provides components it draws and simulates: `Transform`, `Sprite`, `Text`, `RigidBody`, `Velocity`, `ParticleEmitter` and more, listed in [Engine Components](../reference/components.md). Spawn, query and change them the same way as your own. The ones that move things (`Velocity`, tweens, `Lifetime`, `Oscillate`, `Follow`, animation) run inside the engine, so the script never loops over them.

## Making entities

Entities are made with a bundle: a list of components, through [Commands](commands.md):

```rust
@drift::system(schedule: drift::Schedule::Startup)
fn spawn_enemy(cmd: drift::Commands, assets: drift::Assets) {
    cmd.spawn([
        at(400.0, 120.0),
        Sprite { image: assets.texture("assets/slime.png"), .. },
        Health { hp: 30, max: 30 },
        Enemy {},
    ]);
}
```

A bundle holds at most one component of each type. Add or replace one later with `cmd.insert(e, component)`, and take one off with `cmd.remove<T>(e)`.

## Choosing components

Small components combine better than large ones. A `Health` component works on players, enemies and crates alike, where a `PlayerStats` struct holding health, speed and score only works on players. Systems then ask for exactly the data they use, which also lets the engine run more of them beside its own work.
