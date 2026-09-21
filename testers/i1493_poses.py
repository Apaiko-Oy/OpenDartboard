# #1493: compose the three board planes into camera poses, and report the ray-intersection
# residual, in millimetres, per dart.
#
#   python3 i1493_poses.py <rows.txt>
#
# It asserts NO THRESHOLD -- ADR-0084 s4 defers that deliberately and a constant chosen
# from one fixture is #1322's mistake -- and it concludes NO ARCHITECTURE: #1488 is the
# decision and it is the maintainer's. Its exit status is about the INSTRUMENT only: a
# census that intersected nothing has measured nothing.
#
# ------------------------------------------------------------------------------------
# THE PREMISE THIS FILE HAD TO CORRECT, STATED BEFORE ANY NUMBER
# ------------------------------------------------------------------------------------
# #1493 says the extrinsics are free because "a plane per camera IS a pose relative to the
# board". Half of that is true and the half that is not is the half a ray needs.
#
# wire_model::planeOf() returns a HOMOGRAPHY H: board plane -> image. Under a pinhole
# camera H is proportional to K [r1 r2 t] -- the pose is in there, multiplied by the
# intrinsics. #1467 built the plane so that NO intrinsics would be needed, and its header
# says so in those words: "no solvePnP here, no intrinsics, no focal length and no two-fold
# pose ambiguity". That is exactly why UNPROJECTION is free: mapping a pixel onto the board
# is H^-1 and nothing else, which is what #1490 measured.
#
# A RAY NEEDS A CAMERA CENTRE, and a camera centre is not in H. Recovering one means
# supplying or assuming K. This file assumes the smallest K there is -- square pixels, no
# skew, the principal point at the frame centre, no lens distortion -- which leaves one
# unknown, the focal length f, and a single plane homography over-determines it: the two
# orthonormality constraints on [r1 r2] each give an estimate. So the poses below are
# RECOVERED UNDER AN ASSUMPTION, not read off the calibration, and both estimates of f are
# printed side by side because their disagreement is the direct measure of how well a
# pinhole model fits this homography at all. The rig's cameras are 100-degree webcams
# (README, hardware reference), where the no-distortion half of that assumption is the one
# to doubt.
#
# WHAT IS FREE OF f, AND WHY IT BOUNDS EVERYTHING BELOW. A ray through pixel x from the
# camera centre meets the board plane at H^-1 x, whatever f is -- the camera centre moves
# along the ray as f changes and the ray does not. So two cameras' rays meet the plane at
# exactly the two points #1490 already measured the spread between, and the ray-ray
# residual can only ever be SMALLER than that in-plane separation. The residual is
# therefore not an independent measurement of the same thing: it is the answer to a
# narrower question -- can the two cameras' disagreement be explained by ONE 3D point that
# simply is not on the board plane? The height of the meeting point above the plane is
# printed beside every residual for that reason.
import math
import sys

MM = 170.0  # |q| = 1 in board units is the outer edge of the double ring


# ---- the smallest linear algebra that will do, so this file needs no numpy ------------
def matvec(M, v):
    return [sum(M[r][c] * v[c] for c in range(3)) for r in range(3)]


def transpose(M):
    return [[M[c][r] for c in range(3)] for r in range(3)]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def norm(a):
    return math.sqrt(dot(a, a))


def scale(a, s):
    return [x * s for x in a]


def sub(a, b):
    return [x - y for x, y in zip(a, b)]


def add(a, b):
    return [x + y for x, y in zip(a, b)]


def unit(a):
    n = norm(a)
    return scale(a, 1.0 / n) if n > 0 else a


def inverse3(M):
    d = (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1])
         - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
         + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]))
    if abs(d) < 1e-18:
        return None
    c = [[0.0] * 3 for _ in range(3)]
    c[0][0] = (M[1][1] * M[2][2] - M[1][2] * M[2][1]) / d
    c[0][1] = (M[0][2] * M[2][1] - M[0][1] * M[2][2]) / d
    c[0][2] = (M[0][1] * M[1][2] - M[0][2] * M[1][1]) / d
    c[1][0] = (M[1][2] * M[2][0] - M[1][0] * M[2][2]) / d
    c[1][1] = (M[0][0] * M[2][2] - M[0][2] * M[2][0]) / d
    c[1][2] = (M[0][2] * M[1][0] - M[0][0] * M[1][2]) / d
    c[2][0] = (M[1][0] * M[2][1] - M[1][1] * M[2][0]) / d
    c[2][1] = (M[0][1] * M[2][0] - M[0][0] * M[2][1]) / d
    c[2][2] = (M[0][0] * M[1][1] - M[0][1] * M[1][0]) / d
    return c


