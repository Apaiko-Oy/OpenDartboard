"""#1653: the board plane is built from a BLOOMED conic, and bloom is constant in pixels.

    python3 i1653_bloom_plane.py --sim
    python3 i1653_bloom_plane.py --log <run.txt> --annotations <fixture.csv> --fixture <n> --window <w>

THE CAUSE. `wire_model::planeOf` builds each camera's board plane from ONE traced conic
(the outer doubles mark) and the bull, on the premise that the conic is the image of a
board circle. The traced mark is not: the colour mask it is read from blooms past the
paint by a few PIXELS (#1499), and a constant-pixel offset of a perspective ellipse is
not the image of any circle -- the offset is more millimetres where the board is
foreshortened than where it is not. planeOf reads that shape as tilt, so the plane is
warped: #1499's de-bias fixes its SCALE (the median), not its SHAPE. `--sim` holds it
on a pinhole camera with no lens: at the rigs' own tilt (|p| 0.27-0.30) and bloom
(4.5 px, which the model reads as the measured ~3 mm per edge), a true 100 mm point
reads up to 2.4-3.2 mm inward, a true 166 mm point up to 5-6.6 mm inward (on the side
away from the camera), and board angles are wrong by up to +-0.43 deg in a
second-harmonic pattern -- 0.6 mm of arc at the 88 mm of r22 v2.1. The treble band's
two edges then read asymmetric (inner -3.8, outer +2.9 mm), which is the pattern every
camera's calibration line shows and #1553 then scored as "bloom".

THE CORRECTION it would name -- NOT BUILT, and why: build the plane from the doubles
band's CENTRE LINE (the midpoint of the outer and inner traced marks along each ray
from the bull), which is the image of the 166 mm circle whatever the bloom, provided
the two edges of one band bloom by the same number of pixels; unit circle = 166 mm, no
de-bias. `--sim` holds that it leaves < 0.05 mm and 0.00 deg. But `--log` (below) says
what it would buy on the rigs, measured 2026-09-27 on all four windows: the implied
bloom is 4.2-6.3 px, the wire warp +-0.26..0.45 deg per camera, and every annotated
crossing moves by at most 0.8 mm -- r18 v3.3 (T20) 96.30 -> 95.98, r22 v3.2 (D20)
159.69 -> 160.39, r22 v2.1 (S12) 323.64 -> 323.68 deg, r18 v5.1 97.71 -> 97.35. No
target row changes region, so the warp is real and is not what loses them (#1653).

`--log` carries the logged run's own models (I1647FIT) through the same correction
OFFLINE, to state the prediction before any run: per camera, the bloom b (px) that makes
the logged outer conic and the logged inner-mark reach (from the de-bias factor) a
pixel-constant bloom of one plane; that plane (the midband plane, to the sim's 0.05 mm);
and every annotated dart's crossing through it, beside the crossing through the model the
run used. The comb (theta20) is refitted through the new plane from the run's own
endpoints, as board_model::fitBoardToCamera does.

A reporter: it decides nothing and asserts nothing about the numbers.
"""

import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

SEQ = [20, 1, 18, 4, 13, 6, 10, 15, 2, 17, 3, 19, 7, 16, 8, 11, 14, 9, 12, 5]


# ---------------------------------------------------------------- small linear algebra
def solve(M, b):
    n = len(b)
    A = [row[:] + [b[i]] for i, row in enumerate(M)]
    for i in range(n):
        p = max(range(i, n), key=lambda k: abs(A[k][i]))
        A[i], A[p] = A[p], A[i]
        for k in range(n):
            if k != i:
                f = A[k][i] / A[i][i]
                A[k] = [x - f * y for x, y in zip(A[k], A[i])]
    return [A[i][n] / A[i][i] for i in range(n)]


