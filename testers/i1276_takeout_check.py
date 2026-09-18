#!/usr/bin/env python3
"""#1276: where a takeout goes, driven end to end against #822's stub.

A takeout closes the round in hand. Which door it belongs at is therefore decided by the
round -- by where that round's FIRST dart was pushed -- and not by what this board happens
to be bound to when the round ends. The two are different things across exactly one event:
a Casual Contest given up in the middle of a round (#891's release).

The REAL detector is started for every case, with the three mock files and a cycle budget,
the way #891's phases ran it. Its stdin is /dev/null, so #1259's pairing question is never
asked (a redirected start asks nothing) and every credential here is written by --pair /
--pair-contest first. The stub (testers/turnaus_stub.py) records every request, so each
case asserts both what the board SAID and what it SENT.

Cases:
  givenup      the evening ends with a round in hand: the takeout that follows the release
               is DROPPED, not sent to the club. The stub records no Organisation takeout
               closing nothing; the log says so once; the counters count it.
               Controls in the same run: the darts thrown after the release DO reach the
               club, and the round they open IS closed there.
  organisation a board with only a club binding: every round it begins is taken out at the
               Organisation door, with a Visit identifier back.
  casual       a board on a live Contest: the round is taken out at the Casual door, under
               the Casual credential, and nothing at all reaches the club's door.
  spool        a takeout already written to the spool, owed to a Contest this board is no
               longer bound to, is abandoned on resume rather than delivered to the club --
               beside a club takeout in the same spool, which is delivered.

Runs inside od-amd64:bullseye with the worktree at /app and a Linux build in /app/build:
  python3 /app/testers/i1276_takeout_check.py [case ...]
Exit code 0 only when every check passed. Prints `checks N   failed M`.
"""
import hashlib
import json
import os
import re
import socket
import subprocess
import urllib.request
import sys
import time

APP = os.environ.get("OD_APP", "/app")
BIN = os.environ.get("OD_BIN", APP + "/build/opendartboard")
STUB = APP + "/testers/turnaus_stub.py"
WORK = os.environ.get("CHECK_WORK", "/tmp/i1276-check")
CAMS = ",".join(APP + "/mocks/cam_%d.mp4" % i for i in (1, 2, 3))

CLUB = "483920"
CASUAL = "571643"
CLUB_TOKEN = "17|" + "z" * 40
CASUAL_TOKEN = "31|" + "y" * 40
CONTEST_ID = 12
ANSI = re.compile(r"\x1b\[[0-9;]*m")

# What the client says when it will not send a takeout. Asserted verbatim, because "the log
# says so once" is the acceptance criterion and a message nobody reads is not one.
DROP_LINE = "TURNAUS: a takeout was dropped rather than sent"
RELEASE_LINE = "TURNAUS: the binding to Casual Contest"

checks = 0
failures = []


def check(ok, name):
    global checks
    checks += 1
    print(("PASS " if ok else "FAIL ") + name, flush=True)
    if not ok:
        failures.append(name)
    return ok


def sha16(token):
    return hashlib.sha256(token.encode()).hexdigest()[:16]


