#pragma once

#include <drift/Resource.hpp>
#include <drift/Handle.hpp>
#include <drift/Types.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace drift {

class App;
class Commands;
class RendererResource;

// Flip bits stored in the high bits of a tile id (the Tiled convention; LDtk
// flips are converted to the same bits). getTile() strips them.
constexpr uint32_t TileFlipH        = 0x80000000u;
constexpr uint32_t TileFlipV        = 0x40000000u;
constexpr uint32_t TileFlipDiagonal = 0x20000000u;   // swap x and y; with H = rotate 90 degrees
constexpr uint32_t TileFlagsMask    = 0xF0000000u;

// ---- Custom properties (Tiled properties, LDtk field instances) ----

struct TilemapProperty {
    std::string name;
    std::string type;          // Tiled: string/int/float/bool/color/file/object/class; LDtk: Int/Float/String/Bool/Color/Point/EntityRef/Enum(...)
    std::string text;          // value as text (class and unknown values as JSON)
    double      number = 0.0;  // value as a number (int, float, bool, object id)
    Vec2        point;         // LDtk Point fields, in level pixels (cell center)
    std::vector<TilemapProperty> elements;  // LDtk Array<...> fields
};

struct TilemapProperties {
    std::vector<TilemapProperty> items;

    const TilemapProperty* find(std::string_view name) const;
    bool has(std::string_view name) const { return find(name) != nullptr; }
    std::string getString(std::string_view name, std::string_view fallback = {}) const;
    int32_t     getInt(std::string_view name, int32_t fallback = 0) const;
    float       getFloat(std::string_view name, float fallback = 0.f) const;
    bool        getBool(std::string_view name, bool fallback = false) const;
    Color       getColor(std::string_view name, Color fallback = Color::white()) const;
};

// ---- Objects (Tiled object layers, LDtk entities) ----

enum class MapObjectShape : uint8_t { Rect, Point, Ellipse, Polygon, Polyline, Tile, Text };

struct MapObject {
    int32_t        id = 0;          // Tiled object id; LDtk: index in the level
    std::string    iid;             // LDtk instance id (empty for Tiled)
    std::string    name;            // Tiled name; LDtk entity identifier
    std::string    type;            // Tiled class/type; LDtk entity identifier
    MapObjectShape shape = MapObjectShape::Rect;
    Vec2           position;        // top-left of the bounds, map pixels
    Vec2           size;            // bounds size, map pixels (0 for points)
    Vec2           pivot;           // normalized anchor inside the bounds (LDtk pivot; Tiled tile objects: {0,1})
    float          rotation = 0.f;  // radians
    uint32_t       tile = 0;        // tile id with flip bits for Tile objects
    std::vector<Vec2> points;       // polygon/polyline points, relative to position
    std::vector<std::string> tags;  // LDtk entity tags
    TilemapProperties properties;

    Vec2 anchor() const { return {position.x + size.x * pivot.x, position.y + size.y * pivot.y}; }
    Vec2 center() const { return {position.x + size.x * 0.5f, position.y + size.y * 0.5f}; }
};

// ---- Layers ----

enum class TilemapLayerKind : uint8_t {
    Tiles,      // Tiled tile layer, LDtk Tiles / AutoLayer
    IntGrid,    // LDtk IntGrid (values plus its auto-layer tiles)
    Objects,    // Tiled object layer, LDtk Entities
    Image,      // Tiled image layer
};

struct TileCoord {
    int32_t x = 0;
    int32_t y = 0;
    bool operator==(TileCoord o) const { return x == o.x && y == o.y; }
};

// ---- Runtime autotiling ----

enum class AutotileMode : uint8_t {
    Neighbours4,   // 16 tiles, mask bits N=1 E=2 S=4 W=8
    Neighbours8,   // blob tileset, mask bits NW=1 N=2 NE=4 W=8 E=16 SW=32 S=64 SE=128;
                   // a corner bit counts only when both edges next to it are set
};

