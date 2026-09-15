# OpenDartboard API 🎯

Real-time dart scoring via WebSocket and HTTP REST API.

## WebSocket Endpoint (Primary)

```
ws://<ip-adress>:13520/scores?token=<token>
```

### Where it listens, and who may subscribe

The socket listens on **loopback only** (`127.0.0.1:13520`) unless the detector is started
with **`--listen`**, which opens it on every interface (`0.0.0.0:13520`). The startup banner
and the log line `WebSocket server listening on ws://...` say which.

Every subscriber presents the board's **token** as the `token` query parameter on the
upgrade request - on loopback as on the network, so a local tool and a phone use one path.
It goes in the query string because a browser's `WebSocket` cannot set a header. An upgrade
without it, or with a wrong one, is refused with **`401 Unauthorized`** and one log line
naming the address it came from; the token itself is never logged.

The token is generated on the detector's first run and kept in `score_token` in its working
directory, mode `0600` (`--token-file <path>` moves it). It is printed by

```
opendartboard --show-token
```

and nowhere else. Hand it to the app that will subscribe; there is one token per board.

The REST routes below share the listener, so `--listen` puts them on the network as well.
They carry no credential; nothing in this document changes that.

### Several subscribers, and what each one is promised

Several clients may subscribe to one board at once - a marking screen and a phone beside it,
say - and each is promised the same three things. **How many** is bounded by the web server:
every connection, a subscriber or a REST request, is served on one thread of a fixed pool of
`max(8, hardware threads - 1)` - **eight** on a Raspberry Pi 4 - and a subscriber keeps its
thread for as long as it is subscribed. A connection beyond the pool is not refused; its
upgrade waits unanswered until a thread frees, and while every thread is held by a subscriber
the REST routes wait too.

- **Every subscriber receives every message, in the order it was published**, from the
  moment its upgrade is accepted. Nothing is replayed: a subscriber that connects, or
  reconnects, receives what is published from then on and not what it missed while it was
  away. If a client needs the earlier darts it keeps them itself.
- **A subscriber that stops reading is dropped after a bounded wait, and the wait is
  logged.** The board pings every subscriber every **30 seconds** and expects a pong within
  **10 seconds** (a browser's `WebSocket` answers a ping on its own, and so do the libraries in
  the examples below; a hand-rolled client must). A subscriber that has not answered is
  dropped - so one that stops reading is gone **within 41 seconds** of doing so: the 30 and the
  10, and the tenth of a second the board takes to look - and so is one whose socket does not
  accept a frame within **5 seconds**. Each drop is one warning line naming the peer, the wait,
  the reason, and how many messages were delivered and how many were still owed. A subscriber
  that closes, with a close frame or by closing its socket, is let go within a tenth of a
  second, and it holds its thread until then.
- **The others are never delayed by it.** Every subscriber has its own outbox and its own
  writer; the board's publishing loop puts a message on every outbox and never waits for a
  socket. A subscriber that has stopped taking messages blocks nothing but itself.

Subscribing changes nothing about what the board detects or publishes: the socket is a
reader of the score stream, not a participant in it.

When the board stops, each subscriber is sent a close frame.

### Real-time Score Streaming

The **main feature** - connects and receives live dart scores as JSON messages in real-time.

**Example Score Message:**

```json
{
  "score": "D20",
  "segment": 20,
  "ring": "double",
  "board": { "radius": 0.97, "angle": 3.4 },
  "position": { "x": 150, "y": 200 },
  "confidence": 0.95,
  "camera": 0,
  "processing_time": 15,
  "timestamp": 1699123456789
}
```

### Field Contract

