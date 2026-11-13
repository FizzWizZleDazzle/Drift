# drift/rpg

Everything `import "drift/rpg" { * };` brings in. The [Inventory, Stats, Quests and Dialogue](../guides/game-systems.md) guide shows them in use.

## Structs

| Type | Fields and methods |
|------|--------------------|
| `Inventory` | `slots`, `capacity`, `stack_size`. `add(item, count)`, `remove(item, count)`, `count(item)`, `has(item, count)`, `is_full()`, `items()` |
| `ItemStack` | `item`, `count` |
| `Stats` | `base`, `mods`. `set_base(stat, value)`, `base_of(stat)`, `add(m)`, `remove_source(source)`, `get(stat)`, `tick(dt)` |
| `Modifier` | `stat`, `kind`, `amount`, `source`, `seconds` (-1 for no time limit) |
| `Flags` | `values`. `get(name)`, `set(name, value)`, `on(name)`, `add(name, by)`, `check(condition)`, `apply(effect)` |
| `Quests` | `states`, `counters`. `start`, `complete`, `fail`, `state`, `is_active`, `is_done`, `progress(quest, objective, by)`, `count(quest, objective)`, `in_state(s)` |
| `Dialogue` | `script`, `node`, `line`, `vars`, `names`. `start(node)`, `active()`, `current()`, `speaker()`, `text()`, `choices()`, `advance()`, `choose(i)` |
| `DialogueScript` | `start`, `nodes` (a `{string: DialogueNode}` map) |
| `DialogueNode` | `lines`, `choices`, `next`, `set` |
| `DialogueLine` | `who`, `text` |
| `DialogueChoice` | `text`, `next`, `when`, `set` |

## Functions

| Function | Returns |
|----------|---------|
| `inventory(capacity, stack_size)` | An empty `Inventory` |
| `stats()`, `flags()`, `quests()` | Empty `Stats`, `Flags` and `Quests` |
| `modifier(stat, kind, amount, source)` | A `Modifier` without a time limit |
| `dialogue(script, vars)` | A `Dialogue` over a script, with `vars` as its `Flags` |

## Enums

| Enum | Values |
|------|--------|
| `ModKind` | `Add`, `Percent`, `Multiply` |
| `QuestState` | `Inactive`, `Active`, `Done`, `Failed` |
