#include <drift/resources/TilemapResource.h>
#include <drift/resources/RendererResource.h>
#include <drift/Log.h>
#include "core/HandlePool.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace drift {

// ---------------------------------------------------------------------------
// Tiled flip flags (stored in the high bits of each tile id)
// ---------------------------------------------------------------------------

static constexpr uint32_t FLIP_HORIZONTAL = 0x80000000u;
static constexpr uint32_t FLIP_VERTICAL   = 0x40000000u;
static constexpr uint32_t FLIP_DIAGONAL   = 0x20000000u;
static constexpr uint32_t FLIP_MASK       = FLIP_HORIZONTAL | FLIP_VERTICAL | FLIP_DIAGONAL;

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

struct TilesetInfo {
    int32_t       firstgid;
    TextureHandle texture;
    int32_t       tileWidth;
    int32_t       tileHeight;
    int32_t       columns;
    std::string   imagePath;
};

struct MapObject {
    std::string name;
    std::string type;
    float       x;
    float       y;
    float       w;
    float       h;
    int32_t     id;
};

struct ObjectGroup {
    std::string            name;
    std::vector<MapObject> objects;
};

struct TileLayer {
    std::string          name;
    int32_t              width;
    int32_t              height;
    std::vector<int32_t> data;
};

struct TilemapData {
    int32_t                  width;
    int32_t                  height;
    int32_t                  tileWidth;
    int32_t                  tileHeight;
    std::vector<TileLayer>   layers;
    std::vector<ObjectGroup> objectGroups;
    std::vector<TilesetInfo> tilesets;
};

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct TilemapResource::Impl {
    RendererResource& renderer;
    HandlePool<TilemapTag, TilemapData> tilemaps;

    Impl(RendererResource& r) : renderer(r) {}
};

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static bool tilesetSourceRect(const TilemapData& tm, int32_t tileId,
                              TextureHandle* outTexture, Rect* outSrc) {
    const TilesetInfo* ts = nullptr;
    for (int i = static_cast<int>(tm.tilesets.size()) - 1; i >= 0; --i) {
        if (tileId >= tm.tilesets[i].firstgid) {
            ts = &tm.tilesets[i];
            break;
        }
    }
    if (!ts) return false;

    int32_t localId = tileId - ts->firstgid;
    int32_t col = localId % ts->columns;
    int32_t row = localId / ts->columns;

    *outTexture = ts->texture;
    outSrc->x = static_cast<float>(col * ts->tileWidth);
    outSrc->y = static_cast<float>(row * ts->tileHeight);
    outSrc->w = static_cast<float>(ts->tileWidth);
    outSrc->h = static_cast<float>(ts->tileHeight);
    return true;
}

