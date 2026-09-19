#!/usr/bin/env python3
"""#1366: the dart's place on the board, from the seam that writes it to the wire.

Three cases, and they are three different questions rather than one asked three ways.

  seam    testers/i1366_position_check.cpp: what a body may say, held against #1365's
          rules quoted as the server spells them. Pure -- no client, no socket, no spool.
  spool   THE CRITERION A HAPPY PATH CANNOT PROVE. #822 rule 3 writes a body once, when
          the dart is offered, and posts those same bytes for ever. So a position added
          at POST time is a position every dart thrown while the network was down loses,
          and a run against a reachable server would look perfect. Here three placed
          darts are offered at a CLOSED PORT, the bytes on the disk are read, and a
          SECOND PROCESS over the same spool delivers them to a stub that says what
          arrived.
  rig     mocks/rig-20260918, the real binary, #822's stub: every pushed body held to
          the BOARD log line the same detection printed.

Runs inside od-amd64:bullseye with the worktree at /app and a Linux build in /app/build:
  python3 /app/testers/i1366_position_check.py [case ...]
Exit code 0 only when every check passed. Prints `checks N   failed M`.
"""
import json
import os
import re
import socket
import subprocess
import sys
import time

APP = os.environ.get("OD_APP", "/app")
BIN = os.environ.get("OD_BIN", APP + "/build/opendartboard")
STUB = APP + "/testers/turnaus_stub.py"
WORK = os.environ.get("CHECK_WORK", "/tmp/i1366-check")
RIG = ",".join(APP + "/mocks/rig-20260918/cam_%d.mp4" % i for i in (1, 2, 3))
RIG_CYCLES = int(os.environ.get("OD_RIG_CYCLES", "2000"))

CLUB = "483920"
ANSI = re.compile(r"\x1b\[[0-9;]*m")

# What score_processing prints for every result it publishes, once each. `to_string(float)`
# gives six places; a body carries the four the columns keep, so the comparison rounds.
BOARD_LINE = re.compile(
    r"BOARD: (?P<wedge>wedge measured|wedge by default) \| ring=(?P<ring>[^|]*)\| "
    r"segment=(?P<segment>-?\d+) \| radius=(?P<radius>none|-?[0-9.]+) \| angle=(?P<angle>none|-?[0-9.]+)")

# The four decimal places #1365 keeps, in the one spelling this file uses for both sides.
PLACES = 4

checks = 0
failures = []


def check(ok, name):
    global checks
    checks += 1
    print(("PASS " if ok else "FAIL ") + name, flush=True)
    if not ok:
        failures.append(name)
    return ok


def at_places(value):
    """The four places a body carries, as a float, for comparing with a log line."""
    return round(float(value), PLACES)


class Stub:
    def __init__(self, case, port, **env):
        self.dir = os.path.join(WORK, case)
        os.makedirs(self.dir, exist_ok=True)
        self.transcript = os.path.join(self.dir, "transcript.jsonl")
        e = dict(os.environ)
        e.update({"STUB_PORT": str(port), "STUB_TRANSCRIPT": self.transcript,
                  "STUB_SAMPLE_SECONDS": "0", "STUB_INTERVAL_SECONDS": "5",
                  "STUB_SILENCE_SECONDS": "60"})
        e.update({k: str(v) for k, v in env.items()})
        self.err = open(os.path.join(self.dir, "stub.err"), "w")
        self.proc = subprocess.Popen([sys.executable, STUB], env=e, stderr=self.err, stdout=self.err)
        deadline = time.time() + 10
        while time.time() < deadline:
            try:
                socket.create_connection(("127.0.0.1", port), timeout=0.2).close()
                return
            except OSError:
                time.sleep(0.1)
        raise RuntimeError("stub did not listen")

    def events(self, name=None):
        out = []
        try:
            with open(self.transcript) as fh:
                for line in fh:
                    if line.strip():
                        ev = json.loads(line)
                        if name is None or ev["event"] == name:
                            out.append(ev)
        except FileNotFoundError:
            pass
        return out

    def stop(self):
        self.proc.terminate()  # its own pid, nothing else
        self.proc.wait(timeout=10)
        self.err.close()


