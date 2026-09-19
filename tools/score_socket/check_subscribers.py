#!/usr/bin/env python3
"""Subscriber check for OpenDartboard (issue #1188).

One command, two detector runs on the same inputs at the same cycle budget.

Run 1, two readers: subscribers A and B both subscribe before the first dart. A reads
to the end. B closes its socket the moment it has read the first END and subscribes
again at once. The check asserts A received the reference stream in order with nothing
dropped or duplicated; that B's first subscription is a prefix of it and its second a
suffix of it that overlaps the first nowhere - so what a reconnecting subscriber gets
is what is published from then on, and nothing is replayed. While A and B are
subscribed it opens as many upgrades at once as httplib has pool threads and counts
how many are admitted, and whether the rest are admitted once those close.

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

With --turnaus-stub, each run also pairs the board with #822's stub
(testers/turnaus_stub.py) before it starts, and asserts on the outbound side: what the
board posted to the stub in each run is the reference - a detection per scoring dart,
a takeout per END, in order, none absorbed, none refused, each with its own reference -
and run 1's pushes are run 2's. So subscribing, reconnecting and being dropped change
nothing about what the board pushes to a server, measured on the push itself. Without
the flag the check is the 22 it always was.

    python3 tools/score_socket/check_subscribers.py --turnaus-stub \\
        --cams mocks/cam_1.mp4,mocks/cam_2.mp4,mocks/cam_3.mp4

Python 3.8+, standard library only, Linux (it reads TCP_INFO). The WebSocket client
is the raw one from tools/board_position, so the bytes read are the bytes on the wire.

#1282 added three options so that the SAME check can be run against a board that is not
a local Linux process, and specifically against the Windows build. The check itself still
runs on Linux -- it is the CLIENT that reads TCP_INFO, and its sockets are this machine's
whatever the board is. Every default is what it was, so a run that passes none of them is
byte for byte the run this file has always made:

    --host            where the board is. Default 127.0.0.1.
    --token-file      where the token is, when the board does not keep it beside the run.
    --detector-arg    passed on to the detector, repeatable (--listen, for one).
    --cams            a path that is already absolute FOR THE BOARD is left alone. Only
                      C:\\... is recognised as that, so a POSIX path is resolved exactly
                      as before.

    WSLENV=OD_MAX_CYCLES python3 tools/score_socket/check_subscribers.py \
        --binary /mnt/c/od-run/opendartboard.exe --host 172.25.0.1 --detector-arg --listen \
        --cams 'C:\\od-run\\mocks\\cam_1.mp4,C:\\od-run\\mocks\\cam_2.mp4,C:\\od-run\\mocks\\cam_3.mp4'
"""

import argparse
import base64
import fcntl
import json
import os
import re
import select
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import termios
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
# 1100 cycles read 3411 until #817 (54ef303112d7d3f80072a636fda826948fafe78f), whose
# commit says: "ensure_calls moves 3411 -> 3412: setFileLogging's one directory call,
# which nothing used to make." The ten messages are unchanged; the figure is the
# control's on any tree with #817 beneath it.
REFERENCE_ENSURE_CALLS = {1100: 3412, 900: 2790}

# The bounds the board states (websocket_service.cpp): a ping every 30 s, a pong
# within 10 s of it. A subscriber that stops reading is gone within their sum.
PING_INTERVAL = 30.0
PONG_WAIT = 10.0
DROP_BOUND = PING_INTERVAL + PONG_WAIT

TCP_CLOSE_WAIT, TCP_CLOSE = 8, 7

STUB_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "testers", "turnaus_stub.py")