def median(v):
    s = sorted(v)
    n = len(s)
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


def stat(v, unit_name="mm"):
    if not v:
        return "no measurements"
    s = sorted(v)
    return "n=%-3d min %7.1f   median %7.1f   max %7.1f %s" % (
        len(s), s[0], median(s), s[-1], unit_name)


# ---- the pose, recovered from one plane homography under an assumed K -----------------
class Pose:
    """H = K [r1 r2 t] up to scale, with K = diag-ish(f, f, 1) about the frame centre."""

    def __init__(self, H, cx, cy):
        self.H, self.cx, self.cy = H, cx, cy
        self.cols = [[H[0][j], H[1][j], H[2][j]] for j in range(3)]
        self.f_ortho, self.f_equal, self.f_ls = self._focal_estimates()

    def _ab(self, h):
        return h[0] - self.cx * h[2], h[1] - self.cy * h[2], h[2]

    def _focal_estimates(self):
        """s = 1/f^2 from each of the two constraints on [r1 r2], and from both at once.

        r1.r2 = 0  ->  (a1 a2 + b1 b2) s + w1 w2 = 0
        |r1|=|r2|  ->  ((a1^2+b1^2) - (a2^2+b2^2)) s + (w1^2 - w2^2) = 0

        Both are homogeneous in H's arbitrary scale, so neither depends on it.
        """
        a1, b1, w1 = self._ab(self.cols[0])
        a2, b2, w2 = self._ab(self.cols[1])
        p1, q1 = a1 * a2 + b1 * b2, w1 * w2
        p2, q2 = (a1 * a1 + b1 * b1) - (a2 * a2 + b2 * b2), w1 * w1 - w2 * w2

        def f_of(s):
            return (1.0 / math.sqrt(s)) if (s is not None and s > 0) else None

        s_o = -q1 / p1 if abs(p1) > 1e-18 else None
        s_e = -q2 / p2 if abs(p2) > 1e-18 else None
        den = p1 * p1 + p2 * p2
        s_l = -(p1 * q1 + p2 * q2) / den if den > 1e-30 else None
        return f_of(s_o), f_of(s_e), f_of(s_l)

    def at(self, f):
        """R, t, C at a given focal length. Returns None if the columns degenerate."""
        Kinv = [[1.0 / f, 0.0, -self.cx / f], [0.0, 1.0 / f, -self.cy / f], [0.0, 0.0, 1.0]]
        c1, c2, c3 = (matvec(Kinv, self.cols[j]) for j in range(3))
        n1, n2 = norm(c1), norm(c2)
        if n1 < 1e-15 or n2 < 1e-15:
            return None
        lam = 2.0 / (n1 + n2)                      # |r1| = |r2| = 1, both asked
        if c3[2] < 0:                              # the board is in front of the camera
            lam = -lam
        r1, r2, t = scale(c1, lam), scale(c2, lam), scale(c3, lam)
        # what the assumption cost, before anything is repaired: these would be 1, 1, 0
        defects = (n1 * abs(lam), n2 * abs(lam), dot(unit(r1), unit(r2)))
        e1 = unit(r1)
        e2 = unit(sub(r2, scale(e1, dot(e1, r2))))  # Gram-Schmidt: the smallest repair
        e3 = cross(e1, e2)
        R = [[e1[0], e2[0], e3[0]], [e1[1], e2[1], e3[1]], [e1[2], e2[2], e3[2]]]
        Rt = transpose(R)
        C = scale(matvec(Rt, t), -1.0)             # camera centre, board units
        return {"R": R, "Rt": Rt, "t": t, "C": C, "Kinv": Kinv, "defects": defects}


def matmul(A, B):
    return [[sum(A[r][k] * B[k][c] for k in range(3)) for c in range(3)] for r in range(3)]


