# Limitations

What Candela games cannot do yet, and what to do instead where there is a way.

- **Tested in Chrome on Linux.** Other browsers with WebGL2 have not been tried. The desktop `drift` is built for Linux, macOS and Windows; only Linux is tested.
- **No script reload while the game runs.** Rebuild the page and reload it after a change.
- **Systems run one at a time.** Candela runs scripts on one thread, so your systems never run at the same time as each other. The engine's own work, such as physics and particles, runs beside them where it can.
- **Engine components cross into scripts as copies.** A system that takes an engine component, such as `Transform`, receives it from the engine per entity and per call. Thousands of entities in per-entity systems slow the game down; keep queries narrow, as [Queries](concepts/queries.md#keeping-queries-cheap) describes.
- **A dynamic body needs a `Velocity` to show its motion.** Without one it stays where it was drawn while the physics world moves it.
- **Sensors without a body notice dynamic bodies only.** A kinematic or static body passes through them unreported.
- **Networking needs the web build.** HTTP requests and WebSockets go through the browser; on the desktop they answer with an error.
- **Data files load when asked.** Editing a data file while the game runs changes nothing until the game loads it again.
- **Not every C++ feature is reachable.** A game is all Candela; the engine's C++ plugins and hooks are available only where this book lists them.
