"""#1511: the shaft-axis census -- accuracy against hand-measured lines, AND the
coverage/refusal table over every event, because the acceptance demands both and a
census of only the plausible lines has measured nothing.

    python3 i1511_census.py --log <run.txt> --truth <table.md> \
        --annotations <fixture.csv> --fixture <name> [--no-arrival v.d,...]

Reads one detector run made with OD_SHAFT_CENSUS=1. Every window the vote advanced
prints one I1511AXIS line per camera slot (abstentions included, with their refusal);
the published SCORE line that follows binds that window to a detected dart, and END
lines split visits.

ALIGNMENT IS #1504-AWARE, AND SINCE #1554 IT IS GLOBAL. Detected events are matched
to annotated throws SPATIALLY -- the assignment minimising the summed distance from
each annotation's line to the detected observation (valid axes matched line-to-line;
otherwise the published tip against the annotated entry) -- never merely by order, so
an undetected throw shifts nothing. The matching is monotone in time (arrivals and
throws are both time-ordered, so matched pairs may not cross) but it no longer trusts
VISIT boundaries: #1554 measured what per-visit-index alignment does on
rig-20260922, where visit 1 holds two throws that never arrive (--no-arrival below)
and the detector's visit segmentation therefore runs a dart ahead of the truth's --
every accepted axis from window 9 on was judged against the PREVIOUS dart's
annotation, and the census reported lateral errors of 24-192 px on axes that lie
1.6-7.7 px from the darts they actually observed. Unmatched annotations are DETECTION
failures and unmatched detections are reported, both apart from axis errors.

--no-arrival (i1512_census.py's flag, same semantics) lists visit.dart annotations
the RECORDING never delivers as an arrival -- a dart parked before frame one, a
hand-placed dart, a throw that never hit the board. Their absence is a property of
the recording, not a detection failure, and an event matched to one is itself
SUSPECT and stays out of the accuracy figures.

A reporter in i1510p2_census.py's mould: it decides nothing about the numbers. Exit
status is about whether the run could be read: 0 parsed with something compared, 2
nothing to census -- which a caller must not mistake for accuracy.
"""

import argparse
import csv
import math
import re
import sys

ANSI = re.compile(r"\x1b\[[0-9;]*m")
SCORE_RE = re.compile(
    r"SCORE:\s+(\S+)\s+\|\s+Position:\s+\((-?\d+),(-?\d+)\)\s+\|\s+"
    r"Confidence:\s+([0-9.]+)\s+\|\s+Camera:\s+(-?\d+)"
)
# The three shadow fields are #1554's and OPTIONAL, so the pre-#1554 logs this
# census's before/after was measured against still parse.
AXIS_RE = re.compile(
    r"I1511AXIS window=(\d+) opened=(\d+) closed=(\d+) cam=(\d+) valid=(\d) "
    r"p=\(([-0-9.]+),([-0-9.]+)\) d=\(([-0-9.]+),([-0-9.]+)\) angle=([-0-9.]+) "
    r"extent=([-0-9.]+) width=([-0-9.]+) rms=([-0-9.]+) sigma=([-0-9.]+) px=(\d+) "
    r"cols=(\d+) trimmed=(\d+) frac=([-0-9.]+) tipGap=([-0-9.]+) "
    r"(?:shadowPx=(\d+) shadowCols=(\d+) subtract=(\d) )?refusal=(.*)"
)
THROW_RE = re.compile(r"^(?:\*{1,2})?(miss|BULL|DBULL|[TDS]?\d{1,2})(?:\*{1,2})?$", re.IGNORECASE)


def read_truth(path):
    visits = []
    in_table = False
    for raw in open(path, "r", errors="replace"):
        cells = [c.strip() for c in raw.strip().strip("|").split("|")] if "|" in raw else []
        if len(cells) >= 4 and cells[0].lower() == "visit":
            in_table = True
            continue
        if in_table:
            if len(cells) < 4 or not cells[0].isdigit():
                if visits:
                    break
                continue
            throws = []
            for cell in cells[1:4]:
                m = THROW_RE.match(cell)
                if m:
                    throws.append(m.group(1).upper())
            visits.append(throws)
    return visits


