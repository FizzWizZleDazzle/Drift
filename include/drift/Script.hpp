#pragma once

#include <drift/Types.hpp>
#include <drift/World.hpp>
#include <drift/components/Sprite.hpp>

namespace drift {

class App;
class Commands;

// Unity-style Script base class.
// Attach to an entity as a component. ScriptSystem calls the virtuals.
class Script {
public:
    virtual ~Script() = default;

    virtual void onCreate() {}
    virtual void onUpdate(float dt) {}
    virtual void onDestroy() {}

    // Context accessors (set by ScriptSystem before calling virtuals)
    EntityId entity() const { return entity_; }
    World& world() const { return *world_; }
    App& app() const { return *app_; }

    // Convenience: read/write position via Transform2D
    Vec2 position() const;
    void setPosition(Vec2 pos);

    // Built-in component accessors
    const Transform2D* getTransform() const;
    Transform2D* getTransformMut();
    const Sprite* getSprite() const;
    Sprite* getSpriteMut();

    // Commands access (for spawn/despawn from scripts)
    Commands& commands() const;

    template<typename T>
    const T* getComponent(ComponentId compId) const {
        if (!world_ || entity_ == InvalidEntityId) return nullptr;
        return world_->get<T>(entity_, compId);
    }

    template<typename T>
    T* getComponentMut(ComponentId compId) {
        if (!world_ || entity_ == InvalidEntityId) return nullptr;
        return world_->getMut<T>(entity_, compId);
    }

    // Internal: set by ScriptSystem, not for user code
    void _bind(EntityId e, World* w, App* a) {
        entity_ = e;
        world_ = w;
        app_ = a;
    }

private:
    EntityId entity_ = InvalidEntityId;
    World* world_ = nullptr;
    App* app_ = nullptr;
};

// ECS component that holds a Script pointer.
// The Script is owned externally (user manages lifetime).
struct ScriptPtr {
    Script* script = nullptr;
};

} // namespace drift
