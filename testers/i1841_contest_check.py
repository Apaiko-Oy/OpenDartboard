#!/usr/bin/env python3
"""#1841: a board bound to a given-up Casual Contest lets it go, driven end to end against
#822's stub.

A given-up or discarded Contest does not answer 401 (#891's model, which is what the stub
answered until #1841 and what every earlier tester ran against). It answers 409 from
CasualDetectionController::boardOf() in Laravel's validation shape -- {"message": "...",
"errors": {"board": ["..."]}}, the sentence localised -- and the client's one 409 sentence
on that door was the autoscorer door's "stands at no Station". Live on 2026-10-13 a
Raspberry Pi 5 beat into given-up Contest 22 for twenty-five minutes saying exactly that,
with a club binding underneath it that was never used, until somebody removed the `contest`
object from credentials.json by hand.

The REAL detector is started for the three end-to-end cases, with the three mock files and a
cycle budget, the way #1276's cases run it. Its stdin is /dev/null, so #1259's pairing
question is never asked, and every credential here is written by --pair / --pair-contest
first. The stub (testers/turnaus_stub.py) records every request, so each case asserts both
what the board SAID and what it SENT.

Cases:
  over       STUB_GIVE_UP_STATUS=409: the third beat meets 409 + errors.board. The log says,
             in order, that the Contest is over (naming the door), that the binding has
             ended (HTTP 409: the Contest was given up), and that the board is still paired
             to its Organisation; neither door's "no Station" sentence appears; the beat
             after the refused one lands at /api/v1/autoscorer/heartbeats under the club
             token; nothing reaches a Casual door afterwards; the run ends on the club
             binding with no `contest` object in the file.
  control    STUB_GIVE_UP_STATUS=401 (the default, spelled out): the same ending with the
             401 reason, and the stub's own events unchanged in shape -- today's path.
  nostation  STUB_GIVE_UP_STATUS=409-nostation: 409 WITHOUT errors.board at the Contest
             door keeps the binding, says the door-named no-Station sentence, and never
             beats the club door.
  harness    i1841_beat_check.cpp, the client driven directly -- the only form a MUTATED
             client can be driven in, since the detector binary is the worktree's one build.
             `over` releases within thirty seconds and the next beat lands at the club;
             `kept` holds the binding through five no-Station 409s.
  pure       i1841_body_check.cpp (what unit_check.sh 1841 runs) passes; and the autoscorer
             door's two 409 sentences are in turnaus_client.cpp verbatim and once each.

--mutate copies src/ to scratch, makes every `contestOverBody(res.body)` at the two call
sites `false` (every 409 read as "no Station": the client of 2026-10-13), runs `harness`
against the copy, and exits 0 only when the `over` line went red and `kept` stayed green.

Runs inside od-amd64:bullseye with the worktree at /app and a Linux build in /app/build:
  python3 /app/testers/i1841_contest_check.py [--mutate] [case ...]
Exit code 0 only when every check passed (or, under --mutate, when the mutation was caught).
Prints `checks N   failed M`.

NOT RUN WHERE IT WAS WRITTEN (2026-10-13, no Docker on the authoring box): the predictions
above are what the code says it does; the maintainer's gate is the first measurement.
"""
import hashlib
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time

APP = os.environ.get("OD_APP", "/app")
BIN = os.environ.get("OD_BIN", APP + "/build/opendartboard")
STUB = APP + "/testers/turnaus_stub.py"
WORK = os.environ.get("CHECK_WORK", "/tmp/i1841-check")
CAMS = ",".join(APP + "/mocks/cam_%d.mp4" % i for i in (1, 2, 3))

CLUB = "483920"
CASUAL = "571643"
CLUB_TOKEN = "17|" + "z" * 40
CASUAL_TOKEN = "31|" + "y" * 40
CONTEST_ID = 12
ANSI = re.compile(r"\x1b\[[0-9;]*m")

# The client's sentences, asserted verbatim: the log is the acceptance criterion.
OVER_LINE = re.compile(r"TURNAUS: the server says Casual Contest %d is over "
                       r"\(409 with errors\.board at (/api/v1/casual/[a-z]+)\)" % CONTEST_ID)