def rotate_board_frame(H, deg):
    """H for a board frame turned by `deg` about the board's own axis.

    planeOf() leaves exactly one degree of freedom unfitted -- the rotation about the
    board's axis -- and it falls out of the fitted ellipse's own RotatedRect angle, which
    is a property of that CAMERA'S view and not of the board. So every camera's "board
    frame" has its own zero, and this is how one is turned onto another's.
    """
    a = math.radians(deg)
    Rz = [[math.cos(a), -math.sin(a), 0.0], [math.sin(a), math.cos(a), 0.0], [0.0, 0.0, 1.0]]
    return matmul(H, Rz)


def setup_of(H, cx, cy, fpx):
    """Everything one camera contributes: the pose at a focal length, and H^-1."""
    pose = Pose(H, cx, cy)
    P = pose.at(fpx)
    if P is None:
        return None
    P["Hinv"] = inverse3(H)
    P["f"] = fpx
    P["pose"] = pose
    return P if P["Hinv"] else None


def ray(P, px, py):
    """The ray through a pixel, in board coordinates: (origin, unit direction)."""
    d_cam = matvec(P["Kinv"], [px, py, 1.0])
    return P["C"], unit(matvec(P["Rt"], d_cam))


def closest_approach(P1, d1, P2, d2):
    """Where two rays come nearest, and how near. Distances in whatever P is in."""
    w0 = sub(P1, P2)
    b = dot(d1, d2)
    den = 1.0 - b * b
    if abs(den) < 1e-12:
        return None
    dd, ee = dot(d1, w0), dot(d2, w0)
    s = (b * ee - dd) / den
    u = (ee - b * dd) / den
    X1 = add(P1, scale(d1, s))
    X2 = add(P2, scale(d2, u))
    return {"residual": norm(sub(X1, X2)), "mid": scale(add(X1, X2), 0.5),
            "s": s, "u": u, "parallax": math.degrees(math.acos(min(1.0, abs(b))))}


def plane_hit(Hinv, px, py):
    """Where a pixel lands on the board plane. Free of f, and #1490's own mapping."""
    q = matvec(Hinv, [px, py, 1.0])
    if q[2] == 0.0:
        return None
    return [q[0] / q[2], q[1] / q[2], 0.0]


# ---- read the census -------------------------------------------------------------------
cams, rows, end = {}, [], {}
for line in open(sys.argv[1]):
    if not line.startswith("I1493"):
        continue
    fields = dict(kv.split("=", 1) for kv in line.split() if "=" in kv)
    if line.startswith("I1493CAM "):
        cams[int(fields["cam"])] = fields
    elif line.startswith("I1493DART "):
        rows.append(fields)
    elif line.startswith("I1493END "):
        end = fields

raw = {}
for c in sorted(cams):
    f = cams[c]
    if f["planeBuilt"] != "1":
        continue
    h = [float(x) for x in f["H"].split(",")]
    raw[c] = {"H": [h[0:3], h[3:6], h[6:9]],
              "cx": float(f["frameW"]) * 0.5, "cy": float(f["frameH"]) * 0.5,
              "phase": None if f.get("phaseDeg", "none") == "none" else float(f["phaseDeg"])}

print()
print("=" * 88)
print("  THE CAMERAS, AND THE FOCAL LENGTH A BOARD PLANE IMPLIES")
print("=" * 88)
print()
for c in sorted(cams):
    f = cams[c]
    print("  camera %d: sees=%s scorable=%s anchored=%s  doubles ring %.1f x %.1f px  bull (%s)"
          % (c, f["sees"], f["scorable"], f.get("anchored", "?"),
             float(f["doublesPx"]), float(f["doublesMinorPx"]), f["bull"]))
    print("            planeBuilt=%s tilt=%s conicOfDoubles=%s wedge grid at %s deg"
          % (f["planeBuilt"], f["tilt"], f["conicOfDoubles"], f.get("phaseDeg", "?")))
    if c not in raw:
        print("            NO PLANE -- this camera contributes no ray")
        continue
    p = Pose(raw[c]["H"], raw[c]["cx"], raw[c]["cy"])
    raw[c]["pose"] = p

    def show(x):
        return "%.1f px" % x if x else "IMAGINARY"
    print("            f from r1.r2=0: %-12s from |r1|=|r2|: %-12s from both: %s"
          % (show(p.f_ortho), show(p.f_equal), show(p.f_ls)))
