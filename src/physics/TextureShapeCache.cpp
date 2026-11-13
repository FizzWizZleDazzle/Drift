#include <drift/resources/TextureShapeCache.hpp>

#include <drift/App.hpp>
#include <drift/Image.hpp>
#include <drift/Log.hpp>
#include <drift/resources/RendererResource.hpp>

#include <algorithm>
#include <cmath>

namespace drift {

const std::vector<Outline>& TextureShapeCache::Shapes::pieces() const {
    if (!hasPieces_) {
        pieces_ = convexPieces(outlines);
        hasPieces_ = true;
    }
    return pieces_;
}

const TextureShapeCache::Shapes* TextureShapeCache::get(TextureHandle texture, Rect frame,
                                                        const AlphaShapeOptions& options) {
    if (!texture.valid()) return nullptr;
    Entry& e = entries_[texture.id];
    if (!e.read) {
        e.read = true;
        auto* renderer = app_->getResource<RendererResource>();
        Image img = renderer ? renderer->readTexture(texture) : Image{};
        if (img.empty()) DRIFT_LOG_WARN("TextureShapeCache: texture %u cannot be read; it has no shapes", texture.id);
        else keepAlpha(e, img);
    }
    if (e.alpha.empty()) return nullptr;

    int32_t x0 = 0, y0 = 0, x1 = e.width, y1 = e.height;
    if (frame.w > 0.f && frame.h > 0.f) {
        x0 = std::clamp(static_cast<int32_t>(std::lround(frame.x)), 0, e.width);
        y0 = std::clamp(static_cast<int32_t>(std::lround(frame.y)), 0, e.height);
        x1 = std::clamp(static_cast<int32_t>(std::lround(frame.x + frame.w)), x0, e.width);
        y1 = std::clamp(static_cast<int32_t>(std::lround(frame.y + frame.h)), y0, e.height);
    }
    Key key{x0, y0, x1, y1, options.threshold, options.tolerance, options.minArea};
    auto found = e.shapes.find(key);
    if (found != e.shapes.end()) return found->second.get();

    auto shapes = std::make_unique<Shapes>();
    shapes->size = {static_cast<float>(x1 - x0), static_cast<float>(y1 - y0)};
    if (x1 > x0 && y1 > y0) {
        AlphaView view{e.alpha.data() + static_cast<size_t>(y0) * e.width + x0, x1 - x0, y1 - y0, 1, e.width};
        shapes->outlines = alphaOutlines(view, options);
    }
    const Shapes* out = shapes.get();
    e.shapes.emplace(key, std::move(shapes));
    return out;
}

void TextureShapeCache::keepAlpha(Entry& e, const Image& img) {
    e.read = true;
    e.width = img.width();
    e.height = img.height();
    e.alpha.resize(static_cast<size_t>(e.width) * e.height);
    const uint8_t* px = img.data();
    for (size_t i = 0; i < e.alpha.size(); ++i) e.alpha[i] = px[i * 4 + 3];
}

void TextureShapeCache::rebuild(TextureHandle texture, const Image* pixels) {
    entries_.erase(texture.id);
    ++versions_[texture.id];
    if (pixels && !pixels->empty()) keepAlpha(entries_[texture.id], *pixels);
}

uint32_t TextureShapeCache::version(TextureHandle texture) const {
    auto it = versions_.find(texture.id);
    return it == versions_.end() ? 0u : it->second;
}

} // namespace drift
