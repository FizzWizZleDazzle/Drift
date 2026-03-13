#define CUTE_ASEPRITE_IMPLEMENTATION
#include "cute_aseprite.h"

#include <drift/resources/SpriteResource.hpp>
#include <drift/resources/RendererResource.hpp>
#include <drift/Log.hpp>
#include "core/HandlePool.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace drift {

// ---- Internal types ----

struct AseTag {
    std::string name;
    int32_t fromFrame;
    int32_t toFrame;
    bool looping;
    std::vector<float> durations;
};

struct SpriteSheetData {
    TextureHandle texture;
    int32_t frameW;
    int32_t frameH;
    int32_t columns;
    int32_t rows;
    int32_t totalFrames;
    Vec2 pivot;
    std::vector<Rect> frameRects;
    std::vector<AseTag> tags;
};

struct AnimationData {
    SpritesheetHandle sheet;
    std::vector<int32_t> frames;
    std::vector<float> durations;
    int32_t frameCount;
    bool looping;
    int32_t currentFrame;
    float elapsed;
    bool finished;
};

struct SpriteResource::Impl {
    RendererResource& renderer;
    HandlePool<SpritesheetTag, SpriteSheetData> sheets;
    HandlePool<AnimationTag, AnimationData> anims;

    Impl(RendererResource& r) : renderer(r) {}
};

SpriteResource::SpriteResource(RendererResource& renderer)
    : impl_(new Impl(renderer)) {}

SpriteResource::~SpriteResource() { delete impl_; }

SpritesheetHandle SpriteResource::createSpritesheet(TextureHandle texture, int32_t frameW, int32_t frameH) {
    int32_t texW = 0, texH = 0;
    impl_->renderer.getTextureSize(texture, &texW, &texH);
    if (texW <= 0 || texH <= 0 || frameW <= 0 || frameH <= 0)
        return SpritesheetHandle{};

    SpriteSheetData data;
    data.texture = texture;
    data.frameW = frameW;
    data.frameH = frameH;
    data.columns = texW / frameW;
    data.rows = texH / frameH;
    data.totalFrames = data.columns * data.rows;
    data.pivot = {0, 0};

    data.frameRects.reserve(data.totalFrames);
    for (int32_t r = 0; r < data.rows; ++r) {
        for (int32_t c = 0; c < data.columns; ++c) {
            data.frameRects.push_back(Rect{
                static_cast<float>(c * frameW),
                static_cast<float>(r * frameH),
                static_cast<float>(frameW),
                static_cast<float>(frameH)
            });
        }
    }

    return impl_->sheets.create(std::move(data));
}

SpritesheetHandle SpriteResource::loadAseprite(const char* path) {
    if (!path) return SpritesheetHandle{};

    ase_t* ase = cute_aseprite_load_from_file(path, nullptr);
    if (!ase) {
        DRIFT_LOG_ERROR("Failed to load Aseprite file: %s", path);
        return SpritesheetHandle{};
    }

    int w = ase->w;
    int h = ase->h;
    int frameCount = ase->frame_count;

    // Create atlas: all frames side by side
    int atlasW = w * frameCount;
    int atlasH = h;
    std::vector<uint8_t> pixels(atlasW * atlasH * 4, 0);

    for (int i = 0; i < frameCount; ++i) {
        ase_frame_t* frame = ase->frames + i;
        for (int y = 0; y < h; ++y) {
            std::memcpy(
                pixels.data() + (y * atlasW + i * w) * 4,
                (uint8_t*)frame->pixels + y * w * 4,
                w * 4
            );
        }
    }

    TextureHandle tex = impl_->renderer.createTextureFromPixels(pixels.data(), atlasW, atlasH);
    if (!tex.valid()) {
        cute_aseprite_free(ase);
        return SpritesheetHandle{};
    }

    SpriteSheetData data;
    data.texture = tex;
    data.frameW = w;
    data.frameH = h;
    data.columns = frameCount;
    data.rows = 1;
    data.totalFrames = frameCount;
    data.pivot = {0, 0};

    for (int i = 0; i < frameCount; ++i) {
        data.frameRects.push_back(Rect{
            static_cast<float>(i * w), 0.f,
            static_cast<float>(w), static_cast<float>(h)
        });
    }

    // Load tags
    for (int i = 0; i < ase->tag_count; ++i) {
        ase_tag_t* tag = ase->tags + i;
        AseTag at;
        at.name = tag->name;
        at.fromFrame = tag->from_frame;
        at.toFrame = tag->to_frame;
        at.looping = true;
        for (int f = tag->from_frame; f <= tag->to_frame && f < frameCount; ++f) {
            at.durations.push_back(ase->frames[f].duration_milliseconds / 1000.f);
        }
        data.tags.push_back(std::move(at));
    }

    cute_aseprite_free(ase);
    return impl_->sheets.create(std::move(data));
}

