#pragma once

// Internal map model shared by the Tiled and LDtk loaders and the
// TilemapResource (rendering, queries, collision).

#include <drift/resources/TilemapResource.hpp>
#include <drift/resources/RendererResource.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace drift::tilemap {

// Cells per chunk side. Tile storage, render meshes and culling all work
// in chunks of this size.
constexpr int32_t ChunkSize = 16;

inline int32_t floorDiv(int32_t a, int32_t b) {
    int32_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

inline uint64_t chunkKey(int32_t cx, int32_t cy) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) | static_cast<uint32_t>(cy);
}
inline int32_t chunkKeyX(uint64_t k) { return static_cast<int32_t>(static_cast<uint32_t>(k >> 32)); }
inline int32_t chunkKeyY(uint64_t k) { return static_cast<int32_t>(static_cast<uint32_t>(k)); }

struct AnimFrame {
    uint32_t localId = 0;
    float    duration = 0.1f;   // seconds
};

struct TileInfo {
    std::string            cls;
    TilemapProperties      properties;
    std::vector<AnimFrame> animation;
    float                  animationLength = 0.f;
    bool                   solid = false;      // collision shapes or property collision=true
    // Image-collection tilesets: one texture per tile.
    TextureHandle          image;
    int32_t                imageW = 0, imageH = 0;
};

struct Tileset {
    uint32_t      firstgid = 1;
    uint32_t      tileCount = 0;
    std::string   name;
    TextureHandle texture;
    int32_t       textureW = 0, textureH = 0;
    int32_t       tileW = 0, tileH = 0;
    int32_t       columns = 1;
    int32_t       margin = 0, spacing = 0;
    Vec2          tileOffset;
    bool          gridRenderSize = false;   // Tiled tilerendersize "grid"
    bool          collection = false;       // one image per tile
    std::unordered_map<uint32_t, TileInfo> tiles;

    const TileInfo* info(uint32_t localId) const {
        auto it = tiles.find(localId);
        return it == tiles.end() ? nullptr : &it->second;
    }
};

// A tile drawn at an arbitrary layer-pixel position (LDtk stacked or
// off-grid tiles, tiles with their own alpha).
struct TileInstance {
    Vec2     position;
    uint32_t tile = 0;
    float    alpha = 1.f;
};

struct SubMesh {
    struct Anim {
        uint32_t       vertex = 0;
        const Tileset* tileset = nullptr;
        uint32_t       localId = 0;
        uint32_t       flags = 0;
        int32_t        frame = -1;
    };
    TextureHandle texture;
    std::vector<RendererResource::MeshVertex> vertices;   // layer pixels
    std::vector<uint32_t> indices;
    std::vector<Anim>     anims;
};

struct RenderChunk {
    bool                 dirty = true;
    Rect                 bounds;        // layer pixels, covers every quad
    std::vector<SubMesh> meshes;
};

struct Layer {
    std::string       name;
    TilemapLayerKind  kind = TilemapLayerKind::Tiles;
    TilemapProperties properties;
    bool              visible = true;
    float             opacity = 1.f;
    Color             tint = Color::white();
    Vec2              offset;
    Vec2              parallax = Vec2::one();
    int32_t           cellW = 16, cellH = 16;

    bool              hasZ = false;
    float             z = 0.f;
    int32_t           renderLayer = -1;    // -1 = the map's

    bool                 collision = false;
    std::vector<int32_t> collisionValues;

    // Cell bounds [min, max) of everything stored.
    int32_t minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool    empty = true;

    std::unordered_map<uint64_t, std::vector<uint32_t>> tiles;     // ChunkSize^2 per chunk
    std::unordered_map<uint64_t, std::vector<int32_t>>  ints;      // IntGrid values
    std::unordered_map<uint64_t, std::vector<TileInstance>> extras;
    std::unordered_map<uint64_t, RenderChunk> render;

    std::vector<MapObject> objects;
    std::unordered_map<int32_t, std::string> intGridNames;

    // Image layers
    TextureHandle image;
    Vec2          imageSize;
    bool          repeatX = false, repeatY = false;

    void grow(int32_t x, int32_t y) {
        if (empty) { minX = x; minY = y; maxX = x + 1; maxY = y + 1; empty = false; return; }
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (x + 1 > maxX) maxX = x + 1;
        if (y + 1 > maxY) maxY = y + 1;
    }

