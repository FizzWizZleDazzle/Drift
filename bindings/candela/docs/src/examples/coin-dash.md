# Coin Dash

A complete game: move a square with the arrow keys, WASD or a gamepad stick, collect coins, and dodge hazards that bounce around the arena. A hit ends the round; R restarts it. The best score is saved between runs.

It shows most of what a game needs: components and resources of its own, systems with declared writes, queries with tuples and lookups, commands, collision and sensor messages, input actions, physics, text on the HUD, particles, sound made from samples, timers, save data and settings from `drift.toml`.

Run it from its folder:

```sh
cd examples/coin_dash
drift run
```

## drift.toml

```toml
[project]
name = "Coin Dash"
organization = "Drift Examples"
entry = "main.cdl"

[window]
width = 960
height = 540

[render]
clear_color = [0.07, 0.08, 0.12]

[physics]
gravity = [0, 0]

[coin_dash]
hazard_speed = 220.0
spawn_every = 4.0
```

The arena is top-down, so `[physics]` turns gravity off. `[coin_dash]` holds the game's tuning values; the `Tuning` struct below reads them.

## main.cdl

```rust
// Coin Dash: collect coins, dodge the bouncing hazards.

import "drift";
import "drift" { Entity, Commands, Transform, Velocity, Sprite, Rectangle, Text,
                 RigidBody, BodyKind, BoxCollider, CircleCollider, ParticleEmitter,
                 Lifetime, Align, VAlign, Blend, Ease, Key, GamepadAxis, Color, Vec2,
                 Texture, Sound, Timer, Input, Time, at, vec2, rgb, rgba, tween,
                 hud_layer, repeating_timer };
import "std/math" as math;
import "std/random" as random;

// ---- Components ----

@drift::component
struct Player { speed: float = 320.0 }

@drift::component
struct Coin { value: int = 1 }

@drift::component
struct Hazard {}

@drift::component
struct ScoreLabel {}

@drift::component
struct HintLabel {}

// ---- Resources ----

@drift::settings(section: "coin_dash")
struct Tuning {
    hazard_speed: float = 220.0,
    spawn_every: float = 4.0,
}

@drift::resource
struct Game {
    score: int = 0,
    best: int = 0,
    over: bool = false,
    spawn: Timer = repeating_timer(4.0),
}

// Made in setup; it has no defaults, so it exists once setup inserts it.
@drift::resource
struct Art {
    coin: Texture,
    ball: Texture,
    pickup: Sound,
    crash: Sound,
}

// ---- Startup ----

@drift::system(schedule: drift::Schedule::Startup, writes: [Game])
fn setup(cmd: Commands, assets: drift::Assets, input: Input, save: drift::Save,
         tuning: Tuning, game: Game) {
    input.bind_axis2d("move", Key::Left, Key::Right, Key::Up, Key::Down);
    input.bind_axis2d("move", Key::A, Key::D, Key::W, Key::S);
    input.bind_stick("move", GamepadAxis::LeftX, GamepadAxis::LeftY);
    input.bind_key("restart", Key::R);

    cmd.insert_resource(Art {
        coin: disc(assets, 24, rgb(255, 210, 60)),
        ball: disc(assets, 32, rgb(240, 80, 90)),
        pickup: tone(assets, 880.0, 0.12),
        crash: tone(assets, 110.0, 0.35),
    });
    game.best = save.get_int("best", 0);
    game.spawn = repeating_timer(tuning.spawn_every);

    arena(cmd);
    cmd.spawn([
        at(480.0, 270.0),
        Rectangle { size: vec2(28.0, 28.0), color: rgb(90, 170, 255), .. },
        RigidBody { kind: BodyKind::Dynamic, fixed_rotation: true, .. },
        BoxCollider { half_size: vec2(14.0, 14.0), .. },
        Velocity { .. },
        Player { .. },
    ]);
    cmd.spawn([at(20.0, 14.0), Text { size: 28.0, layer: hud_layer(), .. }, ScoreLabel {}]);
    cmd.spawn([
        at(480.0, 270.0),
        Text { value: "Hit!  Press R to play again", size: 32.0, align: Align::Center,
               valign: VAlign::Middle, outline: 2.0, layer: hud_layer(), visible: false, .. },
        HintLabel {},
    ]);
}

// The first round starts once Art exists.
@drift::system(schedule: drift::Schedule::Startup, after: [setup])
fn first_round(cmd: Commands, art: Art) {
    for i in range(3) {
        spawn_coin(cmd, art);
    }
}

// ---- Every frame ----

@drift::system(writes: [Velocity])
fn steer(players: (Player, Velocity)[], input: Input, game: Game) {
    let dir = vec2(0.0, 0.0);
    if !game.over {
        dir = input.axis2d("move");
    }
    for row in players {
        let (player, velocity) = row;
        velocity.linear = dir.mul(player.speed);
    }
}

@drift::system(writes: [Game])
fn spawn_hazards(cmd: Commands, time: Time, game: Game, art: Art, tuning: Tuning,
                 players: (Player, Transform)[]) {
    if game.over || game.spawn.tick(time.dt) == 0 {
        return;
    }
    let pos = vec2(random::float_range(80.0, 880.0), 90.0);
    for row in players {
        let (_, t) = row;
        if t.position.distance(pos) < 160.0 {
            pos = vec2(960.0 - pos.x, 460.0);
        }
    }
    let angle = random::float_range(0.0, 6.28);
    cmd.spawn([
        Transform { position: pos, .. },
        Sprite { image: art.ball, .. },
        RigidBody { .. },
        CircleCollider { radius: 16.0, restitution: 1.0, friction: 0.0, .. },
        Velocity { linear: vec2(math::cos(angle), math::sin(angle)).mul(tuning.hazard_speed), .. },
        Hazard {},
    ]);
}

@drift::system(writes: [Game])
fn pick_up(hits: drift::Overlap<Player, Coin>[], coins: drift::Lookup<Coin>,
           places: drift::Lookup<Transform>, cmd: Commands, effects: drift::Effects,
           game: Game, art: Art) {
    for hit in hits {
        if game.over {
            return;
        }
        let coin = hit.b;
        if coins.get(coin) is Some(c) {
            game.score += c.value;
        }
        if places.get(coin) is Some(t) {
            effects.burst(t.position, ParticleEmitter {
                color_start: rgb(255, 230, 120), color_end: rgba(255, 120, 40, 0),
                speed_min: 60.0, speed_max: 180.0, lifetime_min: 0.2, lifetime_max: 0.5,
                size_start: 5.0, blend: Blend::Additive, ..
            }, 24);
            // A "+1" that floats up and goes away by itself.
            cmd.spawn([
                Transform { position: t.position.add(vec2(0.0, -16.0)), .. },
                Text { value: "+1", size: 20.0, color: rgb(255, 230, 120), align: Align::Center, .. },
                Velocity { linear: vec2(0.0, -60.0), .. },
                Lifetime { seconds: 0.6 },
            ]);
        }
        art.pickup.play();
        cmd.despawn(coin);
        spawn_coin(cmd, art);
    }
}

@drift::system(writes: [Game])
fn crash(hits: drift::Contact<Player, Hazard>[], effects: drift::Effects,
         save: drift::Save, game: Game, art: Art) {
    if hits.len() == 0 || game.over {
        return;
    }
    game.over = true;
    art.crash.play();
    effects.flash(rgb(255, 60, 60), 0.4);
    if game.score > game.best {
        game.best = game.score;
        save.set_int("best", game.best);
        save.flush();
    }
}

@drift::system(writes: [Game, Transform],
               after: [crash])
fn restart(input: Input, cmd: Commands, game: Game, art: Art,
           players: (Player, Transform)[],
           coins: (Entity, Coin)[], hazards: (Entity, Hazard)[]) {
    if !game.over || !input.action_pressed("restart") {
        return;
    }
    for row in coins {
        cmd.despawn(row.0);
    }
    for row in hazards {
        cmd.despawn(row.0);
    }
    for row in players {
        let (_, t) = row;
        t.position = vec2(480.0, 270.0);
    }
    game.score = 0;
    game.over = false;
    game.spawn.reset();
    for i in range(3) {
        spawn_coin(cmd, art);
    }
}

@drift::system(writes: [Text], after: [pick_up, crash, restart])
fn show_score(game: Game, scores: (ScoreLabel, Text)[], hints: (HintLabel, Text)[]) {
    for row in scores {
        row.1.value = "Score " + string(game.score) + "    Best " + string(game.best);
    }
    for row in hints {
        row.1.visible = game.over;
    }
}

// ---- Helpers ----

fn spawn_coin(cmd: Commands, art: Art) {
    cmd.spawn([
        at(random::float_range(80.0, 880.0), random::float_range(90.0, 460.0)),
        Sprite { image: art.coin, .. },
        CircleCollider { radius: 12.0, sensor: true, .. },
        tween().scale_to(vec2(1.25, 1.25), 0.5, Ease::InOutSine).yoyo(-1),
        Coin { .. },
    ]);
}

// Static walls around the arena and a faint grid.
fn arena(cmd: Commands) {
    let color = rgb(60, 66, 96);
    wall(cmd, vec2(480.0, 62.0), vec2(960.0, 12.0), color);
    wall(cmd, vec2(480.0, 534.0), vec2(960.0, 12.0), color);
    wall(cmd, vec2(6.0, 298.0), vec2(12.0, 480.0), color);
    wall(cmd, vec2(954.0, 298.0), vec2(12.0, 480.0), color);
    let y = 80.0;
    while y < 530.0 {
        cmd.spawn([at(480.0, y), Rectangle { size: vec2(936.0, 1.0), color: rgba(255, 255, 255, 12), z: -10.0, .. }]);
        y += 40.0;
    }
}

fn wall(cmd: Commands, pos: Vec2, size: Vec2, c: Color) {
    cmd.spawn([
        Transform { position: pos, .. },
        Rectangle { size: size, color: c, .. },
        BoxCollider { half_size: size.mul(0.5), friction: 0.0, .. },
    ]);
}

// A filled circle texture with a soft edge.
fn disc(assets: drift::Assets, size: int, c: Color) -> Texture {
    let pixels: Color[] = [];
    let r = float(size) * 0.5;
    for y in range(size) {
        for x in range(size) {
            let dx = float(x) + 0.5 - r;
            let dy = float(y) + 0.5 - r;
            let a = r - (dx * dx + dy * dy).sqrt();
            if a > 1.0 {
                a = 1.0;
            }
            if a < 0.0 {
                a = 0.0;
            }
            pixels.push(c.alpha(a));
        }
    }
    return assets.create_texture(size, size, pixels);
}

// A short sine blip that falls in pitch.
fn tone(assets: drift::Assets, freq: float, seconds: float) -> Sound {
    let rate = 22050;
    let n = int(seconds * float(rate));
    let samples: float[] = [];
    let phase = 0.0;
    for i in range(n) {
        let t = float(i) / float(n);
        phase += 6.2831853 * freq * (1.0 - 0.4 * t) / float(rate);
        samples.push(math::sin(phase) * 0.4 * (1.0 - t));
    }
    return assets.create_sound(samples, rate);
}
```

## How it fits together

- `setup` runs once. It binds the input actions, makes the art, reads the best score and spawns the arena, the player and the HUD. `first_round` runs after it, once `Art` exists.
- `steer` turns the `move` action into the player's velocity; physics moves the body.
- `spawn_hazards` ticks the timer in `Game` and spawns a hazard away from the player.
- `pick_up` reads this frame's sensor overlaps between a `Player` and a `Coin`. `crash` reads the contacts between a `Player` and a `Hazard`.
- `restart` clears the round when R is pressed after a hit.
- `show_score` runs after the systems that change the score, so the HUD shows this frame's value.

Every system that changes a component or resource names it in `writes:`. The engine runs `steer`, `spawn_hazards` and the others one after another on the script thread, and runs its own work (physics, the coin tweens, the "+1" labels' `Lifetime`) beside them where their data does not overlap. See [The Frame](../concepts/frame.md).