def read_annotations(path):
    """(visit, dart) -> {camera -> row}; rows without a line (occlusion skips) absent."""
    per = {}
    for row in csv.DictReader(open(path, "r")):
        try:
            v, d, cam = int(row["visit"]), int(row["dart"]), int(row["camera"])
            x1, y1 = float(row["x1"]), float(row["y1"])
            x2, y2 = float(row["x2"]), float(row["y2"])
        except (ValueError, KeyError):
            continue
        tip = None
        if row.get("tip_x") and row.get("tip_y"):
            tip = (float(row["tip_x"]), float(row["tip_y"]))
        per.setdefault((v, d), {})[cam] = {
            "line": ((x1, y1), (x2, y2)), "tip": tip,
            "thrown": row["thrown"], "note": row.get("note", "")}
    return per


class Event(object):
    def __init__(self, window):
        self.window = window
        self.cams = {}      # cam (1-based) -> axis dict
        self.score = None
        self.camera = -1    # published best camera, 0-based
        self.position = None


def read_run(path):
    """Detected visits of Events, plus every axis line for the coverage table."""
    visits = [[]]
    all_axis = []
    pending = None
    for raw in open(path, "r", errors="replace"):
        line = ANSI.sub("", raw)
        m = AXIS_RE.search(line)
        if m:
            obs = {
                "window": int(m.group(1)), "cam": int(m.group(4)),
                "valid": m.group(5) == "1",
                "p": (float(m.group(6)), float(m.group(7))),
                "d": (float(m.group(8)), float(m.group(9))),
                "angle": float(m.group(10)), "extent": float(m.group(11)),
                "width": float(m.group(12)), "rms": float(m.group(13)),
                "sigma": float(m.group(14)), "px": int(m.group(15)),
                "cols": int(m.group(16)), "trimmed": int(m.group(17)),
                "frac": float(m.group(18)), "tipGap": float(m.group(19)),
                "shadowPx": int(m.group(20)) if m.group(20) is not None else 0,
                "shadowCols": int(m.group(21)) if m.group(21) is not None else 0,
                "subtract": m.group(22) == "1",
                "refusal": m.group(23).strip(),
            }
            all_axis.append(obs)
            if pending is None or pending.window != obs["window"]:
                pending = Event(obs["window"])
            pending.cams[obs["cam"]] = obs
            continue
        m = SCORE_RE.search(line)
        if m:
            score = m.group(1)
            if score == "END":
                pending = None
                if visits[-1]:
                    visits.append([])
                continue
            ev = pending if pending is not None else Event(-1)
            pending = None
            ev.score = score
            ev.position = (float(m.group(2)), float(m.group(3)))
            ev.camera = int(m.group(5))
            visits[-1].append(ev)
    if visits and not visits[-1]:
        visits.pop()
    return visits, all_axis


def line_angle(p, q):
    a = math.degrees(math.atan2(q[1] - p[1], q[0] - p[0])) % 180.0
    return a


def angle_diff(a, b):
    d = abs(a % 180.0 - b % 180.0)
    return min(d, 180.0 - d)


def perp_distance(point, on_line, direction):
    dx, dy = point[0] - on_line[0], point[1] - on_line[1]
    return abs(dx * direction[1] - dy * direction[0])


def match_cost(ev, annots):
    """How badly this detected event fits this annotated throw, px; None = no basis."""
    costs = []
    for cam, ann in annots.items():
        obs = ev.cams.get(cam)
        (p1, p2) = ann["line"]
        mid = ((p1[0] + p2[0]) / 2.0, (p1[1] + p2[1]) / 2.0)
        if obs and obs["valid"]:
            costs.append(perp_distance(mid, obs["p"], obs["d"]) +
                         2.0 * angle_diff(line_angle(p1, p2), obs["angle"]))
        elif ev.camera + 1 == cam and ev.position and ann["tip"]:
            costs.append(math.hypot(ev.position[0] - ann["tip"][0],
                                    ev.position[1] - ann["tip"][1]))
    return sum(costs) / len(costs) if costs else None


MATCH_COST_CAP = 150.0


