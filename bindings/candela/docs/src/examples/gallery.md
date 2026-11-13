# Gallery

Every example is a complete game in one folder under `bindings/candela/examples`, built as a web page by the docs workflow. Play them here, then read the page to see how each one works.

| Game | Shows | Play |
|------|-------|------|
| [Star Catcher](../getting-started/tutorial.md) | The tutorial game: input, sensors, a resource, a state and a timer | [Play](../play/star_catcher/) |
| [Platformer](platformer.md) | A text level, gravity, jumping, ground contacts, sensors, a best time | [Play](../play/platformer/) |
| [Top-Down Shooter](shooter.md) | Mouse aiming, a fire-rate cooldown, chasing enemies, overlaps, particles | [Play](../play/shooter/) |
| [Breakout](breakout.md) | Bouncing bodies, a kinematic paddle, contact handlers, tweens, sounds from samples | [Play](../play/breakout/) |
| [Snake](snake.md) | A grid game without physics, a timer, spawning and despawning, states, save data | [Play](../play/snake/) |
| [Roguelike](roguelike.md) | A generated dungeon, a tilemap made in code, weighted tables, pathfinding, turns | [Play](../play/roguelike/) |
| [Effects Showcase](effects.md) | Every kind of particle emitter, bursts, post effects, a trail, easing curves | [Play](../play/effects/) |
| [Menus and States](menu.md) | Title, options, play and pause screens as states, settings and saved options | [Play](../play/menu/) |
| [Save and Load](save-load.md) | Save slots with ints, floats and strings, a picture kept as text | [Play](../play/save_load/) |
| [Flappy Bird](flappy-bird.md) | Sprites and sounds from files, states, settings, kinematic pipes | [Play](../play/flappy_bird/) |
| [Coin Dash](coin-dash.md) | Most of what a game needs on one screen: states, physics, HUD, save data | [Play](../play/coin_dash/) |
| [Cave Walk](cave-walk.md) | Caves and dungeons from one seed, noise, flow fields | [Play](../play/cave_walk/) |

Click a game once so it has the keyboard. Sound starts after the first click or key press.

To build one yourself, run `drift build --web` in its folder and serve `build/web`, as [Publishing to the Web](../guides/publishing.md) describes. Copying an example folder is a good way to start a game of your own.
