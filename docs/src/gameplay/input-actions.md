# Input Actions

Input actions give inputs names. Game code asks whether "jump" was pressed or where "move" points, and the bindings behind those names cover keyboard, mouse and gamepad at once. Players can rebind them at runtime and the bindings save to JSON.

**Header:** `#include <drift/gameplay/InputActions.hpp>`

**Plugin:** `InputActionsPlugin` (included in `DefaultPlugins`; needs `InputPlugin`)

`InputActions` reads the same state as [`InputResource`](../subsystems/input.md); use `InputResource` directly for one-off checks such as a debug key.

## Defining actions and axes

Bind in a startup system. Each call adds a binding; an action can have any number.

```cpp
void bindControls(ResMut<InputActions> actions) {
    actions->bindAction("jump", InputBinding::key(Key::Space));
    actions->bindAction("jump", InputBinding::button(GamepadButton::South));

    // 1D axis: negative side, positive side.
    actions->bindAxis("throttle", InputBinding::key(Key::S), InputBinding::key(Key::W));
    actions->bindAxis("throttle", InputBinding::axis(GamepadAxis::LeftTrigger),
                                  InputBinding::axis(GamepadAxis::RightTrigger));

    // 2D axis: left, right, up, down. Up is -Y, matching screen coordinates.
    actions->bindAxis2D("move", InputBinding::key(Key::A), InputBinding::key(Key::D),
                                InputBinding::key(Key::W), InputBinding::key(Key::S));
    actions->bindStick("move", GamepadAxis::LeftX, GamepadAxis::LeftY);
}
```

A binding is one physical input:

| Constructor | Input |
|-------------|-------|
| `InputBinding::key(Key)` | A keyboard key (any SDL scancode cast to `Key` works) |
| `InputBinding::mouse(MouseButton)` | A mouse button |
| `InputBinding::button(GamepadButton)` | A gamepad button (`South`, `East`, `West`, `North`, shoulders, d-pad, sticks, `Start`, `Back`, `Guide`) |
| `InputBinding::axis(GamepadAxis, positive)` | One half of a stick or trigger axis |

Every binding reads as 0..1, so a trigger can drive a button action and a key can drive an axis.

### In drift.toml

Actions can also be bound in the `[input]` section of [drift.toml](../reference/configuration.md), one key per action with its bindings in their text form (`key:<name>`, `mouse:Left`, `button:South`, and `#<code>` for a numeric code):

```toml
[input]
jump = ["key:Space", "button:South"]
fire = "mouse:Left"
```

`InputActionsPlugin` binds them when it builds. Saving drift.toml while the game runs replaces the bindings of the actions whose line changed. Axes are bound in code.

## Reading them

```cpp
void playerControl(Res<InputActions> actions, QueryMut<Transform2D, With<Player>> q, Res<Time> time) {
    Vec2 move = actions->axis2D("move");          // length <= 1
    q.iter([&](Transform2D& t) { t.position += move * 200.f * time->delta; });
    if (actions->pressed("jump")) { /* ... */ }
}
```

| Query | Returns |
|-------|---------|
| `pressed(name)` | True on the frame the action becomes held, even when it is let go within the same frame |
| `held(name)` | True while held |
| `released(name)` | True on the frame it stops being held |
| `value(name)` | Actions: 0..1. 1D axes: -1..1. 2D axes: the vector's length |
| `axis(name)` | 1D axis value (x for a 2D axis) |
| `axis2D(name)` | 2D axis vector |

Several bindings of one action combine: the action is held while any of them is, and pressing a second binding while the first is down is not a new press. An axis counts as held once its magnitude passes `pressThreshold`, so axes also work for menu navigation. Unknown names read as idle.

Action state follows input events as they arrive, so every `Update` system sees the current frame's input.

## Gamepads and dead zones

| Setting | Default | Meaning |
|---------|---------|---------|
| `deadZone` | 0.2 | Stick values below this read as 0; the rest is rescaled to 0..1 |
| `pressThreshold` | 0.5 | Analog value at which an action or axis counts as held |
| `gamepad` | -1 | Gamepad slot to read; -1 reads any connected gamepad |

2D axes use a radial dead zone on the combined vector, so diagonals are not cut off. Diagonal keyboard input is normalized to length 1.

For local multiplayer, keep one `InputActions` per player in your own resource, set each one's `gamepad`, and drive them the way the plugin does: `setSource(&input)`, `beginFrame()` before events, and register each as an event handler with `app.addEventHandler(...)`.

## Rebinding

```cpp
// "Press a key" menu: the first input that went down this frame.
if (auto b = actions->captureNext(*input)) {
    actions->rebind("jump", 0, 0, *b);   // slot 0, replace the first binding
}
```

- `rebind(name, slot, index, binding)` replaces one binding, or appends when `index` is past the end.
- `setBindings(name, slot, list)` replaces a whole slot.
- `bindings(name, slot)` lists them, for a controls menu. `InputBinding::toString()` gives a readable name such as `key:Space` or `button:South`.

Actions have one slot (0). Axes have one slot per direction: `AxisSlot::Negative` / `Positive` for 1D, `AxisSlot::Left` / `Right` / `Up` / `Down` for 2D.

## Saving bindings

`toJson()` and `fromJson()` round-trip every binding through text, which pairs with [`SaveData`](save-data.md):

```cpp
// After defining the defaults:
if (save->has("bindings")) actions->fromJson(save->json()["bindings"]);

// After the player rebinds:
save->set("bindings", actions->toJson());
save->flush();
```

`fromJson` only updates names that already exist with the same shape, so a stale save cannot add actions your game does not define or turn an action into an axis. Unknown entries are ignored.
