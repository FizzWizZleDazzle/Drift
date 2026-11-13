#include "Test.hpp"

#include <drift/Hierarchy.hpp>
#include <drift/World.hpp>

using namespace drift;

// ---------------------------------------------------------------------------
// Hierarchy
// ---------------------------------------------------------------------------

namespace {

EntityId spawnAt(World& w, Vec2 p, float rot = 0.f, Vec2 scale = {1.f, 1.f}) {
    EntityId e = w.createEntity();
    w.set(e, w.componentRegistry().get<Transform2D>(), Transform2D{p, rot, scale});
    return e;
}

} // namespace

TEST(hierarchy_links_stay_consistent) {
    World w;
    EntityId a = spawnAt(w, {0, 0}), b = spawnAt(w, {10, 0}), c = spawnAt(w, {20, 0});
    CHECK(setParent(w, b, a));
    CHECK(setParent(w, c, b));
    CHECK(parentOf(w, c) == b);
    CHECK(parentOf(w, a) == InvalidEntityId);
    CHECK(childrenOf(w, a).size() == 1);
    CHECK(descendantsOf(w, a).size() == 2);
    // No loops: a cannot go under its own grandchild.
    CHECK(!setParent(w, a, c));
    CHECK(!setParent(w, a, a));
    // Moving c under a takes it off b.
    CHECK(setParent(w, c, a));
    CHECK(childrenOf(w, b).empty());
    CHECK(childrenOf(w, a).size() == 2);
    CHECK(removeParent(w, c));
    CHECK(parentOf(w, c) == InvalidEntityId);
    CHECK(childrenOf(w, a).size() == 1);
}

TEST(hierarchy_world_transforms) {
    World w;
    const float pi = 3.14159265f;
    EntityId root = spawnAt(w, {100, 50}, pi / 2.f, {2.f, 2.f});
    EntityId child = spawnAt(w, {10, 0}, 0.25f);
    setParent(w, child, root);
    GlobalTransform2D g = worldTransformOf(w, child);
    // (10, 0) scaled by 2 and turned a quarter turn clockwise on screen.
    CHECK_NEAR(g.position.x, 100.f, 1e-3);
    CHECK_NEAR(g.position.y, 70.f, 1e-3);
    CHECK_NEAR(g.rotation, pi / 2.f + 0.25f, 1e-5);
    CHECK_NEAR(g.scale.x, 2.f, 1e-6);
    Vec2 back = worldToLocal(w, root, g.position);
    CHECK_NEAR(back.x, 10.f, 1e-3);
    CHECK_NEAR(back.y, 0.f, 1e-3);
    Vec2 there = localToWorld(w, root, {0, 5});
    CHECK_NEAR(there.x, 90.f, 1e-3);
    CHECK_NEAR(there.y, 50.f, 1e-3);
}

TEST(hierarchy_mirrored_parent_mirrors_children) {
    World w;
    EntityId root = spawnAt(w, {0, 0}, 0.f, {-1.f, 1.f});
    EntityId child = spawnAt(w, {10, 5}, 0.5f);
    setParent(w, child, root);
    GlobalTransform2D g = worldTransformOf(w, child);
    CHECK_NEAR(g.position.x, -10.f, 1e-4);
    CHECK_NEAR(g.position.y, 5.f, 1e-4);
    CHECK_NEAR(g.rotation, -0.5f, 1e-6);
    CHECK_NEAR(g.scale.x, -1.f, 1e-6);
}

TEST(hierarchy_keep_world_reparents_in_place) {
    World w;
    EntityId a = spawnAt(w, {100, 100}, 1.0f, {2.f, 2.f});
    EntityId b = spawnAt(w, {-40, 30}, -0.3f, {0.5f, 1.5f});
    EntityId item = spawnAt(w, {7, 3}, 0.2f);
    setParent(w, item, a);
    GlobalTransform2D before = worldTransformOf(w, item);
    CHECK(setParent(w, item, b, true));
    GlobalTransform2D after = worldTransformOf(w, item);
    CHECK_NEAR(after.position.x, before.position.x, 1e-3);
    CHECK_NEAR(after.position.y, before.position.y, 1e-3);
    CHECK_NEAR(after.rotation, before.rotation, 1e-5);
    CHECK(removeParent(w, item, true));
    GlobalTransform2D root = worldTransformOf(w, item);
    CHECK_NEAR(root.position.x, before.position.x, 1e-3);
    CHECK_NEAR(root.position.y, before.position.y, 1e-3);
}

TEST(hierarchy_despawn_takes_the_subtree) {
    World w;
    EntityId a = spawnAt(w, {0, 0}), b = spawnAt(w, {0, 0}), c = spawnAt(w, {0, 0}), d = spawnAt(w, {0, 0});
    setParent(w, b, a);
    setParent(w, c, b);
    setParent(w, d, a);
    despawnRecursive(w, b);
    CHECK(!w.isAlive(b));
    CHECK(!w.isAlive(c));
    CHECK(w.isAlive(a));
    CHECK(w.isAlive(d));
    CHECK(childrenOf(w, a).size() == 1);
    despawnRecursive(w, a);
    CHECK(!w.isAlive(d));
}
