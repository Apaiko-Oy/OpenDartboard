#!/usr/bin/env python3
"""Subscriber check for OpenDartboard (issue #1188).

One command, two detector runs on the same inputs at the same cycle budget.

Run 1, two readers: subscribers A and B both subscribe before the first dart. A reads
to the end. B closes its socket the moment it has read the first END and subscribes
again at once. The check asserts A received the reference stream in order with nothing
dropped or duplicated; that B's first subscription is a prefix of it and its second a
suffix of it that overlaps the first nowhere - so what a reconnecting subscriber gets
is what is published from then on, and nothing is replayed.

Run 2, one dead subscriber: A reads to the end; C accepts the upgrade with a small
receive buffer and never reads again, never answering a ping. The check measures
from its own clock when the board closed C's connection, reads the board's log line
for the drop and its stated wait, and asserts both are inside the bound the board
promises (ping interval + pong wait). A's stream must be the reference stream, and
the delivery latency of A's messages - the check's receive time against the
board's own `timestamp` field, one clock - is printed for both runs and held under
--latency-bound in the run where C was stalled.

Both runs: the SCORE lines in the detector's log and the ensure_calls figure on the
cycle-budget line must agree between the runs and with the reference, so subscribing,
disconnecting and being dropped changed nothing about what the board published.

    python3 tools/score_socket/check_subscribers.py \\
        --cams mocks/cam_1.mp4,mocks/cam_2.mp4,mocks/cam_3.mp4

Python 3.8+, standard library only, Linux (it reads TCP_INFO). The WebSocket client
is the raw one from tools/board_position, so the bytes read are the bytes on the wire.
"""

import argparse
import json
import os
import re
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "board_position"))
from check_board_position import RawWebSocket, read_token  # noqa: E402

# The ten messages of the reference control at 1100 cycles - the control envelope's
# stream, which every earlier document's eight are the 900-cycle prefix of.
REFERENCE = [
    ("S20", 625, 356), ("S20", 828, 226), ("S20", 667, 214), ("END", -1, -1),
    ("S20", 408, 411), ("S20", 491, 446), ("D20", 376, 447), ("END", -1, -1),
    ("S20", 559, 280), ("S20", 522, 458),
]
REFERENCE_ENSURE_CALLS = {1100: 3411, 900: 2790}

# The bounds the board states (websocket_service.cpp): a ping every 30 s, a pong
# within 10 s of it. A subscriber that stops reading is gone within their sum.
PING_INTERVAL = 30.0
PONG_WAIT = 10.0
DROP_BOUND = PING_INTERVAL + PONG_WAIT

TCP_CLOSE_WAIT, TCP_CLOSE = 8, 7


def tcp_state(sock):
    """The kernel's state for this socket; CLOSE_WAIT once the far end has closed."""
    info = sock.getsockopt(socket.IPPROTO_TCP, socket.TCP_INFO, 8)
    return struct.unpack("B", info[:1])[0]


class Run:
    def __init__(self, binary, cams, width, height, workdir, cycles, label):
        self.workdir = workdir
        os.makedirs(workdir, exist_ok=True)
        env = dict(os.environ, OD_MAX_CYCLES=str(cycles))
        argv = [binary, "--debug", "--cams", cams, "--width", str(width), "--height", str(height)]
        self.stdout_path = os.path.join(workdir, f"detector-{label}.out")
        self.stdout = open(self.stdout_path, "wb")
        self.proc = subprocess.Popen(argv, cwd=workdir, env=env, stdout=self.stdout, stderr=subprocess.STDOUT)

    def wait(self, timeout):
        try:
            self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.proc.send_signal(signal.SIGINT)
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.stdout.close()
        return self.proc.returncode

    def log_text(self):
        with open(self.stdout_path, errors="replace") as handle:
            return handle.read()