| Field             | Type              | Range                                                    | Description                                                   |
| ----------------- | ----------------- | -------------------------------------------------------- | ------------------------------------------------------------- |
| `score`           | `string`          | `"S1"-"D20"`, `"BULL"`, `"OUTER"`, `"MISS"`, `"END"`     | Dart score value                                              |
| `segment`         | `integer`, `null` | `1-20`                                                   | The number the dart is in; `null` on a bull, a miss and `END` |
| `ring`            | `string`, `null`  | `"single"`, `"double"`, `"triple"`, `"bull"`, `"outer"`  | The ring the dart is in; `null` on a miss and `END`           |
| `board.radius`    | `float`           | `0.0-1.0`                                                | Distance from the bull centre, `1.0` at the outer edge of the double ring |
| `board.angle`     | `float`, `null`   | `0.0-360.0`                                              | Degrees clockwise from the vertical through the middle of the 20 |
| `board`           | `object`, `null`  |                                                          | `null` on a miss and `END`                                    |
| `position.x`      | `integer`         | `0-XXX`                                                  | X coordinate in pixels, in the frame of `camera`              |
| `position.y`      | `integer`         | `0-XXX`                                                  | Y coordinate in pixels, in the frame of `camera`              |
| `confidence`      | `float`           | `0.0-1.0`                                                | Detection confidence                                          |
| `camera`          | `integer`         | `0-2`                                                    | Camera index                                                  |
| `processing_time` | `integer`         | `1-1000`                                                 | Processing time in ms                                         |
| `timestamp`       | `integer`         | Unix timestamp                                           | Message timestamp in ms                                       |

### Where the dart is

`segment`, `ring` and `board` say where the dart is **on the board**, so a client can draw it
on its own picture of a board without knowing which camera saw it. `position` says where the
dart is **in a picture**: it is, and always was, in the pixels of the one camera `camera`
names - the camera whose reading the board published - and it is kept for the developer's
debug views. Nothing can be drawn from it without that camera's calibration.

`segment` and `ring` are the decision `score` is composed from, stated as fields; a client
never needs to parse the string. `board` is polar: `radius` is `0.0` at the centre of the bull
and `1.0` at the outer edge of the double ring, so the rings lie at the board's own radii -
the bull to `0.037`, the outer bull to `0.094`, the triple ring from `0.582` to `0.629`, the
double ring from `0.953` to `1.0`; `angle` is degrees clockwise from the vertical through the
middle of the 20, so the 20 spans `351`-`9`, the 1 spans `9`-`27`, and every segment is 18
degrees wide, in the order `20 1 18 4 13 6 10 15 2 17 3 19 7 16 8 11 14 9 12 5`.

The position is derived from the calibration of the camera `camera` names - its ring ellipses
as a radial ruler and its wires as an angular ruler - and from the same decision that produced
`score`, so it always falls inside the segment and ring the score names. It is a rectification
against that camera's calibration, not a measurement against a second camera, and it is only
as good as that calibration: a wire-to-wire fraction is read in image angle. Where the camera
that scored has no orientation - the detector's own log says `wedge by default` - the 20 is the
segment the detector asserts, and `angle` says where in that wedge the dart is.

**An absence is `null`, never `0`.** `0.0` is a real angle and a real radius. `END` and `MISS`
carry `"segment": null, "ring": null, "board": null`; a bull carries `"segment": null` with a
ring and a radius, and `"angle": null` when the orientation is unknown.

### Finding a board on the network

A board whose socket is open on the network - started with `--listen` - **announces itself
over mDNS** so an app can list the boards on the wifi and offer them by name. A board on
loopback only announces nothing, and a board that stops withdraws its announcement.

| What | Value |
| --- | --- |
| Service type | `_opendartboard._tcp` (browse `_opendartboard._tcp.local.`) |
| Instance name | the board's **label** |
| Port (SRV) | `13520`, the score socket |
| TXT `path` | `/scores` |
| TXT `label` | the label again, unescaped, for resolvers that mangle instance names |
| TXT `version` | the detector's version |
| TXT `auth` | `token` - the upgrade needs `?token=`, which the announcement never carries |

**The label** is `--label <name>`, or the board's hostname when that is not given. Give each
board in a club its own (`--label "Kello 1"`): two boards announcing one label leave it to
the responder to rename one of them, and the new name says nothing a person can use.

