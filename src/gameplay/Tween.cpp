#include <drift/gameplay/Tween.hpp>
#include <drift/World.hpp>
#include <drift/Log.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>

#include <algorithm>
#include <utility>

namespace drift {

namespace {

int channels(TweenProperty p) {
    switch (p) {
        case TweenProperty::Position:
        case TweenProperty::Scale:      return 2;
        case TweenProperty::Tint:       return 4;
        case TweenProperty::Rotation:
        case TweenProperty::Alpha:
        case TweenProperty::CameraZoom: return 1;
    }
    return 1;
}

template<typename T>
T* component(World& world, EntityId e) {
    const auto& reg = world.componentRegistry();
    if (!reg.has<T>() || !world.isAlive(e)) return nullptr;
    return world.getMut<T>(e, reg.get<T>());
}

// Read (write == false) or write the property's channels.
bool access(World& world, EntityId e, TweenProperty p, float* v, bool write) {
    auto u8 = [](float f) { return static_cast<uint8_t>(std::clamp(f + 0.5f, 0.f, 255.f)); };
    switch (p) {
        case TweenProperty::Position:
        case TweenProperty::Rotation:
        case TweenProperty::Scale: {
            auto* t = component<Transform2D>(world, e);
            if (!t) return false;
            if (p == TweenProperty::Position) {
                if (write) t->position = {v[0], v[1]}; else { v[0] = t->position.x; v[1] = t->position.y; }
            } else if (p == TweenProperty::Scale) {
                if (write) t->scale = {v[0], v[1]}; else { v[0] = t->scale.x; v[1] = t->scale.y; }
            } else {
                if (write) t->rotation = v[0]; else v[0] = t->rotation;
            }
            return true;
        }
        case TweenProperty::Tint:
        case TweenProperty::Alpha: {
            auto* s = component<Sprite>(world, e);
            if (!s) return false;
            if (p == TweenProperty::Alpha) {
                if (write) s->tint.a = u8(v[0] * 255.f); else v[0] = s->tint.a / 255.f;
            } else if (write) {
                s->tint = {u8(v[0]), u8(v[1]), u8(v[2]), u8(v[3])};
            } else {
                v[0] = s->tint.r; v[1] = s->tint.g; v[2] = s->tint.b; v[3] = s->tint.a;
            }
            return true;
        }
        case TweenProperty::CameraZoom: {
            auto* c = component<Camera>(world, e);
            if (!c) return false;
            if (write) c->zoom = v[0]; else v[0] = c->zoom;
            return true;
        }
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// TweenBuilder
// ---------------------------------------------------------------------------

TweenBuilder::TweenBuilder(Tweens* owner, EntityId target) : owner_(owner) {
    tween_.target = target;
}

TweenBuilder::~TweenBuilder() {
    if (owner_ && !started_ && !tween_.steps.empty()) {
        DRIFT_LOG_WARN("TweenBuilder destroyed without start(); the tween was discarded");
    }
}

TweenBuilder& TweenBuilder::property(TweenProperty p, const float* to, bool relative,
                                     float duration, Ease ease) {
    Step s;
    s.kind = Step::Kind::Property;
    s.property = p;
    for (int i = 0; i < channels(p); ++i) s.to[i] = to[i];
    s.relative = relative;
    s.duration = std::max(0.f, duration);
    s.ease = ease;
    tween_.steps.push_back(std::move(s));
    return *this;
}

TweenBuilder& TweenBuilder::moveTo(Vec2 to, float d, Ease e) {
    float v[2] = {to.x, to.y};
    return property(TweenProperty::Position, v, false, d, e);
}
TweenBuilder& TweenBuilder::moveBy(Vec2 delta, float d, Ease e) {
    float v[2] = {delta.x, delta.y};
    return property(TweenProperty::Position, v, true, d, e);
}
TweenBuilder& TweenBuilder::rotateTo(float r, float d, Ease e) { return property(TweenProperty::Rotation, &r, false, d, e); }
TweenBuilder& TweenBuilder::rotateBy(float r, float d, Ease e) { return property(TweenProperty::Rotation, &r, true, d, e); }
TweenBuilder& TweenBuilder::scaleTo(Vec2 to, float d, Ease e) {
    float v[2] = {to.x, to.y};
    return property(TweenProperty::Scale, v, false, d, e);
}
TweenBuilder& TweenBuilder::tintTo(Color c, float d, Ease e) {
    float v[4] = {float(c.r), float(c.g), float(c.b), float(c.a)};
    return property(TweenProperty::Tint, v, false, d, e);
}
TweenBuilder& TweenBuilder::fadeTo(float a, float d, Ease e) { return property(TweenProperty::Alpha, &a, false, d, e); }
TweenBuilder& TweenBuilder::zoomTo(float z, float d, Ease e) { return property(TweenProperty::CameraZoom, &z, false, d, e); }

TweenBuilder& TweenBuilder::value(float from, float to, float duration, Ease ease,
                                  std::function<void(float)> apply) {
    Step s;
    s.kind = Step::Kind::Value;
    s.from[0] = from;
    s.to[0] = to;
    s.captured = true;
    s.duration = std::max(0.f, duration);
    s.ease = ease;
    s.onValue = std::move(apply);
    tween_.steps.push_back(std::move(s));
    return *this;
}

TweenBuilder& TweenBuilder::delay(float seconds) {
    Step s;
    s.kind = Step::Kind::Delay;
    s.duration = std::max(0.f, seconds);
    tween_.steps.push_back(std::move(s));
    return *this;
}

TweenBuilder& TweenBuilder::call(std::function<void()> fn) {
    Step s;
    s.kind = Step::Kind::Call;
    s.onCall = std::move(fn);
    tween_.steps.push_back(std::move(s));
    return *this;
}

TweenBuilder& TweenBuilder::loop(int32_t times, TweenLoop mode) {
    tween_.loops = times == 0 ? 1 : times;
    tween_.mode = mode;
    return *this;
}

TweenId TweenBuilder::start() {
    if (started_ || !owner_) return InvalidTweenId;
    started_ = true;
    if (tween_.steps.empty()) return InvalidTweenId;
    return owner_->add(std::move(tween_));
}

// ---------------------------------------------------------------------------
// Tweens
// ---------------------------------------------------------------------------

TweenBuilder Tweens::on(EntityId target) { return TweenBuilder(this, target); }
TweenBuilder Tweens::sequence() { return TweenBuilder(this, InvalidEntityId); }

TweenId Tweens::add(Tween&& t) {
    t.id = nextId_++;
    if (nextId_ == InvalidTweenId) nextId_ = 1;
    TweenId id = t.id;
    pending_.push_back(std::move(t));
    return id;
}

Tweens::Tween* Tweens::find(TweenId id) {
    for (auto& t : active_) if (t.id == id && !t.dead) return &t;
    for (auto& t : pending_) if (t.id == id && !t.dead) return &t;
    return nullptr;
}

const Tweens::Tween* Tweens::find(TweenId id) const {
    return const_cast<Tweens*>(this)->find(id);
}

bool Tweens::isActive(TweenId id) const { return find(id) != nullptr; }

size_t Tweens::activeCount() const {
    size_t n = 0;
    for (auto& t : active_) n += t.dead ? 0 : 1;
    for (auto& t : pending_) n += t.dead ? 0 : 1;
    return n;
}

void Tweens::pause(TweenId id) { if (auto* t = find(id)) t->paused = true; }
void Tweens::resume(TweenId id) { if (auto* t = find(id)) t->paused = false; }

void Tweens::stopAll(EntityId target) {
    for (auto& t : active_) if (t.target == target) t.dead = true;
    for (auto& t : pending_) if (t.target == target) t.dead = true;
}

void Tweens::stop(TweenId id, bool complete) {
    Tween* t = find(id);
    if (!t) return;
    if (complete) completing_.push_back(id);
    t->dead = !complete;
    if (complete) t->paused = false;
}

bool Tweens::beginStep(Tween& t, Step& s, World& world) {
    if (s.kind == Step::Kind::Call) {
        if (s.onCall) s.onCall();
        return true;
    }
    if (s.kind != Step::Kind::Property || s.captured) return true;
    float cur[4] = {};
    if (!access(world, t.target, s.property, cur, false)) return false;
    int n = channels(s.property);
    for (int i = 0; i < n; ++i) {
        s.from[i] = cur[i];
        if (s.relative) s.to[i] += cur[i];
    }
    s.captured = true;
    return true;
}

bool Tweens::applyStep(Tween& t, Step& s, float progress, World& world) {
    if (s.kind == Step::Kind::Delay || s.kind == Step::Kind::Call) return true;
    float k = ease(s.ease, progress);
    if (s.kind == Step::Kind::Value) {
        if (s.onValue) s.onValue(s.from[0] + (s.to[0] - s.from[0]) * k);
        return true;
    }
    float v[4] = {};
    int n = channels(s.property);
    for (int i = 0; i < n; ++i) v[i] = s.from[i] + (s.to[i] - s.from[i]) * k;
    return access(world, t.target, s.property, v, true);
}

void Tweens::advance(Tween& t, float dt, World& world, Events<TweenFinished>* finished) {
    const int32_t last = static_cast<int32_t>(t.steps.size()) - 1;
    float remaining = dt;
    // A cycle that consumes no time (all steps zero length) must not spin forever.
    int32_t zeroTimeSteps = 0;

    for (;;) {
        Step& s = t.steps[t.stepIndex];
        if (!t.stepStarted) {
            if (!beginStep(t, s, world)) { t.dead = true; return; }
            t.stepStarted = true;
        }
        float take = std::min(remaining, s.duration - t.stepTime);
        if (take < 0.f) take = 0.f;
        t.stepTime += take;
        remaining -= take;
        float p = s.duration > 0.f ? std::min(1.f, t.stepTime / s.duration) : 1.f;
        if (!applyStep(t, s, t.forward ? p : 1.f - p, world)) { t.dead = true; return; }
        if (t.stepTime < s.duration) return;   // step still running; dt used up

        zeroTimeSteps = take > 0.f ? 0 : zeroTimeSteps + 1;
        t.stepTime = 0.f;
        t.stepStarted = false;
        t.stepIndex += t.forward ? 1 : -1;
        if (t.stepIndex >= 0 && t.stepIndex <= last) {
            if (zeroTimeSteps > 2 * (last + 1)) return;
            continue;
        }

        // End of a cycle.
        ++t.cyclesDone;
        if (t.loops > 0 && t.cyclesDone >= t.loops) {
            t.dead = true;
            if (finished) finished->send({t.id, t.target});
            return;
        }
        if (t.mode == TweenLoop::Yoyo) {
            t.forward = !t.forward;
            t.stepIndex = t.forward ? 0 : last;
        } else {
            t.stepIndex = 0;
        }
        if (zeroTimeSteps > 2 * (last + 1)) return;
    }
}

void Tweens::finishAll(Tween& t, World& world) {
    // Run every remaining step to its end value in the current direction.
    const int32_t last = static_cast<int32_t>(t.steps.size()) - 1;
    for (int32_t i = t.stepIndex; i >= 0 && i <= last; i += t.forward ? 1 : -1) {
        Step& s = t.steps[i];
        if (!(i == t.stepIndex && t.stepStarted) && !beginStep(t, s, world)) return;
        applyStep(t, s, t.forward ? 1.f : 0.f, world);
    }
}

void Tweens::update(float dt, World& world, Events<TweenFinished>* finished) {
    for (auto& t : pending_) active_.push_back(std::move(t));
    pending_.clear();

    std::vector<TweenId> completing;
    completing.swap(completing_);
    for (TweenId id : completing) {
        for (auto& t : active_) {
            if (t.id != id || t.dead) continue;
            finishAll(t, world);
            t.dead = true;
            if (finished) finished->send({t.id, t.target});
        }
    }

    float scaled = dt * timeScale;
    // Index loop: callbacks may start new tweens, which go to pending_.
    for (size_t i = 0; i < active_.size(); ++i) {
        Tween& t = active_[i];
        if (t.dead || t.paused) continue;
        advance(t, scaled, world, finished);
    }

    active_.erase(std::remove_if(active_.begin(), active_.end(),
                                 [](const Tween& t) { return t.dead; }),
                  active_.end());
}

} // namespace drift
