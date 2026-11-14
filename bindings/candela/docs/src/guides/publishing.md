# Publishing to the Web

`drift build --web` turns a game folder into a web page that runs in any browser with WebGL2, on desktops, phones and tablets. This page covers publishing it, what the page does around the game, and what changes in the browser.

## Build and try it

```sh
drift build --web                          # writes build/web/
cd build/web && python3 -m http.server     # open http://localhost:8000
```

The folder holds `index.html`, the engine (`drift.js`, `drift.wasm`), `game.pack` with your scripts, `drift.toml` and assets, and the files the settings below add (`icon.png`, `manifest.webmanifest`, `sw.js`). Every link in it is relative, so the folder works at any address: the root of a site, a subfolder, or inside an iframe. Browsers do not run it from `file://`; serve it over HTTP.

## Hot reload while you work

Build with `--dev` and the open page picks up every new build without reloading:

```sh
drift build --web --dev                    # writes build/web/ with hot reload
cd build/web && python3 -m http.server     # open http://localhost:8000
# edit, then run `drift build --web --dev` again: the page changes in place
```

A dev page asks the server about `game.pack` twice a second. When the pack changes, the page fetches it, compiles the new code and swaps it into the running game. The player stays where it was, the score stays, and the next frame runs the new code. Any static server works: the page compares the `ETag`, `Last-Modified` and `Content-Length` the server sends for the pack.

What a reload keeps:

- **Entities and engine components**: every `Transform`, `Sprite`, body and particle stays as it was.
- **Your components and resources**, matched to the new structs field by field. A field keeps its value; a new field gets its default; a removed field is dropped. A value the game never changed follows a new default: change `speed: float = 200.0` to `300.0` and every entity whose `speed` was still 200 now has 300.
- **States** stay in the variant they were in. A removed variant, or a new `@state` enum, starts at its first variant and runs its `on_enter` systems.
- **Messages** not read yet, and messages sent with `send_after`.
- **Systems switched off** with `Systems` stay off. A system that raised an error and was turned off runs again with its new code.

Startup systems do not run again, so an edit to `setup` shows the next time the game starts: reload the page, or start over with `cmd.restart()`. A startup system added by the edit runs once at the next frame.

Changed textures, sounds, fonts and scenes load again in place. A changed data file or `drift.toml` reloads the code too, so resource defaults built with `load_data` read the new file.

When the new code does not compile, the game keeps running the code it had, and the compiler's message shows over the game until the next build compiles or you close it. The same message goes to the browser console. When a value cannot move into the new code, the page loads again from the start and the console says which value: this happens when a field's type changes while it holds a value the game set, such as `hp: int` becoming `hp: float` after the player took damage.

A dev page is never an offline page: `--dev` ignores `[web] offline`. Publish a build made without `--dev`; it carries no hot reload and asks the server nothing.

## The page

While the engine and `game.pack` download, the page shows the game's name and a progress bar. Both downloads start as soon as the page does, side by side, and the engine compiles while it arrives. `drift build --web` compiles the game's scripts into `game.pack`, so the page starts the game without compiling them. When it cannot, it says why and the page compiles the scripts when it loads, as a dev page always does.

If the game cannot start, the page says so instead of showing a black canvas: a script that does not compile shows the compiler's message with its file and line, and a missing or broken `game.pack` shows what failed. The same text goes to the browser console.

`[project]` in `drift.toml` describes the page:

| Key | Does |
|-----|------|
| `name` | The page title, and the name on the loading screen |
| `description` | The page's description for search engines and link previews |
| `icon` | An image in the game folder (`.png`, `.svg`, `.ico`, `.webp` or `.jpg`): the tab icon and the home screen icon. Use a square PNG of at least 512 pixels |

`[web]` sets up the rest:

```toml
[web]
background = "#14141c"        # behind the game and the loading screen
orientation = "landscape"     # "any", "landscape" or "portrait"
fullscreen_button = true      # a Fullscreen button in the corner
touch_controls = "auto"       # "auto" (touch screens only), "always" or "never"
touch_stick = "arrows"        # "arrows", "wasd" or "" for none
touch_buttons = ["Space", { key = "X", label = "Fire" }]
offline = true                # installable, plays without a connection
```

`drift build --web` checks these and stops with a message naming the key when a value is wrong.

## Hosting

Upload the folder to any static host: GitHub Pages, Netlify, Cloudflare Pages, an S3 bucket, your own server. On GitHub Pages, commit the folder's files to the branch or folder Pages publishes, or upload it from a workflow with `actions/upload-pages-artifact`, as this book does with its examples. What makes it load fast:

- **Compression.** `drift.wasm` is most of the download and shrinks to about a third with gzip or Brotli. Most hosts compress it on their own; on your own server, turn on gzip or Brotli for `.wasm`, `.js` and `.pack`.
- **The wasm type.** Serve `.wasm` as `application/wasm`. The page compiles the engine as it downloads either way.
- **Caching.** The file names do not change between builds, so let browsers keep them but check for a newer copy: `Cache-Control: no-cache` with an `ETag` or `Last-Modified` answer. A long `max-age` would keep players on an old build.
- **HTTPS.** `offline` and the clipboard only work on `https://` pages (and `http://localhost`).

## itch.io and other sites

The page works inside an iframe, so it runs as an HTML game on itch.io and on game portals:

1. `drift build --web`, then zip the contents of `build/web` so `index.html` is at the top of the zip: `cd build/web && zip -r ../game.zip .`
2. On itch.io, make a new project of kind HTML, upload the zip and tick "This file will be played in the browser".
3. Set the viewport size to the game's `[window]` size or larger, and turn on the fullscreen button and "Mobile friendly" if the game supports touch.

To embed it in your own site, point an iframe at the folder and allow fullscreen:

