# Assets

Assets are the files a game uses: images, sounds, music and maps. Put them in the `assets` folder of the game; `drift build --web` packs that folder into the page, and paths in scripts are relative to the game folder.

```
my_game/
  assets/
    ship.png
    jump.wav
    theme.ogg
    level1.tmj
```

## Loading

| Call | Loads | Into |
|------|-------|------|
| `texture(path)` | PNG, JPG, BMP, TGA or GIF images | `Texture`, for sprites, particles and tilesets |
| `sound(path)` | WAV, OGG or MP3, decoded at load | `Sound`, for effects |
| `music(path)` | WAV, OGG or MP3, decoded while it plays | `Sound`, for long tracks |
| `assets.tilemap(path)`, `assets.ldtk_level(path, level)` | Tiled and LDtk maps | `Map` |
| `assets.image(path)` | An image file's pixels | `Image`, to change before showing |
| `assets.spritesheet(texture, w, h)` | Cuts a texture into frames | `Spritesheet` |

`texture`, `sound` and `music` work anywhere, field defaults included, and load each path once: asking again hands back the same asset. The usual place for them is a resource that loads everything the game shows:

```rust
@resource
struct Art {
    ship: Texture = texture("assets/ship.png"),
    rock: Texture = texture("assets/rock.png"),
}

@resource
struct Sfx {
    jump: Sound = sound("assets/jump.wav"),
    theme: Sound = music("assets/theme.ogg"),
}

@system(schedule: Schedule::Startup)
fn setup(cmd: Commands, art: Art) {
    cmd.spawn([at(100.0, 100.0), Sprite { image: art.ship, .. }]);
}
```

A file that does not load stops the game with its path, before the first frame when a resource default loads it. In the browser, check the page's console for the message.

## Making assets in code

A game needs no files at all. `Assets` makes textures and sounds from data:

- `assets.create_texture(w, h, pixels)` makes a texture from a list of colors.
- `assets.create_image(w, h, color)` makes an [`Image`](../guides/sprites.md#images): pixels to draw into, then show with `to_texture()`.
- `assets.create_sound(samples, rate)` makes a sound from samples between -1 and 1.
- `assets.create_tilemap(...)` makes an empty map to fill from a grid.
- `assets.render_target(w, h)` makes a texture a camera draws into.

Most examples in this book draw their art this way, so they run from a single script.

## Handles

`Texture`, `Sound`, `Map`, `Image`, `Font` and `Effect` are handles: small values naming something the engine owns. Copy them freely, and keep the ones you reuse in a resource. `Texture {}` is no texture.

Maps and images hold memory until you free them: call `map.destroy()` when a level is replaced and `image.free()` when you are done with its pixels. Textures and sounds stay loaded for the whole run.

See [Assets](../reference/assets.md) in the reference for every method.
