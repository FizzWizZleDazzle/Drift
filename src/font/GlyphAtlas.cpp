#include "font/GlyphAtlas.hpp"

#include <drift/resources/RendererResource.hpp>

#include <algorithm>
#include <cstring>

namespace drift {

namespace {
constexpr int Gap = 1;   // empty pixels between cells so filtering never bleeds
}

bool GlyphAtlas::addPage() {
    Page p;
    p.rgba.assign(static_cast<size_t>(PageSize) * PageSize * 4, 0);
    for (size_t i = 0; i < p.rgba.size(); i += 4) {
        p.rgba[i] = p.rgba[i + 1] = p.rgba[i + 2] = 255;
    }
    TextureOptions opts;
    opts.filter = filter_;
    p.texture = renderer_.createTextureFromPixels(p.rgba.data(), PageSize, PageSize, opts);
    if (!p.texture.valid()) return false;
    pages_.push_back(std::move(p));
    return true;
}

bool GlyphAtlas::allocateIn(Page& p, int w, int h, int& x, int& y) {
    int cw = w + Gap, ch = h + Gap;
    // Best fitting existing shelf: tall enough, not much taller.
    Shelf* best = nullptr;
    for (Shelf& s : p.shelves) {
        if (s.h < ch || s.h > ch + ch / 3 + 2 || s.x + cw > PageSize) continue;
        if (!best || s.h < best->h) best = &s;
    }
    if (!best) {
        if (p.nextY + ch > PageSize) return false;
        p.shelves.push_back({p.nextY, ch, 0});
        p.nextY += ch;
        best = &p.shelves.back();
    }
    x = best->x;
    y = best->y;
    best->x += cw;
    return true;
}

bool GlyphAtlas::allocate(int w, int h, Region& out) {
    if (w <= 0 || h <= 0 || w + Gap > PageSize || h + Gap > PageSize) return false;
    for (size_t i = 0; i < pages_.size(); ++i) {
        int x, y;
        if (allocateIn(pages_[i], w, h, x, y)) {
            out = {pages_[i].texture, static_cast<int>(i), x, y, w, h};
            return true;
        }
    }
    if (!addPage()) return false;
    int x, y;
    if (!allocateIn(pages_.back(), w, h, x, y)) return false;
    out = {pages_.back().texture, static_cast<int>(pages_.size() - 1), x, y, w, h};
    return true;
}

void GlyphAtlas::writeAlpha(const Region& r, const uint8_t* alpha, int stride) {
    if (r.page < 0 || r.page >= static_cast<int>(pages_.size())) return;
    Page& p = pages_[r.page];
    for (int row = 0; row < r.h; ++row) {
        uint8_t* dst = p.rgba.data() + (static_cast<size_t>(r.y + row) * PageSize + r.x) * 4;
        const uint8_t* src = alpha + static_cast<size_t>(row) * stride;
        for (int col = 0; col < r.w; ++col) dst[col * 4 + 3] = src[col];
    }
    p.dirtyY0 = std::min(p.dirtyY0, r.y);
    p.dirtyY1 = std::max(p.dirtyY1, r.y + r.h);
}

void GlyphAtlas::flush() {
    for (Page& p : pages_) {
        if (p.dirtyY1 <= p.dirtyY0) continue;
        renderer_.updateTexture(p.texture, 0, p.dirtyY0, PageSize, p.dirtyY1 - p.dirtyY0,
                                p.rgba.data() + static_cast<size_t>(p.dirtyY0) * PageSize * 4);
        p.dirtyY0 = PageSize;
        p.dirtyY1 = 0;
    }
}

void GlyphAtlas::reset() {
    if (pages_.empty()) return;
    for (size_t i = 1; i < pages_.size(); ++i) renderer_.destroyTexture(pages_[i].texture);
    pages_.resize(1);
    Page& p = pages_[0];
    for (size_t i = 3; i < p.rgba.size(); i += 4) p.rgba[i] = 0;
    p.shelves.clear();
    p.nextY = 0;
    p.dirtyY0 = 0;
    p.dirtyY1 = PageSize;
    flush();
}

} // namespace drift
