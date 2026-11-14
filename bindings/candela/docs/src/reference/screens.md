<!-- Written by scripts/candela-reference.py from the doc comments in
     bindings/candela/drift/src. Edit those, not this page. -->

# Screens: drift/screens

A title screen, play, pause and game over, with fades and wipes between them. The module
adds the `Screen` state and the `Screens` resource that changes it behind a transition.

```rust
import "drift" { * };
import "drift/screens" { * };
```

Mark game systems with `in_state: [Screen::Playing], run_if: [running]` so they stop on
other screens and while paused. See [Building Blocks](../guides/building-blocks.md).

## Screen

Which screen the game is on: a `@state` enum that starts on Title.

Change it with `Screens.go` for a transition, or `State<Screen>.set` for a cut.

```rust
@system(in_state: [Screen::Playing], run_if: [running])
fn move_enemies(enemy: (Enemy, Transform), time: Time) { }
```

```rust
enum Screen { Title, Playing, GameOver }
```

| Variant | Meaning |
|---------|---------|
| `Title` | The title screen, where the game starts. |
| `Playing` | The game itself; the only screen Escape and P pause on. |
| `GameOver` | After the run ends. |

## TransitionKind

How `Screens` covers the window while it changes screen.

```rust
screens.go_with(Screen::GameOver, TransitionKind::Wipe);
```

```rust
enum TransitionKind { Fade, Wipe, Cut }
```

| Variant | Meaning |
|---------|---------|
| `Fade` | The window fades to the color and back. |
| `Wipe` | The color sweeps in from the left, then off to the right. |
| `Cut` | The screen changes at once, with no cover. |

## Screens

Changes screens behind a transition: go() covers the window, switches the Screen state once
it is covered, then uncovers it. drift.toml [screens] can set `color`, `seconds` (each way)
and `kind`.

The transition runs in real time and draws over everything, the HUD included.

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

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `color` | `Color` | `Color { r: 0.0, g: 0.0, b: 0.0, a: 1.0 }` | The color that covers the window. |
| `seconds` | `float` | `0.35` | Seconds to cover the window, and again to uncover it. |
| `kind` | `TransitionKind` | `TransitionKind::Fade` | The transition go() uses; go_with sets it to the one it was given. |
| `target` | `Screen` | `Screen::Title` | The screen the running transition goes to. |
| `phase` | `int` | `0` | 0 idle, 1 covering, 2 uncovering. |
| `t` | `float` | `0.0` | How much of the window is covered, 0 to 1. |

```toml
[screens]
seconds = 0.5
```

### Screens.go

```rust
fn go(self, to: Screen)
```

Goes to `to` with the transition in `kind`, a fade unless drift.toml or an earlier
go_with set another.

```rust
screens.go(Screen::Playing);
```

### Screens.fade_to

```rust
fn fade_to(self, to: Screen)
```

Goes to `to` behind a fade.

```rust
screens.fade_to(Screen::GameOver);
```

### Screens.wipe_to

```rust
fn wipe_to(self, to: Screen)
```

Goes to `to` behind a wipe.

```rust
screens.wipe_to(Screen::Title);
```

### Screens.go_with

```rust
fn go_with(self, to: Screen, kind: TransitionKind)
```

Goes to `to` behind the transition `kind`.

Called while the window is still being covered, it only changes where the transition
goes; the transition already running keeps its kind.

```rust
screens.go_with(Screen::Playing, TransitionKind::Cut);
```

### Screens.busy

```rust
fn busy(self) -> bool
```

Whether a transition is running.

```rust
if !screens.busy() && input.key_pressed(Key::Enter) { screens.go(Screen::Playing); }
```

## scr_transition

```rust
@system
fn scr_transition(screens: Screens, state: State<Screen>, time: Time, win: Window, draw: Draw)
```

A system the module adds: it runs in every game that imports the module.

Runs the transition: covers the window, switches the Screen state once it is covered, then
uncovers it.

## OnlyIn

Despawns the entity when the game leaves `screen`: menus and anything else that belongs to
one screen.

The check runs every frame, so an entity spawned with another screen's OnlyIn is despawned
at once.

