#pragma once

#include <drift/Plugin.hpp>
#include <drift/Reflect.hpp>
#include <drift/App.hpp>
#include <drift/Query.hpp>
#include <drift/Commands.hpp>
#include <drift/WorldResource.h>
#include <drift/components/Hierarchy.hpp>
#include <drift/components/GlobalTransform.hpp>

namespace drift {

// Propagate transforms: depth-first from root entities (Without<Parent>) that
// have Children, composing down the tree. Every entity in a tree, root
// included, gets a GlobalTransform2D; nothing has to be added by hand.
inline void propagateTransforms(
    Query<Transform2D, Children, Without<Parent>> roots,
    ResMut<WorldResource> worldRes)
{
    auto& world = worldRes->world();
    const auto& registry = world.componentRegistry();
    ComponentId childrenId = registry.getByName("Children");
    ComponentId transform2dId = registry.getByName("Transform2D");
    ComponentId globalTransformId = registry.getByName("GlobalTransform2D");

    if (childrenId == 0 || globalTransformId == 0) return;

    struct StackEntry { EntityId entity; GlobalTransform2D parentGT; };
    std::vector<StackEntry> stack;
    std::vector<std::pair<EntityId, GlobalTransform2D>> rootGTs;

    // Collect first: setting components while the query iterates would
    // change the tables under it.
    roots.iterWithEntity([&](EntityId id, const Transform2D& t, const Children&) {
        rootGTs.push_back({id, GlobalTransform2D::from(t)});
    });

    // Writes a GlobalTransform2D only when it changed: a tree that did not
    // move costs reads, not writes and change ticks.
    auto store = [&](EntityId id, const GlobalTransform2D& gt) {
        const auto* old = static_cast<const GlobalTransform2D*>(world.getComponent(id, globalTransformId));
        if (old && old->position.x == gt.position.x && old->position.y == gt.position.y &&
            old->rotation == gt.rotation && old->scale.x == gt.scale.x && old->scale.y == gt.scale.y) {
            return;
        }
        world.setComponent(id, globalTransformId, &gt, sizeof(gt));
    };

    for (auto& [id, gt] : rootGTs) {
        store(id, gt);
        const auto* ch = static_cast<const Children*>(world.getComponent(id, childrenId));
        if (!ch) continue;
        for (int i = 0; i < ch->count; ++i) {
            if (world.isAlive(ch->ids[i])) stack.push_back({ch->ids[i], gt});
        }
    }

    while (!stack.empty()) {
        auto [entityId, parentGT] = stack.back();
        stack.pop_back();

        if (!world.isAlive(entityId)) continue;

        const auto* localT = static_cast<const Transform2D*>(
            world.getComponent(entityId, transform2dId));
        if (!localT) continue;

        GlobalTransform2D gt = GlobalTransform2D::compose(parentGT, *localT);
        store(entityId, gt);

        const auto* ch = static_cast<const Children*>(world.getComponent(entityId, childrenId));
        if (ch) {
            for (int i = 0; i < ch->count; ++i) {
                if (world.isAlive(ch->ids[i])) {
                    stack.push_back({ch->ids[i], gt});
                }
            }
        }
    }
}

class HierarchyPlugin : public Plugin {
public:
    DRIFT_PLUGIN(HierarchyPlugin)

    void build(App& app) override {
        reflect::component<GlobalTransform2D>(app.world(), "GlobalTransform2D")
            .field("position", &GlobalTransform2D::position)
            .field("rotation", &GlobalTransform2D::rotation)
            .field("scale", &GlobalTransform2D::scale);
        app.postUpdate<propagateTransforms>("propagate_transforms");
    }
};

} // namespace drift
