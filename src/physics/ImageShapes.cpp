// Outlines and convex pieces from an image's alpha channel.

#include <drift/ImageShapes.hpp>
#include <drift/Image.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <utility>

namespace drift {

namespace {

float cross(Vec2 o, Vec2 a, Vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }
float cross2(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
bool same(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }

float segmentDistanceSq(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 ab = b - a, ap = p - a;
    float len = ab.dot(ab);
    float t = len > 0.f ? std::clamp(ap.dot(ab) / len, 0.f, 1.f) : 0.f;
    Vec2 d = ap - ab * t;
    return d.dot(d);
}

bool pointInLoop(Vec2 p, const Outline& loop) {
    bool in = false;
    for (size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
        Vec2 a = loop[i], b = loop[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

// Whether the open segments p1-p2 and q1-q2 meet. Touching counts, except
// at points equal to an end of p, which bridges share with the edges they
// start from.
bool segmentsMeet(Vec2 p1, Vec2 p2, Vec2 q1, Vec2 q2) {
    if (same(q1, p1) || same(q1, p2) || same(q2, p1) || same(q2, p2)) {
        // Sharing an end: they meet only if they overlap along a line.
        Vec2 shared = (same(q1, p1) || same(q1, p2)) ? q1 : q2;
        Vec2 other = same(shared, q1) ? q2 : q1;
        Vec2 far = same(shared, p1) ? p2 : p1;
        if (std::fabs(cross(shared, far, other)) > 1e-6f) return false;
        return (other - shared).dot(far - shared) > 0.f;
    }
    float d1 = cross(q1, q2, p1), d2 = cross(q1, q2, p2);
    float d3 = cross(p1, p2, q1), d4 = cross(p1, p2, q2);
    const float eps = 1e-6f;
    if (((d1 > eps && d2 < -eps) || (d1 < -eps && d2 > eps)) && ((d3 > eps && d4 < -eps) || (d3 < -eps && d4 > eps)))
        return true;
    auto onSegment = [](Vec2 a, Vec2 b, Vec2 p) {
        return std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x) && std::min(a.y, b.y) <= p.y &&
               p.y <= std::max(a.y, b.y);
    };
    // A point of one lying on the other.
    if (std::fabs(d3) <= eps && onSegment(p1, p2, q1)) return true;
    if (std::fabs(d4) <= eps && onSegment(p1, p2, q2)) return true;
    if (std::fabs(d1) <= eps && onSegment(q1, q2, p1)) return true;
    if (std::fabs(d2) <= eps && onSegment(q1, q2, p2)) return true;
    return false;
}

// Whether direction `d` from vertex `v` points into the polygon, whose
// neighbours of `v` are `prev` and `next` (positive winding).
bool insideWedge(Vec2 prev, Vec2 v, Vec2 next, Vec2 d) {
    Vec2 e0 = v - prev, e1 = next - v;
    bool left0 = cross2(e0, d) > 0.f, left1 = cross2(e1, d) > 0.f;
    return cross2(e0, e1) >= 0.f ? (left0 && left1) : (left0 || left1);
}

// Joins each hole to the outline with a two-way bridge from the hole's
// rightmost point to the closest outline point it can see, so the result is
// one loop that ear clipping can cut.
Outline joinHoles(Outline poly, std::vector<const Outline*> holes) {
    std::sort(holes.begin(), holes.end(), [](const Outline* a, const Outline* b) {
        auto maxX = [](const Outline* o) {
            float m = -1e30f;
            for (Vec2 p : *o) m = std::max(m, p.x);
            return m;
        };
        return maxX(a) > maxX(b);
    });
    for (size_t h = 0; h < holes.size(); ++h) {
        const Outline& hole = *holes[h];
        size_t m = 0;
        for (size_t i = 1; i < hole.size(); ++i)
            if (hole[i].x > hole[m].x || (hole[i].x == hole[m].x && hole[i].y < hole[m].y)) m = i;
        Vec2 M = hole[m];

        std::vector<std::pair<float, size_t>> order;
        order.reserve(poly.size());
        for (size_t i = 0; i < poly.size(); ++i) {
            Vec2 d = poly[i] - M;
            order.push_back({d.dot(d), i});
        }
        std::sort(order.begin(), order.end());

        auto clear = [&](Vec2 a, Vec2 b, const Outline& loop) {
            for (size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++)
                if (segmentsMeet(a, b, loop[j], loop[i])) return false;
            return true;
        };
        size_t pick = SIZE_MAX;
        for (auto [dist, i] : order) {
            (void)dist;
            Vec2 V = poly[i];
            if (same(V, M)) continue;
            Vec2 prev = poly[(i + poly.size() - 1) % poly.size()], next = poly[(i + 1) % poly.size()];
            if (!insideWedge(prev, V, next, M - V)) continue;
            if (!clear(V, M, poly) || !clear(V, M, hole)) continue;
            bool blocked = false;
            for (size_t k = h + 1; k < holes.size() && !blocked; ++k) blocked = !clear(V, M, *holes[k]);
            if (blocked) continue;
            pick = i;
            break;
        }
        if (pick == SIZE_MAX) pick = order.empty() ? 0 : order.front().second;   // no clear bridge: take the closest

        Outline joined;
        joined.reserve(poly.size() + hole.size() + 2);
        joined.insert(joined.end(), poly.begin(), poly.begin() + static_cast<std::ptrdiff_t>(pick) + 1);
        for (size_t k = 0; k <= hole.size(); ++k) joined.push_back(hole[(m + k) % hole.size()]);
        joined.push_back(poly[pick]);
        joined.insert(joined.end(), poly.begin() + static_cast<std::ptrdiff_t>(pick) + 1, poly.end());
        poly = std::move(joined);
    }
    return poly;
}

bool inTriangle(Vec2 p, Vec2 a, Vec2 b, Vec2 c) {
    const float eps = -1e-6f;
    return cross(a, b, p) >= eps && cross(b, c, p) >= eps && cross(c, a, p) >= eps;
}

// Triangles (indices into `pts`, positive winding) covering the loop.
std::vector<std::vector<int32_t>> earClip(const Outline& pts) {
    std::vector<std::vector<int32_t>> tris;
    std::vector<int32_t> idx(pts.size());
    std::iota(idx.begin(), idx.end(), 0);
    const float eps = 1e-7f;
    size_t i = 0, misses = 0;
    while (idx.size() > 3) {
        size_t n = idx.size();
        i %= n;
        int32_t a = idx[(i + n - 1) % n], b = idx[i], c = idx[(i + 1) % n];
        float cr = cross(pts[a], pts[b], pts[c]);
        bool clip = false;
        if (std::fabs(cr) <= eps) {
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));   // a straight run or a spike: no area
            misses = 0;
            continue;
        }
        if (cr > eps) {
            clip = true;
            for (int32_t q : idx) {
                if (q == a || q == b || q == c) continue;
                Vec2 p = pts[q];
                if (same(p, pts[a]) || same(p, pts[b]) || same(p, pts[c])) continue;
                if (inTriangle(p, pts[a], pts[b], pts[c])) {
                    clip = false;
                    break;
                }
            }
        }
        if (!clip && misses > n) {
            // Nothing is an ear (the loop touches itself after
            // simplification): cut the most convex corner and go on.
            size_t best = 0;
            float bestCr = -1e30f;
            for (size_t k = 0; k < n; ++k) {
                float ck = cross(pts[idx[(k + n - 1) % n]], pts[idx[k]], pts[idx[(k + 1) % n]]);
                if (ck > bestCr) bestCr = ck, best = k;
            }
            i = best;
            a = idx[(i + n - 1) % n], b = idx[i], c = idx[(i + 1) % n];
            if (bestCr > eps) tris.push_back({a, b, c});
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
            misses = 0;
            continue;
        }
        if (clip) {
            tris.push_back({a, b, c});
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
            if (i > 0) --i;
            misses = 0;
        } else {
            ++i;
            ++misses;
        }
    }
    if (idx.size() == 3 && cross(pts[idx[0]], pts[idx[1]], pts[idx[2]]) > eps) tris.push_back(idx);
    return tris;
}

bool convex(const Outline& pts, const std::vector<int32_t>& poly) {
    size_t n = poly.size();
    for (size_t k = 0; k < n; ++k)
        if (cross(pts[poly[(k + n - 1) % n]], pts[poly[k]], pts[poly[(k + 1) % n]]) < -1e-5f) return false;
    return true;
}

// Merges neighbouring pieces across a shared edge while the result stays
// convex and within `maxVertices`.
std::vector<std::vector<int32_t>> mergePieces(const Outline& pts, std::vector<std::vector<int32_t>> pieces,
                                              int32_t maxVertices) {
    auto key = [](int32_t u, int32_t v) { return (static_cast<uint64_t>(static_cast<uint32_t>(u)) << 32) | static_cast<uint32_t>(v); };
    std::map<uint64_t, size_t> edgeOwner;   // directed edge -> piece
    std::vector<bool> alive(pieces.size(), true);
    auto own = [&](size_t p) {
        const auto& poly = pieces[p];
        for (size_t k = 0; k < poly.size(); ++k) edgeOwner[key(poly[k], poly[(k + 1) % poly.size()])] = p;
    };
    auto disown = [&](size_t p) {
        const auto& poly = pieces[p];
        for (size_t k = 0; k < poly.size(); ++k) {
            auto it = edgeOwner.find(key(poly[k], poly[(k + 1) % poly.size()]));
            if (it != edgeOwner.end() && it->second == p) edgeOwner.erase(it);
        }
    };
    for (size_t p = 0; p < pieces.size(); ++p) own(p);

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t p = 0; p < pieces.size(); ++p) {
            if (!alive[p]) continue;
            bool merged = true;
            while (merged) {
                merged = false;
                const auto& A = pieces[p];
                for (size_t k = 0; k < A.size(); ++k) {
                    int32_t u = A[k], v = A[(k + 1) % A.size()];
                    auto it = edgeOwner.find(key(v, u));
                    if (it == edgeOwner.end() || it->second == p || !alive[it->second]) continue;
                    size_t q = it->second;
                    const auto& B = pieces[q];
                    if (static_cast<int32_t>(A.size() + B.size()) - 2 > maxVertices) continue;
                    // A from v round to u, then B between u and v.
                    std::vector<int32_t> joined;
                    size_t start = (k + 1) % A.size();
                    for (size_t s = 0; s < A.size(); ++s) joined.push_back(A[(start + s) % A.size()]);
                    size_t bu = static_cast<size_t>(std::find(B.begin(), B.end(), u) - B.begin());
                    for (size_t s = 1; s + 1 < B.size(); ++s) joined.push_back(B[(bu + s) % B.size()]);
                    if (!convex(pts, joined)) continue;
                    disown(p);
                    disown(q);
                    alive[q] = false;
                    pieces[p] = std::move(joined);
                    own(p);
                    merged = changed = true;
                    break;
                }
            }
        }
    }
    std::vector<std::vector<int32_t>> out;
    for (size_t p = 0; p < pieces.size(); ++p)
        if (alive[p]) out.push_back(std::move(pieces[p]));
    return out;
}

} // namespace

