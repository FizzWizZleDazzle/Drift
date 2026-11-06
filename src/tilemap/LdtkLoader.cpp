// LDtk (.ldtk) loader: every level (including external .ldtkl levels and
// multi-world projects) becomes one MapData. IntGrid, AutoLayer and Tiles
// layers become tile layers, Entities layers become object layers, and
// field instances become properties.

#include "tilemap/TilemapData.hpp"

#include <drift/Log.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>

using json = nlohmann::json;

namespace drift::tilemap {

namespace {

struct TilesetDef {
    int32_t uid = 0;
    int32_t grid = 16;
    int32_t cWid = 1;
    size_t  index = 0;     // into the shared tileset list
};

struct LayerDef {
    std::string type;
    Vec2 parallax;         // LDtk factor (0 = moves with the camera)
    std::unordered_map<int32_t, std::string> intGridNames;
};

struct Project {
    std::string baseDir;
    std::vector<Tileset> tilesets;
    std::unordered_map<int32_t, TilesetDef> tilesetDefs;
    std::unordered_map<int32_t, LayerDef> layerDefs;
    int32_t defaultGrid = 16;
};

std::string str(const json& j, const char* key) {
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

void parseValue(const json& v, const std::string& type, int32_t grid, TilemapProperty& out) {
    out.type = type;
    if (v.is_null()) return;
    if (type.rfind("Array<", 0) == 0 && v.is_array()) {
        std::string inner = type.substr(6, type.size() - 7);
        for (const json& e : v) {
            TilemapProperty el;
            parseValue(e, inner, grid, el);
            out.elements.push_back(std::move(el));
        }
        out.number = static_cast<double>(out.elements.size());
        out.text = v.dump();
        return;
    }
    if (v.is_string()) {
        out.text = v.get<std::string>();
        out.number = std::atof(out.text.c_str());
    } else if (v.is_boolean()) {
        out.number = v.get<bool>() ? 1.0 : 0.0;
        out.text = v.get<bool>() ? "true" : "false";
    } else if (v.is_number()) {
        out.number = v.get<double>();
        out.text = v.dump();
    } else if (type == "Point" && v.is_object()) {
        float cx = v.value("cx", 0.f), cy = v.value("cy", 0.f);
        out.point = {(cx + 0.5f) * grid, (cy + 0.5f) * grid};
        out.text = v.dump();
    } else if (type == "EntityRef" && v.is_object()) {
        out.text = str(v, "entityIid");
    } else {
        out.text = v.dump();
    }
}

void parseFields(const json& j, int32_t grid, TilemapProperties& out) {
    auto it = j.find("fieldInstances");
    if (it == j.end() || !it->is_array()) return;
    for (const json& f : *it) {
        TilemapProperty prop;
        prop.name = str(f, "__identifier");
        parseValue(f.contains("__value") ? f["__value"] : json(), str(f, "__type"), grid, prop);
        out.items.push_back(std::move(prop));
    }
}

void loadDefs(LoadContext& ctx, const json& root, Project& proj) {
    proj.defaultGrid = root.value("defaultGridSize", 16);
    const json& defs = root.contains("defs") ? root["defs"] : json::object();

    uint32_t nextGid = 1;
    if (defs.contains("tilesets")) {
        for (const json& t : defs["tilesets"]) {
            std::string rel = str(t, "relPath");
            if (rel.empty()) continue;   // LDtk's internal icon atlas
            Tileset ts;
            ts.name = str(t, "identifier");
            ts.tileW = ts.tileH = t.value("tileGridSize", 16);
            ts.margin = t.value("padding", 0);
            ts.spacing = t.value("spacing", 0);
            ts.columns = std::max(1, t.value("__cWid", 1));
            ts.tileCount = static_cast<uint32_t>(ts.columns * std::max(1, t.value("__cHei", 1)));
            ts.firstgid = nextGid;
            nextGid += ts.tileCount;
            ts.texture = ctx.loadTexture(proj.baseDir + rel);
            Vec2 sz = ctx.textureSize(ts.texture);
            ts.textureW = t.value("pxWid", static_cast<int32_t>(sz.x));
            ts.textureH = t.value("pxHei", static_cast<int32_t>(sz.y));
            if (t.contains("customData")) {
                for (const json& cd : t["customData"]) {
                    TileInfo& info = ts.tiles[cd.value("tileId", 0u)];
                    TilemapProperty p;
                    p.name = "customData";
                    p.type = "String";
                    p.text = str(cd, "data");
                    info.properties.items.push_back(std::move(p));
                }
            }
            if (t.contains("enumTags")) {
                for (const json& et : t["enumTags"]) {
                    std::string tag = str(et, "enumValueId");
                    for (const json& id : et.value("tileIds", json::array())) {
                        TileInfo& info = ts.tiles[id.get<uint32_t>()];
                        if (info.cls.empty()) info.cls = tag;
                        TilemapProperty p;
                        p.name = tag;
                        p.type = "Bool";
                        p.text = "true";
                        p.number = 1.0;
                        info.properties.items.push_back(std::move(p));
                    }
                }
            }
            TilesetDef def;
            def.uid = t.value("uid", 0);
            def.grid = ts.tileW;
            def.cWid = ts.columns;
            def.index = proj.tilesets.size();
            proj.tilesetDefs[def.uid] = def;
            proj.tilesets.push_back(std::move(ts));
        }
    }

    if (defs.contains("layers")) {
        for (const json& l : defs["layers"]) {
            LayerDef def;
            def.type = str(l, "type");
            def.parallax = {l.value("parallaxFactorX", 0.f), l.value("parallaxFactorY", 0.f)};
            for (const json& v : l.value("intGridValues", json::array())) {
                def.intGridNames[v.value("value", 0)] = str(v, "identifier");
            }
            proj.layerDefs[l.value("uid", 0)] = std::move(def);
        }
    }
}

uint32_t tileId(const Project& proj, int32_t tilesetUid, const json& tile) {
    auto it = proj.tilesetDefs.find(tilesetUid);
    if (it == proj.tilesetDefs.end()) return 0;
    const Tileset& ts = proj.tilesets[it->second.index];
    uint32_t t = tile.value("t", 0u);
    uint32_t id = ts.firstgid + t;
    int32_t f = tile.value("f", 0);
    if (f & 1) id |= TileFlipH;
    if (f & 2) id |= TileFlipV;
    return id;
}

void addTiles(const Project& proj, int32_t tilesetUid, const json& tiles, Layer& layer) {
    for (const json& tile : tiles) {
        uint32_t id = tileId(proj, tilesetUid, tile);
        if (!id) continue;
        const json& px = tile["px"];
        int32_t x = px[0].get<int32_t>(), y = px[1].get<int32_t>();
        float alpha = tile.value("a", 1.f);
        bool onGrid = (x % layer.cellW == 0) && (y % layer.cellH == 0);
        int32_t cx = floorDiv(x, layer.cellW), cy = floorDiv(y, layer.cellH);
        if (onGrid && alpha >= 1.f && layer.tile(cx, cy) == 0) {
            layer.setTile(cx, cy, id);
        } else {
            layer.addExtra({{static_cast<float>(x), static_cast<float>(y)}, id, alpha});
        }
    }
}

bool loadLevel(const Project& proj, const json& level, MapData& map) {
    map.name = str(level, "identifier");
    map.width = level.value("pxWid", 0) / std::max(1, proj.defaultGrid);
    map.height = level.value("pxHei", 0) / std::max(1, proj.defaultGrid);
    map.tileW = map.tileH = proj.defaultGrid;
    map.pixelSize = {level.value("pxWid", 0.f), level.value("pxHei", 0.f)};
    map.worldOffset = {level.value("worldX", 0.f), level.value("worldY", 0.f)};
    map.parallaxOrigin = {map.pixelSize.x * 0.5f, map.pixelSize.y * 0.5f};
    map.background = parseColor(str(level, "__bgColor"), map.background);
    map.tilesets = proj.tilesets;
    parseFields(level, proj.defaultGrid, map.properties);
    {
        TilemapProperty iid;
        iid.name = "iid";
        iid.type = "String";
        iid.text = str(level, "iid");
        map.properties.items.push_back(std::move(iid));
    }

    const json* layers = level.contains("layerInstances") && level["layerInstances"].is_array()
                             ? &level["layerInstances"] : nullptr;
    json external;
    if (!layers) {
        std::string rel = str(level, "externalRelPath");
        if (rel.empty()) return false;
        external = json::parse(readFile(proj.baseDir + rel), nullptr, false);
        if (external.is_discarded() || !external.contains("layerInstances")) {
            DRIFT_LOG_ERROR("tilemap: cannot load external level %s", (proj.baseDir + rel).c_str());
            return false;
        }
        layers = &external["layerInstances"];
    }

    // LDtk lists the top layer first; Drift draws index 0 first.
    for (auto it = layers->rbegin(); it != layers->rend(); ++it) {
        const json& li = *it;
        std::string type = str(li, "__type");
        Layer layer;
        layer.name = str(li, "__identifier");
        layer.visible = li.value("visible", true);
        layer.opacity = li.value("__opacity", 1.f);
        layer.offset = {li.value("__pxTotalOffsetX", 0.f), li.value("__pxTotalOffsetY", 0.f)};
        layer.cellW = layer.cellH = std::max(1, li.value("__gridSize", proj.defaultGrid));
        layer.collision = toLower(layer.name).find("collision") != std::string::npos;

        auto def = proj.layerDefs.find(li.value("layerDefUid", 0));
        if (def != proj.layerDefs.end()) {
            // LDtk factor f slows the layer by f; Drift's factor is the
            // fraction of camera motion the layer follows.
            layer.parallax = {1.f - def->second.parallax.x, 1.f - def->second.parallax.y};
            layer.intGridNames = def->second.intGridNames;
        }

        int32_t tilesetUid = li.contains("overrideTilesetUid") && li["overrideTilesetUid"].is_number()
                                 ? li["overrideTilesetUid"].get<int32_t>()
                                 : (li.contains("__tilesetDefUid") && li["__tilesetDefUid"].is_number()
                                        ? li["__tilesetDefUid"].get<int32_t>() : -1);

        if (type == "IntGrid") {
            layer.kind = TilemapLayerKind::IntGrid;
            int32_t cw = li.value("__cWid", 0);
            const json& csv = li.contains("intGridCsv") ? li["intGridCsv"] : json::array();
            for (size_t i = 0; i < csv.size(); ++i) {
                int32_t v = csv[i].get<int32_t>();
                if (v) layer.setInt(static_cast<int32_t>(i % cw), static_cast<int32_t>(i / cw), v);
            }
            if (li.contains("autoLayerTiles")) addTiles(proj, tilesetUid, li["autoLayerTiles"], layer);
        } else if (type == "AutoLayer") {
            layer.kind = TilemapLayerKind::Tiles;
            if (li.contains("autoLayerTiles")) addTiles(proj, tilesetUid, li["autoLayerTiles"], layer);
        } else if (type == "Tiles") {
            layer.kind = TilemapLayerKind::Tiles;
            if (li.contains("gridTiles")) addTiles(proj, tilesetUid, li["gridTiles"], layer);
        } else if (type == "Entities") {
            layer.kind = TilemapLayerKind::Objects;
            layer.collision = false;
            int32_t index = 0;
            for (const json& e : li.value("entityInstances", json::array())) {
                MapObject obj;
                obj.id = index++;
                obj.iid = str(e, "iid");
                obj.name = obj.type = str(e, "__identifier");
                obj.size = {e.value("width", 0.f), e.value("height", 0.f)};
                if (e.contains("__pivot")) obj.pivot = {e["__pivot"][0].get<float>(), e["__pivot"][1].get<float>()};
                Vec2 px{e["px"][0].get<float>(), e["px"][1].get<float>()};
                obj.position = {px.x - obj.size.x * obj.pivot.x, px.y - obj.size.y * obj.pivot.y};
                for (const json& tag : e.value("__tags", json::array())) obj.tags.push_back(tag.get<std::string>());
                if (e.contains("__tile") && e["__tile"].is_object()) {
                    const json& t = e["__tile"];
                    auto tsIt = proj.tilesetDefs.find(t.value("tilesetUid", -1));
                    if (tsIt != proj.tilesetDefs.end()) {
                        const TilesetDef& d = tsIt->second;
                        int32_t col = t.value("x", 0) / std::max(1, d.grid);
                        int32_t row = t.value("y", 0) / std::max(1, d.grid);
                        obj.tile = proj.tilesets[d.index].firstgid + static_cast<uint32_t>(row * d.cWid + col);
                    }
                }
                parseFields(e, layer.cellW, obj.properties);
                layer.objects.push_back(std::move(obj));
            }
        } else {
            continue;
        }
        map.layers.push_back(std::move(layer));
    }
    return true;
}

} // namespace

bool loadLdtk(LoadContext& ctx, const std::string& path, const char* levelName, bool all,
              std::vector<MapData>& out) {
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

    Project proj;
    proj.baseDir = directoryOf(path);
    loadDefs(ctx, root, proj);

    // Levels with the layout of the world they belong to. Linear layouts
    // store -1 positions; those levels are laid out in order.
    struct LevelRef {
        const json* level;
        std::string layout;
        float linear;
    };
    std::vector<LevelRef> levels;
    auto addWorld = [&](const json& list, const std::string& layout) {
        float linear = 0.f;
        for (const json& l : list) {
            levels.push_back({&l, layout, linear});
            linear += layout == "LinearVertical" ? l.value("pxHei", 0.f) : l.value("pxWid", 0.f);
        }
    };
    if (root.contains("levels") && root["levels"].is_array() && !root["levels"].empty()) {
        addWorld(root["levels"], str(root, "worldLayout"));
    } else if (root.contains("worlds") && root["worlds"].is_array()) {
        for (const json& w : root["worlds"]) {
            if (w.contains("levels") && w["levels"].is_array()) addWorld(w["levels"], str(w, "worldLayout"));
        }
    }

    bool found = false;
    for (const LevelRef& ref : levels) {
        const json& level = *ref.level;
        bool wanted = all || (!levelName && !found) || (levelName && str(level, "identifier") == levelName);
        if (!wanted) continue;
        MapData map;
        map.path = path;
        if (!loadLevel(proj, level, map)) continue;
        found = true;
        if (ref.layout == "LinearHorizontal") map.worldOffset = {ref.linear, 0.f};
        if (ref.layout == "LinearVertical") map.worldOffset = {0.f, ref.linear};
        out.push_back(std::move(map));
        if (!all) break;
    }
    if (!found) {
        DRIFT_LOG_ERROR("tilemap: %s has no level %s", path.c_str(), levelName ? levelName : "");
        return false;
    }
    return true;
}

} // namespace drift::tilemap
