#pragma once

#include <drift/Types.hpp>
#include <drift/Handle.hpp>
#include <drift/ComponentRegistry.hpp>
#include <drift/World.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/Name.hpp>
#include <drift/components/Hierarchy.hpp>
#include <drift/Bundle.hpp>

#include <vector>
#include <cstdint>
#include <cstring>
#include <functional>
#include <typeinfo>

namespace drift {

class EntityAllocator;
class Commands;

// Fluent builder for entity mutations via Commands
class EntityCommands {
public:
    EntityCommands(Commands& cmd, EntityId e) : cmd_(cmd), entity_(e) {}

    template<typename T>
    EntityCommands& insert(const T& value);

    // Variadic insert: insert multiple components in one call
    template<typename T1, typename T2, typename... Rest>
    EntityCommands& insert(const T1& v1, const T2& v2, const Rest&... rest);

    // Bundle insert: insert all components from a Bundle
    template<typename... Ts>
    EntityCommands& insert(const Bundle<Ts...>& bundle);

    template<typename T>
    EntityCommands& remove();

    // Spawn children: withChildren([](ChildSpawner& spawner) { spawner.spawn().insert(...); })
    template<typename Fn>
    EntityCommands& withChildren(Fn&& fn);

    EntityCommands& despawn();

    EntityId id() const { return entity_; }
    operator EntityId() const { return entity_; }

private:
    Commands& cmd_;
    EntityId entity_;
};

class Commands {
public:
    Commands(EntityAllocator& allocator, const ComponentRegistry& registry, World* world = nullptr);

    // New generic API
    EntityCommands spawn();
    EntityCommands entity(EntityId e);

    // Raw API
    void insert(EntityId entity, ComponentId comp, const void* data, size_t size);
    void despawn(EntityId entity);
    void remove(EntityId entity, ComponentId comp);

    // Typed insert. Auto-registers unknown component types if World* is available.
    template<typename T>
    void insert(EntityId entity, const T& value) {
        if (!registry_.has<T>() && world_) {
            world_->registerComponent<T>(typeid(T).name());
        }
        insert(entity, registry_.get<T>(), &value, sizeof(T));
    }

    // Queue an arbitrary deferred mutation to run during flush.
    void queue(std::function<void(World&)> fn) {
        CommandEntry entry;
        entry.type = CommandEntry::Custom;
        entry.entity = 0;
        entry.component = 0;
        entry.customFn = std::move(fn);
        queue_.push_back(std::move(entry));
    }

    // Apply all queued commands to the World (exclusive access)
    void flush(World& world);

    const ComponentRegistry& registry() const { return registry_; }

private:
    EntityAllocator& allocator_;
    const ComponentRegistry& registry_;
    World* world_ = nullptr;

    struct CommandEntry {
        enum Type { Spawn, Despawn, Insert, Remove, Custom } type;
        EntityId entity;
        ComponentId component;
        std::vector<uint8_t> data;
        std::function<void(World&)> customFn;
    };

    std::vector<CommandEntry> queue_;
};

template<typename T>
EntityCommands& EntityCommands::insert(const T& value) {
    cmd_.insert<T>(entity_, value);
    return *this;
}

// Variadic insert: insert multiple components in one call
template<typename T1, typename T2, typename... Rest>
EntityCommands& EntityCommands::insert(const T1& v1, const T2& v2, const Rest&... rest) {
    insert<T1>(v1);
    insert<T2>(v2);
    (insert<Rest>(rest), ...);
    return *this;
}

template<typename... Ts>
EntityCommands& EntityCommands::insert(const Bundle<Ts...>& bundle) {
    std::apply([this](const Ts&... args) {
        (insert<Ts>(args), ...);
    }, static_cast<const std::tuple<Ts...>&>(bundle));
    return *this;
}

template<typename T>
EntityCommands& EntityCommands::remove() {
    cmd_.remove(entity_, cmd_.registry().get<T>());
    return *this;
}

// ChildSpawner: used with EntityCommands::withChildren()
class ChildSpawner {
public:
    ChildSpawner(Commands& cmd, EntityId parent) : cmd_(cmd), parent_(parent) {}

    EntityCommands spawn() {
        auto ec = cmd_.spawn();
        ec.insert<Parent>(Parent{parent_});
        childIds_.push_back(ec.id());
        return ec;
    }

    const std::vector<EntityId>& childIds() const { return childIds_; }

private:
    Commands& cmd_;
    EntityId parent_;
    std::vector<EntityId> childIds_;
};

template<typename Fn>
EntityCommands& EntityCommands::withChildren(Fn&& fn) {
    ChildSpawner spawner(cmd_, entity_);
    fn(spawner);
    // Insert Children component on parent with all tracked child IDs
    Children children{};
    for (auto id : spawner.childIds()) {
        children.add(id);
    }
    insert<Children>(children);
    return *this;
}

} // namespace drift
