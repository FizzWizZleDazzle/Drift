#include <drift/Script.h>
#include <drift/App.h>
#include <drift/World.h>
#include <drift/Log.h>

namespace drift {

// ---------------------------------------------------------------------------
// Script convenience methods
// ---------------------------------------------------------------------------

Vec2 Script::position() const {
    if (!world_ || entity_ == InvalidEntity) return {};
    auto compId = world_->transform2dId();
    auto* t = world_->get<Transform2D>(entity_, compId);
    return t ? t->position : Vec2{};
}

void Script::setPosition(Vec2 pos) {
    if (!world_ || entity_ == InvalidEntity) return;
    auto compId = world_->transform2dId();
    auto* t = world_->getMut<Transform2D>(entity_, compId);
    if (t) t->position = pos;
}

} // namespace drift
