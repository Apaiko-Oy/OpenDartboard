#!/usr/bin/env python3
# turnaus#1992: where does today's board model put every wire crossing a camera can
# locate, in millimetres -- and would one homography with one radial term put them closer?
#
# AN INSTRUMENT with --frames (nothing in it can fail on a wrong number) and a CHECK with
# --synthetic (registered: run_all.sh's 1992-crossings, through testers/i1992_check.sh).
# Stdlib only, as i1560_k1_census.py is and for its reason: the image has an interpreter
# and no cv2, the host has neither.
#
#   testers/i1992_check.sh                                   # the registered check
#   python3 testers/i1992_crossing_census.py --synthetic     # the same, directly
#   python3 testers/i1992_crossing_census.py --frames <dir> --fits r18open=<replay log> ...
#
# THE PREDICTION, WRITTEN BEFORE THE COUNT (2026-11-14, committed before the first run).
#
# The issue predicts "treble residual under 1 mm held-out" for a joint fit with one radial
# term, and reads today's logged -2.7 / -4.6 / +4.7 mm ring residuals as calibration error.
# Having read board_model.hpp, the prediction made here is narrower and partly AGAINST the
# issue's premise:
#
#   (a) Today's logged ring residuals are of the colour MASK's traced edge, and #1553
#       already found them to be bloom: inner edges negative, outer edges positive, the
#       band widened both ways. A crossing located on the band's half-height edge and the
#       bed's luminance edge carries no such widening. So under TODAY'S model the treble
#       crossings are predicted at roughly +0.5..+1 mm on BOTH edges (same sign), not at
#       -3 / +2: with kappa = -2.45e-7 px^-2, a treble imaged ~150 px and a doubles ring
#       ~250 px from the distortion centre, a projective map scaled on the doubles ring
#       mis-places the treble by kappa * r_t * (r_t^2 - r_d^2) = +1.5 px = +0.7 mm.
#   (b) Today's map is built from a conic and one point (the bull); its ANGLES have one
#       fitted number, the comb's offset. The tangential residual is predicted to be as
#       large as the radial one or larger -- a bull a pixel or two off is a tilt error.
#   (c) A homography fitted to all crossings, kappa = 0, is predicted to take most of
#       what there is to take; the radial term to add a few tenths of a millimetre, and
#       held-out crossings (odd wires fitted, even wires read) to land under 1 mm rms.
#   (d) kappa fitted per camera from <= 80 crossings is predicted NOT to be resolved to
#       better than #1560's pooled sigma: the board fills the middle of the frame.
#
# WHAT A CROSSING IS HERE. No stage of the detector locates one: the twenty boundaries on
# a calibration are GENERATED from the plane (wire_model.hpp), the rings are mask traces.
# This census takes i1560's extraction (imported, not copied): the four band edges along
# rays from the bull at a fixed bandness threshold, each fitted with a trimmed conic, and
# the twenty bed-boundary luminance edges traced over 0.18-0.50 and 0.66-0.90 of the
# doubles conic. A crossing is a straight line through one wire's trace points NEAR the
# ring (0.40-0.76 for the treble pair, which the trace straddles; 0.74-0.90 for the
# doubles pair, which it stops short of, so those are EXTRAPOLATED 5-10% of a radius and
# marked) met with that ring's conic. Twenty wires x four rings = 80 at most.
#
# "TODAY'S MODEL" is the detector's own, not a re-derivation: the I1647FIT line of a real
# replay of the same clip (H board plane -> image, unitPerMm, theta20, advance), which is
# the map boardPointOf carries a dart through.

import argparse
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i1560_k1_census as lens  # noqa: E402  (the extraction, and its linear algebra)

PP = (640.0, 360.0)
MEASURED_KAPPA = -2.45e-7  # lens_census::MEASURED_KAPPA
RINGS = (("t_in", 99.0), ("t_out", 107.0), ("d_in", 162.0), ("d_out", 170.0))
SECTOR = 2.0 * math.pi / 20.0


