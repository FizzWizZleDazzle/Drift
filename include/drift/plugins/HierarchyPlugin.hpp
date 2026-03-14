#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/Query.hpp>
#include <drift/Commands.hpp>
#include <drift/WorldResource.h>
#include <drift/components/Hierarchy.hpp>
#include <drift/components/GlobalTransform.hpp>

namespace drift {

// Propagate transforms: BFS from root entities (Without<Parent>), composing down the tree
inline void propagateTransforms(
    QueryMut<Transform2D, GlobalTransform2D, Without<Parent>> roots,
    ResMut<WorldResource> worldRes)
{
    auto& world = worldRes->world();
    const auto& registry = world.componentRegistry();
    ComponentId childrenId = registry.getByName("Children");
    ComponentId transform2dId = registry.getByName("Transform2D");
    ComponentId globalTransformId = registry.getByName("GlobalTransform2D");

    if (childrenId == 0 || globalTransformId == 0) return;

    // Update root entities: GlobalTransform2D = Transform2D
    roots.iterWithEntity([&](EntityId id, Transform2D& t, GlobalTransform2D& gt) {
        gt = GlobalTransform2D::from(t);
    });

    // BFS propagation
    // Use a simple iterative approach: process entities that have Children
    struct StackEntry { EntityId entity; GlobalTransform2D parentGT; };
    std::vector<StackEntry> stack;

    roots.iterWithEntity([&](EntityId id, Transform2D&, GlobalTransform2D& gt) {
        const auto* ch = static_cast<const Children*>(world.getComponent(id, childrenId));
        if (ch) {
            for (int i = 0; i < ch->count; ++i) {
                if (world.isAlive(ch->ids[i])) {
                    stack.push_back({ch->ids[i], gt});
                }
            }
        }
    });

    while (!stack.empty()) {
        auto [entityId, parentGT] = stack.back();
        stack.pop_back();

        if (!world.isAlive(entityId)) continue;

        const auto* localT = static_cast<const Transform2D*>(
            world.getComponent(entityId, transform2dId));
        if (!localT) continue;

        GlobalTransform2D gt = GlobalTransform2D::compose(parentGT, *localT);
        world.setComponent(entityId, globalTransformId, &gt, sizeof(gt));

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
        app.world().registerComponent<GlobalTransform2D>("GlobalTransform2D");
        app.postUpdate<propagateTransforms>("propagate_transforms");
    }
};

} // namespace drift
