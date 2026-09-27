"""#1653: where does each camera's board model put the rings and the wires -- per camera,
against the hand annotations, under named variants of the model's radial scale.

    python3 i1653_model_census.py --log <run.txt> --annotations <fixture.csv> \
        --fixture <name> --window <word> [--variant NAME ...] [--rows]

Reads one detector run from a build that logs I1647FIT (each camera's whole board model)
and the `board model [...]` calibration line (the model's own residual for every traced
ring, median over 120 rays). No event matching is needed: a run calibrates once per
camera, so every annotated dart's hand-drawn shaft lines are carried through the models
the run used, and their equal-weight crossing ("annX", #1647's parallax-free reference)
is compared with the dart's TRUE region from the annotation's `thrown` column:

    radial margin   signed mm inside the true ring's band (99..107 for a treble,
                    162..170 for a double, 15.9..99 or 107..162 for a single; negative
                    means the model puts the dart OUTSIDE its true ring)
    wedge margin    signed mm of arc inside the true wedge at annX's radius

Per camera it prints the traced rings in model millimetres (from the calibration line):
each band's centre (bloom-free where the band's two edges bloom alike) against spec.

VARIANTS (radial scale only; wire angles are left as the run fitted them):
    asis        the model as the run used it (#1499's de-bias from the doubles marks)
    outer       the outer doubles mark alone (no de-bias, #1499's predecessor)
    treble      each camera's scale taken from its traced TREBLE band centre (103 mm)
    four        the mean of the doubles- and treble-centre scales

A reporter: it decides nothing and asserts nothing about the numbers.
"""

import argparse
import csv
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from i1647_radius_census import FIT_RE, ANSI, Model, crossing  # noqa: E402

BOARD_RE = re.compile(r"Camera (\d) board model \[.*?factor ([0-9.]+), .*?residuals (.*?) -- signed")
RING_RE = re.compile(r"(the bullseye|the 25 ring|the treble ring's inner edge|the treble ring's outer edge|"
                     r"the doubles ring's inner edge|the doubles ring) \[[a-z-]+\] ([-+][0-9.]+) mm")
KEYS = {"the bullseye": "b50", "the 25 ring": "b25", "the treble ring's inner edge": "tin",
        "the treble ring's outer edge": "tout", "the doubles ring's inner edge": "din",
        "the doubles ring": "dout"}
SEQ = [20, 1, 18, 4, 13, 6, 10, 15, 2, 17, 3, 19, 7, 16, 8, 11, 14, 9, 12, 5]
SPEC = {"b50": 6.35, "b25": 15.9, "tin": 99.0, "tout": 107.0, "din": 162.0, "dout": 170.0}


def truth_of(thrown):
    t = thrown.strip().upper()
    if t in ("MISS", "BULL", "DBULL", "25", "OUTER", "THROWN", ""):
        return None
    ring = "S"
    if t[0] in "TD":
        ring, t = t[0], t[1:]
    try:
        return ring, int(t)
    except ValueError:
        return None


def read_models(log):
    models, board = {}, {}
    for raw in open(log, "r", errors="replace"):
        line = ANSI.sub("", raw)
        m = FIT_RE.search(line)
        if m:
            models[int(m.group(2))] = Model(m)
            continue
        m = BOARD_RE.search(line)
        if m:
            res = {KEYS[k]: float(v) for k, v in RING_RE.findall(m.group(3))}
            board.setdefault(int(m.group(1)), []).append((float(m.group(2)), res))
    # The calibration line that belongs to the model the run used: same de-bias factor.
    rings = {}
    for cam, mdl in models.items():
        f = mdl.unit * 170.0
        cands = board.get(cam, [])
        if cands:
            rings[cam] = min(cands, key=lambda fr: abs(fr[0] - f))
    return models, rings


def read_annotations(path):
    per = {}
    for row in csv.DictReader(open(path, "r")):
        try:
            key = (int(row["visit"]), int(row["dart"]))
            cam = int(row["camera"])
            line = ((float(row["x1"]), float(row["y1"])), (float(row["x2"]), float(row["y2"])))
        except (ValueError, KeyError):
            continue
        d = per.setdefault(key, {"thrown": row["thrown"], "lines": {}})
        d["lines"][cam] = line
    return per


