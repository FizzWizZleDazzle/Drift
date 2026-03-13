#pragma once

#include <drift/Types.hpp>
#include <drift/Handle.hpp>
#include <drift/components/Sprite.h>
#include <drift/components/Camera.h>

#include <vector>
#include <cstdint>
#include <cstring>

namespace drift {

class World;

class Commands {
public:
    explicit Commands(World& world);

    // Entity pre-allocation: returns a valid Entity ID immediately.
    Entity spawn();

    // Queue component insertion (raw)
    void insert(Entity entity, ComponentId comp, const void* data, size_t size);

#ifndef SWIG
    template<typename T>
    void insert(Entity entity, ComponentId comp, const T& value) {
        insert(entity, comp, &value, sizeof(T));
    }
#endif

    // Queue entity destruction
    void despawn(Entity entity);

    // Queue component removal
    void remove(Entity entity, ComponentId comp);

    // Built-in component setters (queue mutations without World access)
    void setTransform(Entity entity, const Transform2D& transform);
    void setSprite(Entity entity, const Sprite& sprite);
    void setCamera(Entity entity, const Camera& camera);

    // Convenience: spawn with Sprite + Transform2D in one call
    Entity spawnSprite(TextureHandle texture, Vec2 position, float zOrder = 0.f);

    // Convenience: spawn a camera entity
    Entity spawnCamera(Vec2 position, float zoom = 1.f, bool active = true);

    // Apply all queued commands to the World (exclusive access)
    void flush();

private:
    World& world_;

    struct CommandEntry {
        enum Type { Spawn, Despawn, Insert, Remove } type;
        Entity entity;
        ComponentId component;
        std::vector<uint8_t> data;
    };

    std::vector<CommandEntry> queue_;
};

} // namespace drift
