#include <drift/Commands.hpp>
#include <drift/EntityAllocator.hpp>
#include <drift/World.hpp>
#include <drift/components/Hierarchy.hpp>

#include <cstring>

namespace drift {

Commands::Commands(EntityAllocator& allocator, const ComponentRegistry& registry, World* world)
    : allocator_(allocator), registry_(registry), world_(world)
{
}

EntityCommands& EntityCommands::despawn() {
    cmd_.despawn(entity_);
    return *this;
}

EntityCommands Commands::spawn() {
    EntityId e = allocator_.allocate();
    CommandEntry entry;
    entry.type = CommandEntry::Spawn;
    entry.entity = e;
    entry.component = 0;
    queue_.push_back(std::move(entry));
    return EntityCommands(*this, e);
}

EntityCommands Commands::entity(EntityId e) {
    return EntityCommands(*this, e);
}

void Commands::insert(EntityId entity, ComponentId comp, const void* data, size_t size) {
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

void Commands::despawn(EntityId entity) {
    CommandEntry entry;
    entry.type = CommandEntry::Despawn;
    entry.entity = entity;
    entry.component = 0;
    queue_.push_back(std::move(entry));
}

void Commands::remove(EntityId entity, ComponentId comp) {
    CommandEntry entry;
    entry.type = CommandEntry::Remove;
    entry.entity = entity;
    entry.component = comp;
    queue_.push_back(std::move(entry));
}

void Commands::flush(World& world) {
    if (queue_.empty()) return;

    for (auto& cmd : queue_) {
        switch (cmd.type) {
            case CommandEntry::Spawn:
                // Entity already allocated via allocator_.allocate()
                break;

            case CommandEntry::Despawn:
                if (world.isAlive(cmd.entity)) {
                    // Recursively despawn children
                    ComponentId childrenId = world.componentRegistry().getByName("Children");
                    if (childrenId != 0) {
                        const auto* ch = static_cast<const Children*>(
                            world.getComponent(cmd.entity, childrenId));
                        if (ch) {
                            for (int ci = 0; ci < ch->count; ++ci) {
                                if (world.isAlive(ch->ids[ci])) {
                                    world.destroyEntity(ch->ids[ci]);
                                }
                            }
                        }
                    }
                    world.destroyEntity(cmd.entity);
                }
                break;

            case CommandEntry::Insert:
                if (world.isAlive(cmd.entity) && cmd.component != 0) {
                    world.setComponent(cmd.entity, cmd.component,
                                        cmd.data.data(), cmd.data.size());
                }
                break;

            case CommandEntry::Remove:
                if (world.isAlive(cmd.entity) && cmd.component != 0) {
                    world.removeComponent(cmd.entity, cmd.component);
                }
                break;

            case CommandEntry::Custom:
                if (cmd.customFn) cmd.customFn(world);
                break;
        }
    }

    queue_.clear();
}

} // namespace drift