void SpriteResource::destroySpritesheet(SpritesheetHandle sheet) {
    impl_->sheets.destroy(sheet);
}

Rect SpriteResource::getFrame(SpritesheetHandle sheet, int32_t col, int32_t row) const {
    auto* s = impl_->sheets.get(sheet);
    if (!s) return {};
    int idx = row * s->columns + col;
    if (idx < 0 || idx >= static_cast<int>(s->frameRects.size())) return {};
    return s->frameRects[idx];
}

int32_t SpriteResource::frameCount(SpritesheetHandle sheet) const {
    auto* s = impl_->sheets.get(sheet);
    return s ? s->totalFrames : 0;
}

void SpriteResource::setPivot(SpritesheetHandle sheet, float x, float y) {
    auto* s = impl_->sheets.get(sheet);
    if (s) s->pivot = {x, y};
}

Vec2 SpriteResource::getPivot(SpritesheetHandle sheet) const {
    auto* s = impl_->sheets.get(sheet);
    return s ? s->pivot : Vec2{};
}

TextureHandle SpriteResource::getTexture(SpritesheetHandle sheet) const {
    auto* s = impl_->sheets.get(sheet);
    return s ? s->texture : TextureHandle{};
}

AnimationHandle SpriteResource::createAnimation(SpritesheetHandle sheet, const AnimationDef& def) {
    if (!impl_->sheets.valid(sheet) || !def.frames || def.frameCount <= 0)
        return AnimationHandle{};

    AnimationData data;
    data.sheet = sheet;
    data.frames.assign(def.frames, def.frames + def.frameCount);
    if (def.durations) {
        data.durations.assign(def.durations, def.durations + def.frameCount);
    } else {
        data.durations.assign(def.frameCount, 0.1f);
    }
    data.frameCount = def.frameCount;
    data.looping = def.looping;
    data.currentFrame = 0;
    data.elapsed = 0.f;
    data.finished = false;

    return impl_->anims.create(std::move(data));
}

AnimationHandle SpriteResource::createAnimationFromTag(SpritesheetHandle sheet, const char* tagName) {
    if (!tagName) return AnimationHandle{};
    auto* s = impl_->sheets.get(sheet);
    if (!s) return AnimationHandle{};

    for (auto& tag : s->tags) {
        if (tag.name == tagName) {
            AnimationData data;
            data.sheet = sheet;
            for (int32_t f = tag.fromFrame; f <= tag.toFrame; ++f) {
                data.frames.push_back(f);
            }
            data.durations = tag.durations;
            if (data.durations.size() < data.frames.size()) {
                data.durations.resize(data.frames.size(), 0.1f);
            }
            data.frameCount = static_cast<int32_t>(data.frames.size());
            data.looping = tag.looping;
            data.currentFrame = 0;
            data.elapsed = 0.f;
            data.finished = false;
            return impl_->anims.create(std::move(data));
        }
    }
    return AnimationHandle{};
}

void SpriteResource::destroyAnimation(AnimationHandle anim) {
    impl_->anims.destroy(anim);
}

void SpriteResource::resetAnimation(AnimationHandle anim) {
    auto* a = impl_->anims.get(anim);
    if (!a) return;
    a->currentFrame = 0;
    a->elapsed = 0.f;
    a->finished = false;
}

Rect SpriteResource::updateAnimation(AnimationHandle anim, float dt) {
    auto* a = impl_->anims.get(anim);
    if (!a || a->finished || a->frameCount <= 0) return getAnimationFrame(anim);

    a->elapsed += dt;
    while (a->elapsed >= a->durations[a->currentFrame]) {
        a->elapsed -= a->durations[a->currentFrame];
        a->currentFrame++;
        if (a->currentFrame >= a->frameCount) {
            if (a->looping) {
                a->currentFrame = 0;
            } else {
                a->currentFrame = a->frameCount - 1;
                a->finished = true;
                break;
            }
        }
    }
    return getAnimationFrame(anim);
}

Rect SpriteResource::getAnimationFrame(AnimationHandle anim) const {
    auto* a = impl_->anims.get(anim);
    if (!a || a->frameCount <= 0) return {};
    auto* s = impl_->sheets.get(a->sheet);
    if (!s) return {};
    int idx = a->frames[a->currentFrame];
    if (idx < 0 || idx >= static_cast<int>(s->frameRects.size())) return {};
    return s->frameRects[idx];
}

bool SpriteResource::isAnimationFinished(AnimationHandle anim) const {
    auto* a = impl_->anims.get(anim);
    return a ? a->finished : true;
}

SpritesheetHandle SpriteResource::getAnimationSheet(AnimationHandle anim) const {
    auto* a = impl_->anims.get(anim);
    return a ? a->sheet : SpritesheetHandle{};
}

} // namespace drift