print()
print("  A 1280-wide 100-degree lens -- the README's hardware reference for this rig -- is")
print("  about 537 px. These are what the pinhole model needs the lens to be for THIS")
print("  homography to be a pose, and they are not a measurement of the lens.")
print()


def focal_for(c):
    """Both constraints at once where they can be had, then either alone.

    The joint estimate is first because each constraint alone degenerates on its own
    geometry -- the synthetic control has a camera whose r1.r2 estimate is imaginary while
    its |r1|=|r2| estimate is exact -- and all three are printed above."""
    p = raw[c]["pose"]
    return p.f_ls or p.f_ortho or p.f_equal


usable = [c for c in sorted(raw) if focal_for(c)]
if len(usable) < 2:
    print("  FEWER THAN TWO CAMERAS YIELD A POSE. Nothing can be intersected.")
    sys.exit(1)

# Two compositions, and the difference between them is the whole of this probe's answer.
#   "own"     each camera's planeOf() frame as it comes
#   "aligned" each turned onto its own twenty-fold wedge grid, so the three frames agree
#             to within a whole number of sectors
own, aligned = {}, {}
for c in usable:
    fpx = focal_for(c)
    S = setup_of(raw[c]["H"], raw[c]["cx"], raw[c]["cy"], fpx)
    if S:
        own[c] = S
    if raw[c]["phase"] is not None:
        A = setup_of(rotate_board_frame(raw[c]["H"], raw[c]["phase"]),
                     raw[c]["cx"], raw[c]["cy"], fpx)
        if A:
            aligned[c] = A


def report_poses(setups, title):
    print("=" * 88)
    print("  " + title)
    print("=" * 88)
    print()
    for c in sorted(setups):
        S = setups[c]
        C = scale(S["C"], MM)
        dist = norm(C)
        n1, n2, ortho = S["defects"]
        elev = math.degrees(math.asin(max(-1.0, min(1.0, C[2] / dist)))) if dist > 0 else 0.0
        azim = math.degrees(math.atan2(C[1], C[0]))
        print("  camera %d  f=%.1f px" % (c, S["f"]))
        print("      centre   (%8.1f, %8.1f, %8.1f) mm   |C| = %.1f mm" % (C[0], C[1], C[2], dist))
        print("      %.1f deg out of the board's plane, at azimuth %.1f deg" % (elev, azim))
        print("      what the pinhole assumption cost: |r1|=%.4f |r2|=%.4f r1.r2=%+.4f"
              "   (1, 1 and 0 if it held exactly)" % (n1, n2, ortho))
    print()
    print("  PAIRWISE, which is what a stereo pair is:")
    ks = sorted(setups)
    for i, a in enumerate(ks):
        for b in ks[i + 1:]:
            Ca, Cb = scale(setups[a]["C"], MM), scale(setups[b]["C"], MM)
            va, vb = unit(scale(Ca, -1.0)), unit(scale(Cb, -1.0))
            ang = math.degrees(math.acos(max(-1.0, min(1.0, dot(va, vb)))))
            print("      %d-%d  baseline %7.1f mm   %.1f deg apart as seen from the board centre"
                  % (a, b, norm(sub(Ca, Cb)), ang))
    print()


report_poses(own, "THE COMPOSED POSES, in millimetres about the board's centre")
print("  THE SIGN OF z IS A CONVENTION AND NOT A FINDING. An image's y axis points down,")
print("  so a board frame read out of one is left-handed with respect to the board; every")
print("  camera here is reflected the same way and a reflection is an isometry, so no")
print("  distance or residual below moves by a hair. What it means is that a centre printed")
print("  at z = -260 mm is 260 mm in FRONT of the board, not behind it.")
print()

# ---- the darts -------------------------------------------------------------------------
darts = {}
for f in rows:
    darts.setdefault(int(f["dart"]), []).append(f)


def tips_of(dart_rows, setups):
    out = {}
    for f in dart_rows:
        c = int(f["cam"])
        if f["tipFound"] != "1" or c not in setups:
            continue
        x, y = (float(v) for v in f["tip"].split(","))
        out[c] = {"px": x, "py": y, "onBoard": f.get("onBoard") == "1",
                  "score": f.get("score", "-")}
    return out


