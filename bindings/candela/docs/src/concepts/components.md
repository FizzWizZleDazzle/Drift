# Components and Entities

An entity is a thing in the game: the player, a coin, a wall, a score label. It is only an id. Components are the data attached to it: where it is, what it looks like, how fast it moves, how much health it has. Games describe their things by the components they combine, and systems act on every entity that has the components they ask for.

## Your own components

Mark a struct with `@component`:

```rust
@component
struct Health { hp: int = 100, max: int = 100 }

@component
struct Enemy {}
```

A component with no fields, like `Enemy`, is a tag: it marks entities so systems and queries can find them.

Give fields defaults where you can. A bundle can then write `Health { .. }` and take them all. A field may be any type a Candela struct holds: numbers, strings, lists, maps, other structs and enums, and engine values such as `Vec2`, `Color`, `Entity`, `Texture` and `Timer`.

A component can bring others with it. With `@component(requires: [Velocity])` on `Bird`, any entity that gets a `Bird` without a `Velocity` gets a default one:

```rust
@component(requires: [Velocity])
struct Bird { flap: float = 420.0 }
```

## Engine components

The engine provides the components it draws and simulates: `Transform`, `Sprite`, `Rectangle`, `Circle`, `Text`, `Velocity`, `RigidBody`, the colliders, `Camera`, `ParticleEmitter`, lights, sound sources and more, listed in [Engine Components](../reference/components.md). Spawn, query and change them the same way as your own. The engine moves and animates entities that have `Velocity`, `Tween`, `Lifetime` and physics bodies by itself, so a system only sets them up.

## Making entities

`Commands` makes entities from a bundle: a list of components, at most one of each type.

```rust
@system(schedule: Schedule::Startup)
fn spawn_enemy(cmd: Commands) {
    let slime = cmd.spawn([
        at(400.0, 120.0),
        Sprite { image: texture("assets/slime.png"), .. },
        Health { hp: 30, max: 30 },
        Enemy {},
    ]);
    log("spawned " + string(slime.id));
}
```

`spawn` returns the new entity's id at once, so later commands in the same system can use it. A bundle is a plain list, so a function can build one and a game can keep its prefabs as functions:

```rust
fn bullet(from: Transform) -> any[] {
    return [
        Transform { position: from.position, rotation: from.rotation, .. },
        Circle { radius: 3.0, .. },
        Velocity { linear: from_angle(from.rotation) * 380.0, .. },
        Lifetime { seconds: 2.0 },
        Bullet {},
    ];
}

cmd.spawn(bullet(t));
cmd.spawn(plus(bullet(t), [Piercing {}]));   // a bundle with extras
```

## Changing entities

`Commands` also adds and removes components, and removes entities:

| Command | Does |
|---------|------|
| `cmd.spawn(bundle)` | Makes an entity and returns its id |
| `cmd.spawn_child(parent, bundle)` | The same, with its `Transform` relative to `parent` |
| `cmd.insert(e, component)` | Adds a component, or replaces the one of its type |
| `cmd.remove<T>(e)` | Takes a component off; nothing happens if `e` has none |
| `cmd.despawn(e)` | Removes the entity and its children |
| `cmd.insert_resource(r)`, `cmd.remove_resource<R>()` | Adds or removes a [resource](resources.md) |
| `cmd.send(message)` | Sends a [message](events.md) |
| `cmd.send_after(seconds, message)` | Sends a message once `seconds` have passed |
| `cmd.restart()` | Starts the whole game over after this frame |

Commands apply when the system that issued them returns, in the order issued, before the next system starts. A system that runs later in the frame sees the new entities; one that ran earlier sees them next frame. Despawning an entity that is already gone does nothing, so two systems may despawn the same entity in one frame.

Assigning to a component a system received changes it directly; that needs no command. See [Systems](systems.md).

## Choosing components

Small components combine better than large ones. A `Health` component works on players, enemies and crates alike, where a `PlayerStats` struct holding health, speed and score only works on players. Systems then ask for exactly the data they use.
