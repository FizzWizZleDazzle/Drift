#pragma once

#include <drift/Plugin.hpp>
#include <drift/App.hpp>
#include <drift/Query.hpp>
#include <drift/components/Sprite.hpp>

namespace drift {

// Top-down depth sorting: sprites lower on screen (larger y) draw in front.
// Each frame sets Sprite::zOrder = baseZ + (position.y + pivotY) * scale.
// Put pivotY at the sprite's feet so tall sprites overlap correctly. Keep
// scale small enough that the result stays between the zOrder bands you
// use for other things (the default maps 1000 pixels to 1.0).
struct YSort {
    float baseZ = 0.f;
    float pivotY = 0.f;
    float scale = 0.001f;
};

inline void ySortSystem(QueryMut<Sprite, Transform2D, YSort> sorted) {
    sorted.iter([](Sprite& s, Transform2D& t, YSort& y) {
        s.zOrder = y.baseZ + (t.position.y + y.pivotY) * y.scale;
    });
}

class YSortPlugin : public Plugin {
public:
    void build(App& app) override {
        app.world().registerComponent<YSort>("YSort");
        app.addSystem<ySortSystem>("y_sort", Phase::PostUpdate);
    }
    DRIFT_PLUGIN(YSortPlugin)
};

} // namespace drift
