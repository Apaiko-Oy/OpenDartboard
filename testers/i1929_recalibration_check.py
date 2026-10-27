#!/usr/bin/env python3
"""turnaus#1929: a board asked to recalibrate between legs, driven end to end against the stub.

The REAL detector on the shipped mocks (a file source), paired through #822's stub, which
serves turnaus#1928's two board-facing addresses (testers/turnaus_stub.py). The tablet's press
is the stub's harness control, made by this program at a moment it chose from the transcript.
Each case asserts what the board SAID (its terminal) and what it SENT (the transcript).

Cases:
  legs   the board polls; a request made while a round is in hand (forced past the stub's own
         409, which is the race the board must close itself) is REFUSED in words that say a
         leg is in play and the board does not stop; a request made after the takeout is
         ACCEPTED with no round in hand, confirmed once, and the board stops with exit 61. This
         harness then starts it again in the same directory, as the launcher and systemd do:
         the start calibrates afresh although --reuse-calibration is on its command line, beats
         CALIBRATING and then READY with the camera report the server records as the outcome,
         logs BEFORE and AFTER for each camera, and -- with the stub going on listing the
         answered request, as a lost answer leaves it -- neither confirms it again nor stops.
  moved  a board holding a Moved verdict refuses the request in words that name the verdict,
         confirms once, keeps the verdict file, and does not stop.

PREDICTION, STATED FIRST: every check passes on this tree. testers/i1929_check.sh runs this
after the pure check's mutations; the board-level mutation proof is in that file's header.

Runs inside od-amd64:bullseye with the worktree at /app and a Linux build in /app/build:
  python3 /app/testers/i1929_recalibration_check.py [case ...]
Exit code 0 only when every check passed. Prints `checks N   failed M`.
"""
import json
import os
import sys
import time
import urllib.request

APP = os.environ.get("OD_APP", "/app")
os.environ.setdefault("CHECK_WORK", "/tmp/i1929-check")
sys.path.insert(0, os.path.join(APP, "testers"))
import i1259_pairing_check as harness  # noqa: E402  the Stub and the Board, and #1853's wait_event

WORK = harness.WORK
CLUB = harness.CLUB
QUESTION = harness.QUESTION
check = harness.check
failures = harness.failures

ROUND_REFUSAL = "A leg is in play: this board holds darts of a round that has not been taken out."
THROWING_REFUSAL = "A leg is in play: darts were thrown at this board after the request was made."


def press(port, force=False):
    """The tablet's press: (status, reference or None)."""
    request = urllib.request.Request("http://127.0.0.1:%d/stub/recalibration" % port,
                                     data=json.dumps({"force": force}).encode(), method="POST",
                                     headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=5) as answer:
            return answer.status, json.loads(answer.read())["data"]["reference"]
    except urllib.error.HTTPError as refused:
        return refused.code, None


def pair(board, name):
    check(board.wait_for(QUESTION, 1, 60), "%s: the board asks for a code" % name)
    board.type(CLUB)
    check(board.wait_for("The board is paired to its club", 1, 30), "%s: paired" % name)


def wait_exit(board, timeout):
    deadline = time.time() + timeout
    while board.proc.poll() is None and time.time() < deadline:
        board.pump()
    board.pump(0.2)
    return board.proc.poll()


def confirmations(stub, reference):
    return [e for e in stub.events("recalibration_confirmed") if e["reference"] == reference]