```html
<iframe src="game/index.html" width="960" height="540" allow="fullscreen; autoplay; gamepad"></iframe>
```

Keyboard input reaches the game once the player clicks into it, as with every embedded page. Browsers that partition storage keep the saves of an iframe from another site per embedding site; a sandboxed iframe without `allow-same-origin` keeps none.

## Phones and tablets

Touch works without any setup: a finger on the game is the left mouse button at that point, so `mouse_pressed`, `mouse_screen` and actions bound to `mouse:Left` respond to taps. A tap shorter than a frame still counts as a press.

For games played with keys, `[web]` adds on-screen controls that press keys for the game, so key reads and actions bound to those keys work unchanged:

- `touch_stick` puts a stick in the lower left. `"arrows"` presses the arrow keys and `"wasd"` presses W, A, S and D; tilting it diagonally presses two. It is digital: an axis built from those keys reads -1, 0 or 1.
- `touch_buttons` puts buttons in the lower right, one per key. A key name (`"Space"`, `"Z"`, `"Return"`, `"Up"`, any `Key` variant) shows that name; `{ key = "X", label = "Fire" }` shows a label instead.
- `touch_controls` decides when they show: `"auto"` on touch screens only, `"always"`, or `"never"` while you design your own.

`orientation` covers the game with a notice asking players on a phone or tablet held the other way to turn it. The game keeps running behind it. In fullscreen, browsers that can lock the orientation do.

`fullscreen_button` shows a Fullscreen button in the corner of the page, the way to fill a phone's screen; phones keep the browser's bars otherwise. The button only shows where the browser offers fullscreen for pages, which iPhones do not.

`Platform.is_touch()` tells a game it is on a touch screen, to show tap hints instead of key names.

## Sound

Browsers keep a page silent until the player taps, clicks or presses a key. The page starts the sound on that first gesture, and sounds played before it are skipped. Give the game a title screen that waits for a tap or key, and the first sound the player should hear plays.

## Background tabs

A tab in the background gets no frames from the browser, so the game pauses there; its sound pauses with it. When the player comes back, the game carries on from where it was: the first frame back advances one ordinary step, not the time spent away. Save progress at checkpoints, since a player may close a background tab without coming back.

## Fullscreen and pointer lock

```rust
@system
fn options(input: Input, window: Window) {
    if input.key_pressed(Key::F) {
        window.set_fullscreen(!window.fullscreen());
    }
    if input.mouse_pressed(MouseButton::Left) {
        window.lock_pointer(true);     // mouse-look; Escape releases it
    }
}
```

Browsers only grant fullscreen and pointer lock right after the player taps, clicks or presses a key. Asked for from a system that runs because of one, as above, they happen at once. Asked for at any other time, they wait for the player's next tap, click or key.

While the pointer is locked the cursor is hidden and `input.mouse_delta()` reports movement. On the desktop the same calls make the window fullscreen and capture the mouse.

`window.pixel_ratio()` is the number of screen pixels per window unit: 2 or 3 on phones, 1 on most monitors. The page renders at the screen's full resolution and resizes with the browser window on its own; the ratio is there for games that pick art by screen density.

## The browser from a game

The `Platform` resource reaches the browser around the game. Its methods work on the desktop too, where they answer as the desktop would.

```rust
@system(schedule: Schedule::Startup)
fn start(platform: Platform, game: Game) {
    let level = platform.query("level");     // index.html?level=3 -> "3"
    if level != "" {
        game.level = int(level);
    }
    let now = platform.local_time();
    if now.month == 12 {
        game.snow = true;
    }
}
```

| Method | Does |
|--------|------|
| `is_web()` | True in a browser |
| `is_touch()` | True on a touch screen |
| `open_url(url)` | Opens a link in a new tab; on the desktop, in the default browser. Browsers block it unless the player tapped, clicked or pressed a key just before |
| `copy(text)` | Puts text on the clipboard |
| `pasted()` | In a browser, the text the player last pasted into the page (Ctrl+V), since pages cannot read the clipboard unasked. On the desktop, the clipboard's text |
| `query(name)` | A value from the page address, `""` when it is not there; always `""` on the desktop |
| `local_time()` | A `LocalTime`: `year`, `month` (1 to 12), `day`, `hour`, `minute`, `second`, `weekday` (0 is Sunday) and `unix`, seconds since 1970 in UTC |

## Offline and install

With `offline = true` the page plays without a connection after the first visit, and browsers offer to install it as an app with its own icon and window. The build adds `manifest.webmanifest`, which names the game, its icon, colors and orientation, and `sw.js`, a service worker that keeps a copy of every file of the page.

A new build replaces the copy: a returning player's browser fetches the new build in the background on their next visit and plays it from the visit after. Turning `offline` off and building into the same folder writes a `sw.js` that removes the stored copy and itself, so players who installed the old page move to the new one.

Offline pages need HTTPS, which itch.io and the hosts above serve.

## Saves

`Save` works the same in the browser. The slot is kept in the browser's `localStorage` for the page's site, so it survives reloads and browser restarts. `flush()` stores it before returning; call it at checkpoints as you would on the desktop, and the save is kept even if the player closes the tab right after.

The browser can refuse storage: in a sandboxed iframe without `allow-same-origin`, when the player blocks site data, or when the site's quota (usually about 5 MB) is full. `flush()` then fails with `drift_save_failed` and the slot lives only as long as the page. Clearing the site's data in the browser deletes the save.

Games on one site that share `organization` and `name` in `[project]` share a slot, as they would on one computer. Give each game its own `name`.

## Limits in the browser

- Every system and all engine work run on one thread.
- The page needs WebGL2, which every current desktop and mobile browser has.
- The touch stick presses keys, so it has no half-tilt.