ENDED_409 = "TURNAUS: the binding to Casual Contest %d has ended (HTTP 409: the Contest was given up)" % CONTEST_ID
ENDED_401 = re.compile(r"TURNAUS: the binding to Casual Contest %d has ended \(HTTP 401" % CONTEST_ID)
ENDED_ANY = "TURNAUS: the binding to Casual Contest"
CLUB_LINE = "TURNAUS: this board is still paired to its Organisation, so Match scoring continues."
# The Contest door's no-Station sentences, door named (#1841) ...
CONTEST_NOSTATION_BEAT = "TURNAUS: the server says this Contest board stands at no Station (409); the beat has nowhere to land"
CONTEST_NOSTATION_PUSH = "TURNAUS: the server says this Contest board stands at no Station (409)"
# ... and the autoscorer door's, unchanged since #892, which a Contest board must never print.
CLUB_NOSTATION_PUSH = 'log_warning("TURNAUS: the server says this board stands at no Station (409)");'
CLUB_NOSTATION_BEAT = ('log_warning("TURNAUS: the server says this board stands at no Station (409); '
                       'the beat has nowhere to land");')
ANY_NOSTATION = "stands at no Station"

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


def position(text, needle):
    """Where a sentence first appears, or -1: the three sentences are asserted in ORDER."""
    if isinstance(needle, re.Pattern):
        found = needle.search(text)
        return found.start() if found else -1
    return text.find(needle)


def next_beat_lands_at_club(stub, case):
    """The acceptance criterion's last clause: after the refusal, the NEXT heartbeat is at
    /api/v1/autoscorer/heartbeats with the club credential.

    The refusal is usually the beat's (#891: it goes every interval, a dart only when
    somebody throws) but may be a push's, or both threads' at once before the release
    lands, so the sequence read is every heartbeat and every refusal at either Casual route,
    in the stub's order: the first heartbeat after the first refusal must be the club's, and
    no Casual heartbeat may follow any refusal.
    """
    seq = [e for e in stub.events() if e["event"] in ("casual_beat", "beat", "casual_refused")]
    refused = [i for i, e in enumerate(seq) if e["event"] == "casual_refused"]
    if not check(bool(refused), "%s: something was refused at a Casual route at all (%s)"
                 % (case, [e["event"] for e in seq])):
        return
    following = [e for e in seq[refused[0] + 1:] if e["event"] != "casual_refused"]
    check(bool(following) and following[0]["event"] == "beat"
          and following[0]["auth_sha256_16"] == sha16(CLUB_TOKEN),
          "%s: the heartbeat after the refusal landed at /api/v1/autoscorer/heartbeats under the club token (%s)"
          % (case, [(e["event"], e.get("auth_sha256_16") == sha16(CLUB_TOKEN)) for e in following[:2]]))
    check(all(e["event"] == "beat" for e in following),
          "%s: and every heartbeat after it went to the club door (%s)"
          % (case, [e["event"] for e in following]))


def released_to_the_club(board, stub, case, ended_line):
    """What every release has in common, 401 or 409: #891's ending, #1276's accounting."""
    ended = position(board.text, ended_line)
    club = position(board.text, CLUB_LINE)
    check(board.text.count(ENDED_ANY) == 1,
          "%s: the binding was released exactly once (%d)" % (case, board.text.count(ENDED_ANY)))
    check(ended >= 0, "%s: with the reason the issue names" % case)
    check(club > ended >= 0, "%s: and then `still paired to its Organisation` (ended@%d club@%d)"
          % (case, ended, club))
    next_beat_lands_at_club(stub, case)
    given_up = stub.events("casual_given_up")
    ended_at = given_up[0]["at"] if given_up else 0
    after = [e for e in stub.events() if e["at"] >= ended_at
             and e["event"] in ("casual_counted", "casual_takeout", "casual_absorbed", "casual_beat")]
    check(after == [], "%s: nothing reached a Casual door after the evening ended (%s)"
          % (case, [e["event"] for e in after]))
    summary = board.summary()
    check(summary.get("binding") == "organisation",
          "%s: the board ends on its club binding (%s)" % (case, summary.get("binding")))
    cred = board.credential()
    check(cred is not None and "contest" not in cred,
          "%s: the `contest` object is gone from credentials.json (%s)"
          % (case, sorted(cred) if cred else cred))