struct AutotileRules {
    AutotileMode mode = AutotileMode::Neighbours4;
    // A source cell is terrain when its value (IntGrid value or tile id)
    // is in `terrain`; empty = any non-zero value.
    std::vector<int32_t> terrain;
    // Tile id (with optional flip bits) per mask: 16 entries for
    // Neighbours4, 256 for Neighbours8. A 0 entry falls back to `fallback`.
    std::vector<uint32_t> tiles;
    uint32_t fallback = 0;
    bool edgesAreTerrain = false;   // cells outside the layer count as terrain
};

// ---- Drawing without the ECS ----

struct TilemapDrawParams {
    Vec2         origin;                 // world position of the map's top-left (an LDtk level adds its world offset)
    Vec2         scale = Vec2::one();
    float        zOrder = 0.f;           // z of the bottom layer
    float        zStep = 1.f;            // added per layer, bottom to top
    uint8_t      renderLayer = 0;
    CameraHandle camera;                 // parallax camera, and the only one culling checks; empty = default camera for parallax, every camera for culling
    bool         cull = true;
};

// ---- Maps made in code ----

// An empty map for createTilemap: tile layers of width x height cells over
// one tileset image. Tile ids count from 1 at the image's top-left, left to
// right and then down, as in Tiled.
struct TilemapDesc {
    int32_t       width = 0;                // cells
    int32_t       height = 0;
    int32_t       tileWidth = 16;           // pixels per cell, and per tile in the image
    int32_t       tileHeight = 16;
    TextureHandle tileset;
    int32_t       margin = 0;               // pixels around the image edge
    int32_t       spacing = 0;              // pixels between tiles
    std::vector<std::string> layers = {"Tiles"};   // bottom to top
    std::string   name = "generated";
};

// ---- Object spawning hook ----

struct ObjectSpawnContext {
    App&          app;
    Commands&     commands;
    EntityId      mapEntity;
    TilemapHandle map;
    int32_t       layer;                 // index of the object's layer
    Vec2          position;              // world position of the bounds' top-left
    Vec2          size;                  // world size of the bounds
    Vec2          anchor;                // world position of the pivot
};

using ObjectSpawner = std::function<void(const MapObject&, const ObjectSpawnContext&)>;

// Loads Tiled (.tmj/.json) and LDtk (.ldtk) levels, renders them as cached
// chunk meshes, answers tile queries, and builds merged colliders.
class TilemapResource : public Resource {
public:
    explicit TilemapResource(RendererResource& renderer);
    ~TilemapResource() override;

    DRIFT_RESOURCE(TilemapResource)
    bool mainThreadOnly() const override { return true; }

    // ---- Loading ----
    // Tiled JSON map, or one LDtk level (by identifier; nullptr = the first).
    TilemapHandle loadTilemap(const char* path, const char* level = nullptr);
    // Every level in an LDtk project (a Tiled map yields one handle).
    std::vector<TilemapHandle> loadLevels(const char* path);
    // An empty map to fill with setTile, for generated levels. Returns an
    // invalid handle when the size or tileset is missing.
    TilemapHandle createTilemap(const TilemapDesc& desc);
    void destroyTilemap(TilemapHandle map);

    // ---- Map info ----
    const char* mapName(TilemapHandle map) const;       // LDtk level identifier; Tiled file name
    int32_t mapWidth(TilemapHandle map) const;          // cells
    int32_t mapHeight(TilemapHandle map) const;         // cells
    int32_t tileWidth(TilemapHandle map) const;         // pixels per cell
    int32_t tileHeight(TilemapHandle map) const;
    Rect    bounds(TilemapHandle map) const;            // map pixels, covers every tile
    Vec2    worldOffset(TilemapHandle map) const;       // LDtk level position in the world
    Color   backgroundColor(TilemapHandle map) const;
    const TilemapProperties& properties(TilemapHandle map) const;

