# Scenes and Prefabs

A scene file holds entities and their components as JSON. Build levels and reusable objects (prefabs) as files, spawn them from systems, and save the running world back to a file.

## The file

```json
{
  "drift_scene": 1,
  "entities": [
    {
      "id": 0,
      "components": {
        "Name": { "value": "Player" },
        "Transform2D": { "position": [100, 200] },
        "Sprite": { "texture": "assets/player.png", "origin": [16, 16] },
        "Player": { "speed": 320.0, "pet": 1 }
      }
    },
    {
      "id": 1,
      "components": {
        "Name": { "value": "Sword" },
        "Parent": { "entity": 0 },
        "Transform2D": { "position": [20, 0] }
      }
    }
  ]
}
```

- Components are keyed by name. Yours use their struct name and field names, so `Player` above is the game's `@component struct Player`.
- Engine components use the engine's own names and fields, the ones the C++ side of Drift uses: `Transform2D` for `Transform`, `Velocity2D`, `RigidBody2D`, `BoxCollider2D`, `CircleCollider2D` and `Joint2D` for the physics components, `Shape2D` for `Rectangle` and `Circle`, and `Sprite` with `texture`, `srcRect`, `origin` (in pixels) and `tint` (0 to 255). Save a scene from the running game to see every field of the components you use.
- Fields you leave out keep their defaults, so hand-written files stay short.
- `id` names an entity inside the file. Entity references (`Parent`, `CameraFollow.target`, and `Entity` fields of your own components, such as `pet` above) hold these ids, or `null` for none, and spawning points them at the new entities.
- Asset fields of engine components hold the file path, loaded once however many entities use it. Asset fields of your own components (a `Texture` or `Sound` in a component) are saved as their ids, which mean nothing in another run; keep assets in resources and look them up instead.

Your own components are saved and loaded like the engine's. A field the file names but the struct no longer has is skipped; a field the struct gained since the file was written takes its default.

## Spawning a scene

Load the file through `Assets` and spawn it through `Commands`:

```rust
@system(schedule: Schedule::Startup)
fn load_level(cmd: Commands, assets: Assets) {
    let level = cmd.spawn_scene(assets.scene("assets/level1.json"), SceneOptions { .. });
    if level.find("Player") is Some(player) {
        cmd.spawn([at(0.0, 0.0), Camera { .. }, CameraFollow { target: player, .. }]);
    }
}
```

`spawn_scene` returns a `SceneInstance` at once, with the new entities in file order (`entities`), the ones without a parent in the file (`roots`), and `find(name)` for the first entity with that `Name`. The entities and their components exist for the systems that run after this one.

A path that does not exist logs `Scene: cannot read 'assets/level1.json'` and throws `"drift_load_failed: assets/level1.json"`; catch it with `catch e` and test `e.starts_with("drift_load_failed")`.

## Prefabs

![Four street lamps from one scene file, the third with a red bulb](../images/scenes/prefab.png)

A prefab is a scene file you spawn many times. This one, `assets/lamp.json`, is a lamp post with its bulb as a child; the bulb carries the game's own `Glow` component:

```json
{{#include ../../gallery/scenes/prefab/assets/lamp.json}}
```

The game loads it once and spawns four copies, moving each with `offset` and giving the third a red bulb with an override. [Run it in the browser](../play/gallery/scenes/prefab/).

```rust
{{#include ../../gallery/scenes/prefab/main.cdl:spawn}}
```

<details>
<summary>The whole program</summary>

```rust
{{#include ../../gallery/scenes/prefab/main.cdl:all}}
```

</details>

`SceneOptions` changes each copy:

| Field | Does |
|-------|------|
| `offset` | Moves each root entity by this much |
| `parent` | Makes the roots children of an entity |
| `overrides` | Per entity, by `Name`: a JSON patch of its components |

```rust
@system
fn spawn_waves(cmd: Commands, time: Time, waves: Waves, assets: Assets) {
    if waves.timer.tick(time.dt) == 0 {
        return;
    }
    let bat = assets.scene("assets/bat.json");
    let overrides: {string: string} = {};
    overrides["Bat"] = "{\"Sprite\": {\"tint\": [255, 150, 150, 255]}}";
    cmd.spawn_scene(bat, SceneOptions { offset: vec2(640.0, 80.0), overrides: overrides, .. });
}
```

An override replaces the fields it lists and keeps the rest. Setting a component to `null` leaves it out of that copy.

## Saving

```rust
@system
fn save_level(input: Input, cmd: Commands) {
    if input.key_pressed(Key::F5) {
        cmd.save_scene("level.json");
    }
}
```

`save_scene` writes every entity that has a saved component, after the system returns. `save_prefab(path, root)` writes one entity and its children, which is how you make a prefab from something you built in the running game. A `Parent` pointing outside the saved entities is dropped, so `root` becomes the top of the file.

## Reloading

In dev builds, Drift watches the scene files it loaded. Save one in an editor and the entities spawned from it update in place: they keep their ids, their components follow the file, new entities spawn and removed ones despawn. Spawn options apply again. Physics bodies follow the new collider and body settings, and your components in the file are applied again. The [dev overlay](debugging.md#the-dev-overlay) also has a Scene panel to save, spawn and reload files by hand.