def case_over():
    """THE ISSUE. The real shape, met by the real detector's heartbeat."""
    port = 18941
    stub = Stub("over", port, STUB_GIVE_UP_STATUS=409, STUB_GIVE_UP_AFTER_BEATS=2)
    board = Board("over", port)
    check(board.pair("--pair", CLUB) == 0, "over: the board pairs to its club")
    check(board.pair("--pair-contest", CASUAL) == 0, "over: and to the evening's Contest")
    cred = board.credential()
    check(cred is not None and "contest" in cred, "over: the file holds both bindings before the run")
    rc = board.run(1100)
    check(rc == 0, "over: the run ends normally (rc=%s)" % rc)

    given_up = stub.events("casual_given_up")
    check(len(given_up) == 1, "over: the stub gave the evening up once (%d)" % len(given_up))
    refused = stub.events("casual_refused")
    check(len(refused) >= 1 and all(e.get("status") == 409 and e.get("shape") == "409" for e in refused),
          "over: the stub answered the give-up with 409 + errors.board (%s)"
          % [(e.get("status"), e.get("path")) for e in refused])

    # THE SENTENCES, IN ORDER: the 409 read as what it is, the release, the fall-back.
    over = position(board.text, OVER_LINE)
    ended = position(board.text, ENDED_409)
    check(over >= 0, "over: the log says Casual Contest 12 is over, naming the Contest door's route (%s)"
          % (OVER_LINE.search(board.text).group(1) if OVER_LINE.search(board.text) else None))
    check(ended > over >= 0, "over: then `has ended (HTTP 409: the Contest was given up)` (over@%d ended@%d)"
          % (over, ended))
    released_to_the_club(board, stub, "over", ENDED_409)
    check(ANY_NOSTATION not in board.text,
          "over: neither door's `stands at no Station` sentence was printed (%s)"
          % [l.strip() for l in board.text.splitlines() if ANY_NOSTATION in l][:2])

    # The fall-back is not only said: the club's door is used, the way the Pi's was once the
    # file had been edited by hand (44 darts in ten minutes).
    check(len(stub.events("counted")) >= 1,
          "over: darts thrown after the release reach the club (%d)" % len(stub.events("counted")))
    stub.stop()


def case_control():
    """#891's model, which the stub answered until #1841 and still answers by default."""
    port = 18942
    stub = Stub("control", port, STUB_GIVE_UP_STATUS=401, STUB_GIVE_UP_AFTER_BEATS=2)
    board = Board("control", port)
    check(board.pair("--pair", CLUB) == 0, "control: the board pairs to its club")
    check(board.pair("--pair-contest", CASUAL) == 0, "control: and to the evening's Contest")
    rc = board.run(1100)
    check(rc == 0, "control: the run ends normally (rc=%s)" % rc)

    given_up = stub.events("casual_given_up")
    check(len(given_up) == 1, "control: the stub gave the evening up once (%d)" % len(given_up))
    refused = stub.events("casual_refused")
    check(len(refused) >= 1 and all("status" not in e and "shape" not in e for e in refused),
          "control: the stub's 401 events carry no #1841 keys -- the default path, byte for byte (%s)"
          % [sorted(e) for e in refused[:1]])
    check(OVER_LINE.search(board.text) is None, "control: nothing said a 409 was met")
    released_to_the_club(board, stub, "control", ENDED_401)
    check(ANY_NOSTATION not in board.text, "control: no `stands at no Station` sentence either")
    stub.stop()


def case_nostation():
    """A 409 at the Contest door WITHOUT errors.board: no Station at that Contest, kept."""
    port = 18943
    stub = Stub("nostation", port, STUB_GIVE_UP_STATUS="409-nostation", STUB_GIVE_UP_AFTER_BEATS=2)
    board = Board("nostation", port)
    check(board.pair("--pair", CLUB) == 0, "nostation: the board pairs to its club")
    check(board.pair("--pair-contest", CASUAL) == 0, "nostation: and to the evening's Contest")
    rc = board.run(1100)
    check(rc == 0, "nostation: the run ends normally (rc=%s)" % rc)

    refused = [e for e in stub.events("casual_refused") if e.get("path") == "/api/v1/casual/heartbeats"]
    check(len(refused) >= 2 and all(e.get("shape") == "409-nostation" for e in refused),
          "nostation: the stub answered at least two beats with a 409 without errors.board (%d)" % len(refused))
    beat_lines = board.text.count(CONTEST_NOSTATION_BEAT)
    check(beat_lines >= 2, "nostation: the beat said `this Contest board stands at no Station (409); "
          "the beat has nowhere to land` each time (%d)" % beat_lines)
    unnamed = [l.strip() for l in board.text.splitlines()
               if ANY_NOSTATION in l and "this Contest board" not in l]
    check(unnamed == [], "nostation: the autoscorer door's un-named sentence never printed (%s)" % unnamed[:1])
    check(ENDED_ANY not in board.text, "nostation: the binding was not released")
    check(CLUB_LINE not in board.text, "nostation: and the club sentence was not said")
    check(OVER_LINE.search(board.text) is None, "nostation: nothing said the Contest was over")
    check(stub.events("beat") == [], "nostation: the club door was beaten not once (%d)"
          % len(stub.events("beat")))
    summary = board.summary()
    check(summary.get("binding") == "contest:%d" % CONTEST_ID,
          "nostation: the board ends still bound to the Contest (%s)" % summary.get("binding"))
    cred = board.credential()
    check(cred is not None and "contest" in cred, "nostation: the `contest` object is still in the file")
    stub.stop()


