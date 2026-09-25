"""#1586: WHY the geometry refuses a dart -- the cause census of TOO-FEW-CONSTRAINTS.

    python3 i1586_census.py --log <run.txt> --truth <table.md> --annotations <fixture.csv> \
        --fixture <name> --window <dev|opening> [--no-arrival v.d,v.d]
    python3 i1586_census.py --pool <census output> [<census output> ...]

WHAT IT READS. One detector run made with OD_GEO_SCORE=on AND OD_SHAFT_CENSUS=1 -- the
runs #1555's harness already makes, so this census costs no replay of its own when it is
pointed at them. Per called dart the run prints, in this order: one I1511AXIS line per
camera (the axis the camera fitted or refused, with the refusal's own words), one
I1512CAM line per camera (usable, excluded, and the exclusion `constraintFrom` named),
the I1512ENTRY verdict, and the I1555PUBLISH line. The detection-to-throw join is
#1504's spatial matcher, imported from i1511_census exactly as i1555_census imports it,
so this census and the bake-off can never disagree about which detection was which dart.
Neither of those files is edited here (#1587 owns them); both are only imported.

WHAT IT PRINTS, per fixture and window:

    I1586 REFUSED v<visit>.<dart> window=<w> thrown=<score> usable=<n>/<offered>
        one line per refused matched dart, then for each camera
    I1586 CAM v<visit>.<dart> cam=<k> usable=<0|1> class=<reason class> saw=<0|1> \
        axisErrDeg=<deg|-> axisOffPx=<px|-> excl=<the exclusion, verbatim>

`saw` is whether the hand annotation (testers/i1511_annotations) holds a line for this
dart on this camera -- i.e. whether the camera could see the dart at all. `axisErrDeg`
and `axisOffPx` compare the line the camera FITTED (the I1511AXIS p/d, printed even when
the axis was refused wherever a fit was attempted) against that annotated line: the angle
between them and the perpendicular distance of the annotated entry from the fitted line.
Together they answer the issue's question per camera: did the camera see the dart, and is
the axis it refused sound?

    I1586 CAUSE class=<reason class> cameras=<n> saw=<n> sound=<n>
        the per-reason count over every unusable camera on a refused dart, and of those
        how many saw the dart and how many held a sound fitted line (angle within
        SOUND_DEG of the annotation and entry within SOUND_PX of the line).
    I1586 TALLY fixture=... window=... matched=<n> refused=<n> <class>=<n> ...
        the pooled line; --pool sums them.

THE PREDICTION, STATED BEFORE THIS CENSUS WAS FIRST RUN (committed in this file ahead of
the run, so the history shows it came first):

    1. rig-20260922's dev window: camera 1 is excluded as "no accepted board fit" on
       every one of its 10 refusals -- the #1510 wire-coherence gate (0.578 against 0.60)
       costs that window a whole camera, so a dart there needs BOTH remaining cameras to
       produce an axis, and one axis refusal is enough to refuse the dart.
    2. Pooled over all four runs, the dominant per-camera reason is NOT a frame refusal
       but an AXIS refusal ("no usable axis: ..."), and within the axis refusals the
       largest class is "not straight" -- the new dart's silhouette merged with a dart
       already on the board (darts 2 and 3 of a visit), which the straightness gate
       reads as two objects one line does not explain.
    3. So the repair, if one is justified, is in how an axis is read on a crowded board,
       and the census's `saw` and `axisErrDeg` columns decide whether the refused lines
       were sound (repairable) or really absent (detection's problem, stop).

A reporter in i1555_census.py's mould: it decides nothing about the numbers. Exit 0 when
the run parsed and matched something, 2 when there was nothing to census.
"""

import argparse
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i1511_census as axis_census  # noqa: E402  (the matcher, #1504's alignment)
import i1555_census as bakeoff      # noqa: E402  (the publish-block binder)

ANSI = re.compile(r"\x1b\[[0-9;]*m")
CAM_RE = re.compile(r"I1512CAM window=(\d+) cam=(\d+) usable=(\d) excluded=(\d) .* excl=(.*)$")

# What "sound" means for a fitted-but-refused line, against the hand annotation. The
# annotation's own floor is ~1.5-4 deg by eye and ~0.5-1 deg fitted (i1511_annotations'
# README), and the axis census reports p90 ~1 deg on admitted axes; 5 deg and 15 px are
# generous on purpose -- a line this census calls sound is one a reader would call the
# dart, not one the gates would admit.
SOUND_DEG = 5.0
SOUND_PX = 15.0


def reason_class(excl):
    """The exclusion reduced to its named reason, numbers stripped."""
    e = excl.strip()
    if e.startswith("no usable axis: "):
        tail = e[len("no usable axis: "):]
        return "axis:" + tail.split(":", 1)[0].strip().replace(" ", "-")
    if e.startswith("inconsistent"):
        return "inconsistent"
    return e.split(",", 1)[0].split("(", 1)[0].strip().replace(" ", "-")


def read_cam_lines(path):
    """window -> {cam (1-based) -> (usable, excluded, exclusion)}."""
    out = {}
    for raw in open(path, "r", errors="replace"):
        m = CAM_RE.search(ANSI.sub("", raw))
        if m:
            out.setdefault(int(m.group(1)), {})[int(m.group(2))] = (
                m.group(3) == "1", m.group(4) == "1", m.group(5).strip())
    return out