class Stub:
    def __init__(self, case, port, **env):
        self.dir = os.path.join(WORK, case)
        os.makedirs(self.dir, exist_ok=True)
        self.transcript = os.path.join(self.dir, "transcript.jsonl")
        e = dict(os.environ)
        e.update({"STUB_PORT": str(port), "STUB_TRANSCRIPT": self.transcript, "STUB_SAMPLE_SECONDS": "0",
                  "STUB_INTERVAL_SECONDS": "2", "STUB_SILENCE_SECONDS": "60"})
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
        self.cursor = os.path.join(self.config, "owed.cursor")
        self.env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": self.dir,
                    "XDG_CONFIG_HOME": os.path.join(self.dir, "config")}
        self.text = ""

    def pair(self, flag, code):
        out = os.path.join(self.dir, "pair%s.out" % flag.replace("-", "_"))
        with open(out, "w") as fh:
            return subprocess.run([BIN, flag, code, "--turnaus", "http://127.0.0.1:%d" % self.port,
                                   "--allow-plaintext"],
                                  cwd=self.cwd, env=self.env, stdin=subprocess.DEVNULL,
                                  stdout=fh, stderr=subprocess.STDOUT, timeout=60).returncode

    def run(self, cycles, label="run"):
        """One scoring run to a cycle budget. stdout and stderr to separate files."""
        env = dict(self.env)
        env["OD_MAX_CYCLES"] = str(cycles)
        out = os.path.join(self.dir, label + ".out")
        err = os.path.join(self.dir, label + ".err")
        with open(out, "w") as o, open(err, "w") as e:
            rc = subprocess.run([BIN, "--debug", "--cams", CAMS, "--width", "1280", "--height", "720",
                                 "--allow-plaintext"],
                                cwd=self.cwd, env=env, stdin=subprocess.DEVNULL,
                                stdout=o, stderr=e, timeout=900).returncode
        with open(out) as fh:
            body = ANSI.sub("", fh.read())
        with open(err) as fh:
            body += ANSI.sub("", fh.read())
        self.text = body
        return rc

    def credential(self):
        try:
            with open(self.credentials) as fh:
                return json.load(fh)
        except (OSError, ValueError):
            return None

    def summary(self):
        """The client's own shutdown line, as a dict of its counters."""
        match = re.search(r"TURNAUS: client stopped\. (.*)", self.text)
        if not match:
            return {}
        out = {}
        for pair in match.group(1).split():
            if "=" in pair:
                key, value = pair.split("=", 1)
                out[key] = value
        return out


def empty_takeouts(events):
    return [e for e in events if not e.get("closed")]


def full_takeouts(events):
    return [e for e in events if e.get("closed")]


def case_givenup():
    """#891's give-up, and the takeout that follows it.

    The stub gives the evening up at the third dart, which is the last dart of the round in
    hand; it beats every two seconds, so the refusal is met by the beat before the round's
    takeout is published. That is #891's `givenup2` ordering, deterministic here rather than
    raced: the release lands between the round's last dart and its END.
    """
    port = 18921
    stub = Stub("givenup", port, STUB_GIVE_UP_AFTER=3)
    board = Board("givenup", port)
    check(board.pair("--pair", CLUB) == 0, "givenup: the board pairs to its club")
    check(board.pair("--pair-contest", CASUAL) == 0, "givenup: and to the evening's Contest")
    rc = board.run(1100)
    check(rc == 0, "givenup: the run ends normally (rc=%s)" % rc)

    given_up = stub.events("casual_given_up")
    check(len(given_up) == 1, "givenup: the stub gave the evening up once (%d)" % len(given_up))
    counted = stub.events("casual_counted")
    check(len(counted) >= 1 and counted[0]["round"],
          "givenup: the Contest really had a round in hand when it ended (%s)"
          % (counted[-1]["round"] if counted else None))
    check(board.text.count(RELEASE_LINE) == 1,
          "givenup: the binding was released once (%d)" % board.text.count(RELEASE_LINE))

    # THE ISSUE. The takeout that ends a round begun inside the Contest is dropped.
    drops = [l for l in board.text.splitlines() if DROP_LINE in l]
    check(len(drops) == 1, "givenup: the takeout is said to be dropped exactly once (%d)" % len(drops))
    check(drops and ("Casual Contest %d" % CONTEST_ID) in drops[0],
          "givenup: and the line names the Contest the round was begun under: %s"
          % (drops[0].strip() if drops else None))

    club_takeouts = stub.events("takeout")
    check(empty_takeouts(club_takeouts) == [],
          "givenup: the club is sent no takeout closing nothing (%d of %d)"
          % (len(empty_takeouts(club_takeouts)), len(club_takeouts)))
    check(not [e for e in stub.events("casual_takeout")],
          "givenup: and nothing reached the Casual takeout door after the evening ended")

    # The fall-back, in the same run: the club is still this board's binding and still works.
    club_counted = stub.events("counted")
    check(len(club_counted) >= 1, "givenup: the darts thrown after the release reach the club (%d)"
          % len(club_counted))
    check(len(full_takeouts(club_takeouts)) >= 1,
          "givenup: and the round they open is closed at the club, with a Visit identifier (%s)"
          % [e.get("visitId") for e in club_takeouts])

    summary = board.summary()
    check(summary.get("binding") == "organisation",
          "givenup: the board ends on its club binding (%s)" % summary.get("binding"))
    # Every drop the log accounts for: what the release abandoned, plus this takeout.
    abandoned = 0
    for line in board.text.splitlines():
        found = re.search(r"has ended \(.*?\)\. (\d+) push\(es\) owed to it were dropped", line)
        if found:
            abandoned = int(found.group(1))
    check(summary.get("dropped") == str(abandoned + len(drops)),
          "givenup: dropped=%s is the %d abandoned at the release plus the %d takeout"
          % (summary.get("dropped"), abandoned, len(drops)))
    stub.stop()


