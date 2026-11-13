#pragma once

// Parent/child links between entities, kept consistent: a child's Parent and
// its parent's Children always agree. Use these instead of inserting Parent
// or Children by hand.

#include <drift/Types.hpp>
#include <drift/components/GlobalTransform.hpp>
#include <drift/components/Hierarchy.hpp>

#include <vector>

namespace drift {

class World;

// Makes `child` a child of `parent`; its Transform2D becomes relative to the
// parent. With `keepWorld`, the child's Transform2D is rewritten so it stays
// where it was on screen. `parent` = InvalidEntityId detaches the child and
// makes it a root. Returns false, changing nothing, when either entity is
// dead, the parent is the child or one of its descendants, or the parent
// already has MaxChildren children.
bool setParent(World& world, EntityId child, EntityId parent, bool keepWorld = false);

// Detaches `child` from its parent; same as setParent(world, child, InvalidEntityId).
inline bool removeParent(World& world, EntityId child, bool keepWorld = false) {
    return setParent(world, child, InvalidEntityId, keepWorld);
}

// The entity's parent, or InvalidEntityId for a root.
EntityId parentOf(const World& world, EntityId e);

// The entity's direct children, in the order they were added.
std::vector<EntityId> childrenOf(const World& world, EntityId e);

// Every entity below `e`, depth first, without `e` itself.
std::vector<EntityId> descendantsOf(const World& world, EntityId e);

// Destroys `e` and everything below it, and unlinks it from its parent.
void despawnRecursive(World& world, EntityId e);

// The entity's transform after every parent, composed from the current
// Transform2D values. GlobalTransform2D holds the same, as of the last
// transform propagation (PostUpdate); this one is never stale.
GlobalTransform2D worldTransformOf(const World& world, EntityId e);

// A point in the entity's local space (its Transform2D's frame: what a child
// at that position would be at) to world space, and back.
Vec2 localToWorld(const World& world, EntityId e, Vec2 local);
Vec2 worldToLocal(const World& world, EntityId e, Vec2 worldPoint);

// The Transform2D an entity under `parent` needs to sit at `worldT`.
// parent = InvalidEntityId returns worldT itself.
Transform2D localTransformFor(const World& world, EntityId parent, const GlobalTransform2D& worldT);

} // namespace drift