def compare(axis, annot):
    """(angle error deg, entry offset px) of a fitted line against an annotated one."""
    if axis is None or annot is None:
        return None, None
    dx, dy = axis["d"]
    if abs(dx) + abs(dy) < 1e-6 or axis["p"][0] < 0:
        return None, None
    (x1, y1), (x2, y2) = annot["line"]
    aang = axis_census.line_angle((x1, y1), (x2, y2))
    fang = math.degrees(math.atan2(dy, dx)) % 180.0
    err = axis_census.angle_diff(aang, fang)
    entry = annot["tip"] if annot["tip"] is not None else (x2, y2)
    off = axis_census.perp_distance(entry, axis["p"], (dx, dy))
    return err, off


def pool(files):
    totals, rows = {}, 0
    for path in files:
        for raw in open(path, "r", errors="replace"):
            if "I1586 TALLY " not in raw:
                continue
            rows += 1
            print(raw.rstrip())
            for tok in raw.split("I1586 TALLY ", 1)[1].split():
                k, _, v = tok.partition("=")
                if k in ("fixture", "window") or not v.isdigit():
                    continue
                totals[k] = totals.get(k, 0) + int(v)
    if not rows:
        print("I1586 POOL: no TALLY lines -- nothing to pool")
        return 2
    head = "matched=%d refused=%d" % (totals.pop("matched", 0), totals.pop("refused", 0))
    print("I1586 POOLED over %d run(s): %s | %s" % (
        rows, head, " ".join("%s=%d" % kv for kv in sorted(totals.items(), key=lambda kv: -kv[1]))))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pool", nargs="*", default=None)
    ap.add_argument("--log")
    ap.add_argument("--truth")
    ap.add_argument("--annotations")
    ap.add_argument("--fixture")
    ap.add_argument("--window", default="?")
    ap.add_argument("--no-arrival", default="")
    args = ap.parse_args()
    if args.pool:
        return pool(args.pool)
    for needed in ("log", "truth", "annotations", "fixture"):
        if not getattr(args, needed):
            ap.error("--%s is required unless --pool is given" % needed)

    no_arrival = set()
    for token in args.no_arrival.split(","):
        if token.strip():
            v, d = token.strip().split(".")
            no_arrival.add((int(v), int(d)))

    annots = axis_census.read_annotations(args.annotations)
    visits, _ = axis_census.read_run(args.log)
    publishes, _ = bakeoff.read_publish_blocks(args.log)
    cams = read_cam_lines(args.log)
    flat = [ev for visit in visits for ev in visit]
    for ev, block in zip(flat, publishes):
        ev.pub = block
    for ev in flat:
        if not hasattr(ev, "pub"):
            ev.pub = None

    assignment = axis_census.assign_events(visits, annots, no_arrival)
    assigned = assignment["assigned"]
    suspect = set(k for k, _ in assignment["suspect"])
    tag = "fixture=%s window=%s" % (args.fixture, args.window)

    matched = refused = 0
    per_class = {}     # class -> [cameras, saw, sound]
    per_dart = {}      # the set of classes that cost a dart, as a word
    for v, visit in enumerate(visits):
        for ei, ev in enumerate(visit):
            if (v, ei) not in assigned or ev.pub is None or (v, ei) in suspect:
                continue
            matched += 1
            if ev.pub["outcome"] != "TOO-FEW-CONSTRAINTS":
                continue
            refused += 1
            key = assigned[(v, ei)]
            thrown = bakeoff.norm(list(annots[key].values())[0]["thrown"])
            w = ev.pub["window"]
            camrows = cams.get(w, {})
            usable = sum(1 for c in camrows.values() if c[0] and not c[1])
            print("I1586 REFUSED v%d.%d window=%d thrown=%s usable=%d/%d published=%s %s"
                  % (key[0], key[1], w, thrown, usable, len(camrows),
                     bakeoff.norm(ev.pub["score"]),
                     bakeoff.verdict(ev.pub["score"], thrown)))
            classes = []
            for cam in sorted(camrows):
                ok, excluded, excl = camrows[cam]
                annot = annots[key].get(cam)
                err, off = compare(ev.cams.get(cam), annot)
                if ok and not excluded:
                    cls = "-"
                else:
                    cls = reason_class(excl)
                    classes.append(cls)
                    agg = per_class.setdefault(cls, [0, 0, 0])
                    agg[0] += 1
                    agg[1] += 1 if annot is not None else 0
                    agg[2] += 1 if (err is not None and err <= SOUND_DEG and
                                    off <= SOUND_PX) else 0
                print("I1586 CAM v%d.%d cam=%d usable=%d class=%s saw=%d axisErrDeg=%s "
                      "axisOffPx=%s excl=%s"
                      % (key[0], key[1], cam, 1 if (ok and not excluded) else 0, cls,
                         1 if annot is not None else 0,
                         "-" if err is None else "%.1f" % err,
                         "-" if off is None else "%.1f" % off,
                         "-" if (ok and not excluded) else excl))
            word = "+".join(sorted(classes)) or "none"
            per_dart[word] = per_dart.get(word, 0) + 1

    if not matched:
        print("I1586 CENSUS %s: nothing matched (#1490)" % tag)
        return 2
    for cls, (n, saw, sound) in sorted(per_class.items(), key=lambda kv: -kv[1][0]):
        print("I1586 CAUSE %s class=%s cameras=%d saw=%d sound=%d" % (tag, cls, n, saw, sound))
    for word, n in sorted(per_dart.items(), key=lambda kv: -kv[1]):
        print("I1586 DART-CAUSE %s darts=%d unusable=%s" % (tag, n, word))
    print("I1586 TALLY fixture=%s window=%s matched=%d refused=%d %s"
          % (args.fixture, args.window, matched, refused,
             " ".join("%s=%d" % (cls, agg[0]) for cls, agg in sorted(per_class.items()))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