def pairs_over(setups):
    out = []
    for d in sorted(darts):
        t = tips_of(darts[d], setups)
        cs = sorted(t)
        for i, a in enumerate(cs):
            for b in cs[i + 1:]:
                Pa = ray(setups[a], t[a]["px"], t[a]["py"])
                Pb = ray(setups[b], t[b]["px"], t[b]["py"])
                ca = closest_approach(Pa[0], Pa[1], Pb[0], Pb[1])
                if ca is None:
                    continue
                ha = plane_hit(setups[a]["Hinv"], t[a]["px"], t[a]["py"])
                hb = plane_hit(setups[b]["Hinv"], t[b]["px"], t[b]["py"])
                mid = scale(ca["mid"], MM)
                out.append({"dart": d, "a": a, "b": b,
                            "residual": ca["residual"] * MM,
                            "parallax": ca["parallax"],
                            "height": mid[2], "radius": math.hypot(mid[0], mid[1]),
                            "separation": norm(sub(ha, hb)) * MM if (ha and hb) else None,
                            "onBoard": t[a]["onBoard"] and t[b]["onBoard"],
                            "scoreA": t[a]["score"], "scoreB": t[b]["score"]})
    return out


all_pairs = pairs_over(own)

print("=" * 88)
print("  EVERY DART TWO OR MORE CAMERAS SAW, AND HOW CLOSE THEIR RAYS COME TO MEETING")
print("=" * 88)
print()
print("  residual    how near the two rays pass each other, in millimetres. 0 would be two")
print("              rays really meeting at one 3D point.")
print("  parallax    the angle between the two rays. It is what turns a residual into a")
print("              statement: two rays a degree apart pass close to each other wherever")
print("              they are pointed, and residual is about separation x sin(parallax).")
print("  separation  the two tips unprojected onto the board plane and measured apart --")
print("              #1490's own number, recomputed here. It is free of the focal length,")
print("              and the residual can never exceed it.")
print("  height      how far the nearest approach is out of the board's plane; radius, how")
print("              far from the board's centre it is. The board's own edge is 170 mm.")
print("  onBoard     both cameras put their tip inside the outer double ellipse -- the")
print("              shipped scorePoint's own decision, and #1490's tip-plausibility split.")
print()
print("   dart  pair   residual  parallax   separation    height    radius  onBoard  scores")
print("   ----  ----  ---------  --------  -----------  --------  --------  -------  ------")
for p in all_pairs:
    print("   %4d  %d-%d  %9.1f  %7.2f  %11s  %+8.1f  %8.1f  %7s  %s / %s"
          % (p["dart"], p["a"], p["b"], p["residual"], p["parallax"],
             ("%.1f" % p["separation"]) if p["separation"] is not None else "n/a",
             p["height"], p["radius"], "yes" if p["onBoard"] else "no",
             p["scoreA"], p["scoreB"]))
if not all_pairs:
    print("   (none)")
print()

on = [p for p in all_pairs if p["onBoard"]]
off = [p for p in all_pairs if not p["onBoard"]]


def partition(name, pop, note=True):
    print("  %s" % name)
    print("    residual   %s" % stat([p["residual"] for p in pop]))
    print("    parallax   %s" % stat([p["parallax"] for p in pop], "deg"))
    print("    separation %s" % stat([p["separation"] for p in pop if p["separation"] is not None]))
    print("    |height|   %s" % stat([abs(p["height"]) for p in pop]))
    nd = len(set(p["dart"] for p in pop))
    print("    over %d pair%s, on %d dart%s."
          % (len(pop), "" if len(pop) == 1 else "s", nd, "" if nd == 1 else "s"))
    if note and 0 < len(pop) <= 3:
        print("    THREE PAIRS OR FEWER IS NOT A DISTRIBUTION. The rows above are the")
        print("    measurement; a median of %d is quoted only so nothing is counted by eye." % len(pop))
    print()


