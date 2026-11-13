<!-- Written by scripts/candela-reference.py from the drift module and
     bindings/candela/docs/api-notes.md. Edit those, not this page. -->

# Screens: drift/screens

A title screen, play, pause and game over, with fades and wipes between them. The module adds the `Screen` state and the `Screens` resource that changes it behind a transition.

```rust
import "drift" { * };
import "drift/screens" { * };
```

Mark game systems with `in_state: [Screen::Playing], run_if: [running]` so they stop on other screens and while paused. See [Building Blocks](../guides/building-blocks.md).

## Screen

Which screen the game is on. Title first.

```rust
enum Screen { Title, Playing, GameOver }
```

## TransitionKind

```rust
enum TransitionKind { Fade, Wipe, Cut }
```

## Screens

Changes screens behind a transition: go() covers the window, switches the Screen state once it is covered, then uncovers it. drift.toml [screens] can set `color`, `seconds` (each way) and `kind`.

```rust
@settings(section: "screens")
struct Screens {
    color: Color = Color { r: 0.0, g: 0.0, b: 0.0, a: 1.0 },
    seconds: float = 0.35,
    kind: TransitionKind = TransitionKind::Fade,
    target: Screen = Screen::Title,
    phase: int = 0,
    t: float = 0.0,
}
```

### Screens.go

```rust
fn go(self, to: Screen)
```

To `to` with the default transition (a fade).

### Screens.fade_to

```rust
fn fade_to(self, to: Screen)
```

### Screens.wipe_to

```rust
fn wipe_to(self, to: Screen)
```

### Screens.go_with

```rust
fn go_with(self, to: Screen, kind: TransitionKind)
```

### Screens.busy

```rust
fn busy(self) -> bool
```

Whether a transition is running.

## scr_transition

```rust
@system
fn scr_transition(screens: Screens, state: State<Screen>, time: Time, win: Window, draw: Draw)
```

A system the module adds: it runs in every game that imports the module.

## OnlyIn

Despawns the entity when the game leaves `screen`: menus and anything else that belongs to one screen.

```rust
@component
struct OnlyIn {
    screen: Screen = Screen::Title,
}
```

## scr_only_in

```rust
@system
fn scr_only_in(e: Entity, o: OnlyIn, state: State<Screen>, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## Pause

Whether the game is paused. Escape, P and the gamepad's Start button pause and resume while the Screen is Playing; pausing freezes game time and shows a menu with Resume and Quit. `menu` off pauses without it.

```rust
@resource
struct Pause {
    on: bool = false,
    menu: bool = true,
    allowed: bool = true,
    title: string = "Paused",
}
```

### Pause.set

```rust
fn set(self, on: bool, time: Time)
```

## running

```rust
fn running(p: Pause) -> bool
```

A run_if condition: true unless paused.

## PauseMenu

The pause menu's widgets.

```rust
@component
struct PauseMenu {}
```

## pause_pressed

```rust
fn pause_pressed(input: Input) -> bool
```

## scr_pause_keys

```rust
@system(in_state: [Screen::Playing])
fn scr_pause_keys(pause: Pause, input: Input, time: Time, screens: Screens)
```

A system the module adds: it runs in every game that imports the module.

## scr_pause_menu

```rust
@system(after: [scr_pause_keys])
fn scr_pause_menu(pause: Pause, theme: Theme, menus: (Entity, PauseMenu)[], focus: UiFocus, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

## scr_unpause

```rust
@system(on_exit: Screen::Playing)
fn scr_unpause(pause: Pause, time: Time)
```

A system the module adds: it runs in every game that imports the module.

Leaving Playing ends a pause.

## scr_buttons

```rust
@system
fn scr_buttons(p: ButtonPressed, screens: Screens, pause: Pause, time: Time)
```

A system the module adds: it runs in every game that imports the module.

The buttons the screens put up: "play" and "retry" start a game, "title" goes back to the title, "resume" ends a pause.

## title_screen

```rust
fn title_screen(cmd: Commands, theme: Theme, title: string, subtitle: string)
```

A title, a line under it and a Play button, gone when the title screen ends. Call it from an `on_enter: Screen::Title` system.

## game_over_screen

```rust
fn game_over_screen(cmd: Commands, theme: Theme, title: string, detail: string)
```

A heading, a line of detail (the score) and Play again and Title buttons, gone when the screen ends. Call it from an `on_enter: Screen::GameOver` system.
