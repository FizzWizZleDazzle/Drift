#include <drift/Scene.hpp>
#include <drift/App.hpp>
#include <drift/AssetServer.hpp>
#include <drift/Commands.hpp>
#include <drift/Log.hpp>
#include <drift/Reflect.hpp>
#include <drift/World.hpp>
#include <drift/components/Hierarchy.hpp>
#include <drift/components/Name.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <new>
#include <unordered_map>
#include <unordered_set>

namespace drift {

using json = nlohmann::json;

namespace {

constexpr const char* kSceneKind = "scene";
constexpr int kFormatVersion = 1;

int64_t localIdOf(const json& ent, size_t index) {
    auto it = ent.find("id");
    return (it != ent.end() && it->is_number_integer()) ? it->get<int64_t>()
                                                        : static_cast<int64_t>(index);
}

std::string nameOf(const json& comps) {
    if (!comps.is_object()) return {};
    auto n = comps.find("Name");
    if (n == comps.end() || !n->is_object()) return {};
    auto v = n->find("value");
    return (v != n->end() && v->is_string()) ? v->get<std::string>() : std::string{};
}

// Component JSON of every entity with the spawn overrides merged in.
std::vector<json> mergedComponents(const json& doc, const SceneSpawnOptions& opts) {
    std::vector<json> out;
    const json& ents = doc["entities"];
    out.reserve(ents.size());
    for (size_t i = 0; i < ents.size(); ++i) {
        json comps = ents[i].value("components", json::object());
        if (opts.overrides.is_object()) {
            auto byId = opts.overrides.find(std::to_string(localIdOf(ents[i], i)));
            if (byId != opts.overrides.end()) comps.merge_patch(*byId);
            std::string name = nameOf(comps);
            if (!name.empty()) {
                auto byName = opts.overrides.find(name);
                if (byName != opts.overrides.end()) comps.merge_patch(*byName);
            }
        }
        out.push_back(std::move(comps));
    }
    return out;
}

bool readJsonFile(const char* path, json& out) {
    size_t size = 0;
    void* data = SDL_LoadFile(path, &size);
    if (!data) {
        DRIFT_LOG_ERROR("Scene: cannot read '%s': %s", path, SDL_GetError());
        return false;
    }
    json doc = json::parse(static_cast<const char*>(data), static_cast<const char*>(data) + size,
                           nullptr, false);
    SDL_free(data);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("entities") ||
        !doc["entities"].is_array()) {
        DRIFT_LOG_ERROR("Scene: '%s' is not a scene file (expected an object with \"entities\")", path);
        return false;
    }
    out = std::move(doc);
    return true;
}

} // namespace

