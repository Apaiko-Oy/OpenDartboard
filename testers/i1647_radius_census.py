"""#1647: where does the solved entry's radius come from -- the per-row, per-camera table.

    python3 i1647_radius_census.py --log <run.txt> --annotations <fixture.csv> \
        --fixture <name> --window <word> [--no-arrival 1.1] [--all]

Reads one detector run made with OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 on a build that logs
I1647FIT (each camera's whole board model) and I1641LOCAL (each camera's traced treble
edges at the entry's angle) -- the four logs `testers/run_all.sh 1555-bakeoff` leaves in
$OD_RUNS_BASE/1555/ (r18-dev.txt, r18-open.txt, r22-dev.txt, r22-open.txt; rig-20260922
takes --no-arrival 1.1, as i1555_inside.sh's census does). Events are bound to annotated
throws by i1511's matcher, imported, so this census cannot disagree with #1512's or
#1555's about which dart is which.

For every matched, solved dart it states the radius three ways, each one step further
from the pixels the hand annotation measured:

    tip       the annotated board-contact pixel of camera c, carried to board mm through
              camera c's OWN model (the same H, scale and anchor the solve used). NOT a
              radius reference: the visible contact pixel is usually a point of the
              dart ABOVE the surface, and a pixel off the plane lands away from the
              camera (#1512's parallax trap) -- its 3-15 mm spread over cameras is that.
    annX      the least-squares crossing of the ANNOTATED shaft lines, transported
              exactly as solveEntry transports the fitted ones (equal weights). This is
              the parallax-free reference: the image's own answer to "where is the dart
              in this board model", with perfect lines.
    solved    I1512ENTRY's r -- the fitted axes, weighted, as scored.

and per camera: fitOff, the fitted axis line's signed offset from annX (mm, board);
imgOff, the fitted axis against the annotated one in the IMAGE (px, + = image-right);
annOff, the annotated line's own offset from annX; and the camera's own traced
inner/outer treble edges at the entry's angle (I1641LOCAL in0/out0) with #1553's
adjustments.

MEASURED (2026-09-27, all four windows, 59 solved matched rows; the numbers and the
table are on turnaus#1647): solved - annX is +0.46 mm mean, sd 1.82 (16 treble rows:
+0.51, sd 2.14). The fitted entry carries no inward radial bias against the image; the
inward reading lives in the board model, which puts the hand-annotated crossing of
r18 v3.3 (T20) at 96.3 mm and of r22 v3.2 (D20, unsolved) at 158.8-159.7 mm. imgOff is
+1.8 to +5.0 px median on all six camera-fixture pairs (a lateral, mostly tangential
error of the fitted axes, cause not named).

A reporter: it decides nothing and asserts nothing about the numbers.
"""

import argparse
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i1511_census as axis_census  # noqa: E402
import i1512_census as entry_census  # noqa: E402

ANSI = re.compile(r"\x1b\[[0-9;]*m")
FIT_RE = re.compile(
    r"I1647FIT window=(-?\d+) cam=(\d+) resolved=(\d) theta20=([-0-9.eE+]+) "
    r"advance=([-0-9.]+) unitPerMm=([-0-9.eE+]+) H=(\S+) bull=([-0-9.]+),([-0-9.]+)")
LOCAL_RE = re.compile(
    r"I1641LOCAL window=(-?\d+) cam=(\d+) r=([-0-9.]+) in0=([-0-9.]+) in9=([-0-9.]+) "
    r"inMed=([-0-9.]+) out0=([-0-9.]+) out9=([-0-9.]+) outMed=([-0-9.]+) "
    r"inAdj=([-0-9.]+) outAdj=([-0-9.]+)")

# WDF spec radii, mm (perspective_processing::DartboardSpec).
INNER_TREBLE, OUTER_TREBLE, INNER_DOUBLE, OUTER_DOUBLE = 99.0, 107.0, 162.0, 170.0


def inv3(m):
    a, b, c, d, e, f, g, h, i = m
    A = e * i - f * h
    B = -(d * i - f * g)
    C = d * h - e * g
    det = a * A + b * B + c * C
    return [A / det, -(b * i - c * h) / det, (b * f - c * e) / det,
            B / det, (a * i - c * g) / det, -(a * f - c * d) / det,
            C / det, -(a * h - b * g) / det, (a * e - b * d) / det]


