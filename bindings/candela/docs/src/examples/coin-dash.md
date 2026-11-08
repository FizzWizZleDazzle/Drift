# Coin Dash

A complete game: move a square with the arrow keys, WASD or a gamepad stick, collect coins, and dodge hazards that bounce around the arena. A hit ends the round; R restarts it. The best score is saved between runs.

It shows most of what a game needs: components and resources of its own, states, per-entity systems and collision handlers, queries and lookups, commands, input actions, physics, text on the HUD, particles, sound made from samples, timers, save data and settings from `drift.toml`.

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

[input]
restart = ["key:R", "button:South"]

[coin_dash]
hazard_speed = 220.0
spawn_every = 4.0
```

The arena is top-down, so `[physics]` turns gravity off. `[input]` binds the restart action; the movement axis is bound in code. `[coin_dash]` holds the game's tuning values; the `Tuning` struct below reads them.

## main.cdl

```rust
// Coin Dash: collect coins, dodge the bouncing hazards.

import "drift" { * };
import "std/math" as math;
import "std/random" as random;

const SPAWN_POINT: Vec2 = Vec2 { x: 480.0, y: 270.0 };

@state
enum Round { Playing, Over }

// ---- Components ----

@component(requires: [Velocity])
struct Player { speed: float = 320.0 }

@component struct Coin { value: int = 1 }
@component struct Hazard {}
@component struct ScoreLabel {}
@component struct HintLabel {}

// ---- Resources ----

@settings(section: "coin_dash")
struct Tuning {
    hazard_speed: float = 220.0,
    spawn_every: float = 4.0,
}

@resource
struct Game { score: int = 0, best: int = 0, spawn: Timer = repeating_timer(4.0) }

// Made from code at load: two textures from pixels and two tones.
@resource
struct Art {
    coin: Texture = disc(24, rgb(255, 210, 60)),
    ball: Texture = disc(32, rgb(240, 80, 90)),
    pickup: Sound = tone(880.0, 0.12),
    crash: Sound = tone(110.0, 0.35),
}

// ---- Startup ----

@system(schedule: Schedule::Startup)
fn setup(cmd: Commands, input: Input, save: Save, game: Game) {
    input.bind_axis2d("move", Key::Left, Key::Right, Key::Up, Key::Down);
    input.bind_axis2d("move", Key::A, Key::D, Key::W, Key::S);
    input.bind_stick("move", GamepadAxis::LeftX, GamepadAxis::LeftY);
    game.best = save.get_int("best", 0);

    arena(cmd);
    cmd.spawn([
        Transform { position: SPAWN_POINT, .. },
        Rectangle { size: vec2(28.0, 28.0), color: rgb(90, 170, 255), .. },
        RigidBody { kind: BodyKind::Dynamic, fixed_rotation: true, .. },
        BoxCollider { half_size: vec2(14.0, 14.0), .. },
        Player { .. },
    ]);
    cmd.spawn([at(20.0, 14.0), Text { size: 28.0, layer: hud_layer(), .. }, ScoreLabel {}]);
    cmd.spawn([
        Transform { position: SPAWN_POINT, .. },
        Text { value: "Hit!  Press R to play again", size: 32.0, align: Align::Center,
               valign: VAlign::Middle, outline: 2.0, layer: hud_layer(), visible: false, .. },
        HintLabel {},
    ]);
}

// ---- Rounds ----

// A new round: no coins or hazards left over, the player at the start.
@system(on_enter: Round::Playing)
fn new_round(cmd: Commands, game: Game, tuning: Tuning, art: Art,
             players: (Player, Transform, Velocity)[],
             coins: (Entity, Coin)[], hazards: (Entity, Hazard)[]) {
    for (e, _) in coins {
        cmd.despawn(e);
    }
    for (e, _) in hazards {
        cmd.despawn(e);
    }
    for (_, t, v) in players {
        t.position = SPAWN_POINT;
        v.linear = vec2(0.0, 0.0);
    }
    game.score = 0;
    game.spawn = repeating_timer(tuning.spawn_every);
    for i in range(3) {
        cmd.spawn(coin(art));
    }
}

@system(on_enter: Round::Over)
fn crash(game: Game, art: Art, effects: Effects, save: Save,
         players: (Player, Velocity)[]) {
    for (_, v) in players {
        v.linear = vec2(0.0, 0.0);
    }
    art.crash.play();
    effects.flash(rgb(255, 60, 60), 0.4);
    if game.score > game.best {
        game.best = game.score;
        save.set_int("best", game.best);
        save.flush();
    }
}

fn restart_pressed(input: Input) -> bool => input.action_pressed("restart");

@system(in_state: [Round::Over], run_if: [restart_pressed])
fn restart(round: State<Round>) {
    round.set(Round::Playing);
}

// ---- Every frame ----

