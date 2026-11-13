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

Install `drift` with `curl -fsSL https://raw.githubusercontent.com/FizzWizZleDazzle/Drift/main/install.sh | sh` (on Windows, `install.ps1`), run a game with `drift run <folder>`, and make a web page of it with `drift build --web <folder>`. To build `drift` from source, configure with `-DDRIFT_BUILD_CANDELA=ON`; it needs a Rust toolchain (see [Installation](../getting-started/installation.md)).

The [Candela book](https://fizzwizzledazzle.github.io/Drift/candela-book/) (source in `bindings/candela/docs`) covers the API in full: components, systems, queries, states, messages, the engine components and resources, and the examples. The examples are in `bindings/candela/examples`: Flappy Bird, Coin Dash and Cave Walk.

Games written against the earlier `candela run` package (`app(config).startup(...).run()`) move to this API: the package is replaced by the `drift` program and the `drift` module.
