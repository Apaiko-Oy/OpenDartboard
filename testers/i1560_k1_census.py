#!/usr/bin/env python3
# #1560: is #1467's wire residual one lens constant? k1 (and f) per camera, from footage.
#
# Two things in one file, and the split matters. With --frames it is an INSTRUMENT --
# i1499_band_census's kind, host-side, nothing in it can fail on a wrong number: it
# measures, per camera per fixture, every wedge-boundary wire's bow off its own chord
# and the four ring conic residuals, fits one pixel-space radial constant per camera,
# prints the bow residual before and after, and fits f from the rings alone. With
# --synthetic it is a CHECK and registered as one (run_all.sh, 1560-lensmodel), because
# the two halves of the solver that cannot live in a header -- the Gauss-Newton camera
# fit and the rings-only focal-length fit -- still need a control and a mutation.
# The ENSEMBLE half is inlined in lens_census.hpp and held by i1560_lens_check.cpp
# through unit_check.sh (1560-lenscheck). The verdict this instrument produced is
# recorded in that header, beside the constants it measured.
#
# It runs on the HOST, deliberately: Docker is a shared single resource on this rig
# (#1552 held it for the life of #1560's branch), the fit is pure math, and the only
# footage step is a 30-frame average that ffmpeg does exactly as the detector's
# averageOf does (camera.cpp seeks frame round(fps*(3.0 - 0.18*camIdx)) on registry
# builds, #1551, then averages 30 frames). No OpenCV, no numpy: stdlib only.
#
#   python3 testers/i1560_k1_census.py --dump-cmds --mocks mocks --out <dir> | bash
#   python3 testers/i1560_k1_census.py --frames <dir>       # measure those frames
#   python3 testers/i1560_k1_census.py --synthetic          # controls + mutations, exits
#                                                           # on the failure count
#
# Frame provenance (SAY the window, #1551): tags r18w3/r22w3 are the registry 3 s
# calibration window (frames 90/84/79 for cams 1/2/3), r22open is the clip opening
# (OD_SEEK_VIDEO=off's window) -- rig-20260922 camera 1 is only ADMITTED by the wire
# stage at the opening window, so its registry-window numbers here come from this
# instrument's own extraction, not from a detector calibration that exists.
#
# Method, in one paragraph. A dartboard is a calibration target: twenty straight wedge
# wires and four known circles (99/107/162/170 mm, DartboardSpec). Perspective cannot
# bend a straight line, so any systematic curvature of a wire's image trace is lens
# distortion (or non-planar hardware); a circle images to an exact conic under a pinhole,
# so any non-conic egging of a ring is the same. Extraction: average 30 frames, find the
# red/green band edges along rays from the bull seed (subpixel threshold crossing), fit
# the outer-doubles conic, then walk 20 wedge-boundary luminance edges across ~30 radii
# between the rings (subpixel gradient peak).
#
# THE TWO MEASUREMENTS ARE SEPARATE ON PURPOSE, and which data answers which question is
# the whole design. kappa comes from the WIRES and nothing else -- it is a pixel-space
# constant and the bending of a straight line is a pixel-space fact, so no pose, no f
# and no board model enter it, and nothing a pose got wrong can be laundered into it.
# f comes from the RINGS and nothing else -- a plane target fixes f through perspective
# foreshortening, which four concentric circles of known millimetre radii carry at four
# radii at once and twenty concurrent straight lines do not carry at all. Measured, not
# argued: with the wires in the f fit, f ran to 1e11 on three of nine cameras.
#
# The BEFORE/AFTER the issue asks for is therefore on the bows themselves: the rms of
# the twenty measured bows, and the rms of what is left once the fitted kappa is taken
# out of every wire. The trap (#1513, binding): radial distortion cannot bend a line
# through the distortion centre, so each camera also reports where its board sits
# relative to (640,360), the bow k1 = -0.20 would draw ON ITS OWN MEASURED CHORDS, and
# the smallest |k1| its geometry and its scatter could separate from zero at 2 sigma.
# A camera that cannot separate 0.20 -- the middle of #1513's sweep -- answers "cannot
# tell from this footage", never "k1 = 0".

import argparse
import json
import math
import os
import random
import sys

W, H = 1280, 720
PP = (640.0, 360.0)  # principal point pinned at the image centre (OV9732, no crop)
RING_MM = [99.0, 107.0, 162.0, 170.0]  # DartboardSpec: treble in/out, double in/out
F_GUESS = 424.0  # perspective_processing.cpp's hard-coded 120-degree diagonal

# Bull seeds from the #1551 admission transcripts (byte-identical across runs), so the
# instrument starts where the detector's own calibration did. rig-20260922 cam 1 at the
# 3 s window is the bull the REFUSED calibration still measured (bull detection is
# upstream of the wire gate).
SEEDS = {
    ("r18w3", 1): (671, 309), ("r18w3", 2): (626, 292), ("r18w3", 3): (700, 298),
    ("r22w3", 1): (640, 302), ("r22w3", 2): (644, 232), ("r22w3", 3): (704, 285),
    ("r22open", 1): (653, 302), ("r22open", 2): (644, 232), ("r22open", 3): (710, 280),
}
FIXTURE = {"r18w3": "rig-20260918", "r22w3": "rig-20260922", "r22open": "rig-20260922"}
START_FRAME = {"r18w3": {1: 90, 2: 84, 3: 79}, "r22w3": {1: 90, 2: 84, 3: 79},
               "r22open": {1: 0, 2: 0, 3: 0}}


def dump_commands(mocks, outdir):
    lines = []
    for tag, cams in START_FRAME.items():
        for cam, start in cams.items():
            src = os.path.join(mocks, FIXTURE[tag], "cam_%d.mp4" % cam)
            dst = os.path.join(outdir, "%s-cam%d.ppm" % (tag, cam))
            lines.append(
                "ffmpeg -v error -y -i \"%s\" -vf \"trim=start_frame=%d,setpts=PTS-STARTPTS,"
                "tmix=frames=30,select='eq(n\\,29)'\" -fps_mode passthrough -frames:v 1 \"%s\""
                % (src, start, dst))
    return lines


# ---------------------------------------------------------------------------- image io

def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # P6, whitespace/comments, w h maxval, raw
    tokens, i = [], 0
    while len(tokens) < 4:
        while i < len(data) and data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b"#":
            while i < len(data) and data[i] != 10:
                i += 1
            continue
        j = i
        while j < len(data) and not data[j:j + 1].isspace():
            j += 1
        tokens.append(data[i:j])
        i = j
    assert tokens[0] == b"P6", "not a raw PPM"
    w, h, maxv = int(tokens[1]), int(tokens[2]), int(tokens[3])
    assert maxv == 255
    return w, h, data[i + 1:i + 1 + w * h * 3]


class Frame:
    def __init__(self, path):
        self.w, self.h, self.px = read_ppm(path)

    def rgb(self, x, y):
        """Bilinear-interpolated (r, g, b) floats."""
        if x < 0 or y < 0 or x >= self.w - 1 or y >= self.h - 1:
            return (0.0, 0.0, 0.0)
        x0, y0 = int(x), int(y)
        fx, fy = x - x0, y - y0
        p = self.px
        i00 = (y0 * self.w + x0) * 3
        i10 = i00 + 3
        i01 = i00 + self.w * 3
        i11 = i01 + 3
        w00 = (1 - fx) * (1 - fy)
        w10 = fx * (1 - fy)
        w01 = (1 - fx) * fy
        w11 = fx * fy
        return tuple(p[i00 + c] * w00 + p[i10 + c] * w10 + p[i01 + c] * w01 + p[i11 + c] * w11
                     for c in range(3))

    def bandness(self, x, y):
        """How red-or-green a pixel is: the doubles/treble bands and nothing black/white."""
        r, g, b = self.rgb(x, y)
        return max(r - max(g, b), g - max(r, b))

    def luma(self, x, y):
        r, g, b = self.rgb(x, y)
        return (r + g + b) / 3.0


# ------------------------------------------------------------------- small linear algebra

def solve(a, b):
    """Gaussian elimination with partial pivoting; a is n x n (list of lists)."""
    n = len(a)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for col in range(n):
        piv = max(range(col, n), key=lambda r: abs(m[r][col]))
        if abs(m[piv][col]) < 1e-300:
            raise ValueError("singular")
        m[col], m[piv] = m[piv], m[col]
        d = m[col][col]
        for r in range(col + 1, n):
            f = m[r][col] / d
            for c in range(col, n + 1):
                m[r][c] -= f * m[col][c]
    x = [0.0] * n
    for r in range(n - 1, -1, -1):
        s = m[r][n] - sum(m[r][c] * x[c] for c in range(r + 1, n))
        x[r] = s / m[r][r]
    return x


