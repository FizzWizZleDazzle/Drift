# Random Numbers and Noise

Seeded randomness for gameplay and procedural content. The same seed gives the same results on every platform, the web build included, so a level seed, a replay, or a daily challenge reproduces exactly.

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
| `reseed(seed, stream)` | Restart the sequence; the same seed with another stream gives another sequence |
| `split()` | A new generator seeded from this one, for a sub-task |
| `state()`, `increment()`, `Rng::fromState(state, increment)` | Save a generator's position and resume it later |
| `Rng::hash(text)` | A 64-bit seed from text, for seeds players type |

```cpp
const float lootWeights[] = {70.f, 25.f, 5.f};   // common, rare, epic
int tier = rng.weightedIndex(lootWeights);

std::vector<Card> deck = ...;
rng.shuffle(std::span<Card>(deck));

Rng daily(Rng::hash("2026-11-12"));
```

Integer results, `next01()`, `angle()` and float `range()` are identical on every platform: the engine compiles them without fused multiply-add. `unitVector()`, `insideCircle()` and `normal()` go through the platform's `sin`, `cos` and `log`, whose last bits can differ between math libraries; leave them out of anything two platforms must agree on bit for bit. The [generators](../gameplay/procedural-generation.md) avoid them for that reason.

For weighted choices of items rather than indices, see `WeightedTable` on the [Procedural Generation](../gameplay/procedural-generation.md#weighted-tables) page.

`Rng` is not for cryptography or anything security related.

## The game's seed

**Header:** `#include <drift/plugins/RandomPlugin.hpp>`

**Plugin:** `RandomPlugin` (included in `DefaultPlugins`)

`RandomPlugin` reads one seed for the whole game from `drift.toml` and keeps it in the `Random` resource:

```toml
[random]
seed = 1234   # 0 (the default) picks a new seed each run and logs it
```

```cpp
void buildLevel(ResMut<Random> random) {
    Rng levels = random->stream("levels");   // the same every time for this seed
    Grid cave = generateCave(levels, 80, 50);
    int32_t mood = random->rng.range(0, 3);  // the shared generator
}
```

| Member | Use |
|--------|-----|
| `seed()` | The seed in use |
| `rng` | A shared generator, seeded from the seed |
| `stream(name)` | A generator for one purpose: the same seed and name always give the same sequence, independent of `rng` and of other names |
| `reseed(seed)` | Start over from another seed |

Streams keep features apart: rolling an extra loot drop does not change the next level. When the seed is 0 the log names the seed it picked; put it in `drift.toml`, or pass `--set random.seed=<n>`, to play the same run again.

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
| `warp(x, y, strength, frequency, type, octaves)` | Moves the point by up to `strength` along two other noise fields (domain warp) |
| `sample(options, x, y)` | Everything at once from a `NoiseOptions` |

Scale the input to choose the feature size: multiplying coordinates by 0.05 gives features about 20 units across. `lacunarity` (default 2) is the frequency step between octaves and `gain` (default 0.5) the amplitude step. Use a different seed, or offset the coordinates, for independent fields.

### Noise options and domain warp

`NoiseOptions` holds a whole recipe, which suits settings structs and level definitions:

```cpp
NoiseOptions terrain{.type = NoiseType::Simplex, .frequency = 0.03f, .octaves = 5, .warp = 12.f};
float h = noise.sample(terrain, x, y);
```

| Field | Default | Meaning |
|-------|---------|---------|
| `type` | `Simplex` | `Value`, `Perlin` or `Simplex` |
| `frequency` | 1 | Multiplies the coordinates |
| `octaves`, `lacunarity`, `gain` | 1, 2, 0.5 | fBm layering; 1 octave samples the type directly |
| `warp` | 0 | Domain warp distance in input units; 0 turns it off |
| `warpFrequency` | 1 | Frequency of the warp field, relative to `frequency` |

Domain warp samples the field at a point pushed around by two more noise fields, which bends straight ridges into swirls and rivers. A warp of a few times the feature size gives the classic marbled look.

`gridFromNoise` turns a noise recipe into a `Grid` of walls and floors; see [Procedural Generation](../gameplay/procedural-generation.md#noise-grids).
