# Networking

HTTP requests for leaderboards, daily challenges and remote config, and WebSockets for simple multiplayer and live services. Both run in web builds through the browser's `fetch` and `WebSocket`. A request or a socket returns at once; what comes back arrives later as a [message](../concepts/messages.md), so the game keeps running while it waits.

## HTTP requests

```rust
struct Entry { name: string = "", score: int = 0 }

@resource
struct Board { request: Request = Request { .. }, top: Entry[] = [] }

@system(schedule: Schedule::Startup)
fn fetch_scores(board: Board) {
    board.request = http_get("https://example.com/api/top");
}

@system
fn got_scores(r: HttpResponse, board: Board) {
    if r.request == board.request && r.ok {
        board.top = parse_json<Entry[]>(r.body);
    }
}

@system
fn submit(_: GameOver, game: Game) {
    http_post("https://example.com/api/scores", to_json(Entry { name: game.player, score: game.score }));
}
```

| Function | Sends |
|----------|-------|
| `http_get(url)` | A GET request |
| `http_post(url, body)` | A POST with `body` as `application/json` |
| `http_request(method, url, body, headers)` | Any method, with headers as a `{string: string}` map |

Each returns a `Request`. Its answer is one `HttpResponse` message:

| Field | Meaning |
|-------|---------|
| `request` | The `Request` it answers; compare with `==` |
| `status` | The HTTP status, or 0 when no answer came |
| `ok` | `true` for a 2xx status |
| `body` | The answer as text; `parse_json<T>(r.body)` reads JSON |
| `error` | Why there was no answer: offline, a bad URL, or a server that does not allow the page's site (CORS) |

A page served from one site can call another only when that server sends CORS headers allowing it. A relative URL such as `/api/scores` goes to the site the page came from.

## WebSockets

```rust
@resource
struct Net { socket: Socket = Socket { .. } }

@system(schedule: Schedule::Startup)
fn connect(net: Net) {
    net.socket = websocket("wss://example.com/room/1");
}

@system
fn on_socket(e: SocketEvent, net: Net, cmd: Commands) {
    match e.kind {
        SocketEventKind::Opened => { net.socket.send(to_json(Hello { name: "Ana" })); }
        SocketEventKind::Message => { cmd.send(parse_json<Move>(e.text)); }
        SocketEventKind::Closed => log("closed with code " + string(e.code)),
        SocketEventKind::Error => warn("socket error: " + e.text),
    }
}
```

`websocket(url)` opens a connection to a `ws://` or `wss://` URL and returns a `Socket`. Every change arrives as a `SocketEvent` with `socket`, `kind`, `text` (the message, or the error or close reason) and `code` (the close code):

| `kind` | When |
|--------|------|
| `Opened` | The connection is ready; `send` works from now on |
| `Message` | A message arrived. Binary messages arrive decoded as UTF-8 text |
| `Closed` | The connection ended, from either side or after an error |
| `Error` | Something went wrong; `Closed` follows |

`socket.send(text)` returns `false` when the socket is not open. `socket.close()` closes it, and a `Closed` event follows.

## When answers arrive

Answers that arrived since the last frame are delivered at the start of the frame, before your `Update` systems run, in the order they arrived. A system with a single `HttpResponse` or `SocketEvent` parameter runs once per answer; a list parameter, `events: SocketEvent[]`, gets them all at once.

Networking needs the web build. On the desktop a request answers at once with `status` 0 and an `error` saying so, and a socket reports `Error` then `Closed`, so a game that handles a missing network runs there too.
