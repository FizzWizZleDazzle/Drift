# Save and Load

Paint a picture, save it into one of three slots, change it, and load it back. Reload the page: the saves are still there.

[Play it in the browser](../play/save_load/). Click a cell, or move the cursor with the arrow keys and press Space, to change its color. 1, 2 and 3 pick a slot; S saves, L loads, C clears the canvas and X erases the slot.

## What it shows

- `Save` with ints, floats and strings, `has` and `erase`
- `flush` to write the save, in the browser to `localStorage`
- Keys named per slot to keep several saves side by side
- A grid kept as a string of digits and read back with `parse`
- Mouse and keyboard on the same grid

## Run it

```sh
cd bindings/candela/examples/save_load
drift build --web
cd build/web && python3 -m http.server 8000
```

The example is self-contained: every image and sound is made in code.

## drift.toml

```toml
{{#include ../../../examples/save_load/drift.toml}}
```

## Saving and loading

The picture is a string with one digit per cell under `slotN.cells`, beside the painting time and a save count. Loading checks `has` first and parses each digit back.

```rust
{{#include ../../../examples/save_load/main.cdl:files}}
```

## The whole game

```rust
{{#include ../../../examples/save_load/main.cdl}}
```
