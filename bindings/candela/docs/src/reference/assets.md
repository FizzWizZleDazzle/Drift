<!-- Written by scripts/candela-reference.py from the doc comments in
     bindings/candela/drift/src. Edit those, not this page. -->

# Assets

Images, sounds, maps and the handles that name them. A handle such as `Texture` or `Sound`
names something the engine owns; copy it freely, and keep the ones you reuse in a resource.
See [Assets](../concepts/assets.md).

## Texture

An image the engine can draw. Load one with `texture(path)`, make one with
`Assets.create_texture` or `Assets.render_target`, or from an [`Image`](#image). A texture
made in code is freed once nothing uses it: no resource, component or message holds it, no
sprite, particle emitter or camera shows it, and nothing drew it this frame.

```rust
struct Texture {
    id: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `int` | `0` | The engine's handle; `Texture {}` is no texture |

```rust
cmd.spawn([at(0.0, 0.0), Sprite { image: art.ship, .. }]);
```

### Texture.size

```rust
fn size(self) -> Vec2
```

Width and height in pixels.

```rust
let s = art.ship.size();
```

### Texture.set_filter

```rust
fn set_filter(self, f: Filter)
```

How the texture looks when scaled. `Filter::Nearest`, the default, keeps pixel art
crisp; `Filter::Linear` smooths it.

```rust
art.photo.set_filter(Filter::Linear);
```

### Texture.to_image

```rust
fn to_image(self) -> Image
```

The texture's pixels, read back from the GPU: a render target as its last frame left it.
The read waits for the GPU, so do it when you need the pixels, not every frame.

```rust
let img = minimap.to_image();
img.save_png("minimap.png");
```

### Texture.update

```rust
fn update(self, img: Image)
```

Writes an image of the texture's size into it. Cheap enough to do every frame.

```rust
fog_image.fill_rect(rect(px, py, 16.0, 16.0), rgba(0, 0, 0, 0));
fog.update(fog_image);
```

### Texture.update_rect

```rust
fn update_rect(self, img: Image, r: Rect)
```

Writes the part `r` of the image to the same place in the texture.

```rust
fog.update_rect(fog_image, rect(px, py, 16.0, 16.0));
```

## texture

```rust
fn texture(path: string) -> Texture
```

Loads the image file at `path` as a texture.

Works anywhere, field defaults included, and hands back the same texture for the same path.
A file that does not load stops the game with the path and the error. Images load from PNG,
JPG, BMP, TGA and GIF files.

```rust
@resource
struct Art { ship: Texture = texture("assets/ship.png") }
```

## Font

A font for `Text.font`. `Font {}` is the built-in font.

```rust
struct Font {
    id: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `int` | `0` | The engine's handle |

```rust
Text { value: "Hello", font: Font { .. }, .. }
```

### Font.measure

```rust
fn measure(self, s: string, size: float) -> Vec2
```

Widest line and height of `s` in this font at `size`.

### Font.line_height

```rust
fn line_height(self, size: float) -> float
```

The distance from one line's top to the next line's top for this font at `size` pixels.

```rust
let y = top + float(row) * font.line_height(20.0);
```

### Font.set_fallback

```rust
fn set_fallback(self, other: Font)
```

Characters this font lacks come from `other`; every font falls back to the built-in one
until this changes it.

## Sound

A sound the engine can play. Load one with `sound(path)` or make one with
`Assets.create_sound`.

```rust
struct Sound {
    id: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `int` | `0` | The engine's handle |

```rust
sfx.jump.play();
```

### Sound.play

```rust
fn play(self) -> Voice
```

Plays the sound once at full volume and returns its `Voice`.

```rust
sfx.coin.play();
```

### Sound.play_with

```rust
fn play_with(self, o: PlayOptions) -> Voice
```

Plays the sound with a volume, a pitch, or looping.

```rust
sfx.coin.play_with(PlayOptions { volume: 0.6, pitch: 1.2, .. });
```

### Sound.duration

```rust
fn duration(self) -> float
```

Length in seconds.

```rust
let seconds = sfx.speech.duration();
```

### Sound.set_max_instances

```rust
fn set_max_instances(self, n: int)
```

At most `n` of this sound at once; another start fades out the oldest. 0 for no limit.

```rust
sfx.step.set_max_instances(3);
```

## sound

```rust
fn sound(path: string) -> Sound
```

Loads the sound file at `path`.

Works anywhere, field defaults included. The whole file is decoded when it loads.

```rust
@resource
struct Sfx { jump: Sound = sound("assets/jump.wav") }
```

## PlayOptions

How one playback sounds. `bus` empty plays on the sfx bus; `pan` is -1 (left) to 1 (right);
`pitch` 2.0 is an octave up and twice as fast; `fade_in` in seconds. When every voice is
busy, a sound with a higher `priority` takes the place of a lower one.

```rust
struct PlayOptions {
    volume: float = 1.0,
    pitch: float = 1.0,
    looping: bool = false,
    pan: float = 0.0,
    bus: Bus = Bus { .. },
    fade_in: float = 0.0,
    priority: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `volume` | `float` | `1.0` | 0 to 1 |
| `pitch` | `float` | `1.0` | Playback speed; 2 is an octave up |
| `looping` | `bool` | `false` | Plays again when it ends, until stopped |
| `pan` | `float` | `0.0` | -1 left to 1 right |
| `bus` | `Bus` | `Bus { .. }` | The bus it plays on; `Bus {}` is the sfx bus |
| `fade_in` | `float` | `0.0` | Seconds to fade in |
| `priority` | `int` | `0` | Keeps its voice over lower priorities when every voice is busy |

```rust
sfx.engine.play_with(PlayOptions { looping: true, volume: 0.4, .. });
```

## Map

Show a map with a [`Tilemap`](components.md#tilemap) component. Cells are
[`Cell`](procgen.md#cell) values and tile ids count from 1, as in Tiled; 0 is an empty cell.
See [Levels and Tilemaps](../guides/levels-and-tilemaps.md).

```rust
struct Map {
    id: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `int` | `0` | The engine's handle |

```rust
@resource
struct Level { map: Map = Map { .. } }
```

### Map.size

```rust
fn size(self) -> Cell
```

Size in cells.

```rust
let cells = level.map.size();   // Cell { x: columns, y: rows }
```

### Map.tile_size

```rust
fn tile_size(self) -> Cell
```

Size of one cell in pixels.

```rust
let px = level.map.tile_size();
```

### Map.bounds

```rust
fn bounds(self) -> Rect
```

The world rect the map covers where its Tilemap entity puts it.

```rust
cmd.insert(camera, CameraBounds { area: level.map.bounds() });
```

### Map.layer_count

```rust
fn layer_count(self) -> int
```

How many layers the map has.

```rust
for i in range(level.map.layer_count()) { log(level.map.layer_name(i)); }
```

### Map.layer

```rust
fn layer(self, name: string) -> int
```

The index of a layer by name. Raises `drift_no_such_layer` for a name the map does not
have.

```rust
let ground = level.map.layer("Ground");
```

### Map.layer_name

```rust
fn layer_name(self, layer: int) -> string
```

The name of the layer at an index.

```rust
let name = level.map.layer_name(0);
```

### Map.set_layer_visible

```rust
fn set_layer_visible(self, layer: int, visible: bool)
```

Shows or hides a layer.

```rust
level.map.set_layer_visible(level.map.layer("Secrets"), true);
```

### Map.set_layer_solid

```rust
fn set_layer_solid(self, layer: int, solid: bool, values: int[])
```

A solid layer becomes static colliders on its Tilemap entity: every tile (or IntGrid
value) in `values`, or any when `values` is empty.

```rust
level.map.set_layer_solid(level.map.layer("Walls"), true, []);
```

### Map.tile

```rust
fn tile(self, layer: int, c: Cell) -> int
```

The tile id in a cell of a layer; 0 for empty.

```rust
if level.map.tile(ground, c) == 0 { log("a hole"); }
```

### Map.set_tile

```rust
fn set_tile(self, layer: int, c: Cell, id: int)
```

Changes a tile. On a solid layer the colliders follow next frame.

```rust
level.map.set_tile(ground, c, 0);   // dig
```

### Map.int_grid

```rust
fn int_grid(self, layer: int, c: Cell) -> int
```

The value in an LDtk IntGrid layer.

```rust
let v = level.map.int_grid(collision, c);
```

### Map.set_int_grid

```rust
fn set_int_grid(self, layer: int, c: Cell, value: int)
```

Changes an LDtk IntGrid value. LDtk's auto-layer rules do not run again.

```rust
level.map.set_int_grid(collision, c, 1);
```

### Map.tile_at

```rust
fn tile_at(self, layer: int, p: Vec2) -> int
```

The tile id under a world position.

```rust
let under = level.map.tile_at(ground, t.position);
```

### Map.world_to_tile

```rust
fn world_to_tile(self, layer: int, p: Vec2) -> Cell
```

The cell under a world position.

```rust
let c = level.map.world_to_tile(ground, input.mouse_world());
```

### Map.tile_to_world

```rust
fn tile_to_world(self, layer: int, c: Cell) -> Vec2
```

The world position of a cell's top-left corner.

```rust
let p = level.map.tile_to_world(ground, cell(3, 4));
```

### Map.write_grid

```rust
fn write_grid(self, layer: int, g: Grid, tiles: int[], at: Cell)
```

Writes a grid into a tile layer from cell `at`: a cell holding value v becomes tile
tiles[v]; 0, and values past the end of `tiles`, leave the cell empty. The counterpart
of to_grid.

```rust
level.map.write_grid(0, dungeon_grid, [1, 2, 3], cell(0, 0));   // FLOOR -> 1, WALL -> 2, DOOR -> 3
```

### Map.to_grid

```rust
fn to_grid(self, layer: int) -> Grid
```

A tile layer's ids as a grid of the map's size.

```rust
let walls = level.map.to_grid(level.map.layer("Walls"));
let path = find_path(walls, from, to, [3], Moves::FourWay);
```

### Map.property

```rust
fn property(self, name: string) -> string
```

A property of the Tiled map or LDtk level, as text; "" when missing.

```rust
let music = level.map.property("music");
```

### Map.destroy

```rust
fn destroy(self)
```

Frees the map. Despawn its Tilemap entities first.

```rust
cmd.despawn(level.map_entity);
level.map.destroy();
```

### Map.objects

```rust
fn objects(self, kind: string) -> MapObject[]
```

The objects of a Tiled class or LDtk identifier, every layer, in order; "" for all of
them.

### Map.object_property

```rust
fn object_property(self, o: MapObject, name: string) -> string
```

A property of an object, as text; "" when it has none of that name.

### Map.tile_property

```rust
fn tile_property(self, tile: int, name: string) -> string
```

A property of a tile in the map's tilesets, as text.

### Map.autotile

```rust
fn autotile(self, source: int, target: int, rules: AutotileRules)
```

Writes `target` tiles from the terrain in `source`, picking each tile by which
neighbours are terrain too.

## ObjectShape

The shape of a map object, as the map editor drew it. LDtk entities are always `Rect`.

```rust
enum ObjectShape { Rect, Point, Ellipse, Polygon, Polyline, Tile, Text }
```

| Variant | Meaning |
|---------|---------|
| `Rect` | A rectangle; `bounds` is the whole shape |
| `Point` | A single point; `bounds` has zero size |
| `Ellipse` | An ellipse inside `bounds` |
| `Polygon` | A closed polygon through `points` |
| `Polyline` | An open line through `points` |
| `Tile` | A tile placed as an object; `tile` names it |
| `Text` | A Tiled text object |

## MapObject

An object from a Tiled object layer or an LDtk entity, in world coordinates where its map's
Tilemap entity puts it.

```rust
struct MapObject {
    map: Map = Map { .. },
    layer: int = 0,
    index: int = 0,
    id: int = 0,
    name: string = "",
    kind: string = "",
    shape: ObjectShape = ObjectShape::Rect,
    bounds: Rect = Rect { .. },
    position: Vec2 = Vec2 { .. },
    rotation: float = 0.0,
    tile: int = 0,
    points: Vec2[] = [],
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `map` | `Map` | `Map { .. }` | The map it comes from |
| `layer` | `int` | `0` | The index of its layer in the map |
| `index` | `int` | `0` | place in its layer |
| `id` | `int` | `0` | the Tiled object id |
| `name` | `string` | `""` | Tiled name; LDtk identifier |
| `kind` | `string` | `""` | Tiled class; LDtk identifier |
| `shape` | `ObjectShape` | `ObjectShape::Rect` | What kind of shape it is |
| `bounds` | `Rect` | `Rect { .. }` | Its bounding box: top-left and size, in pixels; zero size for a point |
| `position` | `Vec2` | `Vec2 { .. }` | Where it stands in the world, in pixels: LDtk's pivot point, a Tiled tile object's bottom-left; use it as the spawned entity's position |
| `rotation` | `float` | `0.0` | radians |
| `tile` | `int` | `0` | the tile of a Tile object |
| `points` | `Vec2[]` | `[]` | polygon and polyline points |

## AutotileMode

Which neighbours `Map.autotile` looks at to pick a tile.

```rust
enum AutotileMode { Neighbours4, Neighbours8 }
```

| Variant | Meaning |
|---------|---------|
| `Neighbours4` | The four edge neighbours: 16 tiles, one per mask of N, E, S and W |
| `Neighbours8` | All eight neighbours, corners included: 256 tile entries, for blob tilesets |

## AutotileRules

How autotile picks tiles. A source cell is terrain when its value is in `terrain` (empty:
any non-zero value). `tiles` holds a tile id per neighbour mask: 16 for Neighbours4 (bits
N=1 E=2 S=4 W=8), 256 for Neighbours8 (NW=1 N=2 NE=4 W=8 E=16 SW=32 S=64 SE=128; a corner
counts only with both edges beside it). 0 entries use `fallback`.

```rust
struct AutotileRules {
    mode: AutotileMode = AutotileMode::Neighbours4,
    terrain: int[] = [],
    tiles: int[] = [],
    fallback: int = 0,
    edges_are_terrain: bool = false,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `mode` | `AutotileMode` | `AutotileMode::Neighbours4` | Which neighbours count |
| `terrain` | `int[]` | `[]` | The source values that are terrain; empty means any non-zero value |
| `tiles` | `int[]` | `[]` | A tile id per neighbour mask: 16 entries for Neighbours4, 256 for Neighbours8 |
| `fallback` | `int` | `0` | The tile for a mask whose `tiles` entry is 0 |
| `edges_are_terrain` | `bool` | `false` | Cells outside the layer count as terrain, so terrain runs off the map's edge |

## Image

Pixels in memory that you change from code and show through a texture: procedural art, paint
programs, fog of war, minimaps, screenshots. The engine holds the pixels and frees them once
no resource, component or message holds the image; `free()` lets them go at once.

```rust
struct Image {
    id: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `int` | `0` | The engine's handle for the image; 0 is no image |

```rust
@system(schedule: Schedule::Startup)
fn sky(cmd: Commands, assets: Assets) {
    let img = assets.create_image(320, 180, rgb(0, 0, 0));
    img.vertical_gradient(rgb(20, 30, 80), rgb(200, 120, 90));
    cmd.spawn([at(160.0, 90.0), Sprite { image: img.to_texture(), .. }]);
    img.free();
}
```

### Image.width

```rust
fn width(self) -> int
```

Width in pixels.

```rust
let w = img.width();
```

### Image.height

```rust
fn height(self) -> int
```

Height in pixels.

```rust
let h = img.height();
```

### Image.get

```rust
fn get(self, x: int, y: int) -> Color
```

The color of one pixel.

```rust
let c = img.get(3, 4);
```

### Image.set

```rust
fn set(self, x: int, y: int, c: Color)
```

Sets one pixel.

```rust
img.set(3, 4, rgb(255, 0, 0));
```

### Image.fill

```rust
fn fill(self, c: Color)
```

Sets every pixel to one color.

```rust
img.fill(rgba(0, 0, 0, 0));
```

### Image.fill_rect

```rust
fn fill_rect(self, r: Rect, c: Color)
```

Sets the pixels in a rect to one color.

```rust
img.fill_rect(rect(0.0, 0.0, 8.0, 8.0), rgb(255, 255, 255));
```

### Image.fill_circle

```rust
fn fill_circle(self, center: Vec2, radius: float, c: Color)
```

Sets the pixels in a circle to one color, alpha included: a transparent color cuts a
round hole.

```rust
img.fill_circle(vec2(40.0, 40.0), 12.0, rgba(0, 0, 0, 0));
```

### Image.horizontal_gradient

```rust
fn horizontal_gradient(self, left: Color, right: Color)
```

Every pixel from `left` at the left edge to `right` at the right.

```rust
img.horizontal_gradient(rgb(255, 80, 60), rgb(60, 80, 255));
```

### Image.vertical_gradient

```rust
fn vertical_gradient(self, top: Color, bottom: Color)
```

Every pixel from `top` at the top edge to `bottom` at the bottom.

```rust
img.vertical_gradient(rgb(0, 0, 40), rgb(80, 160, 255));
```

### Image.noise

```rust
fn noise(self, n: Noise, low: Color, high: Color)
```

The noise at each pixel, from `low` where it is -1 to `high` where it is 1.

```rust
img.noise(Noise { seed: 3, frequency: 0.05, octaves: 4, .. }, rgb(30, 60, 30), rgb(120, 200, 90));
```

### Image.line

```rust
fn line(self, a: Vec2, b: Vec2, c: Color, thickness: float)
```

Shapes, blended over the pixels by their alpha.

```rust
img.line(vec2(0.0, 0.0), vec2(63.0, 63.0), rgb(255, 255, 255), 1.0);
```

### Image.rect_lines

```rust
fn rect_lines(self, r: Rect, c: Color, thickness: float)
```

A rectangle's outline, blended over the pixels.

```rust
img.rect_lines(rect(0.0, 0.0, 64.0, 64.0), rgb(255, 255, 255), 2.0);
```

### Image.circle

```rust
fn circle(self, center: Vec2, radius: float, c: Color)
```

A filled circle, blended over the pixels.

```rust
img.circle(vec2(32.0, 32.0), 12.0, rgb(255, 200, 60));
```

### Image.circle_lines

```rust
fn circle_lines(self, center: Vec2, radius: float, c: Color, thickness: float)
```

A circle's outline, blended over the pixels.

```rust
img.circle_lines(vec2(32.0, 32.0), 20.0, rgb(255, 255, 255), 2.0);
```

### Image.draw

```rust
fn draw(self, src: Image, x: int, y: int)
```

Another image with its top-left at (x, y), blended by its alpha.

```rust
canvas.draw(stamp, 10, 10);
```

### Image.draw_part

```rust
fn draw_part(self, src: Image, frame: Rect, x: int, y: int)
```

The part `frame` of another image.

```rust
canvas.draw_part(sheet_image, rect(16.0, 0.0, 16.0, 16.0), 0, 0);
```

### Image.paste

```rust
fn paste(self, src: Image, x: int, y: int)
```

Another image's pixels as they are, alpha included.

```rust
canvas.paste(icon, 0, 0);
```

### Image.copy

```rust
fn copy(self) -> Image
```

A new image with the same pixels.

```rust
let backup = img.copy();
```

### Image.crop

```rust
fn crop(self, r: Rect) -> Image
```

A new image of the part `r`.

```rust
let corner = img.crop(rect(0.0, 0.0, 16.0, 16.0));
```

### Image.flip_x

```rust
fn flip_x(self)
```

Mirrors the pixels left to right.

```rust
img.flip_x();
```

### Image.flip_y

```rust
fn flip_y(self)
```

Mirrors the pixels top to bottom.

```rust
img.flip_y();
```

### Image.pixels

```rust
fn pixels(self) -> int[]
```

Every pixel as 0xRRGGBBAA, row by row; see pixel() and color_of().

```rust
let px = img.pixels();
let first = color_of(px[0]);
```

### Image.set_pixels

```rust
fn set_pixels(self, px: int[])
```

Replaces every pixel, row by row, as 0xRRGGBBAA ints.

```rust
img.set_pixels(px);
```

### Image.read_rect

```rust
fn read_rect(self, r: Rect) -> int[]
```

The pixels of a rect, row by row, as 0xRRGGBBAA ints.

```rust
let tile = img.read_rect(rect(16.0, 16.0, 16.0, 16.0));
```

### Image.write_rect

```rust
fn write_rect(self, r: Rect, px: int[])
```

Writes 0xRRGGBBAA pixels into a rect, row by row.

```rust
img.write_rect(rect(0.0, 0.0, 16.0, 16.0), tile);
```

### Image.to_texture

```rust
fn to_texture(self) -> Texture
```

A new texture showing the pixels as they are now.

```rust
let t = img.to_texture();
```

### Image.save_png

```rust
fn save_png(self, path: string)
```

Writes the image to a PNG file. Raises `drift_save_failed` when it cannot.

```rust
img.save_png("drawing.png");
```

### Image.download

```rust
fn download(self, path: string)
```

Saves the image and offers it as a download in the browser; on the desktop the file
stays at `path`.

```rust
img.download("drawing.png");
```

### Image.free

```rust
fn free(self)
```

Releases the pixels now, rather than when nothing holds the image any more. Textures made
from the image stay.

```rust
img.free();
```

## pixel

```rust
fn pixel(c: Color) -> int
```

A color as one int, 0xRRGGBBAA, the form Image.pixels() and Image.write_rect use.

```rust
img.write_rect(rect(0.0, 0.0, 1.0, 1.0), [pixel(rgb(255, 0, 0))]);
```

## color_of

```rust
fn color_of(px: int) -> Color
```

The color of a 0xRRGGBBAA int.

```rust
let c = color_of(img.pixels()[0]);
```

## Spritesheet

A texture cut into a grid of frames. Show one frame with `Sprite.frame`, or play frames with
an [`Animator`](components.md#animator).

```rust
struct Spritesheet {
    id: int = 0,
    texture: Texture = Texture { .. },
    frame_w: float = 0.0,
    frame_h: float = 0.0,
    columns: int = 1,
    rows: int = 1,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `int` | `0` | The engine's sheet |
| `texture` | `Texture` | `Texture { .. }` | The whole image |
| `frame_w` | `float` | `0.0` | Width of one frame in pixels |
| `frame_h` | `float` | `0.0` | Height of one frame in pixels |
| `columns` | `int` | `1` | Frames across |
| `rows` | `int` | `1` | Frames down |

```rust
let sheet = assets.spritesheet(texture("assets/hero.png"), 16.0, 16.0);
cmd.spawn([at(100.0, 100.0), Sprite { image: sheet.texture, frame: sheet.frame_at(0), .. }]);
```

### Spritesheet.frame

```rust
fn frame(self, col: int, row: int) -> Rect
```

The frame at a column and row.

```rust
sprite.frame = sheet.frame(2, 1);
```

### Spritesheet.frame_at

```rust
fn frame_at(self, i: int) -> Rect
```

The frame at an index, counted left to right, top to bottom from 0.

```rust
sprite.frame = sheet.frame_at(int(time.elapsed * 10.0) % sheet.count());
```

### Spritesheet.count

```rust
fn count(self) -> int
```

How many frames the sheet has.

```rust
let n = sheet.count();
```

### Spritesheet.clip

```rust
fn clip(self, name: string) -> AnimationClip
```

An Aseprite tag as a clip: its frames in the order the tag plays them (forward, reverse
or ping-pong), each with its duration from the file. A tag set to repeat once plays
once; any other loops. A tag the file does not have raises `drift_unknown_clip` with the
tags it has.

```rust
let knight = assets.aseprite("assets/knight.aseprite");
let run = knight.clip("run");
```

## Shader

A fragment shader: WGSL for the WebGPU backend and GLSL for OpenGL and the browser's WebGL2.

```rust
struct Shader {
    id: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `int` | `0` | The engine's handle for the shader; 0 is no shader |

## Material

A shader with its settings: eight vec4 parameters and up to three extra textures. Sprites
and post effects use materials.

```rust
struct Material {
    id: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `int` | `0` | The engine's handle for the material; 0 is no material |

### Material.set_param

```rust
fn set_param(self, i: int, x: float, y: float, z: float, w: float)
```

Parameter 0 to 7, `material.params[i]` in the shader.

### Material.set_color

```rust
fn set_color(self, i: int, c: Color)
```

Sets parameter `i`, 0 to 7, to a color: r, g, b and a in x, y, z and w.

### Material.set_texture

```rust
fn set_texture(self, slot: int, t: Texture)
```

Texture slot 1 to 3; slot 0 is what is being drawn.

## Voice

One playback of a sound, from Sound.play. Every method is safe after the sound ended; it
then does nothing.

```rust
struct Voice {
    id: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `int` | `0` | The engine's id of the playing sound; 0 is none. |

```rust
@resource
struct Engine { hum: Voice = Voice { .. } }

@system(schedule: Schedule::Startup)
fn start_engine(sfx: Sfx, engine: Engine) {
    engine.hum = sfx.engine.play_with(PlayOptions { looping: true, volume: 0.4, .. });
}
```

### Voice.stop

```rust
fn stop(self, fade_seconds: float)
```

Stops it, fading out over `fade` seconds.

```rust
engine.hum.stop(0.5);
```

### Voice.pause

```rust
fn pause(self, fade_seconds: float)
```

Pauses it, fading out over `fade` seconds.

```rust
engine.hum.pause(0.2);
```

### Voice.resume

```rust
fn resume(self, fade_seconds: float)
```

Resumes it, fading in over `fade` seconds.

```rust
engine.hum.resume(0.2);
```

### Voice.set_volume

```rust
fn set_volume(self, volume: float, fade_seconds: float)
```

Volume 0 to 1, reached over `fade_seconds`.

```rust
engine.hum.set_volume(0.1, 1.0);
```

### Voice.set_pan

```rust
fn set_pan(self, pan: float)
```

Pan from -1 (left) to 1 (right).

```rust
engine.hum.set_pan(-0.5);
```

### Voice.set_pitch

```rust
fn set_pitch(self, pitch: float)
```

Playback speed; 2 is an octave up.

```rust
engine.hum.set_pitch(1.0 + speed / 400.0);
```

### Voice.set_looping

```rust
fn set_looping(self, looping: bool)
```

Whether it plays again when it ends.

```rust
engine.hum.set_looping(false);
```

### Voice.playing

```rust
fn playing(self) -> bool
```

Until it ends or is stopped; a paused voice counts as playing.

```rust
if !voice.playing() { voice = sfx.alarm.play(); }
```

### Voice.paused

```rust
fn paused(self) -> bool
```

Whether it is paused.

```rust
if engine.hum.paused() { engine.hum.resume(0.2); }
```

## music

```rust
fn music(path: string) -> Sound
```

A long track, decoded while it plays instead of all at load, loaded once per path. Works
anywhere, field defaults included.

```rust
@resource
struct Tracks { theme: Sound = music("assets/theme.ogg") }
```

## Bus

A mixer bus. Buses form a tree under master; volume, mute and pause apply to everything
below.

```rust
struct Bus {
    id: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `int` | `0` | The engine's id of the bus. |

```rust
let voices = audio.bus("voice", Bus { .. });
voices.set_volume(options.voice_volume, 0.0);
```

### Bus.set_volume

```rust
fn set_volume(self, volume: float, fade_seconds: float)
```

Volume 0 to 1, reached over `fade` seconds.

```rust
audio.music_bus().set_volume(0.3, 0.5);
```

### Bus.volume

```rust
fn volume(self) -> float
```

The bus's volume.

```rust
let v = audio.music_bus().volume();
```

### Bus.set_muted

```rust
fn set_muted(self, muted: bool)
```

Mutes or unmutes everything on the bus.

```rust
audio.master_bus().set_muted(true);
```

### Bus.muted

```rust
fn muted(self) -> bool
```

Whether the bus is muted.

```rust
let silent = audio.master_bus().muted();
```

### Bus.set_paused

```rust
fn set_paused(self, paused: bool)
```

Pauses or resumes everything on the bus.

```rust
audio.sfx_bus().set_paused(true);
```

### Bus.paused

```rust
fn paused(self) -> bool
```

Whether the bus is paused.

```rust
if audio.sfx_bus().paused() { log("sounds paused"); }
```

### Bus.stop

```rust
fn stop(self, fade_seconds: float)
```

Stops every sound on this bus and the buses below it.

```rust
audio.sfx_bus().stop(0.2);
```

## Scene

A loaded scene or prefab file.

```rust
struct Scene {
    id: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `int` | `0` | The engine's id of the loaded file. |

## SceneOptions

How one copy of a scene spawns: `offset` moves its root entities, `parent` makes them
children of an entity, and `overrides` maps an entity's Name to a JSON patch of its
components.

```rust
struct SceneOptions {
    offset: Vec2 = Vec2 { .. },
    parent: Entity = Entity { .. },
    overrides: {string: string} = {},
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `offset` | `Vec2` | `Vec2 { .. }` | Added to the position of each root entity, in pixels. |
| `parent` | `Entity` | `Entity { .. }` | The entity the roots become children of; no entity leaves them without a parent. |
| `overrides` | `{string: string}` | `{}` | An entity's Name to a JSON patch of its components, applied to that copy only. |

## SceneInstance

The entities one spawn made, in file order.

```rust
struct SceneInstance {
    entities: Entity[] = [],
    roots: Entity[] = [],
    names: string[] = [],
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `entities` | `Entity[]` | `[]` | Every entity the spawn made, in file order. |
| `roots` | `Entity[]` | `[]` | The entities without a parent in the file. |
| `names` | `string[]` | `[]` | Each entity's Name, at the same index as `entities`; "" for one without a Name. |

### SceneInstance.find

```rust
fn find(self, name: string) -> Option<Entity>
```

The first entity with this Name.
