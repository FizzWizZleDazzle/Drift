// Tiled JSON (.tmj / .json) loader: embedded and external (.tsj) tilesets,
// atlas and image-collection tilesets, finite and infinite maps, CSV and
// base64 (uncompressed, zlib, gzip) tile data, group layers, object and
// image layers, custom properties and tile animations.

#include "tilemap/TilemapData.hpp"

#include <drift/Log.hpp>

#include <nlohmann/json.hpp>
#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

using json = nlohmann::json;

namespace drift::tilemap {

namespace {

constexpr float DegToRad = 3.14159265358979f / 180.f;

void parseProperties(const json& j, TilemapProperties& out) {
    auto it = j.find("properties");
    if (it == j.end() || !it->is_array()) return;
    for (const json& p : *it) {
        TilemapProperty prop;
        prop.name = p.value("name", std::string());
        prop.type = p.value("type", std::string("string"));
        const json& v = p.contains("value") ? p["value"] : json();
        if (v.is_string()) {
            prop.text = v.get<std::string>();
            prop.number = std::atof(prop.text.c_str());
        } else if (v.is_boolean()) {
            prop.number = v.get<bool>() ? 1.0 : 0.0;
            prop.text = v.get<bool>() ? "true" : "false";
        } else if (v.is_number()) {
            prop.number = v.get<double>();
            prop.text = v.dump();
        } else if (!v.is_null()) {
            prop.text = v.dump();
        }
        out.items.push_back(std::move(prop));
    }
}

std::string base64Decode(const std::string& in) {
    static const auto table = [] {
        std::array<int8_t, 256> t{};
        t.fill(-1);
        const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) t[static_cast<uint8_t>(chars[i])] = static_cast<int8_t>(i);
        return t;
    }();
    std::string out;
    out.reserve(in.size() * 3 / 4);
    uint32_t buf = 0;
    int bits = 0;
    for (char c : in) {
        int8_t v = table[static_cast<uint8_t>(c)];
        if (v < 0) continue;   // padding, whitespace
        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

bool inflate(const std::string& in, bool gzip, std::string& out) {
    const char* data = in.data();
    int len = static_cast<int>(in.size());
    int parseHeader = 1;
    if (gzip) {
        // Skip the gzip member header; the rest is raw deflate.
        if (len < 18) return false;
        const auto* b = reinterpret_cast<const uint8_t*>(data);
        uint8_t flags = b[3];
        int pos = 10;
        if (flags & 4) pos += 2 + (b[pos] | (b[pos + 1] << 8));
        if (flags & 8) { while (pos < len && b[pos]) ++pos; ++pos; }
        if (flags & 16) { while (pos < len && b[pos]) ++pos; ++pos; }
        if (flags & 2) pos += 2;
        if (pos >= len) return false;
        data += pos;
        len -= pos;
        parseHeader = 0;
    }
    int outLen = 0;
    char* res = stbi_zlib_decode_malloc_guesssize_headerflag(data, len, len * 4 + 64, &outLen, parseHeader);
    if (!res) return false;
    out.assign(res, static_cast<size_t>(outLen));
    stbi_image_free(res);
    return true;
}

// Decodes a layer or chunk "data" field into tile ids.
bool decodeData(const json& data, const std::string& encoding, const std::string& compression,
                size_t expected, std::vector<uint32_t>& out) {
    out.clear();
    if (data.is_array()) {
        out.reserve(data.size());
        for (const json& v : data) out.push_back(v.get<uint32_t>());
        return true;
    }
    if (!data.is_string() || encoding != "base64") return false;
    std::string bytes = base64Decode(data.get<std::string>());
    if (compression == "zlib" || compression == "gzip") {
        std::string raw;
        if (!inflate(bytes, compression == "gzip", raw)) {
            DRIFT_LOG_ERROR("tilemap: cannot inflate %s tile data", compression.c_str());
            return false;
        }
        bytes.swap(raw);
    } else if (!compression.empty()) {
        DRIFT_LOG_ERROR("tilemap: tile data compression '%s' is not supported (use CSV, zlib or gzip)",
                        compression.c_str());
        return false;
    }
    size_t n = bytes.size() / 4;
    out.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const auto* b = reinterpret_cast<const uint8_t*>(bytes.data() + i * 4);
        out[i] = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
                 (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
    }
    if (expected && n != expected) {
        DRIFT_LOG_WARN("tilemap: tile data has %zu entries, expected %zu", n, expected);
    }
    return true;
}

void storeRegion(Layer& layer, int32_t x0, int32_t y0, int32_t w, int32_t h,
                 const std::vector<uint32_t>& ids) {
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            size_t i = static_cast<size_t>(y) * w + x;
            if (i >= ids.size()) return;
            if (ids[i]) layer.setTile(x0 + x, y0 + y, ids[i]);
        }
    }
}

bool loadTileset(LoadContext& ctx, const json& j, const std::string& baseDir, const MapData& map,
                 Tileset& ts) {
    ts.name = j.value("name", std::string());
    ts.tileW = j.value("tilewidth", map.tileW);
    ts.tileH = j.value("tileheight", map.tileH);
    ts.columns = j.value("columns", 0);
    ts.margin = j.value("margin", 0);
    ts.spacing = j.value("spacing", 0);
    ts.tileCount = j.value("tilecount", 0u);
    ts.gridRenderSize = j.value("tilerendersize", std::string("tile")) == "grid";
    if (j.contains("tileoffset")) {
        ts.tileOffset = {j["tileoffset"].value("x", 0.f), j["tileoffset"].value("y", 0.f)};
    }

    std::string image = j.value("image", std::string());
    if (!image.empty()) {
        ts.texture = ctx.loadTexture(baseDir + image);
        Vec2 sz = ctx.textureSize(ts.texture);
        ts.textureW = j.value("imagewidth", static_cast<int32_t>(sz.x));
        ts.textureH = j.value("imageheight", static_cast<int32_t>(sz.y));
        if (ts.columns <= 0 && ts.tileW > 0) {
            ts.columns = std::max(1, (ts.textureW - 2 * ts.margin + ts.spacing) / (ts.tileW + ts.spacing));
        }
    } else {
        ts.collection = true;
    }

    if (j.contains("tiles") && j["tiles"].is_array()) {
        for (const json& t : j["tiles"]) {
            uint32_t id = t.value("id", 0u);
            TileInfo& info = ts.tiles[id];
            info.cls = t.value("class", t.value("type", std::string()));
            parseProperties(t, info.properties);
            if (t.contains("objectgroup") && t["objectgroup"].contains("objects") &&
                !t["objectgroup"]["objects"].empty()) {
                info.solid = true;
            }
            if (info.properties.getBool("collision") || info.properties.getBool("solid")) {
                info.solid = true;
            }
            if (t.contains("animation") && t["animation"].is_array()) {
                for (const json& f : t["animation"]) {
                    AnimFrame frame;
                    frame.localId = f.value("tileid", 0u);
                    frame.duration = std::max(0.001f, f.value("duration", 100) / 1000.f);
                    info.animationLength += frame.duration;
                    info.animation.push_back(frame);
                }
            }
            std::string tileImage = t.value("image", std::string());
            if (!tileImage.empty()) {
                info.image = ctx.loadTexture(baseDir + tileImage);
                Vec2 sz = ctx.textureSize(info.image);
                info.imageW = t.value("imagewidth", static_cast<int32_t>(sz.x));
                info.imageH = t.value("imageheight", static_cast<int32_t>(sz.y));
            }
            if (ts.collection) ts.tileCount = std::max(ts.tileCount, id + 1);
        }
    }
    return true;
}

struct Inherited {
    Vec2  offset;
    float opacity = 1.f;
    Color tint = Color::white();
    Vec2  parallax = Vec2::one();
    bool  visible = true;
};

Color multiply(Color a, Color b) {
    return Color(static_cast<uint8_t>(a.r * b.r / 255), static_cast<uint8_t>(a.g * b.g / 255),
                 static_cast<uint8_t>(a.b * b.b / 255), static_cast<uint8_t>(a.a * b.a / 255));
}

MapObject parseObject(const json& o) {
    MapObject obj;
    obj.id = o.value("id", 0);
    obj.name = o.value("name", std::string());
    obj.type = o.value("class", o.value("type", std::string()));
    obj.position = {o.value("x", 0.f), o.value("y", 0.f)};
    obj.size = {o.value("width", 0.f), o.value("height", 0.f)};
    obj.rotation = o.value("rotation", 0.f) * DegToRad;
    parseProperties(o, obj.properties);

    if (o.contains("gid")) {
        obj.shape = MapObjectShape::Tile;
        obj.tile = o["gid"].get<uint32_t>();
        // Tile objects are anchored at their bottom-left corner.
        obj.position.y -= obj.size.y;
        obj.pivot = {0.f, 1.f};
    } else if (o.value("point", false)) {
        obj.shape = MapObjectShape::Point;
    } else if (o.value("ellipse", false)) {
        obj.shape = MapObjectShape::Ellipse;
    } else if (o.contains("polygon") || o.contains("polyline")) {
        bool polygon = o.contains("polygon");
        obj.shape = polygon ? MapObjectShape::Polygon : MapObjectShape::Polyline;
        const json& pts = polygon ? o["polygon"] : o["polyline"];
        float minX = 0.f, minY = 0.f, maxX = 0.f, maxY = 0.f;
        bool first = true;
        for (const json& p : pts) {
            Vec2 v{p.value("x", 0.f), p.value("y", 0.f)};
            obj.points.push_back(v);
            if (first) { minX = maxX = v.x; minY = maxY = v.y; first = false; }
            minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
            minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
        }
        // Normalize so position is the top-left of the bounds.
        for (Vec2& v : obj.points) { v.x -= minX; v.y -= minY; }
        obj.pivot = {maxX > minX ? -minX / (maxX - minX) : 0.f, maxY > minY ? -minY / (maxY - minY) : 0.f};
        obj.position = {obj.position.x + minX, obj.position.y + minY};
        obj.size = {maxX - minX, maxY - minY};
    } else if (o.contains("text")) {
        obj.shape = MapObjectShape::Text;
        TilemapProperty text;
        text.name = "text";
        text.type = "string";
        text.text = o["text"].value("text", std::string());
        obj.properties.items.push_back(std::move(text));
    }
    return obj;
}

void loadLayers(LoadContext& ctx, const json& layers, const std::string& baseDir, MapData& map,
                bool infinite, const Inherited& parent) {
    for (const json& lj : layers) {
        std::string type = lj.value("type", std::string());

        Inherited inh;
        inh.offset = {parent.offset.x + lj.value("offsetx", 0.f), parent.offset.y + lj.value("offsety", 0.f)};
        inh.opacity = parent.opacity * lj.value("opacity", 1.f);
        inh.tint = parent.tint;
        if (lj.contains("tintcolor")) inh.tint = multiply(inh.tint, parseColor(lj["tintcolor"].get<std::string>(), Color::white()));
        inh.parallax = {parent.parallax.x * lj.value("parallaxx", 1.f), parent.parallax.y * lj.value("parallaxy", 1.f)};
        inh.visible = parent.visible && lj.value("visible", true);

        if (type == "group") {
            if (lj.contains("layers")) loadLayers(ctx, lj["layers"], baseDir, map, infinite, inh);
            continue;
        }

        Layer layer;
        layer.name = lj.value("name", std::string());
        layer.offset = inh.offset;
        layer.opacity = inh.opacity;
        layer.tint = inh.tint;
        layer.parallax = inh.parallax;
        layer.visible = inh.visible;
        layer.cellW = map.tileW;
        layer.cellH = map.tileH;
        parseProperties(lj, layer.properties);
        if (const TilemapProperty* p = layer.properties.find("zOrder")) { layer.hasZ = true; layer.z = static_cast<float>(p->number); }
        if (const TilemapProperty* p = layer.properties.find("renderLayer")) layer.renderLayer = static_cast<int32_t>(p->number);
        layer.collision = layer.properties.getBool("collision", toLower(layer.name).find("collision") != std::string::npos);

        if (type == "tilelayer") {
            layer.kind = TilemapLayerKind::Tiles;
            std::string encoding = lj.value("encoding", std::string("csv"));
            std::string compression = lj.value("compression", std::string());
            std::vector<uint32_t> ids;
            if (lj.contains("chunks")) {
                for (const json& c : lj["chunks"]) {
                    int32_t w = c.value("width", 0), h = c.value("height", 0);
                    if (decodeData(c["data"], encoding, compression, static_cast<size_t>(w) * h, ids)) {
                        storeRegion(layer, c.value("x", 0), c.value("y", 0), w, h, ids);
                    }
                }
            } else if (lj.contains("data")) {
                int32_t w = lj.value("width", map.width), h = lj.value("height", map.height);
                if (decodeData(lj["data"], encoding, compression, static_cast<size_t>(w) * h, ids)) {
                    storeRegion(layer, lj.value("x", 0), lj.value("y", 0), w, h, ids);
                } else {
                    DRIFT_LOG_ERROR("tilemap: cannot decode layer '%s' in %s", layer.name.c_str(), map.path.c_str());
                }
            }
        } else if (type == "objectgroup") {
            layer.kind = TilemapLayerKind::Objects;
            layer.collision = false;
            if (lj.contains("objects")) {
                for (const json& o : lj["objects"]) layer.objects.push_back(parseObject(o));
            }
        } else if (type == "imagelayer") {
            layer.kind = TilemapLayerKind::Image;
            layer.collision = false;
            std::string image = lj.value("image", std::string());
            if (!image.empty()) {
                layer.image = ctx.loadTexture(baseDir + image);
                layer.imageSize = ctx.textureSize(layer.image);
            }
            layer.repeatX = lj.value("repeatx", false);
            layer.repeatY = lj.value("repeaty", false);
        } else {
            continue;
        }
        map.layers.push_back(std::move(layer));
    }
}

} // namespace

