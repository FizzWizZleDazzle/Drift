<!-- Written by scripts/candela-reference.py from the drift module and
     bindings/candela/docs/api-notes.md. Edit those, not this page. -->

# Inventory, Stats, Quests and Dialogue: drift/rpg

Inventories, stats with modifiers, story flags, quests, and dialogue played from a script, as plain values the game keeps and saves.

```rust
import "drift" { * };
import "drift/rpg" { * };
```

See [Inventory, Stats, Quests and Dialogue](../guides/game-systems.md).

## ItemStack

```rust
struct ItemStack {
    item: string = "",
    count: int = 0,
}
```

## Inventory

Items by id, in slots of up to `stack_size` each.

```rust
struct Inventory {
    slots: ItemStack[] = [],
    capacity: int = 20,
    stack_size: int = 99,
}
```

### Inventory.add

```rust
fn add(self, item: string, count: int) -> int
```

Adds what fits; returns how many did not.

### Inventory.remove

```rust
fn remove(self, item: string, count: int) -> bool
```

Takes `count` of the item, or nothing when there are fewer.

### Inventory.count

```rust
fn count(self, item: string) -> int
```

### Inventory.has

```rust
fn has(self, item: string, count: int) -> bool
```

### Inventory.is_full

```rust
fn is_full(self) -> bool
```

### Inventory.items

```rust
fn items(self) -> string[]
```

Each item id once, in slot order.

## inventory

```rust
fn inventory(capacity: int, stack_size: int) -> Inventory
```

## ModKind

Add: plus amount. Percent: plus amount times the base (0.1 is +10%), summed. Multiply: times amount, after the others.

```rust
enum ModKind { Add, Percent, Multiply }
```

## Modifier

A change to one stat. `source` names what gave it (an item, a buff) so it can be taken off again; `seconds` above zero makes it wear off.

```rust
struct Modifier {
    stat: string = "",
    kind: ModKind = ModKind::Add,
    amount: float = 0.0,
    source: string = "",
    seconds: float = -1.0,
}
```

## modifier

```rust
fn modifier(stat: string, kind: ModKind, amount: float, source: string) -> Modifier
```

## Stats

Base values by name, and the modifiers on them.

```rust
struct Stats {
    base: {string: float} = {},
    mods: Modifier[] = [],
}
```

### Stats.set_base

```rust
fn set_base(self, stat: string, value: float)
```

### Stats.base_of

```rust
fn base_of(self, stat: string) -> float
```

### Stats.add

```rust
fn add(self, m: Modifier)
```

### Stats.remove_source

```rust
fn remove_source(self, source: string)
```

Takes off every modifier from `source`.

### Stats.get

```rust
fn get(self, stat: string) -> float
```

The stat with every modifier applied.

### Stats.tick

```rust
fn tick(self, dt: float)
```

Wears timed modifiers down; drops the ones that ran out.

## stats

```rust
fn stats() -> Stats
```

## Flags

Named whole numbers for story state: switches (0 or 1), counters, choices. Missing names read 0. Store it with save.set_value to keep it.

```rust
struct Flags {
    values: {string: int} = {},
}
```

### Flags.get

```rust
fn get(self, name: string) -> int
```

### Flags.set

```rust
fn set(self, name: string, value: int)
```

### Flags.on

```rust
fn on(self, name: string) -> bool
```

### Flags.add

```rust
fn add(self, name: string, by: int) -> int
```

Adds to a counter; returns the new value.

### Flags.check

```rust
fn check(self, condition: string) -> bool
```

Whether a condition holds: "key", "!key", or "key OP number" with OP one of == != &lt; &lt;= > >=. An empty condition holds.

### Flags.apply

```rust
fn apply(self, effect: string)
```

Applies an effect: "key" (sets 1), "key = 3", "key += 2", "key -= 1".

## flags

```rust
fn flags() -> Flags
```

## QuestState

```rust
enum QuestState { Inactive, Active, Done, Failed }
```

## Quests

Quests by id: their state and objective counters. Store it with save.set_value to keep it.

```rust
struct Quests {
    states: {string: QuestState} = {},
    counters: {string: int} = {},
}
```

### Quests.state

```rust
fn state(self, quest: string) -> QuestState
```

### Quests.start

```rust
fn start(self, quest: string)
```

### Quests.complete

```rust
fn complete(self, quest: string)
```

### Quests.fail

```rust
fn fail(self, quest: string)
```

### Quests.is_active

```rust
fn is_active(self, quest: string) -> bool
```

### Quests.is_done

```rust
fn is_done(self, quest: string) -> bool
```

### Quests.progress

```rust
fn progress(self, quest: string, objective: string, by: int) -> int
```

Counts toward an objective of an active quest ("wolves" of "hunt"); returns the count.

### Quests.count

```rust
fn count(self, quest: string, objective: string) -> int
```

### Quests.with_state

```rust
fn with_state(self, s: QuestState) -> string[]
```

The ids of every quest in a state.

## quests

```rust
fn quests() -> Quests
```

## DialogueLine

```rust
struct DialogueLine {
    who: string = "",
    text: string = "",
}
```

## DialogueChoice

`when` is a Flags condition that must hold for the choice to show; `set` are Flags effects applied when it is picked; `next` is the node it leads to ("" ends the dialogue).

```rust
struct DialogueChoice {
    text: string = "",
    next: string = "",
    when: string = "",
    set: string[] = [],
}
```

## DialogueNode

Lines said in order, then the choices, or straight on to `next`. `set` effects apply on entering the node.

```rust
struct DialogueNode {
    lines: DialogueLine[] = [],
    choices: DialogueChoice[] = [],
    next: string = "",
    set: string[] = [],
}
```

## DialogueScript

A whole conversation, usually from a data file: load_data&lt;DialogueScript>("assets/talk/ana.json").

```rust
struct DialogueScript {
    start: string = "",
    nodes: {string: DialogueNode} = {},
}
```

## Dialogue

A conversation being played. Variables live in `vars`; {name} in a line shows vars or `names` (for text such as the player's name).

```rust
struct Dialogue {
    script: DialogueScript,
    node: string = "",
    line: int = 0,
    vars: Flags,
    names: {string: string} = {},
}
```

### Dialogue.start

```rust
fn start(self, node: string)
```

Starts at a node; "" starts at the script's `start`.

### Dialogue.enter

```rust
fn enter(self, node: string)
```

### Dialogue.active

```rust
fn active(self) -> bool
```

### Dialogue.current

```rust
fn current(self) -> DialogueLine
```

### Dialogue.speaker

```rust
fn speaker(self) -> string
```

### Dialogue.text

```rust
fn text(self) -> string
```

The line with its {name} parts filled in.

### Dialogue.substitute

```rust
fn substitute(self, text: string) -> string
```

### Dialogue.choices

```rust
fn choices(self) -> DialogueChoice[]
```

The choices to show now: on the node's last line, those whose `when` holds. Empty while lines remain or when the node has none.

### Dialogue.advance

```rust
fn advance(self)
```

On to the next line, or the next node when the lines are done and there is nothing to choose.

### Dialogue.choose

```rust
fn choose(self, i: int)
```

Picks choice i of choices().

## dialogue

```rust
fn dialogue(script: DialogueScript, vars: Flags) -> Dialogue
```