def assign_events(visits, annots, no_arrival=frozenset()):
    """#1554: ONE spatial assignment both censuses share (i1512_census.py imports it,
    so the two can never disagree about which detection was which dart).

    The alignment is hierarchical, and every level of it was forced by a measurement:

    - Truth-visit INDEX is not trusted. Per-visit-index alignment is what turned
      rig-20260922's two no-arrival throws into a one-dart phase error: from window 9
      on, every accepted axis was judged against the previous dart's annotation, and
      the census reported 24-192 px of lateral error on axes that lie 1.6-7.7 px from
      their own darts (#1554's measurement, frames 490/520/540 and 1265 against
      windows 6, 9 and 10).
    - Detected takeout BOUNDARIES are trusted as separators. A first rewrite matched
      events to throws globally with no visit structure at all, and spatial aliasing
      ate it: players revisit the same wedges, so truth v3.2's annotation fits window
      30 (a visit-6 dart, margin 124) almost as well as window 9 (its own dart,
      margin 137), and the monotone chain that splits truth visit 3 across detected
      visits 4 and 6 out-scored the truth. A detected visit ends at a takeout the
      detector SAW, so its events may not straddle one.
    - A detected visit may span SEVERAL consecutive truth visits, because a MISSED
      takeout merges two real visits into one detected one -- rig-20260922's first
      takeout is exactly that: detected visit 1 holds truth v1.2 (window 3) and truth
      v2.1 (window 6).

    So: detected visits map monotonically onto disjoint consecutive truth-visit
    ranges, events match throws within their range monotonically, and the whole
    two-level assignment maximises the summed MARGIN, cap minus cost. Margin rather
    than match count, because the count is gameable: costs just under the cap are
    abundant on a fixture where every dart leans the same way, and a matcher paid
    per match assembles a chain of barely-admissible junk in preference to a short
    chain of near-zero-cost truth (measured: 8 pairs at median 44-128 px where 5 of
    the detections have annotations 1.6-12 px away).

    A --no-arrival throw never competes for an event AT ALL, which is stronger than
    the pre-#1554 rule (match it, then flag the match suspect) and was forced by a
    measurement: rig-20260922's hand-placed 7 (v1.3) STAYS on the board, a later
    unannotated throw (window 19) landed 2 px from its annotated line, and inside the
    monotone optimisation that 145-margin pairing dragged every visit-2 and visit-3
    match one window late -- the exact phase error this matcher exists to prevent.
    Excluded from the pool, its annotation still earns a SUSPECT-MATCH line for any
    UNMATCHED event that lies within the cap of it, so the diagnostic survives.

    Returns a dict:
      assigned:  {(visit0, event_index): (visit1, dart)}  -- scoreable matches
      suspect:   [((visit0, event_index), (visit1, dart))] -- an unmatched event
                 within the cap of a --no-arrival annotation (reported, never scored)
      undetected:        [(visit1, dart)]  annotated arrivals nothing matched
      recording_absent:  [(visit1, dart)]  --no-arrival throws (the recording's own
                                           fact, not a failure)
      unmatched: [(visit0, event_index)]   detections no annotation claimed
    """
    throws = sorted(k for k in annots.keys() if k not in no_arrival)
    truth_vs = sorted({k[0] for k in throws})

    cost_cache = {}

    def cost_of(ev, key):
        if (id(ev), key) not in cost_cache:
            c = match_cost(ev, annots[key])
            cost_cache[(id(ev), key)] = c if c is not None and c <= MATCH_COST_CAP else None
        return cost_cache[(id(ev), key)]

    def inner(evts, keys):
        """Monotone event-to-throw margin DP within one detected visit and one
        truth-visit range: (total margin, {event_index_in_evts: key})."""
        n, m = len(evts), len(keys)
        f = [[0.0] * (m + 1) for _ in range(n + 1)]
        for i in range(1, n + 1):
            for j in range(1, m + 1):
                best = max(f[i - 1][j], f[i][j - 1])
                c = cost_of(evts[i - 1], keys[j - 1])
                if c is not None:
                    cand = f[i - 1][j - 1] + (MATCH_COST_CAP - c)
                    if cand > best:
                        best = cand
                f[i][j] = best
        matches = {}
        i, j = n, m
        while i > 0 and j > 0:
            if f[i][j] == f[i - 1][j]:
                i -= 1
            elif f[i][j] == f[i][j - 1]:
                j -= 1
            else:
                matches[i - 1] = keys[j - 1]
                i -= 1
                j -= 1
        return f[n][m], matches

    # Outer DP: F[a][b] = best margin over detected visits [0, a) against truth
    # visits truth_vs[0, b), each detected visit taking a consecutive (possibly
    # empty) run of truth visits.
    A, B = len(visits), len(truth_vs)
    F = [[0.0] * (B + 1) for _ in range(A + 1)]
    parent = [[None] * (B + 1) for _ in range(A + 1)]
    inner_cache = {}

    def ranged(a, b0, b1):
        """inner() for detected visit a against truth visits truth_vs[b0:b1]."""
        if (a, b0, b1) not in inner_cache:
            keys = [k for tv in truth_vs[b0:b1] for k in throws if k[0] == tv]
            inner_cache[(a, b0, b1)] = inner(visits[a], keys)
        return inner_cache[(a, b0, b1)]

    for a in range(A + 1):
        for b in range(B + 1):
            if a == 0 and b == 0:
                continue
            best, arg = -1.0, None
            if a > 0 and F[a - 1][b] > best:
                best, arg = F[a - 1][b], ("skip_visit",)
            if b > 0 and F[a][b - 1] > best:
                best, arg = F[a][b - 1], ("skip_truth",)
            if a > 0:
                for b0 in range(b):
                    margin, _ = ranged(a - 1, b0, b)
                    if F[a - 1][b0] + margin > best:
                        best, arg = F[a - 1][b0] + margin, ("range", b0)
            F[a][b] = best
            parent[a][b] = arg

    out = {"assigned": {}, "suspect": [], "undetected": [],
           "recording_absent": sorted(k for k in annots.keys() if k in no_arrival),
           "unmatched": []}
    a, b = A, B
    while a > 0 or b > 0:
        arg = parent[a][b]
        if arg is None:
            break
        if arg[0] == "skip_visit":
            a -= 1
        elif arg[0] == "skip_truth":
            b -= 1
        else:
            b0 = arg[1]
            _, matches = ranged(a - 1, b0, b)
            for ei, key in matches.items():
                out["assigned"][(a - 1, ei)] = key
            a, b = a - 1, b0
    taken = set(out["assigned"].values())
    nearest = {}   # no-arrival key -> (cost, (v, ei)): one SUSPECT line apiece, the
    for v, visit in enumerate(visits):  # nearest unmatched event, not every echo of a
        for ei, ev in enumerate(visit):  # dart that stands in the scene all run long
            if (v, ei) in out["assigned"]:
                continue
            out["unmatched"].append((v, ei))
            for key in out["recording_absent"]:
                c = match_cost(ev, annots[key])
                if c is not None and c <= MATCH_COST_CAP and \
                        (key not in nearest or c < nearest[key][0]):
                    nearest[key] = (c, (v, ei))
    out["suspect"] = sorted((pos, key) for key, (_, pos) in nearest.items())
    for key in throws:
        if key not in taken:
            out["undetected"].append(key)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--truth", required=True)
    ap.add_argument("--annotations", required=True)
    ap.add_argument("--fixture", required=True)
    ap.add_argument("--min-compared", type=int, default=1,
                    help="fewest annotated axis pairs this census must compare to count as an instrument")
    ap.add_argument("--no-arrival", default="",
                    help="comma list of visit.dart the RECORDING never delivers as an "
                         "arrival (a dart parked before frame one, a hand-placed dart, "
                         "a throw that never hit the board): their absence is a "
                         "property of the recording, not a detection failure, and an "
                         "event matched to one is itself suspect")
    args = ap.parse_args()
    no_arrival = set()
    for token in args.no_arrival.split(","):
        token = token.strip()
        if token:
            v, d = token.split(".")
            no_arrival.add((int(v), int(d)))

    truth = read_truth(args.truth)
    annots = read_annotations(args.annotations)
    visits, all_axis = read_run(args.log)
    if not all_axis:
        print("I1511 CENSUS %s: no I1511AXIS lines in %s -- was the run made with "
              "OD_SHAFT_CENSUS=1?" % (args.fixture, args.log))
        return 2

    print("I1511 CENSUS fixture=%s detected_visits=%d truth_visits=%d axis_lines=%d"
          % (args.fixture, len(visits), len(truth), len(all_axis)))

    # ---- COVERAGE / REFUSAL over every advanced window, per camera ----------------------
    windows = sorted({o["window"] for o in all_axis})
    per_cam = {}
    for o in all_axis:
        s = per_cam.setdefault(o["cam"], {"events": 0, "valid": 0, "reasons": {}})
        s["events"] += 1
        if o["valid"]:
            s["valid"] += 1
        else:
            reason = o["refusal"].split(":", 1)[0].strip() or "(unsaid)"
            s["reasons"][reason] = s["reasons"].get(reason, 0) + 1
    print("I1511 COVERAGE windows_advanced=%d" % len(windows))
    for cam in sorted(per_cam):
        s = per_cam[cam]
        reasons = "; ".join("%s=%d" % kv for kv in sorted(s["reasons"].items()))
        print("I1511 COVERAGE cam=%d events=%d valid=%d (%.0f%%) refusals: %s"
              % (cam, s["events"], s["valid"],
                 100.0 * s["valid"] / s["events"] if s["events"] else 0.0,
                 reasons if reasons else "none"))

    # ---- ACCURACY on the annotated subset, spatially matched (#1504, #1554) -------------
    angle_errors = {}
    tip_perp = {}
    err_over_sigma = []
    compared = 0
    assignment = assign_events(visits, annots, no_arrival)
    undetected = ["v%d.%d(%s)" % (key[0], key[1],
                                  list(annots[key].values())[0]["thrown"])
                  for key in assignment["undetected"]]
    unmatched_events = ["v%d#%d(%s)" % (v + 1, ei + 1, visits[v][ei].score)
                        for (v, ei) in assignment["unmatched"]]
    for (v, ei), key in sorted(assignment["suspect"]):
        print("I1511 SUSPECT-MATCH v%d.%d matched by detected event v%d#%d (%s) -- the "
              "recording delivers no arrival for this dart (--no-arrival), so whatever "
              "matched its annotation is not axis accuracy and stays out of the figures"
              % (key[0], key[1], v + 1, ei + 1, visits[v][ei].score))
    for (v, ei), key in sorted(assignment["assigned"].items()):
        ev = visits[v][ei]
        for cam, ann in sorted(annots[key].items()):
            obs = ev.cams.get(cam)
            if not obs:
                continue
            (p1, p2) = ann["line"]
            want_angle = line_angle(p1, p2)
            if obs["valid"]:
                err = angle_diff(want_angle, obs["angle"])
                ref = ann["tip"] if ann["tip"] else ((p1[0] + p2[0]) / 2.0,
                                                     (p1[1] + p2[1]) / 2.0)
                perp = perp_distance(ref, obs["p"], obs["d"])
                angle_errors.setdefault(cam, []).append(err)
                tip_perp.setdefault(cam, []).append(perp)
                if obs["sigma"] > 0:
                    err_over_sigma.append(err / obs["sigma"])
                compared += 1
                print("I1511 PAIR v%d.%d cam=%d thrown=%s angle_obs=%.1f angle_ann=%.1f "
                      "err=%.2f sigma=%.3f perp@%s=%.1f extent=%.0f rms=%.2f shadowPx=%d%s"
                      % (key[0], key[1], cam, ann["thrown"], obs["angle"], want_angle,
                         err, obs["sigma"], "tip" if ann["tip"] else "mid", perp,
                         obs["extent"], obs["rms"], obs["shadowPx"],
                         " note=" + ann["note"] if ann["note"] else ""))
            else:
                print("I1511 PAIR v%d.%d cam=%d thrown=%s REFUSED: %s"
                      % (key[0], key[1], cam, ann["thrown"], obs["refusal"]))

    def med(xs):
        return sorted(xs)[len(xs) // 2] if xs else float("nan")

    for cam in sorted(angle_errors):
        errs, perps = angle_errors[cam], tip_perp[cam]
        print("I1511 ACCURACY cam=%d pairs=%d angle_err median=%.2f p90=%.2f max=%.2f deg | "
              "perp median=%.1f max=%.1f px"
              % (cam, len(errs), med(errs), sorted(errs)[int(len(errs) * 0.9)],
                 max(errs), med(perps), max(perps)))
    if err_over_sigma:
        print("I1511 SIGMA err/sigma n=%d median=%.1f p90=%.1f -- how honest the stated "
              "uncertainty is (annotation floor ~1.5-4 deg dominates, README)"
              % (len(err_over_sigma), med(err_over_sigma),
                 sorted(err_over_sigma)[int(len(err_over_sigma) * 0.9)]))
    if assignment["recording_absent"]:
        print("I1511 RECORDING-FACTS (no arrival exists to detect -- a parked dart, a "
              "hand-placed dart, a throw that never hit the board; not detection "
              "failures): " + ", ".join(
                  "v%d.%d(%s)" % (key[0], key[1],
                                  list(annots[key].values())[0]["thrown"])
                  for key in assignment["recording_absent"]))
    if undetected:
        print("I1511 DETECTION-FAILURES (no event matched an annotated throw, apart from "
              "axis errors, #1504): " + ", ".join(undetected))
    if unmatched_events:
        print("I1511 UNMATCHED-EVENTS (detected, no annotated throw claimed them): "
              + ", ".join(unmatched_events))
    print("I1511 COMPARED pairs=%d" % compared)
    if compared < args.min_compared:
        print("I1511 CENSUS %s: only %d annotated pairs compared (need %d) -- a census "
              "that compared nothing has measured nothing (#1490)"
              % (args.fixture, compared, args.min_compared))
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