def jacobi_eigen(a):
    """Eigen-decomposition of a small symmetric matrix by Jacobi rotations."""
    n = len(a)
    a = [row[:] for row in a]
    v = [[1.0 if i == j else 0.0 for j in range(n)] for i in range(n)]
    for _ in range(100):
        off, p, q = 0.0, 0, 1
        for i in range(n):
            for j in range(i + 1, n):
                if abs(a[i][j]) > off:
                    off, p, q = abs(a[i][j]), i, j
        if off < 1e-12:
            break
        app, aqq, apq = a[p][p], a[q][q], a[p][q]
        theta = 0.5 * math.atan2(2 * apq, aqq - app)
        c, s = math.cos(theta), math.sin(theta)
        for k in range(n):
            akp, akq = a[k][p], a[k][q]
            a[k][p] = c * akp - s * akq
            a[k][q] = s * akp + c * akq
        for k in range(n):
            apk, aqk = a[p][k], a[q][k]
            a[p][k] = c * apk - s * aqk
            a[q][k] = s * apk + c * aqk
        for k in range(n):
            vkp, vkq = v[k][p], v[k][q]
            v[k][p] = c * vkp - s * vkq
            v[k][q] = s * vkp + c * vkq
    eig = [(a[i][i], [v[k][i] for k in range(n)]) for i in range(n)]
    eig.sort(key=lambda e: e[0])
    return eig


# -------------------------------------------------------------------------- conic fitting

def fit_conic(points):
    """Algebraic conic through points, normalised; returns 6 coefficients."""
    mx = sum(p[0] for p in points) / len(points)
    my = sum(p[1] for p in points) / len(points)
    sc = sum(math.hypot(p[0] - mx, p[1] - my) for p in points) / len(points)
    rows = []
    for x, y in points:
        xn, yn = (x - mx) / sc, (y - my) / sc
        rows.append([xn * xn, xn * yn, yn * yn, xn, yn, 1.0])
    ata = [[sum(r[i] * r[j] for r in rows) for j in range(6)] for i in range(6)]
    coeff = jacobi_eigen(ata)[0][1]
    return coeff, mx, my, sc


def conic_residuals(points, fit):
    """First-order geometric distance of each point to the fitted conic, px."""
    (A, B, C, D, E, F), mx, my, sc = fit
    out = []
    for x, y in points:
        xn, yn = (x - mx) / sc, (y - my) / sc
        q = A * xn * xn + B * xn * yn + C * yn * yn + D * xn + E * yn + F
        gx = 2 * A * xn + B * yn + D
        gy = B * xn + 2 * C * yn + E
        g = math.hypot(gx, gy)
        out.append(q / g * sc if g > 1e-12 else 0.0)
    return out


def rms(vals):
    return math.sqrt(sum(v * v for v in vals) / len(vals)) if vals else 0.0


# ----------------------------------------------------------------------- band extraction

def ray_band_runs(frame, cx, cy, theta, r0, r1, thresh=26.0):
    """Contiguous red/green runs along one ray; [(rin, rout, rmid, length)] subpixel."""
    dx, dy = math.cos(theta), math.sin(theta)
    runs, start, prev = [], None, None
    r = r0
    while r <= r1:
        b = frame.bandness(cx + r * dx, cy + r * dy)
        on = b > thresh
        if on and start is None:
            # subpixel inner edge: interpolate crossing between prev sample and this
            if prev is not None and prev[1] <= thresh:
                f = (thresh - prev[1]) / (b - prev[1])
                start = prev[0] + f * (r - prev[0])
            else:
                start = r
        elif not on and start is not None:
            f = (prev[1] - thresh) / (prev[1] - b) if prev[1] > thresh else 0.0
            end = prev[0] + f * (r - prev[0])
            if end - start >= 1.5:
                runs.append((start, end, 0.5 * (start + end), end - start))
            start = None
        prev = (r, b)
        r += 1.0
    if start is not None and prev[0] - start >= 1.5:
        runs.append((start, prev[0], 0.5 * (start + prev[0]), prev[0] - start))
    return runs


def ray_conic_crossing(fit, origin, theta):
    """Distance from `origin` to the fitted conic along direction theta (outer root)."""
    (A, B, C, D, E, F), mx, my, sc = fit
    ox, oy = (origin[0] - mx) / sc, (origin[1] - my) / sc
    c, s = math.cos(theta), math.sin(theta)
    a2 = A * c * c + B * c * s + C * s * s
    b1 = 2 * A * ox * c + B * (ox * s + oy * c) + 2 * C * oy * s + D * c + E * s
    c0 = A * ox * ox + B * ox * oy + C * oy * oy + D * ox + E * oy + F
    disc = b1 * b1 - 4 * a2 * c0
    if disc <= 0 or abs(a2) < 1e-12:
        return None
    t = (-b1 + math.sqrt(disc)) / (2 * a2)
    return t * sc if t > 0 else None


def extract_ring_edges(frame, seed, rounds=3):
    """Per angle: doubles band (in, out) and treble band (in, out) radii, subpixel.

    Two passes. The first picks the outermost coloured run per ray and fits a rough
    conic to the radius-median inliers -- the Winmau badges on the number ring are RED
    and sit OUTSIDE the doubles band, so 'outermost' alone is poisoned at those angles.
    The second re-picks per ray the run nearest the rough conic's own crossing, which
    refuses the badges, the bull, and a parked dart's flight by position.

    The second pass is ITERATED (#1560, measured): the acceptance window is +/-14 px
    about the guide conic's own crossing, so a poor guide rejects rays that hold a
    perfectly good band edge and the yield collapses -- and it is the yield, not the
    threshold, that decides whether a camera's rings can measure f at all. Refitting
    the guide from what was accepted and re-running the acceptance lifted three
    cameras by 25-120% of their points (r18w3 cam2's doubles-outer 271 -> 364 points,
    conic rms 7.37 -> 2.89 px) and moved the clean camera by under 1%.

    Returns dict edge name -> list of (x, y) image points."""
    cx, cy = seed
    n = 720
    edges = {"d_out": [], "d_in": [], "t_out": [], "t_in": []}
    per_angle_runs = []
    first = []
    for i in range(n):
        theta = 2 * math.pi * i / n
        runs = ray_band_runs(frame, cx, cy, theta, 40.0, 430.0)
        per_angle_runs.append((theta, runs))
        # THE PAIR RULE, scale-free on purpose: an oblique board's doubles radius
        # varies two-fold around the ellipse, so no global radius band can pick it --
        # but the true doubles run has its treble partner at ~0.61 of its radius on
        # the same ray (transcript fractions 0.5824..0.6294), and the wooden deck, a
        # red badge or a dart flight has no such partner.
        best = None
        for a in runs:
            if a[2] < 100.0:
                continue
            for b in runs:
                if 0.53 <= b[2] / a[2] <= 0.70:
                    if best is None or a[2] > best[2]:
                        best = a
        if best is not None:
            first.append((theta, best[2]))
    if len(first) < 60:
        return edges
    rough_pts = [(cx + r * math.cos(th), cy + r * math.sin(th)) for th, r in first]
    rough = fit_conic(rough_pts)
    for _ in range(3):
        res = conic_residuals(rough_pts, rough)
        s = max(rms(res), 1.5)
        rough_pts = [p for p, e in zip(rough_pts, res) if abs(e) <= 3.0 * s]
        rough = fit_conic(rough_pts)
    for _round in range(rounds):
        edges = {"d_out": [], "d_in": [], "t_out": [], "t_in": []}
        for i in range(n):
            theta, runs = per_angle_runs[i]
            re = ray_conic_crossing(rough, (cx, cy), theta)
            if re is None:
                continue
            dxy = (math.cos(theta), math.sin(theta))
            # doubles: the run whose MID radius is nearest the expected band mid
            exp_mid = re * (0.9762 + 0.9309) / 2.0 / 0.9762  # conic tracks the outer edge
            db = None
            cand = [r for r in runs if abs(r[2] - exp_mid) < 14.0]
            if cand:
                db = min(cand, key=lambda r: abs(r[2] - exp_mid))
            if db:
                edges["d_out"].append((cx + db[1] * dxy[0], cy + db[1] * dxy[1]))
                edges["d_in"].append((cx + db[0] * dxy[0], cy + db[0] * dxy[1]))
            # treble: fractions 0.5824..0.6294 of the doubles OUTER edge crossing
            exp_t = re * (0.5824 + 0.6294) / 2.0
            cand = [r for r in runs if abs(r[2] - exp_t) < 14.0]
            if cand:
                tb = min(cand, key=lambda r: abs(r[2] - exp_t))
                edges["t_out"].append((cx + tb[1] * dxy[0], cy + tb[1] * dxy[1]))
                edges["t_in"].append((cx + tb[0] * dxy[0], cy + tb[0] * dxy[1]))
        if len(edges["d_out"]) < 60:
            break
        _, rough, _ = trim_conic(median_prefilter(edges["d_out"], seed))
    return edges


def trim_conic(points, passes=2, cut=3.0):
    """Conic fit with iterated outlier rejection; returns kept points, fit, residuals."""
    pts = points[:]
    fit = fit_conic(pts)
    for _ in range(passes):
        res = conic_residuals(pts, fit)
        s = rms(res)
        kept = [p for p, r in zip(pts, res) if abs(r) <= cut * max(s, 0.3)]
        if len(kept) < 12 or len(kept) == len(pts):
            pts = kept or pts
            break
        pts = kept
        fit = fit_conic(pts)
    return pts, fit, conic_residuals(pts, fit)