bool loadTiled(LoadContext& ctx, const std::string& path, MapData& map) {
    std::string text = readFile(path);
    if (text.empty()) {
        DRIFT_LOG_ERROR("tilemap: cannot read %s", path.c_str());
        return false;
    }
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        DRIFT_LOG_ERROR("tilemap: %s is not valid JSON", path.c_str());
        return false;
    }

    std::string orientation = root.value("orientation", std::string("orthogonal"));
    if (orientation != "orthogonal") {
        DRIFT_LOG_WARN("tilemap: %s uses %s orientation; only orthogonal maps are drawn correctly",
                       path.c_str(), orientation.c_str());
    }

    std::string baseDir = directoryOf(path);
    map.path = path;
    {
        auto sep = path.find_last_of("/\\");
        std::string file = sep == std::string::npos ? path : path.substr(sep + 1);
        auto dot = file.find_last_of('.');
        map.name = dot == std::string::npos ? file : file.substr(0, dot);
    }
    map.width = root.value("width", 0);
    map.height = root.value("height", 0);
    map.tileW = root.value("tilewidth", 16);
    map.tileH = root.value("tileheight", 16);
    map.pixelSize = {static_cast<float>(map.width * map.tileW), static_cast<float>(map.height * map.tileH)};
    map.parallaxOrigin = {root.value("parallaxoriginx", 0.f), root.value("parallaxoriginy", 0.f)};
    if (root.contains("backgroundcolor")) {
        map.background = parseColor(root["backgroundcolor"].get<std::string>(), map.background);
    }
    parseProperties(root, map.properties);

    if (root.contains("tilesets")) {
        for (const json& tj : root["tilesets"]) {
            Tileset ts;
            ts.firstgid = tj.value("firstgid", 1u);
            if (tj.contains("source")) {
                std::string src = baseDir + tj["source"].get<std::string>();
                std::string tsText = readFile(src);
                json ext = json::parse(tsText, nullptr, false);
                if (ext.is_discarded() || !ext.is_object()) {
                    DRIFT_LOG_ERROR("tilemap: cannot load tileset %s (export tilesets as JSON .tsj, or embed them)",
                                    src.c_str());
                    continue;
                }
                loadTileset(ctx, ext, directoryOf(src), map, ts);
            } else {
                loadTileset(ctx, tj, baseDir, map, ts);
            }
            map.tilesets.push_back(std::move(ts));
        }
    }

    bool infinite = root.value("infinite", false);
    if (root.contains("layers")) loadLayers(ctx, root["layers"], baseDir, map, infinite, Inherited{});

    if (infinite) {
        // Width and height come from the stored tiles.
        int32_t minX = 0, minY = 0, maxX = 0, maxY = 0;
        bool any = false;
        for (const Layer& l : map.layers) {
            if (l.empty) continue;
            if (!any) { minX = l.minX; minY = l.minY; maxX = l.maxX; maxY = l.maxY; any = true; }
            minX = std::min(minX, l.minX); minY = std::min(minY, l.minY);
            maxX = std::max(maxX, l.maxX); maxY = std::max(maxY, l.maxY);
        }
        map.width = maxX - minX;
        map.height = maxY - minY;
        map.pixelSize = {};   // bounds() comes from the stored chunks
    }
    return true;
}

} // namespace drift::tilemap
