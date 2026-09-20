# #1490: the census rows, read as the answer to one question -- how far apart do two
# cameras place the same dart, in millimetres, through #1467's planeOf() and through the
# BoardPosition the scorer already computes?
#
#   python3 i1490_spread.py <rows.txt>
#
# It asserts NO THRESHOLD. ADR-0084 s4 defers that deliberately, and a constant chosen
# from one fixture is #1322's mistake. Its exit status is about the INSTRUMENT: a census
# that compared nothing has measured nothing.
#
# THE ONE THING A READER HAS TO CARRY INTO THE TABLE. The two mappings do not answer for
# the same tips. planeOf() answers for any pixel -- a plane maps the whole image -- while
# BoardPosition::has_radius is only set where scorePoint put the tip INSIDE the outer
# double ellipse; a camera whose tip detector found the thrower's arm, a previous dart or
# a speck reads MISS and the ruler says nothing about it. So a spread taken over every
# camera that reported a tip would compare a population of 35 pairs against one of 14 and
# read as a difference between the mappings when it is a difference between the darts they
# were asked about. The matched population -- both cameras with the tip on the board -- is
# therefore printed FIRST and is the comparison the issue asks for; the wider one is
# printed after it, named for what it really contains.
import math
import sys

MM = 170.0  # the outer edge of the double ring, in millimetres

rows, cams, end = [], {}, {}
for line in open(sys.argv[1]):
    f = dict(kv.split("=", 1) for kv in line.split() if "=" in kv)
    if line.startswith("I1490DART "):
        rows.append(f)
    elif line.startswith("I1490CAM "):
        cams[f["cam"]] = f
    elif line.startswith("I1490END "):
        end = f


def num(f, k):
    v = f.get(k, "none")
    return None if v == "none" else float(v)


def sector_gap(a, b):
    """Two in-sector angles in degrees, which differ by a whole number of sectors at
    most, reduced to the disagreement that is really there: (-9, +9]."""
    return ((a - b + 9.0) % 18.0) - 9.0