# ------------------------------------------------------------------------- 3x3 algebra

def inv3(m):
    a, b, c, d, e, f, g, h, i = m
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    return [(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
            (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
            (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det]


def apply(m, x, y):
    w = m[6] * x + m[7] * y + m[8]
    return (m[0] * x + m[1] * y + m[2]) / w, (m[3] * x + m[4] * y + m[5]) / w


def undistort(p, kappa):
    """Inverse of lens_census::distortPx about the frame centre (its fixed point)."""
    dx, dy = p[0] - PP[0], p[1] - PP[1]
    ux, uy = dx, dy
    for _ in range(64):
        s = 1.0 + kappa * (ux * ux + uy * uy)
        nx, ny = dx / s, dy / s
        step = abs(nx - ux) + abs(ny - uy)
        ux, uy = nx, ny
        if step < 1e-10:
            break
    return PP[0] + ux, PP[1] + uy


def distort(p, kappa):
    dx, dy = p[0] - PP[0], p[1] - PP[1]
    s = 1.0 + kappa * (dx * dx + dy * dy)
    return PP[0] + dx * s, PP[1] + dy * s


def fit_homography(pairs):
    """DLT, board mm -> image px, Hartley-normalised. pairs: [((X, Y), (x, y))]."""
    sb, si = 170.0, 300.0
    rows = []
    for (X, Y), (x, y) in pairs:
        Xn, Yn = X / sb, Y / sb
        xn, yn = (x - PP[0]) / si, (y - PP[1]) / si
        rows.append([-Xn, -Yn, -1, 0, 0, 0, xn * Xn, xn * Yn, xn])
        rows.append([0, 0, 0, -Xn, -Yn, -1, yn * Xn, yn * Yn, yn])
    ata = [[sum(r[i] * r[j] for r in rows) for j in range(9)] for i in range(9)]
    h = lens.jacobi_eigen(ata)[0][1]
    # denormalise: x = T_i^-1 * Hn * T_b
    hn = [h[0] / sb, h[1] / sb, h[2], h[3] / sb, h[4] / sb, h[5], h[6] / sb, h[7] / sb, h[8]]
    return [si * hn[0] + PP[0] * hn[6], si * hn[1] + PP[0] * hn[7], si * hn[2] + PP[0] * hn[8],
            si * hn[3] + PP[1] * hn[6], si * hn[4] + PP[1] * hn[7], si * hn[5] + PP[1] * hn[8],
            hn[6], hn[7], hn[8]]


# ------------------------------------------------------------------ the residual itself

def split_residual(board, truth_mm, truth_angle):
    """(radial, tangential) mm of a board point against the crossing it should be:
    radial + = the model reads the crossing further out than the wire is; tangential
    + = further round in increasing board angle, as an arc at the wire's own radius."""
    r = math.hypot(board[0], board[1])
    th = math.atan2(board[1], board[0])
    d = (th - truth_angle + math.pi) % (2.0 * math.pi) - math.pi
    return r - truth_mm, d * truth_mm


def residuals_under(crossings, hinv, unit_per_mm, kappa, theta0, advance):
    """Each crossing through one model: [(ring, wire, radial, tangential)].
    The model's angle of wire k is theta0 + advance * k * 18 deg."""
    out = []
    for c in crossings:
        p = undistort(c["px"], kappa) if kappa else c["px"]
        q = apply(hinv, p[0], p[1])
        board = (q[0] / unit_per_mm, q[1] / unit_per_mm)
        rad, tan = split_residual(board, c["mm"], theta0 + advance * c["wire"] * SECTOR)
        out.append((c["ring"], c["wire"], rad, tan))
    return out


def fit_joint(crossings, kappa):
    """One homography through every given crossing, pixels undistorted by `kappa` first.
    Board frame: wire k at k * 18 deg, so theta0 = 0 and advance = +1 by construction."""
    pairs = []
    for c in crossings:
        a = c["wire"] * SECTOR
        pairs.append(((c["mm"] * math.cos(a), c["mm"] * math.sin(a)),
                      undistort(c["px"], kappa) if kappa else c["px"]))
    return inv3(fit_homography(pairs))


def rms2(res):
    return math.sqrt(sum(r[2] ** 2 + r[3] ** 2 for r in res) / len(res)) if res else 0.0


def fit_kappa(crossings, lo=-1.5e-6, hi=1.0e-6):
    """The kappa at which a joint fit leaves its own crossings the smallest residual:
    a scan, then two refinements. Returns (kappa, rms mm, rms mm at kappa = 0)."""
    def cost(k):
        return rms2(residuals_under(crossings, fit_joint(crossings, k), 1.0, k, 0.0, 1.0))
    best = None
    step = (hi - lo) / 50.0
    for _ in range(3):
        ks = [lo + i * step for i in range(int(round((hi - lo) / step)) + 1)]
        best = min(ks, key=cost)
        lo, hi = best - step, best + step
        step /= 10.0
    return best, cost(best), cost(0.0)


def summarise(res, ring=None):
    sel = [r for r in res if ring is None or r[0] == ring]
    if not sel:
        return None
    rad = sorted(r[2] for r in sel)
    tan = sorted(r[3] for r in sel)
    return {"n": len(sel), "rad_med": rad[len(rad) // 2], "tan_med": tan[len(tan) // 2],
            "rad_rms": lens.rms(rad), "tan_rms": lens.rms(tan),
            "worst": max(math.hypot(r[2], r[3]) for r in sel)}


# -------------------------------------------------------------------------- extraction

def line_through(points):
    """Total-least-squares line: (mx, my, ux, uy, rms px)."""
    n = len(points)
    mx = sum(p[0] for p in points) / n
    my = sum(p[1] for p in points) / n
    sxx = sum((p[0] - mx) ** 2 for p in points)
    syy = sum((p[1] - my) ** 2 for p in points)
    sxy = sum((p[0] - mx) * (p[1] - my) for p in points)
    th = 0.5 * math.atan2(2 * sxy, sxx - syy)
    ux, uy = math.cos(th), math.sin(th)
    dev = [-(p[0] - mx) * uy + (p[1] - my) * ux for p in points]
    return mx, my, ux, uy, lens.rms(dev)


def line_meets_conic(line, fit):
    """The point where the line meets the conic nearest the line's own middle."""
    (A, B, C, D, E, F), cx, cy, sc = fit
    mx, my, ux, uy = line[:4]
    x0, y0 = (mx - cx) / sc, (my - cy) / sc
    dx, dy = ux / sc, uy / sc
    a = A * dx * dx + B * dx * dy + C * dy * dy
    b = 2 * A * x0 * dx + B * (x0 * dy + y0 * dx) + 2 * C * y0 * dy + D * dx + E * dy
    c = A * x0 * x0 + B * x0 * y0 + C * y0 * y0 + D * x0 + E * y0 + F
    disc = b * b - 4 * a * c
    if disc < 0 or abs(a) < 1e-15:
        return None
    t = min(((-b + math.sqrt(disc)) / (2 * a), (-b - math.sqrt(disc)) / (2 * a)), key=abs)
    return mx + t * ux, my + t * uy


def locate_crossings(frame, seed):
    """Every crossing this frame gives: ([{px, ring, mm, extrapolated, line_rms}],
    {ring: (points kept, conic rms px)}, traces found)."""
    edges = lens.extract_ring_edges(frame, seed)
    conics, report = {}, {}
    for name, _ in RINGS:
        if len(edges[name]) < 40:
            report[name] = (len(edges[name]), 0.0)
            continue
        kept, fit, res = lens.trim_conic(lens.median_prefilter(edges[name], seed))
        report[name] = (len(kept), lens.rms(res))
        if len(kept) >= 40:
            conics[name] = fit
    if "d_out" not in conics:
        return [], report, 0
    traces = lens.wire_traces(frame, conics["d_out"], seed)
    found = []
    for trace in traces:
        near_treble_in = [p for p in trace if 0.40 <= p[2] <= 0.50]
        near_treble_out = [p for p in trace if 0.66 <= p[2] <= 0.76]
        near_doubles = [p for p in trace if 0.74 <= p[2] <= 0.90]
        for name, mm in RINGS:
            if name not in conics:
                continue
            if name.startswith("t"):
                if len(near_treble_in) < 3 or len(near_treble_out) < 3:
                    continue
                pts, extrapolated = near_treble_in + near_treble_out, False
            else:
                if len(near_doubles) < 5:
                    continue
                pts, extrapolated = near_doubles, True
            line = line_through(pts)
            hit = line_meets_conic(line, conics[name])
            if hit is None:
                continue
            found.append({"px": hit, "ring": name, "mm": mm, "extrapolated": extrapolated,
                          "line_rms": line[4]})
    return found, report, len(traces)


def band_middles(crossings):
    """One point per wire per band: the mean of its inner and outer edge crossing."""
    by = {(c["ring"], c["wire"]): c for c in crossings}
    out = []
    for band, mm in (("t", 103.0), ("d", 166.0)):
        for wire in range(20):
            a, b = by.get((band + "_in", wire)), by.get((band + "_out", wire))
            if a and b:
                out.append({"px": (0.5 * (a["px"][0] + b["px"][0]), 0.5 * (a["px"][1] + b["px"][1])),
                            "ring": band + "_mid", "mm": mm, "wire": wire})
    return out


def read_fits(path):
    """cam -> the last I1647FIT of a replay log (they do not move within one)."""
    fits = {}
    pat = re.compile(r"I1647FIT window=\d+ cam=(\d+) resolved=(\d) theta20=(\S+) advance=(\S+) "
                     r"unitPerMm=(\S+) H=(\S+) bull=([\d.]+),([\d.]+)")
    with open(path, errors="replace") as f:
        for line in f:
            m = pat.search(line)
            if m:
                fits[int(m.group(1))] = {
                    "resolved": m.group(2) == "1", "theta20": float(m.group(3)),
                    "advance": float(m.group(4)), "unit": float(m.group(5)),
                    "H": [float(v) for v in m.group(6).split(",")],
                    "bull": (float(m.group(7)), float(m.group(8)))}
    return fits


def name_wires(crossings, hinv, unit, theta20, advance):
    """Give each crossing its wire, 0..19 from the 20's first boundary, by TODAY'S model:
    the nearest of the comb's twenty. A model 9 degrees out would mis-name them all, and
    the tangential column would say so by sitting near +/-9 deg of arc."""
    for c in crossings:
        q = apply(hinv, c["px"][0], c["px"][1])
        th = math.atan2(q[1], q[0])
        c["wire"] = int(round(advance * (th - theta20) / SECTOR)) % 20


def fmt_row(label, s):
    if s is None:
        return "  %-34s n=0" % label
    return ("  %-34s n=%2d  radial med %+5.2f rms %4.2f | tangential med %+5.2f rms %4.2f | "
            "worst %4.2f mm" % (label, s["n"], s["rad_med"], s["rad_rms"], s["tan_med"],
                                s["tan_rms"], s["worst"]))


def measure(tag, cam, frames_dir, fit):
    path = os.path.join(frames_dir, "%s-cam%d.ppm" % (tag, cam))
    if not os.path.exists(path):
        print("I1992 %s cam%d: no frame at %s" % (tag, cam, path))
        return
    frame = lens.Frame(path)
    crossings, report, traces = locate_crossings(frame, fit["bull"])
    hinv = inv3(fit["H"])
    name_wires(crossings, hinv, fit["unit"], fit["theta20"], fit["advance"])
    # one crossing per (ring, wire): a wire traced twice is a tracker artefact, kept once
    seen, unique = set(), []
    for c in crossings:
        if (c["ring"], c["wire"]) not in seen:
            seen.add((c["ring"], c["wire"]))
            unique.append(c)
    dup = len(crossings) - len(unique)
    crossings = unique
    px_per_mm = math.sqrt(abs(fit["H"][0] * fit["H"][4] - fit["H"][1] * fit["H"][3])) * fit["unit"] / abs(fit["H"][8])
    print("I1992 %s cam%d: %d crossings of 80 on %d traced wires (%d duplicates dropped); "
          "%.2f px/mm at the bull; ring conics %s; trace scatter about its line median %.2f px" %
          (tag, cam, len(crossings), traces, dup, px_per_mm,
           ", ".join("%s %d pts rms %.2f px" % (n, report[n][0], report[n][1]) for n, _ in RINGS),
           sorted(c["line_rms"] for c in crossings)[len(crossings) // 2] if crossings else 0.0))
    if len(crossings) < 12:
        print("  too few to say anything")
        return
    today = residuals_under(crossings, hinv, fit["unit"], 0.0, fit["theta20"], fit["advance"])
    print(" TODAY (the replay's I1647FIT: doubles conic + bull, scale by the two doubles marks):")
    for name, _ in RINGS:
        print(fmt_row(name + (" (extrapolated)" if name.startswith("d") else ""), summarise(today, name)))
    print(fmt_row("all", summarise(today)))

    print(" ONE HOMOGRAPHY THROUGH EVERY CROSSING (in-sample):")
    for label, k in (("kappa 0", 0.0), ("kappa %.2e (#1560's)" % MEASURED_KAPPA, MEASURED_KAPPA)):
        res = residuals_under(crossings, fit_joint(crossings, k), 1.0, k, 0.0, 1.0)
        print(fmt_row(label, summarise(res)))
    kfit, cost, cost0 = fit_kappa(crossings)
    print("  kappa fitted to this camera's crossings: %+.2e (rms %.3f mm, against %.3f at 0)" %
          (kfit, cost, cost0))

    print(" HELD OUT (fitted on the odd wires, read on the even; then the reverse; pooled):")
    for label, k in (("kappa 0", 0.0), ("kappa #1560", MEASURED_KAPPA), ("kappa fitted on the fit half", None)):
        pooled = []
        for parity in (0, 1):
            train = [c for c in crossings if c["wire"] % 2 != parity]
            test = [c for c in crossings if c["wire"] % 2 == parity]
            if len(train) < 12 or not test:
                continue
            kk = fit_kappa(train)[0] if k is None else k
            pooled += residuals_under(test, fit_joint(train, kk), 1.0, kk, 0.0, 1.0)
        print(fmt_row(label, summarise(pooled)))
        if label == "kappa #1560":
            for name, _ in RINGS:
                print(fmt_row("  " + name, summarise(pooled, name)))
    print(" TREBLE HELD OUT WHOLE (fitted on the doubles crossings alone, read on the treble):")
    train = [c for c in crossings if c["ring"].startswith("d")]
    test = [c for c in crossings if c["ring"].startswith("t")]
    if len(train) >= 12 and test:
        for label, k in (("kappa 0", 0.0), ("kappa #1560", MEASURED_KAPPA)):
            print(fmt_row(label, summarise(residuals_under(test, fit_joint(train, k), 1.0, k, 0.0, 1.0))))

    # THE BAND'S MIDDLE (added after the first count, and said so in docs/rig.md: the
    # edges came back widened both ways -- the extraction's fixed bandness threshold
    # blooms as the detector's mask does -- so an edge crossing carries the edge's
    # definition. The mean of a wire's inner and outer crossing of one band is the band's
    # middle, 103 and 166 mm, and a widening equal on both sides cancels in it.)
    mids = band_middles(crossings)
    if len(mids) < 12:
        return
    today = residuals_under(mids, hinv, fit["unit"], 0.0, fit["theta20"], fit["advance"])
    print(" BAND MIDDLES, TODAY (%d of 40):" % len(mids))
    print(fmt_row("treble middle, 103 mm", summarise(today, "t_mid")))
    print(fmt_row("doubles middle, 166 mm (extrap.)", summarise(today, "d_mid")))
    print(" BAND MIDDLES, ONE HOMOGRAPHY (in-sample, then held out odd/even wires):")
    for label, k in (("kappa 0", 0.0), ("kappa #1560", MEASURED_KAPPA), ("kappa fitted", None)):
        kk = fit_kappa(mids)[0] if k is None else k
        res = residuals_under(mids, fit_joint(mids, kk), 1.0, kk, 0.0, 1.0)
        print(fmt_row("in-sample, " + label + (" %+.2e" % kk if k is None else ""), summarise(res)))
        pooled = []
        for parity in (0, 1):
            train = [c for c in mids if c["wire"] % 2 != parity]
            test = [c for c in mids if c["wire"] % 2 == parity]
            if len(train) < 12 or not test:
                continue
            kh = fit_kappa(train)[0] if k is None else k
            pooled += residuals_under(test, fit_joint(train, kh), 1.0, kh, 0.0, 1.0)
        print(fmt_row("held out,  " + label, summarise(pooled)))
        if k is not None:
            print(fmt_row("  held out, treble middle", summarise(pooled, "t_mid")))


# ---------------------------------------------------------------------------- the check

def plant(kappa, noise=0.0, seed=3):
    """A board imaged through a known homography and a known kappa: 80 crossings."""
    import random
    rng = random.Random(seed)
    # a tilted view: unit board mm -> px, about (660, 300), 1.4 px/mm, real perspective
    h = [1.38, -0.21, 660.0, 0.12, 0.92, 300.0, 2.1e-4, -5.2e-4, 1.0]
    out = []
    for k in range(20):
        for name, mm in RINGS:
            a = k * SECTOR
            p = apply(h, mm * math.cos(a), mm * math.sin(a))
            p = distort(p, kappa)
            out.append({"px": (p[0] + rng.gauss(0, noise), p[1] + rng.gauss(0, noise)),
                        "ring": name, "mm": mm, "wire": k})
    return h, out


def run_synthetic():
    bad = []

    def check(name, ok, said):
        print("%s %s: %s" % ("ok  " if ok else "FAIL", name, said))
        if not ok:
            bad.append(name)

    # residual: a crossing 1 mm out and 2 mm round reads (+1, +2)
    rad, tan = split_residual((101.0 * math.cos(0.3 + 2.0 / 100.0), 101.0 * math.sin(0.3 + 2.0 / 100.0)), 100.0, 0.3)
    check("residual: sign and split", abs(rad - 1.0) < 1e-9 and abs(tan - 2.0) < 1e-9,
          "a point 1 mm out and 2 mm round reads radial %+.3f tangential %+.3f" % (rad, tan))

    # undistort inverts distort at this rig's worst corner (lens_census.hpp's)
    p = distort((640 + 300.0, 360 + 213.0), -1.1e-6)
    q = undistort(p, -1.1e-6)
    check("lens: undistort inverts distort", math.hypot(q[0] - 940.0, q[1] - 573.0) < 1e-6,
          "round trip error %.2e px" % math.hypot(q[0] - 940.0, q[1] - 573.0))

    # a flat board, no lens: the joint fit is exact
    h, cr = plant(0.0)
    res = residuals_under(cr, fit_joint(cr, 0.0), 1.0, 0.0, 0.0, 1.0)
    check("fit: a flat board is fitted exactly", rms2(res) < 1e-6, "rms %.2e mm over %d crossings" % (rms2(res), len(res)))

    # the planted homography itself reads the same crossings at zero (the census path:
    # a logged H and a unit, not a fitted one)
    res = residuals_under(cr, inv3(h), 1.0, 0.0, 0.0, 1.0)
    check("census: the true model reads zero", rms2(res) < 1e-9, "rms %.2e mm" % rms2(res))

    # the lens, planted: a kappa-0 fit leaves it, the right kappa removes it, and the
    # scan finds it
    k_true = -6.0e-7
    h, cr = plant(k_true)
    flat = rms2(residuals_under(cr, fit_joint(cr, 0.0), 1.0, 0.0, 0.0, 1.0))
    right = rms2(residuals_under(cr, fit_joint(cr, k_true), 1.0, k_true, 0.0, 1.0))
    check("lens: a flat fit cannot absorb the planted kappa", flat > 0.05,
          "kappa %.1e leaves %.3f mm rms under a kappa-0 homography" % (k_true, flat))
    check("lens: the planted kappa removes it", right < 1e-6, "rms %.2e mm" % right)
    kfit, cost, _ = fit_kappa(cr)
    check("lens: the scan recovers the planted kappa", abs(kfit - k_true) < 2e-9 and cost < 1e-3,
          "fitted %+.3e against %+.3e planted, rms %.2e mm" % (kfit, k_true, cost))

    # held out: fitted on the odd wires, the even wires are predicted
    train = [c for c in cr if c["wire"] % 2 == 1]
    test = [c for c in cr if c["wire"] % 2 == 0]
    held = rms2(residuals_under(test, fit_joint(train, k_true), 1.0, k_true, 0.0, 1.0))
    held_flat = rms2(residuals_under(test, fit_joint(train, 0.0), 1.0, 0.0, 0.0, 1.0))
    check("holdout: the even wires are predicted from the odd", held < 1e-6 and held_flat > 0.05,
          "held-out rms %.2e mm with the planted kappa, %.3f mm without" % (held, held_flat))

    # a doubles-only model mis-places the treble by the lens, with the sign the header
    # predicts: outward (+) under barrel distortion
    dbl = [c for c in cr if c["ring"].startswith("d")]
    tre = [c for c in cr if c["ring"].startswith("t")]
    s = summarise(residuals_under(tre, fit_joint(dbl, 0.0), 1.0, 0.0, 0.0, 1.0))
    check("holdout: a doubles-only flat fit reads the treble outward", s["rad_med"] > 0.05,
          "treble radial median %+.3f mm under kappa %.1e" % (s["rad_med"], k_true))

    # the wire naming: a crossing is named by the model's nearest boundary
    h0, cr0 = plant(0.0)
    names = [dict(c) for c in cr0]
    for c in names:
        c.pop("wire")
    name_wires(names, inv3(h0), 1.0, 0.0, 1.0)
    check("naming: every crossing is given its own wire",
          all(a["wire"] == b["wire"] for a, b in zip(names, cr0)),
          "%d of 80" % sum(1 for a, b in zip(names, cr0) if a["wire"] == b["wire"]))

    # line x conic: a line through a circle's centre meets it at the radius
    circle = [(500 + 120 * math.cos(t / 10.0), 300 + 120 * math.sin(t / 10.0)) for t in range(63)]
    hit = line_meets_conic((560.0, 300.0, 1.0, 0.0), lens.fit_conic(circle))
    check("crossing: a line meets a conic where it should",
          hit is not None and abs(hit[0] - 620.0) < 1e-6 and abs(hit[1] - 300.0) < 1e-6,
          "a radius of a 120 px circle about (500,300), from (560,300), meets it at %s" % (hit,))

    print("I1992 synthetic: %d failed%s" % (len(bad), (" -- " + "; ".join(bad)) if bad else ""))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--synthetic", action="store_true")
    ap.add_argument("--frames")
    ap.add_argument("--fits", nargs="*", default=[], help="tag=<replay log holding I1647FIT lines>")
    args = ap.parse_args()
    if args.synthetic:
        sys.exit(run_synthetic())
    if not args.frames:
        ap.error("--synthetic or --frames")
    for spec in args.fits:
        tag, path = spec.split("=", 1)
        fits = read_fits(path)
        for cam in sorted(fits):
            measure(tag, cam, args.frames, fits[cam])


if __name__ == "__main__":
    main()