AlphaView alphaOf(const Image& image, Rect region) {
    AlphaView v;
    if (image.empty()) return v;
    int32_t x0 = 0, y0 = 0, x1 = image.width(), y1 = image.height();
    if (region.w > 0.f && region.h > 0.f) {
        x0 = std::max(0, static_cast<int32_t>(std::lround(region.x)));
        y0 = std::max(0, static_cast<int32_t>(std::lround(region.y)));
        x1 = std::min(image.width(), static_cast<int32_t>(std::lround(region.x + region.w)));
        y1 = std::min(image.height(), static_cast<int32_t>(std::lround(region.y + region.h)));
    }
    if (x1 <= x0 || y1 <= y0) return v;
    v.data = image.data() + (static_cast<size_t>(y0) * image.width() + x0) * 4 + 3;
    v.width = x1 - x0;
    v.height = y1 - y0;
    v.pixelStride = 4;
    v.rowStride = image.width() * 4;
    return v;
}

std::vector<Outline> traceAlphaOutlines(AlphaView alpha, float threshold) {
    std::vector<Outline> out;
    if (!alpha.data || alpha.width <= 0 || alpha.height <= 0) return out;
    const int32_t W = alpha.width, H = alpha.height;
    // Pixel centres with a ring of empty ones around, so every outline closes.
    const int32_t GW = W + 2, GH = H + 2;
    const int cut = std::clamp(static_cast<int>(std::ceil(threshold * 255.f - 1e-3f)), 1, 255);
    std::vector<uint8_t> g(static_cast<size_t>(GW) * GH, 0);
    for (int32_t y = 0; y < H; ++y) {
        const uint8_t* row = alpha.data + static_cast<ptrdiff_t>(y) * alpha.rowStride;
        uint8_t* dst = g.data() + static_cast<size_t>(y + 1) * GW + 1;
        for (int32_t x = 0; x < W; ++x) dst[x] = row[static_cast<ptrdiff_t>(x) * alpha.pixelStride] >= cut;
    }

    // Outline points sit halfway between two neighbouring centres. Point
    // y*GW+x is between centres (x, y) and (x+1, y); N + y*GW+x between
    // (x, y) and (x, y+1). Each point has one point after it, walking with
    // the solid pixels on the right as the screen shows them (y down).
    const int32_t N = GW * GH;
    std::vector<int32_t> next(static_cast<size_t>(2) * N, -1);
    std::vector<int32_t> starts;
    auto link = [&](int32_t from, int32_t to) {
        next[from] = to;
        starts.push_back(from);
    };
    for (int32_t y = 0; y + 1 < GH; ++y) {
        const uint8_t* top = g.data() + static_cast<size_t>(y) * GW;
        const uint8_t* bottom = top + GW;
        for (int32_t x = 0; x + 1 < GW; ++x) {
            int code = (top[x] << 3) | (top[x + 1] << 2) | (bottom[x + 1] << 1) | bottom[x];
            if (code == 0 || code == 15) continue;
            int32_t T = y * GW + x, B = (y + 1) * GW + x, L = N + y * GW + x, R = N + y * GW + x + 1;
            switch (code) {
                case 8: link(T, L); break;                  // top-left solid
                case 4: link(R, T); break;                  // top-right
                case 2: link(B, R); break;                  // bottom-right
                case 1: link(L, B); break;                  // bottom-left
                case 12: link(R, L); break;                 // top row
                case 6: link(B, T); break;                  // right column
                case 3: link(L, R); break;                  // bottom row
                case 9: link(T, B); break;                  // left column
                case 7: link(L, T); break;                  // all but top-left
                case 11: link(T, R); break;                 // all but top-right
                case 13: link(R, B); break;                 // all but bottom-right
                case 14: link(B, L); break;                 // all but bottom-left
                case 10: link(T, R); link(B, L); break;     // diagonal, joined
                case 5: link(L, T); link(R, B); break;      // diagonal, joined
            }
        }
    }

    auto position = [&](int32_t id) {
        if (id < N) return Vec2{static_cast<float>(id % GW), static_cast<float>(id / GW) - 0.5f};
        id -= N;
        return Vec2{static_cast<float>(id % GW) - 0.5f, static_cast<float>(id / GW)};
    };
    for (int32_t s : starts) {
        if (next[s] < 0) continue;
        Outline loop;
        int32_t p = s;
        while (p >= 0 && next[p] >= 0) {
            loop.push_back(position(p));
            int32_t q = next[p];
            next[p] = -1;
            p = q;
        }
        if (loop.size() >= 3) out.push_back(std::move(loop));
    }
    return out;
}