**How it is announced.** The detector writes an Avahi service file,
`/etc/avahi/services/opendartboard.service` (`--announce-dir <dir>` moves it), when it opens
the socket with `--listen`, and removes it when it stops or starts without `--listen`; the
host's `avahi-daemon` publishes what is in that directory, so the host must run it
(package `avahi-daemon`). A board that cannot write the file logs `not announced:` once and runs anyway. A
detector killed outright (`SIGKILL`, a crash) leaves the file behind until its next start.
**Windows is not announced**: it has no Avahi, and announcing would need a library the
binary does not carry. Use the setup view's QR there.

**When several boards answer**, a client:

- lists every board it resolved, **by label**, and lets the person pick one;
- never connects to more than one board on its own, and never picks one silently - not
  even when only one answers, because the one that answered may not be the board in front
  of the player;
- asks for that board's token (the setup view's QR, or `--show-token`) - a token belongs to
  one board, and the announcement names which board is which, not who may subscribe;
- treats an announcement as a hint: the address and port are where to try, and the `401`
  or the subscription is the answer.

### The setup view: a QR code a phone scans

```
opendartboard --setup [--label <name>] [--setup-address <address>]
```

prints the board's label, the socket address with the token elided, and a **QR code drawn
in the terminal** that encodes the whole subscription URL,

```
ws://<address>:13520/scores?token=<token>
```

so a phone camera pointed at the screen reads exactly the URL this document describes.
The token is inside the QR only; it is not printed beside it. `<address>` is the board's
first non-loopback IPv4 address when `--setup` runs; `--setup-address` names another on a
board with more than one. The URL is the same with or without `--listen`, but the socket
answers there only when the detector runs with `--listen`, and the view says so. The QR is
produced by a vendored encoder (`src/third_party/qrcodegen`, MIT) and needs no network.

## Simple Client Example

### JavaScript

```javascript
// the token is what `opendartboard --show-token` printed on the board
const socket = new WebSocket(`ws://<ip-adress>:13520/scores?token=${encodeURIComponent(token)}`);

socket.onmessage = function (event) {
  const score = JSON.parse(event.data);
  console.log(
    `Score: ${score.score} at (${score.position.x}, ${score.position.y})`
  );
  if (score.board) {
    // where on the board, for a picture of your own: radius 0..1, angle clockwise from the 20
    console.log(`On the board: r=${score.board.radius} angle=${score.board.angle}`);
  }
};

socket.onopen = () => console.log("Connected to OpenDartboard");
socket.onclose = () => console.log("Disconnected");
```

### Python

```python
import websocket
import json

def on_message(ws, message):
    score = json.loads(message)
    print(f"Score: {score['score']} at ({score['position']['x']}, {score['position']['y']})")

from urllib.parse import quote
token = "..."  # what `opendartboard --show-token` printed on the board
ws = websocket.WebSocketApp(f"ws://<ip-adress>:13520/scores?token={quote(token, safe='')}", on_message=on_message)
ws.run_forever()
```

### iOS (Swift)

```swift
import Foundation
import Starscream

class DartboardClient: WebSocketDelegate {
    var socket: WebSocket!

    init(token: String) {
        // the token is what `opendartboard --show-token` printed on the board
        var components = URLComponents(string: "ws://<ip-address>:13520/scores")!
        components.queryItems = [URLQueryItem(name: "token", value: token)]
        var request = URLRequest(url: components.url!)
        socket = WebSocket(request: request)
        socket.delegate = self
        socket.connect()
    }

    func didReceive(event: WebSocketEvent, client: WebSocket) {
        switch event {
        case .text(let text):
            if let data = text.data(using: .utf8),
               let scoreData = try? JSONDecoder().decode(ScoreData.self, from: data) {
                print("Score: \(scoreData.score) at (\(scoreData.position.x), \(scoreData.position.y))")
            }
        case .connected:
            print("Connected to OpenDartboard")
        case .disconnected(let reason, let code):
            print("Disconnected: \(reason)")
        default:
            break
        }
    }
}

