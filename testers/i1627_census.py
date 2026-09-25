#!/usr/bin/env python3
"""#1627's census: what a spike while a motion event settles does to the window after it.

Reads one replay's log (the I1627 SETTLE SPIKE lines, the dart-state lines and the SCORE
lines) and its motion trace (OD_TRACE, motion_processing::dumpTrace) and prints, per
SETTLE SPIKE line:

  I1627 SPIKE <label> cycle=N average=A threshold=T peak_cam=K peak=P others_under=0|1
        action=kept|discarded next_close=C gap=G first=<score> reversions=R second=<score>

  cycle       the cycle the spike was measured on (the log line's own figure)
  peak_cam    the camera whose own board changed most on that cycle, and by how much
  others_under 1 when every other measured camera was under the spike threshold, i.e. one
              camera alone carried the average over it
  next_close  the first cycle after the spike on which an event reached END (trace
              state 2 -> 4: STABILIZING, settled, straight through END into COOLDOWN);
              gap = next_close - cycle. The trace is only written when the run is given a
              cycle budget (scorer.cpp), so a whole-clip replay has none and reads -1.
  first       the first SCORE the program printed after the spike line (END is a window
              that reconciled CLEAN), and `reversions` the CLEAN BY REVERSION lines
              (#1518) between the spike and that SCORE
  second      the SCORE after that

Everything is reported; the harness (i1627_inside.sh) asserts.
"""
import argparse
import csv
import re
import sys

SPIKE = re.compile(r"I1627 SETTLE SPIKE cycle=(\d+) stable=(\d+) average=([0-9.]+) threshold=([0-9.]+) (.*?) -> (IDLE|SPIKE_DETECTED)")
CAM = re.compile(r"cam(\d+)=([0-9.]+|-)")
SCORE = re.compile(r"\[SCORER\] - SCORE: ([A-Z0-9]+) ")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--trace", default=None, help="an OD_TRACE csv; without one next_close/gap read -1")
    ap.add_argument("--label", required=True)
    a = ap.parse_args()

    closes = []
    if a.trace:
        with open(a.trace) as f:
            for row in csv.DictReader(f):
                if row["state_in"] == "2" and row["state_out"] == "4":
                    closes.append(int(row["cycle"]))

    lines = open(a.log, errors="replace").read().splitlines()
    n = 0
    for i, line in enumerate(lines):
        m = SPIKE.search(line)
        if not m:
            continue
        n += 1
        cycle = int(m.group(1))
        avg = float(m.group(3))
        thr = float(m.group(4))
        cams = [(int(c), float(v)) for c, v in CAM.findall(m.group(5)) if v != "-"]
        peak_cam, peak = max(cams, key=lambda cv: cv[1]) if cams else (0, 0.0)
        others_under = int(all(v <= thr for c, v in cams if c != peak_cam))
        action = "discarded" if m.group(6) == "IDLE" else "kept"
        nxt = next((c for c in closes if c > cycle), -1)
        scores, reversions = [], 0
        for later in lines[i + 1:]:
            if "CLEAN BY REVERSION" in later and not scores:
                reversions += 1
            s = SCORE.search(later)
            if s:
                scores.append(s.group(1))
                if len(scores) == 2:
                    break
        scores += ["-"] * (2 - len(scores))
        print(f"I1627 SPIKE {a.label} cycle={cycle} average={avg:.6f} threshold={thr:.6f} "
              f"peak_cam={peak_cam} peak={peak:.6f} others_under={others_under} action={action} "
              f"next_close={nxt} gap={(nxt - cycle) if nxt >= 0 else -1} first={scores[0]} "
              f"reversions={reversions} second={scores[1]}")
    print(f"I1627 SPIKES {a.label} n={n} closes={len(closes)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
