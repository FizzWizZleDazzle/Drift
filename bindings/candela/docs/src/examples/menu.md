# Menus and States

A title screen, an options screen, a small dodging game and a pause screen, each one a state.

[Play it in the browser](../play/menu/). Up and Down pick a menu item, Enter or Space chooses it, Left and Right change an option, and Escape goes back. In the game, dodge the falling blocks with Left and Right; Escape pauses.

## What it shows

- A `@state` enum with `on_enter` and `on_exit` systems that build and clear each screen
- `in_state` to run systems only on some screens, and a pause that freezes the game by not running its systems
- Option defaults from `[menu]` in `drift.toml`, overridden by choices kept in save data
- Menus made of `Text` entities with a highlight system
- A sound played at the volume the player picked

## Run it

```sh
cd bindings/candela/examples/menu
drift build --web
cd build/web && python3 -m http.server 8000
```

The example is self-contained: every image and sound is made in code.

## drift.toml

```toml
{{#include ../../../examples/menu/drift.toml}}
```

## The title screen

Entering the state builds the screen; leaving it clears every entity tagged `OnScreen`.

```rust
{{#include ../../../examples/menu/main.cdl:title}}
```

## Pausing

Escape moves to `Paused`. The game's systems carry `in_state: [Screen::Playing]`, so they stop, and the blocks move in a system rather than by `Velocity` so they stop with them. Coming back to `Playing` runs its `on_enter` again, which leaves a game in progress alone.

```rust
{{#include ../../../examples/menu/main.cdl:pause}}
```

## The whole game

```rust
{{#include ../../../examples/menu/main.cdl}}
```