print("=" * 88)
print("  PARTITIONED BY TIP PLAUSIBILITY, WITH THE COUNTS SAID PLAINLY")
print("=" * 88)
print()
print("  #1492 measured the between-camera tip spread at median 73.6 mm and max 256.5 mm --")
print("  a camera finding the arm or a previous dart rather than the dart. A large residual")
print("  from a wrong tip is not evidence about the poses, so the two populations are never")
print("  pooled. The split is the shipped scorePoint's own: both tips inside the outer")
print("  double ellipse, or not.")
print()
partition("BOTH TIPS ON THE BOARD (the partition the issue asks about)", on)
partition("AT LEAST ONE TIP OFF THE BOARD (reported, never pooled with the above)", off)

# ---- the composition's own degree of freedom -------------------------------------------
print("=" * 88)
print("  THE ROTATION planeOf() NEVER FITTED, AND WHAT IT DOES TO A COMPOSITION")
print("=" * 88)
print()
print("  #1467's plane pins the board's conic and the board's centre, which is seven of a")
print("  homography's eight degrees of freedom. Its own header says what the eighth is:")
print("  'those two measurements leave ONE free parameter, the rotation offset, and it is")
print("  fitted from the candidates themselves' -- fitted by fitTwentyFold, which planeOf")
print("  does not call. So the frame planeOf returns has its zero wherever the fitted")
print("  ellipse's RotatedRect angle happened to fall, which is a fact about THAT CAMERA'S")
print("  view and not about the board.")
print()
print("  Three such frames are not one frame. Turning each camera onto its own twenty-fold")
print("  wedge grid -- the ring the calibration already generated, phaseDeg above -- makes")
print("  them agree to within a WHOLE NUMBER OF SECTORS, and no further: which sector is")
print("  the 20 is the wedge anchor, #1486, and ADR-0084 records that one camera of three")
print("  has one on this rig. Both compositions are therefore printed.")
print()
if len(aligned) < 2:
    print("  FEWER THAN TWO CAMERAS HAVE A WEDGE GRID, so there is no aligned composition.")
    print()
else:
    report_poses(aligned, "THE SAME POSES, EACH TURNED ONTO ITS OWN WEDGE GRID")
    print("  ... and the sector that is still unknown is worth 18 degrees apiece. What that")
    print("  is worth in millimetres, per pair, over all twenty relative sectors:")
    print()
    ks = sorted(aligned)
    sweep = {}
    for i, a in enumerate(ks):
        for b in ks[i + 1:]:
            print("    cameras %d-%d" % (a, b))
            print("      k   b turned   baseline   on-board residual      where it meets")
            print("      --  --------  ---------   -----------------   ------------------")
            rowsk = []
            for k in range(20):
                turned = setup_of(rotate_board_frame(raw[b]["H"], raw[b]["phase"] + k * 18.0),
                                  raw[b]["cx"], raw[b]["cy"], focal_for(b))
                if turned is None:
                    continue
                two = {a: aligned[a], b: turned}
                ps = [p for p in pairs_over(two) if p["onBoard"]]
                base = norm(sub(scale(aligned[a]["C"], MM), scale(turned["C"], MM)))
                rec = {"k": k, "base": base, "n": len(ps),
                       "res": median([p["residual"] for p in ps]) if ps else None,
                       "hgt": median([abs(p["height"]) for p in ps]) if ps else None}
                rowsk.append(rec)
                print("      %2d  %6.0f deg  %7.1f mm   %-17s   %s"
                      % (k, k * 18.0, base,
                         ("median %6.1f mm over %d" % (rec["res"], rec["n"]))
                         if ps else "no on-board pair",
                         ("|height| %6.1f mm" % rec["hgt"]) if ps else ""))
            sweep[(a, b)] = rowsk
            print()
    print("  NOTHING HERE PICKS A k. The row with the smallest residual is not evidence that")
    print("  that sector is the right one -- twenty hypotheses against fourteen pairs will")
    print("  always produce a smallest. What the columns are for is the SPREAD: if turning a")
    print("  camera a fifth of the way round the board barely moves the residual, then the")
    print("  residual is not measuring the thing it is being asked about. The height column")
    print("  is beside it because that is the half that does move -- two rays can pass close")
    print("  to each other 300 mm out of the board's plane, where no dart has ever been.")
    print()
    print("  WHERE EACH PAIR'S TWO COLUMNS POINT, AND WHETHER THE THREE AGREE ROUND THE LOOP.")
    print("  Reported, not chosen: this is a property of the table above, and the counts it")
    print("  rests on are printed beside it because a preference over four pairs is not one.")
    print()
    print("    pair   smallest residual   smallest |height|   on-board pairs it rests on")
    print("    ----   -----------------   -----------------   --------------------------")
    best = {}
    for key in sorted(sweep):
        have = [r for r in sweep[key] if r["res"] is not None]
        if not have:
            continue
        br = min(have, key=lambda r: r["res"])
        bh = min(have, key=lambda r: r["hgt"])
        best[key] = (br, bh)
        print("    %d-%d      k=%-2d  %6.1f mm     k=%-2d  %6.1f mm     %d"
              % (key[0], key[1], br["k"], br["res"], bh["k"], bh["hgt"], br["n"]))
    print()
    if len(best) == 3:
        (a1, b1), (a2, b2), (a3, b3) = sorted(best)
        # (1,2), (1,3), (2,3): a sector assignment is consistent when k12 + k23 == k13.
        for label, idx in (("residual", 0), ("|height|", 1)):
            k12 = best[(a1, b1)][idx]["k"]
            k13 = best[(a2, b2)][idx]["k"]
            k23 = best[(a3, b3)][idx]["k"]
            closes = (k12 + k23 - k13) % 20
            print("    by %-9s  k(%d-%d)=%-2d  k(%d-%d)=%-2d  k(%d-%d)=%-2d  ->  "
                  "k(%d-%d)+k(%d-%d)-k(%d-%d) = %d mod 20%s"
                  % (label, a1, b1, k12, a3, b3, k23, a2, b2, k13,
                     a1, b1, a3, b3, a2, b2, closes,
                     "   THE LOOP CLOSES" if closes == 0 else ""))
        print()
        print("    A loop that closes is the one thing in this section that three independent")
        print("    pairwise readings could not produce by accident in twenty ways each: the")
        print("    sector each pair prefers is a statement about that pair alone, and nothing")
        print("    above makes them agree. A loop that does not close is equally worth having")
        print("    and means only that the preference is not yet a measurement.")
        print()