@system(in_state: [Round::Playing])
fn steer(player: Player, velocity: Velocity, input: Input) {
    velocity.linear = input.axis2d("move") * player.speed;
}

@system(in_state: [Round::Playing])
fn spawn_hazards(cmd: Commands, time: Time, game: Game, art: Art, tuning: Tuning,
                 players: (Player, Transform)[]) {
    if game.spawn.tick(time.dt) == 0 {
        return;
    }
    let pos = vec2(random::float_range(80.0, 880.0), 90.0);
    for (_, t) in players {
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
        Velocity { linear: vec2(math::cos(angle), math::sin(angle)) * tuning.hazard_speed, .. },
        Hazard {},
    ]);
}

// Called once for each coin the player touches.
@system(in_state: [Round::Playing])
fn pick_up(hit: Overlap<Player, Coin>, coins: Lookup<Coin>, places: Lookup<Transform>,
           cmd: Commands, effects: Effects, game: Game, art: Art) {
    if coins.get(hit.b) is Some(c) {
        game.score += c.value;
    }
    if places.get(hit.b) is Some(t) {
        effects.burst(t.position, ParticleEmitter {
            color_start: rgb(255, 230, 120), color_end: rgba(255, 120, 40, 0),
            speed_min: 60.0, speed_max: 180.0, lifetime_min: 0.2, lifetime_max: 0.5,
            size_start: 5.0, blend: Blend::Additive, ..
        }, 24);
        // A "+1" that floats up and goes away by itself.
        cmd.spawn([
            Transform { position: t.position + vec2(0.0, -16.0), .. },
            Text { value: "+1", size: 20.0, color: rgb(255, 230, 120), align: Align::Center, .. },
            Velocity { linear: vec2(0.0, -60.0), .. },
            Lifetime { seconds: 0.6 },
        ]);
    }
    art.pickup.play();
    cmd.despawn(hit.b);
    cmd.spawn(coin(art));
}

@system(in_state: [Round::Playing])
fn hit(_: Contact<Player, Hazard>, round: State<Round>) {
    round.set(Round::Over);
}

// ---- HUD ----

@system
fn score_label(_: ScoreLabel, text: Text, game: Game) {
    text.value = "Score " + string(game.score) + "    Best " + string(game.best);
}

@system
fn hint_label(_: HintLabel, text: Text, round: State<Round>) {
    text.visible = round.now == Round::Over;
}

// ---- Helpers ----

fn coin(art: Art) -> any[] {
    return [
        at(random::float_range(80.0, 880.0), random::float_range(90.0, 460.0)),
        Sprite { image: art.coin, .. },
        CircleCollider { radius: 12.0, sensor: true, .. },
        tween().scale_to(vec2(1.25, 1.25), 0.5, Ease::InOutSine).yoyo(-1),
        Coin { .. },
    ];
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
        BoxCollider { half_size: size * 0.5, friction: 0.0, .. },
    ]);
}

// A filled circle texture with a soft edge.
fn disc(size: int, c: Color) -> Texture {
    let pixels: Color[] = [];
    let r = float(size) * 0.5;
    for y in range(size) {
        for x in range(size) {
            let dx = float(x) + 0.5 - r;
            let dy = float(y) + 0.5 - r;
            pixels.push(c.alpha(clamp(r - (dx * dx + dy * dy).sqrt(), 0.0, 1.0)));
        }
    }
    let assets = Assets { .. };
    return assets.create_texture(size, size, pixels);
}

// A short sine blip that falls in pitch.
fn tone(freq: float, seconds: float) -> Sound {
    let rate = 22050;
    let n = int(seconds * float(rate));
    let samples: float[] = [];
    let phase = 0.0;
    for i in range(n) {
        let t = float(i) / float(n);
        phase += 6.2831853 * freq * (1.0 - 0.4 * t) / float(rate);
        samples.push(math::sin(phase) * 0.4 * (1.0 - t));
    }
    let assets = Assets { .. };
    return assets.create_sound(samples, rate);
}
```

## How it fits together

- `Round` is the game's state. `new_round` runs when a round starts (the first one starts after `setup`), `crash` when it ends, and `restart` waits for the restart action while the round is over.
- `steer` runs once per player: the `move` axis sets its velocity, and physics moves the body.
- `spawn_hazards` ticks the timer in `Game` and spawns a hazard away from the player.
- `pick_up` runs once for each coin the player touches; `hit` runs when the player touches a hazard and ends the round.
- `Art` makes its textures and sounds in its field defaults, so no system loads them.
- `score_label` and `hint_label` run once per label and copy the score and the round into the text.

Drift keeps every change a system makes. It runs the systems one after another on the script thread, and runs its own work (physics, the coin tweens, the "+1" labels' `Lifetime`) beside them where their data does not overlap. See [The Frame](../concepts/frame.md).
