<!-- Written by scripts/candela-reference.py from the drift module and
     bindings/candela/docs/api-notes.md. Edit those, not this page. -->

# Data, Networking and Text

Data files, HTTP and WebSockets, sequences of timed steps, colors, text formatting and localization. See [Data Files](../guides/data-files.md), [Networking](../guides/networking.md) and [Localization](../guides/localization.md).

## hsv

```rust
fn hsv(h: float, s: float, v: float) -> Color
```

A color from hue, saturation and value, each 0.0 to 1.0 (hue goes once around the color wheel: 0.0 red, 0.33 green, 0.67 blue).

## Hsv

```rust
struct Hsv {
    h: float = 0.0,
    s: float = 0.0,
    v: float = 0.0,
}
```

## hex_digit

```rust
fn hex_digit(c: string) -> int
```

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

### Palette.len

```rust
fn len(self) -> int
```

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

## palette

```rust
fn palette(hexes: string[]) -> Palette
```

## pico8

```rust
fn pico8() -> Palette
```

The PICO-8 sixteen: black, dark blue, purple, dark green, brown, dark grey, light grey, white, red, orange, yellow, green, blue, lavender, pink, peach.

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

## Sequence

Steps that run one after another across frames: waits, named events, messages, and waits for a signal. Keep one in a component or resource and tick it every frame.

```rust
struct Sequence {
    steps: SequenceStep[] = [],
    at: int = 0,
    waited: float = 0.0,
    loops: int = 0,
    signals: string[] = [],
}
```

### Sequence.wait

```rust
fn wait(self, seconds: float) -> Sequence
```

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

### Sequence.restart

```rust
fn restart(self)
```

### Sequence.tick

```rust
fn tick(self, dt: float) -> string[]
```

Advances by dt seconds; returns the names of the `then` steps reached.

## sequence

```rust
fn sequence() -> Sequence
```

## format

```rust
fn format(pattern: string, args: any[]) -> string
```

The pattern with each {} replaced by the next argument and each {0}, {1} by the argument at that index: format("{} of {}", [3, 10]) is "3 of 10".

## fill

```rust
fn fill(pattern: string, values: {string: any}) -> string
```

The pattern with each {name} replaced by values[name]: fill("Hi {who}", {"who": "Ana"}). Names not in `values` stay as they are.

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

## clock

```rust
fn clock(seconds: float, tenths: bool) -> string
```

Seconds as "m:ss", or "h:mm:ss" from an hour up; with `tenths`, a digit of tenths after a point: "1:05.3".

## Strings

[locale] folder = "assets/lang"   # files named &lt;language>.json or .toml language = "auto"        # the browser's (or system's) language, or "fr" fallback = "en"          # where keys missing from a language come from

```rust
struct Strings {
    language: string = "",
    table: {string: string} = {},
    fallback: {string: string} = {},
}
```

### Strings.get

```rust
fn get(self, key: string) -> string
```

The text for `key`, from the fallback language when this one lacks it, or the key itself when neither has it.

### Strings.get_with

```rust
fn get_with(self, key: string, values: {string: any}) -> string
```

The text for `key` with its {name} parts filled from `values`.

## locale_setting

```rust
fn locale_setting(key: string, fallback: string) -> string
```

## locale_table

```rust
fn locale_table(folder: string, language: string) -> Option<
```

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

## read_data

```rust
fn read_data(path: string, format: string) -> string
```

## load_data

```rust
fn load_data<T>(path: string) -> T
```

A JSON, TOML or CSV file (by its extension) as a value of type T: a struct, a list, a map. Fields the file leaves out keep their zero value. A CSV file is a list of rows, so T is a list: load_data&lt;Item[]>(path).

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

## HttpResponse

The answer to a request. `ok` is true for a 2xx status; `status` is 0 and `error` says why when there was no answer at all (offline, blocked by CORS, a bad URL).

```rust
@message
struct HttpResponse {
    request: Request,
    status: int,
    ok: bool,
    body: string,
    error: string,
}
```

## http_get

```rust
fn http_get(url: string) -> Request
```

## http_post

```rust
fn http_post(url: string, body: string) -> Request
```

Sends `body` as JSON: http_post(url, to_json(score)).

## http_request

```rust
fn http_request(method: string, url: string, body: string, headers: {string: string}) -> Request
```

Any method, with headers.

## Socket

A WebSocket connection. What happens on it arrives as SocketEvent messages.

```rust
struct Socket {
    id: int = 0,
}
```

### Socket.send

```rust
fn send(self, text: string) -> bool
```

False when the socket is not open.

### Socket.close

```rust
fn close(self)
```

## SocketEventKind

```rust
enum SocketEventKind { Opened, Message, Closed, Error }
```

## SocketEvent

```rust
@message
struct SocketEvent {
    socket: Socket,
    kind: SocketEventKind,
    text: string,
    code: int,
}
```

## websocket

```rust
fn websocket(url: string) -> Socket
```

Opens a WebSocket to a ws:// or wss:// URL. Send once Opened has arrived.
