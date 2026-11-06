// TilemapPlugin: the Tilemap component, its spawn system (colliders and
// object spawners) and its render system.

#include <drift/plugins/TilemapPlugin.hpp>
#include <drift/App.hpp>
#include <drift/Commands.hpp>
#include <drift/Query.hpp>
#include <drift/resources/PhysicsResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/Time.hpp>

#include "tilemap/TilemapInternal.hpp"

#include <unordered_set>

namespace drift {

namespace {

void destroyBody(App& app, tilemap::EntityState& st) {
    if (!st.hasBody) return;
    if (auto* physics = app.getResource<PhysicsResource>()) physics->destroyBody(st.body);
    st.hasBody = false;
}

// One static body per map; each merged rectangle is a box shape whose user
// data is the map entity, so collision events report the map entity.
void buildColliders(App& app, TilemapResource& res, TilemapResource::Impl& impl, EntityId entity,
                    const Tilemap& tm, tilemap::EntityState& st) {
    auto* physics = app.getResource<PhysicsResource>();
    if (!physics) return;
    destroyBody(app, st);
    st.collidersBuilt = true;
    st.collidersDirty = false;

    const tilemap::MapData* m = impl.get(tm.map);
    if (!m) return;
    std::vector<Rect> rects = res.collisionRects(tm.map);
    if (rects.empty()) return;

    BodyDef def;
    def.type = BodyType::Static;
    def.position = m->origin;
    st.body = physics->createBody(def);
    st.hasBody = true;
    for (const Rect& r : rects) {
        Vec2 pts[4] = {{r.x * m->scale.x, r.y * m->scale.y},
                       {(r.x + r.w) * m->scale.x, r.y * m->scale.y},
                       {(r.x + r.w) * m->scale.x, (r.y + r.h) * m->scale.y},
                       {r.x * m->scale.x, (r.y + r.h) * m->scale.y}};
        PhysicsShape shape = physics->addPolygon(st.body, pts, 4, tm.collider);
        physics->setShapeUserData(shape, reinterpret_cast<void*>(static_cast<uintptr_t>(entity)));
    }
}

void tilemapSpawn(App& app) {
    auto* res = app.getResource<TilemapResource>();
    auto& world = app.world();
    auto& reg = world.componentRegistry();
    if (!res || !reg.has<Tilemap>()) return;
    TilemapResource::Impl& impl = *TilemapPluginAccess::impl(*res);

    struct Pending {
        EntityId entity;
        Tilemap tm;
    };
    std::vector<Pending> pending;
    std::unordered_set<EntityId> seen;

    Query<Transform2D, Tilemap> maps(world, reg);
    maps.iterWithEntity([&](EntityId e, const Transform2D& t, const Tilemap& tm) {
        seen.insert(e);
        impl.setPlacement(tm.map, t.position, t.scale);
        auto& st = impl.entities[e];
        if (st.map != tm.map) {
            destroyBody(app, st);
            st = {};
            st.map = tm.map;
        }
        bool needColliders = tm.colliders && (!st.collidersBuilt || st.collidersDirty);
        bool needSpawn = tm.spawnObjects && !st.spawned;
        if (!tm.colliders && st.hasBody) destroyBody(app, st);
        if (needColliders || needSpawn) pending.push_back({e, tm});
    });

    for (auto it = impl.entities.begin(); it != impl.entities.end();) {
        if (!seen.count(it->first)) {
            destroyBody(app, it->second);
            it = impl.entities.erase(it);
        } else {
            ++it;
        }
    }

    for (const Pending& p : pending) {
        auto& st = impl.entities[p.entity];
        if (p.tm.colliders && (!st.collidersBuilt || st.collidersDirty)) {
            buildColliders(app, *res, impl, p.entity, p.tm, st);
        }
        if (p.tm.spawnObjects && !st.spawned) {
            st.spawned = true;
            impl.runSpawners(app, app.commands(), p.entity, p.tm.map);
        }
    }
}

void tilemapRender(App& app) {
    auto* res = app.getResource<TilemapResource>();
    auto& world = app.world();
    auto& reg = world.componentRegistry();
    if (!res || !reg.has<Tilemap>()) return;
    if (auto* time = app.getResource<Time>()) res->setAnimationTime(time->elapsed);

    Query<Transform2D, Tilemap> maps(world, reg);
    maps.iter([&](const Transform2D& t, const Tilemap& tm) {
        if (!tm.visible) return;
        TilemapDrawParams p;
        p.origin = t.position;
        p.scale = t.scale;
        p.zOrder = tm.zOrder;
        p.zStep = tm.zStep;
        p.renderLayer = tm.renderLayer;
        p.camera = tm.camera;
        p.cull = tm.cull;
        res->drawTilemap(tm.map, p);
    });
}

} // namespace

void TilemapPlugin::build(App& app) {
    app.addResource<TilemapResource>(*app.getResource<RendererResource>());
    app.world().registerComponent<Tilemap>("Tilemap");
    // PreUpdate: commands from spawners are applied before Update.
    app.addSystem("tilemap_spawn", Phase::PreUpdate, tilemapSpawn);
    // Registered after SpritePlugin, so the ECS cameras are already synced
    // when parallax and culling read them.
    app.addSystem("tilemap_render", Phase::Render, tilemapRender);
}

} // namespace drift
