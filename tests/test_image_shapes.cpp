#include "Test.hpp"

#include <drift/ImageShapes.hpp>

#include <cmath>
#include <vector>

using namespace drift;

// ---------------------------------------------------------------------------
// Outlines and convex pieces from alpha
// ---------------------------------------------------------------------------

namespace {

struct Mask {
    int32_t w, h;
    std::vector<uint8_t> a;
    Mask(int32_t w, int32_t h) : w(w), h(h), a(static_cast<size_t>(w) * h, 0) {}
    void rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t v = 255) {
        for (int32_t y = y0; y < y1; ++y)
            for (int32_t x = x0; x < x1; ++x) a[static_cast<size_t>(y) * w + x] = v;
    }
    void disc(float cx, float cy, float r, uint8_t v = 255) {
        for (int32_t y = 0; y < h; ++y)
            for (int32_t x = 0; x < w; ++x) {
                float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                if (dx * dx + dy * dy <= r * r) a[static_cast<size_t>(y) * w + x] = v;
            }
    }
    AlphaView view() const { return {a.data(), w, h, 1, w}; }
};

float sumArea(const std::vector<Outline>& loops) {
    float s = 0.f;
    for (const auto& l : loops) s += outlineArea(l);
    return s;
}

bool isConvex(const Outline& p) {
    size_t n = p.size();
    for (size_t i = 0; i < n; ++i) {
        Vec2 a = p[(i + n - 1) % n], b = p[i], c = p[(i + 1) % n];
        if ((b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x) < -1e-4f) return false;
    }
    return true;
}

// Pieces are convex, small enough for Box2D, wound the same way as solid
// outlines, and cover exactly the area of the outlines.
void checkPieces(const std::vector<Outline>& outlines) {
    auto pieces = convexPieces(outlines);
    CHECK(!pieces.empty());
    for (const auto& p : pieces) {
        CHECK(p.size() >= 3 && p.size() <= 8);
        CHECK(isConvex(p));
        CHECK(outlineArea(p) > 0.f);
    }
    CHECK_NEAR(sumArea(pieces), sumArea(outlines), 0.01f * sumArea(outlines) + 0.01f);
}

} // namespace

TEST(alpha_outline_square) {
    Mask m(16, 16);
    m.rect(4, 4, 12, 12);
    auto raw = traceAlphaOutlines(m.view(), 0.5f);
    REQUIRE(raw.size() == 1);
    // Corners are cut by half a pixel: 8x8 less four half-pixel triangles.
    CHECK_NEAR(outlineArea(raw[0]), 64.f - 0.5f, 1e-4f);
    auto simple = alphaOutlines(m.view(), {0.5f, 0.25f, 4.f});
    REQUIRE(simple.size() == 1);
    CHECK(simple[0].size() == 8);   // a square with its corners cut
    CHECK(outlineArea(simple[0]) > 0.f);
    for (Vec2 p : simple[0]) CHECK(p.x >= 4.f && p.x <= 12.f && p.y >= 4.f && p.y <= 12.f);
    // A looser tolerance keeps the 4 corners.
    auto loose = alphaOutlines(m.view(), {0.5f, 1.f, 4.f});
    REQUIRE(loose.size() == 1);
    CHECK(loose[0].size() == 4);
    checkPieces(simple);
}

TEST(alpha_outline_threshold) {
    Mask m(8, 8);
    m.rect(0, 0, 8, 8, 100);   // alpha about 0.39
    CHECK(traceAlphaOutlines(m.view(), 0.5f).empty());
    CHECK(traceAlphaOutlines(m.view(), 0.3f).size() == 1);
    // Pixels at the image edge close against the outside.
    auto full = traceAlphaOutlines(m.view(), 0.3f);
    CHECK_NEAR(outlineArea(full[0]), 64.f - 0.5f, 1e-4f);
}

