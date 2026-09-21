# #1492: the rows of i1492_tip_probe, read as the two questions this issue asks.
#
#   python3 i1492_spread.py <rows.txt> [--quiet]
#
# 1. WHERE THE DISAGREEMENT LIVES. #1490 measured "median 73.6 mm, max 256.5 mm" over
#    every camera that reported a tip. That population mixes two things: cameras that
#    placed the dart ON the board and cameras that placed it off the board entirely,
#    which the scorer publishes as MISS and which score_processing::processScore ALREADY
#    refuses a vote (`may_vote[i] = tip_found && score_test != "MISS"`). So this splits
#    the darts by whether any camera read off the board and prints both halves. On this
#    fixture the split is total, and that is assertion ONE.
#
# 2. WHAT THE OFF-BOARD READINGS ARE. Two mechanisms, told apart by the census
#    dart_processing prints about its own figure, both threshold-free:
#
#      OTHER-PIECE  the published tip is not a point of the contour the centroid was
#                   measured from (tipPiece != 0). The hull spans every admitted contour
#                   while the centroid is piece 0's alone, so the line from centroid to
#                   tip crosses empty space: the point was found on a DIFFERENT object --
#                   the dart already in the board, a shadow, a detached fragment.
#      FLOOR-BOUND  the published tip moves when the 400 px contour floor is removed
#                   (tipNoFloor != tip), so what this reading published is a fact about
#                   the floor and not about the dart.
#
#    A reading that is neither is the algorithm working on a whole, single figure, and
#    an off-board reading of that kind would be a THIRD mechanism. Assertion TWO is that
#    there is none: every off-board reading on this fixture is one of the two above.
#
# NO THRESHOLD IS FITTED HERE. Both classifications are exact facts about one call, and
# the split in (1) is an ORDERING -- the worst all-on-board dart against the best dart
# with an off-board reading -- not a constant.
import math
import sys

MM = 170.0

path = sys.argv[1]
quiet = "--quiet" in sys.argv

cams, tips, darts, end = {}, [], {}, {}
order = []
for line in open(path):
    f = dict(kv.split("=", 1) for kv in line.split() if "=" in kv)
    if line.startswith("I1492CAM "):
        cams[f["cam"]] = f
    elif line.startswith("I1492TIP "):
        tips.append(f)
    elif line.startswith("I1492DART "):
        d = int(f["dart"])
        darts.setdefault(d, []).append(f)
        if d not in order:
            order.append(d)
    elif line.startswith("I1492END "):
        end = f


def num(f, k):
    v = f.get(k, "none")
    return None if v == "none" else float(v)


def figure_for(row):
    """The I1492TIP line this reading came out of, matched on the camera and on the two
    points the two lines share. A reading with no tip has none by construction."""
    if row.get("tipFound") != "1":
        return None
    # The two lines print the same two points to different precisions (%.4f against
    # std::to_string's six places), so they are matched as NUMBERS and not as strings.
    def pair(s):
        a, b = s.split(",")
        return (round(float(a), 2), round(float(b), 2))

    want_tip, want_centre = pair(row["tip"]), pair(row["centre"])
    best = None
    for t in tips:
        if t["cam"] != row["cam"]:
            continue
        if pair(t["tip"]) == want_tip and pair(t["centroid"]) == want_centre:
            best = t
    return best


def sector_gap(a, b):
    return ((a - b + 9.0) % 18.0) - 9.0