    // ---- Layers (index 0 is drawn first) ----
    int32_t layerCount(TilemapHandle map) const;
    int32_t findLayer(TilemapHandle map, std::string_view name) const;   // -1 if missing
    const char* layerName(TilemapHandle map, int32_t layer) const;
    TilemapLayerKind layerKind(TilemapHandle map, int32_t layer) const;
    const TilemapProperties& layerProperties(TilemapHandle map, int32_t layer) const;
    void setLayerVisible(TilemapHandle map, int32_t layer, bool visible);
    void setLayerOpacity(TilemapHandle map, int32_t layer, float opacity);
    void setLayerTint(TilemapHandle map, int32_t layer, Color tint);
    void setLayerOffset(TilemapHandle map, int32_t layer, Vec2 offset);
    void setLayerParallax(TilemapHandle map, int32_t layer, Vec2 factor);
    // Fixed zOrder / render layer for one layer, instead of the map's
    // zOrder + index * zStep and render layer.
    void setLayerZOrder(TilemapHandle map, int32_t layer, float zOrder);
    void setLayerRenderLayer(TilemapHandle map, int32_t layer, uint8_t renderLayer);
    // Marks a layer as solid for collisionRects(). values: IntGrid values
    // or tile ids that are solid; empty = any non-empty cell.
    void setLayerCollision(TilemapHandle map, int32_t layer, bool solid,
                           const std::vector<int32_t>& values = {});

    // ---- Tiles ----
    uint32_t getTile(TilemapHandle map, int32_t layer, int32_t x, int32_t y) const;      // id, no flip bits
    uint32_t getTileFlags(TilemapHandle map, int32_t layer, int32_t x, int32_t y) const; // flip bits only
    void     setTile(TilemapHandle map, int32_t layer, int32_t x, int32_t y, uint32_t tile);
    int32_t  getIntGrid(TilemapHandle map, int32_t layer, int32_t x, int32_t y) const;
    void     setIntGrid(TilemapHandle map, int32_t layer, int32_t x, int32_t y, int32_t value);
    // Name of an LDtk IntGrid value, empty if unnamed.
    const char* intGridName(TilemapHandle map, int32_t layer, int32_t value) const;
    const TilemapProperties* tileProperties(TilemapHandle map, uint32_t tile) const;
    const char* tileClass(TilemapHandle map, uint32_t tile) const;

    // World <-> cell conversion, using where the map was last drawn (the
    // entity's Transform2D, or TilemapDrawParams origin and scale).
    TileCoord worldToTile(TilemapHandle map, Vec2 world, int32_t layer = 0) const;
    Vec2      tileToWorld(TilemapHandle map, TileCoord cell, int32_t layer = 0) const;   // cell top-left
    uint32_t  tileAt(TilemapHandle map, int32_t layer, Vec2 world) const;
    Vec2      origin(TilemapHandle map) const;

    // ---- Objects ----
    const std::vector<MapObject>& objects(TilemapHandle map, int32_t layer) const;
    std::vector<const MapObject*> findObjects(TilemapHandle map, std::string_view type) const;

    // ---- Collision ----
    // Solid cells of every collision layer, merged into as few rectangles
    // as possible. Map pixels.
    std::vector<Rect> collisionRects(TilemapHandle map) const;
    // Same for one layer and an explicit value set (empty = any non-empty cell).
    std::vector<Rect> collisionRects(TilemapHandle map, int32_t layer,
                                     const std::vector<int32_t>& values) const;
    // Rebuilds the colliders of a spawned map entity next frame.
    void refreshColliders(EntityId mapEntity);

    // ---- Autotiling ----
    // Writes `target` tiles from the terrain in `source` (whole layer, or
    // the cells in `region` plus their neighbours).
    void autotile(TilemapHandle map, int32_t source, int32_t target, const AutotileRules& rules);
    void autotile(TilemapHandle map, int32_t source, int32_t target, const AutotileRules& rules,
                  TileCoord regionMin, TileCoord regionMax);

    // ---- Spawning ----
    // Called once per object when a Tilemap entity spawns. type "" matches
    // every object. Returns an id for removeObjectSpawner.
    int32_t addObjectSpawner(std::string_view type, ObjectSpawner spawner);
    void removeObjectSpawner(int32_t id);

    // ---- Drawing ----
    // The TilemapPlugin draws every Tilemap entity; call this to draw a map
    // without an entity. Call it during the Render phase.
    void drawTilemap(TilemapHandle map, const TilemapDrawParams& params = {});

    // Seconds used for animated tiles (TilemapPlugin sets it from Time).
    void setAnimationTime(double seconds);

    struct Impl;

private:
    friend struct TilemapPluginAccess;
    Impl* impl_;
};

} // namespace drift
