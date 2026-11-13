# Commands

`Commands` changes the world's shape: making and removing entities, adding and removing components, inserting resources and sending messages. A system takes it as a parameter, and Drift applies what it recorded when the system returns, in the order recorded.

```rust
fn fired(input: Input) -> bool => input.action_pressed("fire");

@system(run_if: [fired])
fn fire(_: Gun, t: Transform, cmd: Commands) {
    cmd.spawn(bullet(t));
}

// A bundle is a list of components, so a function can build one.
fn bullet(t: Transform) -> any[] {
    return [
        Transform { position: t.position, rotation: t.rotation, .. },
        Sprite { image: texture("assets/bullet.png"), .. },
        Velocity { linear: vec2(600.0, 0.0).rotated(t.rotation), .. },
        Lifetime { seconds: 2.0 },
        Bullet {},
    ];
}
```

Commands change the world's shape; assigning to a parameter changes the data the system received.

## Methods

| Method | Does |
|--------|------|
| `spawn(bundle) -> Entity` | Makes an entity with the components in the list. The id is valid at once: use it in later commands of the same system |
| `spawn_child(parent, bundle) -> Entity` | The same, with its `Transform` relative to `parent` |
| `set_parent(child, parent)` | Moves `child` under `parent`, keeping its `Transform`'s numbers, so they now count from the parent |
| `set_parent_in_place(child, parent)` | Moves `child` under `parent` where it is on screen: its `Transform` is rewritten to match |
| `detach(child)` | Makes `child` a root again, where it is on screen |
| `insert(e, component)` | Adds a component, or replaces the one of its type |
| `remove<T>(e)` | Takes a component off; nothing happens if `e` has none |
| `despawn(e)` | Removes the entity and its children |
| `insert_resource(r)` | Adds a resource, or replaces it |
| `remove_resource<R>()` | Removes a resource; systems that take it stop running |
| `send(message)` | Sends a message to the systems that read its type; see [Messages](messages.md) |
| `spawn_scene(scene, options) -> SceneInstance` | Spawns a copy of a scene or prefab; see [Scenes and Prefabs](../guides/scenes.md) |
| `restart()` | Starts the game over once this frame's systems have run: every entity the game spawned is despawned, resources and states return to their defaults, and the startup systems run again |

## When they apply

Commands apply as soon as their system returns, before the next system starts. A system ordered after this one sees the new entities in its queries; one that runs before it this frame sees them next frame. The engine sees them at once too: a spawned body joins the physics world at the next step.

Despawning an entity that is already gone does nothing, so two systems may despawn the same entity in one frame.
