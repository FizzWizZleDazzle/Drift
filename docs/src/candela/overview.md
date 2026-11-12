# Candela

Drift games can be written in [candela](https://candela.lumenfx.dev) instead of C++. A game declares its components, resources and systems with attributes, and the `drift` program runs it: it compiles the scripts, calls each system every frame with the data its parameters ask for, and draws and simulates the engine components underneath. The engine is the same one C++ games use, on the desktop and in the browser.

```rust
import "drift" { * };

@component(requires: [Velocity])
struct Ship { speed: float = 300.0 }

@system
fn steer(ship: Ship, velocity: Velocity, input: Input) {
    velocity.linear = input.axis2d("move") * ship.speed;
}
```

Build `drift` with `-DDRIFT_BUILD_CANDELA=ON` (it needs a Rust toolchain; see [Installation](../getting-started/installation.md)) and run a game from its folder with `drift run`. For the browser, `drift_candela_web_game(<target> PROJECT <folder>)` makes a page per game.

The Candela book, in `bindings/candela/docs`, covers the API in full: components, systems, queries, states, messages, the engine components and resources, and the examples. The examples are in `bindings/candela/examples`: Flappy Bird and Coin Dash.

Games written against the earlier `candela run` package (`app(config).startup(...).run()`) move to this API: the package is replaced by the `drift` program and the `drift` module.
