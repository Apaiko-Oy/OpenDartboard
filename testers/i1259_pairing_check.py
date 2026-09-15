#!/usr/bin/env python3
"""#1259: the pairing question, driven end to end against #822's stub.

The REAL detector is started for every case, the way a person meets it: under a pseudo-
terminal, so console_prompt::isInteractiveConsole() answers true exactly as it does for a
console, and the answers are typed into that terminal. The stub (testers/turnaus_stub.py)
records every request, so each case asserts both what the board SAID and what it SENT.

Cases (the five in the issue, then the two failures it names):
  1  club        a code accepted at the club door; nothing sent to the Casual door; the
                 question comes before a camera opens; the label is the computer's name;
                 the board goes straight on to beating and scoring. Also: a code that is not
                 six digits is refused locally and nothing is sent.
  2  casual      refused at the club door, accepted at the Casual door, in that order
  3  both        refused at both: the ten-minutes-and-once message, asked again, then paired
  4  midrun      the club token revoked while running: said, asked again, re-paired with a
                 second code, beating again with the second credential
  5  redirected  input from /dev/null and from a pipe nobody writes: never asks, sends no
                 pairing request, runs to its cycle budget; and a credential refused mid-run
                 with no console keeps #822's behaviour and asks nothing
  6  unreachable the address named, asked again; input ended: not paired, still scores
  7  ratelimit   a 429 said with the wait it names; the Casual door NOT tried; asked again

Runs inside od-amd64:bullseye with the worktree at /app and a Linux build in /app/build:
  python3 /app/testers/i1259_pairing_check.py [case ...]
Exit code 0 only when every check passed. Prints `checks N   failed M`.
"""
import hashlib
import json
import os
import pty
import re
import select
import signal
import socket
import subprocess
import sys
import time

APP = os.environ.get("OD_APP", "/app")
BIN = os.environ.get("OD_BIN", APP + "/build/opendartboard")
STUB = APP + "/testers/turnaus_stub.py"
WORK = os.environ.get("CHECK_WORK", "/tmp/i1259-check")
CAMS = ",".join(APP + "/mocks/cam_%d.mp4" % i for i in (1, 2, 3))
HOSTNAME = socket.gethostname()

CLUB = "483920"
CLUB2 = "220917"
CASUAL = "571643"
WRONG = "000000"
TOKEN1 = "17|" + "z" * 40
TOKEN2 = "18|" + "z" * 40
QUESTION = "Type the board's pairing code (six digits) and press Enter:"
ANSI = re.compile(r"\x1b\[[0-9;]*m")

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
        self.transcript = os.path.join(WORK, case, "transcript.jsonl")
        e = dict(os.environ)
        e.update({"STUB_PORT": str(port), "STUB_TRANSCRIPT": self.transcript, "STUB_SAMPLE_SECONDS": "0",
                  "STUB_INTERVAL_SECONDS": "2", "STUB_SILENCE_SECONDS": "60"})
        e.update({k: str(v) for k, v in env.items()})
        self.err = open(os.path.join(WORK, case, "stub.err"), "w")
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

    def wait_event(self, predicate, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for ev in self.events():
                if predicate(ev):
                    return ev
            time.sleep(0.2)
        return None

    def stop(self):
        self.proc.terminate()  # its own pid, nothing else
        self.proc.wait(timeout=10)
        self.err.close()


class Board:
    """The detector under a pseudo-terminal: stdin, stdout and stderr are the terminal."""

    def __init__(self, case, env, args, interactive=True, stdin=None):
        self.dir = os.path.join(WORK, case)
        self.cwd = os.path.join(self.dir, "cwd")
        os.makedirs(self.cwd, exist_ok=True)
        e = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": self.dir,
             "XDG_CONFIG_HOME": os.path.join(self.dir, "config")}
        e.update(env)
        self.credentials = os.path.join(self.dir, "config", "opendartboard", "credentials.json")
        self.text = ""
        self.log = open(os.path.join(self.dir, "board.out"), "w")
        cmd = [BIN, "--cams", CAMS, "--width", "1280", "--height", "720"] + args
        if interactive:
            self.master, slave = pty.openpty()
            self.proc = subprocess.Popen(cmd, cwd=self.cwd, env=e, stdin=slave, stdout=slave, stderr=slave,
                                         start_new_session=True)
            os.close(slave)
        else:
            self.master = None
            self.proc = subprocess.Popen(cmd, cwd=self.cwd, env=e, stdin=stdin, stdout=self.log,
                                         stderr=subprocess.STDOUT)

    def pump(self, seconds=0.2):
        if self.master is None:
            time.sleep(seconds)
            return
        ready, _, _ = select.select([self.master], [], [], seconds)
        if ready:
            try:
                chunk = os.read(self.master, 65536).decode("utf-8", "replace")
            except OSError:
                return
            chunk = ANSI.sub("", chunk).replace("\r", "")
            self.text += chunk
            self.log.write(chunk)
            self.log.flush()

    def output(self):
        if self.master is None:
            self.log.flush()
            with open(os.path.join(self.dir, "board.out")) as fh:
                return ANSI.sub("", fh.read())
        return self.text

    def wait_for(self, needle, count=1, timeout=60):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.output().count(needle) >= count:
                return True
            if self.proc.poll() is not None:
                self.pump(0.1)
                return self.output().count(needle) >= count
            self.pump()
        return False

    def type(self, line):
        time.sleep(0.3)
        os.write(self.master, (line + "\n").encode())

    def end_input(self):
        time.sleep(0.3)
        os.write(self.master, b"\x04")  # Ctrl-D on an empty line: input has ended

    def settle(self, seconds):
        deadline = time.time() + seconds
        while time.time() < deadline:
            self.pump()

    def stop(self):
        """SIGINT, which #825's handler turns into a normal return from main."""
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGINT)
        deadline = time.time() + 30
        while self.proc.poll() is None and time.time() < deadline:
            self.pump()
        if self.proc.poll() is None:
            self.proc.kill()
        self.pump(0.2)
        self.log.close()
        return self.proc.wait()