class Board:
    """One board's HOME: its credential, its spool, and every process run against them."""

    def __init__(self, case, port):
        self.case = case
        self.port = port
        self.dir = os.path.join(WORK, case)
        self.cwd = os.path.join(self.dir, "cwd")
        os.makedirs(self.cwd, exist_ok=True)
        self.config = os.path.join(self.dir, "config", "opendartboard")
        os.makedirs(self.config, exist_ok=True)
        self.credentials = os.path.join(self.config, "credentials.json")
        self.spool = os.path.join(self.config, "owed.jsonl")
        self.env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": self.dir,
                    "XDG_CONFIG_HOME": os.path.join(self.dir, "config")}
        self.text = ""

    def pair(self, flag, code, port=None):
        out = os.path.join(self.dir, "pair.out")
        with open(out, "w") as fh:
            return subprocess.run([BIN, flag, code, "--turnaus",
                                   "http://127.0.0.1:%d" % (port or self.port), "--allow-plaintext"],
                                  cwd=self.cwd, env=self.env, stdin=subprocess.DEVNULL,
                                  stdout=fh, stderr=subprocess.STDOUT, timeout=60).returncode

    def run(self, cycles, cams, label="run"):
        env = dict(self.env)
        env["OD_MAX_CYCLES"] = str(cycles)
        out = os.path.join(self.dir, label + ".out")
        err = os.path.join(self.dir, label + ".err")
        with open(out, "w") as o, open(err, "w") as e:
            rc = subprocess.run([BIN, "--debug", "--cams", cams, "--width", "1280", "--height", "720",
                                 "--allow-plaintext"],
                                cwd=self.cwd, env=env, stdin=subprocess.DEVNULL,
                                stdout=o, stderr=e, timeout=1800).returncode
        body = ""
        for path in (out, err):
            with open(path) as fh:
                body += ANSI.sub("", fh.read())
        self.text = body
        return rc

    def harness(self, binary, mode, port=None, label=None):
        """Run the #1366 client harness over THIS board's config dir."""
        out = os.path.join(self.dir, (label or mode) + ".out")
        with open(out, "w") as fh:
            rc = subprocess.run([binary, "http://127.0.0.1:%d" % (port or self.port),
                                 self.credentials, mode],
                                cwd=self.cwd, env=self.env, stdin=subprocess.DEVNULL,
                                stdout=fh, stderr=subprocess.STDOUT, timeout=300).returncode
        with open(out) as fh:
            return rc, ANSI.sub("", fh.read())

    def spool_records(self):
        out = []
        try:
            with open(self.spool) as fh:
                for line in fh:
                    if line.strip():
                        out.append(json.loads(line))
        except FileNotFoundError:
            pass
        return out


def compile_harness(case, source, binary_name):
    """Build one of #1366's harnesses against THIS tree's own client."""
    binary = os.path.join(WORK, case, binary_name)
    os.makedirs(os.path.dirname(binary), exist_ok=True)
    line = (
        "g++ -std=c++17 -I {app}/src -I {app}/src/utils "
        "-I {app}/build/_deps/nlohmann_json-src/include -I {app}/build/_deps/httplib-src "
        "$(pkg-config --cflags opencv4 2>/dev/null) "
        "{app}/testers/{src} {app}/src/communication/turnaus_client.cpp -o {bin} "
        "$(pkg-config --libs opencv4 2>/dev/null) -lpthread"
    ).format(app=APP, src=source, bin=binary)
    built = subprocess.run(line, shell=True, capture_output=True, text=True, timeout=900)
    if not check(built.returncode == 0, "%s: %s compiles (%s)"
                 % (case, source, built.stderr.strip().splitlines()[-1:] or "no output")):
        return None
    return binary


def position_in(body):
    """The pair a body carries, or None. Half a pair is neither -- it is a 422."""
    has_radius = "board_radius" in body and body["board_radius"] is not None
    has_angle = "board_angle" in body and body["board_angle"] is not None
    if has_radius != has_angle:
        return "half"
    if not has_radius:
        return None
    return (at_places(body["board_radius"]), at_places(body["board_angle"]))


def case_seam():
    """The pure seam, which is where every byte of a body is decided."""
    binary = compile_harness("seam", "i1366_position_check.cpp", "position_check")
    if binary is None:
        return
    run = subprocess.run([binary], capture_output=True, text=True, timeout=300)
    print(run.stdout, flush=True)
    tail = [l for l in run.stdout.splitlines() if l.startswith("checks ")]
    check(run.returncode == 0, "seam: every check at the body-building seam passed (%s)"
          % (tail[-1] if tail else "no summary at all"))