class TurnausStub:
    """#822's stub for one run: started, the board paired against it, stopped, read.

    The PIN is the stub's own and single-use per stub process, so every run gets a
    fresh stub, a fresh transcript and a fresh credential file in its own workdir. The
    pairing's output goes to a file and is never printed: the stub's credential is a
    fixture, but the rule for a credential is the same whoever minted it."""

    PIN = "483920"

    def __init__(self, binary, workdir, port):
        os.makedirs(workdir, exist_ok=True)
        self.transcript = os.path.join(workdir, "turnaus-transcript.jsonl")
        self.credentials = os.path.join(workdir, "turnaus-credentials.json")
        self.url = f"http://127.0.0.1:{port}"
        env = dict(os.environ, STUB_PORT=str(port), STUB_TRANSCRIPT=self.transcript)
        self.stderr = open(os.path.join(workdir, "turnaus-stub.err"), "wb")
        self.proc = subprocess.Popen([sys.executable, os.path.abspath(STUB_SCRIPT)], env=env,
                                     stdout=subprocess.DEVNULL, stderr=self.stderr)
        deadline = time.time() + 15
        while time.time() < deadline:
            try:
                socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
                break
            except OSError:
                time.sleep(0.1)
        with open(os.path.join(workdir, "turnaus-pair.out"), "wb") as out:
            paired = subprocess.run([binary, "--pair", self.PIN, "--turnaus", self.url, "--allow-plaintext",
                                     "--credentials", self.credentials],
                                    cwd=workdir, stdout=out, stderr=subprocess.STDOUT, timeout=60)
        self.paired = paired.returncode == 0

    def detector_args(self):
        return ["--turnaus", self.url, "--allow-plaintext", "--credentials", self.credentials]

    def stop(self):
        self.proc.terminate()
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        self.stderr.close()

    def events(self):
        try:
            with open(self.transcript) as handle:
                return [json.loads(line) for line in handle if line.strip()]
        except OSError:
            return []


def pushes(events):
    """What the board posted, in the order the stub received it."""
    out = []
    for event in events:
        if event.get("event") in ("counted", "absorbed"):
            out.append((event["event"], event.get("sector")))
        elif event.get("event") == "takeout":
            out.append(("takeout", None))
    return out


def client_stopped(log):
    """(queued, delivered, attempts, dropped, still_owed) from #822's last summary line, or None."""
    found = re.findall(r"client stopped\. queued=(\d+) delivered=(\d+) attempts=(\d+) dropped=(\d+) still_owed=(\d+)", log)
    return tuple(int(v) for v in found[-1]) if found else None


def tcp_state(sock):
    """The kernel's state for this socket; CLOSE_WAIT once the far end has closed."""
    info = sock.getsockopt(socket.IPPROTO_TCP, socket.TCP_INFO, 8)
    return struct.unpack("B", info[:1])[0]


class Run:
    def __init__(self, binary, cams, width, height, workdir, cycles, label, extra_args=()):
        self.workdir = workdir
        os.makedirs(workdir, exist_ok=True)
        env = dict(os.environ, OD_MAX_CYCLES=str(cycles))
        argv = [binary, "--debug", "--cams", cams, "--width", str(width), "--height", str(height)] + list(extra_args)
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


def subscribe(port, token, deadline, rcvbuf=None, host="127.0.0.1"):
    """Connect - retrying while the board is still coming up - or return None."""
    last = None
    while time.time() < deadline:
        try:
            if rcvbuf is None:
                return RawWebSocket(host, port, token=token)
            # A receive buffer the client sets before connecting is the window it
            # advertises; the kernel's floor is a few kilobytes, which is the point.
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
            sock.settimeout(5)
            sock.connect((host, port))
            ws = RawWebSocket.__new__(RawWebSocket)
            ws.sock = sock
            _upgrade(ws, port, token, host)
            return ws
        except (OSError, ConnectionError) as error:
            last = error
            time.sleep(0.25)
    print(f"  never subscribed: {last!r}")
    return None


