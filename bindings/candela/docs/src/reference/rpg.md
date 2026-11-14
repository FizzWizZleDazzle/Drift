<!-- Written by scripts/candela-reference.py from the doc comments in
     bindings/candela/drift/src. Edit those, not this page. -->

# Inventory, Stats, Quests and Dialogue: drift/rpg

Inventories, stats with modifiers, story flags, quests, and dialogue played from a script,
as plain values the game keeps and saves.

```rust
import "drift" { * };
import "drift/rpg" { * };
```

See [Inventory, Stats, Quests and Dialogue](../guides/game-systems.md).

## ItemStack

One slot of an `Inventory`: an item id and how many of it the slot holds.

```rust
struct ItemStack {
    item: string = "",
    count: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `item` | `string` | `""` | The item's id, a name of your own such as "potion". |
| `count` | `int` | `0` | How many the slot holds, from 1 to the inventory's `stack_size`. |

## Inventory

Items by id, in slots of up to `stack_size` each.

Keep it in a component or resource, and store it with `save.set_value` to keep it between
runs.

```rust
struct Inventory {
    slots: ItemStack[] = [],
    capacity: int = 20,
    stack_size: int = 99,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `slots` | `ItemStack[]` | `[]` | The slots in use, in the order they were filled. A slot that empties is removed. |
| `capacity` | `int` | `20` | The most slots it holds. |
| `stack_size` | `int` | `99` | The most of one item a slot holds. |

```rust
let bag = inventory(12, 10);
let left = bag.add("arrow", 25);   // 0: three slots of 10, 10 and 5
```

### Inventory.add

```rust
fn add(self, item: string, count: int) -> int
```

Adds what fits; returns how many did not. It tops up slots that already hold the item,
then fills new slots while there is room.

### Inventory.remove

```rust
fn remove(self, item: string, count: int) -> bool
```

Takes `count` of the item, or nothing when there are fewer; returns whether it took them.
It takes from the last slots first.

### Inventory.count

```rust
fn count(self, item: string) -> int
```

How many of the item it holds, over every slot.

### Inventory.has

```rust
fn has(self, item: string, count: int) -> bool
```

Whether it holds at least `count` of the item.

### Inventory.is_full

```rust
fn is_full(self) -> bool
```

Whether every slot is in use. A full inventory can still take more of an item whose
slots have room.

### Inventory.items

```rust
fn items(self) -> string[]
```

Each item id once, in slot order.

## inventory

```rust
fn inventory(capacity: int, stack_size: int) -> Inventory
```

An empty inventory of `capacity` slots holding up to `stack_size` each.

## ModKind

How a `Modifier` changes a stat. A stat reads (base + every Add + base * every Percent),
times every Multiply.

```rust
stats.add(modifier("speed", ModKind::Percent, 0.2, "boots"));   // +20% of base speed
```

```rust
enum ModKind { Add, Percent, Multiply }
```

| Variant | Meaning |
|---------|---------|
| `Add` | Adds `amount` to the stat. |
| `Percent` | Adds `amount` times the base value: 0.1 is +10% of the base. Percents on one stat sum. |
| `Multiply` | Multiplies the stat by `amount`, after the adds and percents. |

## Modifier

A change to one stat. `source` names what gave it (an item, a buff) so it can be taken off
again; `seconds` above zero makes it wear off.

```rust
struct Modifier {
    stat: string = "",
    kind: ModKind = ModKind::Add,
    amount: float = 0.0,
    source: string = "",
    seconds: float = -1.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `stat` | `string` | `""` | The name of the stat it changes. |
| `kind` | `ModKind` | `ModKind::Add` | How it changes the stat. |
| `amount` | `float` | `0.0` | How much: a number to add, a fraction of the base, or a factor, by `kind`. |
| `source` | `string` | `""` | What gave it, such as an item or buff name; `Stats.remove_source` takes it off by this. |
| `seconds` | `float` | `-1.0` | Seconds until it wears off, counted down by `Stats.tick`; zero or below lasts until removed. |

## modifier

```rust
fn modifier(stat: string, kind: ModKind, amount: float, source: string) -> Modifier
```

A modifier that lasts until removed. Set `seconds` on the result to make it wear off.

```rust
let haste = modifier("speed", ModKind::Multiply, 1.5, "potion");
haste.seconds = 10.0;
stats.add(haste);
```

## Stats

Base values by name, and the modifiers on them.

```rust
struct Stats {
    base: {string: float} = {},
    mods: Modifier[] = [],
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `base` | `{string: float}` | `{}` | The base value of each stat, by name. |
| `mods` | `Modifier[]` | `[]` | Every modifier on it, in the order they were added. |

```rust
let s = stats();
s.set_base("attack", 10.0);
s.add(modifier("attack", ModKind::Add, 5.0, "sword"));
let attack = s.get("attack");   // 15.0
```

### Stats.set_base

```rust
fn set_base(self, stat: string, value: float)
```

Sets a stat's base value.

### Stats.base_of

```rust
fn base_of(self, stat: string) -> float
```

A stat's base value without modifiers; 0.0 for a stat with none set.

### Stats.add

```rust
fn add(self, m: Modifier)
```

Puts a modifier on.

### Stats.remove_source

```rust
fn remove_source(self, source: string)
```

Takes off every modifier from `source`.

### Stats.get

```rust
fn get(self, stat: string) -> float
```

The stat with every modifier applied: (base + adds + base * percents) * multiplies.

### Stats.tick

```rust
fn tick(self, dt: float)
```

Wears timed modifiers down by `dt` seconds; drops the ones that ran out. Call it once a
frame with `time.dt`.

## stats

```rust
fn stats() -> Stats
```

Stats with no base values and no modifiers.

## Flags

Named whole numbers for story state: switches (0 or 1), counters, choices. Missing names
read 0. Store it with save.set_value to keep it.

```rust
struct Flags {
    values: {string: int} = {},
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `values` | `{string: int}` | `{}` | The value of each name that was set. |

```rust
let story = flags();
story.set("met_ana", 1);
story.add("wolves", 1);
if story.check("wolves >= 3") { log("enough wolves"); }
```

### Flags.get

```rust
fn get(self, name: string) -> int
```

A flag's value; 0 for a name never set.

### Flags.set

```rust
fn set(self, name: string, value: int)
```

Sets a flag's value.

### Flags.on

```rust
fn on(self, name: string) -> bool
```

Whether a flag is anything but 0.

### Flags.add

```rust
fn add(self, name: string, by: int) -> int
```

Adds to a counter; returns the new value.

### Flags.check

```rust
fn check(self, condition: string) -> bool
```

Whether a condition holds: "key", "!key", or "key OP number" with OP one of == != &lt; &lt;= > >=.
An empty condition holds. A number that does not parse reads as 0.

### Flags.apply

```rust
fn apply(self, effect: string)
```

Applies an effect: "key" (sets 1), "key = 3", "key += 2", "key -= 1". An empty effect
does nothing.

## flags

```rust
fn flags() -> Flags
```

Flags with no names set.

## QuestState

Where a quest stands. A quest goes from Inactive to Active with `Quests.start`, then to Done
or Failed; only an Active quest counts progress.

```rust
enum QuestState { Inactive, Active, Done, Failed }
```

| Variant | Meaning |
|---------|---------|
| `Inactive` | Not started; every quest id starts here. |
| `Active` | Started and not finished; its objectives count progress. |
| `Done` | Completed. |
| `Failed` | Failed. |

## Quests

Quests by id: their state and objective counters. Store it with save.set_value to keep it.

```rust
struct Quests {
    states: {string: QuestState} = {},
    counters: {string: int} = {},
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `states` | `{string: QuestState}` | `{}` | The state of each quest that left Inactive, by quest id. |
| `counters` | `{string: int}` | `{}` | Objective counts, keyed "quest/objective". |

```rust
journal.start("hunt");
if journal.progress("hunt", "wolves", 1) >= 3 {
    journal.complete("hunt");
}
```

### Quests.state

```rust
fn state(self, quest: string) -> QuestState
```

A quest's state; Inactive for one never started.

### Quests.start

```rust
fn start(self, quest: string)
```

Makes an Inactive quest Active. A quest that is Active, Done or Failed stays as it is.

### Quests.complete

```rust
fn complete(self, quest: string)
```

Marks a quest Done, whatever its state.

### Quests.fail

```rust
fn fail(self, quest: string)
```

Marks a quest Failed, whatever its state.

### Quests.is_active

```rust
fn is_active(self, quest: string) -> bool
```

Whether a quest is Active.

### Quests.is_done

```rust
fn is_done(self, quest: string) -> bool
```

Whether a quest is Done.

### Quests.progress

```rust
fn progress(self, quest: string, objective: string, by: int) -> int
```

Counts toward an objective of an active quest ("wolves" of "hunt"); returns the count. A
quest that is not Active keeps its count.

### Quests.count

```rust
fn count(self, quest: string, objective: string) -> int
```

An objective's count; 0 before any progress.

### Quests.with_state

```rust
fn with_state(self, s: QuestState) -> string[]
```

The ids of every quest in a state. A quest never started has no entry, so it is not
listed as Inactive.

## quests

```rust
fn quests() -> Quests
```

Quests with every quest Inactive.

## DialogueLine

One line of dialogue: who says it and what they say.

```rust
struct DialogueLine {
    who: string = "",
    text: string = "",
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `who` | `string` | `""` | The speaker's name; may be empty. |
| `text` | `string` | `""` | The text, with {name} parts that `Dialogue.text` fills in. |

## DialogueChoice

One choice the player can pick at a dialogue node.

`when` is a Flags condition that must hold for the choice to show; `set` are Flags effects
applied when it is picked; `next` is the node it leads to ("" ends the dialogue).

```rust
struct DialogueChoice {
    text: string = "",
    next: string = "",
    when: string = "",
    set: string[] = [],
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `text` | `string` | `""` | What the choice says; {name} parts are filled in. |
| `next` | `string` | `""` | The node it leads to; "" ends the dialogue. |
| `when` | `string` | `""` | A Flags condition that must hold for the choice to show; "" always shows. |
| `set` | `string[]` | `[]` | Flags effects applied when it is picked. |

## DialogueNode

Lines said in order, then the choices, or straight on to `next`. `set` effects apply on
entering the node.

```rust
struct DialogueNode {
    lines: DialogueLine[] = [],
    choices: DialogueChoice[] = [],
    next: string = "",
    set: string[] = [],
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `lines` | `DialogueLine[]` | `[]` | The lines, said in order. |
| `choices` | `DialogueChoice[]` | `[]` | The choices offered on the last line. |
| `next` | `string` | `""` | The node after the last line when there are no choices; "" ends the dialogue. |
| `set` | `string[]` | `[]` | Flags effects applied on entering the node. |

## DialogueScript

A whole conversation, usually from a data file:
load_data&lt;DialogueScript>("assets/talk/ana.json").

```rust
struct DialogueScript {
    start: string = "",
    nodes: {string: DialogueNode} = {},
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `start` | `string` | `""` | The node `Dialogue.start("")` begins at. |
| `nodes` | `{string: DialogueNode}` | `{}` | Every node, by name. |

## Dialogue

A conversation being played. Variables live in `vars`; {name} in a line shows vars or
`names` (for text such as the player's name).

```rust
struct Dialogue {
    script: DialogueScript,
    node: string = "",
    line: int = 0,
    vars: Flags,
    names: {string: string} = {},
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `script` | `DialogueScript` | required | The conversation it plays. |
| `node` | `string` | `""` | The node it is on; "" when the dialogue is over or not started. |
| `line` | `int` | `0` | The index of the current line in the node. |
| `vars` | `Flags` | required | The flags its conditions read and its effects change. |
| `names` | `{string: string}` | `{}` | Text for {name} parts that are not flags, such as the player's name. |

```rust
let talk = dialogue(load_data<DialogueScript>("assets/talk/ana.json"), story);
talk.names["player"] = "Sam";
talk.start("");
log(talk.speaker() + ": " + talk.text());
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

Goes to a node: applies its `set` effects, and goes straight on to its `next` when it has
no lines and no choices. "" or a name the script does not have ends the dialogue.

### Dialogue.active

```rust
fn active(self) -> bool
```

Whether the dialogue is playing.

### Dialogue.current

```rust
fn current(self) -> DialogueLine
```

The current line as written; an empty line when the dialogue is over.

### Dialogue.speaker

```rust
fn speaker(self) -> string
```

Who says the current line.

### Dialogue.text

```rust
fn text(self) -> string
```

The line with its {name} parts filled in.

### Dialogue.substitute

```rust
fn substitute(self, text: string) -> string
```

`text` with each {name} replaced from `names`, then from the flags in `vars`.

### Dialogue.choices

```rust
fn choices(self) -> DialogueChoice[]
```

The choices to show now: on the node's last line, those whose `when` holds. Empty while
lines remain or when the node has none.

### Dialogue.advance

```rust
fn advance(self)
```

On to the next line, or the next node when the lines are done and there is nothing to
choose.

### Dialogue.choose

```rust
fn choose(self, i: int)
```

Picks choice i of choices().

## dialogue

```rust
fn dialogue(script: DialogueScript, vars: Flags) -> Dialogue
```

A dialogue over `script` that reads and changes `vars`; it starts with `start`.
