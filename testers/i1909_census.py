"""turnaus#1909: is the solved entry's error RADIAL? The census the sisal term waits on.

    python3 i1909_census.py --run <bakeoff dir> [--label <name>] [--rows]
    python3 i1909_census.py --self-test

Reads the four annotated logs a `testers/i1555_run.sh` bakeoff leaves in its run directory
(r18-dev.txt, r18-open.txt, r22-dev.txt, r22-open.txt; rig-20260922 with --no-arrival 1.1,
as i1555_inside.sh's census takes it). Every matched, SOLVED dart is bound to its annotated
throw by i1511's matcher, through i1647_radius_census.collect (imported, so this census
cannot disagree with #1512's, #1555's or #1647's about which dart is which), and its error
is the solved entry (I1512ENTRY x,y, canonical board mm) minus a reference, split at the
reference into

    radial      along the unit vector from the bull centre to the reference (+ = the solve
                lies further out than the reference)
    tangential  perpendicular to it, + in the direction phi advances

against TWO references, because they answer different questions:

    tip    the annotated board-contact pixel of every camera that has one, carried to
           canonical mm through that camera's OWN logged model (I1647FIT: H, unitPerMm,
           theta20, advance) and averaged. Where the dart visibly meets the board -- the
           point a sisal term would move the solve TOWARD. It carries the camera's
           parallax wherever the contact pixel is a point above the plane (#1512's trap).
    annX   the least-squares crossing of the ANNOTATED shaft lines, transported exactly as
           solveEntry transports the fitted ones (#1647's parallax-free reference). The
           crossing of perfect lines with the wire plane: it has the same blind spot the
           solve has, so solved - annX is the FIT's error and says nothing about depth.

and in a fixed board direction as well, because a sisal error is a slide along the dart's
own travel, and darts thrown from one oche lean one way: a depth term shows up as a MEAN
VECTOR the same on every dart, not as a radial bias. `up` is the 20's centre (canonical
phi = 9 degrees), `right` is 90 degrees clockwise of it as the thrower sees the board.

Bins are i1911_census.py's (imported), on the reference's radius over 170 mm.

WHAT IT PRINTS, per --label:

    I1909 ERROR ref=<tip|annX> bin=<bin> n=.. rad_med|..| tan_med|..| rad_mean tan_mean
                rad_rms tan_rms radial_share     (radial_share = sum rad^2 / sum |e|^2)
    I1909 VECTOR ref=<..> n=.. mean=(up,right) mm |mean| and the scatter about it
    I1909 ROW    (with --rows) every dart: thrown, geo, r, the two errors split

A reporter: it decides nothing and asserts nothing about the numbers. --self-test holds the
decomposition on planted points and exits non-zero on any difference.
"""

import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

LOGS = [("r18-dev", "rig-20260918", ""), ("r18-open", "rig-20260918", ""),
        ("r22-dev", "rig-20260922", "1.1"), ("r22-open", "rig-20260922", "1.1")]
UP_DEG = 9.0  # the 20's centre in the canonical (sequence) frame: phi 0 is its first wire


def split(err, ref):
    """(radial, tangential) of the error vector `err`, at the reference point `ref`."""
    r = math.hypot(ref[0], ref[1])
    if r <= 0.0:
        return (math.hypot(err[0], err[1]), 0.0)
    ux, uy = ref[0] / r, ref[1] / r
    return (err[0] * ux + err[1] * uy, -err[0] * uy + err[1] * ux)


def up_right(err):
    """The error in the fixed board directions: up = toward the 20, right = clockwise of it.

    The canonical frame advances through 20, 1, 18 (clockwise as the thrower sees the
    board), so `right` is the direction phi advances at the top."""
    a = math.radians(UP_DEG)
    ux, uy = math.cos(a), math.sin(a)
    return (err[0] * ux + err[1] * uy, -err[0] * uy + err[1] * ux)