TEST(alpha_outline_ring_has_a_hole) {
    Mask m(64, 64);
    m.disc(32.f, 32.f, 28.f);
    m.disc(32.f, 32.f, 14.f, 0);
    auto outlines = alphaOutlines(m.view(), {0.5f, 1.f, 4.f});
    REQUIRE(outlines.size() == 2);
    int solid = 0, holes = 0;
    for (const auto& o : outlines) {
        if (outlineArea(o) > 0.f) ++solid;
        else ++holes;
        CHECK(o.size() < 40);   // simplified, not one point per pixel
    }
    CHECK(solid == 1 && holes == 1);
    float expected = 3.14159f * (28.f * 28.f - 14.f * 14.f);
    CHECK_NEAR(sumArea(outlines), expected, expected * 0.03f);
    checkPieces(outlines);
    // No piece covers the hole's centre.
    for (const auto& p : convexPieces(outlines)) {
        bool inside = true;
        for (size_t i = 0; i < p.size(); ++i) {
            Vec2 a = p[i], b = p[(i + 1) % p.size()];
            if ((b.x - a.x) * (32.f - a.y) - (b.y - a.y) * (32.f - a.x) < 0.f) inside = false;
        }
        CHECK(!inside);
    }
}

TEST(alpha_outline_concave_shapes) {
    // An L.
    Mask l(32, 32);
    l.rect(2, 2, 10, 30);
    l.rect(2, 22, 30, 30);
    auto lo = alphaOutlines(l.view(), {0.5f, 1.f, 4.f});
    REQUIRE(lo.size() == 1);
    CHECK(lo[0].size() <= 8);
    // Within the tolerance of the pixels: each long edge may lean by up to a pixel.
    CHECK_NEAR(outlineArea(lo[0]), 8.f * 28.f + 20.f * 8.f, 20.f);
    checkPieces(lo);
    CHECK(convexPieces(lo).size() >= 2);

    // A U with two holes in its base, and a star-like blob.
    Mask u(48, 48);
    u.rect(4, 4, 12, 44);
    u.rect(36, 4, 44, 44);
    u.rect(4, 30, 44, 44);
    u.rect(16, 34, 20, 40, 0);
    u.rect(28, 34, 32, 40, 0);
    auto uo = alphaOutlines(u.view(), {0.5f, 1.f, 4.f});
    CHECK(uo.size() == 3);
    checkPieces(uo);

    Mask blob(64, 64);
    blob.disc(32.f, 32.f, 16.f);
    for (int k = 0; k < 6; ++k) {
        float ang = k * 1.0472f;
        blob.disc(32.f + std::cos(ang) * 20.f, 32.f + std::sin(ang) * 20.f, 9.f);
    }
    auto bo = alphaOutlines(blob.view(), {0.5f, 1.f, 4.f});
    REQUIRE(bo.size() == 1);
    checkPieces(bo);
}

TEST(alpha_outline_drops_specks) {
    Mask m(32, 32);
    m.rect(4, 4, 20, 20);
    m.rect(28, 28, 29, 29);       // one stray pixel
    m.rect(10, 10, 11, 11, 0);    // a one-pixel hole
    auto raw = traceAlphaOutlines(m.view(), 0.5f);
    CHECK(raw.size() == 3);
    auto kept = alphaOutlines(m.view(), {0.5f, 1.f, 4.f});
    REQUIRE(kept.size() == 1);
    CHECK(outlineArea(kept[0]) > 200.f);
    // With no minimum the speck stays.
    CHECK(alphaOutlines(m.view(), {0.5f, 0.25f, 0.f}).size() == 3);
}

TEST(alpha_outline_simplify_keeps_shape) {
    Outline zig;
    for (int i = 0; i <= 20; ++i) zig.push_back({float(i), (i % 2) * 0.2f});
    zig.push_back({20.f, 10.f});
    zig.push_back({0.f, 10.f});
    auto s = simplifyOutline(zig, 0.5f);
    CHECK(s.size() == 4);
    auto exact = simplifyOutline(zig, 0.f);
    CHECK(exact.size() == zig.size());
}
