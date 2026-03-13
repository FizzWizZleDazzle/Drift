#pragma once

#include <drift/Types.hpp>
#include <drift/World.hpp>

namespace drift {

class App;

// Unity-style Script base class.
// Attach to an entity as a component. ScriptSystem calls the virtuals.
// SWIG directors enable C# subclassing.
class Script {
public:
    virtual ~Script() = default;

    virtual void onCreate() {}
    virtual void onUpdate(float dt) {}
    virtual void onDestroy() {}

    // Context accessors (set by ScriptSystem before calling virtuals)
    Entity entity() const { return entity_; }
    World& world() const { return *world_; }
    App& app() const { return *app_; }

    // Convenience: read/write position via Transform2D
    Vec2 position() const;
    void setPosition(Vec2 pos);

    // Internal: set by ScriptSystem, not for user code
    void _bind(Entity e, World* w, App* a) {
        entity_ = e;
        world_ = w;
        app_ = a;
    }

private:
    Entity entity_ = InvalidEntity;
    World* world_ = nullptr;
    App* app_ = nullptr;
};

// ECS component that holds a Script pointer.
// The Script is owned externally (user manages lifetime).
struct ScriptPtr {
    Script* script = nullptr;
};

} // namespace drift
