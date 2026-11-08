# The drift Command

`drift` runs, checks and builds candela games. It reads `drift.toml` in the current directory unless `--config` names another.

## drift run

```sh
drift run                    # the entry script drift.toml names
drift run main.cdl           # a script
drift run game.cdlb          # a built game
```

Compiles the script and the modules it imports, opens the window and runs the game. A compile error prints with its file, line and a pointer at the code, and the game does not start.

## drift check

```sh
drift check
```

Compiles without running: types, attributes, system parameters and `writes:`. It prints every system with what it reads, what it writes and the order it runs in. Editors and CI run it.

## drift build

```sh
drift build                  # writes build/<name>.cdlb
drift build -o game.cdlb
```

Compiles the game into one `.cdlb` file that `drift run` starts without compiling anything. Ship it with `drift.toml` and the assets folder.

## drift new

```sh
drift new my_game
```

Makes a folder with a `drift.toml`, a `main.cdl` that opens a window and moves a square, and an empty `assets/`.

## Options

These go after the subcommand and apply to `run`:

| Option | Does |
|--------|------|
| `--config <path>` | Read this file instead of `./drift.toml` |
| `--set section.key=value` | Override one setting for this run; repeat for more |
| `--no-reload` | Do not watch the scripts and `drift.toml` for changes |

Settings also come from `DRIFT_<SECTION>_<KEY>` environment variables; `--set` wins over them. See [Resources and Settings](../concepts/resources.md#settings).
