# Random Numbers and Noise

Seeded randomness for gameplay and procedural content. The same seed gives the same results on every platform, so a level seed, a replay, or a daily challenge reproduces exactly.

## Rng

**Header:** `#include <drift/math/Random.hpp>`

`Rng` is a small PCG32 generator. Keep one per purpose (level generation, loot, effects) so changing one does not shift the others.

```cpp
Rng rng(levelSeed);

int   damage = rng.range(5, 10);           // 5..10 inclusive
float speed  = rng.range(80.f, 120.f);     // [80, 120)
bool  crit   = rng.chance(0.15f);
Vec2  dir    = rng.unitVector();
Vec2  spot   = rng.insideCircle(64.f);
```

| Call | Result |
|------|--------|
| `nextU32()`, `nextU64()` | Raw bits |
| `next01()` | Float in [0, 1) |
| `below(n)` | Integer in [0, n), without modulo bias |
| `range(lo, hi)` | Integers: inclusive. Floats: [lo, hi) |
| `chance(p)` | True with probability `p` |
| `sign()` | -1 or +1 |
| `angle()`, `unitVector()` | Random direction |
| `insideCircle(r)`, `insideRect(rect)` | Uniform point in a shape |
| `color(alpha)` | Random RGB color |
| `normal(mean, stddev)` | Gaussian sample |
| `pick(span)` | Random element |
| `weightedIndex(weights)` | Index chosen in proportion to its weight; -1 if all weights are zero |
| `shuffle(span)` | Shuffle in place |
| `reseed(seed)` | Restart the sequence |

```cpp
const float lootWeights[] = {70.f, 25.f, 5.f};   // common, rare, epic
int tier = rng.weightedIndex(lootWeights);

std::vector<Card> deck = ...;
rng.shuffle(std::span<Card>(deck));
```

`Rng` is not for cryptography or anything security related.

## Noise

**Header:** `#include <drift/math/Noise.hpp>`

`Noise` produces smooth 2D fields for terrain, clouds, wobble and camera shake. Every function returns values in [-1, 1].

```cpp
Noise noise(worldSeed);

float h = noise.simplex(x * 0.05f, y * 0.05f);                      // one octave
float t = noise.fbm(NoiseType::Simplex, x * 0.02f, y * 0.02f, 5);   // five octaves
```

| Function | Character |
|----------|-----------|
| `value(x, y)` | Smoothly interpolated random lattice values; blocky at low frequency |
| `perlin(x, y)` | Gradient noise; 0 at integer coordinates |
| `simplex(x, y)` | Gradient noise on a triangular grid; fewer directional artifacts |
| `fbm(type, x, y, octaves, lacunarity, gain)` | Layers of one type at rising frequency and falling amplitude, normalized back to [-1, 1] |

Scale the input to choose the feature size: multiplying coordinates by 0.05 gives features about 20 units across. `lacunarity` (default 2) is the frequency step between octaves and `gain` (default 0.5) the amplitude step. Use a different seed, or offset the coordinates, for independent fields.
