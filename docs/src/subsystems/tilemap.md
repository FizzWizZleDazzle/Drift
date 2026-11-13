# Tilemap

`TilemapResource` loads levels made in [Tiled](https://www.mapeditor.org/) and [LDtk](https://ldtk.io/) or built in code, draws them, answers tile queries, and turns their collision layers and objects into game entities. Put a map in the world with a `Tilemap` component and the `TilemapPlugin` handles drawing, colliders and object spawning.

**Headers:** `#include <drift/resources/TilemapResource.hpp>`, `#include <drift/components/Tilemap.hpp>`

**Plugin:** `TilemapPlugin` (included in `DefaultPlugins`)

## Quick Start

```cpp
void setup(ResMut<TilemapResource> maps, Commands& cmd) {
    // Turn "Coin" objects (Tiled) or entities (LDtk) into game entities.
    maps->addObjectSpawner("Coin", [](const MapObject& o, const ObjectSpawnContext& ctx) {
        ctx.commands.spawn().insert(Transform2D{.position = ctx.position},
                                    Coin{o.properties.getInt("value", 1)});
    });

    TilemapHandle level = maps->loadTilemap("assets/level1.tmj");
    cmd.spawn().insert(Transform2D{}, Tilemap{.map = level});
}
```

On the frame after the entity spawns, the plugin builds static colliders from the map's collision layers and calls the spawners for every object. From then on it draws the map every frame.

## Loading

```cpp
TilemapHandle loadTilemap(const char* path, const char* level = nullptr);
std::vector<TilemapHandle> loadLevels(const char* path);
void destroyTilemap(TilemapHandle map);
```

The format follows the file extension: `.ldtk` loads LDtk, anything else loads Tiled JSON. Image paths inside the file resolve relative to the file. Textures are shared between maps that use the same image.

- **Tiled:** `loadTilemap("level.tmj")`. One handle per map.
- **LDtk:** each level is its own map. `loadTilemap("world.ldtk", "Level_2")` loads one level by identifier (the first level when `level` is null); `loadLevels("world.ldtk")` loads all of them. Each level keeps its LDtk world position, see `worldOffset()`.

### What Tiled maps can contain

- Several tilesets per map, embedded or external JSON (`.tsj`). Atlas tilesets (with margin and spacing) and image-collection tilesets (one image per tile). Tiles larger than the grid are drawn bottom-aligned, as in Tiled; tile offsets and "tile render size: grid" are honoured.
- Flipped and rotated tiles (the horizontal, vertical and diagonal flip bits).
- Animated tiles from the tileset editor.
- Tile layer data as CSV or base64 (uncompressed, zlib, gzip).
- Finite and infinite (chunked) maps; infinite maps may use negative coordinates.
- Tile, object, image and group layers. Group layers are flattened: their offset, opacity, tint, visibility and parallax combine with their children's.
- Layer opacity, tint color, offset and parallax factor (with the map's parallax origin).
- Custom properties on the map, layers, tiles and objects.

Only orthogonal maps are drawn correctly. Object templates and zstd-compressed layers are not supported.

### What LDtk projects can contain

- IntGrid layers: the values (see `getIntGrid`) and their auto-layer tiles.
- AutoLayer and Tiles layers, including stacked tiles, tiles off the grid, flips and per-tile alpha.
- Entities layers: every entity becomes a `MapObject` with its fields as properties.
- Several levels, in projects with one world or several, and levels saved as separate files.
- Layer opacity, offsets and parallax factors; level fields and background color.

LDtk auto-layer rules are not re-run at runtime: changing an IntGrid value with `setIntGrid` does not change the tiles. Use `autotile` for runtime terrain.

### Maps made in code

```cpp
TilemapHandle createTilemap(const TilemapDesc& desc);

struct TilemapDesc {
    int32_t width = 0, height = 0;           // cells
    int32_t tileWidth = 16, tileHeight = 16; // pixels per cell and per tile in the image
    TextureHandle tileset;                   // one image of tiles
    int32_t margin = 0, spacing = 0;         // pixels around the image edge and between tiles
    std::vector<std::string> layers = {"Tiles"};   // tile layers, bottom to top
    std::string name = "generated";
};
```

`createTilemap` makes an empty map for levels built in code, such as [generated](../gameplay/procedural-generation.md) ones. Tile ids count from 1 at the image's top-left, left to right and then down, as in Tiled. Fill the layers with `setTile`, or write a whole `Grid` with `fillTilemapLayer`. Everything else on this page works the same as for a loaded map: put it in the world with a `Tilemap` component, mark layers solid with `setLayerCollision`, and autotile. It returns an invalid handle when the size or the tileset is missing.

```cpp
TilemapHandle map = maps->createTilemap({.width = 80, .height = 50, .tileset = tiles,
                                         .layers = {"Floor", "Walls"}});
const uint32_t wallTiles[] = {0, 3};   // GridFloor stays empty, GridWall is tile 3
fillTilemapLayer(*maps, map, 1, cave, wallTiles);
maps->setLayerCollision(map, 1, true, {3});
cmd.spawn().insert(Transform2D{}, Tilemap{.map = map});
```

## Putting a Map in the World

```cpp
struct Tilemap {
    TilemapHandle map;
    float         zOrder = 0.f;          // zOrder of the bottom layer
    float         zStep = 1.f;           // added per layer, bottom to top
    uint8_t       renderLayer = 0;
    bool          visible = true;
    bool          cull = true;           // skip chunks outside the camera
    CameraHandle  camera;                // culling and parallax camera; empty = default camera
    bool          colliders = true;      // static colliders from the collision layers
    bool          spawnObjects = true;   // run the object spawners once
    ShapeDef      collider = {};         // friction, restitution, filter of the colliders
};
```

The entity's `Transform2D` position is the map's top-left corner, and its scale scales the map. An LDtk level adds its world position on top, so spawning every level of a project at the same `Transform2D` lays them out as in the editor. Rotation is not applied.

One component holds the whole map rather than one child entity per layer: the layers live in the resource, so the plugin draws them in one pass with a known order, and you change a layer through the resource (`setLayerVisible`, `setLayerZOrder`, ...) instead of looking up its entity.

Despawning the entity removes its colliders. Entities made by object spawners belong to the game and stay.

## Drawing and Sorting

Every tile layer is cut into chunks of 16 by 16 cells. Each chunk is built into one mesh per texture the first time it is drawn and rebuilt only when one of its tiles changes; each frame the plugin draws the chunks that overlap the camera with `RendererResource::drawMesh`. Animated tiles update their texture coordinates in place.

Layers are drawn bottom to top at `zOrder`, `zOrder + zStep`, `zOrder + 2 * zStep`, ... on `renderLayer`. Object layers take a slot but draw nothing. A sprite whose `zOrder` falls between two layers' values draws between them:

```cpp
// Layers: Background (z 0), Ground (z 1), Foreground (z 2).
cmd.spawn().insert(Transform2D{.position = spawn}, Sprite{.texture = hero, .zOrder = 1.5f});
```

To pin one layer elsewhere, set the Tiled layer properties `zOrder` (float) and `renderLayer` (int), or call:

```cpp
void setLayerZOrder(TilemapHandle map, int32_t layer, float zOrder);
void setLayerRenderLayer(TilemapHandle map, int32_t layer, uint8_t renderLayer);
```

Parallax factors move a layer by that fraction of the camera's motion away from the parallax origin (Tiled's map parallax origin; an LDtk level's center). Factor 1 moves with the world, 0 stays fixed to the camera. Culling and parallax use the `Tilemap::camera`, the default camera when it is empty; with several cameras showing different places, set `cull = false` on maps the other cameras must see.

Image layers draw their image at the layer offset; `repeatx` and `repeaty` tile it across the view.

The level background color (`backgroundColor()`) is not drawn for you; fill a rect behind the map, or use it as the clear color.

### Drawing without an entity

```cpp
void drawTilemap(TilemapHandle map, const TilemapDrawParams& params = {});
```

Call it from a Render-phase system. `TilemapDrawParams` has the same `origin`, `scale`, `zOrder`, `zStep`, `renderLayer`, `camera` and `cull` settings as the component. Animated tiles follow `setAnimationTime(seconds)`, which the plugin sets from `Time::elapsed` every frame.

## Layers

```cpp
int32_t layerCount(TilemapHandle map) const;
int32_t findLayer(TilemapHandle map, std::string_view name) const;     // -1 if missing
const char* layerName(TilemapHandle map, int32_t layer) const;
TilemapLayerKind layerKind(TilemapHandle map, int32_t layer) const;    // Tiles, IntGrid, Objects, Image
const TilemapProperties& layerProperties(TilemapHandle map, int32_t layer) const;

void setLayerVisible(TilemapHandle map, int32_t layer, bool visible);
void setLayerOpacity(TilemapHandle map, int32_t layer, float opacity);
void setLayerTint(TilemapHandle map, int32_t layer, Color tint);
void setLayerOffset(TilemapHandle map, int32_t layer, Vec2 offset);
void setLayerParallax(TilemapHandle map, int32_t layer, Vec2 factor);
```

Layer 0 is the bottom layer, for both formats (LDtk lists its top layer first; Drift reverses that).

## Tiles

```cpp
uint32_t getTile(TilemapHandle map, int32_t layer, int32_t x, int32_t y) const;       // 0 = empty
uint32_t getTileFlags(TilemapHandle map, int32_t layer, int32_t x, int32_t y) const;  // flip bits
void     setTile(TilemapHandle map, int32_t layer, int32_t x, int32_t y, uint32_t tile);
int32_t  getIntGrid(TilemapHandle map, int32_t layer, int32_t x, int32_t y) const;
void     setIntGrid(TilemapHandle map, int32_t layer, int32_t x, int32_t y, int32_t value);
const char* intGridName(TilemapHandle map, int32_t layer, int32_t value) const;       // LDtk value identifier
const TilemapProperties* tileProperties(TilemapHandle map, uint32_t tile) const;
const char* tileClass(TilemapHandle map, uint32_t tile) const;
```

Tile ids are global across the map's tilesets, as in Tiled: the first tile of the first tileset is 1. LDtk tilesets are numbered the same way, in the order the project defines them. To place a flipped tile, add `TileFlipH`, `TileFlipV` or `TileFlipDiagonal` to the id (`TileFlipDiagonal | TileFlipH` rotates 90 degrees clockwise). `setTile` rebuilds only the chunks around the changed cell.

LDtk tile custom data shows up as the tile property `customData`; enum tags become boolean properties named after the enum value.

### World and cell coordinates

```cpp
TileCoord worldToTile(TilemapHandle map, Vec2 world, int32_t layer = 0) const;
Vec2      tileToWorld(TilemapHandle map, TileCoord cell, int32_t layer = 0) const;   // cell top-left
uint32_t  tileAt(TilemapHandle map, int32_t layer, Vec2 world) const;
Vec2      origin(TilemapHandle map) const;    // world position of the map's top-left
```

These use where the map is placed: its entity's `Transform2D` (updated every frame) or the last `drawTilemap` call. The layer's offset counts; its parallax does not.

```cpp
void digAt(Vec2 worldPos, ResMut<TilemapResource> maps, Res<Level> level) {
    int32_t ground = maps->findLayer(level->map, "Ground");
    TileCoord c = maps->worldToTile(level->map, worldPos, ground);
    maps->setTile(level->map, ground, c.x, c.y, 0);   // colliders follow on the next frame
}
```

## Objects and Entities

```cpp
struct MapObject {
    int32_t        id;
    std::string    iid;        // LDtk instance id
    std::string    name;       // Tiled name; LDtk entity identifier
    std::string    type;       // Tiled class; LDtk entity identifier
    MapObjectShape shape;      // Rect, Point, Ellipse, Polygon, Polyline, Tile, Text
    Vec2           position;   // top-left of the bounds, map pixels
    Vec2           size;
    Vec2           pivot;      // normalized anchor (LDtk pivot; Tiled tile objects {0, 1})
    float          rotation;   // radians
    uint32_t       tile;       // tile objects and LDtk entity tiles
    std::vector<Vec2> points;  // polygon/polyline points relative to position
    std::vector<std::string> tags;   // LDtk entity tags
    TilemapProperties properties;

    Vec2 anchor() const;       // position + size * pivot
    Vec2 center() const;
};

const std::vector<MapObject>& objects(TilemapHandle map, int32_t layer) const;
std::vector<const MapObject*> findObjects(TilemapHandle map, std::string_view type) const;  // "" = all
```

Object positions are in map pixels, before the layer offset. Object layers are data: their tile objects are not drawn.

### Spawning game entities

```cpp
int32_t addObjectSpawner(std::string_view type, ObjectSpawner spawner);   // "" = every object
void    removeObjectSpawner(int32_t id);

using ObjectSpawner = std::function<void(const MapObject&, const ObjectSpawnContext&)>;

struct ObjectSpawnContext {
    App&          app;
    Commands&     commands;
    EntityId      mapEntity;
    TilemapHandle map;
    int32_t       layer;
    Vec2          position;   // world top-left (map placement, scale and layer offset applied)
    Vec2          size;       // world size
    Vec2          anchor;     // world pivot point
};
```

When a `Tilemap` entity spawns with `spawnObjects` set, every object of its map is passed to each spawner whose type matches. The same spawner serves both formats, because a Tiled object's class and an LDtk entity's identifier both land in `type`:

```cpp
maps->addObjectSpawner("PlayerSpawn", [](const MapObject&, const ObjectSpawnContext& ctx) {
    ctx.commands.spawn().insert(Transform2D{.position = ctx.anchor}, Player{});
});
```

Register spawners before the map entity spawns, for example in the same startup system.

### Properties

```cpp
struct TilemapProperty {
    std::string name, type, text;
    double      number;    // int, float, bool (0/1)
    Vec2        point;     // LDtk Point fields, level pixels at the cell center
    std::vector<TilemapProperty> elements;   // LDtk arrays
};

struct TilemapProperties {
    std::vector<TilemapProperty> items;
    const TilemapProperty* find(std::string_view name) const;
    bool        has(std::string_view name) const;
    std::string getString(std::string_view name, std::string_view fallback = {}) const;
    int32_t     getInt(std::string_view name, int32_t fallback = 0) const;
    float       getFloat(std::string_view name, float fallback = 0.f) const;
    bool        getBool(std::string_view name, bool fallback = false) const;
    Color       getColor(std::string_view name, Color fallback = Color::white()) const;
};
```

`properties(map)` holds the Tiled map properties or the LDtk level fields (plus the level's `iid`). LDtk entity references hold the referenced entity's `iid` in `text`.

## Collision

A layer is solid when:

- it is a Tiled tile layer with the boolean property `collision`, or its name contains "collision" (any case);
- it is an LDtk IntGrid or tile layer whose identifier contains "collision";
- you mark it with `setLayerCollision`.

Tiles are also solid on any layer when their tileset gives them collision shapes in Tiled's collision editor, or the boolean tile property `collision` or `solid`.

```cpp
void setLayerCollision(TilemapHandle map, int32_t layer, bool solid,
                       const std::vector<int32_t>& values = {});   // IntGrid values or tile ids; empty = any
std::vector<Rect> collisionRects(TilemapHandle map) const;
std::vector<Rect> collisionRects(TilemapHandle map, int32_t layer, const std::vector<int32_t>& values) const;
void refreshColliders(EntityId mapEntity);
```

`collisionRects` merges solid cells into as few rectangles as it can (greedy: widest run first, then grown downward), in map pixels. A spawned `Tilemap` entity with `colliders` set gets one static Box2D body with a box per rectangle. The shapes carry the map entity, so `CollisionStart` events name the map entity. `setTile`, `setIntGrid`, `setLayerCollision` and `autotile` on a solid layer rebuild the colliders on the next frame; `refreshColliders` forces it. The colliders are placed where the entity was when they were built.

Collision layers do not have to be visible: a hidden IntGrid or tile layer is a common way to keep collision separate from art.

## Runtime Autotiling

```cpp
void autotile(TilemapHandle map, int32_t source, int32_t target, const AutotileRules& rules);
void autotile(TilemapHandle map, int32_t source, int32_t target, const AutotileRules& rules,
              TileCoord regionMin, TileCoord regionMax);

struct AutotileRules {
    AutotileMode mode = AutotileMode::Neighbours4;
    std::vector<int32_t>  terrain;    // source values that count as terrain; empty = any non-zero
    std::vector<uint32_t> tiles;      // tile per neighbour mask
    uint32_t fallback = 0;            // used where tiles[mask] is 0
    bool edgesAreTerrain = false;     // cells outside the source count as terrain
};
```

`autotile` reads terrain from `source` (IntGrid values, or tile ids) and writes into `target` the tile for each terrain cell's neighbour mask, clearing cells that are not terrain.

- `Neighbours4`: 16 tiles, mask bits N=1, E=2, S=4, W=8.
- `Neighbours8`: 256 entries for a blob tileset, mask bits NW=1, N=2, NE=4, W=8, E=16, SW=32, S=64, SE=128. A corner bit is set only when both edges beside it are, so 47 distinct masks occur.

The region form updates the cells in the region and their neighbours, which is what you call after changing one cell:

```cpp
maps->setTile(map, mask, c.x, c.y, 1);
maps->autotile(map, mask, platforms, rules, c, c);
```

## Map Info

```cpp
const char* mapName(TilemapHandle map) const;     // LDtk level identifier; Tiled file name
int32_t mapWidth(TilemapHandle map) const;        // cells
int32_t mapHeight(TilemapHandle map) const;
int32_t tileWidth(TilemapHandle map) const;       // pixels per cell
int32_t tileHeight(TilemapHandle map) const;
Rect    bounds(TilemapHandle map) const;          // map pixels covering every tile
Vec2    worldOffset(TilemapHandle map) const;     // LDtk level world position
Color   backgroundColor(TilemapHandle map) const;
const TilemapProperties& properties(TilemapHandle map) const;
```

For an infinite Tiled map, `bounds` covers the stored chunks and may start at negative coordinates.

## Example

`examples/tilemap` puts a Tiled map and two LDtk levels side by side, with a player that runs across both. See [Tilemap example](../examples/tilemap.md).