def stat(v, unit="mm"):
    if not v:
        return "no measurements"
    s = sorted(v)
    med = s[len(s) // 2] if len(s) % 2 else 0.5 * (s[len(s) // 2 - 1] + s[len(s) // 2])
    return "n=%-3d min %6.1f   median %6.1f   max %6.1f %s" % (len(s), s[0], med, s[-1], unit)


darts = {}
for f in rows:
    darts.setdefault(int(f["dart"]), []).append(f)

print()
print("  THE CAMERAS, as this replay calibrated them")
for c in sorted(cams):
    f = cams[c]
    print("    camera %s: doubles ring %s px, bull (%s), plane built=%s tilt=%s, "
          "wedge grid at %s deg, anchored=%s"
          % (c, f["doublesPx"], f["bull"], f["planeBuilt"], f["tilt"], f["phaseDeg"], f["anchored"]))
print()
print("  plane = through #1467's planeOf(): |Hinv(tip)| * 170 mm, and the tip's angle")
print("          within this camera's own twenty-fold wedge grid.")
print("  ruler = the shipped BoardPosition: radius * 170 mm, and 18*fraction across the")
print("          wedge the scorer put the tip in. Absent where the tip is off the board.")
print()

matched = {"r": [], "t": [], "d": [], "rr": [], "rt": [], "rd": []}   # plane r/t/d, ruler r/t/d
wide = {"r": [], "t": [], "d": []}
pair_matched, pair_wide = {}, {}
mapping_gap = []          # |plane - ruler| on ONE camera's own tip
darts_two_on_board = 0
darts_two_tips = 0

for d in sorted(darts):
    fs = darts[d]
    tips = [f for f in fs if num(f, "plR") is not None]
    board = [f for f in fs if num(f, "bpR") is not None]
    if len(tips) >= 2:
        darts_two_tips += 1
    if len(board) >= 2:
        darts_two_on_board += 1

    print("  dart %2d (cycle %s, %s): %d camera(s) reported a tip, %d of them on the board"
          % (d, fs[0]["cycle"], fs[0]["state"], len(tips), len(board)))
    for f in fs:
        plR, bpR = num(f, "plR"), num(f, "bpR")
        if plR is None:
            print("      camera %s  no tip in this window" % f["cam"])
            continue
        gap = ""
        if bpR is not None:
            g = abs(plR - bpR) * MM
            mapping_gap.append(g)
            gap = "   the two mappings differ by %5.1f mm on this one reading" % g
        print("      camera %s  tip=(%s) %-5s  plane %6.1f mm @ %5.2f deg   ruler %s%s"
              % (f["cam"], f["tip"], f.get("score", "-"), plR * MM,
                 num(f, "plPhase") if num(f, "plPhase") is not None else float("nan"),
                 ("%6.1f mm @ %5.2f deg" % (bpR * MM, num(f, "bpPhase")))
                 if bpR is not None else "   off the board, so it says nothing",
                 gap))

    for i in range(len(tips)):
        for j in range(i + 1, len(tips)):
            a, b = tips[i], tips[j]
            pair = "%s-%s" % (a["cam"], b["cam"])
            ra, rb = num(a, "plR"), num(b, "plR")
            pa, pb = num(a, "plPhase"), num(b, "plPhase")
            dr = abs(ra - rb) * MM
            dt = sector_gap(pa, pb)
            rmean = 0.5 * (ra + rb) * MM
            tan = abs(dt) * math.pi / 180.0 * rmean
            sep = math.hypot(dr, tan)

            both_on_board = num(a, "bpR") is not None and num(b, "bpR") is not None
            line = ("      cameras %s  PLANE radial %6.1f mm | in-sector %5.2f deg = %5.1f mm "
                    "across | apart >= %6.1f mm" % (pair, dr, dt, tan, sep))
            if both_on_board:
                rra, rrb = num(a, "bpR"), num(b, "bpR")
                rpa, rpb = num(a, "bpPhase"), num(b, "bpPhase")
                rdr = abs(rra - rrb) * MM
                rdt = sector_gap(rpa, rpb)
                rmean2 = 0.5 * (rra + rrb) * MM
                rtan = abs(rdt) * math.pi / 180.0 * rmean2
                rsep = math.hypot(rdr, rtan)
                print(line)
                print("                  RULER radial %6.1f mm | in-sector %5.2f deg = %5.1f mm "
                      "across | apart >= %6.1f mm" % (rdr, rdt, rtan, rsep))
                for k, v in (("r", dr), ("t", tan), ("d", sep),
                             ("rr", rdr), ("rt", rtan), ("rd", rsep)):
                    matched[k].append(v)
                p = pair_matched.setdefault(pair, {"r": [], "rr": []})
                p["r"].append(dr)
                p["rr"].append(rdr)
            else:
                print(line + "   (one camera's tip is off the board: the ruler has no answer)")
            for k, v in (("r", dr), ("t", tan), ("d", sep)):
                wide[k].append(v)
            pair_wide.setdefault(pair, []).append(dr)

print()
print("  ==== THE ANSWER: both cameras placing the dart ON the board =====================")
print("  the only population where the two mappings answer about the same tips")
print("  camera pairs in it:                    %d" % len(matched["r"]))
print("  darts it rests on:                     %d of the %s the replay scored"
      % (darts_two_on_board, end.get("darts", "?")))
print("    through planeOf()      radial:      %s" % stat(matched["r"]))
print("    through BoardPosition  radial:      %s" % stat(matched["rr"]))
print("    through planeOf()      tangential:  %s" % stat(matched["t"]))
print("    through BoardPosition  tangential:  %s" % stat(matched["rt"]))
print("    through planeOf()      apart:       %s" % stat(matched["d"]))
print("    through BoardPosition  apart:       %s" % stat(matched["rd"]))
print()
print("  per camera pair, because one bad camera and uniformly poor planes look identical")
print("  in a mean (#1359):")
for pair in sorted(pair_matched):
    p = pair_matched[pair]
    print("    cameras %s  plane radial: %s" % (pair, stat(p["r"])))
    print("               ruler radial: %s" % stat(p["rr"]))
print()
print("  the two mappings against EACH OTHER, on one camera's own tip (not a spread):")
print("    |plane - ruler|                     %s" % stat(mapping_gap))

print()
print("  ==== the wider population: every camera that reported a tip ====================")
print("  planeOf() answers for any pixel, so a camera whose tip detector found something")
print("  that is not this dart is IN here and is not a statement about geometry.")
print("  camera pairs:                          %d" % len(wide["r"]))
print("  darts with two or more tips at all:    %d" % darts_two_tips)
print("    through planeOf()      radial:      %s" % stat(wide["r"]))
print("    through planeOf()      apart:       %s" % stat(wide["d"]))
for pair in sorted(pair_wide):
    print("    cameras %s  plane radial: %s" % (pair, stat(pair_wide[pair])))

print()
print("  ==== what these numbers are not ================================================")
print("  * NO THRESHOLD IS ASSERTED. ADR-0084 s4 defers it; nobody had seen this")
print("    distribution before, and a constant fitted to one fixture is #1322.")
print("  * The in-sector figure is a disagreement modulo one wedge. Neither mapping puts")
print("    an angle in a frame two cameras share -- planeOf()'s zero is wherever the")
print("    fitted ellipse's axis fell, and BoardPosition's is the anchored wedge, which")
print("    on this rig no camera has. What both give is a position within this camera's")
print("    own twenty-fold grid, and the grids differ by a whole number of sectors. So a")
print("    disagreement up to half a sector is read exactly and a larger one is not")
print("    visible at all: every `apart` figure is a FLOOR.")
print("  * Agreement is not correctness (ADR-0084's closing section). Only")
print("    mocks/rig-20260918/GROUND-TRUTH.md can tell those apart, and this probe")
print("    deliberately does not ask it.")
print("  * A spread here is the WHOLE CHAIN and not the geometry alone: the tip pixel is")
print("    dart_processing's, and a camera that found the shaft, the flight or a dart")
print("    already in the board disagrees by millimetres that no plane caused. What is")
print("    clean is the COMPARISON -- both mappings are handed the same tip, so the two")
print("    columns differ only by the mapping, whatever the tips are worth.")

# The instrument, not the finding.
sys.exit(0 if matched["r"] else 1)