```rust
@component
struct OnlyIn {
    screen: Screen = Screen::Title,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `screen` | `Screen` | `Screen::Title` | The screen the entity belongs to. |

```rust
cmd.spawn(plus(label(theme, "Ready?", anchor(CENTER, 0.0, 0.0)), [OnlyIn { screen: Screen::Playing }]));
```

## scr_only_in

```rust
@system
fn scr_only_in(only_in: (Entity, OnlyIn), state: State<Screen>, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Despawns every OnlyIn entity whose screen is not the current one.

## Pause

Whether the game is paused. Escape, P and the gamepad's Start button pause and resume while
the Screen is Playing; pausing freezes game time and shows a menu with Resume and Quit.
`menu` off pauses without it.

Pausing sets the time scale to 0 and resuming sets it to 1, so a resume ends any slow
motion the game set. Keys do nothing during a screen transition, and leaving Playing ends a
pause.

```rust
@resource
struct Pause {
    on: bool = false,
    menu: bool = true,
    allowed: bool = true,
    title: string = "Paused",
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `on` | `bool` | `false` | Whether the game is paused. |
| `menu` | `bool` | `true` | Shows the pause menu while paused; off pauses with nothing on screen. |
| `allowed` | `bool` | `true` | Lets the pause keys pause and resume; off ignores them. |
| `title` | `string` | `"Paused"` | The heading of the pause menu. |

```rust
@system
fn no_pause_in_cutscene(pause: Pause, cutscene: Cutscene) {
    pause.allowed = !cutscene.playing;
}
```

### Pause.set

```rust
fn set(self, on: bool, time: Time)
```

Pauses or resumes: sets `on` and the time scale, 0 while paused and 1 after.

```rust
pause.set(true, time);
```

## running

```rust
fn running(p: Pause) -> bool
```

A run_if condition: true unless paused.

```rust
@system(in_state: [Screen::Playing], run_if: [running])
fn spawn_enemies(cmd: Commands) { }
```

## PauseMenu

Marks the pause menu's widgets, which the module spawns and despawns.

```rust
@component
struct PauseMenu {}
```

## pause_pressed

```rust
fn pause_pressed(input: Input) -> bool
```

Whether a pause key was pressed this frame: Escape, P or the first gamepad's Start.

## scr_pause_keys

```rust
@system(in_state: [Screen::Playing])
fn scr_pause_keys(pause: Pause, input: Input, time: Time, screens: Screens)
```

A system the module adds: it runs in every game that imports the module.

Pauses and resumes on the pause keys while the Screen is Playing.

## scr_pause_menu

```rust
@system(after: [scr_pause_keys])
fn scr_pause_menu(pause: Pause, theme: Theme, menus: (Entity, PauseMenu)[], focus: UiFocus, cmd: Commands)
```

A system the module adds: it runs in every game that imports the module.

Shows the pause menu, a heading with Resume and Quit buttons, while paused with `menu` on,
and removes it otherwise.

## scr_unpause

```rust
@system(on_exit: Screen::Playing)
fn scr_unpause(pause: Pause, time: Time)
```

A system the module adds: it runs in every game that imports the module.

Ends a pause when the game leaves the Playing screen.

## scr_buttons

```rust
@system
fn scr_buttons(p: ButtonPressed, screens: Screens, pause: Pause, time: Time)
```

A system the module adds: it runs in every game that imports the module.

Acts on the buttons the screens put up: "play" and "retry" start a game, "title" goes back to
the title, "resume" ends a pause.

A button of your own with one of these ids does the same.

## title_screen

```rust
fn title_screen(cmd: Commands, theme: Theme, title: string, subtitle: string)
```

A title, a line under it and a Play button, gone when the title screen ends. Call it from an
`on_enter: Screen::Title` system.

```rust
@system(on_enter: Screen::Title)
fn show_title(cmd: Commands, theme: Theme) {
    title_screen(cmd, theme, "Star Catcher", "Catch the falling stars");
}
```

## game_over_screen

```rust
fn game_over_screen(cmd: Commands, theme: Theme, title: string, detail: string)
```

A heading, a line of detail (the score) and Play again and Title buttons, gone when the
screen ends. Call it from an `on_enter: Screen::GameOver` system.

```rust
@system(on_enter: Screen::GameOver)
fn show_game_over(cmd: Commands, theme: Theme, game: Game) {
    game_over_screen(cmd, theme, "Game Over", format("Score {}", [game.score]));
}
```
