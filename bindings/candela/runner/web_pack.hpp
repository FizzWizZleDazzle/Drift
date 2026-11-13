// Web pages for Candela games: a game folder packed into one file the web
// runtime loads at startup.
//
// The web runtime (drift.html, drift.js, drift.wasm) is the same for every
// game. A page is that runtime beside game.pack, which holds the game's
// drift.toml, every .cdl script and the asset folder. The runtime fetches the
// pack before main() runs and writes each file into its file system at the
// path it had in the game folder, so the game starts as it does on the
// desktop. `drift build --web` and the in-tree web build both write pages
// through buildWebPage.
//
// game.pack, little-endian:
//
//     "DRIFTPAK"        8 bytes
//     version           u32, 1
//     index length      u32, N
//     index             N bytes of JSON: {"files": [{"path", "offset", "size"}]}
//     file bytes        offsets count from the end of the index
//
// pack_loader.js reads it in the browser.

#pragma once

#include <string>

namespace drift::web {

// Writes the page for the game in `folder` to `out`: index.html, drift.js,
// drift.wasm from `runtime` and the game's game.pack. Returns false and sets
// `error` when the folder, its entry script or the runtime is missing, or a
// file cannot be read or written. `summary` gets one line saying what was
// written.
bool buildWebPage(const std::string& folder, const std::string& out, const std::string& runtime,
                  std::string& error, std::string& summary);

} // namespace drift::web