def ellipse_radius(fit_center_form, theta):
    """Radius of the fitted d_out conic along direction theta from its own centre."""
    (A, B, C, D, E, F), mx, my, sc = fit_center_form
    # centre of the conic in normalised coords
    den = 4 * A * C - B * B
    xc = (B * E - 2 * C * D) / den
    yc = (B * D - 2 * A * E) / den
    c, s = math.cos(theta), math.sin(theta)
    # Q(xc + t c, yc + t s) = 0 -> quadratic in t
    a2 = A * c * c + B * c * s + C * s * s
    b1 = 2 * A * xc * c + B * (xc * s + yc * c) + 2 * C * yc * s + D * c + E * s
    c0 = A * xc * xc + B * xc * yc + C * yc * yc + D * xc + E * yc + F
    disc = b1 * b1 - 4 * a2 * c0
    if disc <= 0 or abs(a2) < 1e-12:
        return None
    t = (-b1 + math.sqrt(disc)) / (2 * a2)
    return t * sc, (mx + (xc + t * c) * sc, my + (yc + t * s) * sc), (mx + xc * sc, my + yc * sc)


# ----------------------------------------------------------------------- wire extraction

def wire_traces(frame, dout_fit, bull, t_lo=(0.18, 0.50), t_hi=(0.66, 0.90), t_step=0.02):
    """Trace the 20 wedge-boundary edges across radius fractions of the doubles conic.

    Sampling centre is the BULL (the image of the board centre, where the wires concur),
    so a straight wire holds a near-constant angle across every fraction and the tracker
    cannot walk onto a neighbour; radii ride the doubles conic's crossing along each ray.

    A point lands where the LUMINANCE EDGE is, at whichever angle the scan found it, so
    an error in the guiding conic moves a point ALONG the wire rather than off it -- and
    a bow is a curvature, which sliding a point along a line cannot create. That is why
    the census's kappa is the one number here that does not inherit the ring
    extraction's quality: five of the nine cameras cannot measure a focal length from
    their rings and all nine measure a bow.
    Tracking is sequential outward from a mid-bed reference fraction, so the small drift
    a bull-seed error buys accumulates one 1.5-degree-bounded step at a time.
    Returns list of traces, each a list of (x, y, frac)."""
    bx, by = bull
    radii = {}
    for i in range(1440):
        th = 2 * math.pi * i / 1440
        radii[i] = ray_conic_crossing(dout_fit, (bx, by), th)

    def scan(frac):
        vals = []
        for i in range(1440):
            r = radii[i]
            if r is None or r <= 0:
                vals.append(None)
                continue
            th = 2 * math.pi * i / 1440
            vals.append(frame.luma(bx + frac * r * math.cos(th), by + frac * r * math.sin(th)))
        peaks = []
        for i in range(1440):
            a, b, c = vals[(i - 1) % 1440], vals[i], vals[(i + 1) % 1440]
            if a is None or b is None or c is None:
                continue
            g0 = (c - a) / 2.0
            a2, c2 = vals[(i - 2) % 1440], vals[(i + 2) % 1440]
            if a2 is None or c2 is None:
                continue
            gm1 = abs((b - a2) / 2.0)
            gp1 = abs((c2 - b) / 2.0)
            gc = abs(g0)
            if gc > 18.0 and gc >= gm1 and gc >= gp1:
                denom = (gm1 - 2 * gc + gp1)
                off = 0.5 * (gm1 - gp1) / denom if abs(denom) > 1e-9 else 0.0
                off = max(-0.5, min(0.5, off))
                peaks.append(((i + off) % 1440) * 360.0 / 1440.0)
        # merge peaks closer than 1.2 deg (the two paint edges of one blade)
        peaks.sort()
        merged = []
        for p in peaks:
            if merged and (p - merged[-1][-1]) < 1.2:
                merged[-1].append(p)
            else:
                merged.append([p])
        if merged and len(merged) > 1 and (360.0 - merged[-1][-1] + merged[0][0]) < 1.2:
            merged[0] = merged.pop() + merged[0]
        return [sum(m) / len(m) for m in merged]

    def nearest(got, ang0, tol):
        best, bd = None, tol
        for a in got:
            d = abs((a - ang0 + 180.0) % 360.0 - 180.0)
            if d < bd:
                best, bd = a, d
        return best

    fracs = [round(t_lo[0] + k * t_step, 4) for k in range(int(round((t_lo[1] - t_lo[0]) / t_step)) + 1)]
    fracs += [round(t_hi[0] + k * t_step, 4) for k in range(int(round((t_hi[1] - t_hi[0]) / t_step)) + 1)]
    ref_frac = 0.34
    ref = scan(ref_frac)
    if len(ref) < 18:
        ref_frac = 0.42
        ref = scan(ref_frac)
    scans = {f: scan(f) for f in fracs}
    traces = []
    for ang0 in ref:
        trace, last = [], ang0
        # walk downward through fractions below the reference, then reset and walk up
        below = sorted([f for f in fracs if f <= ref_frac], reverse=True)
        above = sorted([f for f in fracs if f > ref_frac])
        for leg in (below, above):
            last = ang0
            for f in leg:
                a = nearest(scans[f], last, 1.5)
                if a is None:
                    continue
                last = a
                r = radii[int(a * 4) % 1440]
                if r:
                    th = math.radians(a)
                    trace.append((bx + f * r * math.cos(th), by + f * r * math.sin(th), f))
        traces.append(trace)
    return [t for t in traces if len(t) >= 12]


def quad_sagitta(pts, mx, my, ux, uy):
    """Signed sagitta of points about the line (mx,my)+t(ux,uy): fit dev(t) as a
    quadratic (offset and tilt free, so only curvature is read) and return the
    parabola's height over the span, its rms about the fit, and the span."""
    nx, ny = -uy, ux
    ts = [(x - mx) * ux + (y - my) * uy for x, y in pts]
    devs = [(x - mx) * nx + (y - my) * ny for x, y in pts]
    n = len(ts)
    s0, s1, s2 = n, sum(ts), sum(t * t for t in ts)
    s3, s4 = sum(t ** 3 for t in ts), sum(t ** 4 for t in ts)
    b0 = sum(devs)
    b1 = sum(d * t for d, t in zip(devs, ts))
    b2 = sum(d * t * t for d, t in zip(devs, ts))
    c0, c1, c2 = solve([[s0, s1, s2], [s1, s2, s3], [s2, s3, s4]], [b0, b1, b2])
    span = max(ts) - min(ts)
    resid = [d - (c0 + c1 * t + c2 * t * t) for d, t in zip(devs, ts)]
    return c2 * span * span / 8.0, rms(resid), span, (min(ts), max(ts))


def line_fit_bow(trace):
    """Total-least-squares line with a 10% trim; returns (bow_px, rms_px, quad_px,
    dist_from_pp, n, span, line) where line = (mx, my, ux, uy, tmin, tmax)."""
    xs = [p[0] for p in trace]
    ys = [p[1] for p in trace]
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs) / n
    syy = sum((y - my) ** 2 for y in ys) / n
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / n
    theta = 0.5 * math.atan2(2 * sxy, sxx - syy)
    ux, uy = math.cos(theta), math.sin(theta)     # along the line
    nx, ny = -uy, ux                              # normal
    devs = [(x - mx) * nx + (y - my) * ny for x, y in zip(xs, ys)]
    # robust: drop the worst 10% once (dart shafts, glyph clips)
    order = sorted(range(n), key=lambda i: abs(devs[i]))
    keep = order[: max(8, int(n * 0.9))]
    pts = [(xs[i], ys[i]) for i in keep]
    bow, r, span, trange = quad_sagitta(pts, mx, my, ux, uy)
    d_pp = abs((PP[0] - mx) * nx + (PP[1] - my) * ny)
    return bow, r, bow, d_pp, len(pts), span, (mx, my, ux, uy, trange[0], trange[1])


def _straighten(line, kappa, n=25):
    """The undistorted chord of an OBSERVED line, at a given kappa: undistort the
    samples and take their total-least-squares line. Identity at kappa = 0."""
    mx, my, ux, uy, t0, t1 = line
    pts = []
    for i in range(n):
        t = t0 + (t1 - t0) * i / (n - 1.0)
        dx, dy = mx + t * ux - PP[0], my + t * uy - PP[1]
        xu, yu = dx, dy
        for _ in range(6):
            s = 1.0 + kappa * (xu * xu + yu * yu)
            if abs(s) < 1e-6:
                break
            xu, yu = dx / s, dy / s
        pts.append((PP[0] + xu, PP[1] + yu))
    m = len(pts)
    ax = sum(p[0] for p in pts) / m
    ay = sum(p[1] for p in pts) / m
    sxx = sum((p[0] - ax) ** 2 for p in pts) / m
    syy = sum((p[1] - ay) ** 2 for p in pts) / m
    sxy = sum((p[0] - ax) * (p[1] - ay) for p in pts) / m
    th = 0.5 * math.atan2(2 * sxy, sxx - syy)
    vx, vy = math.cos(th), math.sin(th)
    ts = [(p[0] - ax) * vx + (p[1] - ay) * vy for p in pts]
    return (ax, ay, vx, vy, min(ts), max(ts))


