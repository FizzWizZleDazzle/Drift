#pragma once

#include <drift/Types.hpp>

#include <cstddef>
#include <cstdint>

namespace drift {

class App;

// Component ID = flecs entity_t
using ComponentId = uint64_t;

// Opaque query iterator
struct QueryIter {
    void* _internal = nullptr;
    int32_t count = 0;
};

// ECS World wrapper around flecs
class World {
public:
    World();
    ~World();

    // Progress (tick all flecs systems)
    void progress(float dt);

    // Components
    ComponentId registerComponent(const char* name, size_t size, size_t alignment);
    ComponentId lookupComponent(const char* name) const;

    // Entities
    Entity createEntity();
    void destroyEntity(Entity entity);
    bool isAlive(Entity entity) const;

    void addComponent(Entity entity, ComponentId component);
    void removeComponent(Entity entity, ComponentId component);
    void setComponent(Entity entity, ComponentId component, const void* data, size_t size);
    const void* getComponent(Entity entity, ComponentId component) const;
    void* getComponentMut(Entity entity, ComponentId component);
    bool hasComponent(Entity entity, ComponentId component) const;

    // Typed component helpers (C++ only)
#ifndef SWIG
    template<typename T>
    ComponentId registerComponent(const char* name) {
        return registerComponent(name, sizeof(T), alignof(T));
    }

    template<typename T>
    void set(Entity entity, ComponentId component, const T& value) {
        setComponent(entity, component, &value, sizeof(T));
    }

    template<typename T>
    const T* get(Entity entity, ComponentId component) const {
        return static_cast<const T*>(getComponent(entity, component));
    }

    template<typename T>
    T* getMut(Entity entity, ComponentId component) {
        return static_cast<T*>(getComponentMut(entity, component));
    }
#endif

    // Singletons
    void setSingleton(ComponentId component, const void* data, size_t size);
    const void* getSingleton(ComponentId component) const;
    void* getSingletonMut(ComponentId component);

    // Query
    QueryIter queryIter(const char* queryExpr);
    bool queryNext(QueryIter* iter);
    void* queryField(QueryIter* iter, int32_t index, size_t size);
    Entity* queryEntities(QueryIter* iter);
    void queryFini(QueryIter* iter);

    // Built-in component IDs
    ComponentId transform2dId() const;

    // Internal access
    void* flecsWorld() const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace drift
