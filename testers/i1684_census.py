# unrun-tester: an instrument a person runs over a 1555 bakeoff's logs (#1684). It counts the shape refusals of the shaft axis and asserts nothing.
"""#1684: how often a camera's shaft axis is refused on a figure's SHAPE, and how many of
those refusals have another dart of the same visit standing beside the new one.

    python3 i1684_census.py --log <run.txt> --fixture <name> --window <dev|opening> \
        [--annotations <fixture.csv> [--no-arrival v.d,...]]
    python3 i1684_census.py --pool <census output> [<census output> ...]

WHAT IS COUNTED. Every I1511AXIS line of a window the vote advanced (the run must have
OD_SHAFT_CENSUS=1, which every #1555 bake-off run has) whose refusal is a SHAPE refusal:
"not straight" (the centreline RMS gate) or "not a shaft" (extent over median width, the
elongation gate -- the width refusal). Frame refusals (no fresh figure, clean, no board)
say nothing about stacking and are only totalled.

WHAT "A SAME-VISIT DART NEARBY" MEANS, two ways, and both are printed:

  ann  (annotated fixtures only) -- the event is joined to its throw by #1504's spatial
       matcher (i1511_census.assign_events, imported, never edited). The throw's
       hand-measured shaft segment in THIS camera is compared with the hand-measured
       segments of the earlier darts of the same truth visit (the darts already standing
       in the board). `annNear` is the smallest segment-to-segment distance, px.
  fit  (every fixture) -- the refused figure's own fitted segment (p +- extent/2 along d,
       printed on the refusal) against the fitted segments of the earlier events of the
       same DETECTED visit in the same camera. The detector's own view: rig-20260929 is
       not annotated, so this is the only one there.

A dart is "nearby" when its centreline segment is within NEAR_PX of the new dart's. 30 px
is ~20 mm on these rigs (1.4-1.5 px/mm at the treble): two darts whose barrels are that
close have flights that overlap in the image, so their fresh figures touch.

    I1684 REFUSAL <fixture> <window> w=<window> cam=<k> class=<c> width=.. rms=.. extent=..
        key=v<v>.<d>|- d_in_visit=<n> annNear=<px|-> fitNear=<px|->
    I1684 TALLY fixture=.. window=.. shape=<n> not_straight=<n> not_a_shaft=<n>
        first_in_visit=<n> fit_near=<n> ann_near=<n> ann_known=<n> events=<n>
"""

import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i1511_census as axis_census  # noqa: E402

NEAR_PX = 30.0


def seg_point(p, a, b):
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((p[0] - ax) * dx + (p[1] - ay) * dy) / L2))
    return math.hypot(p[0] - (ax + t * dx), p[1] - (ay + t * dy))


def seg_seg(s1, s2):
    (a, b), (c, d) = s1, s2
    # proper crossing
    def orient(p, q, r):
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
    o1, o2, o3, o4 = orient(a, b, c), orient(a, b, d), orient(c, d, a), orient(c, d, b)
    if o1 * o2 < 0 and o3 * o4 < 0:
        return 0.0
    return min(seg_point(a, c, d), seg_point(b, c, d), seg_point(c, a, b), seg_point(d, a, b))


def fitted_segment(obs):
    if obs is None or obs["p"][0] < 0 or obs["extent"] <= 0:
        return None
    (px, py), (dx, dy), h = obs["p"], obs["d"], obs["extent"] / 2.0
    return ((px - h * dx, py - h * dy), (px + h * dx, py + h * dy))


def ann_full(row):
    """The annotated barrel segment, reaching the entry where one was measured."""
    (a, b) = row["line"]
    if row["tip"] is None:
        return (a, b)
    t = row["tip"]
    # keep the endpoint farther from the tip, run to the tip
    far = a if math.hypot(a[0] - t[0], a[1] - t[1]) >= math.hypot(b[0] - t[0], b[1] - t[1]) else b
    return (far, t)


def shape_class(refusal):
    if refusal.startswith("not straight"):
        return "not_straight"
    if refusal.startswith("not a shaft"):
        return "not_a_shaft"
    return None