def matmul(A, B):
    return [[sum(A[i][k] * B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def inv(m):
    a, b, c = m[0]
    d, e, f = m[1]
    g, h, i = m[2]
    A = e * i - f * h
    B = -(d * i - f * g)
    C = d * h - e * g
    det = a * A + b * B + c * C
    return [[A / det, -(b * i - c * h) / det, (b * f - c * e) / det],
            [B / det, (a * i - c * g) / det, -(a * f - c * d) / det],
            [C / det, -(a * h - b * g) / det, (a * e - b * d) / det]]


def app(H, x, y):
    u = H[0][0] * x + H[0][1] * y + H[0][2]
    v = H[1][0] * x + H[1][1] * y + H[1][2]
    w = H[2][0] * x + H[2][1] * y + H[2][2]
    return u / w, v / w


# ---------------------------------------------------------------- conics and the plane
def fit_ellipse(pts):
    """Algebraic conic fit -> (cx, cy, a, b, theta)."""
    M = [[0.0] * 5 for _ in range(5)]
    r = [0.0] * 5
    for x, y in pts:
        row = [x * y, y * y, x, y, 1.0]
        t = -x * x
        for i in range(5):
            r[i] += row[i] * t
            for j in range(5):
                M[i][j] += row[i] * row[j]
    B, C, D, E, F = solve(M, r)
    A = 1.0
    den = 4 * A * C - B * B
    x0 = (B * E - 2 * C * D) / den
    y0 = (B * D - 2 * A * E) / den
    Fc = A * x0 * x0 + B * x0 * y0 + C * y0 * y0 + D * x0 + E * y0 + F
    th = 0.5 * math.atan2(B, A - C)
    c, s = math.cos(th), math.sin(th)
    Ap = A * c * c + B * c * s + C * s * s
    Cp = A * s * s - B * c * s + C * c * c
    return (x0, y0, math.sqrt(-Fc / Ap), math.sqrt(-Fc / Cp), th)


def plane_of(e, bull):
    """wire_model::planeOf, verbatim in construction: H = A * boost(p = A^-1 bull)."""
    x0, y0, a, b, phi = e
    c, s = math.cos(phi), math.sin(phi)
    A = [[a * c, -b * s, x0], [a * s, b * c, y0], [0, 0, 1]]
    px, py = app(inv(A), *bull)
    r = math.hypot(px, py)
    t = math.atanh(r)
    ch, sh = math.cosh(t), math.sinh(t)
    psi = math.atan2(py, px)
    cp, sp = math.cos(psi), math.sin(psi)
    rot = [[cp, -sp, 0], [sp, cp, 0], [0, 0, 1]]
    rotT = [[cp, sp, 0], [-sp, cp, 0], [0, 0, 1]]
    boost = [[ch, 0, sh], [0, 1, 0], [sh, 0, ch]]
    H = matmul(A, matmul(rot, matmul(boost, rotT)))
    return H, r


def ray_reach(o, d, e):
    x0, y0, a, b, th = e
    c, s = math.cos(th), math.sin(th)
    rx, ry = o[0] - x0, o[1] - y0
    px, py = rx * c + ry * s, -rx * s + ry * c
    dx, dy = d[0] * c + d[1] * s, -d[0] * s + d[1] * c
    A = dx * dx / (a * a) + dy * dy / (b * b)
    B = 2 * (px * dx / (a * a) + py * dy / (b * b))
    C = px * px / (a * a) + py * py / (b * b) - 1
    return (-B + math.sqrt(B * B - 4 * A * C)) / (2 * A)


def midband(eo, ei, bull, n=360):
    """The doubles band's centre line, along rays from the bull, as a fitted conic."""
    mid = []
    for i in range(n):
        a = 2 * math.pi * i / n
        d = (math.cos(a), math.sin(a))
        r = 0.5 * (ray_reach(bull, d, eo) + ray_reach(bull, d, ei))
        mid.append((bull[0] + r * d[0], bull[1] + r * d[1]))
    return fit_ellipse(mid)


def circle_img(H, R, n=720):
    return [app(H, R * math.cos(2 * math.pi * i / n), R * math.sin(2 * math.pi * i / n)) for i in range(n)]


def offset(pts, b):
    """Normal offset of a closed curve, outward (away from its centroid) by b px."""
    n = len(pts)
    cx = sum(p[0] for p in pts) / n
    cy = sum(p[1] for p in pts) / n
    out = []
    for i in range(n):
        p0, p2 = pts[i - 1], pts[(i + 1) % n]
        tx, ty = p2[0] - p0[0], p2[1] - p0[1]
        L = math.hypot(tx, ty)
        nx, ny = ty / L, -tx / L
        if nx * (pts[i][0] - cx) + ny * (pts[i][1] - cy) < 0:
            nx, ny = -nx, -ny
        out.append((pts[i][0] + b * nx, pts[i][1] + b * ny))
    return out


def debias_unit(eo, ei):
    """#1499: board = mean of the outer mark and the inner mark over 162/170 (semi-majors)."""
    ro, ri = max(eo[2], eo[3]), max(ei[2], ei[3])
    return 0.5 * (ro + ri * 170.0 / 162.0) / ro / 170.0


# ---------------------------------------------------------------- the simulation
def camera(alpha_deg, D, f=900.0, pp=(640.0, 360.0)):
    al = math.radians(alpha_deg)
    C = (D * math.sin(al), 0.0, D * math.cos(al))
    z = [-C[0] / D, -C[1] / D, -C[2] / D]
    tmp = [0.0, 0.0, 1.0]
    x = [z[1] * tmp[2] - z[2] * tmp[1], z[2] * tmp[0] - z[0] * tmp[2], z[0] * tmp[1] - z[1] * tmp[0]]
    n = math.sqrt(sum(v * v for v in x))
    x = [v / n for v in x]
    y = [z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]]

    def proj(X, Y):
        d = (X - C[0], Y - C[1], -C[2])
        xc = sum(a * b for a, b in zip(x, d))
        yc = sum(a * b for a, b in zip(y, d))
        zc = sum(a * b for a, b in zip(z, d))
        return pp[0] + f * xc / zc, pp[1] + f * yc / zc
    return proj


def sim():
    for alpha, D in ((45, 620), (60, 500), (65, 500)):
        for b in (0.0, 4.5):
            proj = camera(alpha, D)
            bull = proj(0.0, 0.0)
            ring = lambda R: [proj(R * math.cos(2 * math.pi * i / 720), R * math.sin(2 * math.pi * i / 720))
                              for i in range(720)]
            eo = fit_ellipse(offset(ring(170.0), +b))
            ei = fit_ellipse(offset(ring(162.0), -b))
            for mode in ("asis", "midband"):
                if mode == "asis":
                    H, tilt = plane_of(eo, bull)
                    unit = debias_unit(eo, ei)
                else:
                    H, tilt = plane_of(midband(eo, ei, bull), bull)
                    unit = 1.0 / 166.0
                Hi = inv(H)
                txt = []
                for R in (100.0, 166.0):
                    rad, ang = [], []
                    for k in range(360):
                        th = math.radians(k)
                        u, v = app(Hi, *proj(R * math.cos(th), R * math.sin(th)))
                        rad.append(math.hypot(u, v) / unit - R)
                        ang.append(math.atan2(v, u))
                    best = None
                    for sgn in (1, -1):
                        d = [a - sgn * math.radians(k) for k, a in enumerate(ang)]
                        m = math.atan2(sum(math.sin(v) for v in d), sum(math.cos(v) for v in d))
                        e = max(abs(math.degrees((v - m + math.pi) % (2 * math.pi) - math.pi)) for v in d)
                        best = e if best is None else min(best, e)
                    txt.append("R%.0f radial min %+.2f median %+.2f max %+.2f mm, angle max %.2f deg"
                               % (R, min(rad), sorted(rad)[180], max(rad), best))
                print("I1653 SIM tilt=%.3f bloom=%.1fpx plane=%-7s %s" % (tilt, b, mode, " | ".join(txt)))


# ---------------------------------------------------------------- the offline re-model
class Cam(object):
    pass


def read_cams(log):
    import re
    from i1647_radius_census import FIT_RE, ANSI
    cams = {}
    for raw in open(log, "r", errors="replace"):
        m = FIT_RE.search(ANSI.sub("", raw))
        if m:
            c = Cam()
            c.resolved = m.group(3) == "1"
            c.theta20 = float(m.group(4))
            c.advance = float(m.group(5))
            c.unit = float(m.group(6))
            h = [float(v) for v in m.group(7).split(",")]
            c.H = [h[0:3], h[3:6], h[6:9]]
            c.bull = (float(m.group(8)), float(m.group(9)))
            cams[int(m.group(2))] = c
    return cams


def remodel(c):
    """The midband plane this camera's logged model implies, under pixel-constant bloom."""
    outer = circle_img(c.H, 1.0)
    eo = fit_ellipse(outer)
    ro = max(eo[2], eo[3])
    f = c.unit * 170.0
    ri = (2.0 * f - 1.0) * ro * 162.0 / 170.0    # the inner mark's reach the de-bias read

    def inner_reach(b):
        Ht, _ = plane_of(fit_ellipse(offset(outer, -b)), c.bull)   # true 170 circle's plane
        ei = fit_ellipse(offset(circle_img(Ht, 162.0 / 170.0), -b))
        return max(ei[2], ei[3]), ei
    lo, hi = 0.0, 20.0
    for _ in range(40):
        mid = 0.5 * (lo + hi)
        if inner_reach(mid)[0] > ri:
            lo = mid
        else:
            hi = mid
    b = 0.5 * (lo + hi)
    ei = inner_reach(b)[1]
    Hn, tilt = plane_of(midband(eo, ei, c.bull), c.bull)
    n = Cam()
    n.H, n.Hi, n.unit, n.bull, n.advance, n.resolved = Hn, inv(Hn), 1.0 / 166.0, c.bull, c.advance, c.resolved
    # The comb, refitted through the new plane from the run's own endpoints (the old
    # plane's twenty at theta20 + k*18 deg, which is what rms 0.0 deg says they were).
    step = 2 * math.pi / 20
    old = [app(c.H, math.cos(c.theta20 + k * step), math.sin(c.theta20 + k * step)) for k in range(20)]
    th = [math.atan2(*reversed(app(n.Hi, *p))) for p in old]
    sr = sum(math.cos(20 * t) for t in th)
    si = sum(math.sin(20 * t) for t in th)
    off = math.atan2(si, sr) / 20
    n.theta20 = off + round((th[0] - off) / step) * step
    n.bloom, n.tilt, n.wireDeg = b, tilt, [math.degrees(((t - (n.theta20 + k * step)) + math.pi) %
                                                        (2 * math.pi) - math.pi) for k, t in enumerate(th)]
    return n


def canon(c, u, v, Hi=None, unit=None):
    q = app(Hi or inv(c.H), u, v)
    mx, my = q[0] / (unit or c.unit), q[1] / (unit or c.unit)
    r = math.hypot(mx, my)
    phi = c.advance * (math.atan2(my, mx) - c.theta20)
    return r * math.cos(phi), r * math.sin(phi)


def crossing(lines):
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
    return None if abs(det) < 1e-9 else ((a22 * b1 - a12 * b2) / det, (a11 * b2 - a12 * b1) / det)


def region(x, y):
    r = math.hypot(x, y)
    phi = math.degrees(math.atan2(y, x)) % 360.0
    w = SEQ[int(phi // 18) % 20]
    if r < 6.35:
        return r, phi, "DBULL"
    if r < 15.9:
        return r, phi, "BULL"
    ring = "S" if r < 99 else "T" if r < 107 else "S" if r < 162 else "D" if r < 170 else "MISS"
    return r, phi, (ring + str(w)) if ring != "MISS" else "MISS"


def offline(args):
    from i1653_model_census import read_annotations, truth_of
    cams = read_cams(args.log)
    new = {k: remodel(c) for k, c in cams.items()}
    for k in sorted(cams):
        n = new[k]
        print("I1653 REMODEL fixture=%s window=%s cam=%d bloom=%.2fpx tilt=%.3f theta20 old=%.3f new=%.3f deg "
              "wire shift range %+.2f..%+.2f deg"
              % (args.fixture, args.window, k, n.bloom, n.tilt, math.degrees(cams[k].theta20),
                 math.degrees(n.theta20), min(n.wireDeg), max(n.wireDeg)))
    ann = read_annotations(args.annotations)
    for key in sorted(ann):
        d = ann[key]
        if truth_of(d["thrown"]) is None:
            continue
        res = []
        for which in (cams, new):
            lines = []
            for cam, (p, q) in sorted(d["lines"].items()):
                c = which.get(cam)
                if c is None or not c.resolved:
                    continue
                Hi = getattr(c, "Hi", None) or inv(c.H)
                a = canon(c, p[0], p[1], Hi)
                b = canon(c, q[0], q[1], Hi)
                dx, dy = b[0] - a[0], b[1] - a[1]
                L = math.hypot(dx, dy)
                lines.append((a, (dx / L, dy / L)))
            x = crossing(lines) if len(lines) >= 2 else None
            res.append(x)
        if res[0] is None:
            continue
        r0, p0, s0 = region(*res[0])
        r1, p1, s1 = region(*res[1])
        print("I1653 DART fixture=%s window=%s v%d.%d thrown=%s old r=%.2f phi=%.2f reads=%s | midband r=%.2f "
              "phi=%.2f reads=%s | dr=%+.2f dphi=%+.3f shift=(%+.2f,%+.2f)mm"
              % (args.fixture, args.window, key[0], key[1], d["thrown"], r0, p0, s0, r1, p1, s1, r1 - r0,
                 p1 - p0, res[1][0] - res[0][0], res[1][1] - res[0][1]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", action="store_true")
    ap.add_argument("--log")
    ap.add_argument("--annotations")
    ap.add_argument("--fixture", default="?")
    ap.add_argument("--window", default="?")
    args = ap.parse_args()
    if args.sim:
        sim()
    if args.log:
        offline(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
