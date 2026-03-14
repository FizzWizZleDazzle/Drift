#pragma once

#include <drift/Types.hpp>
#include <drift/Handle.hpp>
#include <drift/ComponentRegistry.hpp>
#include <drift/components/Sprite.h>
#include <drift/components/Camera.h>

#include <vector>
#include <cstdint>
#include <cstring>

namespace drift {

class World;
class EntityAllocator;
class Commands;

// Fluent builder for entity mutations via Commands
class EntityCommands {
public:
    EntityCommands(Commands& cmd, EntityId e) : cmd_(cmd), entity_(e) {}

#ifndef SWIG
    template<typename T>
    EntityCommands& insert(const T& value);
#endif

    // SWIG-visible typed inserts for built-in components
    EntityCommands& insert(const Transform2D& t);
    EntityCommands& insert(const Sprite& s);
    EntityCommands& insert(const Camera& c);

    EntityId id() const { return entity_; }
    operator EntityId() const { return entity_; }

private:
    Commands& cmd_;
    EntityId entity_;
};

class Commands {
public:
    Commands(EntityAllocator& allocator, const ComponentRegistry& registry);

    // New generic API
    EntityCommands spawn();
    EntityCommands entity(EntityId e);

    // Raw API (also SWIG-visible)
    void insert(EntityId entity, ComponentId comp, const void* data, size_t size);
    void despawn(EntityId entity);
    void remove(EntityId entity, ComponentId comp);

#ifndef SWIG
    // Typed insert (C++ only)
    template<typename T>
    void insert(EntityId entity, const T& value) {
        insert(entity, registry_.get<T>(), &value, sizeof(T));
    }
#endif

    // Apply all queued commands to the World (exclusive access)
    void flush(World& world);

    const ComponentRegistry& registry() const { return registry_; }

private:
    EntityAllocator& allocator_;
    const ComponentRegistry& registry_;

    struct CommandEntry {
        enum Type { Spawn, Despawn, Insert, Remove } type;
        EntityId entity;
        ComponentId component;
        std::vector<uint8_t> data;
    };

    std::vector<CommandEntry> queue_;
};

#ifndef SWIG
template<typename T>
EntityCommands& EntityCommands::insert(const T& value) {
    cmd_.insert<T>(entity_, value);
    return *this;
}
#endif

} // namespace drift
