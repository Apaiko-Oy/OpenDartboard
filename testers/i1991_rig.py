"""turnaus#1991: OD_KEEP_WINDOWS on the real binary -- mocks/rig-20260918 pushed at the Turnaus
stub, and every dart the stub counted held to the window the board wrote for it.

The pure check (i1991_keep_check.cpp) holds window_keep.hpp; this holds the WIRING, which
it cannot see: that dart_processing.cpp deposits what the window was read AGAINST and not
what the vote moved the references to a few lines later, and that TurnausClient::offer
names the deposit with the reference Turnaus is given. It reuses #1366's Stub and Board
(i1366_position_check.py), and asks of the directory, by picture NAME -- a name is the
picture's content, so two names equal is two pictures byte-identical, with no decoder:

  * every pushed reference has an account, and every account names pictures that exist
    (the last push may still be with the writer when the run ends: one is allowed missing)
  * every account names this start's calibration, and the file is there
  * in every camera of every account the settled picture is none of its references. A
    deposit taken AFTER the vote moved the working background would name one picture as
    both -- the fault this is written to catch -- and nothing could be read from it
  * a dart called on a board that already held one (`board_state_before` > 0) reads its
    fresh figure against the previous kept dart's settled picture, camera for camera,
    wherever both name one: the chain a replay walks
  * a run with the switch unset writes no directory at all

The second run's pushes are printed beside the first's and NOT compared: a replay is not
deterministic across two runs (docs/rig.md, #1826), so equal sequences would prove little
and unequal ones nothing.

OD_WINDOW_UNIT=cycles is pinned in the board's environment for #1826's reason: Board.run
passes the detector PATH and HOME only, so tester_paths.sh's pin does not reach it.
"""
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i1366_position_check as rig  # noqa: E402

check = rig.check


def accounts_in(directory):
    out = {}
    for path in glob.glob(os.path.join(directory, "darts", "*.json")):
        with open(path) as fh:
            a = json.load(fh)
        out[a["reference"]] = a
    return out


def main():
    port = 18991
    stub = rig.Stub("keep", port)
    board = rig.Board("keep", port)
    keep = os.path.join(board.dir, "windows")
    board.env["OD_WINDOW_UNIT"] = "cycles"
    board.env["OD_KEEP_WINDOWS"] = keep
    check(board.pair("--pair", rig.CLUB) == 0, "the board pairs to its club")
    rc = board.run(rig.RIG_CYCLES, rig.RIG)
    check(rc == 0, "kept: the run ends normally (rc=%s)" % rc)
    counted = stub.events("counted")
    stub.stop()
    pushed = [e["body"].get("reference", "") for e in counted]
    accounts = accounts_in(keep)
    print("kept: %d pushed detections, %d accounts, %d pictures, %d bytes"
          % (len(pushed), len(accounts), len(glob.glob(os.path.join(keep, "pics", "*.png"))),
             sum(os.path.getsize(p) for p in glob.glob(os.path.join(keep, "*", "*")))), flush=True)
    check("I1991 KEEP: every published dart's window is kept under" in board.text,
          "kept: the start says the switch is on and where")
    check(len(pushed) >= 10, "kept: the footage pushed at least ten darts (%d)" % len(pushed))
    missing = [r for r in pushed if r not in accounts]
    check(len(missing) <= 1 and (not missing or missing[0] == pushed[-1]),
          "kept: every pushed reference has an account, bar at most the last (missing: %s)" % (missing or "none"))
    check(not [r for r in accounts if r not in pushed], "kept: no account is of a dart the stub never counted")

    gone, no_calibration, same, cameras = [], [], [], 0
    for ref, a in accounts.items():
        if not a["calibration"] or not os.path.exists(os.path.join(keep, "calibration", a["calibration"] + ".dat")):
            no_calibration.append(ref)
        for c in a["cameras"]:
            for role in ("settled", "clean", "fresh"):
                if c[role] and not os.path.exists(os.path.join(keep, "pics", c[role] + ".png")):
                    gone.append("%s camera %d %s" % (ref, c["camera"], role))
            if c["settled"]:
                cameras += 1
                if c["settled"] in (c["clean"], c["fresh"]):
                    same.append("%s camera %d" % (ref, c["camera"]))
    check(not gone, "kept: every picture an account names is on disk (%s)" % (gone[:3] or "all there"))
    check(not no_calibration, "kept: every account names this start's calibration and the file is there (%s)"
          % (no_calibration[:3] or "all"))
    check(cameras > 0 and not same,
          "kept: in %d camera-windows the settled picture is never one of its own references -- the deposit "
          "is of what the window was read against (%s)" % (cameras, same[:3] or "none the same"))

    order = [r for r in pushed if r in accounts]
    chained = broken = 0
    for prev, ref in zip(order, order[1:]):
        a, p = accounts[ref], accounts[prev]
        if a["board_state_before"] <= 0 or a["window"] <= p["window"]:
            continue
        for c, pc in zip(a["cameras"], p["cameras"]):
            if c["fresh"] and pc["settled"]:
                chained += 1
                if c["fresh"] != pc["settled"]:
                    broken += 1
                    print("   CHAIN %s camera %d reads against %s, and %s settled as %s"
                          % (ref, c["camera"], c["fresh"], prev, pc["settled"]), flush=True)
    check(chained > 0, "kept: some dart was called on a board already holding one (%d camera links)" % chained)
    # #1690 / #1691 move a working background on a window that publishes no dart, so a link
    # may honestly be to a picture no account holds; it is counted, and most must chain.
    check(broken * 4 <= chained, "kept: a later dart's fresh reference IS the previous kept dart's settled picture "
          "(%d of %d links; the rest were re-based by a window that published nothing)" % (chained - broken, chained))
    for ref in order[:3]:
        a = accounts[ref]
        print("   ACCOUNT %s window=%d state_before=%d switches=%s" % (ref, a["window"], a["board_state_before"],
              json.dumps(a["switches"], sort_keys=True)), flush=True)

    # ---- unset ---------------------------------------------------------------------------
    stub = rig.Stub("unset", port + 1)
    plain = rig.Board("unset", port + 1)
    plain.env["OD_WINDOW_UNIT"] = "cycles"
    check(plain.pair("--pair", rig.CLUB) == 0, "unset: the board pairs to its club")
    rc = plain.run(rig.RIG_CYCLES, rig.RIG)
    check(rc == 0, "unset: the run ends normally (rc=%s)" % rc)
    unset_counted = stub.events("counted")
    stub.stop()
    check("I1991" not in plain.text, "unset: no I1991 line is printed")
    written = [p for p in glob.glob(os.path.join(plain.dir, "**", "*"), recursive=True)
               if "/pics/" in p or "/darts/" in p or p.endswith("/windows")]
    check(not written, "unset: no window, picture or account is written anywhere under the board's directory")
    print("unset: %d pushed detections; sectors kept  %s" % (len(unset_counted), " ".join(e["sector"] for e in counted)))
    print("                               sectors unset %s  (printed, not compared: #1826)"
          % " ".join(e["sector"] for e in unset_counted), flush=True)

    print("\nchecks %d   failed %d" % (rig.checks, len(rig.failures)))
    for f in rig.failures:
        print("  - " + f)
    return 1 if rig.failures else 0


if __name__ == "__main__":
    os.makedirs(rig.WORK, exist_ok=True)
    sys.exit(main())
