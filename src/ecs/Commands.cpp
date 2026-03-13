#include <drift/Commands.h>
#include <drift/World.hpp>

#include <cstring>

namespace drift {

Commands::Commands(World& world)
    : world_(world)
{
}

Entity Commands::spawn() {
    Entity e = world_.allocateEntity();
    CommandEntry entry;
    entry.type = CommandEntry::Spawn;
    entry.entity = e;
    entry.component = 0;
    queue_.push_back(std::move(entry));
    return e;
}

void Commands::insert(Entity entity, ComponentId comp, const void* data, size_t size) {
    CommandEntry entry;
    entry.type = CommandEntry::Insert;
    entry.entity = entity;
    entry.component = comp;
    if (data && size > 0) {
        entry.data.resize(size);
        std::memcpy(entry.data.data(), data, size);
    }
    queue_.push_back(std::move(entry));
}

void Commands::despawn(Entity entity) {
    CommandEntry entry;
    entry.type = CommandEntry::Despawn;
    entry.entity = entity;
    entry.component = 0;
    queue_.push_back(std::move(entry));
}

void Commands::remove(Entity entity, ComponentId comp) {
    CommandEntry entry;
    entry.type = CommandEntry::Remove;
    entry.entity = entity;
    entry.component = comp;
    queue_.push_back(std::move(entry));
}

void Commands::setTransform(Entity entity, const Transform2D& transform) {
    insert(entity, world_.transform2dId(), &transform, sizeof(Transform2D));
}

void Commands::setSprite(Entity entity, const Sprite& sprite) {
    insert(entity, world_.spriteId(), &sprite, sizeof(Sprite));
}

void Commands::setCamera(Entity entity, const Camera& camera) {
    insert(entity, world_.cameraId(), &camera, sizeof(Camera));
}

Entity Commands::spawnCamera(Vec2 position, float zoom, bool active) {
    Entity e = spawn();

    Transform2D transform;
    transform.position = position;
    insert(e, world_.transform2dId(), &transform, sizeof(Transform2D));

    Camera camera;
    camera.zoom = zoom;
    camera.active = active;
    insert(e, world_.cameraId(), &camera, sizeof(Camera));

    return e;
}

Entity Commands::spawnSprite(TextureHandle texture, Vec2 position, float zOrder) {
    Entity e = spawn();

    Transform2D transform;
    transform.position = position;
    insert(e, world_.transform2dId(), &transform, sizeof(Transform2D));

    Sprite sprite;
    sprite.texture = texture;
    sprite.zOrder = zOrder;
    insert(e, world_.spriteId(), &sprite, sizeof(Sprite));

    return e;
}

void Commands::flush() {
    if (queue_.empty()) return;

    for (auto& cmd : queue_) {
        switch (cmd.type) {
            case CommandEntry::Spawn:
                // Entity already allocated via allocateEntity()
                break;

            case CommandEntry::Despawn:
                if (world_.isAlive(cmd.entity)) {
                    world_.destroyEntity(cmd.entity);
                }
                break;

            case CommandEntry::Insert:
                if (world_.isAlive(cmd.entity) && cmd.component != 0) {
                    world_.setComponent(cmd.entity, cmd.component,
                                        cmd.data.data(), cmd.data.size());
                }
                break;

            case CommandEntry::Remove:
                if (world_.isAlive(cmd.entity) && cmd.component != 0) {
                    world_.removeComponent(cmd.entity, cmd.component);
                }
                break;
        }
    }

    queue_.clear();
}

} // namespace drift
