#include <drift/Script.hpp>
#include <drift/App.hpp>
#include <drift/World.hpp>
#include <drift/Commands.hpp>
#include <drift/Log.hpp>

namespace drift {

// ---------------------------------------------------------------------------
// Script convenience methods
// ---------------------------------------------------------------------------

Vec2 Script::position() const {
    auto* t = getTransform();
    return t ? t->position : Vec2{};
}

void Script::setPosition(Vec2 pos) {
    auto* t = getTransformMut();
    if (t) t->position = pos;
}

const Transform2D* Script::getTransform() const {
    if (!world_ || entity_ == InvalidEntityId) return nullptr;
    return world_->get<Transform2D>(entity_, world_->transform2dId());
}

Transform2D* Script::getTransformMut() {
    if (!world_ || entity_ == InvalidEntityId) return nullptr;
    return world_->getMut<Transform2D>(entity_, world_->transform2dId());
}

const Sprite* Script::getSprite() const {
    if (!world_ || entity_ == InvalidEntityId) return nullptr;
    return world_->get<Sprite>(entity_, world_->spriteId());
}

Sprite* Script::getSpriteMut() {
    if (!world_ || entity_ == InvalidEntityId) return nullptr;
    return world_->getMut<Sprite>(entity_, world_->spriteId());
}

Commands& Script::commands() const {
    return app_->commands();
}

} // namespace drift
