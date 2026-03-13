#include <drift/World.hpp>
#include <drift/Log.hpp>
#include <drift/components/Sprite.h>
#include <drift/components/Camera.h>

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
};

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct World::Impl {
    ecs_world_t* ecs = nullptr;
    ComponentId transform2dId = 0;
    ComponentId spriteId = 0;
    ComponentId cameraId = 0;
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

    // Register built-in components
    impl_->transform2dId = registerComponent("Transform2D", sizeof(Transform2D), alignof(Transform2D));
    impl_->spriteId = registerComponent("Sprite", sizeof(Sprite), alignof(Sprite));
    impl_->cameraId = registerComponent("Camera", sizeof(Camera), alignof(Camera));

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

Entity World::createEntity() {
    if (!impl_->ecs) return InvalidEntity;
    return static_cast<Entity>(ecs_new(impl_->ecs));
}

void World::destroyEntity(Entity entity) {
    if (!impl_->ecs) return;
    ecs_delete(impl_->ecs, static_cast<ecs_entity_t>(entity));
}

bool World::isAlive(Entity entity) const {
    if (!impl_->ecs) return false;
    return ecs_is_alive(impl_->ecs, static_cast<ecs_entity_t>(entity));
}

void World::addComponent(Entity entity, ComponentId component) {
    if (!impl_->ecs) return;
    ecs_add_id(impl_->ecs, static_cast<ecs_entity_t>(entity), static_cast<ecs_id_t>(component));
}

void World::removeComponent(Entity entity, ComponentId component) {
    if (!impl_->ecs) return;
    ecs_remove_id(impl_->ecs, static_cast<ecs_entity_t>(entity), static_cast<ecs_id_t>(component));
}

void World::setComponent(Entity entity, ComponentId component, const void* data, size_t size) {
    if (!impl_->ecs || !data) return;
    ecs_set_id(impl_->ecs, static_cast<ecs_entity_t>(entity),
               static_cast<ecs_id_t>(component), size, data);
}

const void* World::getComponent(Entity entity, ComponentId component) const {
    if (!impl_->ecs) return nullptr;
    return ecs_get_id(impl_->ecs, static_cast<ecs_entity_t>(entity),
                      static_cast<ecs_id_t>(component));
}

void* World::getComponentMut(Entity entity, ComponentId component) {
    if (!impl_->ecs) return nullptr;
    return ecs_get_mut_id(impl_->ecs, static_cast<ecs_entity_t>(entity),
                          static_cast<ecs_id_t>(component));
}

bool World::hasComponent(Entity entity, ComponentId component) const {
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
    if (hasNext) qi->started = true;
    return hasNext;
}

void* World::queryField(QueryIter* iter, int32_t index, size_t size) {
    if (!iter || !iter->_internal) return nullptr;
    auto* qi = static_cast<QueryIterInternal*>(iter->_internal);
    return ecs_field_w_size(&qi->iter, size, index);
}

Entity* World::queryEntities(QueryIter* iter) {
    if (!iter || !iter->_internal) return nullptr;
    auto* qi = static_cast<QueryIterInternal*>(iter->_internal);
    return reinterpret_cast<Entity*>(const_cast<ecs_entity_t*>(qi->iter.entities));
}

void World::queryFini(QueryIter* iter) {
    if (!iter || !iter->_internal) return;
    auto* qi = static_cast<QueryIterInternal*>(iter->_internal);
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

Entity World::allocateEntity() {
    if (!impl_->ecs) return InvalidEntity;
    // ecs_new creates an empty entity in flecs (no components yet)
    return static_cast<Entity>(ecs_new(impl_->ecs));
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

void* World::flecsWorld() const {
    return impl_->ecs;
}

} // namespace drift