def case_organisation():
    """A club board, all run. Nothing here may move: this is #822's own behaviour."""
    port = 18922
    stub = Stub("organisation", port)
    board = Board("organisation", port)
    check(board.pair("--pair", CLUB) == 0, "organisation: the board pairs to its club")
    rc = board.run(1100)
    check(rc == 0, "organisation: the run ends normally (rc=%s)" % rc)

    takeouts = stub.events("takeout")
    check(len(takeouts) >= 1, "organisation: the round is taken out at the club's door (%d)" % len(takeouts))
    check(empty_takeouts(takeouts) == [],
          "organisation: and no takeout closes nothing (%d)" % len(empty_takeouts(takeouts)))
    check(all(e.get("visitId") for e in takeouts),
          "organisation: every takeout comes back with a Visit identifier (%s)"
          % [e.get("visitId") for e in takeouts])
    check(all(e["auth_sha256_16"] == sha16(CLUB_TOKEN) for e in takeouts),
          "organisation: under the club credential and no other")
    check(DROP_LINE not in board.text, "organisation: nothing was dropped")
    check(not stub.events("casual_takeout") and not stub.events("casual_counted"),
          "organisation: and nothing was sent to the Casual door")
    stub.stop()


def case_casual():
    """A board on a live evening. The takeout belongs to the Contest, not to the club."""
    port = 18923
    stub = Stub("casual", port)
    board = Board("casual", port)
    check(board.pair("--pair", CLUB) == 0, "casual: the board pairs to its club")
    check(board.pair("--pair-contest", CASUAL) == 0, "casual: and to the evening's Contest")
    rc = board.run(1100)
    check(rc == 0, "casual: the run ends normally (rc=%s)" % rc)

    takeouts = stub.events("casual_takeout")
    check(len(takeouts) >= 1, "casual: the round is taken out at the Casual door (%d)" % len(takeouts))
    check(empty_takeouts(takeouts) == [],
          "casual: and no takeout closes nothing (%d)" % len(empty_takeouts(takeouts)))
    check(all(e["auth_sha256_16"] == sha16(CASUAL_TOKEN) for e in takeouts),
          "casual: under the Casual credential, which is what says the takeout carried the round's own binding")
    check(not stub.events("takeout") and not stub.events("counted"),
          "casual: not one arrival at the club's door, in a run whose board is paired to it")
    check(DROP_LINE not in board.text, "casual: nothing was dropped")
    stub.stop()


