# #1494 and #1495: the four arms of one binary read side by side.
#
#   python3 i1494_arms.py <rundir> AA AB BA BB
#
# Every claim below is a COMPARISON between two arms of one binary, or an exact fact about
# one reading. None of them is a number fitted to this fixture (#1322, #1478): a threshold
# on a millimetre figure off this footage is the mistake #1484 refused and #1492 refused
# again, and it would be refused here too.
#
# WHAT THE TWO MARKS MEAN AFTER THE REPAIR, WHICH IS NOT WHAT THEY MEANT BEFORE.
# #1492's census marks a reading OTHER-PIECE when the published tip is not a point of the
# contour the centroid was measured from. Before #1494 that was always a fault, because
# the only reason to be on another contour was that another OBJECT had won the tip. After
# it, a dart's own shaft fragment carrying the tip is the repair WORKING -- that is what
# admitting the fragment was for. So the mark is reported and is no longer the thing
# judged. What is judged is `tipGap`: how far across empty space the published line ran,
# from the tip to the nearest point of the contour the centroid came from. A shaft
# fragment is a few pixels away. The older dart on dart 8 camera 1 was 264.
import sys
import collections

run = sys.argv[1]
arms = sys.argv[2:]

FAILED = 0
def say(msg, ok):
    global FAILED
    print(msg)
    if not ok:
        FAILED = 1

def readings(tag):
    """(dart, cam) -> the I1492DART row, and the I1492TIP figure it came out of."""
    tips, out = [], {}
    rows = []
    for line in open("%s/rows-%s.txt" % (run, tag)):
        f = dict(kv.split("=", 1) for kv in line.split() if "=" in kv)
        if line.startswith("I1492TIP "):
            tips.append(f)
        elif line.startswith("I1492DART "):
            rows.append(f)
    def pair(s):
        a, b = s.split(",")
        return (round(float(a), 2), round(float(b), 2))
    for r in rows:
        fig = None
        if r.get("tipFound") == "1":
            for t in tips:
                if t["cam"] == r["cam"] and pair(t["tip"]) == pair(r["tip"]) \
                   and pair(t["centroid"]) == pair(r["centre"]):
                    fig = t
        out[(r["dart"], r["cam"])] = (r, fig)
    return out

def summary(tag):
    for line in open("%s/spread-%s.txt" % (run, tag)):
        if line.startswith("I1492SUMMARY "):
            return dict(kv.split("=", 1) for kv in line.split() if "=" in kv)
    return {}

A = {t: readings(t) for t in arms}
S = {t: summary(t) for t in arms}

def offboard(t):
    return sum(1 for (r, f) in A[t].values() if r.get("tipFound") == "1" and r.get("score") == "MISS")

def worst_gap(t):
    g = [(int(f["tipGap"]), k) for k, (r, f) in A[t].items() if f is not None and "tipGap" in f]
    return max(g) if g else (0, None)

def other_piece(t):
    return sum(1 for (r, f) in A[t].values()
               if f is not None and f.get("tipPiece") not in ("0", None))

def floor_bound(t):
    return sum(1 for (r, f) in A[t].values()
               if f is not None and f.get("tipNoFloor") != f.get("tip"))

print()
print("  arm  what is pinned                          readings  off-board  OTHER-PIECE  FLOOR-BOUND  worst tipGap")
what = {"AA": "both pins: the tree #1492 measured",
        "AB": "OD_TIP_FIGURE=union: #1495 alone",
        "BA": "OD_ADVANCE_RESET=per-camera: #1494 alone",
        "BB": "nothing: both repairs, which is what ships"}
for t in arms:
    g, k = worst_gap(t)
    print("  %-4s %-40s %5d      %5d       %5d       %5d     %6d px%s"
          % (t, what.get(t, t), sum(1 for (r, f) in A[t].values() if r.get("tipFound") == "1"),
             offboard(t), other_piece(t), floor_bound(t), g,
             "" if k is None else "  (dart %s cam %s)" % k))