def subscribe(port, token, deadline, rcvbuf=None):
    """Connect - retrying while the board is still coming up - or return None."""
    last = None
    while time.time() < deadline:
        try:
            if rcvbuf is None:
                return RawWebSocket("127.0.0.1", port, token=token)
            # A receive buffer the client sets before connecting is the window it
            # advertises; the kernel's floor is a few kilobytes, which is the point.
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
            sock.settimeout(5)
            sock.connect(("127.0.0.1", port))
            ws = RawWebSocket.__new__(RawWebSocket)
            ws.sock = sock
            _upgrade(ws, port, token)
            return ws
        except (OSError, ConnectionError) as error:
            last = error
            time.sleep(0.25)
    print(f"  never subscribed: {last!r}")
    return None


def _upgrade(ws, port, token):
    """The upgrade request the raw client sends, on a socket built by the caller."""
    import base64
    import urllib.parse
    path = f"/scores?token={urllib.parse.quote(token, safe='')}"
    key = base64.b64encode(os.urandom(16)).decode()
    request = (
        f"GET {path} HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n"
        f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
    )
    ws.sock.sendall(request.encode())
    ws.raw = b""
    while b"\r\n\r\n" not in ws.raw:
        piece = ws.sock.recv(4096)
        if not piece:
            raise ConnectionError("closed during the upgrade")
        ws.raw += piece
    head, ws.raw = ws.raw.split(b"\r\n\r\n", 1)
    status = head.split(b"\r\n", 1)[0].decode(errors="replace")
    if " 101 " not in status:
        raise ConnectionError(f"upgrade refused: {status}")
    ws.chunked = b"transfer-encoding: chunked" in head.lower()
    ws.body = b""


class Reader(threading.Thread):
    """A subscriber that reads everything it is sent until the board closes, recording
    the wall-clock time each message was read. `until` stops it early, after that
    many messages, by closing the socket without a word - a phone whose app died."""

    def __init__(self, name, port, token, deadline, until=None, on_first=None):
        super().__init__(daemon=True)
        self.label, self.port, self.token, self.deadline, self.until, self.on_first = name, port, token, deadline, until, on_first
        self.received = []  # (read_at, message)
        self.connected_at = None
        self.peer = None
        self.closed_by = None

    def run(self):
        ws = subscribe(self.port, self.token, self.deadline)
        if ws is None:
            return
        self.connected_at = time.time()
        self.peer = "%s:%d" % ws.sock.getsockname()
        try:
            for text in ws.frames():
                self.received.append((time.time(), json.loads(text)))
                if len(self.received) == 1 and self.on_first:
                    self.on_first.set()
                if self.until is not None and len(self.received) >= self.until:
                    self.closed_by = "us"
                    ws.sock.close()
                    return
            self.closed_by = "board (close frame)"
        except (EOFError, OSError, ConnectionError) as error:
            self.closed_by = f"board ({error!r})"


class DeadSubscriber(threading.Thread):
    """Accepts the upgrade and never reads again: no pong, no close, nothing. Watches
    the kernel's state for the socket so the moment the board closed it is known
    without reading a byte."""

    def __init__(self, port, token, deadline, after):
        super().__init__(daemon=True)
        self.port, self.token, self.deadline, self.after = port, token, deadline, after
        self.connected_at = self.closed_at = None
        self.peer = None
        self.state = None

    def run(self):
        self.after.wait(timeout=max(0.0, self.deadline - time.time()))
        ws = subscribe(self.port, self.token, self.deadline, rcvbuf=4096)
        if ws is None:
            return
        self.connected_at = time.time()
        self.peer = "%s:%d" % ws.sock.getsockname()
        while time.time() < self.deadline:
            self.state = tcp_state(ws.sock)
            if self.state in (TCP_CLOSE_WAIT, TCP_CLOSE):
                self.closed_at = time.time()
                break
            time.sleep(0.1)
        ws.sock.close()


def summary(messages):
    return [(m.get("score"), m.get("position", {}).get("x"), m.get("position", {}).get("y")) for _, m in messages]


def latencies(messages):
    return [read_at - m["timestamp"] / 1000.0 for read_at, m in messages]


def score_lines(log):
    return re.findall(r"SCORE: (\S+) \| Position: \((-?\d+),(-?\d+)\)", log)


