"""#1512: the entry-intersection census -- geometric scoring accuracy, position error
against the hand-measured entry points, coverage and refusals, SIDE BY SIDE with the
string-vote baseline the acceptance compares against.

    python3 i1512_census.py --log <run.txt> --truth <table.md> \
        --annotations <fixture.csv> --fixture <name> [--min-solved N]

Reads one detector run made with OD_GEO_SCORE=on AND OD_SHAFT_CENSUS=1: the I1511AXIS
lines are what the #1504-aware spatial matcher aligns events to annotated throws with
(imported from i1511_census.py rather than re-derived, so the two censuses cannot
disagree about which detection was which dart), and the I1512 lines carry the solve.

A reporter in i1510p2_census.py's mould: it decides nothing about the numbers. Exit
status is about whether the run could be read: 0 parsed with enough solves to be an
instrument, 2 nothing to census.
"""

import argparse
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i1511_census as axis_census  # noqa: E402  (the matcher, #1504's alignment)

ANSI = re.compile(r"\x1b\[[0-9;]*m")
ENTRY_RE = re.compile(
    r"I1512ENTRY window=(-?\d+) outcome=(\S+) solved=(\d) x=([-0-9.]+) y=([-0-9.]+) "
    r"r=([-0-9.]+) phi=([-0-9.]+) sigma=([-0-9.]+)/([-0-9.]+) pair=([-0-9.]+) "
    r"score=(\S+) boundary=([-0-9.]+) ring=([-0-9.]+) wedge=([-0-9.]+) byCam=(\S+) "
    r"agree=(\d) usable=(\d+) offered=(\d+) tips=(\d+)/(\d+) nearestTip=([-0-9.]+) "
    r"published=(\S+) conf=([-0-9.]+) story=(.*)"
)
CAM_RE = re.compile(
    r"I1512CAM window=(-?\d+) cam=(\d+) usable=(\d) excluded=(\d) tangent=([-0-9.]+) "
    r"resid_mm=([-0-9.]+) resid_px=([-0-9.]+) sigmaPerp=([-0-9.]+) lever=([-0-9.]+) "
    r"sigmaDir=([-0-9.]+) img=\(([-0-9.]+),([-0-9.]+)\) pxmm=([-0-9.]+) tipPlaced=(\d) "
    r"tipDist=([-0-9.]+) tipR=([-0-9.]+) excl=(.*)"
)


def read_run_with_entries(path):
    """i1511's visit/event structure, with each Event also carrying its I1512 block."""
    visits, all_axis = axis_census.read_run(path)
    # A second pass binds I1512 blocks to the same SCORE lines the events bound to,
    # in arrival order: the k-th non-END SCORE line of the log is the k-th event.
    entries = []          # (entry dict, [cam dicts]) in arrival order
    pending = None
    order = []
    for raw in open(path, "r", errors="replace"):
        line = ANSI.sub("", raw)
        m = ENTRY_RE.search(line)
        if m:
            pending = ({
                "window": int(m.group(1)), "outcome": m.group(2),
                "solved": m.group(3) == "1",
                "x": float(m.group(4)), "y": float(m.group(5)),
                "r": float(m.group(6)), "phi": float(m.group(7)),
                "sigmaMajor": float(m.group(8)), "sigmaMinor": float(m.group(9)),
                "pair": float(m.group(10)), "score": m.group(11),
                "boundary": float(m.group(12)), "byCam": m.group(15),
                "agree": m.group(16) == "1", "usable": int(m.group(17)),
                "offered": int(m.group(18)), "tipsAgree": int(m.group(19)),
                "tipsSeen": int(m.group(20)), "nearestTip": float(m.group(21)),
                "published": m.group(22), "conf": float(m.group(23)),
                "story": m.group(24).strip(),
            }, {})
            entries.append(pending)
            continue
        m = CAM_RE.search(line)
        if m and pending is not None:
            pending[1][int(m.group(2))] = {
                "usable": m.group(3) == "1", "excluded": m.group(4) == "1",
                "resid_mm": float(m.group(6)), "resid_px": float(m.group(7)),
                "sigmaPerp": float(m.group(8)),
                "img": (float(m.group(11)), float(m.group(12))),
                "pxmm": float(m.group(13)), "tipPlaced": m.group(14) == "1",
                "tipDist": float(m.group(15)), "excl": m.group(17).strip(),
            }
            continue
        sm = axis_census.SCORE_RE.search(line)
        if sm and sm.group(1) != "END":
            order.append(pending)
            pending = None
    # Attach in arrival order to the flattened events.
    flat = [ev for visit in visits for ev in visit]
    for ev, block in zip(flat, order):
        ev.geo = block
    for ev in flat:
        if not hasattr(ev, "geo"):
            ev.geo = None
    return visits, all_axis