def env_for(port):
    # The double-click route: no command line, the address and plaintext by environment.
    return {"OD_TURNAUS_URL": "http://127.0.0.1:%d" % port, "OD_ALLOW_PLAINTEXT": "1"}


def credential():
    pass


def read_credential(board):
    try:
        with open(board.credentials) as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return None


def case_club():
    port = 18911
    stub = Stub("club", port)
    board = Board("club", env_for(port), [])
    check(board.wait_for(QUESTION, 1, 30), "club: an interactive start with no credential asks for a code")
    check("Syötä taulun liitoskoodi" in board.output(), "club: the question is said in Finnish too")
    check("Camera/video" not in board.output() and "initialized successfully" not in board.output(),
          "club: the question comes before a camera opens")
    board.type("12ab")
    check(board.wait_for("A pairing code is six digits.", 1, 10), "club: a code that is not six digits is refused locally")
    check(board.wait_for(QUESTION, 2, 10), "club: and the question is asked again")
    check(len(stub.events("pairing_refused")) + len(stub.events("paired")) + len(stub.events("casual_pairing_refused")) == 0,
          "club: nothing was sent for a code that is not six digits")
    board.type(" " + CLUB + " ")
    check(board.wait_for("The board is paired to its club (device 1).", 1, 20), "club: the code is accepted at the club door")
    paired = stub.events("paired")
    check(len(paired) == 1 and paired[0]["label"] == HOSTNAME,
          "club: the pairing request's label is the computer's name (%s), not OpenDartboard: %s"
          % (HOSTNAME, [p.get("label") for p in paired]))
    check(len(paired) == 1 and paired[0].get("accept") == "application/json", "club: the request asks for JSON")
    check(not stub.events("casual_paired") and not stub.events("casual_pairing_refused"),
          "club: nothing was sent to the Casual door")
    stored = read_credential(board)
    check(stored is not None and stored.get("token") == TOKEN1 and stored.get("label") == HOSTNAME,
          "club: the credential is written where --pair writes it")
    check(board.wait_for("initialized successfully", 1, 60), "club: it goes straight on to the cameras, no restart")
    beat = stub.wait_event(lambda e: e["event"] == "beat" and e["auth_sha256_16"] == sha16(TOKEN1), 60)
    check(beat is not None, "club: and beats with the new credential")
    dart = stub.wait_event(lambda e: e["event"] == "counted" and e["auth_sha256_16"] == sha16(TOKEN1), 120)
    check(dart is not None, "club: and a dart it saw is counted at the club door")
    check(board.output().count(QUESTION) == 2, "club: asked exactly twice (the bad code and the good one)")
    rc = board.stop()
    check(rc == 0, "club: SIGINT still ends the program normally (rc=%s)" % rc)
    stub.stop()