def ensemble_kappa(bows_and_lines, kref=1e-7, refine=2):
    """The assumption-light kappa: no pose, no f, no board model -- just the measured
    bows against the bow each wire's own chord geometry would take per unit kappa
    about the principal point (linear in kappa at this rig's magnitudes; the same
    estimator lens_census.hpp inlines as fitKappaFromBows). Returns (kappa, sigma,
    gain, modelled) with sigma from the per-wire scatter, which prices every
    unmodelled thing honestly, and the bow the fitted kappa MODELS for each wire, so
    a caller can subtract it wire by wire -- which is the census's AFTER.

    The slope has to be taken about the wire's UNDISTORTED chord, and the only chord
    this instrument can see is the distorted one, so a single linear solve is biased
    twice over -- a barrel-shrunk chord is both shorter and already bent, and sagitta
    goes as span squared. The estimator therefore straightens each line at the current
    kappa, predicts the bow that kappa would draw on the straightened chord, and takes
    a Gauss-Newton step on the difference; three passes converge. Measured on the
    synthetic control at a deliberately brutal k1 = -0.20: one linear solve reads
    -0.2478 (+24%), the converged estimator reads within a percent. At the |k1| <= 0.1
    the fixtures actually measure, even the raw bias is inside sigma -- the refinement
    is here so the CONTROL is unbiased, not because the footage needed it."""
    ss = [b for b, _ in bows_and_lines]

    def predict(kappa):
        """(modelled bow, d bow / d kappa) per wire at this kappa."""
        # noqa: the closure over t0/t1/mx/... is per-iteration and consumed at once
        mods, slopes = [], []
        for _bow, line in bows_and_lines:
            mx, my, ux, uy, t0, t1 = _straighten(line, kappa) if kappa else line

            def bow_at(k):
                pts = []
                for i in range(25):
                    t = t0 + (t1 - t0) * i / 24.0
                    dx, dy = mx + t * ux - PP[0], my + t * uy - PP[1]
                    s = 1.0 + k * (dx * dx + dy * dy)
                    pts.append((PP[0] + dx * s, PP[1] + dy * s))
                return quad_sagitta(pts, mx, my, ux, uy)[0]

            b0 = bow_at(kappa)
            mods.append(b0)
            slopes.append((bow_at(kappa + kref) - b0) / kref)
        return mods, slopes

    kappa = 0.0
    mods, gg = predict(kappa)
    for _ in range(max(1, refine) + 1):
        den = sum(g * g for g in gg)
        if den <= 0:
            return 0.0, float("inf"), 0.0, mods
        kappa += sum((s - m0) * g for s, m0, g in zip(ss, mods, gg)) / den
        mods, gg = predict(kappa)
    den = sum(g * g for g in gg)
    if den <= 0:
        return 0.0, float("inf"), 0.0, mods
    n = len(ss)
    sigma = float("inf")
    if n > 1:
        resid2 = sum((s - m0) ** 2 for s, m0 in zip(ss, mods))
        sigma = math.sqrt(resid2 / (n - 1) / den)
    return kappa, sigma, den, mods


def bow_per_unit_kappa(lines, kref=1e-7):
    """The bow each observed wire chord takes per unit kappa, from the chord and the
    principal point alone. This is THE TRAP AS ARITHMETIC and the reason the census's
    sensitivity is not read off a pose: a chord through the distortion centre returns
    0 here whatever else is true, a camera whose pose fit diverged still gets an
    honest sensitivity, and the number is a property of what was measured rather than
    of a model that may not fit."""
    out = []
    for line in lines:
        mx, my, ux, uy, t0, t1 = line
        pts = []
        for i in range(25):
            t = t0 + (t1 - t0) * i / 24.0
            dx, dy = mx + t * ux - PP[0], my + t * uy - PP[1]
            s = 1.0 + kref * (dx * dx + dy * dy)
            pts.append((PP[0] + dx * s, PP[1] + dy * s))
        out.append(quad_sagitta(pts, mx, my, ux, uy)[0] / kref)
    return out


# --------------------------------------------------------------------- camera model fit

