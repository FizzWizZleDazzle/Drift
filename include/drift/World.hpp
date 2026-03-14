#pragma once

#include <drift/Types.hpp>
#include <drift/EntityAllocator.hpp>
#include <drift/ComponentRegistry.hpp>

#include <cstddef>
#include <cstdint>

namespace drift {

class App;

// Opaque query iterator
struct QueryIter {
    void* _internal = nullptr;
    int32_t count = 0;
};

// ECS World wrapper around flecs
class World : public EntityAllocator {
public:
    World();
    ~World() override;

    // Progress (tick all flecs systems)
    void progress(float dt);

    // Components
    ComponentId registerComponent(const char* name, size_t size, size_t alignment);
    ComponentId lookupComponent(const char* name) const;

    // Entities
    EntityId createEntity();
    void destroyEntity(EntityId entity);
    bool isAlive(EntityId entity) const;

    void addComponent(EntityId entity, ComponentId component);
    void removeComponent(EntityId entity, ComponentId component);
    void setComponent(EntityId entity, ComponentId component, const void* data, size_t size);
    const void* getComponent(EntityId entity, ComponentId component) const;
    void* getComponentMut(EntityId entity, ComponentId component);
    bool hasComponent(EntityId entity, ComponentId component) const;

    // Typed component helpers (C++ only)
#ifndef SWIG
    template<typename T>
    ComponentId registerComponent(const char* name) {
        ComponentId id = registerComponent(name, sizeof(T), alignof(T));
        registry_.add<T>(id);
        registry_.addByName(name, id);
        return id;
    }

    template<typename T>
    void set(EntityId entity, ComponentId component, const T& value) {
        setComponent(entity, component, &value, sizeof(T));
    }

    template<typename T>
    const T* get(EntityId entity, ComponentId component) const {
        return static_cast<const T*>(getComponent(entity, component));
    }

    template<typename T>
    T* getMut(EntityId entity, ComponentId component) {
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
    EntityId* queryEntities(QueryIter* iter);
    void queryFini(QueryIter* iter);

    // EntityAllocator interface
    EntityId allocate() override;

    // Built-in component IDs
    ComponentId transform2dId() const;
    ComponentId spriteId() const;
    ComponentId cameraId() const;

    // Component registry
    const ComponentRegistry& componentRegistry() const { return registry_; }
    ComponentRegistry& componentRegistry() { return registry_; }

    // Internal access
    void* flecsWorld() const;

private:
    struct Impl;
    Impl* impl_;
    ComponentRegistry registry_;
};

} // namespace drift
