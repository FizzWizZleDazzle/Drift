#pragma once

// Wave function collapse over tiles (the "simple tiled" model): every cell
// starts able to hold any tile, and cells collapse one by one to a tile the
// rules allow next to their neighbours, until the grid is complete.
//
//   WfcRules rules = WfcRules::fromSample(handDrawnExample);
//   Grid level = generateWfc(rng, rules, 60, 40);
//   if (level.width == 0) { /* every attempt contradicted itself */ }
//
// Tiles are the values of a Grid; the rules say which value may sit to the
// right of, below, left of and above which.

#include <drift/math/Random.hpp>
#include <drift/procgen/Grid.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace drift {

enum class WfcDirection : uint8_t { Right, Down, Left, Up };

class WfcRules {
public:
    // Learns from an example: every value in it becomes a tile weighted by
    // how often it appears, and every pair of neighbouring values is
    // allowed in the direction it appears. `periodic` also counts pairs
    // across the wrapped edges.
    static WfcRules fromSample(const Grid& sample, bool periodic = false);

    // Adds a tile (a grid value) or changes its weight. Returns its index.
    int32_t addTile(int32_t value, float weight = 1.f);
    // `b` may sit in `direction` of `a`, and `a` in the opposite direction
    // of `b`.
    void allow(int32_t a, int32_t b, WfcDirection direction);
    // `a` and `b` may touch on every side.
    void allowAll(int32_t a, int32_t b);
    bool allowed(int32_t a, int32_t b, WfcDirection direction) const;

    size_t tileCount() const { return values_.size(); }
    const std::vector<int32_t>& values() const { return values_; }
    const std::vector<float>& weights() const { return weights_; }
    int32_t indexOf(int32_t value) const;

    // Allowed neighbour tiles, as bitsets of `words()` 64-bit words:
    // mask(direction, tile)[w].
    size_t words() const { return (values_.size() + 63) / 64; }
    const uint64_t* mask(WfcDirection direction, size_t tile) const {
        return adjacency_[static_cast<size_t>(direction)][tile].data();
    }

    static constexpr size_t MaxTiles = 256;

private:
    std::vector<int32_t> values_;
    std::vector<float> weights_;
    std::vector<std::array<uint64_t, MaxTiles / 64>> adjacency_[4];
};

struct WfcOptions {
    // Restarts after a contradiction, each with the generator where the
    // last attempt left it.
    int32_t attempts = 10;
    // Neighbours wrap around the edges, for tiling textures.
    bool periodic = false;
    // Cells fixed before collapsing starts: a grid of the output size where
    // a cell holding a tile value forces that tile and any other value
    // (such as -1) leaves the cell free. Empty for none.
    Grid preset;
};

// A width x height grid of tile values that obeys every rule, or an empty
// grid (width 0) when all attempts hit a contradiction. Up to 256 tiles.
Grid generateWfc(Rng& rng, const WfcRules& rules, int32_t width, int32_t height,
                 const WfcOptions& options = {});

} // namespace drift
