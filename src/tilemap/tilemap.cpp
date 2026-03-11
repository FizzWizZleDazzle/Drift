// drift/src/tilemap/tilemap.cpp
// Tilemap loading and rendering for Drift 2D engine
// Supports Tiled JSON format (.tmj)

#include <drift/tilemap.h>
#include <drift/renderer.h>
#include <drift/types.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

struct TilesetInfo {
    int32_t      firstgid;
    DriftTexture texture;
    int32_t      tile_width;
    int32_t      tile_height;
    int32_t      columns;
    std::string  image_path;
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
    std::string           name;
    int32_t               width;
    int32_t               height;
    std::vector<int32_t>  data;
};

struct TilemapData {
    int32_t                    width;
    int32_t                    height;
    int32_t                    tile_width;
    int32_t                    tile_height;
    std::vector<TileLayer>     layers;
    std::vector<ObjectGroup>   object_groups;
    std::vector<TilesetInfo>   tilesets;
};

// ---------------------------------------------------------------------------
// Global storage
// ---------------------------------------------------------------------------

static std::vector<TilemapData> s_tilemaps;
static uint32_t                 s_next_id = 1;

// ---------------------------------------------------------------------------
// Tiled flip flags (stored in the high bits of each tile id)
// ---------------------------------------------------------------------------

static constexpr uint32_t FLIP_HORIZONTAL = 0x80000000u;
static constexpr uint32_t FLIP_VERTICAL   = 0x40000000u;
static constexpr uint32_t FLIP_DIAGONAL   = 0x20000000u;
static constexpr uint32_t FLIP_MASK       = FLIP_HORIZONTAL | FLIP_VERTICAL | FLIP_DIAGONAL;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static TilemapData* tilemap_get(DriftTilemap map)
{
    uint32_t index = map.id - 1;
    if (index >= static_cast<uint32_t>(s_tilemaps.size())) {
        return nullptr;
    }
    return &s_tilemaps[index];
}

static bool tileset_source_rect(const TilemapData& tm, int32_t tile_id,
                                DriftTexture* out_texture, DriftRect* out_src)
{
    const TilesetInfo* ts = nullptr;
    for (int i = static_cast<int>(tm.tilesets.size()) - 1; i >= 0; --i) {
        if (tile_id >= tm.tilesets[i].firstgid) {
            ts = &tm.tilesets[i];
            break;
        }
    }
    if (!ts) return false;

    int32_t local_id = tile_id - ts->firstgid;
    int32_t col = local_id % ts->columns;
    int32_t row = local_id / ts->columns;

    *out_texture = ts->texture;
    out_src->x = static_cast<float>(col * ts->tile_width);
    out_src->y = static_cast<float>(row * ts->tile_height);
    out_src->w = static_cast<float>(ts->tile_width);
    out_src->h = static_cast<float>(ts->tile_height);
    return true;
}

