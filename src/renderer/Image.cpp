#include <drift/Image.hpp>
#include <drift/Log.hpp>

#include <SDL3/SDL.h>
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace drift {

Image::Image(int32_t width, int32_t height, Color fill)
    : width_(std::max(0, width)), height_(std::max(0, height)) {
    pixels_.resize(static_cast<size_t>(width_) * height_ * 4);
    this->fill(fill);
}

Image Image::load(const char* path) {
    Image img;
    size_t size = 0;
    void* data = path ? SDL_LoadFile(path, &size) : nullptr;
    if (!data) {
        DRIFT_LOG_ERROR("Image: cannot read '%s': %s", path ? path : "", SDL_GetError());
        return img;
    }
    int w = 0, h = 0, n = 0;
    uint8_t* px = stbi_load_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(size), &w, &h, &n, 4);
    SDL_free(data);
    if (!px) {
        DRIFT_LOG_ERROR("Image: cannot decode '%s': %s", path, stbi_failure_reason());
        return img;
    }
    img.width_ = w;
    img.height_ = h;
    img.pixels_.assign(px, px + static_cast<size_t>(w) * h * 4);
    stbi_image_free(px);
    return img;
}

bool Image::savePng(const char* path) const {
    if (empty() || !path) return false;
    if (!stbi_write_png(path, width_, height_, 4, pixels_.data(), width_ * 4)) {
        DRIFT_LOG_ERROR("Image: could not write %s", path);
        return false;
    }
    return true;
}

Color Image::get(int32_t x, int32_t y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return {0, 0, 0, 0};
    const uint8_t* p = &pixels_[(static_cast<size_t>(y) * width_ + x) * 4];
    return {p[0], p[1], p[2], p[3]};
}

void Image::set(int32_t x, int32_t y, Color c) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    uint8_t* p = &pixels_[(static_cast<size_t>(y) * width_ + x) * 4];
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
    p[3] = c.a;
}

void Image::blend(int32_t x, int32_t y, Color c) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || c.a == 0) return;
    if (c.a == 255) {
        set(x, y, c);
        return;
    }
    uint8_t* p = &pixels_[(static_cast<size_t>(y) * width_ + x) * 4];
    float sa = c.a / 255.f, da = p[3] / 255.f;
    float oa = sa + da * (1.f - sa);
    auto mix = [&](uint8_t s, uint8_t d) {
        return static_cast<uint8_t>(std::lround((s * sa + d * da * (1.f - sa)) / oa));
    };
    p[0] = mix(c.r, p[0]);
    p[1] = mix(c.g, p[1]);
    p[2] = mix(c.b, p[2]);
    p[3] = static_cast<uint8_t>(std::lround(oa * 255.f));
}

void Image::fill(Color c) {
    for (size_t i = 0; i < pixels_.size(); i += 4) {
        pixels_[i] = c.r;
        pixels_[i + 1] = c.g;
        pixels_[i + 2] = c.b;
        pixels_[i + 3] = c.a;
    }
}

void Image::fillRect(Rect r, Color c) {
    int32_t x0 = std::max(0, static_cast<int32_t>(std::floor(r.x)));
    int32_t y0 = std::max(0, static_cast<int32_t>(std::floor(r.y)));
    int32_t x1 = std::min(width_, static_cast<int32_t>(std::floor(r.x + r.w)));
    int32_t y1 = std::min(height_, static_cast<int32_t>(std::floor(r.y + r.h)));
    for (int32_t y = y0; y < y1; ++y)
        for (int32_t x = x0; x < x1; ++x) set(x, y, c);
}

void Image::fillCircle(Vec2 center, float radius, Color c) {
    int32_t y0 = std::max(0, static_cast<int32_t>(std::floor(center.y - radius)));
    int32_t y1 = std::min(height_, static_cast<int32_t>(std::ceil(center.y + radius)));
    int32_t x0 = std::max(0, static_cast<int32_t>(std::floor(center.x - radius)));
    int32_t x1 = std::min(width_, static_cast<int32_t>(std::ceil(center.x + radius)));
    for (int32_t y = y0; y < y1; ++y)
        for (int32_t x = x0; x < x1; ++x) {
            float dx = x + 0.5f - center.x, dy = y + 0.5f - center.y;
            if (dx * dx + dy * dy <= radius * radius) set(x, y, c);
        }
}

void Image::fillGradient(Color a, Color b, bool vertical) {
    int32_t n = vertical ? height_ : width_;
    for (int32_t i = 0; i < n; ++i) {
        float t = n > 1 ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.f;
        auto lerp = [t](uint8_t u, uint8_t v) { return static_cast<uint8_t>(std::lround(u + (v - u) * t)); };
        Color c{lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a)};
        if (vertical) {
            for (int32_t x = 0; x < width_; ++x) set(x, i, c);
        } else {
            for (int32_t y = 0; y < height_; ++y) set(i, y, c);
        }
    }
}

void Image::drawLine(Vec2 a, Vec2 b, Color c, float thickness) {
    // Every pixel whose center lies within thickness/2 of the segment.
    float h = std::max(0.5f, thickness * 0.5f);
    int32_t x0 = static_cast<int32_t>(std::floor(std::min(a.x, b.x) - h));
    int32_t x1 = static_cast<int32_t>(std::ceil(std::max(a.x, b.x) + h));
    int32_t y0 = static_cast<int32_t>(std::floor(std::min(a.y, b.y) - h));
    int32_t y1 = static_cast<int32_t>(std::ceil(std::max(a.y, b.y) + h));
    float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    for (int32_t y = std::max(0, y0); y < std::min(height_, y1); ++y) {
        for (int32_t x = std::max(0, x0); x < std::min(width_, x1); ++x) {
            float px = x + 0.5f - a.x, py = y + 0.5f - a.y;
            float t = len2 > 0.f ? std::clamp((px * dx + py * dy) / len2, 0.f, 1.f) : 0.f;
            float ex = px - dx * t, ey = py - dy * t;
            if (ex * ex + ey * ey <= h * h) blend(x, y, c);
        }
    }
}