def case_legs():
    name = "legs"
    port = 18931
    stub = harness.Stub(name, port, STUB_RECALIBRATION_RELIST=1)
    env = harness.env_for(port)
    board = harness.Board(name, env, ["--reuse-calibration"])
    pair(board, name)

    # ---- polled --------------------------------------------------------------------------
    polled = stub.wait_event(lambda e: e["event"] == "recalibrations_listed", 120, board)
    check(polled is not None and polled["references"] == [],
          "legs: the board polls the recalibrations address with nothing pending")

    # ---- mid-leg: a request made while a round is in hand ----------------------------------
    dart = stub.wait_event(lambda e: e["event"] == "counted", 240, board)
    check(dart is not None, "legs: a dart reached the stub, so a round is in hand")
    status, _ = press(port)
    check(status == 409, "legs: the stub's own model refuses a press while the board holds darts (%s)" % status)
    status, mid = press(port, force=True)
    check(status == 202 and mid, "legs: a request made anyway, as if a leg began between the press and the poll")
    answer = stub.wait_event(lambda e: e["event"] == "recalibration_confirmed" and e["reference"] == mid, 30, board)
    check(answer is not None and answer["accepted"] is False,
          "legs: the request made mid-leg is REFUSED, never accepted: %s" % (answer and answer.get("refusal")))
    check(answer is not None and answer.get("refusal") in (ROUND_REFUSAL + " Ask between legs.",
                                                           THROWING_REFUSAL + " Ask between legs."),
          "legs: in the board's own words that a leg is in play")
    check(answer is not None and len(answer["round"]) > 0,
          "legs: and the stub held the board's round in hand when the refusal arrived (%s)"
          % (answer and answer["round"]))
    check(board.proc.poll() is None, "legs: the board did not stop for it")

    # ---- between legs: after the takeout ---------------------------------------------------
    taken = stub.wait_event(lambda e: e["event"] == "takeout" and e["closed"]
                            and e["at"] > (answer["at"] if answer else 0), 120, board)
    check(taken is not None, "legs: the round was taken out")
    status, ref = press(port)
    check(status == 202 and ref, "legs: a request made after the takeout is taken by the stub")
    accepted = stub.wait_event(lambda e: e["event"] == "recalibration_confirmed" and e["reference"] == ref, 30, board)
    check(accepted is not None and accepted["accepted"] is True and accepted["first"],
          "legs: the request made between legs is ACCEPTED")
    check(accepted is not None and accepted["round"] == [] and accepted["body_keys"] == ["accepted"],
          "legs: with no round in hand, and the body is {\"accepted\": true} alone")
    rc = wait_exit(board, 60)
    check(rc == 61, "legs: the board stopped itself for the restart with exit 61 (rc=%s)" % rc)
    first_out = board.output()
    with open(os.path.join(board.dir, "board.1.out"), "w") as fh:
        fh.write(first_out)
    check(("I1929 RECALIBRATION %s ACCEPTED and confirmed" % ref) in first_out,
          "legs: the board said it accepted and confirmed")
    check(("I1929 RECALIBRATION %s before: camera 1" % ref) in first_out,
          "legs: and logged the geometry it was scoring with, per camera, before it stopped")
    check(("I1929 RECALIBRATION %s REFUSED and confirmed" % mid) in first_out,
          "legs: the mid-leg refusal is in its log too")
    check(len(confirmations(stub, mid)) == 1 and len(confirmations(stub, ref)) == 1,
          "legs: each request was confirmed exactly once before the restart")
    stopped_at = time.time()

    # ---- the restart: what the launcher and systemd do -----------------------------------
    again = harness.Board(name, env, ["--reuse-calibration"])
    check(again.wait_for("I1929 RECALIBRATION %s: this start was asked for" % ref, 1, 60),
          "legs: the new start knows it is the recalibration it was asked for")
    check(again.wait_for("does not reuse the cached calibration", 1, 5),
          "legs: and does not reuse the cached calibration --reuse-calibration names")
    calibrating = stub.wait_event(lambda e: e["event"] == "recalibration_heard" and e["reference"] == ref
                                  and e["screen"] == "CALIBRATING", 120, again)
    check(calibrating is not None, "legs: the server hears CALIBRATING after the confirmation")
    settled = stub.wait_event(lambda e: e["event"] == "recalibration_heard" and e["reference"] == ref
                              and e["screen"] in ("CALIBRATED", "FAILED"), 240, again)
    check(settled is not None and settled["screen"] == "CALIBRATED",
          "legs: and then READY, which the screen draws as calibrated: %s" % (settled and settled["screen"]))
    cams = settled and settled.get("cameras")
    check(isinstance(cams, dict) and cams["fitted"] >= 1 and cams["scoring"] + cams["dark"] <= cams["fitted"],
          "legs: the outcome carries the camera report, fitted/scoring/dark: %s" % cams)
    check("calibrations from cache" not in again.output() and "Performing immediate calibration" in again.output(),
          "legs: it calibrated on the picture and loaded nothing from the cache the first run wrote")
    check(again.wait_for("I1929 RECALIBRATED %s: scoring with" % ref, 1, 30)
          and ("I1929 RECALIBRATED %s before: camera 1" % ref) in again.output()
          and ("I1929 RECALIBRATED %s after: camera 1" % ref) in again.output(),
          "legs: the start logs BEFORE and AFTER for each camera")

    # ---- the request listed again: no repeat ---------------------------------------------
    deadline = time.time() + 60
    relisted = []
    while time.time() < deadline and len(relisted) < 3:
        relisted = [e for e in stub.events("recalibrations_listed")
                    if e["at"] > stopped_at and ref in e["references"]]
        again.pump()
    check(len(relisted) >= 3, "legs: the stub listed the answered request again (%d polls)" % len(relisted))
    again.settle(3)
    check(len(confirmations(stub, ref)) == 1,
          "legs: and the board did not confirm it again (%d confirmations)" % len(confirmations(stub, ref)))
    check(again.proc.poll() is None, "legs: nor stop for it again")
    check(again.output().count("I1929 RECALIBRATION %s ALREADY ANSWERED" % ref) == 1,
          "legs: it said once that the request was answered already")
    rc = again.stop()
    check(rc == 0, "legs: SIGINT still ends the restarted board normally (rc=%s)" % rc)
    stub.stop()


