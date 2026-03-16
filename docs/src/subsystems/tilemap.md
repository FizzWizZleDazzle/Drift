# Tilemap

The `TilemapResource` loads and renders tile-based maps exported from [Tiled](https://www.mapeditor.org/) in JSON format. It supports multiple layers, tile queries, runtime tile modification, object layers, and collision rectangle extraction.

**Header:** `#include <drift/resources/TilemapResource.hpp>`

**Plugin:** `TilemapPlugin` (included in `DefaultPlugins`)

## Loading a Tilemap

```cpp
TilemapHandle loadTilemap(const char* path);
void          destroyTilemap(TilemapHandle map);
```

`loadTilemap` reads a Tiled JSON file (`.json` exported from Tiled). The tileset image referenced in the map file is loaded as a texture automatically. The path is relative to the working directory.

```cpp
struct Level : public Resource {
    DRIFT_RESOURCE(Level)
    TilemapHandle map;
};

void loadLevel(ResMut<TilemapResource> tilemaps, ResMut<Level> level) {
    level->map = tilemaps->loadTilemap("assets/maps/level1.json");
}
```

### Tiled Export Settings

When exporting from Tiled, use these settings:

- **Format:** JSON map files (`.json`)
- **Tile layer format:** CSV (default)
- **Embed tilesets:** recommended for simpler asset management

The tileset image path in the JSON file is resolved relative to the JSON file itself.

## Rendering

```cpp
void drawTilemap(TilemapHandle map);
```

`drawTilemap` renders all visible tile layers. It automatically handles camera offset and draws only tiles within the viewport for performance. Call it once per frame in a render system.

```cpp
void renderLevel(ResMut<TilemapResource> tilemaps, Res<Level> level) {
    tilemaps->drawTilemap(level->map);
}
```

The tilemap is drawn at z-order 0 by default. Place sprites at higher z-order values to draw them on top of the map.

## Map Dimensions

```cpp
int32_t layerCount(TilemapHandle map) const;
int32_t mapWidth(TilemapHandle map) const;     // width in tiles
int32_t mapHeight(TilemapHandle map) const;    // height in tiles
int32_t tileWidth(TilemapHandle map) const;    // tile width in pixels
int32_t tileHeight(TilemapHandle map) const;   // tile height in pixels
```

To get the total map size in pixels:

```cpp
float worldW = tilemaps->mapWidth(map) * tilemaps->tileWidth(map);
float worldH = tilemaps->mapHeight(map) * tilemaps->tileHeight(map);
```

## Tile Access

```cpp
int32_t getTile(TilemapHandle map, int32_t layer, int32_t x, int32_t y) const;
void    setTile(TilemapHandle map, int32_t layer, int32_t x, int32_t y, int32_t tileId);
```

`getTile` returns the tile ID at a given position. Tile IDs correspond to the indices in the tileset (0 = empty, 1+ = first tile, etc.). `setTile` modifies the map at runtime.

### Example: Destructible Terrain

```cpp
void destroyTile(ResMut<TilemapResource> tilemaps, Res<Level> level,
                 Vec2 worldPos) {
    int tx = (int)(worldPos.x / tilemaps->tileWidth(level->map));
    int ty = (int)(worldPos.y / tilemaps->tileHeight(level->map));

    int32_t tile = tilemaps->getTile(level->map, 0, tx, ty);
    if (tile != 0) {
        tilemaps->setTile(level->map, 0, tx, ty, 0); // clear the tile
    }
}
```

## Collision Rectangles

```cpp
int32_t getCollisionRects(TilemapHandle map, Rect* outRects, int32_t maxRects) const;
```

Extracts collision rectangles from the tilemap. Tiles with collision data (defined in the Tiled tileset editor) produce axis-aligned rectangles in world space. Returns the number of rectangles written.

Use this to create static physics bodies for the terrain:

```cpp
void createTerrainColliders(ResMut<TilemapResource> tilemaps,
                            ResMut<PhysicsResource> physics,
                            Res<Level> level) {
    Rect rects[512];
    int32_t count = tilemaps->getCollisionRects(level->map, rects, 512);

    for (int i = 0; i < count; ++i) {
        BodyDef bodyDef;
        bodyDef.type = BodyType::Static;
        bodyDef.position = {rects[i].x + rects[i].w * 0.5f,
                            rects[i].y + rects[i].h * 0.5f};
        PhysicsBody body = physics->createBody(bodyDef);
        physics->addBox(body, rects[i].w * 0.5f, rects[i].h * 0.5f);
    }
}
```

## Object Layers

```cpp
int32_t getObjects(TilemapHandle map, const char* layerName,
                   TilemapObject* outObjects, int32_t maxObjects) const;
```

Tiled object layers can define spawn points, trigger zones, and other game-specific data. `getObjects` reads objects from a named layer.

```cpp
struct TilemapObject {
    const char* name;   // object name from Tiled
    const char* type;   // object type/class from Tiled
    Rect rect;          // position and size in world pixels
    int32_t id;         // unique object ID
};
```

### Example: Spawn Points

In Tiled, create an object layer called "spawns" and place point objects with types like "player" and "enemy".

```cpp
void spawnEntities(ResMut<TilemapResource> tilemaps, Res<Level> level,
                   Commands& cmd) {
    TilemapObject objects[64];
    int32_t count = tilemaps->getObjects(level->map, "spawns", objects, 64);

    for (int i = 0; i < count; ++i) {
        Vec2 pos = {objects[i].rect.x, objects[i].rect.y};

        if (strcmp(objects[i].type, "player") == 0) {
            cmd.spawn().insert(Transform2D{.position = pos}, PlayerTag{});
        } else if (strcmp(objects[i].type, "enemy") == 0) {
            cmd.spawn().insert(Transform2D{.position = pos}, EnemyTag{});
        }
    }
}
```

## Complete Example

```cpp
struct Level : public Resource {
    DRIFT_RESOURCE(Level)
    TilemapHandle map;
};

void setupLevel(ResMut<TilemapResource> tilemaps, ResMut<PhysicsResource> physics,
                ResMut<Level> level, Commands& cmd) {
    level->map = tilemaps->loadTilemap("assets/maps/world.json");

    // Create static colliders from tilemap
    Rect rects[1024];
    int32_t count = tilemaps->getCollisionRects(level->map, rects, 1024);
    for (int i = 0; i < count; ++i) {
        BodyDef def;
        def.type = BodyType::Static;
        def.position = {rects[i].x + rects[i].w * 0.5f,
                        rects[i].y + rects[i].h * 0.5f};
        PhysicsBody body = physics->createBody(def);
        physics->addBox(body, rects[i].w * 0.5f, rects[i].h * 0.5f);
    }

    // Spawn entities from object layer
    TilemapObject objects[64];
    int32_t objCount = tilemaps->getObjects(level->map, "entities", objects, 64);
    for (int i = 0; i < objCount; ++i) {
        Vec2 pos = {objects[i].rect.x, objects[i].rect.y};
        if (strcmp(objects[i].type, "player") == 0) {
            cmd.spawn().insert(Transform2D{.position = pos}, PlayerTag{});
        }
    }
}

void renderLevel(ResMut<TilemapResource> tilemaps, Res<Level> level) {
    tilemaps->drawTilemap(level->map);
}

int main() {
    App app;
    app.setConfig({.title = "Tilemap Demo", .width = 1280, .height = 720});
    app.addPlugins<DefaultPlugins>();
    app.initResource<Level>();
    app.startup<setupLevel>("setup_level");
    app.update<renderLevel>("render_level");
    return app.run();
}
```