def pool(files):
    tot, rows = {}, 0
    for path in files:
        for raw in open(path, "r", errors="replace"):
            if "I1684 TALLY " not in raw:
                continue
            rows += 1
            print(raw.rstrip())
            for tok in raw.split("I1684 TALLY ", 1)[1].split():
                k, _, v = tok.partition("=")
                if v.isdigit():
                    tot[k] = tot.get(k, 0) + int(v)
    if not rows:
        print("I1684 POOL: no TALLY lines")
        return 2
    print("I1684 POOLED over %d run(s): %s" % (rows, " ".join("%s=%d" % kv for kv in tot.items())))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pool", nargs="*", default=None)
    ap.add_argument("--log")
    ap.add_argument("--fixture", default="?")
    ap.add_argument("--window", default="?")
    ap.add_argument("--annotations")
    ap.add_argument("--no-arrival", default="")
    args = ap.parse_args()
    if args.pool:
        return pool(args.pool)
    if not args.log:
        ap.error("--log is required unless --pool is given")

    visits, _ = axis_census.read_run(args.log)
    key_of = {}
    annots = {}
    if args.annotations:
        no_arrival = set()
        for token in args.no_arrival.split(","):
            if token.strip():
                v, d = token.strip().split(".")
                no_arrival.add((int(v), int(d)))
        annots = axis_census.read_annotations(args.annotations)
        assigned = axis_census.assign_events(visits, annots, no_arrival)["assigned"]
        key_of = dict(assigned)

    tag = "%s %s" % (args.fixture, args.window)
    t = {"events": 0, "shape": 0, "not_straight": 0, "not_a_shaft": 0, "first_in_visit": 0,
         "fit_near": 0, "ann_near": 0, "ann_known": 0, "ann_far": 0, "frame_refusals": 0,
         "valid": 0}
    for vi, visit in enumerate(visits):
        for ei, ev in enumerate(visit):
            t["events"] += 1
            key = key_of.get((vi, ei))
            for cam, obs in sorted(ev.cams.items()):
                if obs["valid"]:
                    t["valid"] += 1
                    continue
                cls = shape_class(obs["refusal"])
                if cls is None:
                    t["frame_refusals"] += 1
                    continue
                t["shape"] += 1
                t[cls] += 1
                if ei == 0:
                    t["first_in_visit"] += 1
                # fit: earlier events of this detected visit, same camera
                mine = fitted_segment(obs)
                fit_near = None
                for prev in visit[:ei]:
                    s = fitted_segment(prev.cams.get(cam))
                    if mine and s:
                        dd = seg_seg(mine, s)
                        fit_near = dd if fit_near is None else min(fit_near, dd)
                if fit_near is not None and fit_near <= NEAR_PX:
                    t["fit_near"] += 1
                # ann: earlier darts of the same truth visit, same camera
                ann_near = None
                if key is not None and key in annots and cam in annots[key]:
                    t["ann_known"] += 1
                    me = ann_full(annots[key][cam])
                    for (v, d), rows in annots.items():
                        if v == key[0] and d < key[1] and cam in rows:
                            dd = seg_seg(me, ann_full(rows[cam]))
                            ann_near = dd if ann_near is None else min(ann_near, dd)
                    if ann_near is not None and ann_near <= NEAR_PX:
                        t["ann_near"] += 1
                    else:
                        t["ann_far"] += 1
                print("I1684 REFUSAL %s w=%d cam=%d class=%s width=%.1f rms=%.2f extent=%.1f "
                      "key=%s d_in_visit=%d annNear=%s fitNear=%s"
                      % (tag, ev.window, cam, cls, obs["width"], obs["rms"], obs["extent"],
                         "v%d.%d" % key if key else "-", ei + 1,
                         "-" if ann_near is None else "%.1f" % ann_near,
                         "-" if fit_near is None else "%.1f" % fit_near))
    # the retry's own census (OD_SHAFT_CENSUS=1 runs of a binary with #1684's retry):
    # fired = retries made, passed = retries that passed every gate, adopted = used.
    for raw in open(args.log, "r", errors="replace"):
        line = axis_census.ANSI.sub("", raw)
        if "I1684STACK " not in line:
            continue
        body = line.split("I1684STACK ", 1)[1].rstrip()
        f = dict(tok.partition("=")[::2] for tok in body.split(" refusal=", 1)[0].split())
        t["stack_fired"] = t.get("stack_fired", 0) + 1
        t["stack_passed"] = t.get("stack_passed", 0) + (1 if f.get("retryValid") == "1" else 0)
        t["stack_adopted"] = t.get("stack_adopted", 0) + (1 if f.get("adopted") == "1" else 0)
        print("I1684 STACK %s %s" % (tag, body))
    print("I1684 TALLY fixture=%s window=%s %s" % (
        args.fixture, args.window, " ".join("%s=%d" % kv for kv in t.items())))
    return 0 if t["events"] else 2


if __name__ == "__main__":
    sys.exit(main())