print()
print("  ==== the pins are live, so the rows above are four binaries' worth of behaviour ==")
for t in arms[1:]:
    moved = sum(1 for k in A[arms[0]]
                if A[t].get(k, (None, None))[0] is None
                or A[t][k][0].get("tip") != A[arms[0]][k][0].get("tip"))
    say("    %s  %s moved %d of %d readings against %s"
        % ("OK  " if moved > 0 else "FAIL", t, moved, len(A[arms[0]]), arms[0]), moved > 0)

print()
print("  ==== #1495: a second object can no longer win the tip ==========================")
print("    The published tip left the contour its centroid was measured from by this far.")
print("    It is exact, it needs no threshold, and it is the distance #1492 called 'the")
print("    line crossing empty space'.")
gAA, kAA = worst_gap("AA")
gBB, kBB = worst_gap("BB")
say("    %s  the longest stretch of empty space a published line ran across falls from "
    "%d px (dart %s cam %s) to %d px (dart %s cam %s)"
    % ("OK  " if gBB < gAA else "FAIL", gAA, kAA[0], kAA[1], gBB, kBB[0], kBB[1]),
    gBB < gAA)

print()
print("  ==== #1494: the dart's own pieces are back in the figure ========================")
# A reading whose figure grew -- more contours in it than the floor admitted -- with the
# tip moving further from the centroid as a result. That is the fragmentation repair
# happening, counted rather than asserted at a number.
grew = 0
for k, (r, f) in A["BB"].items():
    fa = A["AA"].get(k, (None, None))[1]
    if f is None or fa is None:
        continue
    if int(f.get("pieces", 0)) > int(fa.get("pieces", 0)) and float(f["maxdist"]) > float(fa["maxdist"]):
        grew += 1
say("    %s  %d readings hold more of their own dart than the 400 px floor left them, and "
    "reach further from the centroid for it" % ("OK  " if grew > 0 else "FAIL", grew), grew > 0)

print()
print("  ==== neither repair was traded for the other ====================================")
print("    The half-arms are what says so: AB is #1495 alone and BA is #1494 alone, so a")
print("    repair that only works because the other one hides its cost is visible here.")
say("    %s  off-board readings: AA %d, AB %d, BA %d, BB %d -- the shipping arm is no worse "
    "than the tree #1492 measured"
    % ("OK  " if offboard("BB") <= offboard("AA") else "FAIL",
       offboard("AA"), offboard("AB"), offboard("BA"), offboard("BB")),
    offboard("BB") <= offboard("AA"))
say("    %s  the worst tipGap: AA %d, AB %d, BA %d, BB %d px -- neither half-arm is better "
    "than both together"
    % ("OK  " if worst_gap("BB")[0] <= min(worst_gap(t)[0] for t in arms) else "FAIL",
       worst_gap("AA")[0], worst_gap("AB")[0], worst_gap("BA")[0], worst_gap("BB")[0]),
    worst_gap("BB")[0] <= min(worst_gap(t)[0] for t in arms))

print()
print("  ==== the two populations, on the shipping arm ===================================")
for t in arms:
    s = S[t]
    print("    %-4s on-board darts %s worst %s mm | off-board darts %s best %s mm | off-board readings %s"
          % (t, s.get("onpop", "?"), s.get("onworst", "?"), s.get("offpop", "?"),
             s.get("offbest", "?"), s.get("offboard", "?")))
ow, ob = float(S["BB"].get("onworst", -1)), float(S["BB"].get("offbest", -1))
say("    %s  the two populations are%s disjoint on the shipping arm (%.1f mm against %.1f mm)"
    % ("OK  " if 0 < ow < ob else "FAIL", "" if 0 < ow < ob else " NOT", ow, ob), 0 < ow < ob)

sys.exit(FAILED)
