#include <drift/World.hpp>
#include <drift/Log.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/Name.hpp>
#include <drift/components/Hierarchy.hpp>

#include "flecs.h"

#include <cstring>

namespace drift {

// ---------------------------------------------------------------------------
// Internal query iterator state
// ---------------------------------------------------------------------------

struct QueryIterInternal {
    ecs_world_t* ecsWorld = nullptr;
    ecs_query_t* query = nullptr;
    ecs_iter_t iter = {};
    bool started = false;
    bool exhausted = false;  // true when queryNext returned false (iter already cleaned up)
};

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct World::Impl {
    ecs_world_t* ecs = nullptr;
    ComponentId transform2dId = 0;
    ComponentId spriteId = 0;
    ComponentId cameraId = 0;
    ComponentId nameId = 0;
    ComponentId parentId = 0;
    ComponentId childrenId = 0;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

World::World()
    : impl_(new Impl())
{
    impl_->ecs = ecs_init();
    if (!impl_->ecs) {
        DRIFT_LOG_ERROR("World: ecs_init() failed");
        return;
    }

    // Register built-in components (also populate ComponentRegistry)
    impl_->transform2dId = registerComponent<Transform2D>("Transform2D");
    impl_->spriteId = registerComponent<Sprite>("Sprite");
    impl_->cameraId = registerComponent<Camera>("Camera");
    impl_->nameId = registerComponent<Name>("Name");
    impl_->parentId = registerComponent<Parent>("Parent");
    impl_->childrenId = registerComponent<Children>("Children");

    DRIFT_LOG_INFO("ECS world created (flecs)");
}

World::~World() {
    if (impl_->ecs) {
        ecs_fini(impl_->ecs);
    }
    delete impl_;
}

void World::progress(float dt) {
    if (!impl_->ecs) return;
    ecs_progress(impl_->ecs, static_cast<ecs_ftime_t>(dt));
}

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------

ComponentId World::registerComponent(const char* name, size_t size, size_t alignment) {
    if (!impl_->ecs || !name) return 0;

    ecs_entity_desc_t edesc = {};
    edesc.name = name;

    ecs_component_desc_t desc = {};
    desc.entity = ecs_entity_init(impl_->ecs, &edesc);
    desc.type.size = static_cast<ecs_size_t>(size);
    desc.type.alignment = static_cast<ecs_size_t>(alignment);

    ecs_entity_t id = ecs_component_init(impl_->ecs, &desc);
    if (!id) {
        DRIFT_LOG_ERROR("Failed to register component '%s'", name);
        return 0;
    }
    return static_cast<ComponentId>(id);
}

ComponentId World::lookupComponent(const char* name) const {
    if (!impl_->ecs || !name) return 0;
    return static_cast<ComponentId>(ecs_lookup(impl_->ecs, name));
}

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------

EntityId World::createEntity() {
    if (!impl_->ecs) return InvalidEntityId;
    return static_cast<EntityId>(ecs_new(impl_->ecs));
}

void World::destroyEntity(EntityId entity) {
    if (!impl_->ecs) return;
    // Clean up tick maps for this entity
    for (auto it = changeTicks_.begin(); it != changeTicks_.end(); ) {
        if (it->first.entity == entity) it = changeTicks_.erase(it);
        else ++it;
    }
    for (auto it = addTicks_.begin(); it != addTicks_.end(); ) {
        if (it->first.entity == entity) it = addTicks_.erase(it);
        else ++it;
    }
    ecs_delete(impl_->ecs, static_cast<ecs_entity_t>(entity));
}

bool World::isAlive(EntityId entity) const {
    if (!impl_->ecs) return false;
    return ecs_is_alive(impl_->ecs, static_cast<ecs_entity_t>(entity));
}

void World::addComponent(EntityId entity, ComponentId component) {
    if (!impl_->ecs) return;
    ecs_add_id(impl_->ecs, static_cast<ecs_entity_t>(entity), static_cast<ecs_id_t>(component));
}

void World::removeComponent(EntityId entity, ComponentId component) {
    if (!impl_->ecs) return;
    ecs_remove_id(impl_->ecs, static_cast<ecs_entity_t>(entity), static_cast<ecs_id_t>(component));
}

void World::setComponent(EntityId entity, ComponentId component, const void* data, size_t size) {
    if (!impl_->ecs || !data) return;
    bool hadBefore = hasComponent(entity, component);
    ecs_set_id(impl_->ecs, static_cast<ecs_entity_t>(entity),
               static_cast<ecs_id_t>(component), size, data);
    TickKey key{entity, component};
    changeTicks_[key] = currentTick_;
    if (!hadBefore) {
        addTicks_[key] = currentTick_;
    }
}

const void* World::getComponent(EntityId entity, ComponentId component) const {
    if (!impl_->ecs) return nullptr;
    return ecs_get_id(impl_->ecs, static_cast<ecs_entity_t>(entity),
                      static_cast<ecs_id_t>(component));
}

void* World::getComponentMut(EntityId entity, ComponentId component) {
    if (!impl_->ecs) return nullptr;
    return ecs_get_mut_id(impl_->ecs, static_cast<ecs_entity_t>(entity),
                          static_cast<ecs_id_t>(component));
}

bool World::hasComponent(EntityId entity, ComponentId component) const {
    if (!impl_->ecs) return false;
    return ecs_has_id(impl_->ecs, static_cast<ecs_entity_t>(entity),
                      static_cast<ecs_id_t>(component));
}

// ---------------------------------------------------------------------------
// Singletons
// ---------------------------------------------------------------------------

void World::setSingleton(ComponentId component, const void* data, size_t size) {
    if (!impl_->ecs || !data) return;
    ecs_set_id(impl_->ecs, static_cast<ecs_entity_t>(component),
               static_cast<ecs_id_t>(component), size, data);
}

const void* World::getSingleton(ComponentId component) const {
    if (!impl_->ecs) return nullptr;
    return ecs_get_id(impl_->ecs, static_cast<ecs_entity_t>(component),
                      static_cast<ecs_id_t>(component));
}

void* World::getSingletonMut(ComponentId component) {
    if (!impl_->ecs) return nullptr;
    return ecs_get_mut_id(impl_->ecs, static_cast<ecs_entity_t>(component),
                          static_cast<ecs_id_t>(component));
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

QueryIter World::queryIter(const char* queryExpr) {
    QueryIter result{};
    if (!impl_->ecs || !queryExpr) return result;

    std::lock_guard<std::recursive_mutex> lock(queryMutex_);

    ecs_query_desc_t desc = {};
    desc.expr = queryExpr;

    ecs_query_t* query = ecs_query_init(impl_->ecs, &desc);
    if (!query) {
        DRIFT_LOG_ERROR("Failed to create query for '%s'", queryExpr);
        return result;
    }

    auto* qi = new QueryIterInternal();
    qi->ecsWorld = impl_->ecs;
    qi->query = query;
    qi->iter = ecs_query_iter(impl_->ecs, query);
    qi->started = false;

    result._internal = qi;
    return result;
}

bool World::queryNext(QueryIter* iter) {
    if (!iter || !iter->_internal) return false;
    auto* qi = static_cast<QueryIterInternal*>(iter->_internal);

    bool hasNext = ecs_query_next(&qi->iter);
    iter->count = hasNext ? qi->iter.count : 0;
    if (hasNext) {
        qi->started = true;
    } else {
        qi->exhausted = true;  // flecs already cleaned up the iterator
    }
    return hasNext;
}

void* World::queryField(QueryIter* iter, int32_t index, size_t size) {
    if (!iter || !iter->_internal) return nullptr;
    auto* qi = static_cast<QueryIterInternal*>(iter->_internal);
    return ecs_field_w_size(&qi->iter, size, index);
}

void* World::queryFieldOptional(QueryIter* iter, int32_t index, size_t size) {
    if (!iter || !iter->_internal) return nullptr;
    auto* qi = static_cast<QueryIterInternal*>(iter->_internal);
    if (!ecs_field_is_set(&qi->iter, index)) return nullptr;
    return ecs_field_w_size(&qi->iter, size, index);
}

EntityId* World::queryEntities(QueryIter* iter) {
    if (!iter || !iter->_internal) return nullptr;
    auto* qi = static_cast<QueryIterInternal*>(iter->_internal);
    return reinterpret_cast<EntityId*>(const_cast<ecs_entity_t*>(qi->iter.entities));
}

void World::queryFini(QueryIter* iter) {
    if (!iter || !iter->_internal) return;
    auto* qi = static_cast<QueryIterInternal*>(iter->_internal);

    std::lock_guard<std::recursive_mutex> lock(queryMutex_);

    // If the iterator was started but not fully consumed (early break),
    // we must call ecs_iter_fini to clean up flecs' stack allocator.
    if (qi->started && !qi->exhausted) {
        ecs_iter_fini(&qi->iter);
    }
    if (qi->query) {
        ecs_query_fini(qi->query);
    }
    delete qi;
    iter->_internal = nullptr;
    iter->count = 0;
}

// ---------------------------------------------------------------------------
// Built-in IDs
// ---------------------------------------------------------------------------

EntityId World::allocate() {
    if (!impl_->ecs) return InvalidEntityId;
    // ecs_new creates an empty entity in flecs (no components yet)
    return static_cast<EntityId>(ecs_new(impl_->ecs));
}

ComponentId World::transform2dId() const {
    return impl_->transform2dId;
}

ComponentId World::spriteId() const {
    return impl_->spriteId;
}

ComponentId World::cameraId() const {
    return impl_->cameraId;
}

ComponentId World::nameId() const {
    return impl_->nameId;
}

void World::setCurrentTick(uint32_t tick) {
    currentTick_ = tick;
}

uint32_t World::currentTick() const {
    return currentTick_;
}

uint32_t World::getComponentChangeTick(EntityId entity, ComponentId component) const {
    auto it = changeTicks_.find(TickKey{entity, component});
    return it != changeTicks_.end() ? it->second : 0;
}

uint32_t World::getComponentAddTick(EntityId entity, ComponentId component) const {
    auto it = addTicks_.find(TickKey{entity, component});
    return it != addTicks_.end() ? it->second : 0;
}

void* World::flecsWorld() const {
    return impl_->ecs;
}

std::recursive_mutex& World::queryMutex() {
    return queryMutex_;
}

} // namespace drift
