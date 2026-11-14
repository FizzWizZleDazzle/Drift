<!-- Written by scripts/candela-reference.py from the doc comments in
     bindings/candela/drift/src. Edit those, not this page. -->

# Data, Networking and Text

Data files, HTTP and WebSockets, sequences of timed steps, colors, text formatting and
localization. See [Data Files](../guides/data-files.md),
[Networking](../guides/networking.md) and [Localization](../guides/localization.md).

## hsv

```rust
fn hsv(h: float, s: float, v: float) -> Color
```

A color from hue, saturation and value, each 0.0 to 1.0 (hue goes once around the color
wheel: 0.0 red, 0.33 green, 0.67 blue).

## Hsv

A color as hue, saturation and value, each 0.0 to 1.0. `Color.to_hsv` makes one; `hsv`
turns the three numbers back into a `Color`.

```rust
struct Hsv {
    h: float = 0.0,
    s: float = 0.0,
    v: float = 0.0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `h` | `float` | `0.0` | Hue, once around the color wheel: 0.0 is red, 1/3 green, 2/3 blue. |
| `s` | `float` | `0.0` | Saturation: 0.0 is grey, 1.0 the full color. |
| `v` | `float` | `0.0` | Value: 0.0 is black, 1.0 the brightest. |

```rust
let c = sky.to_hsv();
let paler = hsv(c.h, c.s * 0.5, c.v);
```

## hex_digit

```rust
fn hex_digit(c: string) -> int
```

The value of one hex digit, 0 to 15, in either case. A character that is not a hex digit
gives 0.

## hex

```rust
fn hex(text: string) -> Color
```

A color from "#rgb", "#rrggbb" or "#rrggbbaa" (the # is optional).

## Palette

An ordered set of colors: pick by index, or blend along it as a gradient.

```rust
struct Palette {
    colors: Color[] = [],
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `colors` | `Color[]` | `[]` | The colors, in order. |

### Palette.len

```rust
fn len(self) -> int
```

How many colors it holds.

### Palette.at

```rust
fn at(self, i: int) -> Color
```

The color at i, wrapping around the end.

### Palette.sample

```rust
fn sample(self, t: float) -> Color
```

A gradient through every color: 0.0 is the first, 1.0 the last.

### Palette.random

```rust
fn random(self, rng: Rng) -> Color
```

One of its colors, picked with `rng`. The palette must not be empty.

```rust
let tint = pico8().random(random.stream("tints"));
```

## palette

```rust
fn palette(hexes: string[]) -> Palette
```

A palette from hex colors, in the forms `hex` reads. A malformed color throws
`drift_bad_color`.

```rust
let sunset = palette(["#2e1f47", "#c6505a", "#f2a65a"]);
let sky = sunset.sample(time.elapsed / 60.0);
```

## pico8

```rust
fn pico8() -> Palette
```

The PICO-8 sixteen: black, dark blue, purple, dark green, brown, dark grey, light grey,
white, red, orange, yellow, green, blue, lavender, pink, peach.

## SequenceStep

One step of a Sequence; build them with sequence() and its methods.

```rust
struct SequenceStep {
    kind: int = 0,
    seconds: float = 0.0,
    name: string = "",
    message: any = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `kind` | `int` | `0` | What the step does: 0 waits, 1 is a `then` name, 2 sends a message, 3 waits for a signal |
| `seconds` | `float` | `0.0` | How long a wait step lasts, in seconds |
| `name` | `string` | `""` | The name a `then` step returns or a `wait_for` step waits for |
| `message` | `any` | `0` | The message a `send` step sends |

## Sequence

Steps that run one after another across frames: waits, named events, messages, and waits for
a signal. Keep one in a component or resource and tick it every frame.

```rust
struct Sequence {
    steps: SequenceStep[] = [],
    at: int = 0,
    waited: float = 0.0,
    loops: int = 0,
    signals: string[] = [],
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `steps` | `SequenceStep[]` | `[]` | The steps, in order |
| `at` | `int` | `0` | The index of the step it is on; equal to the number of steps once it has finished |
| `waited` | `float` | `0.0` | Seconds spent so far in the current wait step |
| `loops` | `int` | `0` | How many more times the steps run after they end; -1 forever |
| `signals` | `string[]` | `[]` | Signals sent with `signal` that no `wait_for` step has used yet |

### Sequence.wait

```rust
fn wait(self, seconds: float) -> Sequence
```

Adds a step that waits `seconds` before the next one.

The time left over in a frame carries into the steps after it, so a short wait does not
cost a whole frame.

### Sequence.then

```rust
fn then(self, name: string) -> Sequence
```

tick() returns `name` when the sequence gets here.

### Sequence.send

```rust
fn send(self, message: any) -> Sequence
```

Sends a message when the sequence gets here.

### Sequence.wait_for

```rust
fn wait_for(self, name: string) -> Sequence
```

Waits until signal(name) is called.

### Sequence.repeat

```rust
fn repeat(self, times: int) -> Sequence
```

Runs the steps `times` more times once they end; -1 forever.

### Sequence.signal

```rust
fn signal(self, name: string)
```

Lets a wait_for(name) step go on, now or when the sequence reaches it.

### Sequence.finished

```rust
fn finished(self) -> bool
```

Whether every step has run. A sequence that repeats forever never finishes.

### Sequence.restart

```rust
fn restart(self)
```

Goes back to the first step and forgets pending signals. The `repeat` count is not reset.

### Sequence.tick

```rust
fn tick(self, dt: float) -> string[]
```

Advances by dt seconds; returns the names of the `then` steps reached.

## sequence

```rust
fn sequence() -> Sequence
```

An empty Sequence, to build with `wait`, `then`, `send`, `wait_for` and `repeat`.

```rust
let intro = sequence().wait(1.0).then("show_title").wait(2.0).send(StartGame {});
```

## format

```rust
fn format(pattern: string, args: any[]) -> string
```

The pattern with each {} replaced by the next argument and each {0}, {1} by the argument at
that index: format("{} of {}", [3, 10]) is "3 of 10".

## format_named

```rust
fn format_named(pattern: string, values: {string: any}) -> string
```

The pattern with each {name} replaced by values[name]: format_named("Hi {who}", {"who":
"Ana"}). Names not in `values` stay as they are.

## fixed

```rust
fn fixed(x: float, decimals: int) -> string
```

x with exactly `decimals` digits after the point: fixed(3.14159, 2) is "3.14".

## pad

```rust
fn pad(n: int, width: int) -> string
```

n with zeros in front up to `width` digits: pad(7, 3) is "007".

## grouped

```rust
fn grouped(n: int) -> string
```

n with a comma between each group of three digits: "12,345".

## format_clock

```rust
fn format_clock(seconds: float) -> string
```

Seconds as "m:ss", or "h:mm:ss" from an hour up: "1:05".

## format_clock_tenths

```rust
fn format_clock_tenths(seconds: float) -> string
```

`seconds` as a clock like `format_clock`, with a digit of tenths after a point: "1:05.3".

## Strings

The game's text in the player's language: one file per language, mapping keys to text, in
JSON or TOML. `localization()` makes one; the `[locale]` section of `drift.toml` says where
the files are:

```rust
struct Strings {
    language: string = "",
    table: {string: string} = {},
    fallback: {string: string} = {},
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `language` | `string` | `""` | The language the text comes from, such as "fr"; the fallback language when no file matched. |
| `table` | `{string: string}` | `{}` | Key to text in `language`. |
| `fallback` | `{string: string}` | `{}` | Key to text in the fallback language, for keys `table` lacks. |

```toml
[locale]
folder = "assets/lang"   # files named <language>.json or .toml
language = "auto"        # the browser's (or system's) language, or "fr"
fallback = "en"          # where keys missing from a language come from
```

```rust
let strings = localization();
title.value = strings.get("menu.start");
```

### Strings.get

```rust
fn get(self, key: string) -> string
```

The text for `key`, from the fallback language when this one lacks it, or the key itself
when neither has it.

### Strings.get_with

```rust
fn get_with(self, key: string, values: {string: any}) -> string
```

The text for `key` with its {name} parts filled from `values`.

## locale_setting

```rust
fn locale_setting(key: string, fallback: string) -> string
```

The `[locale]` setting `key` from `drift.toml`, or `fallback` when the file leaves it out.

## locale_table

```rust
fn locale_table(folder: string, language: string) -> Option<
```

The key-to-text table in `folder` for `language`, read from `<language>.json` or, failing
that, `<language>.toml`; `None` when neither file exists.

## localization

```rust
fn localization() -> Strings
```

The text the player reads, as [locale] in drift.toml picks it.

## localization_for

```rust
fn localization_for(language: string) -> Strings
```

The text in one language ("fr-CA" falls back to "fr", then to the fallback language).

## read_text

```rust
fn read_text(path: string) -> string
```

The text of a file, such as one under assets/.

## load_data

```rust
fn load_data<T>(path: string) -> T
```

A JSON, TOML or CSV file (by its extension) as a value of type T: a struct, a list, a map.
Fields the file leaves out keep their zero value. A CSV file is a list of rows, so T is a
list: load_data&lt;Item[]>(path).

## parse_json

```rust
fn parse_json<T>(text: string) -> T
```

JSON text as a value of type T.

## to_json

```rust
fn to_json(value: any) -> string
```

Any value as JSON text: structs become objects, enums their variant name.

## Request

A request in flight. Its answer arrives as an HttpResponse message.

```rust
struct Request {
    id: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `int` | `0` | The engine's number for the request; compare it with `HttpResponse.request.id` |

## HttpResponse

The answer to a request. `ok` is true for a 2xx status; `status` is 0 and `error` says why
when there was no answer at all (offline, blocked by CORS, a bad URL).

```rust
@message
struct HttpResponse {
    request: Request = Request { .. },
    status: int = 0,
    ok: bool = false,
    body: string = "",
    error: string = "",
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `request` | `Request` | `Request { .. }` | The request this answers |
| `status` | `int` | `0` | The HTTP status code, such as 200 or 404; 0 when there was no answer |
| `ok` | `bool` | `false` | Whether the status is in the 200s |
| `body` | `string` | `""` | The answer's body, as text |
| `error` | `string` | `""` | Why there was no answer; "" when there was one |

## http_get

```rust
fn http_get(url: string) -> Request
```

Sends a GET request to `url`; the answer arrives later as an HttpResponse message.
Requests work only in the web build; elsewhere the answer is an error.

```rust
let req = http_get("https://example.com/scores.json");
```

## http_post

```rust
fn http_post(url: string, body: string) -> Request
```

Sends a POST request with `body` as JSON: `http_post(url, to_json(score))`. The answer
arrives later as an HttpResponse message.

## http_request

```rust
fn http_request(method: string, url: string, body: string, headers: {string: string}) -> Request
```

Sends a request with any method and headers; the answer arrives later as an HttpResponse
message.

## Socket

A WebSocket connection. What happens on it arrives as SocketEvent messages.

```rust
struct Socket {
    id: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `int` | `0` | The engine's number for the socket; compare it with `SocketEvent.socket.id` |

### Socket.send

```rust
fn send(self, text: string) -> bool
```

False when the socket is not open.

### Socket.close

```rust
fn close(self)
```

Closes the connection with code 1000; a Closed event follows.

## SocketEventKind

What happened on a Socket.

```rust
enum SocketEventKind { Opened, Message, Closed, Error }
```

| Variant | Meaning |
|---------|---------|
| `Opened` | The connection is open; `send` works from now on |
| `Message` | A message arrived; `text` holds it |
| `Closed` | The connection closed; `code` is the close code and `text` the reason |
| `Error` | Something went wrong; `text` says what. A Closed event follows when the connection ends |

## SocketEvent

Something that happened on a WebSocket: it opened, a message arrived, it closed, or it
failed. Sockets work only in the web build; elsewhere a socket reports an Error and closes.

```rust
@message
struct SocketEvent {
    socket: Socket = Socket { .. },
    kind: SocketEventKind = SocketEventKind::Opened,
    text: string = "",
    code: int = 0,
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `socket` | `Socket` | `Socket { .. }` | The socket it happened on |
| `kind` | `SocketEventKind` | `SocketEventKind::Opened` | What happened |
| `text` | `string` | `""` | The message for Message, the reason for Closed, the cause for Error |
| `code` | `int` | `0` | The WebSocket close code for Closed (1000 is a normal close, 1006 a lost connection); 0 otherwise |

```rust
@system
fn chat(events: SocketEvent[]) {
    for e in events {
        if e.kind == SocketEventKind::Message { log(e.text); }
    }
}
```

## websocket

```rust
fn websocket(url: string) -> Socket
```

Opens a WebSocket to a ws:// or wss:// URL. Send once Opened has arrived.