def case_spool():
    """The same rule one restart later.

    Four records planted by hand, in the shape the client writes them -- #891's `horizon`
    method, and the same reason: nothing here waits for a round to be thrown. The board
    holds the club binding and no Contest.
    """
    port = 18924
    stub = Stub("spool", port)
    board = Board("spool", port)
    check(board.pair("--pair", CLUB) == 0, "spool: the board pairs to its club")

    now = int(time.time() * 1000)
    rows = [
        # Owed to the club: a dart and the takeout that closes it. The positive control --
        # a takeout CAN be delivered from a spool, so the absence below means something.
        {"path": "/api/v1/autoscorer/detections",
         "body": json.dumps({"reference": "01M1EEEEEEEEEEEEEEEEEEEEEE", "sector": "S20", "bounced_out": False}),
         "key": "01M1EEEEEEEEEEEEEEEEEEEEEE", "binding": "organisation", "contest_id": 0,
         "spooled_ms": now - 30 * 1000},
        {"path": "/api/v1/autoscorer/takeouts", "body": "{}", "key": "",
         "binding": "organisation", "contest_id": 0, "spooled_ms": now - 30 * 1000},
        # Owed to an evening this board is no longer on: a dart and its takeout. Both are
        # inside the fifteen-minute horizon and neither may be delivered anywhere.
        {"path": "/api/v1/casual/detections",
         "body": json.dumps({"reference": "01M1FFFFFFFFFFFFFFFFFFFFFF", "sector": "T20", "bounced_out": False}),
         "key": "01M1FFFFFFFFFFFFFFFFFFFFFF", "binding": "contest", "contest_id": CONTEST_ID,
         "spooled_ms": now - 30 * 1000},
        {"path": "/api/v1/casual/takeouts", "body": "{}", "key": "",
         "binding": "contest", "contest_id": CONTEST_ID, "spooled_ms": now - 30 * 1000},
    ]
    with open(board.spool, "w") as fh:
        for row in rows:
            fh.write(json.dumps(row) + "\n")

    rc = board.run(140, "resume")
    check(rc == 0, "spool: the run ends normally (rc=%s)" % rc)
    check("1 abandoned as older than the round" not in board.text,
          "spool: nothing was abandoned by the fifteen-minute horizon; this is the binding rule")
    resumed = re.search(r"resumed (\d+) owed push\(es\) from the spool; (\d+) abandoned as older "
                        r"than the round they belonged to; (\d+) abandoned as owed to a Contest", board.text)
    check(resumed is not None and resumed.group(1) == "2" and resumed.group(3) == "2",
          "spool: two records resumed and two abandoned as owed to a Contest this board is not on (%s)"
          % (str(resumed.groups()) if resumed else "no resume line at all"))

    takeouts = stub.events("takeout")
    check(len(takeouts) == 1 and takeouts[0].get("closed") == ["S20"],
          "spool: the club's own spooled takeout is delivered, closing the dart beside it (%s)"
          % [e.get("closed") for e in takeouts])
    check(empty_takeouts(takeouts) == [],
          "spool: and the Contest's takeout is NOT delivered to the club as an empty round")
    check(not stub.events("casual_takeout") and not stub.events("casual_counted"),
          "spool: nothing was sent to the Casual door either; the evening is over")
    stub.stop()


