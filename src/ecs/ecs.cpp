#include "drift/ecs.h"
#include "drift/drift.h"

#include "flecs.h"

#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------
// DriftWorld -- thin wrapper around a flecs world
// ---------------------------------------------------------------------------

struct DriftWorld {
    ecs_world_t    *ecs          = nullptr;
    DriftComponentId transform2d = 0;
};

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
namespace {

// Map DriftPhase enum to the built-in flecs phase entity.
static ecs_entity_t phase_to_entity(DriftPhase phase)
{
    switch (phase) {
    case DRIFT_PHASE_STARTUP:    return EcsOnLoad;
    case DRIFT_PHASE_PRE_UPDATE: return EcsPreUpdate;
    case DRIFT_PHASE_UPDATE:     return EcsOnUpdate;
    case DRIFT_PHASE_POST_UPDATE:return EcsPostUpdate;
    case DRIFT_PHASE_RENDER:     return EcsOnStore;
    }
    return EcsOnUpdate;
}

// Context block allocated on the heap so we can bridge from flecs'
// ecs_iter_action_t callback signature to our DriftSystemCallback.
struct SystemCtx {
    DriftWorld          *drift_world = nullptr;
    DriftSystemCallback  callback    = nullptr;
    void                *user_data   = nullptr;
};

// The actual flecs callback that dispatches into user code.
static void system_callback_trampoline(ecs_iter_t *it)
{
    SystemCtx *ctx = static_cast<SystemCtx *>(it->ctx);
    if (ctx && ctx->callback) {
        ctx->callback(ctx->drift_world, it->delta_time, ctx->user_data);
    }
}

// Cleanup function called by flecs when the system entity is destroyed.
static void system_ctx_free(void *ptr)
{
    SystemCtx *ctx = static_cast<SystemCtx *>(ptr);
    delete ctx;
}

// Internal state held behind the opaque DriftQueryIter._internal pointer.
struct QueryIterInternal {
    ecs_world_t *ecs_world = nullptr;
    ecs_query_t *query     = nullptr;
    ecs_iter_t   iter      = {};
    bool         started   = false;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// World lifecycle
// ---------------------------------------------------------------------------

DriftWorld *drift_ecs_world_create(void)
{
    DriftWorld *world = new DriftWorld();
    world->ecs = ecs_init();
    if (!world->ecs) {
        DRIFT_LOG_ERROR("drift_ecs_world_create: ecs_init() failed");
        delete world;
        return nullptr;
    }

    // Register the built-in Transform2D component.
    world->transform2d = drift_ecs_component_register(
        world, "DriftTransform2D",
        sizeof(DriftTransform2D), alignof(DriftTransform2D));

    DRIFT_LOG_INFO("ECS world created (flecs)");
    return world;
}

void drift_ecs_world_destroy(DriftWorld *world)
{
    if (!world) {
        return;
    }
    if (world->ecs) {
        ecs_fini(world->ecs);
        world->ecs = nullptr;
    }
    delete world;
    DRIFT_LOG_INFO("ECS world destroyed");
}

void drift_ecs_progress(DriftWorld *world, float dt)
{
    if (!world || !world->ecs) {
        return;
    }
    ecs_progress(world->ecs, static_cast<ecs_ftime_t>(dt));
}

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------

DriftComponentId drift_ecs_component_register(DriftWorld *world, const char *name,
                                               size_t size, size_t alignment)
{
    if (!world || !world->ecs || !name) {
        DRIFT_LOG_ERROR("drift_ecs_component_register: invalid arguments");
        return 0;
    }

    ecs_entity_desc_t edesc = {};
    edesc.name = name;

    ecs_component_desc_t desc = {};
    desc.entity = ecs_entity_init(world->ecs, &edesc);
    desc.type.size      = static_cast<ecs_size_t>(size);
    desc.type.alignment = static_cast<ecs_size_t>(alignment);

    ecs_entity_t id = ecs_component_init(world->ecs, &desc);
    if (!id) {
        DRIFT_LOG_ERROR("drift_ecs_component_register: failed to register '%s'", name);
        return 0;
    }

    DRIFT_LOG_DEBUG("Component registered: '%s' (id=%llu, size=%zu, align=%zu)",
                    name, (unsigned long long)id, size, alignment);
    return static_cast<DriftComponentId>(id);
}

DriftComponentId drift_ecs_component_lookup(DriftWorld *world, const char *name)
{
    if (!world || !world->ecs || !name) {
        return 0;
    }
    ecs_entity_t id = ecs_lookup(world->ecs, name);
    return static_cast<DriftComponentId>(id);
}

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------

DriftEntity drift_ecs_entity_create(DriftWorld *world)
{
    if (!world || !world->ecs) {
        return DRIFT_INVALID_ENTITY;
    }
    ecs_entity_t e = ecs_new(world->ecs);
    return static_cast<DriftEntity>(e);
}

void drift_ecs_entity_destroy(DriftWorld *world, DriftEntity entity)
{
    if (!world || !world->ecs) {
        return;
    }
    ecs_delete(world->ecs, static_cast<ecs_entity_t>(entity));
}

bool drift_ecs_entity_is_alive(DriftWorld *world, DriftEntity entity)
{
    if (!world || !world->ecs) {
        return false;
    }
    return ecs_is_alive(world->ecs, static_cast<ecs_entity_t>(entity));
}

void drift_ecs_entity_add(DriftWorld *world, DriftEntity entity, DriftComponentId component)
{
    if (!world || !world->ecs) {
        return;
    }
    ecs_add_id(world->ecs,
               static_cast<ecs_entity_t>(entity),
               static_cast<ecs_id_t>(component));
}

void drift_ecs_entity_remove(DriftWorld *world, DriftEntity entity, DriftComponentId component)
{
    if (!world || !world->ecs) {
        return;
    }
    ecs_remove_id(world->ecs,
                  static_cast<ecs_entity_t>(entity),
                  static_cast<ecs_id_t>(component));
}

void drift_ecs_entity_set(DriftWorld *world, DriftEntity entity,
                           DriftComponentId component, const void *data, size_t size)
{
    if (!world || !world->ecs || !data) {
        return;
    }
    ecs_set_id(world->ecs,
               static_cast<ecs_entity_t>(entity),
               static_cast<ecs_id_t>(component),
               size, data);
}

const void *drift_ecs_entity_get(DriftWorld *world, DriftEntity entity,
                                  DriftComponentId component)
{
    if (!world || !world->ecs) {
        return nullptr;
    }
    return ecs_get_id(world->ecs,
                      static_cast<ecs_entity_t>(entity),
                      static_cast<ecs_id_t>(component));
}

void *drift_ecs_entity_get_mut(DriftWorld *world, DriftEntity entity,
                                DriftComponentId component)
{
    if (!world || !world->ecs) {
        return nullptr;
    }
    return ecs_get_mut_id(world->ecs,
                          static_cast<ecs_entity_t>(entity),
                          static_cast<ecs_id_t>(component));
}

bool drift_ecs_entity_has(DriftWorld *world, DriftEntity entity, DriftComponentId component)
{
    if (!world || !world->ecs) {
        return false;
    }
    return ecs_has_id(world->ecs,
                      static_cast<ecs_entity_t>(entity),
                      static_cast<ecs_id_t>(component));
}

// ---------------------------------------------------------------------------
// Systems
// ---------------------------------------------------------------------------

DriftSystemId drift_ecs_system_register(DriftWorld *world, const char *name,
                                         DriftPhase phase, const char *query_expr,
                                         DriftSystemCallback callback, void *user_data)
{
    if (!world || !world->ecs || !name || !callback) {
        DRIFT_LOG_ERROR("drift_ecs_system_register: invalid arguments");
        return 0;
    }

    // Heap-allocate a context block that bridges the flecs callback to ours.
    SystemCtx *ctx = new SystemCtx();
    ctx->drift_world = world;
    ctx->callback    = callback;
    ctx->user_data   = user_data;

    ecs_entity_t phase_entity = phase_to_entity(phase);

    ecs_entity_desc_t edesc = {};
    edesc.name = name;
    ecs_id_t add_ids[] = { ecs_dependson(phase_entity), 0 };
    edesc.add = add_ids;

    ecs_system_desc_t desc = {};
    desc.entity = ecs_entity_init(world->ecs, &edesc);

    if (query_expr && query_expr[0] != '\0') {
        desc.query.expr = query_expr;
    }

    desc.callback  = system_callback_trampoline;
    desc.ctx       = ctx;
    desc.ctx_free  = system_ctx_free;

    ecs_entity_t sys = ecs_system_init(world->ecs, &desc);
    if (!sys) {
        DRIFT_LOG_ERROR("drift_ecs_system_register: failed to register '%s'", name);
        delete ctx;
        return 0;
    }

    DRIFT_LOG_DEBUG("System registered: '%s' (id=%llu, phase=%d)",
                    name, (unsigned long long)sys, static_cast<int>(phase));
    return static_cast<DriftSystemId>(sys);
}

// ---------------------------------------------------------------------------
// Singleton resources
// ---------------------------------------------------------------------------

void drift_ecs_set_singleton(DriftWorld *world, DriftComponentId component,
                              const void *data, size_t size)
{
    if (!world || !world->ecs || !data) {
        return;
    }
    // In flecs a singleton is stored on the component entity itself.
    ecs_set_id(world->ecs,
               static_cast<ecs_entity_t>(component),
               static_cast<ecs_id_t>(component),
               size, data);
}

const void *drift_ecs_get_singleton(DriftWorld *world, DriftComponentId component)
{
    if (!world || !world->ecs) {
        return nullptr;
    }
    return ecs_get_id(world->ecs,
                      static_cast<ecs_entity_t>(component),
                      static_cast<ecs_id_t>(component));
}

void *drift_ecs_get_singleton_mut(DriftWorld *world, DriftComponentId component)
{
    if (!world || !world->ecs) {
        return nullptr;
    }
    return ecs_get_mut_id(world->ecs,
                          static_cast<ecs_entity_t>(component),
                          static_cast<ecs_id_t>(component));
}

// ---------------------------------------------------------------------------
// Query iteration
// ---------------------------------------------------------------------------

DriftQueryIter drift_ecs_query_iter(DriftWorld *world, const char *query_expr)
{
    DriftQueryIter result = {};
    result._internal = nullptr;
    result.count     = 0;

    if (!world || !world->ecs || !query_expr) {
        DRIFT_LOG_ERROR("drift_ecs_query_iter: invalid arguments");
        return result;
    }

    ecs_query_desc_t desc = {};
    desc.expr = query_expr;

    ecs_query_t *query = ecs_query_init(world->ecs, &desc);
    if (!query) {
        DRIFT_LOG_ERROR("drift_ecs_query_iter: failed to create query for '%s'", query_expr);
        return result;
    }

    QueryIterInternal *qi = new QueryIterInternal();
    qi->ecs_world = world->ecs;
    qi->query     = query;
    qi->iter      = ecs_query_iter(world->ecs, query);
    qi->started   = false;

    result._internal = qi;
    return result;
}

bool drift_ecs_query_next(DriftQueryIter *iter)
{
    if (!iter || !iter->_internal) {
        return false;
    }

    QueryIterInternal *qi = static_cast<QueryIterInternal *>(iter->_internal);

    bool has_next = ecs_query_next(&qi->iter);
    if (has_next) {
        iter->count = qi->iter.count;
        qi->started = true;
    } else {
        iter->count = 0;
    }

    return has_next;
}

void *drift_ecs_query_field(DriftQueryIter *iter, int32_t index, size_t size)
{
    if (!iter || !iter->_internal) {
        return nullptr;
    }

    QueryIterInternal *qi = static_cast<QueryIterInternal *>(iter->_internal);
    return ecs_field_w_size(&qi->iter, size, index);
}

DriftEntity *drift_ecs_query_entities(DriftQueryIter *iter)
{
    if (!iter || !iter->_internal) {
        return nullptr;
    }

    QueryIterInternal *qi = static_cast<QueryIterInternal *>(iter->_internal);

    // ecs_entity_t and DriftEntity are both uint64_t, so this is safe.
    return reinterpret_cast<DriftEntity *>(const_cast<ecs_entity_t *>(qi->iter.entities));
}

void drift_ecs_query_fini(DriftQueryIter *iter)
{
    if (!iter || !iter->_internal) {
        return;
    }

    QueryIterInternal *qi = static_cast<QueryIterInternal *>(iter->_internal);
    if (qi->query) {
        ecs_query_fini(qi->query);
        qi->query = nullptr;
    }
    delete qi;

    iter->_internal = nullptr;
    iter->count     = 0;
}

// ---------------------------------------------------------------------------
// Built-in component IDs
// ---------------------------------------------------------------------------

DriftComponentId drift_ecs_id_transform2d(DriftWorld *world)
{
    if (!world) {
        return 0;
    }
    return world->transform2d;
}