def case_moved():
    name = "moved"
    port = 18932
    stub = harness.Stub(name, port)
    held = os.path.join(WORK, name, "cwd", "cache", "geometry_moved.txt")
    os.makedirs(os.path.dirname(held), exist_ok=True)
    with open(held, "w") as fh:
        fh.write("camera 2: the bull moved 25.00 px from where it was calibrated\n")
    board = harness.Board(name, harness.env_for(port), [])
    pair(board, name)
    check(board.wait_for("BOARD HOLDING A GEOMETRY FAULT", 1, 30), "moved: the board starts holding the Moved verdict")
    beat = stub.wait_event(lambda e: e["event"] == "beat" and e["condition"] == "ERROR", 60, board)
    check(beat is not None, "moved: and beats ERROR")
    status, ref = press(port)
    check(status == 202 and ref, "moved: a request is made")
    answer = stub.wait_event(lambda e: e["event"] == "recalibration_confirmed" and e["reference"] == ref, 30, board)
    check(answer is not None and answer["accepted"] is False and "Moved" in (answer.get("refusal") or ""),
          "moved: it is REFUSED in words that name the Moved verdict: %s" % (answer and answer.get("refusal")))
    board.settle(6)
    check(len(confirmations(stub, ref)) == 1, "moved: confirmed exactly once")
    check(board.proc.poll() is None, "moved: the board did not stop")
    check(os.path.exists(held), "moved: the verdict file is still there: a request does not clear it")
    check(not os.path.exists(os.path.join(WORK, name, "cwd", "cache", "recalibration_asked.txt")),
          "moved: and no restart was prepared")
    after = [e for e in stub.events("beat") if e["at"] > answer["at"]] if answer else []
    check(after and all(e["condition"] == "ERROR" for e in after), "moved: it goes on beating ERROR")
    rc = board.stop()
    check(rc == 0, "moved: SIGINT ends it normally (rc=%s)" % rc)
    stub.stop()


CASES = {"legs": case_legs, "moved": case_moved}

if __name__ == "__main__":
    wanted = sys.argv[1:] or list(CASES)
    for case in wanted:
        print("== " + case, flush=True)
        os.makedirs(os.path.join(WORK, case), exist_ok=True)
        try:
            CASES[case]()
        except Exception as exc:  # a crashed case is a failed case, by name
            check(False, "%s: raised %r" % (case, exc))
    print("checks %d   failed %d" % (harness.checks, len(failures)), flush=True)
    for f in failures:
        print("  FAILED: " + f)
    sys.exit(0 if not failures else 1)
