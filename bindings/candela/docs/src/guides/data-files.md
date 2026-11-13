# Data Files

Levels, item tables, enemy stats and dialogue read better as data than as code. `load_data<T>(path)` reads a JSON, TOML or CSV file into a value of a type you declare, so the rest of the game works with typed structs instead of raw text.

## Loading a file into a struct

Declare structs shaped like the file, then name the type you want:

```json
[
  {"id": "potion", "name": "Potion", "price": 25, "heal": 0.5},
  {"id": "sword", "name": "Sword", "price": 120, "tags": ["weapon"], "rarity": "Rare"}
]
```

```rust
enum Rarity { Common, Rare }
struct Item { id: string = "", name: string = "", price: int = 0, heal: float = 0.0,
              tags: string[] = [], rarity: Rarity = Rarity::Common }

@resource
struct Shop { items: Item[] = load_data<Item[]>("assets/items.json") }
```

The file's extension picks the format: `.json`, `.toml` or `.csv`. Object keys match field names; a field the file leaves out gets the zero value of its type (`0`, `""`, `false`, an empty list), not the default the struct declares, so give every field a value in the file when the zero is wrong. Keys the struct does not have are ignored. An enum field reads the variant's name. Nested structs, lists and `{string: T}` maps read the same way.

`T` can be any type the file fits: a struct, a list of structs, a map such as `{string: Item}`, or `{string: any}` for data you inspect yourself.

```toml
# assets/levels/forest.toml
name = "Forest"
time_limit = 90.0

[spawn]
x = 120.0
y = 64.0

[[enemies]]
kind = "slime"
count = 3
```

```rust
struct Spawn { x: float = 0.0, y: float = 0.0 }
struct Wave { kind: string = "", count: int = 0 }
struct Level { name: string = "", time_limit: float = 0.0, spawn: Spawn = Spawn { .. }, enemies: Wave[] = [] }

let level = load_data<Level>("assets/levels/forest.toml");
```

## CSV tables

A CSV file is a list of rows, so load it as a list: `load_data<Item[]>("assets/items.csv")`. The first row names the columns, and each later row becomes one struct with the columns as fields. Cells are text in CSV; a cell read into an `int`, `float` or `bool` field is converted (`true` and `1` are true). Quote a cell that holds commas, quotes (written twice) or line breaks:

```text
id,name,price,stackable
potion,"Potion, small",25,true
elixir,"The ""Best"" Elixir",300,false
```

Spreadsheets save this format, so designers can edit balance tables without touching code.

## Text, JSON values and saves

| Function | Does |
|----------|------|
| `load_data<T>(path)` | Reads a JSON, TOML or CSV file into a `T` |
| `read_text(path)` | The text of a file |
| `parse_json<T>(text)` | JSON text into a `T`, such as a server's answer |
| `to_json(value)` | Any value as JSON text: structs become objects, enums their variant name |

A missing or malformed file raises `drift_load_failed` with the path, and the log says why; `parse_json` on text that is not JSON raises `drift_bad_data`.

`save.set_value(key, value)` and `save.get_value<T>(key, fallback)` store any value in the save slot as JSON, which keeps a whole progress struct, an inventory or a quest log in one key. See [Save data](timers-and-saves.md#save-data).

## In the browser

`drift build --web` packs the assets folder into the page, so data files load the same way in the browser. Load data at startup or in resource defaults rather than every frame: each load reads and converts the whole file.

Data files are read when the game asks for them. Editing one while the game runs changes nothing until the game loads it again.
