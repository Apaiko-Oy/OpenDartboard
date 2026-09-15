#!/usr/bin/env python3
"""Board-position check for OpenDartboard (issue #1186).

One command. It runs the detector for a stated cycle budget against stated inputs,
subscribes to ws://127.0.0.1:<port>/scores?token=... like any third-party client would
- since #1187 every subscriber presents the board's token, and this check reads it from
the file the detector writes in its working directory - and asserts over every message
it receives:

  * a scoring dart (S/D/T n, BULL, OUTER) carries `segment`, `ring` and `board`, and
    the published polar position falls inside the segment and ring `score` names;
  * END and MISS carry explicit absences - null, never a zero - for all three;
  * every field the message carried before #1186 is still there with its old type.

    python3 tools/board_position/check_board_position.py --cycles 900 \
        --cams mocks/cam_1.mp4,mocks/cam_2.mp4,mocks/cam_3.mp4

It refuses to pass on a run that published no dart at all, because an assertion over
nothing is not a measurement. Python 3.8+, standard library only. The WebSocket client
is deliberately raw so the check reads the bytes the board puts on the wire and not
what a library makes of them.
"""

import argparse
import base64
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse

# The board's own radii in millimetres (perspective_processing::DartboardSpec), as
# fractions of the outer edge of the double ring - the same numbers the detector's
# radial ruler is marked in.
OUTER_DOUBLE_MM = 170.0
RING_BANDS = {
    "bull": (0.0, 6.35 / OUTER_DOUBLE_MM),
    "outer": (6.35 / OUTER_DOUBLE_MM, 15.9 / OUTER_DOUBLE_MM),
    "triple": (99.0 / OUTER_DOUBLE_MM, 107.0 / OUTER_DOUBLE_MM),
    "double": (162.0 / OUTER_DOUBLE_MM, 1.0),
}
SINGLE_BANDS = [
    (15.9 / OUTER_DOUBLE_MM, 99.0 / OUTER_DOUBLE_MM),
    (107.0 / OUTER_DOUBLE_MM, 162.0 / OUTER_DOUBLE_MM),
]
SEQUENCE = [20, 1, 18, 4, 13, 6, 10, 15, 2, 17, 3, 19, 7, 16, 8, 11, 14, 9, 12, 5]
EPS = 1e-4  # float32 on the wire, boundaries inclusive both ends


# ---------------------------------------------------------------- the wire

