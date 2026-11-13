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
    std::vector<int32_t> frames;    // in play order: reversed and ping-pong tags unrolled
    bool looping;
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
    std::vector<float> durations;   // per frame, from an Aseprite file
    std::vector<AseTag> tags;
};

struct SpriteResource::Impl {
    RendererResource& renderer;
    HandlePool<SpritesheetTag, SpriteSheetData> sheets;

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
        data.durations.push_back(ase->frames[i].duration_milliseconds / 1000.f);
    }

    // Tags, with their frames in the order the tag plays them. A tag set to
    // repeat once plays once; any other repeat count loops.
    for (int i = 0; i < ase->tag_count; ++i) {
        ase_tag_t* tag = ase->tags + i;
        AseTag at;
        at.name = tag->name;
        at.looping = tag->repeat != 1;
        int from = std::max(0, tag->from_frame);
        int to = std::min(frameCount - 1, tag->to_frame);
        std::vector<int32_t> forward;
        for (int f = from; f <= to; ++f) forward.push_back(f);
        std::vector<int32_t> backward(forward.rbegin(), forward.rend());
        // 0 forward, 1 reverse, 2 ping-pong, 3 ping-pong starting at the end.
        int direction = static_cast<int>(tag->loop_animation_direction);
        const auto& first = direction == 1 || direction == 3 ? backward : forward;
        const auto& second = direction == 3 ? forward : backward;
        at.frames = first;
        if ((direction == 2 || direction == 3) && first.size() > 2) {
            at.frames.insert(at.frames.end(), second.begin() + 1, second.end() - 1);
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

Rect SpriteResource::frameAt(SpritesheetHandle sheet, int32_t index) const {
    auto* s = impl_->sheets.get(sheet);
    if (!s || index < 0 || index >= static_cast<int32_t>(s->frameRects.size())) return {};
    return s->frameRects[index];
}

AnimationClip SpriteResource::clipFromTag(SpritesheetHandle sheet, const char* tagName) const {
    AnimationClip clip;
    clip.sheet = sheet;
    auto* s = impl_->sheets.get(sheet);
    if (!s || !tagName) return clip;
    for (const auto& tag : s->tags) {
        if (tag.name != tagName) continue;
        clip.frames = tag.frames;
        for (int32_t f : tag.frames) {
            clip.seconds.push_back(f < static_cast<int32_t>(s->durations.size()) ? s->durations[f] : 0.1f);
        }
        clip.looping = tag.looping;
        return clip;
    }
    std::string known;
    for (const auto& tag : s->tags) known += (known.empty() ? "" : ", ") + tag.name;
    DRIFT_LOG_ERROR("clipFromTag: no tag \"%s\" (tags: %s)", tagName, known.empty() ? "none" : known.c_str());
    return clip;
}

std::vector<std::string> SpriteResource::tagNames(SpritesheetHandle sheet) const {
    std::vector<std::string> names;
    if (auto* s = impl_->sheets.get(sheet)) {
        for (const auto& tag : s->tags) names.push_back(tag.name);
    }
    return names;
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

} // namespace drift