class Model(object):
    def __init__(self, m):
        self.resolved = m.group(3) == "1"
        self.theta20 = float(m.group(4))
        self.advance = float(m.group(5))
        self.unit = float(m.group(6))
        self.H = [float(v) for v in m.group(7).split(",")]
        self.Hinv = inv3(self.H)

    def canon(self, u, v):
        """Image pixel -> canonical board mm (the numbered frame solveEntry uses)."""
        q = self.Hinv
        x = q[0] * u + q[1] * v + q[2]
        y = q[3] * u + q[4] * v + q[5]
        w = q[6] * u + q[7] * v + q[8]
        mx, my = x / w / self.unit, y / w / self.unit
        r = math.hypot(mx, my)
        phi = self.advance * (math.atan2(my, mx) - self.theta20)
        return (r * math.cos(phi), r * math.sin(phi))

    def image(self, canon):
        """Canonical board mm -> image pixel (the inverse of canon)."""
        r = math.hypot(*canon)
        phi = math.atan2(canon[1], canon[0])
        theta = self.theta20 + self.advance * phi
        x, y = r * math.cos(theta) * self.unit, r * math.sin(theta) * self.unit
        h = self.H
        w = h[6] * x + h[7] * y + h[8]
        return ((h[0] * x + h[1] * y + h[2]) / w, (h[3] * x + h[4] * y + h[5]) / w)

    def line(self, p, q):
        """An image line through p and q, transported: (point, unit direction), canonical."""
        a = self.canon(*p)
        b = self.canon(*q)
        dx, dy = b[0] - a[0], b[1] - a[1]
        n = math.hypot(dx, dy)
        return a, (dx / n, dy / n)


def crossing(lines):
    """Equal-weight least-squares point of 2+ lines (point, dir)."""
    a11 = a12 = a22 = b1 = b2 = 0.0
    for (px, py), (dx, dy) in lines:
        nx, ny = -dy, dx
        c = -(nx * px + ny * py)
        a11 += nx * nx
        a12 += nx * ny
        a22 += ny * ny
        b1 -= nx * c
        b2 -= ny * c
    det = a11 * a22 - a12 * a12
    if abs(det) < 1e-9:
        return None
    return ((a22 * b1 - a12 * b2) / det, (a11 * b2 - a12 * b1) / det)


def signed_off(point, line):
    """Signed perpendicular offset of `line` from `point`, and its radial part."""
    (px, py), (dx, dy) = line
    nx, ny = -dy, dx
    off = nx * (px - point[0]) + ny * (py - point[1])
    r = math.hypot(*point)
    radial = off * (nx * point[0] + ny * point[1]) / r if r > 0 else 0.0
    return off, radial


def ring_of(thrown):
    t = thrown.upper()
    if t.startswith("T"):
        return "T"
    if t.startswith("D"):
        return "D"
    if t in ("BULL", "DBULL", "MISS", "OUTER"):
        return t
    return "S"


def truth_band(ring, r_solved):
    """The spec radius interval the truth's ring puts the dart in."""
    if ring == "T":
        return (INNER_TREBLE, OUTER_TREBLE)
    if ring == "D":
        return (INNER_DOUBLE, OUTER_DOUBLE)
    if ring == "S":
        # the single the solved radius is nearer: inner below the treble's middle
        return (15.9, INNER_TREBLE) if r_solved < 103 else (OUTER_TREBLE, INNER_DOUBLE)
    return (float("nan"), float("nan"))


