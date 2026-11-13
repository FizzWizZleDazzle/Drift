# Resources

A resource is one value for the whole game, not attached to any entity: the score, the current level, the state of a menu, a random number generator, the textures and sounds the game uses. Systems take resources as parameters, the same way they take components.

## Your own resources

```rust
@resource
struct Score { value: int = 0, best: int = 0 }

@system
fn count(_: Overlap<Player, Coin>, score: Score) {
    score.value += 1;
}
```

A resource whose fields all have defaults exists from the start. Defaults may call functions, so a resource can load its own assets:

```rust
@resource
struct Art {
    coin: Texture = texture("assets/coin.png"),
    hit: Sound = sound("assets/hit.wav"),
}
```

A file that does not load stops the game with the path and the error.

## Resources made by a system

A resource with a field that has no default exists once a system inserts it:

```rust
@resource
struct Level { map: Map, number: int }

@system(schedule: Schedule::Startup)
fn load_level(cmd: Commands, assets: Assets) {
    cmd.insert_resource(Level { map: assets.tilemap("assets/level1.tmj"), number: 1 });
}
```

A system that takes a resource that does not exist is skipped that frame. Inserting and removing a resource therefore switches a group of systems on and off: a `Boss` resource can run the boss systems only while there is a boss.

## Engine resources

The engine's resources are parameters too, and their methods act on the engine at once:

| Resource | Gives |
|----------|-------|
| `Time` | Frame time, elapsed time and the game's speed |
| `Input` | Keyboard, mouse, gamepads, touch and input actions |
| `Window` | The logical size, quitting, screenshots and downloads |
| `Assets` | Textures, sounds, images, sprite sheets and maps made from data or files |
| `Effects` | Particle bursts, screen flashes, fades and shakes, post effects |
| `Audio` | Music, playlists and mixer buses |
| `Save` | Data kept between runs |
| `Random` | The game's seed and a random generator per purpose |
| `Physics` | Gravity, raycasts, overlap tests and pushes |
| `Camera2D` | Converting between screen and world positions |
| `Draw` | Shapes, text and images for one frame |
| `Systems` | Turning systems on and off by name |

See [Engine Resources](../reference/resources.md) for every field and method.

## Asset handles

A `Texture`, `Sound`, `Map`, `Image` or `Effect` names something the engine owns. Copying one copies the name, not the thing, so keep them in resources and components freely. Their methods act on the engine at once: `art.hit.play()` plays the sound. See [Assets](assets.md).

## Constants and settings

Numbers that never change, such as the size of a tile, are constants:

```rust
const TILE: float = 16.0;
```

Values you tune while making the game, such as speeds and spawn rates, belong in `drift.toml`, read by a `@settings` resource; see [Settings and drift.toml](settings.md).
