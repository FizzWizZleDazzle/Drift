<!-- Written by scripts/candela-reference.py from the drift module and
     bindings/candela/docs/api-notes.md. Edit those, not this page. -->

# Attributes

Attributes tell Drift what your structs, enums and functions are. Write one in front of a top-level declaration. They come with `import "drift" { * };`; a game that imports the module by name writes `@drift::component` instead. The compiler checks each attribute and its arguments, so a misspelt attribute, an unknown type in `requires:` or an unknown function in `after:` is an error with its line, before the game starts.

A struct or function with type parameters cannot carry an attribute, and neither can a method.

## @component

```rust
@component(requires: type[] = [])
```

On a `struct`.

The struct is a component: data attached to entities. Drift stores it with the entity, passes it to the systems that ask for it, and shows it in the inspector. `requires:` lists components added, with their defaults, to an entity that gets this one without them. See [Components](../concepts/components.md).

```rust
@component(requires: [Velocity])
struct Bird { flap: float = 420.0 }

@component
struct Enemy {}          // a tag: no data, it only marks entities
```

## @resource

```rust
@resource
```

On a `struct`.

The struct is a resource: one value for the whole game. A resource whose fields all have defaults exists from the start; one with a required field exists once a system inserts it with `cmd.insert_resource`. A system that takes a resource that does not exist is skipped. See [Resources](../concepts/resources.md).

```rust
@resource
struct Score { value: int = 0, best: int = 0 }
```

## @settings

```rust
@settings(section: string)
```

On a `struct`.

A resource filled from one section of `drift.toml`. Field names are the keys, and every field needs a default for when the file leaves the key out. See [Settings](../concepts/settings.md).

```rust
@settings(section: "rules")
struct Rules { lives: int = 3, enemy_speed: float = 120.0 }
```

```toml
[rules]
lives = 5
```

## @message

```rust
@message
```

On a `struct`.

The struct is a message: something that happened this frame. Systems send it with `cmd.send(...)` and read it as a parameter. See [Events](../concepts/events.md).

```rust
@message
struct Damage { target: Entity, amount: int }
```

## @state

```rust
@state
```

On a `enum`.

The enum is a game state. Its first variant is the starting state. Systems read and change it through `State<Phase>` and run in some variants with `in_state:`, `on_enter:` and `on_exit:`. See [States](../concepts/states.md).

```rust
@state
enum Phase { Title, Playing, GameOver }
```

## @query

```rust
@query(with: type[] = [], without: type[] = [])
```

On a `struct`.

The struct is one row of a query. Each field is a component, or `Entity` for the entity's id. A parameter of type `Mover[]` receives one row per entity that has every field's component, every type in `with:` and none in `without:`. See [Queries](../concepts/queries.md).

```rust
@query(with: [Enemy], without: [Frozen])
struct Mover { entity: Entity, transform: Transform, velocity: Velocity }

@system
fn slow_down(movers: Mover[]) {
    for m in movers {
        m.velocity.linear = m.velocity.linear * 0.9;
    }
}
```

## @system

```rust
@system(after: fn[] = [], before: fn[] = [], run_if: fn[] = [], in_state: enum[] = [], on_enter: enum?, on_exit: enum?, schedule: Schedule = Schedule::Update, enabled: bool = true)
```

On a `fn`.

The function is a system. Drift calls it every frame (once, for `Schedule::Startup`) and fills its parameters with the data their types name. See [Systems](../concepts/systems.md) and [System Parameters](system-parameters.md).

| Argument | Meaning |
|----------|---------|
| `after: [a, b]` | Runs after these systems in the same frame |
| `before: [c]` | Runs before these systems |
| `run_if: [f]` | Runs only in frames where every function returns `true` |
| `in_state: [Phase::Playing]` | Runs only while a `@state` enum is one of these variants |
| `on_enter: Phase::Over` | Runs once when the state becomes this variant, and not otherwise |
| `on_exit: Phase::Over` | Runs once when the state stops being this variant, and not otherwise |
| `schedule: Schedule::Startup` | Runs once before the first frame instead of every frame |
| `enabled: false` | Registers the system without running it |

Systems with no order between them run in the order they appear in the source.

```rust
fn jumped(input: Input) -> bool => input.action_pressed("jump");

@system(in_state: [Phase::Playing], run_if: [jumped], after: [land])
fn jump(player: Player, v: Velocity) {
    v.linear = vec2(v.linear.x, -player.jump_speed);
}
```