def case_noround():
    """#1276's narrow edge: a takeout ending a round THIS PROCESS never began.

    A scripted run of the detector cannot reach it -- the scorer publishes a takeout only
    after it has published darts -- but a board in a pub reaches it every time it restarts
    in the middle of a round: the darts already owed are resumed from the spool, which
    begins no round because nothing was offered, and then the live END of that same round
    arrives. That takeout must still be sent at the live binding, exactly as it always was.
    Dropping it would leave those darts in the server's round in hand until some later
    takeout closed them into the wrong turn, which is a worse failure than the one #1276 is
    about. So the client is driven directly here, by a harness compiled against it.
    """
    port = 18925
    stub = Stub("noround", port)
    board = Board("noround", port)
    base = "http://127.0.0.1:%d" % port

    # Paired through the door like any other board, because the stub admits a credential
    # only after it has minted one -- and the harness is then handed that file's path.
    check(board.pair("--pair", CLUB) == 0, "noround: the board pairs to its club")

    # The dart the PREVIOUS process pushed, planted at the server under the same
    # credential: the round in hand at the club is not empty when the takeout arrives.
    seed = urllib.request.Request(
        base + "/api/v1/autoscorer/detections",
        data=json.dumps({"reference": "01M1GGGGGGGGGGGGGGGGGGGGGG", "sector": "S20",
                         "bounced_out": False}).encode(),
        headers={"Authorization": "Bearer " + CLUB_TOKEN, "Content-Type": "application/json",
                 "Accept": "application/json"})
    with urllib.request.urlopen(seed, timeout=10) as answer:
        check(answer.status == 202, "noround: the previous process's dart is counted at the club")

    binary = os.path.join(board.dir, "i1276_round_check")
    compile_line = (
        "g++ -std=c++17 -I {app}/src -I {app}/src/utils "
        "-I {app}/build/_deps/nlohmann_json-src/include -I {app}/build/_deps/httplib-src "
        "$(pkg-config --cflags opencv4 2>/dev/null) "
        "{app}/testers/i1276_round_check.cpp {app}/src/communication/turnaus_client.cpp -o {bin} "
        "$(pkg-config --libs opencv4 2>/dev/null) -lpthread"
    ).format(app=APP, bin=binary)
    built = subprocess.run(compile_line, shell=True, capture_output=True, text=True, timeout=900)
    if not check(built.returncode == 0, "noround: the round harness compiles (%s)"
                 % built.stderr.strip().splitlines()[-1:] or "no output"):
        stub.stop()
        return

    env = dict(board.env)
    ran = subprocess.run([binary, base, board.credentials], cwd=board.cwd, env=env,
                         capture_output=True, text=True, timeout=300)
    board.text = ANSI.sub("", ran.stdout + ran.stderr)
    with open(os.path.join(board.dir, "harness.out"), "w") as fh:
        fh.write(board.text)

    check(ran.returncode == 0, "noround: the harness ran clean (rc=%s)" % ran.returncode)
    check("NOROUND accepted=1" in board.text,
          "noround: the client accepted a takeout with no round of its own to end")
    check("NOROUND accepted=1 delivered=1 dropped=0" in board.text,
          "noround: it was delivered and nothing was dropped (%s)"
          % [l for l in board.text.splitlines() if l.startswith("NOROUND")])
    check(DROP_LINE not in board.text, "noround: and nothing said a takeout was dropped")

    takeouts = stub.events("takeout")
    check(len(takeouts) == 2, "noround: two takeouts reached the club (%d)" % len(takeouts))
    check(takeouts and takeouts[0].get("closed") == ["S20"] and takeouts[0].get("visitId"),
          "noround: the first closed the round the previous process had begun (%s)"
          % [(e.get("closed"), e.get("visitId")) for e in takeouts])
    check(len(takeouts) > 1 and takeouts[1].get("closed") == ["S20"],
          "noround: and the control -- a dart offered here and its takeout -- closed its own round")
    check(not stub.events("casual_takeout"), "noround: nothing went to the Casual door")
    stub.stop()


CASES = {"givenup": case_givenup, "organisation": case_organisation,
         "casual": case_casual, "spool": case_spool, "noround": case_noround}

if __name__ == "__main__":
    wanted = sys.argv[1:] or list(CASES)
    for name in wanted:
        print("== " + name, flush=True)
        os.makedirs(os.path.join(WORK, name), exist_ok=True)
        try:
            CASES[name]()
        except Exception as exc:  # a crashed case is a failed case, by name
            check(False, "%s: raised %r" % (name, exc))
    print("checks %d   failed %d" % (checks, len(failures)), flush=True)
    for f in failures:
        print("  FAILED: " + f)
    sys.exit(0 if not failures else 1)