void Image::drawRect(Rect r, Color c, float thickness) {
    float t = std::max(1.f, thickness);
    auto band = [&](Rect b) {
        int32_t bx0 = std::max(0, static_cast<int32_t>(std::floor(b.x)));
        int32_t by0 = std::max(0, static_cast<int32_t>(std::floor(b.y)));
        int32_t bx1 = std::min(width_, static_cast<int32_t>(std::floor(b.x + b.w)));
        int32_t by1 = std::min(height_, static_cast<int32_t>(std::floor(b.y + b.h)));
        for (int32_t y = by0; y < by1; ++y)
            for (int32_t x = bx0; x < bx1; ++x) blend(x, y, c);
    };
    band({r.x, r.y, r.w, t});
    band({r.x, r.y + r.h - t, r.w, t});
    band({r.x, r.y + t, t, r.h - t * 2.f});
    band({r.x + r.w - t, r.y + t, t, r.h - t * 2.f});
}

void Image::drawCircle(Vec2 center, float radius, Color c) {
    int32_t y0 = std::max(0, static_cast<int32_t>(std::floor(center.y - radius)));
    int32_t y1 = std::min(height_, static_cast<int32_t>(std::ceil(center.y + radius)));
    int32_t x0 = std::max(0, static_cast<int32_t>(std::floor(center.x - radius)));
    int32_t x1 = std::min(width_, static_cast<int32_t>(std::ceil(center.x + radius)));
    for (int32_t y = y0; y < y1; ++y)
        for (int32_t x = x0; x < x1; ++x) {
            float dx = x + 0.5f - center.x, dy = y + 0.5f - center.y;
            if (dx * dx + dy * dy <= radius * radius) blend(x, y, c);
        }
}

void Image::drawCircleOutline(Vec2 center, float radius, Color c, float thickness) {
    float outer = radius + thickness * 0.5f, inner = std::max(0.f, radius - thickness * 0.5f);
    int32_t y0 = std::max(0, static_cast<int32_t>(std::floor(center.y - outer)));
    int32_t y1 = std::min(height_, static_cast<int32_t>(std::ceil(center.y + outer)));
    int32_t x0 = std::max(0, static_cast<int32_t>(std::floor(center.x - outer)));
    int32_t x1 = std::min(width_, static_cast<int32_t>(std::ceil(center.x + outer)));
    for (int32_t y = y0; y < y1; ++y)
        for (int32_t x = x0; x < x1; ++x) {
            float dx = x + 0.5f - center.x, dy = y + 0.5f - center.y, d2 = dx * dx + dy * dy;
            if (d2 <= outer * outer && d2 >= inner * inner) blend(x, y, c);
        }
}

void Image::blit(const Image& src, int32_t x, int32_t y, Rect srcRect, bool blendPixels) {
    int32_t sx0 = 0, sy0 = 0, sw = src.width_, sh = src.height_;
    if (srcRect.w > 0.f && srcRect.h > 0.f) {
        sx0 = static_cast<int32_t>(srcRect.x);
        sy0 = static_cast<int32_t>(srcRect.y);
        sw = static_cast<int32_t>(srcRect.w);
        sh = static_cast<int32_t>(srcRect.h);
    }
    // Rows of `src` may be this image's own rows; read them first.
    Image part = &src == this ? src.crop({float(sx0), float(sy0), float(sw), float(sh)}) : Image();
    const Image& from = &src == this ? part : src;
    int32_t ox = &src == this ? 0 : sx0, oy = &src == this ? 0 : sy0;
    for (int32_t j = 0; j < sh; ++j)
        for (int32_t i = 0; i < sw; ++i) {
            Color c = from.get(ox + i, oy + j);
            if (blendPixels) blend(x + i, y + j, c);
            else set(x + i, y + j, c);
        }
}

Image Image::crop(Rect r) const {
    int32_t x0 = static_cast<int32_t>(r.x), y0 = static_cast<int32_t>(r.y);
    Image out(static_cast<int32_t>(r.w), static_cast<int32_t>(r.h));
    for (int32_t y = 0; y < out.height_; ++y)
        for (int32_t x = 0; x < out.width_; ++x) out.set(x, y, get(x0 + x, y0 + y));
    return out;
}

void Image::flipX() {
    for (int32_t y = 0; y < height_; ++y)
        for (int32_t x = 0; x < width_ / 2; ++x) {
            uint8_t* a = &pixels_[(static_cast<size_t>(y) * width_ + x) * 4];
            uint8_t* b = &pixels_[(static_cast<size_t>(y) * width_ + (width_ - 1 - x)) * 4];
            std::swap_ranges(a, a + 4, b);
        }
}

void Image::flipY() {
    size_t row = static_cast<size_t>(width_) * 4;
    for (int32_t y = 0; y < height_ / 2; ++y) {
        uint8_t* a = &pixels_[y * row];
        uint8_t* b = &pixels_[(height_ - 1 - y) * row];
        std::swap_ranges(a, a + row, b);
    }
}

} // namespace drift