def med(xs):
    return sorted(xs)[len(xs) // 2] if xs else float("nan")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--truth", required=True)
    ap.add_argument("--annotations", required=True)
    ap.add_argument("--fixture", required=True)
    ap.add_argument("--min-solved", type=int, default=1,
                    help="fewest solved entries this census must see to be an instrument")
    ap.add_argument("--no-arrival", default="",
                    help="comma list of visit.dart the RECORDING never delivers as an "
                         "arrival (a dart parked before frame one, a throw that never "
                         "hit the board): their absence is a property of the recording, "
                         "not a detection failure, and an event MATCHED to one is "
                         "itself suspect")
    args = ap.parse_args()
    no_arrival = set()
    for token in args.no_arrival.split(","):
        token = token.strip()
        if token:
            v, d = token.split(".")
            no_arrival.add((int(v), int(d)))

    truth = axis_census.read_truth(args.truth)
    annots = axis_census.read_annotations(args.annotations)
    visits, all_axis = read_run_with_entries(args.log)
    flat = [ev for visit in visits for ev in visit]
    with_geo = [ev for ev in flat if ev.geo is not None]
    if not with_geo:
        print("I1512 CENSUS %s: no I1512ENTRY lines in %s -- was the run made with "
              "OD_GEO_SCORE=on?" % (args.fixture, args.log))
        return 2
    print("I1512 CENSUS fixture=%s detected_events=%d with_geometry=%d truth_visits=%d"
          % (args.fixture, len(flat), len(with_geo), len(truth)))

    # ---- COVERAGE: what the solver said about every called dart -------------------------
    outcomes = {}
    excl_reasons = {}
    for ev in with_geo:
        entry = ev.geo[0]
        outcomes[entry["outcome"]] = outcomes.get(entry["outcome"], 0) + 1
        for cam, c in sorted(ev.geo[1].items()):
            if c["usable"] and not c["excluded"]:
                continue
            reason = c["excl"].split(":", 1)[0].strip() or "(unsaid)"
            key = "cam%d %s" % (cam, reason)
            excl_reasons[key] = excl_reasons.get(key, 0) + 1
    print("I1512 COVERAGE outcomes: " +
          "; ".join("%s=%d" % kv for kv in sorted(outcomes.items())))
    for key in sorted(excl_reasons):
        print("I1512 EXCLUSIONS %s=%d" % (key, excl_reasons[key]))

    # ---- the #1504 spatial assignment: i1511's own matcher, #1554-global ----------------
    # (assign_events is imported, not re-derived, so the two censuses cannot disagree
    # about which detection was which dart -- and since #1554 it no longer trusts
    # visit boundaries, which is what mis-scored every rig-20260922 axis from window
    # 9 on against the previous dart's annotation.)
    assignment = axis_census.assign_events(visits, annots, no_arrival)
    assigned = assignment["assigned"]        # (visit0, event_index) -> (visit1, dart)
    unmatched = [(v, ei, visits[v][ei]) for (v, ei) in assignment["unmatched"]]
    recording_absent = ["v%d.%d(%s)" % (key[0], key[1],
                                        list(annots[key].values())[0]["thrown"])
                        for key in assignment["recording_absent"]]
    undetected, miss_absent = [], []
    for key in assignment["undetected"]:
        tv, d = key
        label = "v%d.%d(%s)" % (tv, d, list(annots[key].values())[0]["thrown"])
        if tv - 1 < len(truth) and d - 1 < len(truth[tv - 1]) and truth[tv - 1][d - 1] == "MISS":
            miss_absent.append(label)
        else:
            undetected.append(label)

    # ---- ACCURACY side by side, and position error against the annotated entries -------
    def norm_score(s):
        s = s.upper()
        return {"MISS": "MISS", "BULL": "BULL", "DBULL": "BULL", "OUTER": "OUTER"}.get(
            s, s if s[0] in "TDS" else "S" + s)

    n_matched = 0
    vote_right = geo_right = geo_scored = geo_refused = geo_wire = 0
    pos_err_mm = []
    tip_dists = []
    pair_angles = []
    for (v, ei), key in sorted(assignment["suspect"]):
        ev = visits[v][ei]
        # The recording offers no arrival here (a parked dart, a hand-placed dart, a
        # throw that never hit the board), so the event that matched its annotation
        # is itself suspect and stays out of the scorecard.
        print("I1512 SUSPECT-MATCH v%d.%d published=%s geo_outcome=%s -- the "
              "recording delivers no arrival for this dart (--no-arrival), so "
              "whatever matched it is not a scored throw"
              % (key[0], key[1], ev.score,
                 ev.geo[0]["outcome"] if ev.geo is not None else "(no geometry)"))
    for v, visit in enumerate(visits):
        for ei, ev in enumerate(visit):
            if (v, ei) not in assigned or ev.geo is None:
                continue
            key = assigned[(v, ei)]
            ann = annots[key]
            thrown = norm_score(list(ann.values())[0]["thrown"])
            entry, cams = ev.geo
            n_matched += 1
            vote_ok = norm_score(ev.score) == thrown
            vote_right += 1 if vote_ok else 0
            geo_word = entry["score"] if entry["solved"] else entry["outcome"]
            geo_ok = None
            if entry["solved"]:
                geo_scored += 1
                geo_ok = norm_score(entry["score"]) == thrown
                geo_right += 1 if geo_ok else 0
                if entry["outcome"] == "WIRE-UNCERTAIN":
                    geo_wire += 1
                pair_angles.append(entry["pair"])
                if entry["nearestTip"] >= 0:
                    tip_dists.append(entry["nearestTip"])
            else:
                geo_refused += 1
            errs = []
            for cam, c in sorted(cams.items()):
                a = ann.get(cam)
                if not entry["solved"] or a is None or a["tip"] is None:
                    continue
                if c["img"][0] < 0 or c["pxmm"] <= 0:
                    continue
                px = math.hypot(c["img"][0] - a["tip"][0], c["img"][1] - a["tip"][1])
                errs.append(px / c["pxmm"])
            if errs:
                pos_err_mm.append(med(errs))
            print("I1512 PAIR v%d.%d thrown=%s published=%s%s geo=%s%s outcome=%s "
                  "sigma=%.1f boundary=%.1f pair=%.1f posErr=%s tips=%d/%d"
                  % (key[0], key[1], thrown, ev.score, " OK" if vote_ok else " X",
                     geo_word,
                     ("" if geo_ok is None else (" OK" if geo_ok else " X")),
                     entry["outcome"], entry["sigmaMajor"], entry["boundary"],
                     entry["pair"],
                     "/".join("%.1fmm" % e for e in errs) if errs else "-",
                     entry["tipsAgree"], entry["tipsSeen"]))

    # ---- events no annotated throw claimed: the phantoms' own census --------------------
    for v, ei, ev in unmatched:
        if ev.geo is None:
            continue
        entry, cams = ev.geo
        print("I1512 UNMATCHED v%d#%d published=%s conf=%.1f geo_outcome=%s geo_story: %s"
              % (v + 1, ei + 1, ev.score, entry["conf"], entry["outcome"],
                 entry["story"][:200]))
    if recording_absent:
        print("I1512 RECORDING-FACTS (no arrival exists to detect -- a parked dart or "
              "a throw that never hit the board; not detection failures): "
              + ", ".join(recording_absent))
    if miss_absent:
        print("I1512 MISS-NOT-DETECTED (the truth says miss; absence is the right "
              "answer, not a detection failure): " + ", ".join(miss_absent))
    if undetected:
        print("I1512 DETECTION-FAILURES (no event matched an annotated throw, #1504): "
              + ", ".join(undetected))

    # ---- the side-by-side scorecard -----------------------------------------------------
    print("I1512 SCORECARD fixture=%s matched=%d | string-vote correct %d/%d | "
          "geometry solved %d (correct %d/%d, wire-flagged %d), refused %d"
          % (args.fixture, n_matched, vote_right, n_matched,
             geo_scored, geo_right, geo_scored, geo_wire, geo_refused))
    if pos_err_mm:
        print("I1512 POSITION solved-vs-annotated-entry n=%d median=%.1f p90=%.1f max=%.1f mm"
              % (len(pos_err_mm), med(pos_err_mm),
                 sorted(pos_err_mm)[int(len(pos_err_mm) * 0.9)], max(pos_err_mm)))
    if pair_angles:
        print("I1512 CONDITIONING pair angles on solved events: median=%.1f min=%.1f max=%.1f deg"
              % (med(pair_angles), min(pair_angles), max(pair_angles)))
    if tip_dists:
        print("I1512 TIP-DISTANCE nearest tip to solved entry: n=%d median=%.1f p90=%.1f mm"
              % (len(tip_dists), med(tip_dists),
                 sorted(tip_dists)[int(len(tip_dists) * 0.9)]))

    solved_total = sum(1 for ev in with_geo if ev.geo[0]["solved"])
    if solved_total < args.min_solved:
        print("I1512 CENSUS %s: only %d entries solved (need %d) -- a census that "
              "compared nothing has measured nothing (#1490)"
              % (args.fixture, solved_total, args.min_solved))
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