def case_casual():
    port = 18912
    stub = Stub("casual", port)
    board = Board("casual", env_for(port), [])
    check(board.wait_for(QUESTION, 1, 30), "casual: asks")
    board.type(CASUAL)
    check(board.wait_for("The board is paired to Casual Contest 12.", 1, 20), "casual: paired to the Casual Contest")
    order = [e["event"] for e in stub.events() if e["event"] in ("pairing_refused", "paired", "casual_paired",
                                                                   "casual_pairing_refused")]
    check(order == ["pairing_refused", "casual_paired"],
          "casual: the club door first, and the Casual door only after its refusal: %s" % order)
    casual = stub.events("casual_paired")
    check(len(casual) == 1 and casual[0]["label"] == HOSTNAME, "casual: the Casual request sends the computer's name too")
    stored = read_credential(board)
    check(stored is not None and "contest" in stored and "token" not in stored,
          "casual: a Contest binding is written, and no club one")
    beat = stub.wait_event(lambda e: e["event"] == "casual_beat", 60)
    check(beat is not None, "casual: and beats at the Casual door without a restart")
    check(board.output().count(QUESTION) == 1, "casual: asked once")
    board.stop()
    stub.stop()


def case_both():
    port = 18913
    stub = Stub("both", port)
    board = Board("both", env_for(port), [])
    check(board.wait_for(QUESTION, 1, 30), "both: asks")
    board.type(WRONG)
    check(board.wait_for("Codes expire in ten minutes and are used once", 1, 20),
          "both: a code refused at both doors says codes expire in ten minutes and are used once")
    check("Koodit vanhenevat kymmenessä minuutissa ja kelpaavat vain kerran" in board.output(),
          "both: and says it in Finnish")
    check(board.wait_for(QUESTION, 2, 10), "both: and asks again")
    order = [e["event"] for e in stub.events() if e["event"] in ("pairing_refused", "casual_pairing_refused")]
    check(order == ["pairing_refused", "casual_pairing_refused"], "both: one attempt at each door, club first: %s" % order)
    board.type(CLUB)
    check(board.wait_for("The board is paired to its club", 1, 20), "both: the retry pairs")
    check(board.wait_for("initialized successfully", 1, 60), "both: and goes on to the cameras")
    board.stop()
    stub.stop()


def case_midrun():
    port = 18914
    stub = Stub("midrun", port, STUB_CLUB_CODES=CLUB + "," + CLUB2, STUB_REVOKE_CLUB_AFTER_BEATS=5)
    board = Board("midrun", env_for(port), [])
    check(board.wait_for(QUESTION, 1, 30), "midrun: asks")
    board.type(CLUB)
    check(board.wait_for("The board is paired to its club (device 1).", 1, 20), "midrun: paired as device 1")
    revoked = stub.wait_event(lambda e: e["event"] == "club_revoked", 90)
    check(revoked is not None, "midrun: the stub revoked the credential after three beats")
    check(board.wait_for("the board was unpaired", 1, 60), "midrun: the board says it was unpaired")
    check("taulu on irrotettu" in board.output(), "midrun: in Finnish too")
    check(board.wait_for(QUESTION, 2, 10), "midrun: and asks for a new code")
    refused = [e for e in stub.events() if e["event"] == "autoscorer_refused"]
    check(len(refused) >= 1 and refused[0]["auth_sha256_16"] == sha16(TOKEN1), "midrun: it was the old credential refused")
    board.type(CLUB2)
    check(board.wait_for("The board is paired to its club (device 2).", 1, 20), "midrun: re-paired as device 2")
    check(board.wait_for("again, after a new pairing", 1, 20), "midrun: pushing resumes with no restart")
    beat = stub.wait_event(lambda e: e["event"] == "beat" and e["auth_sha256_16"] == sha16(TOKEN2), 30)
    check(beat is not None, "midrun: and beats with the second credential")
    # The stub revoked after a READY beat, so the board was seeing; its first beat under the
    # new credential must say so. A beat thread that snapshots the frame count at resume and
    # beats in the same instant says ERROR here (found on Windows, #1259).
    before = [e for e in stub.events("beat") if e["auth_sha256_16"] == sha16(TOKEN1)]
    said = beat["condition"] if beat else None
    check(said is not None and said != "ERROR",
          "midrun: the first beat after re-pairing does not call a seeing board blind (the last beat "
          "before the refusal said %s, the first after it said %s)"
          % (before[-1]["condition"] if before else None, said))
    stored = read_credential(board)
    check(stored is not None and stored.get("token") == TOKEN2 and stored.get("device_id") == 2,
          "midrun: the second credential is written over the first")
    check(board.proc.poll() is None, "midrun: the detector never exited")
    rc = board.stop()
    check(rc == 0, "midrun: SIGINT still ends it normally with the watcher running (rc=%s)" % rc)
    stub.stop()


