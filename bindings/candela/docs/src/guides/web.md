# Web Games

`drift build --web` turns a game folder into a web page that runs in any browser with WebGL2. This page covers what changes in the browser: saves, and where to put the page.

## Saves

`Save` works the same in the browser. The slot is kept in the browser's `localStorage` for the page's site, so it survives reloads and browser restarts. `flush()` stores it before returning; call it at checkpoints as you would on the desktop, and the save is kept even if the player closes the tab right after.

The browser can refuse storage: in a sandboxed iframe without `allow-same-origin`, when the player blocks site data, or when the site's quota (usually about 5 MB) is full. `flush()` then fails with `drift_save_failed` and the slot lives only as long as the page. Clearing the site's data in the browser deletes the save.

Games on one site that share `organization` and `name` in `[project]` share a slot, as they would on one computer. Give each game its own `name`.