def stat(v):
    if not v:
        return "no measurements"
    s = sorted(v)
    med = s[len(s) // 2] if len(s) % 2 else 0.5 * (s[len(s) // 2 - 1] + s[len(s) // 2])
    return "n=%-3d min %6.1f   median %6.1f   max %6.1f mm" % (len(s), s[0], med, s[-1])


def worst_pair(rows):
    tipped = [f for f in rows if num(f, "plR") is not None]
    worst, which = 0.0, ""
    for i in range(len(tipped)):
        for j in range(i + 1, len(tipped)):
            a, b = tipped[i], tipped[j]
            ra, rb = num(a, "plR"), num(b, "plR")
            dr = abs(ra - rb) * MM
            dt = sector_gap(num(a, "plPhase"), num(b, "plPhase"))
            tan = abs(dt) * math.pi / 180.0 * 0.5 * (ra + rb) * MM
            sep = math.hypot(dr, tan)
            if sep > worst:
                worst, which = sep, "%s-%s" % (a["cam"], b["cam"])
    return worst, which


FAILED = 0


def say(msg, ok):
    global FAILED
    print(msg)
    if not ok:
        FAILED = 1


print()
print("  THE CAMERAS")
for c in sorted(cams):
    f = cams[c]
    print("    camera %s: bull (%s), doubles ellipse %s at %s deg, plane built=%s, wedge grid %s deg"
          % (c, f["bull"], f["size"], f["angle"], f["planeBuilt"], f["phaseDeg"]))

on_darts, off_darts = [], []
offboard_readings = []
print()
print("  EVERY DART, AND WHAT EACH CAMERA FOUND")
for d in order:
    rows = darts[d]
    sep, pair = worst_pair(rows)
    off = [f for f in rows if f.get("tipFound") == "1" and f.get("score") == "MISS"]
    (off_darts if off else on_darts).append((sep, d, pair))
    print("  dart %2d (cycle %s, %s)   worst pair %s apart >= %6.1f mm%s"
          % (d, rows[0]["cycle"], rows[0]["state"], pair or "  -", sep,
             "   <-- a camera is off the board" if off else ""))
    for f in rows:
        fig = figure_for(f)
        if f.get("tipFound") != "1":
            print("      camera %s  no tip in this window" % f["cam"])
            continue
        marks = []
        if fig is not None:
            if fig.get("tipPiece") not in ("0", None):
                marks.append("OTHER-PIECE(%s of %s)" % (fig.get("tipPiece"), fig.get("pieces")))
            if fig.get("tipNoFloor") != fig.get("tip"):
                marks.append("FLOOR-BOUND")
        print("      camera %s  tip=(%s) %-5s  plane %6.1f mm   figure span %s px, pieces %s, areas %s  %s"
              % (f["cam"], f["tip"], f.get("score", "-"),
                 (num(f, "plR") or 0) * MM,
                 fig.get("maxdist", "?").split(".")[0] if fig else "?",
                 fig.get("pieces", "?") if fig else "?",
                 fig.get("areas", "?") if fig else "?",
                 " ".join(marks) if marks else ""))
        if f.get("score") == "MISS":
            offboard_readings.append((d, f["cam"], fig, marks))

print()
print("  ==== ONE: where the disagreement lives ==========================================")
on_w = [s for s, _, _ in on_darts]
off_w = [s for s, _, _ in off_darts]
print("    darts where every reporting camera put the tip ON the board:  %d" % len(on_darts))
print("      worst pair per dart:                %s" % stat(on_w))
print("    darts where at least one camera read off the board (MISS):    %d" % len(off_darts))
print("      worst pair per dart:                %s" % stat(off_w))
print()
print("    #1490's headline -- median 73.6 mm, max 256.5 mm -- is the two populations")
print("    added together. A MISS camera is already refused a vote by processScore, so")
print("    the metres in that figure are not a geometry the scorer uses; they are a tip")
print("    stage that lost the dart, and the scorer noticing.")
if on_w and off_w:
    say("    %s  the two populations are disjoint: the worst all-on-board dart is %.1f mm "
        "and the best dart with an off-board camera is %.1f mm"
        % ("OK  " if max(on_w) < min(off_w) else "FAIL", max(on_w), min(off_w)),
        max(on_w) < min(off_w))
else:
    say("    FAIL one of the two populations is empty, so this fixture cannot answer the "
        "question: on=%d off=%d" % (len(on_darts), len(off_darts)), False)

print()
print("  ==== TWO: what an off-board reading is =========================================")
print("    THE MARKS ARE A MECHANISM CENSUS AND NOT A DIAGNOSIS ON THEIR OWN: a reading")
print("    that scored can carry either one, and the control below says how often. What")
print("    is asserted is the narrower thing -- that NO off-board reading came out of a")
print("    whole, single figure, so there is no third mechanism hiding in this fixture.")
unclassified = [(d, c) for d, c, fig, marks in offboard_readings if not marks]
counted = {}
for d, c, fig, marks in offboard_readings:
    counted[",".join(sorted(marks)) or "NEITHER"] = counted.get(",".join(sorted(marks)) or "NEITHER", 0) + 1
print("    off-board readings:                   %d" % len(offboard_readings))
for k in sorted(counted):
    print("      %-32s %d" % (k, counted[k]))
# the control: the same marks over the readings that DID score
control = {"OTHER-PIECE": 0, "FLOOR-BOUND": 0, "NEITHER": 0, "n": 0}
for d in order:
    for f in darts[d]:
        if f.get("tipFound") != "1" or f.get("score") == "MISS":
            continue
        fig = figure_for(f)
        control["n"] += 1
        m = []
        if fig is not None and fig.get("tipPiece") not in ("0", None):
            control["OTHER-PIECE"] += 1
            m.append(1)
        if fig is not None and fig.get("tipNoFloor") != fig.get("tip"):
            control["FLOOR-BOUND"] += 1
            m.append(1)
        if not m:
            control["NEITHER"] += 1
print("    CONTROL -- the same two marks over the %d readings that DID score:" % control["n"])
print("      OTHER-PIECE %d, FLOOR-BOUND %d, neither %d"
      % (control["OTHER-PIECE"], control["FLOOR-BOUND"], control["NEITHER"]))
say("    %s  every off-board reading is OTHER-PIECE or FLOOR-BOUND; %d are neither%s"
    % ("OK  " if not unclassified else "FAIL", len(unclassified),
       "" if not unclassified else " -- " + ", ".join("dart %s cam %s" % x for x in unclassified)),
    not unclassified)

print()
print("  ==== the instrument, not the finding ===========================================")
planes = sum(1 for c in cams if cams[c]["planeBuilt"] == "1")
ok = len(cams) == 3 and planes >= 2 and int(end.get("darts", 0)) > 0
print("    %d cameras, %d with a board plane, %s cycles, %s darts"
      % (len(cams), planes, end.get("cycles", "?"), end.get("darts", "?")))
say("    %s  the replay reproduced a three-camera board with darts on it"
    % ("OK  " if ok else "FAIL"), ok)

# one machine-readable line, for the shell that runs this twice
print("I1492SUMMARY offboard=%d darts=%s onpop=%d offpop=%d onworst=%.1f offbest=%.1f"
      % (len(offboard_readings), end.get("darts", "0"), len(on_darts), len(off_darts),
         max(on_w) if on_w else -1.0, min(off_w) if off_w else -1.0))
sys.exit(FAILED)