def collect(log, annotations, no_arrival_text, want_all):
    """Every matched, solved row, with each camera's model, lines and edges."""
    no_arrival = set()
    for tok in no_arrival_text.split(","):
        if tok.strip():
            v, d = tok.strip().split(".")
            no_arrival.add((int(v), int(d)))

    models, local = {}, {}
    for raw in open(log, "r", errors="replace"):
        line = ANSI.sub("", raw)
        m = FIT_RE.search(line)
        if m:
            models[(int(m.group(1)), int(m.group(2)))] = Model(m)
            continue
        m = LOCAL_RE.search(line)
        if m:
            local[(int(m.group(1)), int(m.group(2)))] = {
                "in0": float(m.group(4)), "out0": float(m.group(7)),
                "inMed": float(m.group(6)), "outMed": float(m.group(9)),
                "inAdj": float(m.group(10)), "outAdj": float(m.group(11))}

    annots = axis_census.read_annotations(annotations)
    visits, _ = entry_census.read_run_with_entries(log)
    assignment = axis_census.assign_events(visits, annots, no_arrival)
    rows = []
    for (v, ei), key in sorted(assignment["assigned"].items(), key=lambda kv: kv[1]):
        ev = visits[v][ei]
        if ev.geo is None or not ev.geo[0]["solved"]:
            continue
        entry, cams = ev.geo
        w = entry["window"]
        ann = annots[key]
        thrown = list(ann.values())[0]["thrown"]
        ring = ring_of(thrown)
        rs = entry["r"]
        near = (ring in ("T", "D") or abs(rs - INNER_TREBLE) < 12 or
                abs(rs - OUTER_TREBLE) < 12 or abs(rs - INNER_DOUBLE) < 12)
        if not want_all and not near:
            continue
        solved = (entry["x"], entry["y"])
        ann_lines, tips, per = [], [], {}
        for cam in (1, 2, 3):
            mdl = models.get((w, cam))
            a = ann.get(cam)
            c = {"tip": None, "lineOff": None, "lineRad": None, "fitOff": None,
                 "used": cam in cams and cams[cam]["usable"] and not cams[cam]["excluded"]}
            if mdl is not None and mdl.resolved and a is not None:
                if a["tip"] is not None:
                    t = mdl.canon(*a["tip"])
                    c["tip"] = math.hypot(*t)
                    tips.append(t)
                c["annLine"] = mdl.line(*a["line"])
                ann_lines.append(c["annLine"])
            axis = ev.cams.get(cam)
            c["axis"] = axis if (axis is not None and axis["valid"]) else None
            c["mdl"] = mdl if (mdl is not None and mdl.resolved) else None
            if mdl is not None and mdl.resolved and axis is not None and axis["valid"]:
                p = axis["p"]
                q = (p[0] + 100.0 * axis["d"][0], p[1] + 100.0 * axis["d"][1])
                c["fitLine"] = mdl.line(p, q)
            c["local"] = local.get((w, cam))
            c["sigmaPerp"] = cams[cam]["sigmaPerp"] if cam in cams else None
            per[cam] = c
        annx = crossing(ann_lines) if len(ann_lines) >= 2 else None
        for cam, c in per.items():
            ref = annx if annx is not None else solved
            if "fitLine" in c:
                c["fitOff"], c["fitRad"] = signed_off(ref, c["fitLine"])
            if "annLine" in c:
                c["lineOff"], c["lineRad"] = signed_off(ref, c["annLine"])
            # The fitted axis against the annotated one IN THE IMAGE, px, at the
            # annotated crossing's image: + means the fitted line lies image-RIGHT.
            c["imgOff"] = None
            if c["axis"] is not None and c["mdl"] is not None and annx is not None:
                u, v = c["mdl"].image(annx)
                dx, dy = c["axis"]["d"]
                nx, ny = dy, -dx
                if nx < 0:
                    nx, ny = -nx, -ny
                px, py = c["axis"]["p"]
                c["imgOff"] = nx * (px - u) + ny * (py - v)
        rows.append((key, thrown, ring, entry, solved, annx, tips, per))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--annotations", required=True)
    ap.add_argument("--fixture", required=True)
    ap.add_argument("--window", required=True)
    ap.add_argument("--no-arrival", default="")
    ap.add_argument("--all", action="store_true",
                    help="every solved row, not only the treble/double-zone ones")
    args = ap.parse_args()
    rows = collect(args.log, args.annotations, args.no_arrival, args.all)

    print("I1647 RADIUS fixture=%s window=%s rows=%d (solved, matched%s)"
          % (args.fixture, args.window, len(rows), "" if args.all else ", treble/double zone"))
    for key, thrown, ring, entry, solved, annx, tips, per in rows:
        lo, hi = truth_band(ring, entry["r"])
        tipr = [math.hypot(*t) for t in tips]
        tip_mean = sum(tipr) / len(tipr) if tipr else float("nan")
        annx_r = math.hypot(*annx) if annx is not None else float("nan")

        def fmt(x, f="%.2f"):
            return "-" if x is None or (isinstance(x, float) and math.isnan(x)) else f % x
        camtxt = []
        for cam in (1, 2, 3):
            c = per[cam]
            loc = c["local"]
            camtxt.append(
                "c%d%s tip=%s in0=%s out0=%s adj=%s/%s fitOff=%s annOff=%s imgOff=%s" % (
                    cam, "*" if c["used"] else "", fmt(c["tip"]),
                    fmt(loc["in0"]) if loc else "-", fmt(loc["out0"]) if loc else "-",
                    fmt(loc["inAdj"]) if loc else "-", fmt(loc["outAdj"]) if loc else "-",
                    fmt(c["fitOff"]), fmt(c["lineOff"]), fmt(c["imgOff"], "%.1f")))
        print("I1647 ROW v%d.%d %s thrown=%s truth=[%.0f,%.0f] geo=%s solved=%.2f annX=%s "
              "tipMean=%s solved-annX=%s annX-tip=%s | %s"
              % (key[0], key[1], args.window, thrown, lo, hi, entry["score"], entry["r"],
                 fmt(annx_r), fmt(tip_mean),
                 fmt(entry["r"] - annx_r), fmt(annx_r - tip_mean), " | ".join(camtxt)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