def compile_against(src_root, source, binary, case):
    """#1276's compile line, with the client's translation unit taken from `src_root` --
    the worktree's src/ or the mutant's copy of it. Answers the binary's path, or None
    having already said by name that it would not compile."""
    compile_line = (
        "g++ -std=c++17 -I {src} -I {src}/utils "
        "-I {app}/build/_deps/nlohmann_json-src/include -I {app}/build/_deps/httplib-src "
        "$(pkg-config --cflags opencv4 2>/dev/null) "
        "{app}/testers/{source} {src}/communication/turnaus_client.cpp -o {bin} "
        "$(pkg-config --libs opencv4 2>/dev/null) -lpthread"
    ).format(app=APP, src=src_root, source=source, bin=binary)
    built = subprocess.run(compile_line, shell=True, capture_output=True, text=True, timeout=900)
    if not check(built.returncode == 0, "%s: %s compiles against %s (%s)"
                 % (case, source, src_root, built.stderr.strip().splitlines()[-1:] or "no output")):
        return None
    return binary


def case_harness(src_root=None):
    """The client driven directly, against the worktree's client or a mutant's."""
    src_root = src_root or APP + "/src"
    tag = "harness"
    work = os.path.join(WORK, tag)
    os.makedirs(work, exist_ok=True)
    binary = compile_against(src_root, "i1841_beat_check.cpp", os.path.join(work, "i1841_beat_check"), tag)
    if binary is None:
        return

    # over: the first beat gives the evening up, the second meets 409 + errors.board.
    port = 18944
    stub = Stub("harness-over", port, STUB_GIVE_UP_STATUS=409, STUB_GIVE_UP_AFTER_BEATS=1)
    board = Board("harness-over", port)
    check(board.pair("--pair", CLUB) == 0, "harness/over: the board pairs to its club")
    check(board.pair("--pair-contest", CASUAL) == 0, "harness/over: and to the evening's Contest")
    ran = subprocess.run([binary, "http://127.0.0.1:%d" % port, board.credentials, "over"],
                         cwd=board.cwd, env=dict(board.env), capture_output=True, text=True, timeout=120)
    board.text = ANSI.sub("", ran.stdout + ran.stderr)
    with open(os.path.join(board.dir, "harness.out"), "w") as fh:
        fh.write(board.text)
    # THE LINE THE MUTATION TURNS RED, naming the door.
    check(ran.returncode == 0 and "I1841 released=1" in board.text and ENDED_409 in board.text,
          "harness/over: the Contest door's 409 with errors.board ended the binding (%s)"
          % ([l.strip() for l in board.text.splitlines() if l.startswith("I1841")] or ["no I1841 line"]))
    check(OVER_LINE.search(board.text) is not None and CLUB_LINE in board.text,
          "harness/over: said as `Casual Contest 12 is over` and then the club sentence")
    next_beat_lands_at_club(stub, "harness/over")
    stub.stop()

    # kept: the same door, a 409 without errors.board, five times over.
    port = 18945
    stub = Stub("harness-kept", port, STUB_GIVE_UP_STATUS="409-nostation", STUB_GIVE_UP_AFTER_BEATS=1)
    board = Board("harness-kept", port)
    check(board.pair("--pair", CLUB) == 0, "harness/kept: the board pairs to its club")
    check(board.pair("--pair-contest", CASUAL) == 0, "harness/kept: and to the evening's Contest")
    ran = subprocess.run([binary, "http://127.0.0.1:%d" % port, board.credentials, "kept"],
                         cwd=board.cwd, env=dict(board.env), capture_output=True, text=True, timeout=120)
    board.text = ANSI.sub("", ran.stdout + ran.stderr)
    with open(os.path.join(board.dir, "harness.out"), "w") as fh:
        fh.write(board.text)
    check(ran.returncode == 0 and "I1841 kept=1" in board.text and ENDED_ANY not in board.text,
          "harness/kept: a 409 without errors.board at the Contest door kept the binding (%s)"
          % ([l.strip() for l in board.text.splitlines() if l.startswith("I1841")] or ["no I1841 line"]))
    check(board.text.count(CONTEST_NOSTATION_BEAT) >= 2,
          "harness/kept: and said `this Contest board stands at no Station` each time (%d)"
          % board.text.count(CONTEST_NOSTATION_BEAT))
    check(stub.events("beat") == [], "harness/kept: the club door was never beaten")
    stub.stop()


