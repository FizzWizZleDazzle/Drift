# Input

`drift::Input` gives a system the keyboard, mouse and gamepads, and input actions: names such as "jump" or "move" that any mix of keys, buttons and sticks can drive. Write game logic against actions, and players can rebind them.

## Devices

Take `Input` as a parameter and ask about this frame:

```rust
@drift::system
fn controls(input: Input, window: drift::Window) {
    if input.key_pressed(Key::Escape) {
        window.quit();
    }
    if input.mouse_pressed(MouseButton::Left) {
        log("click at " + string(input.mouse_world().x));
    }
}
```

- `key_down` is true while a key is held; `key_pressed` and `key_released` are true only in the frame it changed.
- `mouse_position()` is in logical screen coordinates, letterbox bars accounted for. `mouse_world()` is the same point through the main camera.
- Gamepads use slots 0 to 3 and the Xbox layout: `GamepadButton::South` is A. Sticks read -1 to 1 with Y down; triggers read 0 to 1.

## Actions

Bind actions once, in a startup system, and read them every frame:

```rust
@drift::system(schedule: drift::Schedule::Startup)
fn bind_controls(input: Input) {
    input.bind_key("jump", Key::Space);
    input.bind_button("jump", GamepadButton::South);
    input.bind_axis2d("move", Key::A, Key::D, Key::W, Key::S);
    input.bind_axis2d("move", Key::Left, Key::Right, Key::Up, Key::Down);
    input.bind_stick("move", GamepadAxis::LeftX, GamepadAxis::LeftY);
}

@drift::system(writes: [Velocity])
fn walk(input: Input, players: (Player, Velocity)[]) {
    let dir = input.axis2d("move");
    for row in players {
        row.1.linear = vec2(dir.x * 200.0, row.1.linear.y);
        if input.action_pressed("jump") {
            row.1.linear = vec2(row.1.linear.x, -400.0);
        }
    }
}
```

Bindings of one action combine: the action is held while any of them is, and pressing a second binding while the first is down is not a new press. `action_value` reads an analog binding from 0 to 1.

Axes come in two kinds. `bind_axis(name, negative, positive)` makes a 1D axis from -1 to 1, read with `axis(name)`. `bind_axis2d` and `bind_stick` make a 2D axis, read with `axis2d(name)`; up is -Y, and diagonal keyboard input has length 1. An axis also counts as held once it passes the press threshold, so menus can navigate with sticks.

`set_action_tuning(dead_zone, press_threshold, pad)` sets the stick dead zone, the press threshold, and which gamepad actions read (-1 reads any connected pad). Names nothing binds read as idle.

## Rebinding menus

A controls menu shows the current bindings, waits for the player to press something, and stores it:

```rust
@drift::resource
struct Menu { waiting_for: string = "" }

@drift::system(writes: [Menu])
fn rebind_jump(input: Input, menu: Menu, save: drift::Save) {
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

## Input for tests and replays

`send_key`, `send_mouse_move` and `send_mouse_button` queue input as if it came from the device. It arrives at the start of the next frame and goes through actions like real input, so a test system or a demo replay can drive the game:

```rust
@drift::system
fn autoplay(input: Input, time: Time) {
    if time.frame == 60 {
        input.send_key(Key::Space, true);
    }
    if time.frame == 62 {
        input.send_key(Key::Space, false);
    }
}
```

See [Engine Resources](../reference/resources.md#driftinput) for every method and [Values and Enums](../reference/values.md) for the key and button names.
