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

The [Candela book](https://fizzwizzledazzle.github.io/Drift/candela-book/) (source in `bindings/candela/docs`) covers the API in full: a tutorial that builds a game in the browser, concepts, task guides, the reference of every type and function of the `drift` module, and a [gallery of playable examples](https://fizzwizzledazzle.github.io/Drift/candela-book/examples/gallery.html). The examples are in `bindings/candela/examples`.

Games written against the earlier `candela run` package (`app(config).startup(...).run()`) move to this API: the package is replaced by the `drift` program and the `drift` module.
