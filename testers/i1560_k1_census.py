#!/usr/bin/env python3
# #1560: is #1467's wire residual one lens constant? k1 (and f) per camera, from footage.
#
# unrun-tester: an instrument, not a check -- i1499_band_census's kind, host-side. It
# measures, per camera per fixture, every wedge-boundary wire's bow off its chord and the
# ring conic residuals, then fits one pinhole + Brown-Conrady k1 model per camera and
# prints the residual before and after. Nothing in it can fail on a wrong number; the
# CHECK half (synthetic control + mutation, predictions stated first) is
# i1560_lens_check.cpp through unit_check.sh, against the same math inlined in
# lens_census.hpp. The verdict this instrument produced is recorded there and in the
# issue thread; this file is kept as the way to re-take the measurement.
#
# It runs on the HOST, deliberately: Docker is a shared single resource on this rig
# (#1552 held it for the life of #1560's branch), the fit is pure math, and the only
# footage step is a 30-frame average that ffmpeg does exactly as the detector's
# averageOf does (camera.cpp seeks frame round(fps*(3.0 - 0.18*camIdx)) on registry
# builds, #1551, then averages 30 frames). No OpenCV, no numpy: stdlib only.
#
#   python3 testers/i1560_k1_census.py --frames <dir>       # measure dumped frames
#   python3 testers/i1560_k1_census.py --dump-cmds          # print the ffmpeg lines
#   python3 testers/i1560_k1_census.py --synthetic          # control + mutation only
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
# between the rings (subpixel gradient peak). Fit: project board-plane features through
# f (principal point pinned at 640,360, square pixels -- OV9732, docs/rig.md), rotation,
# translation, k1; Gauss-Newton on point-to-feature distances in board mm; the BEFORE
# fit freezes k1=0 (today's flat projective model), the AFTER fit frees it. The trap
# (#1513, binding): radial distortion cannot bend a line through the distortion centre,
# so each camera also reports where its board sits relative to (640,360) and the bow a
# k1 of -0.20 WOULD produce at its fitted pose; a camera whose predicted signal is
# within noise answers "cannot tell from this footage", never "k1 = 0".

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


