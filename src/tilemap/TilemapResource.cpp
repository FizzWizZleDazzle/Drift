// TilemapResource: owns loaded maps, builds and caches per-layer chunk
// meshes, draws the visible chunks, and answers tile, object and
// collision queries.

#include <drift/resources/TilemapResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/PhysicsResource.hpp>
#include <drift/Log.hpp>

#include "core/HandlePool.hpp"
#include "tilemap/TilemapData.hpp"
#include "tilemap/TilemapInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace drift {

using namespace tilemap;

namespace {

const TilemapProperties kNoProperties;
const std::vector<MapObject> kNoObjects;

// Inset (in texels) that keeps nearest sampling inside a tile's source rect.
constexpr float UvInset = 0.01f;

bool endsWith(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    return s.size() >= n && toLower(s.substr(s.size() - n)) == suffix;
}

// Source rect and texture of a tile; false for unknown ids.
bool tileSource(const Tileset& ts, uint32_t local, TextureHandle* tex, Rect* src, Vec2* texSize) {
    if (ts.collection) {
        const TileInfo* info = ts.info(local);
        if (!info || !info->image.valid()) return false;
        *tex = info->image;
        *src = {0.f, 0.f, static_cast<float>(info->imageW), static_cast<float>(info->imageH)};
        *texSize = {static_cast<float>(info->imageW), static_cast<float>(info->imageH)};
        return true;
    }
    if (!ts.texture.valid() || ts.columns <= 0) return false;
    int32_t col = static_cast<int32_t>(local) % ts.columns;
    int32_t row = static_cast<int32_t>(local) / ts.columns;
    *tex = ts.texture;
    *src = {static_cast<float>(ts.margin + col * (ts.tileW + ts.spacing)),
            static_cast<float>(ts.margin + row * (ts.tileH + ts.spacing)),
            static_cast<float>(ts.tileW), static_cast<float>(ts.tileH)};
    *texSize = {static_cast<float>(ts.textureW), static_cast<float>(ts.textureH)};
    return true;
}

// UVs for the quad corners TL, TR, BR, BL. Tiled applies the diagonal
// flip first, then horizontal, then vertical; sampling runs the inverse.
void writeUVs(RendererResource::MeshVertex* v, Rect src, Vec2 texSize, uint32_t flags) {
    float iw = texSize.x > 0.f ? 1.f / texSize.x : 0.f;
    float ih = texSize.y > 0.f ? 1.f / texSize.y : 0.f;
    float u0 = (src.x + UvInset) * iw, u1 = (src.x + src.w - UvInset) * iw;
    float v0 = (src.y + UvInset) * ih, v1 = (src.y + src.h - UvInset) * ih;
    static const float cx[4] = {0.f, 1.f, 1.f, 0.f};
    static const float cy[4] = {0.f, 0.f, 1.f, 1.f};
    for (int i = 0; i < 4; ++i) {
        float sx = cx[i], sy = cy[i];
        if (flags & TileFlipV) sy = 1.f - sy;
        if (flags & TileFlipH) sx = 1.f - sx;
        if (flags & TileFlipDiagonal) std::swap(sx, sy);
        v[i].uv = {u0 + (u1 - u0) * sx, v0 + (v1 - v0) * sy};
    }
}

SubMesh& meshFor(RenderChunk& rc, TextureHandle tex) {
    for (SubMesh& m : rc.meshes) {
        if (m.texture == tex) return m;
    }
    rc.meshes.emplace_back();
    rc.meshes.back().texture = tex;
    return rc.meshes.back();
}

// Appends one tile quad. `cell` is the top-left of the tile's cell in layer
// pixels; tiles taller than the cell grow upward (Tiled's convention).
void emitTile(const MapData& map, const Layer& layer, RenderChunk& rc, Vec2 cell, uint32_t id,
              float alpha, bool& haveBounds) {
    uint32_t gid = id & ~TileFlagsMask;
    uint32_t flags = id & (TileFlipH | TileFlipV | TileFlipDiagonal);
    const Tileset* ts = map.tilesetFor(gid);
    if (!ts) return;
    uint32_t local = gid - ts->firstgid;
    TextureHandle tex;
    Rect src;
    Vec2 texSize;
    if (!tileSource(*ts, local, &tex, &src, &texSize)) return;

    float w = src.w, h = src.h;
    if (ts->gridRenderSize) { w = static_cast<float>(layer.cellW); h = static_cast<float>(layer.cellH); }
    float x = cell.x + ts->tileOffset.x;
    float y = cell.y + layer.cellH - h + ts->tileOffset.y;

    SubMesh& sm = meshFor(rc, tex);
    uint32_t base = static_cast<uint32_t>(sm.vertices.size());
    Color c(255, 255, 255, static_cast<uint8_t>(std::clamp(alpha, 0.f, 1.f) * 255.f));
    RendererResource::MeshVertex v[4];
    v[0].position = {x, y};
    v[1].position = {x + w, y};
    v[2].position = {x + w, y + h};
    v[3].position = {x, y + h};
    for (auto& vv : v) vv.color = c;
    writeUVs(v, src, texSize, flags);
    sm.vertices.insert(sm.vertices.end(), v, v + 4);
    const uint32_t q[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
    sm.indices.insert(sm.indices.end(), q, q + 6);

    const TileInfo* info = ts->info(local);
    if (info && !info->animation.empty() && !ts->collection) {
        sm.anims.push_back({base, ts, local, flags, -1});
    }

    Rect r{x, y, w, h};
    if (!haveBounds) { rc.bounds = r; haveBounds = true; return; }
    float minX = std::min(rc.bounds.x, r.x), minY = std::min(rc.bounds.y, r.y);
    float maxX = std::max(rc.bounds.x + rc.bounds.w, r.x + r.w);
    float maxY = std::max(rc.bounds.y + rc.bounds.h, r.y + r.h);
    rc.bounds = {minX, minY, maxX - minX, maxY - minY};
}

void buildChunk(const MapData& map, const Layer& layer, uint64_t key, RenderChunk& rc) {
    rc.meshes.clear();
    rc.bounds = {};
    rc.dirty = false;
    bool haveBounds = false;
    int32_t cx = chunkKeyX(key), cy = chunkKeyY(key);
    auto t = layer.tiles.find(key);
    if (t != layer.tiles.end()) {
        for (int32_t ly = 0; ly < ChunkSize; ++ly) {
            for (int32_t lx = 0; lx < ChunkSize; ++lx) {
                uint32_t id = t->second[ly * ChunkSize + lx];
                if (!id) continue;
                Vec2 cell{static_cast<float>((cx * ChunkSize + lx) * layer.cellW),
                          static_cast<float>((cy * ChunkSize + ly) * layer.cellH)};
                emitTile(map, layer, rc, cell, id, 1.f, haveBounds);
            }
        }
    }
    auto e = layer.extras.find(key);
    if (e != layer.extras.end()) {
        for (const TileInstance& inst : e->second) {
            emitTile(map, layer, rc, inst.position, inst.tile, inst.alpha, haveBounds);
        }
    }
}

// Current frame index of an animated tile at `time`.
int32_t animationFrame(const TileInfo& info, double time) {
    if (info.animationLength <= 0.f) return 0;
    float t = static_cast<float>(std::fmod(time, static_cast<double>(info.animationLength)));
    for (size_t i = 0; i < info.animation.size(); ++i) {
        if (t < info.animation[i].duration) return static_cast<int32_t>(i);
        t -= info.animation[i].duration;
    }
    return static_cast<int32_t>(info.animation.size()) - 1;
}

void updateAnimations(SubMesh& sm, double time) {
    for (SubMesh::Anim& a : sm.anims) {
        const TileInfo* info = a.tileset->info(a.localId);
        if (!info) continue;
        int32_t frame = animationFrame(*info, time);
        if (frame == a.frame) continue;
        a.frame = frame;
        TextureHandle tex;
        Rect src;
        Vec2 texSize;
        if (!tileSource(*a.tileset, info->animation[frame].localId, &tex, &src, &texSize)) continue;
        writeUVs(&sm.vertices[a.vertex], src, texSize, a.flags);
    }
}

bool containsValue(const std::vector<int32_t>& values, int32_t v) {
    return std::find(values.begin(), values.end(), v) != values.end();
}

int32_t cellValue(const Layer& layer, int32_t x, int32_t y) {
    if (layer.kind == TilemapLayerKind::IntGrid) return layer.intAt(x, y);
    return static_cast<int32_t>(layer.tile(x, y) & ~TileFlagsMask);
}

// Greedy rectangle merge over a dense solid mask.
void mergeRects(const std::vector<uint8_t>& solid, int32_t w, int32_t h, int32_t x0, int32_t y0,
                float cellW, float cellH, Vec2 offset, std::vector<Rect>& out) {
    std::vector<uint8_t> used(solid.size(), 0);
    auto at = [&](int32_t x, int32_t y) { return static_cast<size_t>(y) * w + x; };
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            if (!solid[at(x, y)] || used[at(x, y)]) continue;
            int32_t rw = 1;
            while (x + rw < w && solid[at(x + rw, y)] && !used[at(x + rw, y)]) ++rw;
            int32_t rh = 1;
            for (;;) {
                if (y + rh >= h) break;
                bool full = true;
                for (int32_t i = 0; i < rw && full; ++i) {
                    full = solid[at(x + i, y + rh)] && !used[at(x + i, y + rh)];
                }
                if (!full) break;
                ++rh;
            }
            for (int32_t yy = 0; yy < rh; ++yy)
                for (int32_t xx = 0; xx < rw; ++xx) used[at(x + xx, y + yy)] = 1;
            out.push_back({offset.x + (x0 + x) * cellW, offset.y + (y0 + y) * cellH, rw * cellW, rh * cellH});
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Construction and loading
// ---------------------------------------------------------------------------

TilemapResource::TilemapResource(RendererResource& renderer) : impl_(new Impl(renderer)) {}

TilemapResource::~TilemapResource() { delete impl_; }

TilemapHandle TilemapResource::loadTilemap(const char* path, const char* level) {
    if (!path) return {};
    std::string p(path);
    if (endsWith(p, ".ldtk")) {
        std::vector<MapData> levels;
        if (!loadLdtk(impl_->ctx, p, level, false, levels) || levels.empty()) return {};
        return impl_->add(std::move(levels.front()));
    }
    MapData map;
    if (!loadTiled(impl_->ctx, p, map)) return {};
    return impl_->add(std::move(map));
}

std::vector<TilemapHandle> TilemapResource::loadLevels(const char* path) {
    std::vector<TilemapHandle> out;
    if (!path) return out;
    std::string p(path);
    if (!endsWith(p, ".ldtk")) {
        TilemapHandle h = loadTilemap(path);
        if (h.valid()) out.push_back(h);
        return out;
    }
    std::vector<MapData> levels;
    if (!loadLdtk(impl_->ctx, p, nullptr, true, levels)) return out;
    for (MapData& m : levels) out.push_back(impl_->add(std::move(m)));
    return out;
}

TilemapHandle TilemapResource::createTilemap(const TilemapDesc& desc) {
    if (desc.width <= 0 || desc.height <= 0 || desc.tileWidth <= 0 || desc.tileHeight <= 0 ||
        !desc.tileset.valid() || desc.layers.empty()) {
        return {};
    }
    Vec2 size = impl_->renderer.textureSize(desc.tileset);
    Tileset ts;
    ts.name = desc.name;
    ts.texture = desc.tileset;
    ts.textureW = static_cast<int32_t>(size.x);
    ts.textureH = static_cast<int32_t>(size.y);
    ts.tileW = desc.tileWidth;
    ts.tileH = desc.tileHeight;
    ts.margin = desc.margin;
    ts.spacing = desc.spacing;
    int32_t stepX = desc.tileWidth + desc.spacing, stepY = desc.tileHeight + desc.spacing;
    ts.columns = std::max(1, (ts.textureW - 2 * desc.margin + desc.spacing) / stepX);
    int32_t rows = std::max(1, (ts.textureH - 2 * desc.margin + desc.spacing) / stepY);
    ts.tileCount = static_cast<uint32_t>(ts.columns * rows);

    MapData map;
    map.name = desc.name;
    map.width = desc.width;
    map.height = desc.height;
    map.tileW = desc.tileWidth;
    map.tileH = desc.tileHeight;
    map.pixelSize = {float(desc.width * desc.tileWidth), float(desc.height * desc.tileHeight)};
    map.tilesets.push_back(std::move(ts));
    for (const std::string& name : desc.layers) {
        Layer l;
        l.name = name;
        l.kind = TilemapLayerKind::Tiles;
        l.cellW = desc.tileWidth;
        l.cellH = desc.tileHeight;
        map.layers.push_back(std::move(l));
    }
    return impl_->add(std::move(map));
}

void TilemapResource::destroyTilemap(TilemapHandle map) {
    impl_->maps.destroy(map);
}

// ---------------------------------------------------------------------------
// Map info
// ---------------------------------------------------------------------------

const char* TilemapResource::mapName(TilemapHandle map) const {
    const MapData* m = impl_->get(map);
    return m ? m->name.c_str() : "";
}

int32_t TilemapResource::mapWidth(TilemapHandle map) const {
    const MapData* m = impl_->get(map);
    return m ? m->width : 0;
}

int32_t TilemapResource::mapHeight(TilemapHandle map) const {
    const MapData* m = impl_->get(map);
    return m ? m->height : 0;
}

int32_t TilemapResource::tileWidth(TilemapHandle map) const {
    const MapData* m = impl_->get(map);
    return m ? m->tileW : 0;
}

int32_t TilemapResource::tileHeight(TilemapHandle map) const {
    const MapData* m = impl_->get(map);
    return m ? m->tileH : 0;
}

Rect TilemapResource::bounds(TilemapHandle map) const {
    const MapData* m = impl_->get(map);
    if (!m) return {};
    bool any = false;
    float minX = 0.f, minY = 0.f, maxX = m->pixelSize.x, maxY = m->pixelSize.y;
    for (const Layer& l : m->layers) {
        if (l.empty) continue;
        float x0 = l.offset.x + l.minX * l.cellW, y0 = l.offset.y + l.minY * l.cellH;
        float x1 = l.offset.x + l.maxX * l.cellW, y1 = l.offset.y + l.maxY * l.cellH;
        if (!any && m->pixelSize.x <= 0.f) { minX = x0; minY = y0; maxX = x1; maxY = y1; }
        any = true;
        minX = std::min(minX, x0); minY = std::min(minY, y0);
        maxX = std::max(maxX, x1); maxY = std::max(maxY, y1);
    }
    return {minX, minY, maxX - minX, maxY - minY};
}

Vec2 TilemapResource::worldOffset(TilemapHandle map) const {
    const MapData* m = impl_->get(map);
    return m ? m->worldOffset : Vec2{};
}

Color TilemapResource::backgroundColor(TilemapHandle map) const {
    const MapData* m = impl_->get(map);
    return m ? m->background : Color{0, 0, 0, 0};
}

const TilemapProperties& TilemapResource::properties(TilemapHandle map) const {
    const MapData* m = impl_->get(map);
    return m ? m->properties : kNoProperties;
}

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

int32_t TilemapResource::layerCount(TilemapHandle map) const {
    const MapData* m = impl_->get(map);
    return m ? static_cast<int32_t>(m->layers.size()) : 0;
}

int32_t TilemapResource::findLayer(TilemapHandle map, std::string_view name) const {
    const MapData* m = impl_->get(map);
    if (!m) return -1;
    for (size_t i = 0; i < m->layers.size(); ++i) {
        if (m->layers[i].name == name) return static_cast<int32_t>(i);
    }
    return -1;
}

const char* TilemapResource::layerName(TilemapHandle map, int32_t layer) const {
    const Layer* l = impl_->layer(map, layer);
    return l ? l->name.c_str() : "";
}

TilemapLayerKind TilemapResource::layerKind(TilemapHandle map, int32_t layer) const {
    const Layer* l = impl_->layer(map, layer);
    return l ? l->kind : TilemapLayerKind::Tiles;
}

const TilemapProperties& TilemapResource::layerProperties(TilemapHandle map, int32_t layer) const {
    const Layer* l = impl_->layer(map, layer);
    return l ? l->properties : kNoProperties;
}

void TilemapResource::setLayerVisible(TilemapHandle map, int32_t layer, bool visible) {
    if (Layer* l = impl_->layer(map, layer)) l->visible = visible;
}

void TilemapResource::setLayerOpacity(TilemapHandle map, int32_t layer, float opacity) {
    if (Layer* l = impl_->layer(map, layer)) l->opacity = std::clamp(opacity, 0.f, 1.f);
}

void TilemapResource::setLayerTint(TilemapHandle map, int32_t layer, Color tint) {
    if (Layer* l = impl_->layer(map, layer)) l->tint = tint;
}

void TilemapResource::setLayerOffset(TilemapHandle map, int32_t layer, Vec2 offset) {
    if (Layer* l = impl_->layer(map, layer)) l->offset = offset;
}

void TilemapResource::setLayerParallax(TilemapHandle map, int32_t layer, Vec2 factor) {
    if (Layer* l = impl_->layer(map, layer)) l->parallax = factor;
}

void TilemapResource::setLayerZOrder(TilemapHandle map, int32_t layer, float zOrder) {
    if (Layer* l = impl_->layer(map, layer)) { l->hasZ = true; l->z = zOrder; }
}

void TilemapResource::setLayerRenderLayer(TilemapHandle map, int32_t layer, uint8_t renderLayer) {
    if (Layer* l = impl_->layer(map, layer)) l->renderLayer = renderLayer;
}

void TilemapResource::setLayerCollision(TilemapHandle map, int32_t layer, bool solid,
                                        const std::vector<int32_t>& values) {
    Layer* l = impl_->layer(map, layer);
    if (!l) return;
    l->collision = solid;
    l->collisionValues = values;
    impl_->markCollidersDirty(map);
}

// ---------------------------------------------------------------------------
// Tiles
// ---------------------------------------------------------------------------

uint32_t TilemapResource::getTile(TilemapHandle map, int32_t layer, int32_t x, int32_t y) const {
    const Layer* l = impl_->layer(map, layer);
    return l ? (l->tile(x, y) & ~TileFlagsMask) : 0u;
}

uint32_t TilemapResource::getTileFlags(TilemapHandle map, int32_t layer, int32_t x, int32_t y) const {
    const Layer* l = impl_->layer(map, layer);
    return l ? (l->tile(x, y) & TileFlagsMask) : 0u;
}

void TilemapResource::setTile(TilemapHandle map, int32_t layer, int32_t x, int32_t y, uint32_t tile) {
    Layer* l = impl_->layer(map, layer);
    if (!l || l->kind == TilemapLayerKind::Objects || l->kind == TilemapLayerKind::Image) return;
    const MapData* m = impl_->get(map);
    auto solid = [&](uint32_t id) {
        id &= ~TileFlagsMask;
        const Tileset* ts = id ? m->tilesetFor(id) : nullptr;
        const TileInfo* info = ts ? ts->info(id - ts->firstgid) : nullptr;
        return info && info->solid;
    };
    bool affectsCollision = l->collision || solid(l->tile(x, y)) || solid(tile);
    l->setTile(x, y, tile);
    if (affectsCollision) impl_->markCollidersDirty(map);
}

int32_t TilemapResource::getIntGrid(TilemapHandle map, int32_t layer, int32_t x, int32_t y) const {
    const Layer* l = impl_->layer(map, layer);
    return l ? l->intAt(x, y) : 0;
}

void TilemapResource::setIntGrid(TilemapHandle map, int32_t layer, int32_t x, int32_t y, int32_t value) {
    Layer* l = impl_->layer(map, layer);
    if (!l) return;
    l->setInt(x, y, value);
    if (l->collision) impl_->markCollidersDirty(map);
}

const char* TilemapResource::intGridName(TilemapHandle map, int32_t layer, int32_t value) const {
    const Layer* l = impl_->layer(map, layer);
    if (!l) return "";
    auto it = l->intGridNames.find(value);
    return it == l->intGridNames.end() ? "" : it->second.c_str();
}

const TilemapProperties* TilemapResource::tileProperties(TilemapHandle map, uint32_t tile) const {
    const MapData* m = impl_->get(map);
    tile &= ~TileFlagsMask;
    const Tileset* ts = (m && tile) ? m->tilesetFor(tile) : nullptr;
    const TileInfo* info = ts ? ts->info(tile - ts->firstgid) : nullptr;
    return info ? &info->properties : nullptr;
}

const char* TilemapResource::tileClass(TilemapHandle map, uint32_t tile) const {
    const MapData* m = impl_->get(map);
    tile &= ~TileFlagsMask;
    const Tileset* ts = (m && tile) ? m->tilesetFor(tile) : nullptr;
    const TileInfo* info = ts ? ts->info(tile - ts->firstgid) : nullptr;
    return info ? info->cls.c_str() : "";
}

TileCoord TilemapResource::worldToTile(TilemapHandle map, Vec2 world, int32_t layer) const {
    const MapData* m = impl_->get(map);
    const Layer* l = impl_->layer(map, layer);
    if (!m) return {};
    float cw = static_cast<float>(l ? l->cellW : m->tileW), ch = static_cast<float>(l ? l->cellH : m->tileH);
    Vec2 off = l ? l->offset : Vec2{};
    float lx = (world.x - m->origin.x) / (m->scale.x != 0.f ? m->scale.x : 1.f) - off.x;
    float ly = (world.y - m->origin.y) / (m->scale.y != 0.f ? m->scale.y : 1.f) - off.y;
    return {static_cast<int32_t>(std::floor(lx / cw)), static_cast<int32_t>(std::floor(ly / ch))};
}

Vec2 TilemapResource::tileToWorld(TilemapHandle map, TileCoord cell, int32_t layer) const {
    const MapData* m = impl_->get(map);
    const Layer* l = impl_->layer(map, layer);
    if (!m) return {};
    float cw = static_cast<float>(l ? l->cellW : m->tileW), ch = static_cast<float>(l ? l->cellH : m->tileH);
    Vec2 off = l ? l->offset : Vec2{};
    return {m->origin.x + (cell.x * cw + off.x) * m->scale.x, m->origin.y + (cell.y * ch + off.y) * m->scale.y};
}

uint32_t TilemapResource::tileAt(TilemapHandle map, int32_t layer, Vec2 world) const {
    TileCoord c = worldToTile(map, world, layer);
    return getTile(map, layer, c.x, c.y);
}

Vec2 TilemapResource::origin(TilemapHandle map) const {
    const MapData* m = impl_->get(map);
    return m ? m->origin : Vec2{};
}

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

const std::vector<MapObject>& TilemapResource::objects(TilemapHandle map, int32_t layer) const {
    const Layer* l = impl_->layer(map, layer);
    return l ? l->objects : kNoObjects;
}

std::vector<const MapObject*> TilemapResource::findObjects(TilemapHandle map, std::string_view type) const {
    std::vector<const MapObject*> out;
    const MapData* m = impl_->get(map);
    if (!m) return out;
    for (const Layer& l : m->layers) {
        for (const MapObject& o : l.objects) {
            if (type.empty() || o.type == type) out.push_back(&o);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Collision
// ---------------------------------------------------------------------------

std::vector<Rect> TilemapResource::collisionRects(TilemapHandle map, int32_t layer,
                                                  const std::vector<int32_t>& values) const {
    std::vector<Rect> out;
    const Layer* l = impl_->layer(map, layer);
    if (!l || l->empty) return out;
    int32_t w = l->maxX - l->minX, h = l->maxY - l->minY;
    std::vector<uint8_t> solid(static_cast<size_t>(w) * h, 0);
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            int32_t v = cellValue(*l, l->minX + x, l->minY + y);
            solid[static_cast<size_t>(y) * w + x] = v != 0 && (values.empty() || containsValue(values, v));
        }
    }
    mergeRects(solid, w, h, l->minX, l->minY, static_cast<float>(l->cellW), static_cast<float>(l->cellH),
               l->offset, out);
    return out;
}

std::vector<Rect> TilemapResource::collisionRects(TilemapHandle map) const {
    std::vector<Rect> out;
    const MapData* m = impl_->get(map);
    if (!m) return out;

    // Layers that share a grid are merged together so rectangles span them.
    struct Grid {
        int32_t cellW, cellH;
        Vec2 offset;
        int32_t minX, minY, maxX, maxY;
        std::vector<const Layer*> layers;
    };
    std::vector<Grid> grids;
    auto solidTile = [&](uint32_t id) {
        id &= ~TileFlagsMask;
        const Tileset* ts = id ? m->tilesetFor(id) : nullptr;
        const TileInfo* info = ts ? ts->info(id - ts->firstgid) : nullptr;
        return info && info->solid;
    };
    bool anySolidTiles = false;
    for (const Tileset& ts : m->tilesets)
        for (auto& [id, info] : ts.tiles) anySolidTiles = anySolidTiles || info.solid;

    for (const Layer& l : m->layers) {
        if (l.empty) continue;
        bool tilesCanBeSolid = anySolidTiles && l.kind != TilemapLayerKind::Objects;
        if (!l.collision && !tilesCanBeSolid) continue;
        Grid* g = nullptr;
        for (Grid& c : grids) {
            if (c.cellW == l.cellW && c.cellH == l.cellH && c.offset.x == l.offset.x && c.offset.y == l.offset.y) g = &c;
        }
        if (!g) {
            grids.push_back({l.cellW, l.cellH, l.offset, l.minX, l.minY, l.maxX, l.maxY, {}});
            g = &grids.back();
        }
        g->minX = std::min(g->minX, l.minX); g->minY = std::min(g->minY, l.minY);
        g->maxX = std::max(g->maxX, l.maxX); g->maxY = std::max(g->maxY, l.maxY);
        g->layers.push_back(&l);
    }

    for (const Grid& g : grids) {
        int32_t w = g.maxX - g.minX, h = g.maxY - g.minY;
        std::vector<uint8_t> solid(static_cast<size_t>(w) * h, 0);
        for (const Layer* l : g.layers) {
            for (int32_t y = l->minY; y < l->maxY; ++y) {
                for (int32_t x = l->minX; x < l->maxX; ++x) {
                    bool s = false;
                    if (l->collision) {
                        int32_t v = cellValue(*l, x, y);
                        s = v != 0 && (l->collisionValues.empty() || containsValue(l->collisionValues, v));
                    }
                    if (!s && l->kind != TilemapLayerKind::Objects) s = solidTile(l->tile(x, y));
                    if (s) solid[static_cast<size_t>(y - g.minY) * w + (x - g.minX)] = 1;
                }
            }
        }
        mergeRects(solid, w, h, g.minX, g.minY, static_cast<float>(g.cellW), static_cast<float>(g.cellH),
                   g.offset, out);
    }
    return out;
}

void TilemapResource::refreshColliders(EntityId mapEntity) {
    auto it = impl_->entities.find(mapEntity);
    if (it != impl_->entities.end()) it->second.collidersDirty = true;
}

// ---------------------------------------------------------------------------
// Autotiling
// ---------------------------------------------------------------------------

void TilemapResource::autotile(TilemapHandle map, int32_t source, int32_t target, const AutotileRules& rules) {
    const Layer* src = impl_->layer(map, source);
    if (!src || src->empty) return;
    autotile(map, source, target, rules, {src->minX, src->minY}, {src->maxX - 1, src->maxY - 1});
}

void TilemapResource::autotile(TilemapHandle map, int32_t source, int32_t target, const AutotileRules& rules,
                               TileCoord regionMin, TileCoord regionMax) {
    const Layer* src = impl_->layer(map, source);
    Layer* dst = impl_->layer(map, target);
    if (!src || !dst) return;
    size_t expected = rules.mode == AutotileMode::Neighbours4 ? 16u : 256u;
    if (rules.tiles.size() < expected) {
        DRIFT_LOG_WARN("tilemap: autotile rules need %zu tiles, got %zu", expected, rules.tiles.size());
    }

    auto terrain = [&](int32_t x, int32_t y) {
        if (src->empty || x < src->minX || y < src->minY || x >= src->maxX || y >= src->maxY) {
            return rules.edgesAreTerrain;
        }
        int32_t v = cellValue(*src, x, y);
        return v != 0 && (rules.terrain.empty() || containsValue(rules.terrain, v));
    };

    bool changed = false;
    for (int32_t y = regionMin.y - 1; y <= regionMax.y + 1; ++y) {
        for (int32_t x = regionMin.x - 1; x <= regionMax.x + 1; ++x) {
            uint32_t tile = 0;
            if (terrain(x, y) && !(src->empty || x < src->minX || y < src->minY || x >= src->maxX || y >= src->maxY)) {
                bool n = terrain(x, y - 1), e = terrain(x + 1, y), s = terrain(x, y + 1), w = terrain(x - 1, y);
                uint32_t mask = 0;
                if (rules.mode == AutotileMode::Neighbours4) {
                    mask = (n ? 1u : 0u) | (e ? 2u : 0u) | (s ? 4u : 0u) | (w ? 8u : 0u);
                } else {
                    bool nw = n && w && terrain(x - 1, y - 1);
                    bool ne = n && e && terrain(x + 1, y - 1);
                    bool sw = s && w && terrain(x - 1, y + 1);
                    bool se = s && e && terrain(x + 1, y + 1);
                    mask = (nw ? 1u : 0u) | (n ? 2u : 0u) | (ne ? 4u : 0u) | (w ? 8u : 0u) |
                           (e ? 16u : 0u) | (sw ? 32u : 0u) | (s ? 64u : 0u) | (se ? 128u : 0u);
                }
                tile = mask < rules.tiles.size() ? rules.tiles[mask] : 0u;
                if (!tile) tile = rules.fallback;
            }
            if (dst->tile(x, y) != tile) {
                dst->setTile(x, y, tile);
                changed = true;
            }
        }
    }
    if (changed && dst->collision) impl_->markCollidersDirty(map);
}

// ---------------------------------------------------------------------------
// Spawning
// ---------------------------------------------------------------------------

int32_t TilemapResource::addObjectSpawner(std::string_view type, ObjectSpawner spawner) {
    int32_t id = impl_->nextSpawnerId++;
    impl_->spawners.push_back({id, std::string(type), std::move(spawner)});
    return id;
}

void TilemapResource::removeObjectSpawner(int32_t id) {
    auto& s = impl_->spawners;
    s.erase(std::remove_if(s.begin(), s.end(), [id](const Impl::Spawner& sp) { return sp.id == id; }), s.end());
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void TilemapResource::setAnimationTime(double seconds) { impl_->time = seconds; }

void TilemapResource::drawTilemap(TilemapHandle map, const TilemapDrawParams& params) {
    MapData* m = impl_->get(map);
    if (!m) return;
    RendererResource& r = impl_->renderer;

    Vec2 scale = params.scale;
    Vec2 topLeft = {params.origin.x + m->worldOffset.x * scale.x, params.origin.y + m->worldOffset.y * scale.y};
    m->origin = topLeft;
    m->scale = scale;

    CameraHandle cam = params.camera.valid() ? params.camera : r.getDefaultCamera();
    Vec2 camPos = r.getCameraPosition(cam);
    Rect view = r.cameraWorldBounds(cam);
    Vec2 parallaxOrigin = {topLeft.x + m->parallaxOrigin.x * scale.x, topLeft.y + m->parallaxOrigin.y * scale.y};

    uint8_t prevLayer = r.layer();
    for (size_t i = 0; i < m->layers.size(); ++i) {
        Layer& l = m->layers[i];
        if (!l.visible || l.kind == TilemapLayerKind::Objects || l.opacity <= 0.f) continue;

        float z = l.hasZ ? l.z : params.zOrder + static_cast<float>(i) * params.zStep;
        r.setLayer(static_cast<uint8_t>(l.renderLayer >= 0 ? l.renderLayer : params.renderLayer));

        // A parallax factor p makes the layer follow p of the camera's
        // motion away from the parallax origin.
        Vec2 shift = {(camPos.x - parallaxOrigin.x) * (1.f - l.parallax.x),
                      (camPos.y - parallaxOrigin.y) * (1.f - l.parallax.y)};
        Vec2 lo = {topLeft.x + l.offset.x * scale.x + shift.x, topLeft.y + l.offset.y * scale.y + shift.y};
        Color tint(l.tint.r, l.tint.g, l.tint.b,
                   static_cast<uint8_t>(std::clamp(l.opacity * l.tint.a, 0.f, 255.f)));

        if (l.kind == TilemapLayerKind::Image) {
            impl_->drawImageLayer(l, lo, scale, view, tint, z);
            continue;
        }

        bool plainTint = tint.r == 255 && tint.g == 255 && tint.b == 255 && tint.a == 255;
        for (auto& [key, rc] : l.render) {
            if (rc.dirty) buildChunk(*m, l, key, rc);
            if (rc.meshes.empty()) continue;
            if (params.cull) {
                Rect wb{lo.x + rc.bounds.x * scale.x, lo.y + rc.bounds.y * scale.y,
                        rc.bounds.w * scale.x, rc.bounds.h * scale.y};
                if (wb.w < 0.f) { wb.x += wb.w; wb.w = -wb.w; }
                if (wb.h < 0.f) { wb.y += wb.h; wb.h = -wb.h; }
                // A chosen camera culls alone; otherwise a chunk any
                // camera may show is drawn, so split screens keep theirs.
                bool shown = params.camera.valid() ? wb.overlaps(view) : r.visible(wb, r.layer());
                if (!shown) continue;
            }
            for (SubMesh& sm : rc.meshes) {
                if (!sm.anims.empty()) updateAnimations(sm, impl_->time);
                auto& out = impl_->scratch;
                out.resize(sm.vertices.size());
                for (size_t v = 0; v < sm.vertices.size(); ++v) {
                    const auto& in = sm.vertices[v];
                    out[v].position = {lo.x + in.position.x * scale.x, lo.y + in.position.y * scale.y};
                    out[v].uv = in.uv;
                    if (plainTint) {
                        out[v].color = in.color;
                    } else {
                        out[v].color = Color(static_cast<uint8_t>(in.color.r * tint.r / 255),
                                             static_cast<uint8_t>(in.color.g * tint.g / 255),
                                             static_cast<uint8_t>(in.color.b * tint.b / 255),
                                             static_cast<uint8_t>(in.color.a * tint.a / 255));
                    }
                }
                r.drawMesh(sm.texture, out.data(), static_cast<int32_t>(out.size()), sm.indices.data(),
                           static_cast<int32_t>(sm.indices.size()), z);
            }
        }
    }
    r.setLayer(prevLayer);
}

} // namespace drift