def case_pure():
    """unit_check.sh 1841's source, run here too; and the autoscorer door's sentences, read."""
    work = os.path.join(WORK, "pure")
    os.makedirs(work, exist_ok=True)
    binary = compile_against(APP + "/src", "i1841_body_check.cpp", os.path.join(work, "i1841_body_check"), "pure")
    if binary is not None:
        ran = subprocess.run([binary], capture_output=True, text=True, timeout=60)
        with open(os.path.join(work, "body_check.out"), "w") as fh:
            fh.write(ran.stdout + ran.stderr)
        check(ran.returncode == 0 and "I1841 body PASS" in ran.stdout,
              "pure: contestOverBody holds to both doors' shapes (%s)"
              % ([l for l in ran.stdout.splitlines() if l.startswith("FAIL")] or "all ok"))

    # The autoscorer door's 409 is unchanged: no stub knob answers 409 at the club door (the
    # brief allowed one new knob, and it is the Contest door's), so this is read off the
    # source rather than driven -- both of #892's sentences, verbatim, once each.
    with open(APP + "/src/communication/turnaus_client.cpp") as fh:
        source = fh.read()
    check(source.count(CLUB_NOSTATION_PUSH) == 1,
          "pure: the autoscorer door's push sentence is in turnaus_client.cpp verbatim, once (%d)"
          % source.count(CLUB_NOSTATION_PUSH))
    check(source.count(CLUB_NOSTATION_BEAT) == 1,
          "pure: and its heartbeat sentence likewise (%d)" % source.count(CLUB_NOSTATION_BEAT))
    check(source.count("contestOverBody(res.body)") == 2,
          "pure: the body is read at exactly the two call sites the mutation targets (%d)"
          % source.count("contestOverBody(res.body)"))


def mutate():
    """Every 409 read as `no Station`: the client of 2026-10-13, on a scratch copy of src/."""
    mutant = os.path.join(WORK, "mutant")
    shutil.rmtree(mutant, ignore_errors=True)
    shutil.copytree(APP + "/src", os.path.join(mutant, "src"))
    path = os.path.join(mutant, "src", "communication", "turnaus_client.cpp")
    with open(path) as fh:
        source = fh.read()
    hits = source.count("contestOverBody(res.body)")
    if hits != 2:
        print("MUTATION NOT APPLIED: expected the needle twice in turnaus_client.cpp, found %d" % hits)
        return 2
    with open(path, "w") as fh:
        fh.write(source.replace("contestOverBody(res.body)", "false"))
    print("== harness (mutant: every 409 is no-Station)", flush=True)
    case_harness(os.path.join(mutant, "src"))
    red = [f for f in failures if f.startswith("harness/over: the Contest door's 409 with errors.board ended")]
    kept_green = not any(f.startswith("harness/kept") for f in failures)
    print("checks %d   failed %d" % (checks, len(failures)), flush=True)
    for f in failures:
        print("  FAILED: " + f)
    if red and kept_green:
        print("MUTATION CAUGHT: with every 409 read as no-Station the `over` line goes red naming the "
              "Contest door, and `kept` stays green.")
        return 0
    print("MUTATION NOT CAUGHT: over red=%s kept green=%s" % (bool(red), kept_green))
    return 1


CASES = {"over": case_over, "control": case_control, "nostation": case_nostation,
         "harness": case_harness, "pure": case_pure}

if __name__ == "__main__":
    args = sys.argv[1:]
    if args and args[0] == "--mutate":
        os.makedirs(WORK, exist_ok=True)
        sys.exit(mutate())
    wanted = args or list(CASES)
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
