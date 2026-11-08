# Limitations

- **Desktop only.** Candela games do not run in the browser yet. Drift's C++ games do; write the game in C++ to ship it on the web.
- **Tested on Linux.** Windows and macOS builds are not verified yet.
- **Systems run one at a time.** candela runs scripts on one thread, so your systems never run at the same time as each other. The engine's own work runs beside them.
- **No callbacks into scripts.** The engine calls systems; it does not call other script functions. Behavior that reacts to the engine reads messages instead.
- **Body settings apply at creation.** Changing a `RigidBody` or collider field on an existing entity has no effect; remove and insert the component to rebuild the body.
- **Startup systems run once.** A script reload does not run them again; restart to rebuild what they made.
- **C++ systems are not reachable.** A game is all candela. Tiled object spawners and other hooks that take C++ functions have candela equivalents only where this book lists them.