def extract_ring_edges(frame, seed):
    """Per angle: doubles band (in, out) and treble band (in, out) radii, subpixel.

    Two passes. The first picks the outermost coloured run per ray and fits a rough
    conic to the radius-median inliers -- the Winmau badges on the number ring are RED
    and sit OUTSIDE the doubles band, so 'outermost' alone is poisoned at those angles.
    The second re-picks per ray the run nearest the rough conic's own crossing, which
    refuses the badges, the bull, and a parked dart's flight by position.

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
        outer = [r for r in runs if r[2] > 100.0]
        if outer:
            first.append((theta, outer[-1][2]))
    if len(first) < 60:
        return edges
    med = sorted(r for _, r in first)[len(first) // 2]
    rough_pts = [(cx + r * math.cos(th), cy + r * math.sin(th))
                 for th, r in first if 0.85 * med <= r <= 1.15 * med]
    rough = fit_conic(rough_pts)
    for _ in range(2):
        res = conic_residuals(rough_pts, rough)
        s = max(rms(res), 1.0)
        rough_pts = [p for p, e in zip(rough_pts, res) if abs(e) <= 3.0 * s]
        rough = fit_conic(rough_pts)
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


def line_fit_bow(trace):
    """Total-least-squares line; returns (bow_px, rms_px, quad_px, dist_from_pp, n)."""
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
    devs = [ (x - mx) * nx + (y - my) * ny for x, y in zip(xs, ys)]
    ts = [ (x - mx) * ux + (y - my) * uy for x, y in zip(xs, ys)]
    # robust: drop the worst 10% once (dart shafts, glyph clips)
    order = sorted(range(n), key=lambda i: abs(devs[i]))
    keep = order[: max(8, int(n * 0.9))]
    ts = [ts[i] for i in keep]
    devs = [devs[i] for i in keep]
    xs = [xs[i] for i in keep]
    ys = [ys[i] for i in keep]
    n = len(keep)
    mx2, my2 = sum(xs) / n, sum(ys) / n
    # quadratic dev(t) = c0 + c1 t + c2 t^2 -- c2 is the bend
    s0, s1, s2, s3, s4 = n, sum(ts), sum(t * t for t in ts), sum(t ** 3 for t in ts), sum(t ** 4 for t in ts)
    b0, b1, b2 = sum(devs), sum(d * t for d, t in zip(devs, ts)), sum(d * t * t for d, t in zip(devs, ts))
    c0, c1, c2 = solve([[s0, s1, s2], [s1, s2, s3], [s2, s3, s4]], [b0, b1, b2])
    span = (max(ts) - min(ts))
    bow = c2 * span * span / 8.0   # sagitta of the fitted parabola over the span
    resid = [d - (c0 + c1 * t + c2 * t * t) for d, t in zip(devs, ts)]
    # distance of the (straight) chord from the principal point
    d_pp = abs((PP[0] - mx2) * nx + (PP[1] - my2) * ny)
    return bow, rms(resid), c2, d_pp, n, span


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
SIGMA_RING_MM = 1.0
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
    """Coarse init: bull-ish centre, scale from the doubles conic, tilt from its axis
    ratio, board assumed below-viewed (camera looks up: +Y tilt)."""
    fit = fit_conic(dout_points)
    e0 = ellipse_radius(fit, 0.0)
    if e0 is None:
        return None
    # sample conic radii to get major/minor
    rr = []
    for i in range(72):
        e = ellipse_radius(fit, 2 * math.pi * i / 72)
        if e:
            rr.append(e[0])
    rmax, rmin = max(rr), min(rr)
    _, _, (ecx, ecy) = e0
    f0 = F_GUESS
    tz = f0 * 170.0 / rmax
    tilt = math.acos(max(0.2, min(1.0, rmin / rmax)))
    # direction of minor axis == direction of tilt; assume tilt about the horizontal
    tx = (ecx - PP[0]) / f0 * tz
    ty = (ecy - PP[1]) / f0 * tz
    return [f0, -tilt, 0.0, 0.0, tx, ty, tz, 0.0]


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


def fit_camera(data, init):
    """The census fit: pose first at kappa=0 (both tilt signs -- a conic cannot tell
    which way a plane tips), then kappa freed. Returns (p_before, p_after, sigma)."""
    best = None
    for sign in (1.0, -1.0):
        trial = init[:]
        trial[1] *= sign
        trial = align_board_rotation(trial, data)
        p, cost = gauss_newton(trial, data, free=[0, 1, 2, 3, 4, 5, 6])
        p = align_board_rotation(p, data)
        p, cost = gauss_newton(p, data, free=[0, 1, 2, 3, 4, 5, 6])
        if best is None or cost < best[1]:
            best = (p, cost)
    p_before = best[0]
    p_after, _ = gauss_newton(p_before, data, free=[0, 1, 2, 3, 4, 5, 6, 7])
    sig = param_sigma(p_after, data, [0, 1, 2, 3, 4, 5, 6, 7])
    return p_before, p_after, sig


def profile_f(data, p_after, grid=(340, 380, 424, 470, 520, 570, 620, 700, 800, 1000)):
    """Chi-squared profile over f with everything else (kappa included) refitted:
    the honest answer to 'did the footage measure f, and how well'."""
    prof = []
    for f in grid:
        p = p_after[:]
        p[0] = float(f)
        p, cost = gauss_newton(p, data, free=[1, 2, 3, 4, 5, 6, 7], iters=25)
        prof.append((f, cost, p[7]))
    return prof


def k1_at(mk, f):
    return mk * 1e-6 * f * f


def run_synthetic():
    """Control + mutation, predictions first (the same claims i1560_lens_check.cpp
    asserts against the math inlined in lens_census.hpp)."""
    print("I1560SYN PREDICTIONS, stated before the runs:")
    print("I1560SYN   control: data distorted at k1=-0.20 (f=430) -> recovered k1(430) within +/-0.05")
    print("I1560SYN   mutation: undistorted data (k1=0) -> recovered |k1(430)| <= 0.05")
    base = [430.0, -0.85, 0.10, 0.35, 20.0, -35.0, 300.0, 0.0]
    for name, k1true in (("control", -0.20), ("mutation", 0.0)):
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
        print("I1560SYN %s: true k1=%.2f -> recovered k1(430)=%.4f +/- %.4f, f=%.1f +/- %.1f  [%s]"
              % (name, k1true, k1_rec, k1_sig, p1[0], sig.get(0, float("nan")),
                 "OK" if ok else "FAIL"))


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
        conic_report[name] = (len(kept), rms(res))
        ring_points += [(x, y, rho) for x, y in kept]
    traces = wire_traces(frame, dout_fit, seed)
    wire_points = [(x, y) for t in traces for x, y, _ in t]
    bows = [line_fit_bow(t) for t in traces]
    data = {"rings": ring_points, "wires": wire_points, "bull": seed}

    # board position in frame (the trap's first half)
    e0 = ellipse_radius(dout_fit, 0.0)
    _, _, (ecx, ecy) = e0
    off = math.hypot(ecx - PP[0], ecy - PP[1])

    init = initial_pose(kept_dout, (ecx, ecy))
    p_before, p_after, sig = fit_camera(data, init)
    rb = residuals(p_before, data, raw=True)
    ra = residuals(p_after, data, raw=True)
    nr = len(ring_points)
    nw = len(wire_points)
    scale = CameraModel(p_after).local_scale(120.0, 0.0)
    prof = profile_f(data, p_after)

    # sensitivity (the trap's second half): the bow k1=-0.20 (at nominal f=424)
    # would draw at this camera's fitted pose and board position
    probe = p_after[:]
    probe[7] = -0.20 / (F_GUESS * F_GUESS) * 1e6
    camp = CameraModel(probe)
    camu = CameraModel(p_after[:7] + [0.0])
    maxbow = 0.0
    for k in range(20):
        ang = math.radians(9.0 + 18.0 * k)
        pts = []
        for j in range(25):
            frac = 0.18 + (0.90 - 0.18) * j / 24.0
            r = 170.0 * frac
            pts.append(camp.project(r * math.cos(ang), r * math.sin(ang)) + (frac,))
        bw = abs(line_fit_bow(pts)[0])
        maxbow = max(maxbow, bw)
    noise_px = sorted(b[1] for b in bows)[len(bows) // 2] if bows else 0.5
    # a bow is measured per wire to ~rms/sqrt(n); the ensemble of 20 wires tightens it
    bow_floor = max(0.05, noise_px / math.sqrt(max(1, len(bows))))
    informative = maxbow > 2.0 * bow_floor
    window = "3s" if tag.endswith("w3") else "opening"

    print("I1560 %s cam%d window=%s board_centre=(%.0f,%.0f) off_pp=%.0fpx bull=(%d,%d)" %
          (tag, cam_no, window, ecx, ecy, off, seed[0], seed[1]))
    for name, rho in (("t_in", 99.0), ("t_out", 107.0), ("d_in", 162.0), ("d_out", 170.0)):
        n, r = conic_report[name]
        print("I1560CONIC %s cam%d %s mm=%.0f n=%d rms=%.3fpx" % (tag, cam_no, name, rho, n, r))
    for i, b in enumerate(bows):
        print("I1560WIRE %s cam%d wire=%02d n=%d span=%.0fpx bow=%+.2fpx rms=%.2fpx chord_off_pp=%.0fpx"
              % (tag, cam_no, i, b[4], b[5], b[0], b[1], b[3]))
    k1_424 = k1_at(p_after[7], F_GUESS)
    k1_424_sig = k1_at(sig.get(7, float("nan")), F_GUESS)
    print("I1560FIT %s cam%d BEFORE(kappa=0): f=%.1f rms_ring=%.3fmm rms_wire=%.3fmm" %
          (tag, cam_no, p_before[0], rms(rb[:nr]), rms(rb[nr:nr + nw])))
    print("I1560FIT %s cam%d AFTER: f=%.1f kappa=%.4fe-6/px^2 k1(f=424)=%.4f+/-%.4f "
          "rms_ring=%.3fmm rms_wire=%.3fmm (%.2fpx/mm)"
          % (tag, cam_no, p_after[0], p_after[7], k1_424, k1_424_sig,
             rms(ra[:nr]), rms(ra[nr:nr + nw]), scale))
    best_f = min(prof, key=lambda e: e[1])
    print("I1560FPROF %s cam%d " % (tag, cam_no) +
          " ".join("f=%d:chi2=%.0f" % (f, c) for f, c, _ in prof) +
          "  min_at_f=%d" % best_f[0])
    print("I1560TRAP %s cam%d predicted_max_bow_at_k1(424)=-0.20: %.2fpx vs bow-floor %.2fpx -> %s"
          % (tag, cam_no, maxbow, bow_floor,
             "informative" if informative else "CANNOT TELL FROM THIS FOOTAGE"))
    if dump_json:
        with open(dump_json, "w") as f:
            json.dump({"rings": ring_points, "wires": wire_points, "bull": seed,
                       "before": p_before, "after": p_after, "profile_f": prof}, f)
    return {"tag": tag, "cam": cam_no, "off": off, "f": p_after[0], "k1_424": k1_424,
            "sig_k1_424": k1_424_sig, "kappa": p_after[7],
            "rms_wire_before": rms(rb[nr:nr + nw]), "rms_wire_after": rms(ra[nr:nr + nw]),
            "rms_ring_before": rms(rb[:nr]), "rms_ring_after": rms(ra[:nr]),
            "bows": [(b[0], b[3]) for b in bows], "informative": informative,
            "window": window, "profile_f": prof}


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
        run_synthetic()
        return
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
