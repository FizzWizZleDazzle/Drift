#pragma once

#include <drift/Types.hpp>
#include <drift/World.hpp>

namespace drift {

// Fluent entity builder
class EntityBuilder {
public:
    explicit EntityBuilder(World& world)
        : world_(world), entity_(world.createEntity()) {}

    EntityId id() const { return entity_; }

#ifndef SWIG
    template<typename T>
    EntityBuilder& set(ComponentId component, const T& value) {
        world_.set<T>(entity_, component, value);
        return *this;
    }
#endif

    EntityBuilder& add(ComponentId component) {
        world_.addComponent(entity_, component);
        return *this;
    }

    EntityBuilder& setRaw(ComponentId component, const void* data, size_t size) {
        world_.setComponent(entity_, component, data, size);
        return *this;
    }

    EntityId build() { return entity_; }

private:
    World& world_;
    EntityId entity_;
};

} // namespace drift