def case_spool():
    """A dart delivered after a restart keeps its position.

    The body is written ONCE, at the offer, and posted for ever. So this is asked of the
    bytes on the disk -- written by a process that never reached a server -- and then of
    what a second process delivers out of them. A position put in at POST time would leave
    the spool file clean and still arrive at a reachable stub: the first half of this case
    is the only place that difference is visible.
    """
    port = 18661
    gone = 18670  # nothing has ever listened here
    stub = Stub("spool", port)
    board = Board("spool", port)
    check(board.pair("--pair", CLUB) == 0, "spool: the board pairs to its club")

    # ONE server for both halves, and the board's network goes away rather than the
    # server. Stopping the stub instead would re-pair against a second stub process,
    # which mints a new club token generation and answers the board's own credential
    # with a 401 -- measured, and it is #1259's rule working rather than this case's
    # subject. What is wanted here is a board that cannot reach a Turnaus that is fine.

    binary = compile_harness("spool", "i1366_spool_check.cpp", "spool_check")
    if binary is None:
        return

    rc, text = board.harness(binary, "offer", port=gone)
    check(rc == 0, "spool: three placed darts and a takeout were offered at a closed port (rc=%s)" % rc)
    check("delivered=0" in text, "spool: and nothing was delivered, because nothing was listening")

    # ---- THE ASSERTION, made against the disk ----------------------------------------
    records = board.spool_records()
    detections = [r for r in records if r["path"].endswith("/detections")]
    check(len(detections) == 3, "spool: three detections are on the disk (%d records, %d detections)"
          % (len(records), len(detections)))

    # The three the harness offers, named here so the sweep is by VALUE and not only by key.
    wanted = {"T20": (0.6123, 12.3456), "D18": (0.9876, 201.5), "Bull": (0.0212, 44.25)}
    on_disk = {}
    for record in detections:
        body = json.loads(record["body"])
        on_disk[body.get("sector")] = position_in(body)
    check(on_disk == wanted,
          "spool: EVERY position is in the bytes on the disk, by key and by value -- a position "
          "added at POST time would leave this file clean (%s)" % on_disk)

    takeouts = [r for r in records if r["path"].endswith("/takeouts")]
    check(len(takeouts) == 1 and takeouts[0]["body"] == "{}",
          "spool: and a takeout body is still exactly {} (%s)" % [t["body"] for t in takeouts])

    # ---- and then of what a SECOND PROCESS delivers out of them ------------------------
    rc, text = board.harness(binary, "resume", label="resume")
    check(rc == 0, "spool: a second process over the same spool delivers the round (rc=%s)" % rc)

    counted = stub.events("counted")
    delivered = {}
    for event in counted:
        delivered[event["sector"]] = position_in(event["body"])
    check(delivered == wanted,
          "spool: and what ARRIVED after the restart carries every position, by key and by value (%s)"
          % delivered)
    check(len(stub.events("takeout")) == 1,
          "spool: the takeout that closes the round arrives too, so this was a whole round")
    stub.stop()


def case_rig():
    """mocks/rig-20260918, the real binary, and the BOARD lines held to what was pushed.

    One BOARD line is printed for every result score_processing publishes, and every one
    of those is offered, so the two sequences are the same sequence read at two ends of
    the program. What is asked of each pair is #1366's whole contract: a known position
    reaches the body at four places, an unknown one reaches it as NO KEY AT ALL rather
    than as a nought, a MISS carries none whatever it knew, and half a position is never
    sent.
    """
    port = 18662
    stub = Stub("rig", port)
    board = Board("rig", port)
    check(board.pair("--pair", CLUB) == 0, "rig: the board pairs to its club")
    rc = board.run(RIG_CYCLES, RIG)
    check(rc == 0, "rig: the run ends normally (rc=%s)" % rc)

    lines = [m.groupdict() for m in BOARD_LINE.finditer(board.text)]
    counted = stub.events("counted")
    print("rig: %d BOARD lines, %d pushed detections" % (len(lines), len(counted)), flush=True)
    for line in lines:
        print("   BOARD ring=%(ring)s segment=%(segment)s radius=%(radius)s angle=%(angle)s" % line,
              flush=True)
    for event in counted:
        print("   PUSH  " + json.dumps(event["body"], sort_keys=True), flush=True)

    if not check(len(lines) > 0 and len(counted) > 0,
                 "rig: the footage scored at least one dart and at least one was pushed "
                 "(%d BOARD lines, %d pushes)" % (len(lines), len(counted))):
        stub.stop()
        return
    if not check(len(lines) == len(counted),
                 "rig: one BOARD line per pushed detection -- the two ends of the same sequence "
                 "(%d and %d)" % (len(lines), len(counted))):
        stub.stop()
        return

    matched = 0
    absent = 0
    for line, event in zip(lines, counted):
        body = event["body"]
        got = position_in(body)
        a_miss = event["sector"] == "None"
        knows_both = line["radius"] != "none" and line["angle"] != "none"
        if knows_both and not a_miss:
            expected = (at_places(line["radius"]), at_places(line["angle"]))
            check(got == expected,
                  "rig: %s was pushed at the place its BOARD line printed (%s vs %s)"
                  % (event["sector"], got, expected))
            matched += 1
        else:
            check(got is None,
                  "rig: %s knew %s, so it was pushed with NEITHER key rather than a nought (%s)"
                  % (event["sector"], "nothing" if not knows_both else "a place it may not send", got))
            absent += 1
    check(all(position_in(e["body"]) != "half" for e in counted),
          "rig: nothing was pushed with half a position, which the door refuses with 422")
    check(all(position_in(e["body"]) is None for e in counted if e["sector"] == "None"),
          "rig: every MISS was pushed without a place on the board")
    print("rig: %d pushes carried a position, %d carried none" % (matched, absent), flush=True)
    stub.stop()


CASES = {"seam": case_seam, "spool": case_spool, "rig": case_rig}

if __name__ == "__main__":
    os.makedirs(WORK, exist_ok=True)
    wanted = sys.argv[1:] or list(CASES)
    for name in wanted:
        if name not in CASES:
            print("no such case: %s; the cases are %s" % (name, ", ".join(CASES)))
            sys.exit(2)
    for name in wanted:
        print("\n==== %s ====" % name, flush=True)
        CASES[name]()
    print("\nchecks %d   failed %d" % (checks, len(failures)))
    for f in failures:
        print("  - " + f)
    sys.exit(1 if failures else 0)