def scale_for(variant, cam, rings):
    """What the camera's model millimetres are multiplied by under a variant."""
    if variant == "asis" or cam not in rings:
        return 1.0
    f, res = rings[cam]
    if variant == "outer":
        return 1.0 / f  # unit f/170 -> 1/170: millimetres shrink by f
    dc = 0.5 * (res.get("din", 0.0) + res.get("dout", 0.0))   # doubles centre residual
    tc = 0.5 * (res.get("tin", 0.0) + res.get("tout", 0.0))   # treble centre residual
    s_t = 103.0 / (103.0 + tc)
    s_d = 166.0 / (166.0 + dc)
    if variant == "treble":
        return s_t
    if variant == "four":
        return 0.5 * (s_t + s_d)
    raise ValueError(variant)


def margins(ring, wedge, x, y):
    r = math.hypot(x, y)
    phi = math.degrees(math.atan2(y, x)) % 360.0
    if ring == "T":
        lo, hi = 99.0, 107.0
    elif ring == "D":
        lo, hi = 162.0, 170.0
    else:
        lo, hi = (15.9, 99.0) if r < 103.0 else (107.0, 162.0)
    rad = min(r - lo, hi - r)
    k = SEQ.index(wedge)
    a0 = 18.0 * k
    d = (phi - a0) % 360.0
    ang = min(d, 18.0 - d) if d <= 18.0 else -min(d - 18.0, 360.0 - d)
    return r, phi, rad, math.radians(ang) * r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--annotations", required=True)
    ap.add_argument("--fixture", required=True)
    ap.add_argument("--window", required=True)
    ap.add_argument("--variant", action="append")
    ap.add_argument("--rows", action="store_true")
    args = ap.parse_args()
    variants = args.variant or ["asis", "outer", "treble", "four"]
    models, rings = read_models(args.log)
    ann = read_annotations(args.annotations)

    for cam in sorted(models):
        m = models[cam]
        f, res = rings.get(cam, (float("nan"), {}))
        txt = " ".join("%s=%+.1f" % (k, res[k]) for k in ("b50", "b25", "tin", "tout", "din", "dout") if k in res)
        tc = 0.5 * (res.get("tin", 0) + res.get("tout", 0))
        dc = 0.5 * (res.get("din", 0) + res.get("dout", 0))
        print("I1653 CAM fixture=%s window=%s cam=%d deBias=%.4f theta20=%.4fdeg %s "
              "trebleCentre=%+.2f doublesCentre=%+.2f trebleHalfWidth=%.2f doublesHalfWidth=%.2f "
              "scale[treble]=%.4f"
              % (args.fixture, args.window, cam, m.unit * 170.0, math.degrees(m.theta20), txt, tc, dc,
                 0.5 * (res.get("tout", 0) - res.get("tin", 0)) + 4.0,
                 0.5 * (res.get("dout", 0) - res.get("din", 0)) + 4.0, scale_for("treble", cam, rings)))

    for variant in variants:
        out = []
        for key in sorted(ann):
            d = ann[key]
            tr = truth_of(d["thrown"])
            if tr is None:
                continue
            lines = []
            for cam, (p, q) in sorted(d["lines"].items()):
                mdl = models.get(cam)
                if mdl is None or not mdl.resolved:
                    continue
                s = scale_for(variant, cam, rings)
                (px, py), (dx, dy) = mdl.line(p, q)
                lines.append(((px * s, py * s), (dx, dy)))
            if len(lines) < 2:
                continue
            x = crossing(lines)
            if x is None:
                continue
            r, phi, rad, ang = margins(tr[0], tr[1], *x)
            out.append((key, d["thrown"], len(lines), r, phi, rad, ang))
        bad_r = [o for o in out if o[5] < 0]
        bad_a = [o for o in out if o[6] < 0]
        tre = [o[3] for o in out if o[1].upper().startswith("T")]
        print("I1653 VARIANT fixture=%s window=%s variant=%s darts=%d ringWrong=%d wedgeWrong=%d "
              "trebleMeanR=%s ringWrongRows=[%s] wedgeWrongRows=[%s]"
              % (args.fixture, args.window, variant, len(out), len(bad_r), len(bad_a),
                 ("%.2f" % (sum(tre) / len(tre))) if tre else "-",
                 " ".join("v%d.%d:%s@%.1f(%+.1f)" % (o[0][0], o[0][1], o[1], o[3], o[5]) for o in bad_r),
                 " ".join("v%d.%d:%s@%.1fdeg(%+.2fmm)" % (o[0][0], o[0][1], o[1], o[4], o[6]) for o in bad_a)))
        if args.rows:
            for o in out:
                print("I1653 ROW variant=%s v%d.%d thrown=%s lines=%d r=%.2f phi=%.2f radMargin=%+.2f "
                      "wedgeMargin=%+.2f" % ((variant,) + o[0] + o[1:]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