class RawWebSocket:
    """A text-frame reader over one socket. Handles a chunked upgrade body, because
    httplib streams the frames through a content provider."""

    def __init__(self, host, port, path="/scores", token=None):
        # #1187: the token goes in the query string, because a browser's WebSocket cannot
        # set a header. None presents nothing, which is how the refusal is measured.
        if token is not None:
            path = f"{path}?token={urllib.parse.quote(token, safe='')}"
        self.sock = socket.create_connection((host, port), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        request = (
            f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n"
            f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
        )
        self.sock.sendall(request.encode())
        self.raw = b""
        while b"\r\n\r\n" not in self.raw:
            piece = self.sock.recv(4096)
            if not piece:
                raise ConnectionError("closed during the upgrade")
            self.raw += piece
        head, self.raw = self.raw.split(b"\r\n\r\n", 1)
        status = head.split(b"\r\n", 1)[0].decode(errors="replace")
        if " 101 " not in status:
            raise ConnectionError(f"upgrade refused: {status}")
        self.chunked = b"transfer-encoding: chunked" in head.lower()
        self.body = b""  # de-chunked frame bytes
        self.sock.settimeout(1.0)

    def _fill(self):
        piece = self.sock.recv(65536)
        if not piece:
            raise EOFError
        self.raw += piece

    def _dechunk(self):
        """Move complete chunks from raw to body. Returns False when raw has no full chunk."""
        moved = False
        while True:
            end = self.raw.find(b"\r\n")
            if end < 0:
                return moved
            try:
                size = int(self.raw[:end].split(b";")[0], 16)
            except ValueError:
                raise ConnectionError("bad chunk header on the wire")
            if size == 0:
                raise EOFError
            if len(self.raw) < end + 2 + size + 2:
                return moved
            self.body += self.raw[end + 2:end + 2 + size]
            self.raw = self.raw[end + 2 + size + 2:]
            moved = True

    def _pump(self):
        if self.chunked:
            if not self._dechunk():
                self._fill()
                self._dechunk()
        else:
            self.body += self.raw
            self.raw = b""
            if not self.body:
                self._fill()
                self.body += self.raw
                self.raw = b""

    def frames(self):
        """Yield text payloads until the socket closes. Pings are answered by ignoring
        them; the board never expects a pong."""
        while True:
            while len(self.body) < 2:
                try:
                    self._pump()
                except socket.timeout:
                    continue
            opcode = self.body[0] & 0x0F
            length = self.body[1] & 0x7F
            offset = 2
            if length == 126:
                while len(self.body) < 4:
                    self._pump()
                length = int.from_bytes(self.body[2:4], "big")
                offset = 4
            elif length == 127:
                while len(self.body) < 10:
                    self._pump()
                length = int.from_bytes(self.body[2:10], "big")
                offset = 10
            while len(self.body) < offset + length:
                try:
                    self._pump()
                except socket.timeout:
                    continue
            payload = self.body[offset:offset + length]
            self.body = self.body[offset + length:]
            if opcode == 0x1:
                yield payload.decode("utf-8")
            elif opcode == 0x8:
                return


# ---------------------------------------------------------------- the assertions

def between(value, low, high):
    return low - EPS <= value <= high + EPS


def check_message(message):
    """Returns a list of failures for one message; empty means it held."""
    failures = []
    for field, kinds in (
        ("score", (str,)),
        ("confidence", (int, float)),
        ("camera", (int,)),
        ("processing_time", (int,)),
        ("timestamp", (int,)),
    ):
        if field not in message:
            failures.append(f"{field} missing - an existing field went away")
        elif isinstance(message[field], bool) or not isinstance(message[field], kinds):
            failures.append(f"{field} changed type: {type(message[field]).__name__}")
    position = message.get("position")
    if not isinstance(position, dict) or not all(
        isinstance(position.get(axis), int) and not isinstance(position.get(axis), bool) for axis in ("x", "y")
    ):
        failures.append("position.x/position.y are no longer two integers")
    for field in ("segment", "ring", "board"):
        if field not in message:
            failures.append(f"{field} missing")
    if failures:
        return failures

    score = message["score"]
    segment, ring, board = message["segment"], message["ring"], message["board"]

    if score in ("END", "MISS"):
        for name, value in (("segment", segment), ("ring", ring), ("board", board)):
            if value is not None:
                failures.append(f"{score} carries {name}={value!r}; an absence is null, never a value")
        return failures

    # A scoring dart: the string, the fields and the polar position must agree.
    if score == "BULL":
        want_ring, want_segment = "bull", None
    elif score == "OUTER":
        want_ring, want_segment = "outer", None
    elif score[:1] in "SDT" and score[1:].isdigit() and 1 <= int(score[1:]) <= 20:
        want_ring = {"S": "single", "D": "double", "T": "triple"}[score[0]]
        want_segment = int(score[1:])
    else:
        return [f"score {score!r} is outside the published vocabulary"]

    if ring != want_ring:
        failures.append(f"ring={ring!r} but score {score} names {want_ring}")
    if segment != want_segment:
        failures.append(f"segment={segment!r} but score {score} names {want_segment}")
    if not isinstance(board, dict):
        failures.append(f"board={board!r} on a scoring dart; expected radius and angle")
        return failures

    radius, angle = board.get("radius"), board.get("angle")
    if not isinstance(radius, (int, float)) or isinstance(radius, bool):
        failures.append(f"board.radius={radius!r} on a scoring dart")
    else:
        bands = SINGLE_BANDS if want_ring == "single" else [RING_BANDS[want_ring]]
        if not any(between(radius, low, high) for low, high in bands):
            failures.append(f"board.radius={radius:.4f} is outside the {want_ring} ring {bands}")
        if radius < -EPS or radius > 1 + EPS:
            failures.append(f"board.radius={radius:.4f} is outside [0, 1]")

    if want_segment is None:
        # A bull has no segment; the angle is a number where the orientation is known and
        # null where it is not, and either is allowed. A number must still be an angle.
        if angle is not None and (not isinstance(angle, (int, float)) or not (0 <= angle < 360)):
            failures.append(f"board.angle={angle!r} on a bull is neither null nor in [0, 360)")
        return failures

    if not isinstance(angle, (int, float)) or isinstance(angle, bool):
        failures.append(f"board.angle={angle!r} but score {score} names a segment")
        return failures
    if not (0 <= angle < 360):
        failures.append(f"board.angle={angle:.4f} is outside [0, 360)")
    centre = 18.0 * SEQUENCE.index(want_segment)
    delta = (angle - centre + 180.0) % 360.0 - 180.0
    if abs(delta) > 9.0 + EPS:
        failures.append(
            f"board.angle={angle:.4f} is {abs(delta):.2f} degrees from the middle of the {want_segment}, "
            f"which spans 9 either side"
        )
    return failures


# ---------------------------------------------------------------- the run

def read_token(path, deadline):
    """The detector writes its token before it listens; wait for the file, then read it."""
    while time.time() < deadline:
        try:
            with open(path) as handle:
                token = handle.read().strip()
            if token:
                return token
        except OSError:
            pass
        time.sleep(0.25)
    return None


def collect(host, port, sink, stop, connect_timeout, token=None):
    """Connect - retrying while the board is still calibrating - and read until the
    socket closes or `stop` is set."""
    deadline = time.time() + connect_timeout
    ws = None
    last_error = "no attempt"
    while ws is None and time.time() < deadline and not stop.is_set():
        try:
            ws = RawWebSocket(host, port, token=token)
        except (OSError, ConnectionError) as error:
            last_error = repr(error)
            time.sleep(0.25)
    if ws is None:
        sink.append(("error", f"never connected to the score socket (last: {last_error})"))
        return
    sink.append(("connected", ws.chunked))
    try:
        for text in ws.frames():
            sink.append(("message", text))
            if stop.is_set():
                break
    except (EOFError, OSError, ConnectionError) as error:
        sink.append(("closed", repr(error)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default="build/opendartboard")
    parser.add_argument("--cams", required=True)
    parser.add_argument("--cycles", type=int, default=900)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--port", type=int, default=13520)
    parser.add_argument("--workdir", default=None, help="fresh directory for the run; a temp dir by default")
    parser.add_argument("--expect-darts", type=int, default=None, help="fail unless exactly this many SCORE messages arrive")
    parser.add_argument("--connect-timeout", type=float, default=180.0)
    parser.add_argument("--run-timeout", type=float, default=900.0)
    parser.add_argument("--token", default=None, help="the board's token; read from <workdir>/score_token by default")
    args = parser.parse_args()

    workdir = args.workdir or tempfile.mkdtemp(prefix="board-position-")
    os.makedirs(workdir, exist_ok=True)
    binary = os.path.abspath(args.binary)
    cams = ",".join(os.path.abspath(c) for c in args.cams.split(","))
    env = dict(os.environ, OD_MAX_CYCLES=str(args.cycles))
    stdout = open(os.path.join(workdir, "detector.out"), "wb")
    stderr = open(os.path.join(workdir, "detector.err"), "wb")
    proc = subprocess.Popen(
        [binary, "--debug", "--cams", cams, "--width", str(args.width), "--height", str(args.height)],
        cwd=workdir, env=env, stdout=stdout, stderr=stderr,
    )

    token_path = os.path.join(workdir, "score_token")
    token = args.token if args.token is not None else read_token(token_path, time.time() + args.connect_timeout)
    events = []
    stop = threading.Event()
    reader = threading.Thread(
        target=collect, args=("127.0.0.1", args.port, events, stop, args.connect_timeout, token), daemon=True
    )
    reader.start()
    try:
        proc.wait(timeout=args.run_timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    stop.set()
    reader.join(timeout=5)

    print("board-position check v1")
    print(f"binary   {binary}")
    print(f"cams     {cams}")
    print(f"cycles   {args.cycles}   workdir {workdir}   detector exit {proc.returncode}")
    connected = [e for e in events if e[0] == "connected"]
    print(f"socket   {'connected (chunked upgrade body)' if connected and connected[0][1] else 'connected' if connected else 'NEVER CONNECTED'}")
    print(f"token    {'given on the command line' if args.token is not None else f'read from {token_path}' if token else f'NOT FOUND at {token_path}'}, presented as ?token=")

    messages = []
    for kind, payload in events:
        if kind == "message":
            try:
                messages.append(json.loads(payload))
            except json.JSONDecodeError:
                messages.append({"_unparseable": payload})
        elif kind == "error":
            print(f"error    {payload}")

    failed = 0
    darts = 0
    for index, message in enumerate(messages, 1):
        if "_unparseable" in message:
            failed += 1
            print(f"  {index:2d}  FAIL  not JSON: {message['_unparseable']!r}")
            continue
        failures = check_message(message)
        score = message.get("score")
        if score not in ("END", "MISS"):
            darts += 1
        board = message.get("board")
        summary = (
            f"{score!s:5} segment={message.get('segment')!s:4} ring={message.get('ring')!s:6} "
            f"board={'null' if board is None else ('r=%.4f angle=%s' % (board.get('radius', float('nan')), 'null' if board.get('angle') is None else '%.3f' % board['angle']))}"
            f"  pixels=({message.get('position', {}).get('x')},{message.get('position', {}).get('y')}) camera={message.get('camera')}"
        )
        if failures:
            failed += 1
            print(f"  {index:2d}  FAIL  {summary}")
            for failure in failures:
                print(f"            - {failure}")
        else:
            print(f"  {index:2d}  ok    {summary}")

    print(f"messages {len(messages)}   scoring darts {darts}   failed {failed}")
    verdict = "PASS"
    if not connected:
        verdict = "FAIL: the check never subscribed, so it measured nothing"
    elif darts == 0:
        verdict = "FAIL: no scoring dart was published, so nothing was asserted over"
    elif args.expect_darts is not None and darts != args.expect_darts:
        verdict = f"FAIL: {darts} scoring darts where {args.expect_darts} were expected"
    elif failed:
        verdict = f"FAIL: {failed} message(s) did not hold"
    print(verdict)
    return 0 if verdict == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