print("=" * 88)
print("  HOW MUCH OF THE ABOVE IS THE FOCAL LENGTH THIS FILE ASSUMED")
print("=" * 88)
print()
print("  The residual is the one figure here that depends on f, so it is re-read at other")
print("  values of it, the same value given to every camera. NO VALUE BELOW IS ASSERTED TO")
print("  BE RIGHT and nothing is concluded from the table; it is here so a reader can see")
print("  whether the residual is a statement about the poses or about the assumption.")
print()
print("     f (px)   on-board pairs            all pairs")
print("     ------   -----------------------   -----------------------")
for fpx in [300.0, 400.0, 537.0, 700.0, 900.0, 1200.0, 1800.0, 2600.0]:
    su = {}
    for c in usable:
        S = setup_of(raw[c]["H"], raw[c]["cx"], raw[c]["cy"], fpx)
        if S:
            su[c] = S
    ps = pairs_over(su)
    o = [p["residual"] for p in ps if p["onBoard"]]
    a = [p["residual"] for p in ps]
    print("     %6.0f   %-23s   %s"
          % (fpx, ("median %6.1f mm (n=%d)" % (median(o), len(o))) if o else "no pairs",
             ("median %6.1f mm (n=%d)" % (median(a), len(a))) if a else "no pairs"))
print("     est.     %-23s   %s"
      % (("median %6.1f mm (n=%d)" % (median([p["residual"] for p in on]), len(on))) if on else "no pairs",
         ("median %6.1f mm (n=%d)" % (median([p["residual"] for p in all_pairs]), len(all_pairs)))
         if all_pairs else "no pairs"))
print("              (est. = each camera's own f from its own homography, the table above)")
print()

print("=" * 88)
print("  WHAT THIS CENSUS READ")
print("=" * 88)
print("    %s cycles, %s darts, %d cameras calibrated, %d with a board plane, %d with a pose."
      % (end.get("cycles", "?"), end.get("darts", "?"), len(cams), len(raw), len(own)))
print("    %d dart%s had two or more cameras report a tip."
      % (len(set(p["dart"] for p in all_pairs)),
         "" if len(set(p["dart"] for p in all_pairs)) == 1 else "s"))
print()

sys.exit(0 if all_pairs else 1)
