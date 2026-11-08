# Attributes

Attributes tell Drift what your structs and functions are. Write them in front of a top-level `struct` or `fn`, behind the module name: `@drift::component`. The candela compiler checks each one when the game compiles: a misspelt attribute, a type in `writes:` that does not exist, or a function in `after:` that is not declared is a compile error with its line, before the game starts.

A struct or function with type parameters cannot carry an attribute, and neither can a method.

## On structs

### `@drift::component`

```rust
@drift::component
struct Health { hp: int = 100, max: int = 100 }
```

The struct is a component: data attached to entities. Drift stores it with the entity, passes it to the systems that query it, shows it in the inspector and saves it in scene files. Fields may be any candela type a struct can hold. See [Components](../concepts/components.md).

### `@drift::resource`

```rust
@drift::resource
struct Score { value: int = 0 }
```

The struct is a resource: one value for the whole game. A resource whose fields all have defaults exists from the start; one without is created when a system inserts it with `cmd.insert_resource`. See [Resources and Settings](../concepts/resources.md).

### `@drift::settings(section: string)`

```rust
@drift::settings(section: "rules")
struct Rules { lives: int = 3, enemy_speed: float = 120.0 }
```

A resource filled from the `[rules]` section of `drift.toml`, then from `DRIFT_RULES_<KEY>` environment variables and `--set rules.key=value`. Field names are the keys. Every field needs a default. Saving `drift.toml` while the game runs updates the changed fields.

### `@drift::message`

```rust
@drift::message
struct Damage { target: drift::Entity, amount: int }
```

The struct is a message: something that happened this frame. Systems send it with `cmd.send(...)` and receive the frame's messages as a list parameter. See [Messages and Collisions](../concepts/messages.md).

### `@drift::query(with: type[] = [], without: type[] = [])`

```rust
@drift::query(without: [Frozen])
struct Mover { entity: drift::Entity, transform: Transform, velocity: Velocity }
```

The struct is a row of a query. Each field is a component, or `drift::Entity` for the entity's id, and a system parameter of type `Mover[]` receives one row per entity that has every field's component, every type in `with:` and none in `without:`. See [Queries](../concepts/queries.md).

## On functions

### `@drift::system`

```rust
@drift::system(writes: [Transform], after: [steer], schedule: drift::Schedule::Update)
fn move_things(movers: Mover[], time: drift::Time) { ... }
```

The function is a system: Drift calls it every frame (or once, for `Startup`) and fills its parameters. A system's parameters say what it reads; see [System Parameters](system-parameters.md) for the kinds.

| Argument | Type | Default | Meaning |
|----------|------|---------|---------|
| `writes` | `type[]` | `[]` | Components and resources the system changes. Changes to anything else it receives are discarded, and dev builds log the first one. |
| `after` | `fn[]` | `[]` | Systems that run before this one in the same schedule. |
| `before` | `fn[]` | `[]` | Systems that run after this one in the same schedule. |
| `schedule` | `drift::Schedule` | `Update` | `Startup` runs once before the first frame; `Update` runs every frame. |
| `enabled` | `bool` | `true` | `false` keeps the system registered but not run; toggle it with `drift::Systems`. |

Systems with no ordering between them run in the order they appear in the source.
