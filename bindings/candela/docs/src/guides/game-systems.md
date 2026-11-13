# Inventory, Stats, Quests and Dialogue

Building blocks most RPGs, shops and adventure games write by hand: an inventory, stats with modifiers, story flags and quests, and branching dialogue read from data files. Each is a plain value: keep it in a resource or component, and store it with [`save.set_value`](saving.md) to keep it between runs. They come with the `drift/rpg` module; the [drift/rpg reference](../reference/rpg.md) lists every member.

```rust
import "drift" { * };
import "drift/rpg" { * };
```

## Inventory

An `Inventory` holds items by id, stacked up to a size per slot:

```rust
@resource
struct Bag { items: Inventory = inventory(20, 99) }   // 20 slots, 99 per stack

@system
fn pick_up(hit: Overlap<Player, Loot>, loot: Lookup<Loot>, bag: Bag, cmd: Commands) {
    if loot.get(hit.b) is Some(l) {
        if bag.items.add(l.item, l.count) == 0 {
            cmd.despawn(hit.b);
        }
    }
}
```

| Member | Does |
|--------|------|
| `add(item, count)` | Adds what fits, filling existing stacks first; returns how many did not fit |
| `remove(item, count)` | Takes `count`, from the last stacks first; takes nothing and returns `false` when there are fewer |
| `count(item)`, `has(item, count)` | How many there are; whether there are at least `count` |
| `is_full()` | Every slot is in use |
| `items()` | Each item id once, in slot order |
| `slots` | The stacks: `ItemStack { item, count }` |

Ids are strings, so item definitions (names, prices, icons) can live in a [data file](data-files.md) keyed by the same ids.

## Stats and modifiers

`Stats` keeps base values by name and the modifiers on them: equipment, buffs and debuffs.

```rust
@component
struct Fighter { stats: Stats = stats() }

fn equip_boots(f: Fighter) {
    f.stats.add(modifier("speed", ModKind::Add, 20.0, "boots"));
}

fn haste(f: Fighter) {
    f.stats.add(Modifier { stat: "speed", kind: ModKind::Percent, amount: 0.5, source: "haste", seconds: 5.0 });
}

@system
fn move_fighters(f: Fighter, v: Velocity, time: Time) {
    f.stats.tick(time.dt);
    v.linear = v.linear.normalized() * f.stats.get("speed");
}
```

`get(stat)` is `(base + every Add + base * the sum of every Percent) * every Multiply`. With a base speed of 100, boots (+20) and haste (+50%) give 170.

| Member | Does |
|--------|------|
| `set_base(stat, value)`, `base_of(stat)` | The value before modifiers; a stat never set is 0 |
| `add(m)` | Adds a modifier |
| `remove_source(source)` | Takes off every modifier from one source, such as an item being unequipped |
| `get(stat)` | The value with every modifier applied |
| `tick(dt)` | Counts down modifiers with `seconds` above 0 and drops the ones that ran out |

A `Modifier` has `stat`, `kind` (`ModKind::Add`, `Percent` or `Multiply`), `amount`, `source` and `seconds` (-1 for no time limit). `modifier(stat, kind, amount, source)` makes one without a time limit.

## Flags and quests

`Flags` holds named whole numbers for story state: switches, counters and choices. A name never set reads 0.

```rust
@resource
struct Story { flags: Flags = flags(), quests: Quests = quests() }

@system
fn wolf_killed(_: Died, story: Story) {
    if story.quests.progress("hunt", "wolves", 1) >= 5 {
        story.quests.complete("hunt");
        story.flags.set("hunter", 1);
    }
}
```

| `Flags` member | Does |
|--------|------|
| `get(name)`, `set(name, value)`, `on(name)` | Read, write, and whether a flag is not 0 |
| `add(name, by)` | Adds to a counter; returns the new value |
| `check(condition)` | Tests a condition written as text: `"met_ana"`, `"!met_ana"`, or a name, one of `==`, `!=`, `<`, `<=`, `>`, `>=`, and a number, such as `"gold >= 5"`. An empty condition holds |
| `apply(effect)` | Applies an effect written as text: `"met_ana"` sets 1, `"gold = 10"`, `"gold += 5"`, `"gold -= 5"` |

