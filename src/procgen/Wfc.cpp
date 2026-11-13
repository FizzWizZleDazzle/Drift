#include <drift/procgen/Wfc.hpp>

#include <algorithm>
#include <bit>
#include <climits>
#include <map>

namespace drift {

namespace {

constexpr int kDx[4] = {1, 0, -1, 0};   // Right, Down, Left, Up
constexpr int kDy[4] = {0, 1, 0, -1};

WfcDirection opposite(WfcDirection d) { return static_cast<WfcDirection>((static_cast<int>(d) + 2) % 4); }

} // namespace

int32_t WfcRules::indexOf(int32_t value) const {
    auto it = std::find(values_.begin(), values_.end(), value);
    return it == values_.end() ? -1 : static_cast<int32_t>(it - values_.begin());
}

int32_t WfcRules::addTile(int32_t value, float weight) {
    int32_t i = indexOf(value);
    if (i >= 0) {
        weights_[i] = weight;
        return i;
    }
    if (values_.size() >= MaxTiles) return -1;
    values_.push_back(value);
    weights_.push_back(weight);
    for (auto& dir : adjacency_) dir.push_back({});
    return static_cast<int32_t>(values_.size() - 1);
}

void WfcRules::allow(int32_t a, int32_t b, WfcDirection d) {
    int32_t ia = indexOf(a), ib = indexOf(b);
    if (ia < 0) ia = addTile(a);
    if (ib < 0) ib = addTile(b);
    if (ia < 0 || ib < 0) return;
    adjacency_[static_cast<int>(d)][ia][ib / 64] |= uint64_t(1) << (ib % 64);
    adjacency_[static_cast<int>(opposite(d))][ib][ia / 64] |= uint64_t(1) << (ia % 64);
}

void WfcRules::allowAll(int32_t a, int32_t b) {
    for (int d = 0; d < 4; ++d) allow(a, b, static_cast<WfcDirection>(d));
}

bool WfcRules::allowed(int32_t a, int32_t b, WfcDirection d) const {
    int32_t ia = indexOf(a), ib = indexOf(b);
    if (ia < 0 || ib < 0) return false;
    return (adjacency_[static_cast<int>(d)][ia][ib / 64] >> (ib % 64)) & 1u;
}

WfcRules WfcRules::fromSample(const Grid& s, bool periodic) {
    WfcRules r;
    // Tiles in order of first appearance, weighted by count.
    std::vector<int32_t> order;
    std::map<int32_t, float> counts;
    for (int32_t v : s.cells) {
        if (counts.find(v) == counts.end()) order.push_back(v);
        counts[v] += 1.f;
    }
    for (int32_t v : order) r.addTile(v, counts[v]);
    for (int32_t y = 0; y < s.height; ++y)
        for (int32_t x = 0; x < s.width; ++x) {
            int32_t a = s.get(x, y);
            int32_t rx = x + 1, dy = y + 1;
            if (periodic) {
                rx %= s.width;
                dy %= s.height;
            }
            if (s.inBounds(rx, y)) r.allow(a, s.get(rx, y), WfcDirection::Right);
            if (s.inBounds(x, dy)) r.allow(a, s.get(x, dy), WfcDirection::Down);
        }
    return r;
}

Grid generateWfc(Rng& rng, const WfcRules& rules, int32_t width, int32_t height, const WfcOptions& opt) {
    const size_t T = rules.tileCount();
    if (T == 0 || width <= 0 || height <= 0) return {};
    const size_t W = rules.words();
    const size_t N = static_cast<size_t>(width) * static_cast<size_t>(height);
    const std::vector<float>& weights = rules.weights();

    std::vector<uint64_t> full(W, 0);
    for (size_t t = 0; t < T; ++t) full[t / 64] |= uint64_t(1) << (t % 64);

    std::vector<uint64_t> wave(N * W);
    std::vector<int32_t> counts(N);
    std::vector<uint32_t> stack;
    std::vector<uint64_t> reach(W);

    auto setCell = [&](size_t i, size_t tile) {
        std::fill_n(&wave[i * W], W, 0);
        wave[i * W + tile / 64] = uint64_t(1) << (tile % 64);
        counts[i] = 1;
        stack.push_back(static_cast<uint32_t>(i));
    };

    // Narrows the neighbours of every cell on the stack; false on a
    // contradiction (a cell with no tile left).
    auto propagate = [&]() {
        while (!stack.empty()) {
            uint32_t i = stack.back();
            stack.pop_back();
            int32_t x = static_cast<int32_t>(i % static_cast<uint32_t>(width));
            int32_t y = static_cast<int32_t>(i / static_cast<uint32_t>(width));
            for (int d = 0; d < 4; ++d) {
                int32_t nx = x + kDx[d], ny = y + kDy[d];
                if (opt.periodic) {
                    nx = (nx + width) % width;
                    ny = (ny + height) % height;
                } else if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                    continue;
                }
                std::fill(reach.begin(), reach.end(), 0);
                for (size_t w = 0; w < W; ++w) {
                    uint64_t bits = wave[i * W + w];
                    while (bits) {
                        size_t t = w * 64 + static_cast<size_t>(std::countr_zero(bits));
                        bits &= bits - 1;
                        const uint64_t* m = rules.mask(static_cast<WfcDirection>(d), t);
                        for (size_t k = 0; k < W; ++k) reach[k] |= m[k];
                    }
                }
                size_t n = static_cast<size_t>(ny) * static_cast<size_t>(width) + static_cast<size_t>(nx);
                bool changed = false;
                int32_t c = 0;
                for (size_t k = 0; k < W; ++k) {
                    uint64_t v = wave[n * W + k] & reach[k];
                    if (v != wave[n * W + k]) changed = true;
                    wave[n * W + k] = v;
                    c += std::popcount(v);
                }
                if (!changed) continue;
                counts[n] = c;
                if (c == 0) return false;
                stack.push_back(static_cast<uint32_t>(n));
            }
        }
        return true;
    };