float outlineArea(const Outline& loop) {
    double a = 0.0;
    for (size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++)
        a += static_cast<double>(loop[j].x) * loop[i].y - static_cast<double>(loop[i].x) * loop[j].y;
    return static_cast<float>(a * 0.5);
}

Outline simplifyOutline(const Outline& loop, float tolerance) {
    const size_t n = loop.size();
    if (n < 4) return loop;
    float tolSq = std::max(tolerance, 1e-4f);
    tolSq *= tolSq;
    // Split the loop at its leftmost point and the point farthest from it.
    size_t i0 = 0;
    for (size_t i = 1; i < n; ++i)
        if (loop[i].x < loop[i0].x || (loop[i].x == loop[i0].x && loop[i].y < loop[i0].y)) i0 = i;
    size_t i1 = i0;
    float far = -1.f;
    for (size_t i = 0; i < n; ++i) {
        Vec2 d = loop[i] - loop[i0];
        if (d.dot(d) > far) far = d.dot(d), i1 = i;
    }
    if (i1 < i0) i1 += n;
    std::vector<uint8_t> keep(n, 0);
    keep[i0] = 1;
    keep[i1 % n] = 1;
    std::vector<std::pair<size_t, size_t>> stack{{i0, i1}, {i1, i0 + n}};
    while (!stack.empty()) {
        auto [s, e] = stack.back();
        stack.pop_back();
        if (e <= s + 1) continue;
        Vec2 a = loop[s % n], b = loop[e % n];
        float worst = -1.f;
        size_t at = s;
        for (size_t k = s + 1; k < e; ++k) {
            float d = segmentDistanceSq(loop[k % n], a, b);
            if (d > worst) worst = d, at = k;
        }
        if (worst > tolSq) {
            keep[at % n] = 1;
            stack.push_back({s, at});
            stack.push_back({at, e});
        }
    }
    Outline out;
    for (size_t i = 0; i < n; ++i)
        if (keep[i]) out.push_back(loop[i]);
    if (out.size() < 3) out.clear();
    return out;
}