`check` and `apply` take text so conditions and effects can live in data files; dialogue uses them.

`Quests` tracks quests by id. A quest is `QuestState::Inactive` until started, then `Active`, `Done` or `Failed`.

| `Quests` member | Does |
|--------|------|
| `start(quest)` | Makes an inactive quest active |
| `complete(quest)`, `fail(quest)` | Ends it |
| `state(quest)`, `is_active(quest)`, `is_done(quest)` | Where it stands |
| `progress(quest, objective, by)` | Counts toward an objective of an active quest and returns the count; does nothing once the quest is not active |
| `count(quest, objective)` | An objective's count |
| `in_state(s)` | Every quest in a state, for a quest log screen |

## Dialogue

A conversation is a set of nodes. Each node has lines said in order, then either choices or a `next` node. Write them in a JSON or TOML file:

```json
{
  "start": "hello",
  "nodes": {
    "hello": {
      "set": ["met_ana"],
      "lines": [
        {"who": "Ana", "text": "Hello, {player}."},
        {"who": "Ana", "text": "You have {gold} gold. Want a potion?"}
      ],
      "choices": [
        {"text": "Buy one (5 gold)", "next": "buy", "when": "gold >= 5", "set": ["gold -= 5", "potions += 1"]},
        {"text": "No thanks", "next": "bye"}
      ]
    },
    "buy": {"lines": [{"who": "Ana", "text": "Here you go."}], "next": "bye"},
    "bye": {"lines": [{"who": "Ana", "text": "See you."}]}
  }
}
```

A node's `set` effects apply when the dialogue enters it. A choice shows only when its `when` condition holds; picking it applies its `set` effects and goes to its `next` node. An empty or missing `next` ends the conversation. Conditions and effects are the ones `Flags` reads, against the dialogue's `vars`.

```rust
@resource
struct Talk { now: Dialogue = dialogue(load_data<DialogueScript>("assets/talk/ana.json"), flags()) }

@system(schedule: Schedule::Startup)
fn begin(talk: Talk) {
    talk.now.names["player"] = "Bo";
    talk.now.start("");
}

@system
fn show_dialogue(talk: Talk, input: Input, box: DialogueBox, text: Text) {
    if !talk.now.active() {
        text.value = "";
        return;
    }
    text.value = talk.now.speaker() + ": " + talk.now.text();
    let choices = talk.now.choices();
    for i in range(choices.len()) {
        text.value = text.value + "\n" + string(i + 1) + ". " + choices[i].text;
    }
    if choices.len() == 0 && input.key_pressed(Key::Space) {
        talk.now.advance();
    }
    if choices.len() > 0 && input.key_pressed(Key::Num1) { talk.now.choose(0); }
    if choices.len() > 1 && input.key_pressed(Key::Num2) { talk.now.choose(1); }
}
```

| Member | Does |
|--------|------|
| `dialogue(script, vars)` | A conversation over a `DialogueScript`, with `vars` as its `Flags` |
| `start(node)` | Starts at a node; `""` starts at the script's `start` |
| `active()` | Whether the conversation is running |
| `speaker()`, `text()` | The current line's `who`, and its `text` with `{name}` filled from `names` and `vars` |
| `choices()` | The choices to show: on the node's last line, those whose `when` holds |
| `advance()` | The next line, or the `next` node after the last; does nothing while there are choices |
| `choose(i)` | Picks choice `i` of `choices()` |
| `vars`, `names` | The `Flags` conditions and effects use, and `{string: string}` text such as the player's name |

A dialogue keeps its own copy of the `vars` it was given. To keep what the player chose, save `talk.now.vars`, or copy the flags you need into the game's own when the conversation ends.