static Flip flipFlagsToFlip(uint32_t raw) {
    int flip = static_cast<int>(Flip::None);
    if (raw & FLIP_HORIZONTAL) flip |= static_cast<int>(Flip::H);
    if (raw & FLIP_VERTICAL)   flip |= static_cast<int>(Flip::V);
    if (raw & FLIP_DIAGONAL)   flip |= static_cast<int>(Flip::H) | static_cast<int>(Flip::V);
    return static_cast<Flip>(flip);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

TilemapResource::TilemapResource(RendererResource& renderer)
    : impl_(new Impl(renderer)) {}

TilemapResource::~TilemapResource() { delete impl_; }

TilemapHandle TilemapResource::loadTilemap(const char* path) {
    if (!path) return TilemapHandle{};

    std::ifstream file(path);
    if (!file.is_open()) {
        DRIFT_LOG_ERROR("tilemap: failed to open %s", path);
        return TilemapHandle{};
    }

    json root;
    try {
        file >> root;
    } catch (const json::parse_error& e) {
        DRIFT_LOG_ERROR("tilemap: JSON parse error in %s: %s", path, e.what());
        return TilemapHandle{};
    }

    TilemapData tm{};
    tm.width      = root.value("width", 0);
    tm.height     = root.value("height", 0);
    tm.tileWidth  = root.value("tilewidth", 0);
    tm.tileHeight = root.value("tileheight", 0);

    // Resolve base directory from path
    std::string baseDir;
    {
        std::string p(path);
        auto sep = p.find_last_of("/\\");
        if (sep != std::string::npos) {
            baseDir = p.substr(0, sep + 1);
        }
    }

    // Tilesets
    if (root.contains("tilesets") && root["tilesets"].is_array()) {
        for (auto& tsJson : root["tilesets"]) {
            TilesetInfo ts{};
            ts.firstgid   = tsJson.value("firstgid", 1);
            ts.tileWidth  = tsJson.value("tilewidth", tm.tileWidth);
            ts.tileHeight = tsJson.value("tileheight", tm.tileHeight);
            ts.columns    = tsJson.value("columns", 1);
            ts.imagePath  = tsJson.value("image", std::string());

            std::string fullPath = baseDir + ts.imagePath;
            ts.texture = impl_->renderer.loadTexture(fullPath.c_str());

            tm.tilesets.push_back(std::move(ts));
        }
    }

    // Layers
    if (root.contains("layers") && root["layers"].is_array()) {
        for (auto& layerJson : root["layers"]) {
            std::string layerType = layerJson.value("type", std::string());

            if (layerType == "tilelayer") {
                TileLayer layer{};
                layer.name   = layerJson.value("name", std::string());
                layer.width  = layerJson.value("width", tm.width);
                layer.height = layerJson.value("height", tm.height);

                if (layerJson.contains("data") && layerJson["data"].is_array()) {
                    layer.data.reserve(layerJson["data"].size());
                    for (auto& val : layerJson["data"]) {
                        layer.data.push_back(val.get<int32_t>());
                    }
                }

                tm.layers.push_back(std::move(layer));
            } else if (layerType == "objectgroup") {
                ObjectGroup group{};
                group.name = layerJson.value("name", std::string());

                if (layerJson.contains("objects") && layerJson["objects"].is_array()) {
                    for (auto& objJson : layerJson["objects"]) {
                        MapObject obj{};
                        obj.name = objJson.value("name", std::string());
                        obj.type = objJson.value("type", std::string());
                        obj.x    = objJson.value("x", 0.f);
                        obj.y    = objJson.value("y", 0.f);
                        obj.w    = objJson.value("width", 0.f);
                        obj.h    = objJson.value("height", 0.f);
                        obj.id   = objJson.value("id", 0);
                        group.objects.push_back(std::move(obj));
                    }
                }

                tm.objectGroups.push_back(std::move(group));
            }
        }
    }

    return impl_->tilemaps.create(std::move(tm));
}

void TilemapResource::destroyTilemap(TilemapHandle map) {
    impl_->tilemaps.destroy(map);
}

void TilemapResource::drawTilemap(TilemapHandle map) {
    auto* tm = impl_->tilemaps.get(map);
    if (!tm) return;

    for (const TileLayer& layer : tm->layers) {
        for (int32_t y = 0; y < layer.height; ++y) {
            for (int32_t x = 0; x < layer.width; ++x) {
                int32_t rawId = layer.data[static_cast<size_t>(y) * layer.width + x];
                if (rawId == 0) continue;

                uint32_t flipBits = static_cast<uint32_t>(rawId) & FLIP_MASK;
                int32_t  tileId   = static_cast<int32_t>(static_cast<uint32_t>(rawId) & ~FLIP_MASK);

                TextureHandle texture{};
                Rect src{};
                if (!tilesetSourceRect(*tm, tileId, &texture, &src)) continue;

                Vec2 pos;
                pos.x = static_cast<float>(x * tm->tileWidth);
                pos.y = static_cast<float>(y * tm->tileHeight);

                Flip flip = flipFlagsToFlip(flipBits);

                impl_->renderer.drawSprite(
                    texture, pos, src, Vec2::one(), 0.f, {}, Color::white(), flip, 0.f);
            }
        }
    }
}

int32_t TilemapResource::getTile(TilemapHandle map, int32_t layer, int32_t x, int32_t y) const {
    auto* tm = impl_->tilemaps.get(map);
    if (!tm) return 0;
    if (layer < 0 || layer >= static_cast<int32_t>(tm->layers.size())) return 0;

    const TileLayer& l = tm->layers[layer];
    if (x < 0 || x >= l.width || y < 0 || y >= l.height) return 0;

    int32_t raw = l.data[static_cast<size_t>(y) * l.width + x];
    return static_cast<int32_t>(static_cast<uint32_t>(raw) & ~FLIP_MASK);
}

void TilemapResource::setTile(TilemapHandle map, int32_t layer, int32_t x, int32_t y, int32_t tileId) {
    auto* tm = impl_->tilemaps.get(map);
    if (!tm) return;
    if (layer < 0 || layer >= static_cast<int32_t>(tm->layers.size())) return;

    TileLayer& l = tm->layers[layer];
    if (x < 0 || x >= l.width || y < 0 || y >= l.height) return;

    l.data[static_cast<size_t>(y) * l.width + x] = tileId;
}

int32_t TilemapResource::layerCount(TilemapHandle map) const {
    auto* tm = impl_->tilemaps.get(map);
    return tm ? static_cast<int32_t>(tm->layers.size()) : 0;
}

int32_t TilemapResource::mapWidth(TilemapHandle map) const {
    auto* tm = impl_->tilemaps.get(map);
    return tm ? tm->width : 0;
}

int32_t TilemapResource::mapHeight(TilemapHandle map) const {
    auto* tm = impl_->tilemaps.get(map);
    return tm ? tm->height : 0;
}

int32_t TilemapResource::tileWidth(TilemapHandle map) const {
    auto* tm = impl_->tilemaps.get(map);
    return tm ? tm->tileWidth : 0;
}

int32_t TilemapResource::tileHeight(TilemapHandle map) const {
    auto* tm = impl_->tilemaps.get(map);
    return tm ? tm->tileHeight : 0;
}

int32_t TilemapResource::getCollisionRects(TilemapHandle map, Rect* outRects, int32_t maxRects) const {
    auto* tm = impl_->tilemaps.get(map);
    if (!tm || !outRects || maxRects <= 0) return 0;

    int32_t count = 0;

    for (const TileLayer& layer : tm->layers) {
        // Check if layer name contains "collision" (case-insensitive)
        std::string lowerName = layer.name;
        for (auto& c : lowerName) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lowerName.find("collision") == std::string::npos) continue;

        for (int32_t y = 0; y < layer.height; ++y) {
            for (int32_t x = 0; x < layer.width; ++x) {
                int32_t raw = layer.data[static_cast<size_t>(y) * layer.width + x];
                int32_t tileId = static_cast<int32_t>(static_cast<uint32_t>(raw) & ~FLIP_MASK);
                if (tileId == 0) continue;

                if (count >= maxRects) return count;

                Rect& r = outRects[count];
                r.x = static_cast<float>(x * tm->tileWidth);
                r.y = static_cast<float>(y * tm->tileHeight);
                r.w = static_cast<float>(tm->tileWidth);
                r.h = static_cast<float>(tm->tileHeight);
                ++count;
            }
        }
    }

    return count;
}

int32_t TilemapResource::getObjects(TilemapHandle map, const char* layerName,
                                    TilemapObject* outObjects, int32_t maxObjects) const {
    auto* tm = impl_->tilemaps.get(map);
    if (!tm || !layerName || !outObjects || maxObjects <= 0) return 0;

    int32_t count = 0;

    for (const ObjectGroup& group : tm->objectGroups) {
        if (group.name != layerName) continue;

        for (const MapObject& obj : group.objects) {
            if (count >= maxObjects) return count;

            TilemapObject& out = outObjects[count];
            out.name = obj.name.c_str();
            out.type = obj.type.c_str();
            out.rect.x = obj.x;
            out.rect.y = obj.y;
            out.rect.w = obj.w;
            out.rect.h = obj.h;
            out.id = obj.id;
            ++count;
        }
    }

    return count;
}

} // namespace drift