std::vector<Outline> alphaOutlines(AlphaView alpha, const AlphaShapeOptions& options) {
    std::vector<Outline> out;
    float minArea = std::max(options.minArea, 0.f);
    for (Outline& loop : traceAlphaOutlines(alpha, options.threshold)) {
        float area = outlineArea(loop);
        if (std::fabs(area) < minArea || area == 0.f) continue;
        Outline simple = simplifyOutline(loop, options.tolerance);
        if (simple.size() < 3) continue;
        float after = outlineArea(simple);
        if ((after > 0.f) != (area > 0.f) || std::fabs(after) < 1e-3f) continue;   // simplified away
        out.push_back(std::move(simple));
    }
    return out;
}

std::vector<Outline> convexPieces(const std::vector<Outline>& outlines, int32_t maxVertices) {
    maxVertices = std::max(maxVertices, 3);
    std::vector<size_t> solids, holes;
    std::vector<float> areas(outlines.size());
    for (size_t i = 0; i < outlines.size(); ++i) {
        if (outlines[i].size() < 3) continue;
        areas[i] = outlineArea(outlines[i]);
        if (areas[i] > 0.f) solids.push_back(i);
        else if (areas[i] < 0.f) holes.push_back(i);
    }
    // Each hole belongs to the smallest solid outline around it.
    std::vector<std::vector<const Outline*>> holesOf(outlines.size());
    for (size_t h : holes) {
        Vec2 probe = outlines[h][0];
        size_t owner = SIZE_MAX;
        for (size_t s : solids)
            if (pointInLoop(probe, outlines[s]) && (owner == SIZE_MAX || areas[s] < areas[owner])) owner = s;
        if (owner != SIZE_MAX) holesOf[owner].push_back(&outlines[h]);
    }
    std::vector<Outline> out;
    for (size_t s : solids) {
        Outline poly = holesOf[s].empty() ? outlines[s] : joinHoles(outlines[s], holesOf[s]);
        auto pieces = mergePieces(poly, earClip(poly), maxVertices);
        for (const auto& piece : pieces) {
            Outline o;
            for (int32_t i : piece) o.push_back(poly[i]);
            out.push_back(std::move(o));
        }
    }
    return out;
}

} // namespace drift