def _upgrade(ws, port, token, host="127.0.0.1"):
    """The upgrade request the raw client sends, on a socket built by the caller."""
    import base64
    import urllib.parse
    path = f"/scores?token={urllib.parse.quote(token, safe='')}"
    key = base64.b64encode(os.urandom(16)).decode()
    request = (
        f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n"
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

    def __init__(self, name, port, token, deadline, until=None, on_first=None, host="127.0.0.1"):
        super().__init__(daemon=True)
        self.label, self.port, self.token, self.deadline, self.until, self.on_first = name, port, token, deadline, until, on_first
        self.host = host
        self.received = []  # (read_at, message)
        self.connected_at = None
        self.peer = None
        self.closed_by = None

    def run(self):
        ws = subscribe(self.port, self.token, self.deadline, host=self.host)
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

    def __init__(self, port, token, deadline, after, host="127.0.0.1"):
        super().__init__(daemon=True)
        self.port, self.token, self.deadline, self.after = port, token, deadline, after
        self.host = host
        self.connected_at = self.closed_at = None
        self.peer = None
        self.state = None
        self.queued = None  # bytes the kernel held for C, unread, when the board closed it

    def run(self):
        self.after.wait(timeout=max(0.0, self.deadline - time.time()))
        ws = subscribe(self.port, self.token, self.deadline, rcvbuf=4096, host=self.host)
        if ws is None:
            return
        self.connected_at = time.time()
        self.peer = "%s:%d" % ws.sock.getsockname()
        while time.time() < self.deadline:
            self.state = tcp_state(ws.sock)
            if self.state in (TCP_CLOSE_WAIT, TCP_CLOSE):
                self.closed_at = time.time()
                self.queued = struct.unpack("i", fcntl.ioctl(ws.sock.fileno(), termios.FIONREAD, b"\0\0\0\0"))[0]
                break
            time.sleep(0.1)
        ws.sock.close()


def probe_capacity(port, token, attempts, wait, host="127.0.0.1"):
    """httplib serves every connection on a thread from a fixed pool, and a subscriber
    keeps its thread for as long as it is subscribed. Opens `attempts` upgrades at once
    and returns (peers, answered at once, answered once those had closed): how many
    subscribers the board admits beside the ones already there, and whether the rest
    are admitted when a thread frees rather than refused."""
    socks = [socket.create_connection((host, port), timeout=5) for _ in range(attempts)]
    peers = ["%s:%d" % s.getsockname() for s in socks]
    for s in socks:
        key = base64.b64encode(os.urandom(16)).decode()
        s.sendall((f"GET /scores?token={token} HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n"
                   f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())

    def answered(pending, seconds):
        heads, admitted = {s: b"" for s in pending}, []
        end = time.time() + seconds
        while pending and time.time() < end:
            ready, _, _ = select.select(pending, [], [], max(0.0, end - time.time()))
            for s in ready:
                piece = s.recv(4096)
                heads[s] += piece
                if not piece or b"\r\n" in heads[s]:
                    pending = [p for p in pending if p is not s]
                    if b" 101 " in heads[s].split(b"\r\n", 1)[0]:
                        admitted.append(s)
        return admitted

    at_once = answered(list(socks), wait)
    for s in at_once:
        s.close()
    later = answered([s for s in socks if s not in at_once], wait + 2)
    time.sleep(0.5)
    for s in socks:
        s.close()
    return peers, len(at_once), len(later)


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
    parser.add_argument("--turnaus-stub", action="store_true", help="pair each run with #822's stub and assert on what the board pushed")
    parser.add_argument("--stub-port", type=int, default=8899)
    parser.add_argument("--host", default="127.0.0.1", help="#1282: where the board is, when it is not this process's loopback")
    parser.add_argument("--token-file", default=None,
                        help="#1282: read the token here rather than in each run's workdir. A Windows board keeps "
                             "its token in AppData, not beside the run, so the path has to be said.")
    parser.add_argument("--detector-arg", action="append", default=[], help="#1282: passed on to the detector, repeatable")
    args = parser.parse_args()

    workdir = args.workdir or tempfile.mkdtemp(prefix="subscribers-")
    binary = os.path.abspath(args.binary)
    # #1282: a path that is already absolute FOR THE BOARD is left alone. Only a drive
    # letter counts as that, so every POSIX path is resolved exactly as it always was --
    # os.path.abspath would otherwise glue this process's cwd onto C:\... and hand the
    # board a name nothing can open.
    cams = ",".join(c if re.match(r"^[A-Za-z]:[\\/]", c) else os.path.abspath(c) for c in args.cams.split(","))
    host = args.host
    port = args.port
    reference = REFERENCE if args.cycles >= 1100 else REFERENCE[:8]

    print("subscribers check v1")
    print(f"binary   {binary}")
    print(f"cams     {cams}")
    if host != "127.0.0.1" or args.detector_arg:
        print(f"board    {host}:{port}   detector args {' '.join(args.detector_arg) or '(none)'}")
    print(f"cycles   {args.cycles}   workdir {workdir}   port {port}   drop bound {DROP_BOUND:.0f} s (ping {PING_INTERVAL:.0f} s + pong {PONG_WAIT:.0f} s)")

    results = []

    def record(label, expected, got, detail=""):
        ok = str(expected) == str(got)
        results.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'}  {label:64} expected {str(expected):10} got {str(got):10} {detail}")

    # ---------------------------------------------------------------- run 1
    print("run 1: two readers, B reconnects after the first END")
    stub1 = TurnausStub(binary, os.path.join(workdir, "run1"), args.stub_port) if args.turnaus_stub else None
    run = Run(binary, cams, args.width, args.height, os.path.join(workdir, "run1"), args.cycles, "two-readers",
              list(stub1.detector_args() if stub1 else ()) + args.detector_arg)
    token = read_token(args.token_file or os.path.join(workdir, "run1", "score_token"), time.time() + args.connect_timeout)
    if token is None:
        print("FAIL: no token, nothing to present")
        run.wait(0)
        return 1
    deadline = time.time() + args.connect_timeout
    first_end = reference.index(("END", -1, -1)) + 1
    first1 = threading.Event()
    a1 = Reader("A", port, token, deadline, on_first=first1, host=host)
    b1 = Reader("B", port, token, deadline, until=first_end, host=host)
    a1.start()
    b1.start()
    # How many subscribers one board holds at once: httplib 0.14.3's pool is
    # max(8, hardware threads - 1), and A and B already hold two of it.
    pool = max(8, (os.cpu_count() or 1) - 1)
    probe_peers = []
    if first1.wait(timeout=args.connect_timeout + args.run_timeout):
        probe_peers, at_once, later = probe_capacity(port, token, pool, 3.0, host=host)
        record(f"subscribers admitted at once beside A and B (pool of {pool} threads)", pool - 2, at_once)
        record("the ones left waiting are admitted once those close, not refused", 2, later)
    b1.join(timeout=args.run_timeout)
    b2 = Reader("B'", port, token, time.time() + args.connect_timeout, host=host)
    reconnected_at = time.time()
    b2.start()
    exit1 = run.wait(args.run_timeout)
    if stub1:
        stub1.stop()
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
    record("nothing was dropped in run 1", 0, len([p for p in re.findall(r"subscriber (\S+) dropped after", log1) if p not in probe_peers]))
    lat1 = latencies(a1.received)
    print(f"    A's delivery latency, run 1: max {max(lat1):.3f} s, median {sorted(lat1)[len(lat1) // 2]:.3f} s over {len(lat1)}" if lat1 else "    A received nothing")
    scores1, budget1 = score_lines(log1), ensure_calls(log1)
    print(f"    detector exit {exit1}; budget line {budget1}")

    # ---------------------------------------------------------------- run 2
    print("run 2: A reads, C accepts the upgrade after A's first message and never reads again")
    stub2 = TurnausStub(binary, os.path.join(workdir, "run2"), args.stub_port) if args.turnaus_stub else None
    run = Run(binary, cams, args.width, args.height, os.path.join(workdir, "run2"), args.cycles, "one-dead",
              list(stub2.detector_args() if stub2 else ()) + args.detector_arg)
    token = read_token(args.token_file or os.path.join(workdir, "run2", "score_token"), time.time() + args.connect_timeout)
    deadline = time.time() + args.connect_timeout
    first = threading.Event()
    a2 = Reader("A", port, token, deadline, on_first=first, host=host)
    c = DeadSubscriber(port, token, deadline + args.run_timeout, after=first, host=host)
    a2.start()
    c.start()
    exit2 = run.wait(args.run_timeout)
    if stub2:
        stub2.stop()
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
    print(f"    bytes the kernel was holding for C, unread, when the board closed it: {c.queued}")
    record("A was not dropped and did not disconnect early", "board (close frame)", a2.closed_by)
    lat2 = latencies(a2.received)
    if lat2:
        during = [l for (read_at, _), l in zip(a2.received, lat2) if c.connected_at and read_at >= c.connected_at and (c.closed_at is None or read_at <= c.closed_at)]
        print(f"    A's delivery latency, run 2: max {max(lat2):.3f} s, median {sorted(lat2)[len(lat2) // 2]:.3f} s over {len(lat2)}; "
              f"while C was stalled: {len(during)} message(s), max {max(during):.3f} s" if during else
              f"    A's delivery latency, run 2: max {max(lat2):.3f} s over {len(lat2)}; no message fell inside C's stall")
        record(f"A's worst delivery latency while C was stalled is under {args.latency_bound:.1f} s", True,
               bool(during) and max(during) < args.latency_bound, f"{max(during):.3f} s" if during else "nothing measured")
        if lat1:
            record("A's worst latency beside C is within 0.1 s of run 1's, where there was no C", True,
                   max(lat2) <= max(lat1) + 0.1, f"run 2 {max(lat2):.3f} s, run 1 {max(lat1):.3f} s")
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

    if args.turnaus_stub:
        # #822: the push to a server, which #1188's issue asked about and this fork did
        # not have when the check was written. Run 1 is the churn - B closing and coming
        # back, eight probes opening and closing - and run 2 is the dead subscriber held
        # until the pong rule drops it. Neither may change what the board posts.
        print("both runs: what the board pushed to the server (#822, against testers/turnaus_stub.py)")
        want_push = [("takeout", None) if s == "END" else ("counted", s) for s, _, _ in reference]
        pushed = {}
        for n, stub, log in ((1, stub1, log1), (2, stub2, log2)):
            events = stub.events()
            pushed[n] = pushes(events)
            stopped = client_stopped(log)
            counted_refs = [e.get("reference") for e in events if e.get("event") == "counted"]
            print(f"    run {n}: " + " ".join(f"{kind}:{sector}" if sector else kind for kind, sector in pushed[n]))
            print(f"    run {n}: client stopped (queued, delivered, attempts, dropped, still_owed) = {stopped}")
            record(f"run {n}: the board paired with the stub", True, stub.paired)
            record(f"run {n}: what the board pushed is the reference, in order", want_push, pushed[n])
            record(f"run {n}: no push was absorbed, refused or sent anywhere unknown", 0,
                   sum(1 for e in events if e.get("event") in ("absorbed", "answer_lost", "pairing_refused", "unknown_path")))
            record(f"run {n}: every detection carried its own reference", len(counted_refs), len(set(counted_refs)))
            record(f"run {n}: the client dropped nothing and owed nothing at exit", "0 0",
                   f"{stopped[3]} {stopped[4]}" if stopped else "no summary line")
        record("run 1's pushes are run 2's", pushed[1], pushed[2])

    failed = results.count(False)
    print(f"checks {len(results)}   failed {failed}")
    verdict = "PASS" if failed == 0 else f"FAIL: {failed} check(s) did not hold"
    print(verdict)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
