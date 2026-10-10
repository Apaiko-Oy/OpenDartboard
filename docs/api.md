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
  "confidence": 0.7,
  "uncertainty": 6.1,
  "boundary": 2.4,
  "boundary_kind": "wedge",
  "alternative": "D5",
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
| `uncertainty`     | `float`, `null`   | `0.0-`                                                   | One-sigma position uncertainty in board millimetres, measured **across** the boundary `boundary_kind` names; `null` where no millimetre position was measured |
| `boundary`        | `float`, `null`   | `0.0-`                                                   | Millimetres to the nearest boundary that could change this call; `null` with `uncertainty` |
| `boundary_kind`   | `string`, `null`  | `"ring"`, `"wedge"`                                      | Which kind of wire `boundary` measures to; `null` where nothing could flip the call |
| `alternative`     | `string`, `null`  | `"S1"-"D20"`, `"BULL"`, `"OUTER"`, `"MISS"`              | The **other** candidate score when this dart is flagged; `null` when it is not |
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

**A bull and an outer bull have no wedge in them at all**, and the log says a third thing about
those rather than either of the two above: `wedge not in this reading`. Their score comes from
the ring ellipses, the angular ruler is never asked, and `segment` is `null` accordingly. That
bears on `confidence` and not only on the log line, because the confidences count *cameras*:
`0.9` is two or more cameras agreeing and `0.7` is one standing alone, so a `0.9` on a `BULL`
or an `OUTER` is two cameras agreeing about a **ring** and says nothing about any camera's
orientation (#1489).

**An absence is `null`, never `0`.** `0.0` is a real angle and a real radius. `END` and `MISS`
carry `"segment": null, "ring": null, "board": null`; a bull carries `"segment": null` with a
ring and a radius, and `"angle": null` when the orientation is unknown.

### How close the call was, and what the other answer is

**A dart whose uncertainty reaches a scoring wire publishes anyway, and says so** (#1556). The
score is the **more probable candidate** and it arrives at the same instant it always did -
nothing waits for anybody. What is added is that the message can now say the call was close
and name the second candidate, so a client can ask a human instead of guessing.

`alternative` is the whole of the flag. It is a score string or `null`, and it is non-null
**exactly** when this dart is flagged: the detector measured the solved position's one-sigma
uncertainty **across** the nearest boundary that could change the call, found the boundary
inside it, and could read back what the board says on the other side. `score` is what
publishes; `alternative` is what a correction would say instead. A client that never reads
the field behaves exactly as it did before.

`confidence` carries the demotion and **no new value was invented for it**. `0.7` is what a
flagged geometric reading publishes at and `0.9` is what a clear one publishes at, which are
the two numbers this socket already published; a client must not read `0.7` as "flagged",
because a lone camera's string-vote reading is also a `0.7` and measures no millimetres at
all. The flag is `alternative`, and only `alternative`.

`uncertainty` and `boundary` are published **whether or not the dart is flagged**, because
"how close was this call" is a question worth answering when the answer is "not close". Both
are `null` together, on every reading whose position was not measured in board millimetres -
a `MISS`, an `END`, and every dart the string vote published. `boundary` measures to the
scoring edge the detector actually judges the ring at, which on a treble is corrected for
that camera's measured segmentation bloom (#1553), so the two numbers are comparable: the
systematic part is already spent and `uncertainty` is the statistical part alone.

`boundary_kind` says which kind of wire is the near one: `"ring"` if the call would change
band (a `T20` to an `S20`), `"wedge"` if it would change number (an `S19` to an `S7`). A
bull, an outer bull and a miss have no wedge in them at all, so only a ring wire can flip
them and `boundary_kind` is never `"wedge"` there.

**The flag rate is a real number and it is not small: 73% of published darts carry one.**
Measured over both ground-truthed fixtures in both calibration windows - 37 of 51 solved
darts, and `testers/i1556_census.py` reports it on every run. At a solved-position precision
of about six millimetres most darts genuinely sit within one sigma of some wire: a wedge is
31 mm of arc wide at the treble ring, so half of it is inside one sigma of a 6 mm instrument.
**A client that treats every flag as a question for a human asks on three darts in four, not
on one in eight**, and anything built on this field has to be designed for that.

**The flag is not a wrong-answer detector.** On the one fixture that can carry an accuracy
claim, every wrongly-scored dart the geometry published was flagged, in both calibration
windows independently - and so were 16 of the 30 correctly-scored ones. The honest reading of
the field is "this call was close", with `uncertainty` and `boundary` beside it, and what to
do about that is the client's decision. `alternative` is a question, never a correction: of
the two wrong darts, one's other candidate is exactly what was thrown and the other's is not.

**The push to Turnaus says the same thing more narrowly, and the difference is deliberate**
(#1366). A detection posted to `/api/v1/{autoscorer,casual}/detections` carries the same two
numbers as `board_radius` and `board_angle`, **both or neither** — that door refuses half a
polar position with a 422, and a 422 is not retried, so a dart sent with a radius and no angle
would be lost rather than stored without its place. So the bull with a ring, a radius and no
orientation, which this socket publishes as `"angle": null`, is posted with **no board fields
at all**; so is a `MISS`, and a takeout body stays `{}`. The two numbers are also rounded to
the four decimal places Turnaus keeps, and an angle that rounds to `360.0000` is posted as `0`,
because 360 degrees is 0 degrees. Nothing about what this socket publishes changed.

**The Turnaus body also carries `candidates` on every dart, and the socket does not** (#1721,
2026-10-01). On 2026-09-25 (#1628) the maintainer held this board's outward contract fixed: a
vote-path publish carries no millimetre uncertainty and no second candidate (#1556 rule 1).
On 2026-10-01 the maintainer asked for candidates with every dart, so the marking page's fix
row always has something to tap; that supersedes the 2026-09-25 decision **for the Turnaus
post body only**. A body posted to `/api/v1/{autoscorer,casual}/detections` may carry
`candidates`: at most three sector strings, best first, in Turnaus's spelling (`Bull`, `25`,
`None`, as `sector` is), never the dart's own place, and absent rather than `[]` when there is
none. Four would be refused with a 422 and cost the dart, so three is the cap. They are ranked
from what the scoring already knew - a flagged dart's other candidate first, then the other
cameras' readings, then the rings and wedges across the nearest wires - and
`src/detector/geometry/detection/dart_candidates.hpp` says what each kind of dart gets (an
asserted 20 is offered its rings and never the wedges beside it). This socket's payload is
exactly what the table above says; `candidates` is not in it.

### The rest of the board's account, posted with every dart (turnaus#1787)

**The Turnaus body carries the board's whole account of the dart, and the socket does
not** (turnaus#1787, 2026-10-10). The server (turnaus#1786) stores a correction beside what
the board said, so what the board said has to reach it: every number below is one the log
already prints about the dart -- the `path=` of I1555PUBLISH, the UNCERTAINTY sentence's
sigma and margin, "Geometric score ... from N intersecting constraint(s)", "Consensus
score ... from N cameras", LONE-WIRE's and RING-WIRE's margins -- posted rather than
re-derived. **The existing fields are byte for byte what they were**: a body is the keys
below added to #1366's, and a detector result that carries no account (`path` empty) still
builds exactly #1366's bytes (`testers/i1787_body_check.cpp` compares the literal). An
older server ignores keys it does not read.

A field is **present exactly when the board has the number, and absent otherwise** --
never `null`, never a nought, as #1366 spelled the position -- so a reader asks "is it
there" and never "is it real".

| Field          | Type           | Present                      | Meaning                                                                                                    |
| -------------- | -------------- | ---------------------------- | ---------------------------------------------------------------------------------------------------------- |
| `path`         | `string`       | every dart                   | `"geometry"` (the entry solve named it) or `"vote"` (a camera's score string did), as I1555PUBLISH's `path=` |
| `flagged`      | `bool`         | every dart                   | the call was close (#1556); `alternative` says what the other answer is                                    |
| `confidence`   | `number`       | every dart                   | the socket's `confidence`, two places                                                                      |
| `degraded`     | `bool`         | every dart                   | the geometry was asked, refused by name, and the vote published (#1555)                                   |
| `cameras_used` | `int[]`        | when a camera scored it      | **numbered from 1 as every log line numbers cameras**: the lines the solve intersected, or the one camera whose string the vote published. Absent on the MISS no camera scored |
| `crossing_deg` | `number`       | a two-line solve             | the two lines' crossing angle, which IS that solve's across-wire sigma (#1766)                              |
| `sigma_mm`     | `number`       | a geometric dart             | the socket's `uncertainty`: one sigma across the wire `wire_kind` names                                     |
| `margin_mm`    | `number`       | a geometric dart             | the socket's `boundary`: millimetres to that wire                                                          |
| `wire_kind`    | `string`       | a geometric dart             | `"ring"` or `"wedge"`                                                                                       |
| `agreeing`     | `int`          | `path` is `vote`             | how many cameras agreed on the string: 2 or more is the Consensus line, 1 a lone reading, 0 the MISS      |
| `lone_wire_mm` | `number`       | LONE-WIRE measured it        | the published lone reading's margin to the nearest wedge wire (#1628)                                      |
| `ring_wire_mm` | `number`       | RING-WIRE measured it        | the same reading's margin to the nearest ring wire (#1773; absent on a tree without that line)             |

A flagged two-line geometric dart therefore posts, beside #1366's keys and #1651's
`alternative` and #1721's `candidates`:

```json
{"path":"geometry","flagged":true,"confidence":0.7,"degraded":false,"cameras_used":[2,3],
 "crossing_deg":27.0,"sigma_mm":5.0,"margin_mm":4.4,"wire_kind":"ring"}
```

and a lone camera's vote dart `{"path":"vote","flagged":false,"confidence":0.7,"degraded":true,
"cameras_used":[2],"agreeing":1,"lone_wire_mm":7.8}`. A MISS carries `path`, `flagged`,
`confidence`, `degraded` and `agreeing` and nothing a camera never measured.

### The log, uploaded, and a corrected dart's frames, on request (turnaus#1787)

Two more things the board does for the server's record, both on the Turnaus client's push
thread, **never on the scoring thread**, and both only once the END's takeout and every
dart before it are delivered -- a takeout is never delayed behind a log post. Route names
and shapes are this board's proposal for turnaus#1786 to build to; the server half does not
exist yet, and a deployment without a route answers 404, which the board takes as "not
here" once and says once.

**The log upload.** A board started with `--log-file <path>` (or `--debug`, which sets one)
posts the lines written since its last post at every END, and the rest at shutdown:

```
POST /api/v1/autoscorer/log            (a club board; /api/v1/casual/log on a Contest)
Authorization: Bearer <the board's credential>
{"file":"darts-log.txt","offset":48213,"text":"[20:14:02.113][INFO][SCORER] - SCORE: T20 ...\n...","final":false}
```

- `file` is the log file's leaf name, `offset` the byte offset in that file the chunk
  begins at, `text` the bytes from there -- whole lines, at most 1 MiB per post, more posts
  while behind -- and `final` true on the shutdown chunk, which also carries the tail
  without waiting for its newline.
- **Idempotent by byte offset, and the server is the judge.** The server appends a chunk
  only when `offset` equals the length it already holds for that board and file, answering
  `202`. Any other offset is answered **`409`** with `{"data":{"length":<bytes it holds>}}`,
  and the board moves to that offset and continues -- forward after a restart (every
  process starts at offset 0 and is told where the server is on its first post), back
  after a lost answer (the server took the chunk, the board never heard; the re-post is
  refused rather than stored twice). The same bytes are therefore never kept twice and
  nothing is ever rewritten. `testers/i1787_upload_check.cpp` holds the bookkeeping,
  lost answers and restarts included.
- A post that does not get through (no server, 5xx, 429) is said **once** in the log and
  retried from the same offset at the next END; `401`, `403`, `404` and `422` end the upload
  for the run, said once, and never touch the darts. `TURNAUS: log upload: N bytes ...
  acknowledged` at shutdown says how far it got.
- Nothing is spooled: the file is the spool, append-only, and the offset is the cursor.

**A corrected dart's frames.** With `OD_KEEP_FRAMES=on` the board keeps the settled frames
of its last ten published darts (`OD_KEEP_FRAMES=25` keeps 25; docs/rig.md has the memory
measured). **Which route shape, and why: the board polls** rather than serving a route of
its own. The score socket exists and could carry a token-authorised `GET /frames/{reference}`
that Turnaus calls, but that works only when the server can reach the board, and a board in
a garage is behind NAT and a club's is on a wifi the server was never on; the client is
HTTP-out only today and stays so. So at every END, when the buffer is on, the board asks:

```
GET /api/v1/autoscorer/frame-requests        (/api/v1/casual/frame-requests on a Contest)
Authorization: Bearer <the board's credential>
-> 200 {"data":[{"reference":"01J9X..."}, ...]}     the darts the server wants pictures of
```

and answers each one:

```
POST /api/v1/autoscorer/frame-requests/{reference}
{"reference":"01J9X...","found":true,"window":45,
 "census":["I1512ENTRY window=45 ...","I1681CONTROL window=45 ..."],
 "frames":[{"camera":1,"png_base64":"iVBOR..."},{"camera":2,"png_base64":"..."},{"camera":3,"png_base64":"..."}]}
-> 202
```

or, for a dart no longer kept -- never kept, or pushed out by the ten after it --
`{"reference":"01J9X...","found":false}`. `window` is the detection window that called the
dart, `census` the `I1512`/`I1681`/`I1773` lines the scoring printed about that window
(present only when the census pin `OD_GEO_SCORE=on` printed them, as in the log),
`frames` one PNG per camera that brought a frame to the window, `camera` numbered from 1
as the log numbers them, grey, at the camera's own size. It costs one GET per END while the
buffer is on and nothing while it is off (a board with the buffer off never polls, so a
request to it simply ages out on the server -- turnaus#1786 calls the whole thing best
effort). The answer is the only thing that ever leaves the buffer; nothing about a
correction is kept on the board.

### Finding a board on the network

A board whose socket is open on the network - started with `--listen` - **announces itself
over mDNS** so an app can list the boards on the wifi and offer them by name. A board on
loopback only announces nothing, and a board that stops withdraws its announcement.

**An announcement means a socket.** A board that cannot see - its cameras did not open, or
it did not calibrate on the frames they gave - stays up and reports `ERROR`, and it never
opens the score socket. Such a board is **not announced even with `--listen`**, and it
removes any announcement an earlier run left, so nothing on the wifi ever names a score
socket that was not opened.

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