struct ScoreData: Codable {
    let score: String
    let position: Position
    let confidence: Double
    let camera: Int
    let processing_time: Int
    let timestamp: Int64
}

struct Position: Codable {
    let x: Int
    let y: Int
}
```

### Android (Kotlin)

```kotlin
import okhttp3.*
import org.json.JSONObject

class DartboardClient : WebSocketListener() {
    private var webSocket: WebSocket? = null

    fun connect(token: String) {
        // the token is what `opendartboard --show-token` printed on the board
        val client = OkHttpClient()
        val url = HttpUrl.Builder()
            .scheme("http").host("<ip-address>").port(13520)
            .addPathSegment("scores")
            .addQueryParameter("token", token)
            .build()
        val request = Request.Builder()
            .url(url)
            .build()
        webSocket = client.newWebSocket(request, this)
    }

    override fun onMessage(webSocket: WebSocket, text: String) {
        try {
            val scoreData = JSONObject(text)
            val score = scoreData.getString("score")
            val position = scoreData.getJSONObject("position")
            val x = position.getInt("x")
            val y = position.getInt("y")

            println("Score: $score at ($x, $y)")
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    override fun onOpen(webSocket: WebSocket, response: Response) {
        println("Connected to OpenDartboard")
    }

    override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
        println("Disconnected: $reason")
    }

    override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
        println("Connection failed: ${t.message}")
    }
}

// Usage
val client = DartboardClient()
client.connect(token)
```

---

## HTTP REST API (Secondary)

### Health Check

```
GET http://<ip-adress>:13520/health
```

**Response:**

```json
{
  "status": "ok",
  "service": "OpenDartboard"
}
```

### Configuration Management

#### [WIP] Get Current Configuration

```
GET http://<ip-adress>:13520/config
```

**Response:**

```json
{
  "key": "value"
}
```

#### [WIP] Update Configuration

```
PUT http://<ip-adress>:13520/config
```

```json
{
  "key": "value"
}
```

**Response:**

```json
{
  "status": "updated"
}
```

### Calibration Control

#### [WIP] Start Calibration Process

```
POST http://<ip-adress>:13520/calibrate/start
```

**Response:**

```json
{
  "status": "started"
}
```

#### Get Calibration Status

```
GET http://<ip-adress>:13520/calibrate/status
```

**Response:**

```json
{
  "calibrating": true
}
```

## Error Handling

### WebSocket Errors

- **Connection Failed**: Check if OpenDartboard is running - and, from another device, that it was started with `--listen`; without it the socket is loopback only
- **401 Unauthorized on the upgrade**: no `token` query parameter, or a wrong one; `opendartboard --show-token` on the board prints the right one
- **Connection Lost**: Implement exponential backoff reconnection; a reconnecting subscriber receives what is published from then on, nothing is replayed
- **Dropped after 10-40 seconds while the app was in the background**: the board pings every 30 seconds and drops a subscriber that does not pong within 10; a client that cannot answer while backgrounded should reconnect when it returns
- **Invalid JSON**: Parse errors in client code

### HTTP Errors

- **400 Bad Request**: Invalid JSON in PUT/POST requests
- **401 Unauthorized**: A WebSocket upgrade on `/scores` without the token
- **404 Not Found**: Endpoint doesn't exist
- **500 Internal Error**: Server-side issue

## Test Clients

- **REST API**: Use cURL commands above or Postman/Insomnia

## Performance Notes

- **WebSocket**: No rate limiting, handle high-frequency dart detections
- **Ping frames**: Sent every 30 seconds; a subscriber that does not pong within 10 seconds is dropped, and a stalled subscriber never delays the others
- **Message bursts**: Possible during rapid scoring sequences
- **Reconnection**: Client responsibility for WebSocket reconnection
