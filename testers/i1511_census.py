"""#1511: the shaft-axis census -- accuracy against hand-measured lines, AND the
coverage/refusal table over every event, because the acceptance demands both and a
census of only the plausible lines has measured nothing.

    python3 i1511_census.py --log <run.txt> --truth <table.md> \
        --annotations <fixture.csv> --fixture <name>

Reads one detector run made with OD_SHAFT_CENSUS=1. Every window the vote advanced
prints one I1511AXIS line per camera slot (abstentions included, with their refusal);
the published SCORE line that follows binds that window to a detected dart, and END
lines split visits.

ALIGNMENT IS #1504-AWARE. Detected visits align to ground-truth visits in order, but
WITHIN a visit a detected dart is matched to an annotated throw SPATIALLY -- the
assignment minimising the summed distance from each annotation's line to the detected
observation (valid axes matched line-to-line; otherwise the published tip against the
annotated entry) -- never merely by order, so an undetected throw shifts nothing.
Unmatched annotations are DETECTION failures and unmatched detections are reported,
both apart from axis errors.

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
AXIS_RE = re.compile(
    r"I1511AXIS window=(\d+) opened=(\d+) closed=(\d+) cam=(\d+) valid=(\d) "
    r"p=\(([-0-9.]+),([-0-9.]+)\) d=\(([-0-9.]+),([-0-9.]+)\) angle=([-0-9.]+) "
    r"extent=([-0-9.]+) width=([-0-9.]+) rms=([-0-9.]+) sigma=([-0-9.]+) px=(\d+) "
    r"cols=(\d+) trimmed=(\d+) frac=([-0-9.]+) tipGap=([-0-9.]+) refusal=(.*)"
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
                "refusal": m.group(20).strip(),
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--truth", required=True)
    ap.add_argument("--annotations", required=True)
    ap.add_argument("--fixture", required=True)
    ap.add_argument("--min-compared", type=int, default=1,
                    help="fewest annotated axis pairs this census must compare to count as an instrument")
    args = ap.parse_args()

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

    # ---- ACCURACY on the annotated subset, spatially matched (#1504) --------------------
    angle_errors = {}
    tip_perp = {}
    err_over_sigma = []
    compared = 0
    undetected, unmatched_events = [], []
    for v in range(max(len(visits), len(truth))):
        events = list(visits[v]) if v < len(visits) else []
        throws = [d for d in (1, 2, 3) if (v + 1, d) in annots]
        # every (event, annotated throw) cost, then greedy best-first assignment
        pairs = []
        for ei, ev in enumerate(events):
            for d in throws:
                c = match_cost(ev, annots[(v + 1, d)])
                if c is not None:
                    pairs.append((c, ei, d))
        pairs.sort()
        taken_e, taken_d, assigned = set(), set(), {}
        for c, ei, d in pairs:
            if ei in taken_e or d in taken_d or c > 150.0:
                continue
            taken_e.add(ei)
            taken_d.add(d)
            assigned[ei] = d
        for d in throws:
            if d not in taken_d:
                undetected.append("v%d.%d(%s)" % (v + 1, d,
                                 list(annots[(v + 1, d)].values())[0]["thrown"]))
        for ei, ev in enumerate(events):
            if ei not in assigned:
                unmatched_events.append("v%d#%d(%s)" % (v + 1, ei + 1, ev.score))
                continue
            d = assigned[ei]
            for cam, ann in sorted(annots[(v + 1, d)].items()):
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
                          "err=%.2f sigma=%.3f perp@%s=%.1f extent=%.0f rms=%.2f%s"
                          % (v + 1, d, cam, ann["thrown"], obs["angle"], want_angle,
                             err, obs["sigma"], "tip" if ann["tip"] else "mid", perp,
                             obs["extent"], obs["rms"],
                             " note=" + ann["note"] if ann["note"] else ""))
                else:
                    print("I1511 PAIR v%d.%d cam=%d thrown=%s REFUSED: %s"
                          % (v + 1, d, cam, ann["thrown"], obs["refusal"]))

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
