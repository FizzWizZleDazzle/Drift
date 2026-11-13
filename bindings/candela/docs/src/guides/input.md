# Input

`Input` gives a system the keyboard, mouse and gamepads, and input actions: names such as "jump" or "move" that any mix of keys, buttons and sticks can drive. Write game logic against actions, and players can rebind them.

## Devices

Take `Input` as a parameter and ask about this frame:

```rust
@system
fn controls(input: Input, window: Window) {
    if input.key_pressed(Key::Escape) {
        window.quit();
    }
    if input.mouse_pressed(MouseButton::Left) {
        log("click at " + string(input.mouse_world().x));
    }
}
```

- `key_down` is true while a key is held; `key_pressed` and `key_released` are true only in the frame it changed. A tap that goes down and up between two frames reads as both pressed and released in the next frame, so quick taps are never lost. Mouse buttons, gamepad buttons and actions work the same way.
- `mouse_wheel()` is the wheel movement this frame, positive when the wheel turns away from the player.
- `mouse_position()` is in logical screen coordinates, letterbox bars accounted for. `mouse_world()` is the same point through the main camera.
- Gamepads use slots 0 to 3 and the Xbox layout: `GamepadButton::South` is A. Sticks read -1 to 1 with Y down; triggers read 0 to 1.

## Actions

Bind single-button actions in the `[input]` section of `drift.toml`, one key per action with its bindings in text form (`key:<name>`, `mouse:Left`, `button:South`; [Settings](../concepts/settings.md#input-bindings) lists the names):

```toml
[input]
jump = ["key:Space", "button:South"]
```

Axes are bound in code, once, in a startup system; `bind_key`, `bind_mouse`, `bind_button` and `bind` add action bindings from code too:

```rust
@system(schedule: Schedule::Startup)
fn bind_controls(input: Input) {
    input.bind_axis2d("move", Key::A, Key::D, Key::W, Key::S);
    input.bind_axis2d("move", Key::Left, Key::Right, Key::Up, Key::Down);
    input.bind_stick("move", GamepadAxis::LeftX, GamepadAxis::LeftY);
}

@system
fn walk(_: Player, v: Velocity, input: Input) {
    v.linear = vec2(input.axis2d("move").x * 200.0, v.linear.y);
    if input.action_pressed("jump") {
        v.linear = vec2(v.linear.x, -400.0);
    }
}
```

An action also works as a [run condition](../concepts/systems.md#running-some-of-the-time): `fn jumped(input: Input) -> bool => input.action_pressed("jump");` and `@system(run_if: [jumped])`.

Bindings of one action combine: the action is held while any of them is, and pressing a second binding while the first is down is not a new press. `action_value` reads an analog binding from 0 to 1.

Axes come in two kinds. `bind_axis(name, negative, positive)` makes a 1D axis from -1 to 1, read with `axis(name)`. `bind_axis2d` and `bind_stick` make a 2D axis, read with `axis2d(name)`; up is -Y, and diagonal keyboard input has length 1. An axis also counts as held once it passes the press threshold, so menus can navigate with sticks.

`set_action_tuning(dead_zone, press_threshold, pad)` sets the stick dead zone, the press threshold, and which gamepad actions read (-1 reads any connected pad). Names nothing binds read as idle.

## Rebinding menus

A controls menu shows the current bindings, waits for the player to press something, and stores it:

```rust
@resource
struct Menu { waiting_for: string = "" }

@system
fn rebind_jump(input: Input, menu: Menu, save: Save) {
    if menu.waiting_for == "" {
        return;
    }
    let pressed = input.capture_binding();
    if pressed != "" {
        input.rebind(menu.waiting_for, 0, 0, pressed);
        save.set_string("bindings", input.bindings_json());
        save.flush();
        menu.waiting_for = "";
    }
}
```

- `capture_binding()` returns the first input that went down this frame as text, such as `"key:Space"`, or `""`.
- `bindings(name, slot)` lists a slot's bindings as text, for showing in the menu.
- `rebind(name, slot, index, text)` replaces one binding, or adds one when `index` is past the end.
- Actions have slot 0. A 1D axis has slots 0 and 1 for its negative and positive sides; a 2D axis has 0 to 3 for left, right, up and down.
- `bindings_json()` and `load_bindings_json(text)` save and restore every binding. Load the saved ones after binding the defaults in startup.

## Typed text

Key codes say which key went down; for names, chat and passwords you want the text, with the keyboard layout applied. Turn text input on while a text field has focus and read `text()` every frame:

```rust
@resource
struct NameEntry { name: string = "" }

@system(schedule: Schedule::Startup)
fn start_typing(input: Input) {
    input.start_text_input();
}

@system
fn type_name(input: Input, entry: NameEntry) {
    entry.name = entry.name + input.text();
    if input.key_pressed(Key::Backspace) && entry.name.len() > 0 {
        entry.name = entry.name[..entry.name.len() - 1];
    }
    if input.key_pressed(Key::Return) {
        input.stop_text_input();
    }
}
```

On phones, starting text input brings up the on-screen keyboard. Keys still arrive as usual while it is on, so a game that also reads letter keys should ignore them while the player types.

## Touch

The first finger on a touch screen also moves and clicks the mouse, so a game that reads the mouse works on phones as it is. For several fingers, read `touches()`:

```rust
@system
fn twin_sticks(input: Input, window: Window) {
    for t in input.touches() {
        if t.position.x < window.width / 2.0 {
            let stick = t.position - t.start;   // drag from where the thumb landed
            log("move " + string(stick.x));
        } else if t.pressed {
            log("fire");
        }
    }
}
```

Each `Touch` has an `id` that stays the same while the finger is down, its `position`, `start` (where it went down) and `delta` (movement this frame) in logical screen coordinates, and `pressed` and `released` for the frame it went down or up.

## Input for tests and replays

`send_key`, `send_mouse_move` and `send_mouse_button` queue input as if it came from the device. It arrives at the start of the next frame and goes through actions like real input, so a test system or a demo replay can drive the game:

```rust
@system
fn autoplay(input: Input, time: Time) {
    if time.frame == 60 {
        input.send_key(Key::Space, true);
    }
    if time.frame == 62 {
        input.send_key(Key::Space, false);
    }
}
```

See [`Input`](../reference/resources.md#input) for every method and [Enums](../reference/enums.md#key) for the key and button names. The [Snake](../examples/snake.md) and [Roguelike](../examples/roguelike.md) examples read actions from `drift.toml`; the [Top-Down Shooter](../examples/shooter.md) aims with the mouse.