def case_redirected():
    port = 18915
    stub = Stub("redirected", port, STUB_REVOKE_CLUB_AFTER_BEATS=2)
    env = env_for(port)
    env["OD_MAX_CYCLES"] = "60"
    t0 = time.time()
    board = Board("redirected", env, [], interactive=False, stdin=subprocess.DEVNULL)
    try:
        rc = board.proc.wait(timeout=240)
    except subprocess.TimeoutExpired:
        board.proc.kill()
        rc = "timeout"
    out = board.output()
    check(rc == 0, "redirected: input from /dev/null runs to its cycle budget and exits 0 (rc=%s, %.0fs)" % (rc, time.time() - t0))
    check(QUESTION not in out and "liitoskoodi" not in out, "redirected: /dev/null is never asked anything")
    check("no credential, so nothing is pushed" in out, "redirected: and says what it always said unpaired")
    check(not stub.events("paired") and not stub.events("pairing_refused") and not stub.events("casual_pairing_refused"),
          "redirected: no pairing request is sent")

    # A pipe that is open and never written: a board that waited would wait forever.
    board2 = Board("redirected-pipe", env, [], interactive=False, stdin=subprocess.PIPE)
    try:
        rc2 = board2.proc.wait(timeout=240)
    except subprocess.TimeoutExpired:
        board2.proc.kill()
        rc2 = "timeout"
    out2 = board2.output()
    check(rc2 == 0 and QUESTION not in out2, "redirected: a pipe nobody writes is never waited on (rc=%s)" % rc2)

    # #822's behaviour, unchanged, for a board with no console whose credential is refused.
    subprocess.run([BIN, "--pair", CLUB, "--turnaus", "http://127.0.0.1:%d" % port, "--allow-plaintext"],
                   cwd=board.cwd, env={"PATH": "/usr/bin:/bin", "HOME": board.dir,
                                       "XDG_CONFIG_HOME": os.path.join(board.dir, "config")},
                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
    env3 = dict(env)
    env3["OD_MAX_CYCLES"] = "400"
    board3 = Board("redirected", env3, [], interactive=False, stdin=subprocess.DEVNULL)
    board3.log.close()
    board3.log = open(os.path.join(board3.dir, "board.out"), "a")
    try:
        rc3 = board3.proc.wait(timeout=300)
    except subprocess.TimeoutExpired:
        board3.proc.kill()
        rc3 = "timeout"
    out3 = board3.output()
    check(stub.events("club_revoked") != [], "redirected: the stub revoked the non-interactive board's credential")
    check("credential was refused" in out3 and "the board was unpaired" not in out3 and QUESTION not in out3,
          "redirected: a refused credential with no console logs #822's refusal and asks nothing")
    check(rc3 == 0, "redirected: and runs on to its budget (rc=%s)" % rc3)
    stub.stop()


def case_unreachable():
    port = 18916  # nothing listens here
    os.makedirs(os.path.join(WORK, "unreachable"), exist_ok=True)
    board = Board("unreachable", env_for(port), [])
    check(board.wait_for(QUESTION, 1, 30), "unreachable: asks")
    board.type(CLUB)
    check(board.wait_for("Could not reach Turnaus at http://127.0.0.1:%d" % port, 1, 30),
          "unreachable: says Turnaus could not be reached, naming the address")
    check(board.wait_for(QUESTION, 2, 10), "unreachable: asks again rather than exiting")
    check(board.proc.poll() is None, "unreachable: still running")
    board.end_input()
    check(board.wait_for("The board was not paired.", 1, 10), "unreachable: input ended: says it was not paired")
    check(board.wait_for("initialized successfully", 1, 60), "unreachable: and still goes on to score")
    board.stop()


def case_ratelimit():
    port = 18917
    stub = Stub("ratelimit", port, STUB_RATE_LIMIT_FIRST=1, STUB_RETRY_AFTER=37)
    board = Board("ratelimit", env_for(port), [])
    check(board.wait_for(QUESTION, 1, 30), "ratelimit: asks")
    board.type(CASUAL)
    check(board.wait_for("Too many attempts. Wait 37 seconds, then try again.", 1, 20),
          "ratelimit: a 429 is said with the wait it names")
    check(board.wait_for(QUESTION, 2, 10), "ratelimit: and asks again")
    check(not stub.events("casual_paired") and not stub.events("casual_pairing_refused")
          and len(stub.events("pairing_rate_limited")) == 1,
          "ratelimit: the Casual door is not tried after the club door's 429")
    board.type(CASUAL)
    check(board.wait_for("The board is paired to Casual Contest 12.", 1, 20), "ratelimit: the retry pairs")
    board.stop()
    stub.stop()


CASES = {"club": case_club, "casual": case_casual, "both": case_both, "midrun": case_midrun,
         "redirected": case_redirected, "unreachable": case_unreachable, "ratelimit": case_ratelimit}

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
