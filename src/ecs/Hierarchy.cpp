#include <drift/Hierarchy.hpp>
#include <drift/World.hpp>

#include <algorithm>

namespace drift {

namespace {

struct Ids {
    ComponentId parent, children, transform, global;
};

Ids ids(const World& world) {
    const auto& reg = world.componentRegistry();
    return {reg.get<Parent>(), reg.get<Children>(), reg.get<Transform2D>(), reg.getByName("GlobalTransform2D")};
}

EntityId parentId(const World& world, const Ids& id, EntityId e) {
    const auto* p = world.isAlive(e) ? world.get<Parent>(e, id.parent) : nullptr;
    return p && world.isAlive(p->entity) ? p->entity : InvalidEntityId;
}

// An entity that is neither a parent nor a child draws from its Transform2D;
// drop the GlobalTransform2D it kept from its time in a tree.
void dropStaleGlobal(World& world, const Ids& id, EntityId e) {
    if (!id.global || !world.isAlive(e)) return;
    if (world.hasComponent(e, id.parent) || world.hasComponent(e, id.children)) return;
    if (world.hasComponent(e, id.global)) world.removeComponent(e, id.global);
}

void unlinkFromParent(World& world, const Ids& id, EntityId child) {
    const auto* p = world.get<Parent>(child, id.parent);
    if (!p) return;
    EntityId old = p->entity;
    world.removeComponent(child, id.parent);
    if (world.isAlive(old)) {
        if (auto* ch = world.getMut<Children>(old, id.children)) {
            ch->remove(child);
            if (ch->count == 0) world.removeComponent(old, id.children);
        }
        dropStaleGlobal(world, id, old);
    }
}

} // namespace

EntityId parentOf(const World& world, EntityId e) {
    return parentId(world, ids(world), e);
}

std::vector<EntityId> childrenOf(const World& world, EntityId e) {
    std::vector<EntityId> out;
    Ids id = ids(world);
    if (!world.isAlive(e)) return out;
    if (const auto* ch = world.get<Children>(e, id.children)) {
        for (int i = 0; i < ch->count; ++i) {
            if (world.isAlive(ch->ids[i])) out.push_back(ch->ids[i]);
        }
    }
    return out;
}

std::vector<EntityId> descendantsOf(const World& world, EntityId e) {
    std::vector<EntityId> out;
    std::vector<EntityId> stack = childrenOf(world, e);
    std::reverse(stack.begin(), stack.end());
    while (!stack.empty()) {
        EntityId cur = stack.back();
        stack.pop_back();
        out.push_back(cur);
        auto kids = childrenOf(world, cur);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
    }
    return out;
}

bool setParent(World& world, EntityId child, EntityId parent, bool keepWorld) {
    Ids id = ids(world);
    if (!world.isAlive(child)) return false;
    if (parent != InvalidEntityId) {
        if (!world.isAlive(parent) || parent == child) return false;
        // The new parent must not be below the child.
        for (EntityId up = parentId(world, id, parent); up != InvalidEntityId; up = parentId(world, id, up)) {
            if (up == child) return false;
        }
        if (parentId(world, id, child) == parent) return true;
        const auto* ch = world.get<Children>(parent, id.children);
        if (ch && ch->count >= MaxChildren) return false;
    }

    GlobalTransform2D worldT = worldTransformOf(world, child);
    unlinkFromParent(world, id, child);

    if (parent == InvalidEntityId) {
        dropStaleGlobal(world, id, child);
    } else {
        world.set(child, id.parent, Parent{parent});
        Children ch{};
        if (const auto* cur = world.get<Children>(parent, id.children)) ch = *cur;
        ch.add(child);
        world.set(parent, id.children, ch);
    }
    if (keepWorld && world.hasComponent(child, id.transform)) {
        world.set(child, id.transform, localTransformFor(world, parent, worldT));
    }
    return true;
}

void despawnRecursive(World& world, EntityId e) {
    if (!world.isAlive(e)) return;
    Ids id = ids(world);
    std::vector<EntityId> doomed = descendantsOf(world, e);
    unlinkFromParent(world, id, e);
    for (EntityId d : doomed) {
        if (world.isAlive(d)) world.destroyEntity(d);
    }
    world.destroyEntity(e);
}

GlobalTransform2D worldTransformOf(const World& world, EntityId e) {
    Ids id = ids(world);
    std::vector<const Transform2D*> chain;
    for (EntityId cur = e; cur != InvalidEntityId && chain.size() < 1024; cur = parentId(world, id, cur)) {
        const auto* t = world.isAlive(cur) ? world.get<Transform2D>(cur, id.transform) : nullptr;
        static const Transform2D identity{};
        chain.push_back(t ? t : &identity);
    }
    if (chain.empty()) return {};
    GlobalTransform2D gt = GlobalTransform2D::from(*chain.back());
    for (size_t i = chain.size() - 1; i-- > 0;) gt = GlobalTransform2D::compose(gt, *chain[i]);
    return gt;
}

Vec2 localToWorld(const World& world, EntityId e, Vec2 local) {
    return worldTransformOf(world, e).toWorld(local);
}

Vec2 worldToLocal(const World& world, EntityId e, Vec2 worldPoint) {
    return worldTransformOf(world, e).toLocal(worldPoint);
}

Transform2D localTransformFor(const World& world, EntityId parent, const GlobalTransform2D& worldT) {
    if (parent == InvalidEntityId || !world.isAlive(parent)) return {worldT.position, worldT.rotation, worldT.scale};
    GlobalTransform2D p = worldTransformOf(world, parent);
    bool mirrored = (p.scale.x < 0.f) != (p.scale.y < 0.f);
    float rot = worldT.rotation - p.rotation;
    Transform2D t;
    t.position = p.toLocal(worldT.position);
    t.rotation = mirrored ? -rot : rot;
    t.scale = {p.scale.x != 0.f ? worldT.scale.x / p.scale.x : 0.f, p.scale.y != 0.f ? worldT.scale.y / p.scale.y : 0.f};
    return t;
}

} // namespace drift