# ---- the sisal model, offline (--sisal): the term OD_SISAL_MM would apply, on logged numbers --
#
# The transported lines alone do NOT give the dart's angle to the board: every one of them
# lies IN the board plane and passes through the entry, so they fix the entry and nothing
# about elevation. The angle needs each camera's centre, and the only thing that names it
# is the camera's own homography: H = K [r1 r2 t] for a pinhole with square pixels and the
# principal point at the frame's centre leaves the focal length the one unknown, and the
# two rotation constraints (r1 . r2 = 0, |r1| = |r2|) answer it. The dart's 3D axis is
# then the line common to every camera's plane through its centre and its transported line.

PRINCIPAL = (640.0, 360.0)  # the bakeoff's 1280x720
MIN_SIN_ELEV = 0.5          # an axis under 30 degrees to the board is not a dart: no slide


def ring_word(r):
    """The ring the spec radii put radius r in (no #1553 bloom adjustment: an estimate)."""
    for lim, w in ((6.35, "DBULL"), (15.9, "BULL"), (99.0, "S-in"), (107.0, "T"), (162.0, "S-out"),
                   (170.0, "D")):
        if r < lim:
            return w
    return "MISS"


def camera_centre(mdl, pp=PRINCIPAL):
    """(focal px, (x, y, h) canonical mm with h > 0 the camera's height off the plane), or None."""
    h = mdl.H
    u = mdl.unit
    # H in mm: plane units are u per mm, so the first two columns scale by u.
    g = [[(h[0] - pp[0] * h[6]) * u, (h[1] - pp[0] * h[7]) * u, h[2] - pp[0] * h[8]],
         [(h[3] - pp[1] * h[6]) * u, (h[4] - pp[1] * h[7]) * u, h[5] - pp[1] * h[8]],
         [h[6] * u, h[7] * u, h[8]]]
    p1 = g[0][0] * g[0][1] + g[1][0] * g[1][1]
    q1 = g[2][0] * g[2][1]
    p2 = g[0][0] ** 2 + g[1][0] ** 2 - g[0][1] ** 2 - g[1][1] ** 2
    q2 = g[2][0] ** 2 - g[2][1] ** 2
    if p1 * p1 + p2 * p2 <= 0.0:
        return None
    w = -(p1 * q1 + p2 * q2) / (p1 * p1 + p2 * p2)
    if not w > 0.0:
        return None
    f = 1.0 / math.sqrt(w)
    a = [[g[0][j] / f, g[1][j] / f, g[2][j]] for j in range(3)]  # columns
    n1 = math.sqrt(sum(x * x for x in a[0]))
    n2 = math.sqrt(sum(x * x for x in a[1]))
    lam = 2.0 / (n1 + n2)
    r1 = [x * lam for x in a[0]]
    r2 = [x * lam for x in a[1]]
    t = [x * lam for x in a[2]]
    if t[2] < 0.0:
        r1, r2, t = [-x for x in r1], [-x for x in r2], [-x for x in t]
    r3 = [r1[1] * r2[2] - r1[2] * r2[1], r1[2] * r2[0] - r1[0] * r2[2], r1[0] * r2[1] - r1[1] * r2[0]]
    c = [-(r1[0] * t[0] + r1[1] * t[1] + r1[2] * t[2]),
         -(r2[0] * t[0] + r2[1] * t[1] + r2[2] * t[2]),
         -(r3[0] * t[0] + r3[1] * t[1] + r3[2] * t[2])]
    r = math.hypot(c[0], c[1])
    phi = mdl.advance * (math.atan2(c[1], c[0]) - mdl.theta20)
    return f, (r * math.cos(phi), r * math.sin(phi), abs(c[2]))