def rot_matrix(rx, ry, rz):
    """Rotation from a rotation vector (Rodrigues)."""
    th = math.sqrt(rx * rx + ry * ry + rz * rz)
    if th < 1e-12:
        return [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
    kx, ky, kz = rx / th, ry / th, rz / th
    c, s, v = math.cos(th), math.sin(th), 1 - math.cos(th)
    return [
        [c + kx * kx * v, kx * ky * v - kz * s, kx * kz * v + ky * s],
        [ky * kx * v + kz * s, c + ky * ky * v, ky * kz * v - kx * s],
        [kz * kx * v - ky * s, kz * ky * v + kx * s, c + kz * kz * v],
    ]


class CameraModel:
    """f, rotation vector, translation (mm), and pixel-space radial distortion.

    The distortion parameter is KAPPA in units of 1e-6 px^-2, applied around the
    principal point in PIXELS: p_d = pp + (p_u - pp) * (1 + kappa * r_px^2). This is
    exactly Brown-Conrady's k1 with kappa = k1 / f^2 -- reparameterised because the
    bending of a straight line is a pure pixel-space fact: measured bow determines
    kappa however well or badly f itself resolves, where a normalised k1 would inherit
    f's uncertainty squared. k1 at any f is kappa * f^2."""

    def __init__(self, params):
        self.f, self.rx, self.ry, self.rz, self.tx, self.ty, self.tz, mk = params
        self.kappa = mk * 1e-6
        self.R = rot_matrix(self.rx, self.ry, self.rz)

    def undistort_px(self, xd, yd):
        """Invert the pixel-space distortion by fixed point; px offsets from pp."""
        xu, yu = xd, yd
        for _ in range(5):
            r2 = xu * xu + yu * yu
            d = 1.0 + self.kappa * r2
            if abs(d) < 1e-6:
                break
            xu, yu = xd / d, yd / d
        return xu, yu

    def unproject_to_board(self, u, v):
        """Image px -> board plane mm (Z=0 plane of the model)."""
        xu, yu = self.undistort_px(u - PP[0], v - PP[1])
        xn, yn = xu / self.f, yu / self.f
        # ray p = t * (xn, yn, 1); board plane: R^T (p - T) has Z = 0
        R, T = self.R, (self.tx, self.ty, self.tz)
        # Z_board = r13(px-tx)... using columns of R as board axes in camera coords:
        # p = R [X Y 0]^T + T -> solve 2x2 for X, Y after eliminating t
        r = R
        # camera ray direction d = (xn, yn, 1); p = s d
        # s d = X c1 + Y c2 + T  with c1, c2 columns
        a = [[r[0][0], r[0][1], -xn], [r[1][0], r[1][1], -yn], [r[2][0], r[2][1], -1.0]]
        b = [-T[0], -T[1], -T[2]]
        X, Y, _s = solve(a, b)
        return X, Y

    def project(self, X, Y):
        R, T = self.R, (self.tx, self.ty, self.tz)
        px = R[0][0] * X + R[0][1] * Y + T[0]
        py = R[1][0] * X + R[1][1] * Y + T[1]
        pz = R[2][0] * X + R[2][1] * Y + T[2]
        xu, yu = self.f * px / pz, self.f * py / pz
        r2 = xu * xu + yu * yu
        d = 1.0 + self.kappa * r2
        return PP[0] + xu * d, PP[1] + yu * d

    def local_scale(self, X, Y):
        """px per mm at a board point, radial direction."""
        e = 0.5
        r = math.hypot(X, Y) or 1.0
        ux, uy = X / r, Y / r
        x1, y1 = self.project(X - e * ux, Y - e * uy)
        x2, y2 = self.project(X + e * ux, Y + e * uy)
        return math.hypot(x2 - x1, y2 - y1) / (2 * e)


# Per-class measurement sigmas, mm on the board plane. Wires are the cleanest thing
# this extractor measures (0.1-0.6 px about their own line, ~2.4 px/mm); ring edges
# carry colour bloom and wire-crossing scallops; the bull seed is the detector's own,
# good to a few px. The census question is whether one kappa explains the WIRE
# residual, so the wires carry the weight they earned.
SIGMA_WIRE_MM = 0.25
SIGMA_RING_MM = 0.6
SIGMA_BULL_MM = 1.5


def residuals(params, data, huber_sigma=4.0, raw=False):
    """Noise-weighted residuals (sigma units, Huber-tempered); data holds
    rings [(u,v,rho)], wires [(u,v)] (boundaries at multiples of 18 deg in board
    frame -- the board's own rotation lives in rz), and bull (u,v)."""
    cam = CameraModel(params)
    out = []
    for u, v, rho in data["rings"]:
        try:
            X, Y = cam.unproject_to_board(u, v)
            e = math.hypot(X, Y) - rho
        except ValueError:
            e = 10.0
        out.append(e if raw else e / SIGMA_RING_MM)
    for u, v in data["wires"]:
        try:
            X, Y = cam.unproject_to_board(u, v)
            r = math.hypot(X, Y)
            psi = math.atan2(Y, X)
            d = (math.degrees(psi) % 18.0)
            d = d if d <= 9.0 else d - 18.0
            e = r * math.sin(math.radians(d))
        except ValueError:
            e = 10.0
        out.append(e if raw else e / SIGMA_WIRE_MM)
    if data.get("bull"):
        try:
            X, Y = cam.unproject_to_board(*data["bull"])
        except ValueError:
            X, Y = 10.0, 10.0
        out.append(X if raw else X / SIGMA_BULL_MM)
        out.append(Y if raw else Y / SIGMA_BULL_MM)
    if raw:
        return out
    hub = []
    for e in out:
        a = abs(e)
        hub.append(e if a <= huber_sigma
                   else math.copysign(math.sqrt(huber_sigma * (2 * a - huber_sigma)), e))
    return hub


def numeric_step(p, idx):
    if idx == 7:          # microkappa, O(1)
        return 1e-3
    if idx == 0:          # f, px
        return 1e-2
    return max(1e-6, abs(p[idx]) * 1e-5)


def gauss_newton(params, data, free, iters=40):
    """Damped Gauss-Newton over the free parameter indices."""
    p = params[:]
    lam = 1e-3
    last = None
    for _ in range(iters):
        r0 = residuals(p, data)
        cost = sum(e * e for e in r0)
        m = len(r0)
        jac = []
        for idx in free:
            step = numeric_step(p, idx)
            q = p[:]
            q[idx] += step
            r1 = residuals(q, data)
            jac.append([(r1[i] - r0[i]) / step for i in range(m)])
        n = len(free)
        jtj = [[sum(jac[i][k] * jac[j][k] for k in range(m)) for j in range(n)] for i in range(n)]
        jtr = [sum(jac[i][k] * r0[k] for k in range(m)) for i in range(n)]
        improved = False
        for _try in range(8):
            a = [[jtj[i][j] + (lam * jtj[i][i] if i == j else 0.0) for j in range(n)] for i in range(n)]
            try:
                dx = solve(a, [-g for g in jtr])
            except ValueError:
                lam *= 10
                continue
            q = p[:]
            for i, idx in enumerate(free):
                q[idx] += dx[i]
            r1 = residuals(q, data)
            c1 = sum(e * e for e in r1)
            if c1 < cost:
                p, cost, improved = q, c1, True
                lam = max(lam * 0.3, 1e-7)
                break
            lam *= 10
        if not improved:
            break
        if last is not None and abs(last - cost) < 1e-9 * max(1.0, cost):
            break
        last = cost
    return p, cost


def param_sigma(params, data, free):
    """1-sigma from the Gauss-Newton covariance at the solution, inflated by
    sqrt(chi2/dof) so an underestimated noise model widens rather than flatters."""
    r0 = residuals(params, data)
    m = len(r0)
    dof = max(1, m - len(free))
    s2 = sum(e * e for e in r0) / dof
    jac = []
    for idx in free:
        step = numeric_step(params, idx)
        q = params[:]
        q[idx] += step
        r1 = residuals(q, data)
        jac.append([(r1[i] - r0[i]) / step for i in range(m)])
    n = len(free)
    jtj = [[sum(jac[i][k] * jac[j][k] for k in range(m)) for j in range(n)] for i in range(n)]
    try:
        cov = [solve(jtj, [1.0 if i == j else 0.0 for i in range(n)]) for j in range(n)]
    except ValueError:
        return {idx: float("nan") for idx in free}
    return {idx: math.sqrt(max(0.0, cov[i][i] * s2)) for i, idx in enumerate(free)}


def initial_pose(dout_points, wires_seed_center):
    """Coarse init: centre and scale from the doubles conic, tilt from its axis ratio
    ABOUT THE AXIS ITS MINOR AXIS NAMES.

    The direction matters and assuming it was the pitfall (#1560, measured): a circle
    tilted by tau images to an ellipse whose MINOR axis lies along the direction of
    steepest depth change, so the rotation axis is the image direction perpendicular
    to it. Starting every camera at 'tilt about the horizontal' left the fit a
    rotation away from its own minimum, and a plane target's chi-squared surface in
    (f, tz) is a long shallow valley -- so it converged to a different f from a
    different start, which is a measurement of the starting point. With the direction
    read off the ellipse, the rings-only fit lands on the same f from every f start
    this census tries. The sign is still ambiguous -- a conic cannot say which way a
    plane tips -- so the caller tries both."""
    fit = fit_conic(dout_points)
    e0 = ellipse_radius(fit, 0.0)
    if e0 is None:
        return None
    # sample conic radii to get major/minor and the minor-axis direction
    rr = []
    for i in range(180):
        th = math.pi * i / 180.0
        e = ellipse_radius(fit, th)
        if e:
            rr.append((e[0], th))
    if not rr:
        return None
    rmax = max(r for r, _ in rr)
    rmin, th_min = min(rr)
    _, _, (ecx, ecy) = e0
    f0 = F_GUESS
    tz = f0 * 170.0 / rmax
    tilt = math.acos(max(0.2, min(1.0, rmin / rmax)))
    ax, ay = -math.sin(th_min), math.cos(th_min)   # rotation axis _|_ the minor axis
    tx = (ecx - PP[0]) / f0 * tz
    ty = (ecy - PP[1]) / f0 * tz
    return [f0, tilt * ax, tilt * ay, 0.0, tx, ty, tz, 0.0]


# ------------------------------------------------------------------------ synthetic data

def synthesize(params_true, n_ring=240, n_wire_per=28, noise=0.4, seed=7):
    """Footage-shaped synthetic data: ring points on the four known circles, wire points
    on the twenty boundaries, the bull, gaussian pixel noise."""
    rng = random.Random(seed)
    cam = CameraModel(params_true)
    rings, wires = [], []
    for rho in RING_MM:
        for i in range(n_ring // 4):
            th = 2 * math.pi * (i + rng.random() * 0.5) / (n_ring // 4)
            u, v = cam.project(rho * math.cos(th), rho * math.sin(th))
            rings.append((u + rng.gauss(0, noise), v + rng.gauss(0, noise), rho))
    for k in range(20):
        ang = math.radians(9.0 + 18.0 * k)
        for j in range(n_wire_per):
            frac = 0.18 + (0.90 - 0.18) * j / (n_wire_per - 1)
            if 0.52 < frac < 0.64:
                continue
            r = 170.0 * frac
            u, v = cam.project(r * math.cos(ang), r * math.sin(ang))
            wires.append((u + rng.gauss(0, noise), v + rng.gauss(0, noise)))
    bu, bv = cam.project(0.0, 0.0)
    bull = (bu + rng.gauss(0, 2.0), bv + rng.gauss(0, 2.0))
    return {"rings": rings, "wires": wires, "bull": bull}


def rotvec_from_matrix(m):
    """Log map: rotation matrix -> rotation vector."""
    tr = m[0][0] + m[1][1] + m[2][2]
    c = max(-1.0, min(1.0, (tr - 1.0) / 2.0))
    th = math.acos(c)
    if th < 1e-9:
        return [0.0, 0.0, 0.0]
    s = 2.0 * math.sin(th)
    return [th * (m[2][1] - m[1][2]) / s, th * (m[0][2] - m[2][0]) / s,
            th * (m[1][0] - m[0][1]) / s]


def align_board_rotation(params, data):
    """The wire cost is 18-degree periodic in the board's own rotation, and a start
    exactly midway between two boundary hypotheses is a ridge where the gradient
    cancels -- Gauss-Newton then bends f and kappa instead of turning the board.
    A coarse scan over one period settles it before any derivative is taken."""
    best, bestc = params, None
    R = rot_matrix(params[1], params[2], params[3])
    for i in range(25):
        delta = math.radians(-9.0 + 18.0 * i / 24.0)
        c, s = math.cos(delta), math.sin(delta)
        rz = [[c, -s, 0.0], [s, c, 0.0], [0.0, 0.0, 1.0]]
        rr = [[sum(R[a][k] * rz[k][b] for k in range(3)) for b in range(3)] for a in range(3)]
        q = params[:]
        q[1], q[2], q[3] = rotvec_from_matrix(rr)
        cost = sum(e * e for e in residuals(q, data))
        if bestc is None or cost < bestc:
            best, bestc = q, cost
    return best


def fit_camera(data, init, fix_f=True):
    """The WHOLE-MODEL fit -- rings, wires, bull and kappa together: pose first at
    kappa=0 (both tilt signs, since a conic cannot tell which way a plane tips), then
    kappa freed. f stays at the caller's value by default, because the f question is
    answered by fit_f_from_rings instead: measured, #1560, with the wires in and f
    free, f runs to 1e11 on three of nine cameras.

    The CENSUS no longer reads its kappa from here -- ensemble_kappa does that from
    the wire bows alone, with no pose to get wrong. This path survives as the
    synthetic control's subject, which is its own justification: it is the most
    credulous estimator in the file, and a planted k1 has to come back out of it.
    Returns (p_before, p_after, sigma)."""
    pose = [1, 2, 3, 4, 5, 6] if fix_f else [0, 1, 2, 3, 4, 5, 6]
    best = None
    for sign in (1.0, -1.0):
        trial = init[:]
        trial[1] *= sign
        trial = align_board_rotation(trial, data)
        p, cost = gauss_newton(trial, data, free=pose)
        p = align_board_rotation(p, data)
        p, cost = gauss_newton(p, data, free=pose)
        if best is None or cost < best[1]:
            best = (p, cost)
    p_before = best[0]
    p_after, _ = gauss_newton(p_before, data, free=pose + [7])
    sig = param_sigma(p_after, data, pose + [7])
    return p_before, p_after, sig


F_GRID = (300, 350, 400, 450, 500, 550, 600, 650, 700, 800, 900, 1100, 1400, 2000)


def fit_f_from_rings(ring_points, init, noise_mm=None):
    """f, from the FOUR RINGS ALONE -- the census's answer to the maintainer's
    'report f alongside k1'.

    Why the rings alone. A plane target fixes f only through perspective
    foreshortening -- the near half of the board images at a bigger scale than the far
    half -- and four concentric circles of KNOWN millimetre radii carry that signal at
    four radii at once. The wires do not: twenty straight lines through one point are a
    projective object, so adding them to this fit buys nothing and costs everything
    (measured, #1560: with the wires in and f free, f runs to 1e11 on three of nine
    cameras -- the orthographic limit, where the wire term is flat in f and the ring
    term is outvoted 400 residuals to 288). And the census does not NEED f for the
    distortion question, because kappa is pixel-space.

    Returns (f, ring_rms_mm, tz_mm, profile, band) where profile is the chi-squared
    over F_GRID with the pose refitted at each pinned f, and band is the grid interval
    within 1 + 1/dof of the minimum -- or (None, ...) when the profile has no interior
    minimum on the grid, which is what an unresolved camera looks like and is reported
    as 'f not resolved' rather than as the number at the grid edge."""
    data = {"rings": ring_points, "wires": [], "bull": None}
    # A plane target's chi-squared surface in (f, tz) is a long shallow valley whose
    # floor is reached from a good tilt and missed from a bad one, so the start is
    # scanned rather than guessed: eight tilt azimuths at the magnitude the ellipse's
    # axis ratio implies, three focal lengths, cheap iterations -- then the winner is
    # refined properly from six focal lengths. Before this scan, one camera's profile
    # came back non-monotone (38.8, 38.1, 38.5, 40.1, 41.1 mm over a rising grid),
    # which is a measurement of where the optimiser started.
    tilt = math.hypot(init[1], init[2]) or 0.6
    coarse = None
    for a in range(8):
        az = math.pi * a / 4.0
        for f0 in (450.0, 700.0, 1100.0):
            q = init[:]
            q[1], q[2], q[3] = tilt * math.cos(az), tilt * math.sin(az), 0.0
            q[6] = init[6] * f0 / init[0]
            q[0] = f0
            q[7] = 0.0
            try:
                q, cost = gauss_newton(q, data, free=[0, 1, 2, 3, 4, 5, 6], iters=25)
            except (ValueError, ZeroDivisionError):
                continue
            if coarse is None or cost < coarse[1]:
                coarse = (q, cost)
    if coarse is None:
        return None, float("nan"), float("nan"), [], (None, None)
    best = None
    for f0 in (300.0, 424.0, 550.0, 700.0, 900.0, 1200.0):
        q = coarse[0][:]
        q[6] = coarse[0][6] * f0 / coarse[0][0]
        q[0] = f0
        q[7] = 0.0
        try:
            q, cost = gauss_newton(q, data, free=[0, 1, 2, 3, 4, 5, 6], iters=80)
        except (ValueError, ZeroDivisionError):
            continue
        if best is None or cost < best[1]:
            best = (q, cost)
    if best is None:
        return None, float("nan"), float("nan"), [], (None, None)
    prof = []
    for f in F_GRID:
        q = best[0][:]
        q[6] = best[0][6] * f / best[0][0]
        q[0] = float(f)
        try:
            q, cost = gauss_newton(q, data, free=[1, 2, 3, 4, 5, 6], iters=60)
        except (ValueError, ZeroDivisionError):
            continue
        prof.append((f, cost, rms(residuals(q, data, raw=True)), q[6]))
    if not prof:
        return None, float("nan"), float("nan"), [], (None, None)
    rmin = min(p[2] for p in prof)
    imin = [p[2] for p in prof].index(rmin)
    interior = 0 < imin < len(prof) - 1
    # THE INTERVAL IS A PROFILE WIDTH AND IS CALLED ONE. A chi-squared interval here
    # would read +/- one grid step on every camera, because the ring residuals are
    # systematic (bloom, a wire-crossing scallop, a band that is thin on the far side)
    # and not independent draws -- 288 correlated points make any minimum look
    # 80-sigma deep. So the interval reported is where the residual rises 20% above
    # its best, read off the parabola through the three grid points around the
    # minimum in log f, since f and standoff trade multiplicatively.
    band = (None, None)
    f_hat = None
    if interior:
        xs = [math.log(prof[i][0]) for i in (imin - 1, imin, imin + 1)]
        ys = [prof[i][2] for i in (imin - 1, imin, imin + 1)]
        d1 = (ys[2] - ys[0]) / (xs[2] - xs[0])
        d2 = 2.0 * ((ys[2] - ys[1]) / (xs[2] - xs[1]) - (ys[1] - ys[0]) / (xs[1] - xs[0])) \
            / (xs[2] - xs[0])
        if d2 > 0:
            x0 = xs[1] - d1 / d2
            ymin = ys[1] + 0.5 * d2 * (x0 - xs[1]) ** 2 - d1 * (x0 - xs[1])
            ymin = min(ymin, rmin)
            half = math.sqrt(max(0.0, 2.0 * 0.2 * ymin / d2))
            f_hat = math.exp(x0)
            band = (math.exp(x0 - half), math.exp(x0 + half))
            # a width wider than the grid is a flat profile's parabola, not an
            # interval: say nothing rather than print a number that means nothing
            if band[0] < F_GRID[0] or band[1] > F_GRID[-1]:
                band = (None, None)
    # f is REPORTED only when the footage, not the grid, turned the residual round:
    # both ends of the grid must sit above the 20% line and the interval must fall
    # inside it.
    turned = (f_hat is not None and band[0] is not None
              and prof[0][2] > 1.2 * rmin and prof[-1][2] > 1.2 * rmin)
    # AND THE MODEL HAS TO FIT. A confidence interval read off a model the data
    # refutes is not a measurement of anything. The floor is this camera's own
    # extraction scatter -- the conic residual of its four rings, in mm -- and a
    # pinhole that misses concentric circles by more than three times the scatter of
    # the points it was given is describing something else.
    fits = noise_mm is None or rmin <= 3.0 * noise_mm
    resolved = turned and fits
    return ((f_hat if resolved else None), rmin, prof[imin][3], prof, band)


def wire_grid_residual(ring_points, traces, init, f_px, microkappa):
    """#1467's quantity, from this instrument's extraction: the rms angular deviation
    of the wire points from the twenty-fold grid in the board plane, in DEGREES.

    The pose is refitted from scratch at the given kappa -- rings AND wires, f pinned
    at the measured value, kappa pinned at the given one -- so the AFTER is not handed
    the answer, and both tilt signs and a coarse scan over the board's own rotation
    are tried, because the wire cost is 18-degree periodic and a start midway between
    two boundary hypotheses is a ridge where the gradient cancels."""
    wire_points = [(x, y) for t in traces for x, y, _ in t]
    data = {"rings": ring_points, "wires": wire_points, "bull": None}
    base = init[:]
    base[6] = init[6] * f_px / init[0]
    base[0] = f_px
    base[7] = microkappa
    best = None
    for sign in (1.0, -1.0):
        q = base[:]
        q[1], q[2] = base[1] * sign, base[2] * sign
        q = align_board_rotation(q, data)
        q, cost = gauss_newton(q, data, free=[1, 2, 3, 4, 5, 6], iters=60)
        q = align_board_rotation(q, data)
        q, cost = gauss_newton(q, data, free=[1, 2, 3, 4, 5, 6], iters=60)
        if best is None or cost < best[1]:
            best = (q, cost)
    cam = CameraModel(best[0])
    per_trace, devs = [], []
    for trace in traces:
        here = []
        for x, y, _frac in trace:
            try:
                X, Y = cam.unproject_to_board(x, y)
            except ValueError:
                continue
            r = math.hypot(X, Y)
            if r < 30.0:
                continue
            d = math.degrees(math.atan2(Y, X)) % 18.0
            here.append((r, d if d <= 9.0 else d - 18.0))
        if here:
            per_trace.append(sum(d for _, d in here) / len(here))
            devs += here
    if not devs:
        return None
    mean = sum(d for _, d in devs) / len(devs)
    # TWO NUMBERS, because they answer two different questions and only one of them is
    # #1467's. PER WIRE -- the rms of the twenty traces' own mean offsets -- is the
    # "this wire sits at the wrong angle" quantity #1467 reports. PER POINT is larger
    # here for a reason that is not the lens: a trace is a straight line and a straight
    # line that misses the board's centre by a millimetre sweeps almost two degrees of
    # polar angle at r = 30 mm and 0.4 at 150, so the per-point figure is mostly the
    # board-centre offset the pose could not absorb. The BAND breakdown is the test
    # that matters either way: a lens's angular effect grows with radius, and a wire
    # that is simply in the wrong place does not care.
    out = []
    for lo, hi in ((30.0, 70.0), (70.0, 120.0), (120.0, 170.0)):
        band = [d - mean for r, d in devs if lo <= r < hi]
        out.append((lo, hi, len(band), rms(band) if band else float("nan")))
    pt_mean = sum(per_trace) / len(per_trace)
    return (rms([p - pt_mean for p in per_trace]), rms([d - mean for _, d in devs]), out)


def fmt(v):
    """A grid endpoint or a None, printed the same width either way."""
    return ("%.0f" % v) if v is not None else "-"


def k1_at(mk, f):
    return mk * 1e-6 * f * f


def run_synthetic():
    """Control + mutation, predictions first, for each of the three things this
    instrument claims to measure. i1560_lens_check.cpp asserts the same claims about
    the ensemble half against the math inlined in lens_census.hpp; this covers the
    two halves that cannot live in a header -- the full Gauss-Newton kappa fit and
    the rings-only f fit."""
    print("I1560SYN PREDICTIONS, stated before the runs:")
    print("I1560SYN   kappa control:  data distorted at k1=-0.20 (f=430) -> recovered "
          "k1(430) within +/-0.05")
    print("I1560SYN   kappa mutation: undistorted data (k1=0)           -> recovered "
          "|k1(430)| <= 0.05")
    print("I1560SYN   f control:      rings drawn at a KNOWN f -> fit_f_from_rings "
          "recovers it within 5%, at f=430 and at f=725")
    print("I1560SYN   f mutation:     the same rings with the perspective term removed "
          "(an orthographic board) -> f NOT RESOLVED, because a scale is not a focal length")
    base = [430.0, -0.85, 0.10, 0.35, 20.0, -35.0, 300.0, 0.0]
    fails = 0
    for name, k1true in (("kappa control", -0.20), ("kappa mutation", 0.0)):
        truth = base[:]
        truth[7] = k1true / (430.0 * 430.0) * 1e6  # microkappa
        data = synthesize(truth)
        init = truth[:]
        init[0] = F_GUESS
        init[7] = 0.0
        init[6] = 320.0  # do not start at the answer
        init[1] += 0.1
        _, p1, sig = fit_camera(data, init)
        k1_rec = k1_at(p1[7], 430.0)
        k1_sig = k1_at(sig.get(7, float("nan")), 430.0)
        ok = abs(k1_rec - k1true) <= 0.05
        fails += 0 if ok else 1
        # and the ENSEMBLE, on the same truth, through the wire bows alone
        cam = CameraModel(truth)
        pairs = []
        for k in range(20):
            ang = math.radians(9.0 + 18.0 * k)
            pts = [cam.project(170.0 * fr * math.cos(ang), 170.0 * fr * math.sin(ang))
                   for fr in [0.18 + 0.72 * j / 24.0 for j in range(25)]]
            b = line_fit_bow(pts)
            pairs.append((b[0], b[6]))
        ek, es, _g, _s = ensemble_kappa(pairs)
        ek1 = k1_at(ek * 1e6, 430.0)
        eok = abs(ek1 - k1true) <= 0.05
        fails += 0 if eok else 1
        print("I1560SYN %-14s: true k1=%+.2f -> full fit k1(430)=%+.4f +/- %.4f [%s]; "
              "ensemble k1(430)=%+.4f +/- %.4f [%s]"
              % (name, k1true, k1_rec, k1_sig, "OK" if ok else "FAIL",
                 ek1, k1_at(es * 1e6, 430.0), "OK" if eok else "FAIL"))
    for f_true in (430.0, 725.0):
        truth = base[:]
        truth[0] = f_true
        truth[6] = base[6] * f_true / base[0]
        data = synthesize(truth, noise=0.4)
        init = truth[:]
        init[0], init[6], init[1] = F_GUESS, 320.0, truth[1] + 0.1
        f_rec, f_rms, f_tz, _prof, band = fit_f_from_rings(data["rings"], init)
        ok = f_rec is not None and abs(f_rec - f_true) / f_true <= 0.05
        fails += 0 if ok else 1
        print("I1560SYN f control   : true f=%.0f -> recovered f=%s (rings rms %.2fmm, "
              "profile width [%s,%s])  [%s]"
              % (f_true, ("%.1f" % f_rec) if f_rec else "NOT RESOLVED", f_rms,
                 fmt(band[0]), fmt(band[1]), "OK" if ok else "FAIL"))
    # THE F MUTATION. Flatten the perspective: project every ring point with the depth
    # term frozen at the board centre's depth, so the image is an affine (orthographic)
    # picture of the board. An orthographic image has a scale and no focal length, so a
    # fit that still reported one would be reading its own prior.
    truth = base[:]
    truth[0], truth[6] = 725.0, base[6] * 725.0 / base[0]
    cam = CameraModel(truth)
    rng = random.Random(11)
    flat = []
    z0 = truth[6]
    for rho in RING_MM:
        for i in range(60):
            th = 2 * math.pi * i / 60
            X, Y = rho * math.cos(th), rho * math.sin(th)
            R, T = cam.R, (truth[4], truth[5], truth[6])
            px = R[0][0] * X + R[0][1] * Y + T[0]
            py = R[1][0] * X + R[1][1] * Y + T[1]
            flat.append((PP[0] + truth[0] * px / z0 + rng.gauss(0, 0.4),
                         PP[1] + truth[0] * py / z0 + rng.gauss(0, 0.4), rho))
    init = truth[:]
    init[0], init[6], init[1] = F_GUESS, 320.0, truth[1] + 0.1
    f_rec, f_rms, _tz, _prof, band = fit_f_from_rings(flat, init)
    ok = f_rec is None
    fails += 0 if ok else 1
    print("I1560SYN f mutation  : orthographic rings -> %s (rings rms %.2fmm, "
          "profile width [%s,%s])  [%s]"
          % (("f=%.1f" % f_rec) if f_rec else "NOT RESOLVED", f_rms, fmt(band[0]), fmt(band[1]),
             "OK" if ok else "FAIL"))
    print("I1560SYN %s: %d failure(s)" % ("FAIL" if fails else "PASS", fails))
    return fails


# ------------------------------------------------------------------------------- driver

def median_prefilter(points, seed, window=5, cut=2.5):
    """Reject ring-edge points whose radius from the seed deviates more than `cut` px
    from the local median -- wire-crossing scallops, glyph clips, dart shadows."""
    if len(points) < 2 * window + 1:
        return points
    polar = sorted(
        (math.atan2(y - seed[1], x - seed[0]), math.hypot(x - seed[0], y - seed[1]), (x, y))
        for x, y in points)
    n = len(polar)
    kept = []
    for i in range(n):
        neigh = sorted(polar[(i + j) % n][1] for j in range(-window, window + 1))
        med = neigh[window]
        if abs(polar[i][1] - med) <= cut:
            kept.append(polar[i][2])
    return kept


def measure(tag, cam_no, frames_dir, dump_json):
    path = os.path.join(frames_dir, "%s-cam%d.ppm" % (tag, cam_no))
    if not os.path.exists(path):
        return None
    frame = Frame(path)
    seed = SEEDS[(tag, cam_no)]
    edges = extract_ring_edges(frame, seed)
    if len(edges["d_out"]) < 100:
        print("I1560 %s cam%d: only %d doubles-edge points -- not measurable" %
              (tag, cam_no, len(edges["d_out"])))
        return None
    kept_dout, dout_fit, _ = trim_conic(median_prefilter(edges["d_out"], seed))
    conic_report = {}
    ring_points = []
    for name, rho in (("t_in", 99.0), ("t_out", 107.0), ("d_in", 162.0), ("d_out", 170.0)):
        kept, fit, res = trim_conic(median_prefilter(edges[name], seed))
        # the 20-fold component of the conic residual, which a lens cannot draw
        # (radial distortion of a smooth curve is smooth in angle): paint/bloom
        # varying with the underlying bed, so it is attributed, not just excluded
        amp = 0.0
        if kept:
            e0 = ellipse_radius(fit, 0.0)
            ccx, ccy = e0[2] if e0 else seed
            cr = ci = 0.0
            for (x, y), e in zip(kept, res):
                th = math.atan2(y - ccy, x - ccx)
                cr += e * math.cos(20 * th)
                ci += e * math.sin(20 * th)
            amp = 2.0 * math.hypot(cr, ci) / len(kept)
        conic_report[name] = (len(kept), rms(res), amp)
        # the MODEL sees the fitted conic, resampled -- the 20-fold paint systematic
        # averages out of the conic parameters, and feeding the raw points instead
        # lets a few px of periodic bloom masquerade as perspective or distortion
        if len(kept) >= 40:
            e0 = ellipse_radius(fit, 0.0)
            if e0 is not None:
                ccx, ccy = e0[2]
                for i in range(72):
                    th = 2 * math.pi * i / 72
                    r = ray_conic_crossing(fit, (ccx, ccy), th)
                    if r:
                        ring_points.append((ccx + r * math.cos(th),
                                            ccy + r * math.sin(th), rho))
    traces = wire_traces(frame, dout_fit, seed)
    wire_points = [(x, y) for t in traces for x, y, _ in t]
    bows = [line_fit_bow(t) for t in traces]

    # board position in frame (the trap's first half)
    e0 = ellipse_radius(dout_fit, 0.0)
    _, _, (ecx, ecy) = e0
    off = math.hypot(ecx - PP[0], ecy - PP[1])

    init = initial_pose(kept_dout, (ecx, ecy))

    # this camera's own extraction scatter, in mm on the board: the median conic
    # residual of its four rings over the doubles ellipse's mean px-per-mm. It is the
    # floor any fit to these points has to reach before its parameters mean anything.
    dout_rr = [e[0] for e in (ellipse_radius(dout_fit, math.pi * i / 90.0)
                              for i in range(90)) if e]
    px_per_mm = (sum(dout_rr) / len(dout_rr) / 170.0) if dout_rr else 2.0
    ring_noise_mm = (sorted(v[1] for v in conic_report.values())[2] / px_per_mm)

    # f, FROM THE RINGS ALONE, with the profile that says how well (see fit_f_from_rings)
    f_fit, f_rms, f_tz, f_prof, f_band = fit_f_from_rings(ring_points, init, ring_noise_mm)
    f_used = f_fit if f_fit else F_GUESS

    # THE ENSEMBLE KAPPA (assumption-light): the measured wire bows against the bow
    # each wire's own chord geometry takes per unit kappa about the principal point.
    # No pose, no f, no board model -- so it cannot launder a pose error into kappa,
    # and its sigma is the scatter of twenty independent wires rather than a noise
    # model somebody chose. lens_census.hpp inlines exactly this as fitKappaFromBows.
    ens_kappa, ens_sigma, ens_gain, ens_model = ensemble_kappa([(b[0], b[6]) for b in bows])
    ens_k1 = k1_at(ens_kappa * 1e6, F_GUESS)
    ens_k1_sig = k1_at(ens_sigma * 1e6, F_GUESS)

    # BEFORE and AFTER, on the quantity the census is about: the twenty wire bows.
    # Before is what the footage holds; after is what is left once this camera's one
    # fitted kappa has been taken out of every wire. A systematic residual collapses
    # here; a random one does not move, and the ratio is the fraction of #1467's wire
    # residual that ONE LENS CONSTANT can account for.
    bow_before = [b[0] for b in bows]
    bow_after = [b[0] - m0 for b, m0 in zip(bows, ens_model)]
    bow_rms_before, bow_rms_after = rms(bow_before), rms(bow_after)

    # ...AND THE SAME BEFORE/AFTER IN #1467'S OWN UNITS, where the rings resolved a
    # focal length to carry a board pose. #1467 reports the wire residual in DEGREES
    # about the twenty-fold grid; this reads the same quantity off this instrument's
    # own extraction, with the board rotation and pose refitted from scratch at each
    # kappa so the comparison is not handed the answer. It is only printed for a camera
    # whose f resolved, because an angle in the board plane is a question about the
    # homography and a homography built on a refuted f is not one.
    grid_before = grid_after = None
    if f_fit:
        grid_before = wire_grid_residual(ring_points, traces, init, f_fit, 0.0)
        grid_after = wire_grid_residual(ring_points, traces, init, f_fit,
                                        ens_kappa * 1e6)

    # SENSITIVITY (the trap's second half), from the measured chords and the principal
    # point alone -- no pose, because a camera whose pose fit diverged still deserves
    # an honest answer about what its geometry could have seen.
    unit = bow_per_unit_kappa([b[6] for b in bows])
    maxbow = max([abs(g) * 0.20 / (f_used * f_used) for g in unit] or [0.0])
    noise_px = sorted(b[1] for b in bows)[len(bows) // 2] if bows else 0.5
    # a bow is measured per wire to ~rms/sqrt(n); the ensemble of 20 wires tightens it
    bow_floor = max(0.05, noise_px / math.sqrt(max(1, len(bows))))
    # The honest sensitivity: the smallest |k1(424)| this camera's geometry AND this
    # extraction's scatter could tell from zero at 2 sigma. A centred board sends
    # ens_gain to zero and this to infinity, which is the trap answering by itself.
    # The bar is 0.20 because that is the middle of #1513's swept range and the
    # smallest k1 in it that would cost a treble bed under #1488's board-space
    # scoring -- a camera that cannot separate THAT from zero has not answered.
    detect_k1 = 2.0 * ens_k1_sig
    informative = (maxbow > 2.0 * bow_floor and math.isfinite(detect_k1)
                   and detect_k1 < 0.20)
    window = "3s" if tag.endswith("w3") else "opening"

    print("I1560 %s cam%d window=%s board_centre=(%.0f,%.0f) off_pp=%.0fpx bull=(%d,%d)" %
          (tag, cam_no, window, ecx, ecy, off, seed[0], seed[1]))
    for name, rho in (("t_in", 99.0), ("t_out", 107.0), ("d_in", 162.0), ("d_out", 170.0)):
        n, r, amp = conic_report[name]
        print("I1560CONIC %s cam%d %s mm=%.0f n=%d rms=%.3fpx twentyfold=%.3fpx"
              % (tag, cam_no, name, rho, n, r, amp))
    for i, b in enumerate(bows):
        print("I1560WIRE %s cam%d wire=%02d n=%d span=%.0fpx bow=%+.2fpx rms=%.2fpx chord_off_pp=%.0fpx"
              % (tag, cam_no, i, b[4], b[5], b[0], b[1], b[3]))
    print("I1560ENS %s cam%d kappa=%+.4fe-6/px^2 k1(424)=%+.4f+/-%.4f gain=%.3g n=%d "
          "bow_rms BEFORE=%.3fpx AFTER=%.3fpx (%.0f%% of the wire residual is this one kappa)"
          % (tag, cam_no, ens_kappa * 1e6, ens_k1, ens_k1_sig, ens_gain, len(bows),
             bow_rms_before, bow_rms_after,
             100.0 * (1.0 - (bow_rms_after / bow_rms_before if bow_rms_before else 1.0))))
    if grid_before is not None:
        print("I1560GRID %s cam%d wire residual about the 18-degree grid (#1467's own "
              "quantity, this extraction): per wire BEFORE=%.2fdeg AFTER=%.2fdeg, "
              "per point BEFORE=%.2fdeg AFTER=%.2fdeg; per point by board radius "
              "BEFORE %s AFTER %s"
              % (tag, cam_no, grid_before[0], grid_after[0], grid_before[1], grid_after[1],
                 " ".join("%.0f-%.0fmm:%.2f(n=%d)" % (lo, hi, v, n)
                          for lo, hi, n, v in grid_before[2]),
                 " ".join("%.0f-%.0fmm:%.2f" % (lo, hi, v)
                          for lo, hi, _n, v in grid_after[2])))
    if f_fit:
        print("I1560F %s cam%d f=%.0fpx ring_rms=%.2fmm (scatter %.2fmm) standoff=%.0fmm "
              "profile_width=[%s,%s] fov_diag=%.0fdeg (k1 at THIS f = %+.4f+/-%.4f)"
              % (tag, cam_no, f_fit, f_rms, ring_noise_mm, f_tz, fmt(f_band[0]), fmt(f_band[1]),
                 2.0 * math.degrees(math.atan(math.hypot(W, H) / 2.0 / f_fit)),
                 k1_at(ens_kappa * 1e6, f_fit), k1_at(ens_sigma * 1e6, f_fit)))
    else:
        print("I1560F %s cam%d f NOT RESOLVED -- best ring_rms=%.2fmm against this "
              "camera's own %.2fmm extraction scatter, profile width [%s,%s] on a "
              "grid of [%d,%d]; these rings do not measure a focal length"
              % (tag, cam_no, f_rms, ring_noise_mm, fmt(f_band[0]), fmt(f_band[1]),
                 F_GRID[0], F_GRID[-1]))
    print("I1560FPROF %s cam%d " % (tag, cam_no) +
          " ".join("%d:%.2fmm" % (f, rr) for f, _c, rr, _tz in f_prof))
    print("I1560TRAP %s cam%d predicted_max_bow_at_k1(f=%.0f)=-0.20: %.2fpx vs bow-floor %.2fpx; "
          "smallest |k1(424)| separable from 0 at 2sigma = %.3f -> %s"
          % (tag, cam_no, f_used, maxbow, bow_floor, detect_k1,
             "informative" if informative else "CANNOT TELL FROM THIS FOOTAGE"))
    if dump_json:
        with open(dump_json, "w") as f:
            json.dump({"rings": ring_points, "wires": wire_points, "bull": seed,
                       "f_profile": f_prof, "kappa": ens_kappa}, f)
    return {"tag": tag, "cam": cam_no, "off": off, "f": f_fit, "f_rms": f_rms,
            "f_band": f_band, "f_tz": f_tz, "kappa": ens_kappa,
            "bow_rms_before": bow_rms_before, "bow_rms_after": bow_rms_after,
            "bows": [(b[0], b[3]) for b in bows], "informative": informative,
            "window": window, "maxbow": maxbow,
            "ens_k1": ens_k1, "ens_k1_sig": ens_k1_sig, "ens_gain": ens_gain,
            "detect_k1": detect_k1, "conic": conic_report}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", help="directory of <tag>-cam<n>.ppm 30-frame averages")
    ap.add_argument("--dump-cmds", action="store_true")
    ap.add_argument("--mocks", default="mocks")
    ap.add_argument("--out", default=".")
    ap.add_argument("--only", help="tag:cam, e.g. r18w3:1")
    ap.add_argument("--json-dir", help="dump extracted points per camera as JSON")
    ap.add_argument("--synthetic", action="store_true")
    args = ap.parse_args()
    if args.dump_cmds:
        for line in dump_commands(args.mocks, args.out):
            print(line)
        return
    if args.synthetic:
        # the harness reads the exit code and nothing else (#1335), so the control
        # and the mutation have to end the process rather than just print
        sys.exit(1 if run_synthetic() else 0)
    if not args.frames:
        ap.error("--frames, --dump-cmds or --synthetic")
    results = []
    for (tag, cam_no) in sorted(SEEDS):
        if args.only and args.only != "%s:%d" % (tag, cam_no):
            continue
        js = (os.path.join(args.json_dir, "%s-cam%d.json" % (tag, cam_no))
              if args.json_dir else None)
        r = measure(tag, cam_no, args.frames, js)
        if r:
            results.append(r)
    print("I1560 done: %d cameras measured" % len(results))


if __name__ == "__main__":
    main()