static DriftFlip flip_flags_to_drift(uint32_t raw)
{
    int flip = DRIFT_FLIP_NONE;
    if (raw & FLIP_HORIZONTAL) flip |= DRIFT_FLIP_H;
    if (raw & FLIP_VERTICAL)   flip |= DRIFT_FLIP_V;
    if (raw & FLIP_DIAGONAL)   flip |= (DRIFT_FLIP_H | DRIFT_FLIP_V);
    return static_cast<DriftFlip>(flip);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

DriftTilemap drift_tilemap_load(const char* path)
{
    DriftTilemap handle{};

    std::ifstream file(path);
    if (!file.is_open()) {
        std::fprintf(stderr, "[drift] tilemap: failed to open %s\n", path);
        return handle;
    }

    json root;
    try {
        file >> root;
    } catch (const json::parse_error& e) {
        std::fprintf(stderr, "[drift] tilemap: JSON parse error in %s: %s\n",
                     path, e.what());
        return handle;
    }

    TilemapData tm{};
    tm.width       = root.value("width",      0);
    tm.height      = root.value("height",     0);
    tm.tile_width  = root.value("tilewidth",  0);
    tm.tile_height = root.value("tileheight", 0);

    // ----- Tilesets -----
    if (root.contains("tilesets") && root["tilesets"].is_array()) {
        for (auto& ts_json : root["tilesets"]) {
            TilesetInfo ts{};
            ts.firstgid    = ts_json.value("firstgid",   1);
            ts.tile_width  = ts_json.value("tilewidth",  tm.tile_width);
            ts.tile_height = ts_json.value("tileheight", tm.tile_height);
            ts.columns     = ts_json.value("columns",    1);
            ts.image_path  = ts_json.value("image",      std::string());

            // Resolve relative image path against the directory of the map file.
            std::string base_dir;
            {
                std::string p(path);
                auto sep = p.find_last_of("/\\");
                if (sep != std::string::npos) {
                    base_dir = p.substr(0, sep + 1);
                }
            }
            std::string full_image_path = base_dir + ts.image_path;
            ts.texture = drift_texture_load(full_image_path.c_str());

            tm.tilesets.push_back(std::move(ts));
        }
    }

    // ----- Layers -----
    if (root.contains("layers") && root["layers"].is_array()) {
        for (auto& layer_json : root["layers"]) {
            std::string layer_type = layer_json.value("type", std::string());

            if (layer_type == "tilelayer") {
                TileLayer layer{};
                layer.name   = layer_json.value("name",   std::string());
                layer.width  = layer_json.value("width",  tm.width);
                layer.height = layer_json.value("height", tm.height);

                if (layer_json.contains("data") && layer_json["data"].is_array()) {
                    layer.data.reserve(layer_json["data"].size());
                    for (auto& val : layer_json["data"]) {
                        layer.data.push_back(val.get<int32_t>());
                    }
                }

                tm.layers.push_back(std::move(layer));
            } else if (layer_type == "objectgroup") {
                ObjectGroup group{};
                group.name = layer_json.value("name", std::string());

                if (layer_json.contains("objects") && layer_json["objects"].is_array()) {
                    for (auto& obj_json : layer_json["objects"]) {
                        MapObject obj{};
                        obj.name = obj_json.value("name", std::string());
                        obj.type = obj_json.value("type", std::string());
                        obj.x    = obj_json.value("x",    0.0f);
                        obj.y    = obj_json.value("y",    0.0f);
                        obj.w    = obj_json.value("width",  0.0f);
                        obj.h    = obj_json.value("height", 0.0f);
                        obj.id   = obj_json.value("id",   0);
                        group.objects.push_back(std::move(obj));
                    }
                }

                tm.object_groups.push_back(std::move(group));
            }
        }
    }

    s_tilemaps.push_back(std::move(tm));
    handle.id = s_next_id++;
    return handle;
}

void drift_tilemap_destroy(DriftTilemap map)
{
    TilemapData* tm = tilemap_get(map);
    if (!tm) return;
    *tm = TilemapData{};
}

void drift_tilemap_draw(DriftTilemap map)
{
    TilemapData* tm = tilemap_get(map);
    if (!tm) return;

    for (const TileLayer& layer : tm->layers) {
        for (int32_t y = 0; y < layer.height; ++y) {
            for (int32_t x = 0; x < layer.width; ++x) {
                int32_t raw_id = layer.data[static_cast<size_t>(y) * layer.width + x];
                if (raw_id == 0) continue;

                uint32_t flip_bits = static_cast<uint32_t>(raw_id) & FLIP_MASK;
                int32_t  tile_id   = static_cast<int32_t>(static_cast<uint32_t>(raw_id) & ~FLIP_MASK);

                DriftTexture texture{};
                DriftRect src{};
                if (!tileset_source_rect(*tm, tile_id, &texture, &src)) {
                    continue;
                }

                DriftVec2 pos;
                pos.x = static_cast<float>(x * tm->tile_width);
                pos.y = static_cast<float>(y * tm->tile_height);

                DriftFlip flip = flip_flags_to_drift(flip_bits);

                drift_sprite_draw(texture,
                                  pos,
                                  src,
                                  DriftVec2{1.0f, 1.0f},
                                  0.0f,
                                  DriftVec2{0.0f, 0.0f},
                                  DRIFT_COLOR_WHITE,
                                  flip,
                                  0.0f);
            }
        }
    }
}

int32_t drift_tilemap_get_tile(DriftTilemap map, int32_t layer, int32_t x, int32_t y)
{
    TilemapData* tm = tilemap_get(map);
    if (!tm) return 0;
    if (layer < 0 || layer >= static_cast<int32_t>(tm->layers.size())) return 0;

    const TileLayer& l = tm->layers[layer];
    if (x < 0 || x >= l.width || y < 0 || y >= l.height) return 0;

    int32_t raw = l.data[static_cast<size_t>(y) * l.width + x];
    return static_cast<int32_t>(static_cast<uint32_t>(raw) & ~FLIP_MASK);
}

void drift_tilemap_set_tile(DriftTilemap map, int32_t layer, int32_t x, int32_t y, int32_t tile_id)
{
    TilemapData* tm = tilemap_get(map);
    if (!tm) return;
    if (layer < 0 || layer >= static_cast<int32_t>(tm->layers.size())) return;

    TileLayer& l = tm->layers[layer];
    if (x < 0 || x >= l.width || y < 0 || y >= l.height) return;

    l.data[static_cast<size_t>(y) * l.width + x] = tile_id;
}

int32_t drift_tilemap_get_layer_count(DriftTilemap map)
{
    TilemapData* tm = tilemap_get(map);
    return tm ? static_cast<int32_t>(tm->layers.size()) : 0;
}

int32_t drift_tilemap_get_width(DriftTilemap map)
{
    TilemapData* tm = tilemap_get(map);
    return tm ? tm->width : 0;
}

int32_t drift_tilemap_get_height(DriftTilemap map)
{
    TilemapData* tm = tilemap_get(map);
    return tm ? tm->height : 0;
}

int32_t drift_tilemap_get_tile_width(DriftTilemap map)
{
    TilemapData* tm = tilemap_get(map);
    return tm ? tm->tile_width : 0;
}

int32_t drift_tilemap_get_tile_height(DriftTilemap map)
{
    TilemapData* tm = tilemap_get(map);
    return tm ? tm->tile_height : 0;
}

int32_t drift_tilemap_get_collision_rects(DriftTilemap map, DriftRect* out_rects, int32_t max_rects)
{
    TilemapData* tm = tilemap_get(map);
    if (!tm || !out_rects || max_rects <= 0) return 0;

    int32_t count = 0;

    for (const TileLayer& layer : tm->layers) {
        bool is_collision = false;
        std::string lower_name = layer.name;
        for (auto& c : lower_name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower_name.find("collision") != std::string::npos) {
            is_collision = true;
        }
        if (!is_collision) continue;

        for (int32_t y = 0; y < layer.height; ++y) {
            for (int32_t x = 0; x < layer.width; ++x) {
                int32_t raw = layer.data[static_cast<size_t>(y) * layer.width + x];
                int32_t tile_id = static_cast<int32_t>(static_cast<uint32_t>(raw) & ~FLIP_MASK);
                if (tile_id == 0) continue;

                if (count >= max_rects) return count;

                DriftRect& r = out_rects[count];
                r.x = static_cast<float>(x * tm->tile_width);
                r.y = static_cast<float>(y * tm->tile_height);
                r.w = static_cast<float>(tm->tile_width);
                r.h = static_cast<float>(tm->tile_height);
                ++count;
            }
        }
    }

    return count;
}

int32_t drift_tilemap_get_objects(DriftTilemap map, const char* layer_name,
                                  DriftTilemapObject* out_objects, int32_t max_objects)
{
    TilemapData* tm = tilemap_get(map);
    if (!tm || !layer_name || !out_objects || max_objects <= 0) return 0;

    int32_t count = 0;

    for (const ObjectGroup& group : tm->object_groups) {
        if (group.name != layer_name) continue;

        for (const MapObject& obj : group.objects) {
            if (count >= max_objects) return count;

            DriftTilemapObject& out = out_objects[count];
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