def dart_axis(lines_and_centres):
    """Unit 3D direction INTO the board (z < 0) common to every camera's plane, or None."""
    normals = []
    for (px, py), (dx, dy), (cx, cy, ch) in lines_and_centres:
        v = (cx - px, cy - py, ch)
        n = (dy * v[2], -dx * v[2], dx * v[1] - dy * v[0])  # (dx,dy,0) x v
        nn = math.sqrt(sum(x * x for x in n))
        if nn > 0.0:
            normals.append([x / nn for x in n])
    if len(normals) < 2:
        return None
    # The direction is the null vector of sum N N^T: the cross product of the two most
    # independent normals, refined by one inverse-iteration step over all of them.
    best = None
    for i in range(len(normals)):
        for j in range(i + 1, len(normals)):
            a, b = normals[i], normals[j]
            d = (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
            n = math.sqrt(sum(x * x for x in d))
            if best is None or n > best[0]:
                best = (n, [x / n for x in d] if n > 0 else None)
    d = best[1]
    if d is None:
        return None
    if d[2] > 0.0:
        d = [-x for x in d]
    return d


def sisal_shift(axis, depth_mm):
    """The in-plane move of the entry for a tip `depth_mm` below the plane along `axis`."""
    s = depth_mm / abs(axis[2])
    return (s * axis[0], s * axis[1])


def med(xs):
    s = sorted(xs)
    n = len(s)
    if n == 0:
        return float("nan")
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


def table(rows, label, show_rows):
    import i1911_census as bins
    for refname in ("tip", "annX"):
        per_bin, vecs = {}, []
        for row in rows:
            ref = row[refname]
            if ref is None:
                continue
            e = (row["solved"][0] - ref[0], row["solved"][1] - ref[1])
            rad, tan = split(e, ref)
            b = bins.radius_bin(math.hypot(*ref) / 170.0)
            for key in (b, "all"):
                per_bin.setdefault(key, []).append((rad, tan))
            vecs.append(up_right(e))
            row.setdefault("split", {})[refname] = (rad, tan, b)
        order = [name for name, _, _ in bins.BINS] + ["all"]
        for b in order:
            xs = per_bin.get(b)
            if not xs:
                continue
            rads = [x[0] for x in xs]
            tans = [x[1] for x in xs]
            srad = sum(r * r for r in rads)
            stan = sum(t * t for t in tans)
            print("I1909 ERROR %s ref=%s bin=%s n=%d rad_med|%.2f| tan_med|%.2f| rad_mean=%+.2f "
                  "tan_mean=%+.2f rad_rms=%.2f tan_rms=%.2f radial_share=%.2f"
                  % (label, refname, b, len(xs), med([abs(r) for r in rads]),
                     med([abs(t) for t in tans]), sum(rads) / len(rads), sum(tans) / len(tans),
                     math.sqrt(srad / len(xs)), math.sqrt(stan / len(xs)),
                     srad / (srad + stan) if srad + stan > 0 else float("nan")))
        if vecs:
            mu = (sum(v[0] for v in vecs) / len(vecs), sum(v[1] for v in vecs) / len(vecs))
            sd = math.sqrt(sum((v[0] - mu[0]) ** 2 + (v[1] - mu[1]) ** 2 for v in vecs) / len(vecs))
            print("I1909 VECTOR %s ref=%s n=%d mean=(up %+.2f, right %+.2f) mm |mean|=%.2f "
                  "scatter_rms=%.2f" % (label, refname, len(vecs), mu[0], mu[1], math.hypot(*mu), sd))
    if show_rows:
        for row in rows:
            s = row.get("split", {})

            def f(name):
                if name not in s:
                    return "-"
                return "rad=%+.2f tan=%+.2f bin=%s" % s[name]
            print("I1909 ROW %s %s v%d.%d thrown=%s geo=%s r=%.2f | tip %s | annX %s"
                  % (label, row["log"], row["key"][0], row["key"][1], row["thrown"], row["geo"],
                     math.hypot(*row["solved"]), f("tip"), f("annX")))


def gather(run):
    import i1647_radius_census as radius
    here = os.path.dirname(os.path.abspath(__file__))
    rows = []
    for name, fixture, no_arrival in LOGS:
        log = os.path.join(run, name + ".txt")
        if not os.path.exists(log):
            print("I1909 MISSING %s" % log)
            continue
        ann = os.path.join(here, "i1511_annotations", fixture + ".csv")
        for key, thrown, ring, entry, solved, annx, tips, per in radius.collect(log, ann, no_arrival, True):
            tip = None
            if tips:
                tip = (sum(t[0] for t in tips) / len(tips), sum(t[1] for t in tips) / len(tips))
            lc, focals = [], []
            for cam in sorted(per):
                c = per[cam]
                if not c["used"] or "fitLine" not in c or c["mdl"] is None:
                    continue
                pose = camera_centre(c["mdl"])
                if pose is None:
                    continue
                focals.append(pose[0])
                lc.append((c["fitLine"][0], c["fitLine"][1], pose[1]))
            rows.append({"log": name, "key": key, "thrown": thrown, "geo": entry["score"],
                         "solved": solved, "tip": tip, "annX": annx,
                         "axis": dart_axis(lc), "focals": focals})
    return rows


def self_test():
    bad = 0

    def check(name, got, want):
        nonlocal bad
        ok = all(abs(g - w) < 1e-6 for g, w in zip(got, want))
        print("%s %s got=%s want=%s" % ("OK  " if ok else "FAIL", name, got, want))
        bad += 0 if ok else 1
    # A solve 2 mm further out than a reference on the +x axis is +2 radial, 0 tangential.
    check("radial-out", split((2.0, 0.0), (100.0, 0.0)), (2.0, 0.0))
    # At phi = 90 degrees, +x is the tangential MINUS direction (phi advances toward -x).
    check("tangential", split((1.0, 0.0), (0.0, 100.0)), (0.0, -1.0))
    check("inward", split((0.0, -3.0), (0.0, 50.0)), (-3.0, 0.0))
    # Up is phi = 9 degrees: an error pointing there is all `up`.
    a = math.radians(UP_DEG)
    check("up", up_right((math.cos(a), math.sin(a))), (1.0, 0.0))
    check("median", (med([3.0, 1.0, 2.0]), med([1.0, 2.0, 3.0, 4.0])), (2.0, 2.5))

    # THE ACCEPTANCE'S PLANT. A known axis at 45 degrees to the board, leaning radially
    # outward as it goes in, and d = 2: the entry moves 2.0 mm radially, the way the dart is
    # travelling (outward). The sign flipped moves it 2.0 mm the other way and fails.
    s45 = math.sqrt(0.5)
    check("slide-45", sisal_shift((s45, 0.0, -s45), 2.0), (2.0, 0.0))
    check("slide-45-radial", split(sisal_shift((s45, 0.0, -s45), 2.0), (100.0, 0.0)), (2.0, 0.0))
    # An axis square to the board slides nothing, whatever the depth.
    check("slide-square", sisal_shift((0.0, 0.0, -1.0), 3.0), (0.0, 0.0))

    # The same 45-degree dart end to end: two planted pinhole cameras (f 800 px, principal
    # point the frame's centre), the board seen through their homographies, the dart's shaft
    # projected into each image and transported back as solveEntry transports a fitted line;
    # the camera centres recovered from H alone, the axis from the planes, the slide from it.
    import i1647_radius_census as radius
    entry, travel = (100.0, 0.0, 0.0), (s45, 0.0, -s45)
    lc, worst_f = [], 0.0
    for centre in ((60.0, -420.0, 520.0), (-380.0, 160.0, 560.0)):
        z = [-c for c in centre]
        nz = math.sqrt(sum(c * c for c in z))
        z = [c / nz for c in z]
        x = [z[1], -z[0], 0.0]
        nx = math.hypot(x[0], x[1])
        x = [c / nx for c in x]
        y = [z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]]
        R = [x, y, z]  # rows: the camera's axes in board coordinates
        t = [-sum(R[i][k] * centre[k] for k in range(3)) for i in range(3)]
        K = [[800.0, 0.0, PRINCIPAL[0]], [0.0, 800.0, PRINCIPAL[1]], [0.0, 0.0, 1.0]]

        def project(X):
            c = [sum(R[i][k] * X[k] for k in range(3)) + t[i] for i in range(3)]
            u = [sum(K[i][k] * c[k] for k in range(3)) for i in range(3)]
            return (u[0] / u[2], u[1] / u[2])
        unit = 1.0 / 170.0
        Hmm = [[sum(K[i][k] * (R[k][0], R[k][1], t[k])[j] for k in range(3)) for j in range(3)] for i in range(3)]
        mdl = radius.Model.__new__(radius.Model)
        mdl.resolved, mdl.theta20, mdl.advance, mdl.unit = True, 0.0, 1.0, unit
        mdl.H = [Hmm[0][0] / unit, Hmm[0][1] / unit, Hmm[0][2],
                 Hmm[1][0] / unit, Hmm[1][1] / unit, Hmm[1][2],
                 Hmm[2][0] / unit, Hmm[2][1] / unit, Hmm[2][2]]
        mdl.Hinv = radius.inv3(mdl.H)
        f, c = camera_centre(mdl)
        worst_f = max(worst_f, abs(f - 800.0))
        check("centre", c, centre)
        shaft = [tuple(entry[k] - s * travel[k] for k in range(3)) for s in (20.0, 60.0)]
        line = mdl.line(project(shaft[0]), project(shaft[1]))
        lc.append((line[0], line[1], c))
    check("focal", (worst_f,), (0.0,))
    axis = dart_axis(lc)
    check("axis", axis, travel)
    check("slide-planted", split(sisal_shift(axis, 2.0), entry[:2]), (2.0, 0.0))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run")
    ap.add_argument("--label", default="run")
    ap.add_argument("--rows", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--sisal", type=float, nargs="*", default=[],
                    help="offline: slide each solve along its 3D axis by these depths (mm)")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if not args.run:
        ap.error("--run <bakeoff dir> or --self-test")
    rows = gather(args.run)
    print("I1909 CENSUS %s run=%s solved_matched=%d" % (args.label, args.run, len(rows)))
    table(rows, args.label, args.rows)
    if args.sisal:
        axes = [r for r in rows if r["axis"] is not None]
        elev = sorted(math.degrees(math.asin(abs(r["axis"][2]))) for r in axes)
        foc = sorted(f for r in rows for f in r["focals"])
        print("I1909 AXIS %s rows_with_axis=%d of %d elevation_deg med=%.1f p10=%.1f p90=%.1f "
              "focal_px med=%.0f min=%.0f max=%.0f"
              % (args.label, len(axes), len(rows), med(elev), elev[len(elev) // 10],
                 elev[(len(elev) * 9) // 10], med(foc), foc[0], foc[-1]))
        if args.rows:
            for r in axes:
                a = r["axis"]
                print("I1909 AXISROW %s v%d.%d thrown=%s elev=%.1f into=(%+.2f,%+.2f) up/right=(%+.2f,%+.2f) "
                      "focals=%s" % (r["log"], r["key"][0], r["key"][1], r["thrown"],
                                     math.degrees(math.asin(abs(a[2]))), a[0], a[1],
                                     up_right((a[0], a[1]))[0], up_right((a[0], a[1]))[1],
                                     "/".join("%.0f" % f for f in r["focals"])))
        for d in args.sisal:
            moved = []
            for r in rows:
                m = dict(r)
                if r["axis"] is not None and abs(r["axis"][2]) >= MIN_SIN_ELEV:
                    dx, dy = sisal_shift(r["axis"], d)
                    m["solved"] = (r["solved"][0] + dx, r["solved"][1] + dy)
                    m["shift"] = math.hypot(dx, dy)
                moved.append(m)
            sh = sorted(m.get("shift", 0.0) for m in moved)
            print("I1909 SISAL %s d=%.1f moved=%d shift_mm med=%.2f max=%.2f (offline: the logged "
                  "fitted lines and models, not a replay)" % (args.label, d, sum(1 for m in moved if "shift" in m),
                                                              med(sh), sh[-1]))
            table(moved, "%s-sisal%.1f" % (args.label, d), False)
            flips = []
            for r, m in zip(rows, moved):
                a, b = ring_word(math.hypot(*r["solved"])), ring_word(math.hypot(*m["solved"]))
                if a != b:
                    flips.append("v%d.%d(%s) thrown=%s %s->%s" % (r["key"][0], r["key"][1], r["log"],
                                                               r["thrown"], a, b))
            print("I1909 RINGFLIP %s d=%.1f n=%d %s" % (args.label, d, len(flips), "; ".join(flips)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
