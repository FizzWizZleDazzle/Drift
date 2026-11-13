# Localization

Show the game's text in the player's language. Each language is one file of keys and texts; the game asks for a key and gets the text in the language picked at startup, falling back to a default language for keys a translation lacks.

## Language files

Put one file per language in a folder under assets, named by language code, in JSON or TOML. `assets/lang/en.json`:

```json
{ "title": "Space Rocks", "score": "Score: {score}", "lives": "{n} lives left" }
```

`assets/lang/fr.toml`:

```toml
title = "Rochers de l'espace"
score = "Score : {score}"
```

`[locale]` in `drift.toml` says where they are and which language to use. Every key has a default:

```toml
[locale]
folder = "assets/lang"   # files named <language>.json or <language>.toml
language = "auto"        # "auto": the browser's language; or a code such as "fr"
fallback = "en"          # where keys missing from a translation come from
```

With `"auto"`, a web build asks the browser for the player's preferred language, and a desktop build reads the system's (`LANG`).

## Using the text

`localization()` loads the language `[locale]` picks. Keep the result in a resource:

```rust
@resource
struct Ui { text: Strings = localization() }

@system(schedule: Schedule::Startup)
fn hud(cmd: Commands, ui: Ui) {
    cmd.spawn([at(16.0, 16.0), Text { value: ui.text.get("title"), size: 32.0, layer: hud_layer(), .. }]);
}

@system
fn show_score(ui: Ui, game: Game, label: ScoreLabel, text: Text) {
    text.value = ui.text.get_with("score", {"score": grouped(game.score)});
}
```

| Member | Returns |
|--------|---------|
| `get(key)` | The text for `key`; from the fallback language when this one lacks it; the key itself when neither has it, so a missing key shows on screen instead of failing |
| `get_with(key, values)` | The same, with each `{name}` replaced by `values[name]` |
| `language` | The language loaded: `"fr"` for a player whose browser asks for `"fr-CA"` when only `fr` exists |

A language with a region, such as `fr-CA`, uses `fr-CA.json` when it exists, then `fr.json`, then the fallback language. `localization_for("de")` loads one language directly, for a language menu; store the player's choice with `save.set_string` and pass it in on the next start.

Numbers and times read better formatted: `grouped`, `fixed`, `pad` and `clock` in [Text](text.md#formatting-numbers) work inside `get_with` values.