EntityId SceneInstance::find(const char* name) const {
    if (!name) return InvalidEntityId;
    for (size_t i = 0; i < names.size() && i < entities.size(); ++i) {
        if (names[i] == name) return entities[i];
    }
    return InvalidEntityId;
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct SceneResource::Impl {
    App& app;

    struct SceneEntry {
        std::shared_ptr<const json> doc;
        std::string path;   // empty for scenes made in code
    };
    std::vector<SceneEntry> scenes;   // handle id = index + 1

    struct Live {
        uint32_t scene = 0;
        std::map<int64_t, EntityId> byLocal;
        SceneSpawnOptions options;
    };
    std::vector<Live> live;

    explicit Impl(App& a) : app(a) {}

    SceneEntry* entry(SceneHandle h) {
        return (h.id > 0 && h.id <= scenes.size()) ? &scenes[h.id - 1] : nullptr;
    }

    nlohmann::json serialize(const SceneSaveOptions& options, std::vector<EntityId>* outOrder) const;

    reflect::JsonContext context() const {
        reflect::JsonContext ctx;
        ctx.assets = app.getResource<AssetServer>();
        return ctx;
    }

    // Instance description from the document alone (no world access).
    SceneInstance describe(const json& doc, const std::vector<EntityId>& ids,
                           const SceneSpawnOptions& opts) const {
        SceneInstance inst;
        inst.entities = ids;
        const json& ents = doc["entities"];
        std::unordered_set<int64_t> locals;
        for (size_t i = 0; i < ents.size(); ++i) locals.insert(localIdOf(ents[i], i));
        std::vector<json> comps = mergedComponents(doc, opts);
        for (size_t i = 0; i < comps.size(); ++i) {
            inst.names.push_back(nameOf(comps[i]));
            bool hasParent = false;
            auto p = comps[i].find("Parent");
            if (p != comps[i].end() && p->is_object()) {
                auto e = p->find("entity");
                hasParent = e != p->end() && e->is_number_integer() && locals.count(e->get<int64_t>());
            }
            if (!hasParent) inst.roots.push_back(ids[i]);
        }
        return inst;
    }

    // Write the document's components onto `ids` (one per entity, in file
    // order). With `replace`, reflected components missing from the file are
    // removed, so reloads follow the file exactly.
    void apply(World& world, const json& doc, const std::vector<EntityId>& ids,
               const SceneSpawnOptions& opts, bool replace) {
        const json& ents = doc["entities"];
        std::vector<json> comps = mergedComponents(doc, opts);

        std::unordered_map<int64_t, EntityId> byLocal;
        for (size_t i = 0; i < ents.size(); ++i) byLocal[localIdOf(ents[i], i)] = ids[i];

        reflect::JsonContext ctx = context();
        ctx.loadEntity = [&byLocal](const json& j) -> EntityId {
            if (!j.is_number_integer()) return InvalidEntityId;
            auto it = byLocal.find(j.get<int64_t>());
            return it != byLocal.end() ? it->second : InvalidEntityId;
        };

        const auto& registry = world.componentRegistry();
        ComponentId parentId = registry.get<Parent>();
        ComponentId childrenId = registry.get<Children>();
        ComponentId transformId = registry.get<Transform2D>();

        std::unordered_set<EntityId> affectedParents;
        std::unordered_set<EntityId> members(ids.begin(), ids.end());
        if (replace) {
            for (EntityId e : ids) {
                if (const auto* p = world.get<Parent>(e, parentId)) affectedParents.insert(p->entity);
            }
        }

        for (size_t i = 0; i < ids.size(); ++i) {
            EntityId e = ids[i];
            if (!world.isAlive(e)) continue;
            const json& c = comps[i];

            if (replace) {
                for (const reflect::ComponentInfo* info : reflect::components()) {
                    ComponentId cid = info->id(world);
                    if (cid && world.hasComponent(e, cid) && !c.contains(info->name)) {
                        world.removeComponent(e, cid);
                    }
                }
            }

            for (auto it = c.begin(); it != c.end(); ++it) {
                if (it->is_null()) continue;
                const reflect::ComponentInfo* info = reflect::findComponent(it.key());
                ComponentId cid = info ? info->id(world) : 0;
                if (!cid) {
                    DRIFT_LOG_WARN("Scene: unknown component '%s' skipped", it.key().c_str());
                    continue;
                }
                reflect::Value value(*info->info);
                reflect::fromJson(*info->info, value.get(), *it, ctx);
                world.setComponent(e, cid, value.get(), info->info->size);
            }

            // Roots: attach to the spawn parent and apply the offset.
            const auto* parent = world.get<Parent>(e, parentId);
            if (parent && parent->entity == InvalidEntityId) {
                world.removeComponent(e, parentId);
                parent = nullptr;
            }
            if (!parent) {
                if (opts.parent != InvalidEntityId) {
                    Parent p{opts.parent};
                    world.set(e, parentId, p);
                }
                if (auto* t = world.getMut<Transform2D>(e, transformId)) {
                    t->position += opts.offset;
                }
            }
            if (const auto* p = world.get<Parent>(e, parentId)) affectedParents.insert(p->entity);
        }

        // Rebuild Children on every parent this instance touched.
        for (EntityId p : affectedParents) {
            if (p == InvalidEntityId || !world.isAlive(p)) continue;
            Children ch{};
            if (const auto* old = world.get<Children>(p, childrenId)) {
                for (int k = 0; k < old->count; ++k) {
                    EntityId c = old->ids[k];
                    if (members.count(c) || !world.isAlive(c)) continue;
                    const auto* cp = world.get<Parent>(c, parentId);
                    if (cp && cp->entity == p) ch.add(c);
                }
            }
            for (EntityId c : ids) {
                const auto* cp = world.isAlive(c) ? world.get<Parent>(c, parentId) : nullptr;
                if (!cp || cp->entity != p) continue;
                if (ch.count >= MaxChildren) {
                    DRIFT_LOG_WARN("Scene: entity has more than %d children; extra ones are not listed",
                                   MaxChildren);
                    break;
                }
                ch.add(c);
            }
            world.set(p, childrenId, ch);
        }
    }

    void remember(uint32_t scene, const json& doc, const std::vector<EntityId>& ids,
                  const SceneSpawnOptions& opts) {
        Live l;
        l.scene = scene;
        l.options = opts;
        const json& ents = doc["entities"];
        for (size_t i = 0; i < ents.size(); ++i) l.byLocal[localIdOf(ents[i], i)] = ids[i];
        live.push_back(std::move(l));
        if (live.size() % 64 == 0) prune();
    }

    void prune() {
        World& world = app.world();
        live.erase(std::remove_if(live.begin(), live.end(), [&](const Live& l) {
            for (const auto& [local, e] : l.byLocal) {
                if (world.isAlive(e)) return false;
            }
            return true;
        }), live.end());
    }
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

SceneResource::SceneResource(App& app) : impl_(std::make_unique<Impl>(app)) {}
SceneResource::~SceneResource() = default;

json SceneResource::serialize(const SceneSaveOptions& options) const {
    return impl_->serialize(options, nullptr);
}

json SceneResource::Impl::serialize(const SceneSaveOptions& options,
                                    std::vector<EntityId>* outOrder) const {
    World& world = app.world();
    const auto& registry = world.componentRegistry();
    ComponentId parentId = registry.get<Parent>();
    ComponentId childrenId = registry.get<Children>();

    // Which entities.
    std::unordered_set<EntityId> set;
    if (options.entities.empty()) {
        for (const reflect::ComponentInfo* info : reflect::components()) {
            ComponentId cid = info->id(world);
            if (!cid) continue;
            for (EntityId e : world.entitiesWith(cid)) set.insert(e);
        }
    } else {
        std::vector<EntityId> stack(options.entities.begin(), options.entities.end());
        while (!stack.empty()) {
            EntityId e = stack.back();
            stack.pop_back();
            if (!world.isAlive(e) || !set.insert(e).second) continue;
            if (!options.includeChildren) continue;
            if (const auto* ch = world.get<Children>(e, childrenId)) {
                for (int k = 0; k < ch->count; ++k) stack.push_back(ch->ids[k]);
            }
        }
    }
    if (options.filter) {
        for (auto it = set.begin(); it != set.end();) {
            it = options.filter(*it) ? std::next(it) : set.erase(it);
        }
    }

    // Order: each root followed by its descendants, so files read top-down.
    std::vector<EntityId> sorted(set.begin(), set.end());
    std::sort(sorted.begin(), sorted.end());
    auto parentIn = [&](EntityId e) {
        const auto* p = world.get<Parent>(e, parentId);
        return p && set.count(p->entity) ? p->entity : InvalidEntityId;
    };
    std::vector<EntityId> order;
    std::unordered_set<EntityId> placed;
    std::function<void(EntityId)> visit = [&](EntityId e) {
        if (!placed.insert(e).second) return;
        order.push_back(e);
        if (const auto* ch = world.get<Children>(e, childrenId)) {
            for (int k = 0; k < ch->count; ++k) {
                if (set.count(ch->ids[k]) && parentIn(ch->ids[k]) == e) visit(ch->ids[k]);
            }
        }
    };
    for (EntityId e : sorted) {
        if (parentIn(e) == InvalidEntityId) visit(e);
    }
    for (EntityId e : sorted) visit(e);   // anything left (broken links)

    std::unordered_map<EntityId, int64_t> local;
    for (size_t i = 0; i < order.size(); ++i) local[order[i]] = static_cast<int64_t>(i);

    reflect::JsonContext ctx = context();
    ctx.saveEntity = [&local](EntityId e) -> json {
        auto it = local.find(e);
        return it != local.end() ? json(it->second) : json(nullptr);
    };

    json ents = json::array();
    for (EntityId e : order) {
        json comps = json::object();
        for (const reflect::ComponentInfo* info : reflect::components()) {
            ComponentId cid = info->id(world);
            if (!cid || !world.hasComponent(e, cid)) continue;
            if (cid == parentId && parentIn(e) == InvalidEntityId) continue;
            const void* value = world.getComponent(e, cid);
            if (!value) continue;
            comps[info->name] = reflect::toJson(*info->info, value, ctx);
        }
        ents.push_back(json{{"id", local[e]}, {"components", std::move(comps)}});
    }
    if (outOrder) *outOrder = std::move(order);
    return json{{"drift_scene", kFormatVersion}, {"entities", std::move(ents)}};
}

bool SceneResource::save(const char* path, const SceneSaveOptions& options) {
    if (!path) return false;
    std::vector<EntityId> order;
    json doc = impl_->serialize(options, &order);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        DRIFT_LOG_ERROR("Scene: cannot write '%s'", path);
        return false;
    }
    out << doc.dump(2) << '\n';
    out.close();
    if (!out) {
        DRIFT_LOG_ERROR("Scene: writing '%s' failed", path);
        return false;
    }
    DRIFT_LOG_INFO("Scene: saved %zu entities to '%s'", order.size(), path);

    // The saved file becomes a loaded scene and the saved entities its
    // instance: reloading the file later updates them instead of spawning
    // copies. An entity belongs to the file it was last saved to.
    auto* assets = impl_->app.getResource<AssetServer>();
    uint32_t id = assets ? assets->find(kSceneKind, path) : 0;
    auto shared = std::make_shared<const json>(std::move(doc));
    if (auto* entry = impl_->entry(SceneHandle{id})) {
        entry->doc = shared;
    } else {
        impl_->scenes.push_back({shared, path});
        id = static_cast<uint32_t>(impl_->scenes.size());
        if (assets) assets->track(kSceneKind, id, path);
    }
    std::unordered_set<EntityId> saved(order.begin(), order.end());
    for (auto& l : impl_->live) {
        for (auto it = l.byLocal.begin(); it != l.byLocal.end();) {
            it = (l.scene == id || saved.count(it->second)) ? l.byLocal.erase(it) : std::next(it);
        }
    }
    impl_->remember(id, *shared, order, SceneSpawnOptions{});
    return true;
}

SceneHandle SceneResource::load(const char* path) {
    if (!path) return {};
    auto* assets = impl_->app.getResource<AssetServer>();
    if (assets) {
        if (uint32_t id = assets->find(kSceneKind, path)) return SceneHandle{id};
    }
    json doc;
    if (!readJsonFile(path, doc)) return {};
    impl_->scenes.push_back({std::make_shared<const json>(std::move(doc)), path});
    SceneHandle h{static_cast<uint32_t>(impl_->scenes.size())};
    if (assets) assets->track(kSceneKind, h.id, path);
    return h;
}

SceneHandle SceneResource::create(json document) {
    if (!document.is_object() || !document.contains("entities") || !document["entities"].is_array()) {
        DRIFT_LOG_ERROR("Scene: create() needs an object with an \"entities\" array");
        return {};
    }
    impl_->scenes.push_back({std::make_shared<const json>(std::move(document)), {}});
    return SceneHandle{static_cast<uint32_t>(impl_->scenes.size())};
}

const json* SceneResource::document(SceneHandle scene) const {
    auto* e = impl_->entry(scene);
    return e ? e->doc.get() : nullptr;
}

bool SceneResource::reload(SceneHandle scene) {
    auto* entry = impl_->entry(scene);
    if (!entry || entry->path.empty()) return false;
    json doc;
    if (!readJsonFile(entry->path.c_str(), doc)) return false;
    entry->doc = std::make_shared<const json>(std::move(doc));
    const json& d = *entry->doc;
    const json& ents = d["entities"];

    World& world = impl_->app.world();
    impl_->prune();
    for (auto& l : impl_->live) {
        if (l.scene != scene.id) continue;
        std::vector<EntityId> ids;
        std::map<int64_t, EntityId> next;
        for (size_t i = 0; i < ents.size(); ++i) {
            int64_t local = localIdOf(ents[i], i);
            auto it = l.byLocal.find(local);
            EntityId e = (it != l.byLocal.end() && world.isAlive(it->second))
                             ? it->second : world.createEntity();
            ids.push_back(e);
            next[local] = e;
        }
        // Entities the file no longer has.
        for (const auto& [local, e] : l.byLocal) {
            if (!next.count(local) && world.isAlive(e)) world.destroyEntity(e);
        }
        l.byLocal = std::move(next);
        impl_->apply(world, d, ids, l.options, true);
    }
    return true;
}

SceneInstance SceneResource::spawn(Commands& commands, SceneHandle scene,
                                   const SceneSpawnOptions& options) {
    auto* entry = impl_->entry(scene);
    if (!entry) return {};
    std::shared_ptr<const json> doc = entry->doc;
    std::vector<EntityId> ids;
    for (size_t i = 0; i < (*doc)["entities"].size(); ++i) ids.push_back(commands.spawn().id());

    Impl* impl = impl_.get();
    commands.queue([impl, doc, ids, options](World& world) {
        impl->apply(world, *doc, ids, options, false);
    });
    impl_->remember(scene.id, *doc, ids, options);
    return impl_->describe(*doc, ids, options);
}

SceneInstance SceneResource::spawnNow(SceneHandle scene, const SceneSpawnOptions& options) {
    auto* entry = impl_->entry(scene);
    if (!entry) return {};
    std::shared_ptr<const json> doc = entry->doc;
    World& world = impl_->app.world();
    std::vector<EntityId> ids;
    for (size_t i = 0; i < (*doc)["entities"].size(); ++i) ids.push_back(world.createEntity());
    impl_->apply(world, *doc, ids, options, false);
    impl_->remember(scene.id, *doc, ids, options);
    return impl_->describe(*doc, ids, options);
}

} // namespace drift