def ensure_calls(log):
    match = re.search(r"cycle budget reached: cycles=(\d+) loop_ms=(\d+) ensure_calls=(\d+)", log)
    return (int(match.group(1)), int(match.group(2)), int(match.group(3))) if match else None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default="build/opendartboard")
    parser.add_argument("--cams", required=True)
    parser.add_argument("--cycles", type=int, default=1100, help="the reference's ten messages arrive by 1100")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--port", type=int, default=13520)
    parser.add_argument("--workdir", default=None)
    parser.add_argument("--connect-timeout", type=float, default=180.0)
    parser.add_argument("--run-timeout", type=float, default=900.0)
    parser.add_argument("--latency-bound", type=float, default=1.0, help="seconds; A's worst delivery latency while C is stalled")
    args = parser.parse_args()

    workdir = args.workdir or tempfile.mkdtemp(prefix="subscribers-")
    binary = os.path.abspath(args.binary)
    cams = ",".join(os.path.abspath(c) for c in args.cams.split(","))
    port = args.port
    reference = REFERENCE if args.cycles >= 1100 else REFERENCE[:8]

    print("subscribers check v1")
    print(f"binary   {binary}")
    print(f"cams     {cams}")
    print(f"cycles   {args.cycles}   workdir {workdir}   port {port}   drop bound {DROP_BOUND:.0f} s (ping {PING_INTERVAL:.0f} s + pong {PONG_WAIT:.0f} s)")

    results = []

    def record(label, expected, got, detail=""):
        ok = str(expected) == str(got)
        results.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'}  {label:64} expected {str(expected):10} got {str(got):10} {detail}")

    # ---------------------------------------------------------------- run 1
    print("run 1: two readers, B reconnects after the first END")
    run = Run(binary, cams, args.width, args.height, os.path.join(workdir, "run1"), args.cycles, "two-readers")
    token = read_token(os.path.join(workdir, "run1", "score_token"), time.time() + args.connect_timeout)
    if token is None:
        print("FAIL: no token, nothing to present")
        run.wait(0)
        return 1
    deadline = time.time() + args.connect_timeout
    first_end = reference.index(("END", -1, -1)) + 1
    a1 = Reader("A", port, token, deadline)
    b1 = Reader("B", port, token, deadline, until=first_end)
    a1.start()
    b1.start()
    b1.join(timeout=args.run_timeout)
    b2 = Reader("B'", port, token, time.time() + args.connect_timeout)
    reconnected_at = time.time()
    b2.start()
    exit1 = run.wait(args.run_timeout)
    a1.join(timeout=10)
    b2.join(timeout=10)
    log1 = run.log_text()

    a1_seen, b1_seen, b2_seen = summary(a1.received), summary(b1.received), summary(b2.received)
    for label, seen in (("A", a1_seen), ("B, first subscription", b1_seen), ("B, second subscription", b2_seen)):
        print(f"    {label}: " + " ".join(f"{s}@({x},{y})" for s, x, y in seen))
    record("A received the reference stream, in order", reference, a1_seen)
    record("A received no duplicate", len(set(m["timestamp"] for _, m in a1.received)), len(a1.received))
    record(f"B's first subscription is the reference's first {first_end}", reference[:first_end], b1_seen)
    record("B's second subscription is a suffix of the reference", True, bool(b2_seen) and reference[-len(b2_seen):] == b2_seen, f"{len(b2_seen)} message(s)")
    first_ts = {m["timestamp"] for _, m in b1.received}
    record("B's second subscription replays nothing from its first", 0, sum(1 for _, m in b2.received if m["timestamp"] in first_ts))
    missed = len(reference) - len(b1_seen) - len(b2_seen)
    print(f"    B reconnected {reconnected_at - (b1.received[-1][0] if b1.received else reconnected_at):.3f} s after closing; "
          f"published in the gap and so never received by B: {missed}")
    record("the log says B closed its connection", 1, len(re.findall(r"subscriber " + re.escape(b1.peer or "?") + r" disconnected after [\d.]+ s: the subscriber closed the connection", log1)))
    record("nothing was dropped in run 1", 0, len(re.findall(r"subscriber \S+ dropped after", log1)))
    lat1 = latencies(a1.received)
    print(f"    A's delivery latency, run 1: max {max(lat1):.3f} s, median {sorted(lat1)[len(lat1) // 2]:.3f} s over {len(lat1)}" if lat1 else "    A received nothing")
    scores1, budget1 = score_lines(log1), ensure_calls(log1)
    print(f"    detector exit {exit1}; budget line {budget1}")

    # ---------------------------------------------------------------- run 2
    print("run 2: A reads, C accepts the upgrade after A's first message and never reads again")
    run = Run(binary, cams, args.width, args.height, os.path.join(workdir, "run2"), args.cycles, "one-dead")
    token = read_token(os.path.join(workdir, "run2", "score_token"), time.time() + args.connect_timeout)
    deadline = time.time() + args.connect_timeout
    first = threading.Event()
    a2 = Reader("A", port, token, deadline, on_first=first)
    c = DeadSubscriber(port, token, deadline + args.run_timeout, after=first)
    a2.start()
    c.start()
    exit2 = run.wait(args.run_timeout)
    a2.join(timeout=10)
    c.join(timeout=DROP_BOUND + 15)
    log2 = run.log_text()

    a2_seen = summary(a2.received)
    print(f"    A: " + " ".join(f"{s}@({x},{y})" for s, x, y in a2_seen))
    record("A received the reference stream, in order, beside a dead subscriber", reference, a2_seen)
    record("C subscribed", True, c.connected_at is not None, c.peer or "")
    measured = (c.closed_at - c.connected_at) if (c.connected_at and c.closed_at) else None
    record(f"C's connection was closed by the board within {DROP_BOUND:.0f} s (kernel state, our clock)", True,
           measured is not None and measured <= DROP_BOUND + 2.0, f"{measured:.1f} s" if measured is not None else f"never closed; state {c.state}")
    drop = re.search(r"subscriber " + re.escape(c.peer or "?") + r" dropped after ([\d.]+) s: (.*)", log2)
    record("the log says C was dropped, and why", True, drop is not None, drop.group(2) if drop else "no drop line for C")
    record(f"the logged wait is within {DROP_BOUND:.0f} s", True, drop is not None and float(drop.group(1)) <= DROP_BOUND + 2.0, f"{drop.group(1)} s" if drop else "")
    record("the reason is the missing pong", True, drop is not None and "no pong within" in drop.group(2))
    record("A was not dropped and did not disconnect early", "board (close frame)", a2.closed_by)
    lat2 = latencies(a2.received)
    if lat2:
        during = [l for (read_at, _), l in zip(a2.received, lat2) if c.connected_at and read_at >= c.connected_at and (c.closed_at is None or read_at <= c.closed_at)]
        print(f"    A's delivery latency, run 2: max {max(lat2):.3f} s, median {sorted(lat2)[len(lat2) // 2]:.3f} s over {len(lat2)}; "
              f"while C was stalled: {len(during)} message(s), max {max(during):.3f} s" if during else
              f"    A's delivery latency, run 2: max {max(lat2):.3f} s over {len(lat2)}; no message fell inside C's stall")
        record(f"A's worst delivery latency while C was stalled is under {args.latency_bound:.1f} s", True,
               bool(during) and max(during) < args.latency_bound, f"{max(during):.3f} s" if during else "nothing measured")
    scores2, budget2 = score_lines(log2), ensure_calls(log2)
    print(f"    detector exit {exit2}; budget line {budget2}")

    # ---------------------------------------------------------------- the outbound side
    print("both runs: what the detector itself scored")
    want = [(s, str(x), str(y)) for s, x, y in reference]
    record("run 1's SCORE lines are the reference", want, scores1)
    record("run 2's SCORE lines are the reference", want, scores2)
    record("ensure_calls agree between the runs", budget1[2] if budget1 else None, budget2[2] if budget2 else None)
    if args.cycles in REFERENCE_ENSURE_CALLS:
        record(f"ensure_calls at {args.cycles} cycles is the reference's", REFERENCE_ENSURE_CALLS[args.cycles], budget2[2] if budget2 else None)

    failed = results.count(False)
    print(f"checks {len(results)}   failed {failed}")
    verdict = "PASS" if failed == 0 else f"FAIL: {failed} check(s) did not hold"
    print(verdict)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
