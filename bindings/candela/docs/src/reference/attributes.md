# Attributes

Attributes tell Drift what your structs, enums and functions are. Write them in front of a top-level declaration: `@component`. They come with `import "drift" { * };`; a game that imports the module by name writes them behind it, `@drift::component`. The candela compiler checks each one when the game compiles: a misspelt attribute, a type in `requires:` that does not exist, or a function in `after:` that is not declared is a compile error with its line, before the game starts.

A struct or function with type parameters cannot carry an attribute, and neither can a method.

## On structs

### `@component(requires: type[] = [])`

```rust
@component(requires: [Velocity])
struct Bird { vy: float = 0.0 }
```

The struct is a component: data attached to entities. Drift stores it with the entity, passes it to the systems that query it, shows it in the inspector and saves it in scene files. Fields may be any candela type a struct can hold. `requires:` lists components added from their defaults to an entity that gets this one without them. See [Components](../concepts/components.md).

### `@resource`

```rust
@resource
struct Score { value: int = 0 }
```

The struct is a resource: one value for the whole game. A resource whose fields all have defaults exists from the start; one without is created when a system inserts it with `cmd.insert_resource`. See [Resources and Settings](../concepts/resources.md).

### `@settings(section: string)`

```rust
@settings(section: "rules")
struct Rules { lives: int = 3, enemy_speed: float = 120.0 }
```

A resource filled from the `[rules]` section of `drift.toml`, then from `DRIFT_RULES_<KEY>` environment variables and `--set rules.key=value`. Field names are the keys. Every field needs a default. Saving `drift.toml` while the game runs updates the changed fields.

### `@message`

```rust
@message
struct Damage { target: Entity, amount: int }
```

The struct is a message: something that happened this frame. Systems send it with `cmd.send(...)` and receive it as a list parameter, or one at a time as a single parameter. See [Messages and Collisions](../concepts/messages.md).

### `@query(with: type[] = [], without: type[] = [])`

```rust
@query(without: [Frozen])
struct Mover { entity: Entity, transform: Transform, velocity: Velocity }
```

The struct is a row of a query. Each field is a component, or `Entity` for the entity's id, and a system parameter of type `Mover[]` receives one row per entity that has every field's component, every type in `with:` and none in `without:`. See [Queries](../concepts/queries.md).

## On enums

### `@state`

```rust
@state
enum Phase { Ready, Playing, Over }
```

The enum is a game state. Systems read and change it through `State<Phase>` and run in some of its variants with `in_state:`, `on_enter:` and `on_exit:`. The first variant is the starting state. See [States](../concepts/states.md).

## On functions

### `@system`

```rust
@system(in_state: [Phase::Playing], run_if: [flapped], after: [steer])
fn move_things(movers: Mover[], time: Time) { ... }
```

The function is a system: Drift calls it every frame (or once, for `Startup`) and fills its parameters, once per matching entity or event when a parameter is a single component or event. Drift keeps every change it makes. See [System Parameters](system-parameters.md) for the kinds of parameter.

| Argument | Type | Default | Meaning |
|----------|------|---------|---------|
| `run_if` | `fn[]` | `[]` | Conditions: functions returning `bool`, over resources, `Input`, `Time` and queries. The system runs in frames where all of them hold. |
| `in_state` | variants | `[]` | Runs only while a `@state` enum is one of these variants. |
| `on_enter` | variant | none | Runs once when the state becomes this variant, and not otherwise. |
| `on_exit` | variant | none | Runs once when the state stops being this variant, and not otherwise. |
| `after` | `fn[]` | `[]` | Systems that run before this one in the same schedule. |
| `before` | `fn[]` | `[]` | Systems that run after this one in the same schedule. |
| `schedule` | `Schedule` | `Update` | `Startup` runs once before the first frame; `Update` runs every frame. |
| `enabled` | `bool` | `true` | `false` keeps the system registered but not run; toggle it with `Systems`. |

Systems with no ordering between them run in the order they appear in the source.
