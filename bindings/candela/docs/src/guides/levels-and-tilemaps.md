# Levels and Tilemaps

A level is the ground, walls and things a round is played on. Small levels can be written in the script as text; larger ones are tilemaps, drawn in an editor such as [Tiled](https://www.mapeditor.org/) or [LDtk](https://ldtk.io/), or made by a [generator](procedural-generation.md). A tilemap draws thousands of tiles as one entity, builds colliders from its solid layers, and answers which tile is where.

## Levels written as text

A list of strings, one per row, is a level you can read in the source:

```rust
fn level_rows() -> string[] {
    return [
        "................",
        "..o.o.......F...",
        "..#####...#####.",
        "P...............",
        "################",
    ];
}
```

A startup system walks the rows and spawns what each character stands for. Merge runs of wall characters into one wide collider rather than one per tile: fewer bodies, and nothing for a player to snag on between tiles. The [Platformer](../examples/platformer.md) builds its whole level this way.

## Loading and showing a map

Load the map through `Assets`, keep it in a resource, and put it in the world with a `Tilemap` component:

```rust
@resource
struct Level { map: Map }

@system(schedule: Schedule::Startup)
fn load_level(cmd: Commands, assets: Assets) {
    let map = assets.tilemap("assets/level1.tmj");
    cmd.insert_resource(Level { map: map });
    cmd.spawn([Transform { .. }, Tilemap { map: map, colliders: true, .. }]);
}
```

- Tiled maps load from `.tmj` or `.json` files. Several tilesets, flipped and animated tiles, infinite maps, group layers, layer opacity, tint, offset and parallax all carry over. Only orthogonal maps draw correctly.
- LDtk levels load one at a time: `assets.ldtk_level("world.ldtk", "Level_2")`. IntGrid, auto-layer and tile layers draw; entity layers become map objects.

The map draws at its entity's `Transform`, with `z` and `layer` from the `Tilemap` component. Parallax comes from each layer's settings in the editor.

## Maps made in code

`assets.create_tilemap(width, height, tile_size, tileset, layers)` makes an empty map of tile layers over one tileset image, for levels your systems build, such as [generated](procedural-generation.md) ones. Tile ids count from 1 at the image's top-left, left to right and then down. Fill it tile by tile with `set_tile`, or a whole [grid](procedural-generation.md#grids) at once with `fill`:

```rust
let map = assets.create_tilemap(60, 32, 16, art.tiles, ["Ground", "Walls"]);
map.fill(1, level_grid, [0, 3], cell(0, 0));   // value 0 stays empty, value 1 becomes tile 3
map.set_layer_solid(1, true, [3]);
cmd.spawn([at(0.0, 0.0), Tilemap { map: map, .. }]);
```

`fill(layer, grid, tiles, at)` writes the grid with its top-left at cell `at`; a cell holding value v becomes tile `tiles[v]`, and values past the end of `tiles` leave the cell empty. `to_grid(layer)` reads a layer back as a grid. When a level is replaced, despawn its `Tilemap` entity and call `map.destroy()` to free the old map.

## Layers

Layers are numbered from 0 at the bottom, in both formats. Find one by name:

```rust
let ground = level.map.layer("Ground");
level.map.set_layer_visible(level.map.layer("Secrets"), true);
```

`layer` raises `drift_no_such_layer` for a name the map does not have.

## Tiles and cells

A cell is `Cell { x, y }`. Tile ids count from 1 across the map's tilesets, as in Tiled; 0 is an empty cell.

```rust
@system
fn dig(input: Input, level: Level) {
    if !input.mouse_pressed(MouseButton::Left) {
        return;
    }
    let ground = level.map.layer("Ground");
    let cell = level.map.world_to_tile(input.mouse_world(), ground);
    if level.map.tile(ground, cell) != 0 {
        level.map.set_tile(ground, cell, 0);   // colliders follow next frame
    }
}
```

| Call | Use |
|------|-----|
| `tile(layer, cell)`, `set_tile(layer, cell, id)` | Read and change tiles |
| `int_grid(layer, cell)`, `set_int_grid(layer, cell, value)` | LDtk IntGrid values. Changing a value does not re-run LDtk's auto-layer rules |
| `tile_at(layer, world_pos)` | The tile under a world position |
| `world_to_tile(pos, layer)`, `tile_to_world(cell, layer)` | Convert between world positions and cells; `tile_to_world` gives the top-left |
| `size()`, `tile_size()`, `bounds()` | The map's size in cells, a cell's size in pixels, and its world rect |

Map methods act on the map at once.

## Objects

Tiled object layers and LDtk entity layers are data for your systems. `objects(kind)` lists the objects of one Tiled class or LDtk entity identifier, with their world `bounds` and `anchor` (the pivot point); spawn entities for the ones you care about:

```rust
@system(schedule: Schedule::Startup, after: [load_level])
fn spawn_objects(cmd: Commands, level: Level, art: Art) {   // Art holds the textures, as in Coin Dash
    for o in level.map.objects("Coin") {
        let value = 1;
        let text = level.map.object_property(o, "value");
        if text != "" {
            value = int(text);
        }
        cmd.spawn([
            Transform { position: o.anchor, .. },
            Sprite { image: art.coin, .. },
            CircleCollider { radius: 8.0, sensor: true, .. },
            Coin { value: value },
        ]);
    }
    for o in level.map.objects("PlayerSpawn") {
        cmd.spawn([Transform { position: o.anchor, .. }, Player { .. }]);
    }
}
```

`property(name)` reads a property of the Tiled map or the LDtk level. Properties come back as strings, `""` when missing; convert numbers with `int(...)` or `float(...)`. `tile_property(tile, name)` reads a property set on a tile in Tiled's tileset editor, such as a `damage` value on spike tiles under the player.

Objects also carry their `shape`, `rotation`, the `tile` of a tile object and the `points` of a polygon or polyline, in world positions, so a patrol path drawn as a polyline in Tiled is a list of waypoints:

```rust
for path in level.map.objects("Patrol") {
    cmd.spawn([Transform { position: path.points[0], .. }, Guard { route: path.points, next: 1 }]);
}
```

## Autotiling

`autotile(source, target, rules)` turns terrain into tiles: for every terrain cell of the `source` layer it writes a tile to the `target` layer chosen by which of its neighbours are terrain too, so edges, corners and inner pieces join up. Paint terrain, from code or a generator, and let the rules pick the art:

```rust
let tiles: int[] = [];
for mask in range(16) {
    tiles.push(17 + mask);   // the 16 edge pieces sit in tileset order after tile 17
}
map.autotile(map.layer("Terrain"), map.layer("Ground"), AutotileRules { tiles: tiles, .. });
```

| `AutotileRules` field | Meaning |
|-----------------------|---------|
| `mode` | `AutotileMode::Neighbours4`: 16 tiles indexed by N=1, E=2, S=4, W=8. `Neighbours8`: a 256-entry blob tileset, NW=1, N=2, NE=4, W=8, E=16, SW=32, S=64, SE=128, where a corner counts only when both edges beside it are terrain |
| `terrain` | Source values that are terrain; empty means any non-zero value |
| `tiles` | The tile for each mask; a 0 entry uses `fallback` |
| `edges_are_terrain` | Cells outside the layer count as terrain, so the map's border has no edge pieces |

Run it again after changing the terrain; it rewrites the whole target layer.

## Collision

A layer is solid when:

- it is a Tiled tile layer with the boolean property `collision`, or a name containing "collision";
- it is an LDtk IntGrid or tile layer whose identifier contains "collision";
- you mark it with `set_layer_solid(layer, true, values)`, where `values` lists the tile ids or IntGrid values that count, or is empty for any.

Tiles with collision shapes in Tiled's collision editor are solid on any layer.

With `colliders: true` on the `Tilemap`, the engine merges the solid cells into a few static boxes. Contacts with them name the map entity, so a system taking a `Contact<Player, Tilemap>` runs for each of the player's landings. A solid layer does not have to be visible: a hidden IntGrid layer keeps collision apart from the art.

Changing a tile or IntGrid value on a solid layer rebuilds the colliders on the next frame.

`property(name)` reads a custom property of the Tiled map or the LDtk level as a string, `""` when missing. See [`Map`](../reference/assets.md#map) for every method; the [Roguelike](../examples/roguelike.md) builds a map from a generated dungeon.