    const bool hasPreset = opt.preset.width == width && opt.preset.height == height;
    std::vector<size_t> ties;
    for (int32_t attempt = 0; attempt < std::max(1, opt.attempts); ++attempt) {
        for (size_t i = 0; i < N; ++i) std::copy(full.begin(), full.end(), &wave[i * W]);
        std::fill(counts.begin(), counts.end(), static_cast<int32_t>(T));
        stack.clear();
        if (hasPreset) {
            for (size_t i = 0; i < N; ++i) {
                int32_t t = rules.indexOf(opt.preset.cells[i]);
                if (t >= 0) setCell(i, static_cast<size_t>(t));
            }
        }
        bool ok = propagate();
        while (ok) {
            // The cell with the fewest options left; ties broken at random.
            int32_t best = INT_MAX;
            ties.clear();
            for (size_t i = 0; i < N; ++i) {
                int32_t c = counts[i];
                if (c <= 1) continue;
                if (c < best) {
                    best = c;
                    ties.clear();
                }
                if (c == best) ties.push_back(i);
            }
            if (ties.empty()) break;   // every cell holds one tile
            size_t cell = ties[rng.below(static_cast<uint32_t>(ties.size()))];

            // A tile it still allows, chosen by weight.
            float total = 0.f;
            for (size_t t = 0; t < T; ++t)
                if ((wave[cell * W + t / 64] >> (t % 64)) & 1u) total += std::max(weights[t], 0.f);
            float r = rng.next01() * total;
            size_t pick = T;
            for (size_t t = 0; t < T; ++t) {
                if (!((wave[cell * W + t / 64] >> (t % 64)) & 1u)) continue;
                pick = t;   // the last allowed tile catches rounding and zero weights
                float w = std::max(weights[t], 0.f);
                if (r < w) break;
                r -= w;
            }
            setCell(cell, pick);
            ok = propagate();
        }
        if (!ok) continue;

        Grid out(width, height);
        for (size_t i = 0; i < N; ++i) {
            for (size_t w = 0; w < W; ++w) {
                if (wave[i * W + w]) {
                    out.cells[i] = rules.values()[w * 64 + static_cast<size_t>(std::countr_zero(wave[i * W + w]))];
                    break;
                }
            }
        }
        return out;
    }
    return {};
}

} // namespace drift
