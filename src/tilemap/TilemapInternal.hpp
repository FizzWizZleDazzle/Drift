#pragma once

// TilemapResource::Impl, shared by TilemapResource.cpp and the plugin's
// systems in TilemapPlugin.cpp.

#include <drift/resources/TilemapResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/PhysicsResource.hpp>

#include "core/HandlePool.hpp"
#include "tilemap/TilemapData.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace drift {

namespace tilemap {

// What the plugin remembers about each Tilemap entity.
struct EntityState {
    TilemapHandle map;
    bool          spawned = false;
    bool          collidersBuilt = false;
    bool          collidersDirty = false;
    bool          hasBody = false;
    PhysicsBody   body{};
};

} // namespace tilemap

struct TilemapResource::Impl {
    RendererResource& renderer;
    HandlePool<TilemapTag, std::shared_ptr<tilemap::MapData>> maps;
    std::unordered_map<std::string, TextureHandle> textures;
    tilemap::LoadContext ctx;
    double time = 0.0;
    std::vector<RendererResource::MeshVertex> scratch;
    std::unordered_map<EntityId, tilemap::EntityState> entities;

    struct Spawner {
        int32_t id;
        std::string type;
        ObjectSpawner fn;
    };
    std::vector<Spawner> spawners;
    int32_t nextSpawnerId = 1;

    explicit Impl(RendererResource& r) : renderer(r) {
        ctx.loadTexture = [this](const std::string& path) {
            auto it = textures.find(path);
            if (it != textures.end()) return it->second;
            TextureHandle t = renderer.loadTexture(path.c_str());
            textures[path] = t;
            return t;
        };
        ctx.textureSize = [this](TextureHandle t) { return renderer.textureSize(t); };
    }

    tilemap::MapData* get(TilemapHandle h) {
        auto* p = maps.get(h);
        return p ? p->get() : nullptr;
    }
    const tilemap::MapData* get(TilemapHandle h) const {
        auto* p = maps.get(h);
        return p ? p->get() : nullptr;
    }
    tilemap::Layer* layer(TilemapHandle h, int32_t i) {
        tilemap::MapData* m = get(h);
        if (!m || i < 0 || i >= static_cast<int32_t>(m->layers.size())) return nullptr;
        return &m->layers[i];
    }
    const tilemap::Layer* layer(TilemapHandle h, int32_t i) const {
        const tilemap::MapData* m = get(h);
        if (!m || i < 0 || i >= static_cast<int32_t>(m->layers.size())) return nullptr;
        return &m->layers[i];
    }

    TilemapHandle add(tilemap::MapData&& data) {
        for (tilemap::Layer& l : data.layers) {
            for (auto& kv : l.tiles) l.render[kv.first].dirty = true;
            for (auto& kv : l.extras) l.render[kv.first].dirty = true;
            if (l.kind == TilemapLayerKind::Image && l.image.valid() && (l.repeatX || l.repeatY)) {
                renderer.setTextureOptions(l.image, {TextureFilter::Nearest, TextureWrap::Repeat});
            }
        }
        return maps.create(std::make_shared<tilemap::MapData>(std::move(data)));
    }

    void markCollidersDirty(TilemapHandle map) {
        for (auto& kv : entities) {
            if (kv.second.map == map) kv.second.collidersDirty = true;
        }
    }

    // Where a Tilemap entity puts its map (position is the entity's).
    void setPlacement(TilemapHandle map, Vec2 position, Vec2 scale) {
        tilemap::MapData* m = get(map);
        if (!m) return;
        m->origin = {position.x + m->worldOffset.x * scale.x, position.y + m->worldOffset.y * scale.y};
        m->scale = scale;
    }

    void runSpawners(App& app, Commands& cmd, EntityId entity, TilemapHandle map) {
        const tilemap::MapData* m = get(map);
        if (!m || spawners.empty()) return;
        auto list = spawners;   // a spawner may add or remove spawners
        for (size_t li = 0; li < m->layers.size(); ++li) {
            const tilemap::Layer& l = m->layers[li];
            for (const MapObject& o : l.objects) {
                Vec2 pos = {m->origin.x + (o.position.x + l.offset.x) * m->scale.x,
                            m->origin.y + (o.position.y + l.offset.y) * m->scale.y};
                Vec2 size = {o.size.x * m->scale.x, o.size.y * m->scale.y};
                ObjectSpawnContext c{app, cmd, entity, map, static_cast<int32_t>(li), pos, size,
                                     {pos.x + size.x * o.pivot.x, pos.y + size.y * o.pivot.y}};
                for (const Spawner& s : list) {
                    if (s.type.empty() || s.type == o.type) s.fn(o, c);
                }
            }
        }
    }

    void drawImageLayer(const tilemap::Layer& l, Vec2 origin, Vec2 scale, Rect view, Color color, float z) {
        if (!l.image.valid() || l.imageSize.x <= 0.f || l.imageSize.y <= 0.f) return;
        float W = l.imageSize.x * scale.x, H = l.imageSize.y * scale.y;
        float x0 = origin.x, x1 = origin.x + W, y0 = origin.y, y1 = origin.y + H;
        if (l.repeatX) {
            x0 = origin.x + std::floor((view.x - origin.x) / W) * W;
            x1 = view.x + view.w;
        }
        if (l.repeatY) {
            y0 = origin.y + std::floor((view.y - origin.y) / H) * H;
            y1 = view.y + view.h;
        }
        float u0 = (x0 - origin.x) / W, u1 = (x1 - origin.x) / W;
        float v0 = (y0 - origin.y) / H, v1 = (y1 - origin.y) / H;
        RendererResource::MeshVertex v[4];
        v[0] = {{x0, y0}, {u0, v0}, color};
        v[1] = {{x1, y0}, {u1, v0}, color};
        v[2] = {{x1, y1}, {u1, v1}, color};
        v[3] = {{x0, y1}, {u0, v1}, color};
        const uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
        renderer.drawMesh(l.image, v, 4, idx, 6, z);
    }
};

// Lets the plugin's systems reach the resource's internals.
struct TilemapPluginAccess {
    static TilemapResource::Impl* impl(TilemapResource& r) { return r.impl_; }
};

} // namespace drift