    static int32_t slot(int32_t x, int32_t y) {
        int32_t lx = x - floorDiv(x, ChunkSize) * ChunkSize;
        int32_t ly = y - floorDiv(y, ChunkSize) * ChunkSize;
        return ly * ChunkSize + lx;
    }

    uint32_t tile(int32_t x, int32_t y) const {
        auto it = tiles.find(chunkKey(floorDiv(x, ChunkSize), floorDiv(y, ChunkSize)));
        return it == tiles.end() ? 0u : it->second[slot(x, y)];
    }
    int32_t intAt(int32_t x, int32_t y) const {
        auto it = ints.find(chunkKey(floorDiv(x, ChunkSize), floorDiv(y, ChunkSize)));
        return it == ints.end() ? 0 : it->second[slot(x, y)];
    }

    void markDirty(int32_t x, int32_t y) {
        // Tiles larger than a cell spill into neighbouring chunks, so the
        // neighbours are rebuilt too.
        int32_t cx = floorDiv(x, ChunkSize), cy = floorDiv(y, ChunkSize);
        for (int32_t dy = -1; dy <= 1; ++dy)
            for (int32_t dx = -1; dx <= 1; ++dx) {
                auto it = render.find(chunkKey(cx + dx, cy + dy));
                if (it != render.end()) it->second.dirty = true;
            }
        render[chunkKey(cx, cy)].dirty = true;
    }

    void setTile(int32_t x, int32_t y, uint32_t t) {
        uint64_t k = chunkKey(floorDiv(x, ChunkSize), floorDiv(y, ChunkSize));
        auto it = tiles.find(k);
        if (it == tiles.end()) {
            if (t == 0) return;
            it = tiles.emplace(k, std::vector<uint32_t>(ChunkSize * ChunkSize, 0u)).first;
        }
        it->second[slot(x, y)] = t;
        if (t) grow(x, y);
        markDirty(x, y);
    }
    void setInt(int32_t x, int32_t y, int32_t v) {
        uint64_t k = chunkKey(floorDiv(x, ChunkSize), floorDiv(y, ChunkSize));
        auto it = ints.find(k);
        if (it == ints.end()) {
            if (v == 0) return;
            it = ints.emplace(k, std::vector<int32_t>(ChunkSize * ChunkSize, 0)).first;
        }
        it->second[slot(x, y)] = v;
        if (v) grow(x, y);
    }
    void addExtra(const TileInstance& inst) {
        int32_t cx = floorDiv(static_cast<int32_t>(inst.position.x), cellW);
        int32_t cy = floorDiv(static_cast<int32_t>(inst.position.y), cellH);
        extras[chunkKey(floorDiv(cx, ChunkSize), floorDiv(cy, ChunkSize))].push_back(inst);
        grow(cx, cy);
    }
};

struct MapData {
    std::string name;
    std::string path;
    int32_t     width = 0, height = 0;      // cells
    int32_t     tileW = 16, tileH = 16;
    Vec2        worldOffset;
    Vec2        pixelSize;                  // LDtk level size; Tiled width*tileW
    Color       background = {0, 0, 0, 0};
    Vec2        parallaxOrigin;
    TilemapProperties    properties;
    std::vector<Tileset> tilesets;
    std::vector<Layer>   layers;

    // Where the map was last drawn.
    Vec2 origin;
    Vec2 scale = Vec2::one();

    const Tileset* tilesetFor(uint32_t id) const {
        const Tileset* best = nullptr;
        for (const Tileset& ts : tilesets) {
            if (id >= ts.firstgid && (!best || ts.firstgid > best->firstgid)) best = &ts;
        }
        return best;
    }
};

// Services the loaders need from the resource.
struct LoadContext {
    std::function<TextureHandle(const std::string& path)> loadTexture;
    std::function<Vec2(TextureHandle)> textureSize;
};

bool loadTiled(LoadContext& ctx, const std::string& path, MapData& out);
// Loads the named level (nullptr = first) or, when all is true, every level.
bool loadLdtk(LoadContext& ctx, const std::string& path, const char* level, bool all,
              std::vector<MapData>& out);

// Shared helpers (TilemapProperties.cpp)
std::string readFile(const std::string& path);
std::string directoryOf(const std::string& path);
Color parseColor(const std::string& text, Color fallback);
std::string toLower(std::string s);

} // namespace drift::tilemap
