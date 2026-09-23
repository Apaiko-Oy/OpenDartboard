set -u
# #1505, inside the container. Everything here runs against /app, which is the tree, and
# writes to /run1505. It never ends on an `echo` (#1479): the last statement is `exit`.
#
# THE STATE THIS IS ABOUT. rig-20260918 holds two thrown misses, and until #1505 the two
# behaved completely differently for a reason nobody had measured. The edge probe
# (i1505_edge_probe.cpp) measured it: both darts came to rest OUT OF THE BOARD PLANE on
# the surround, and a tip that is not in the board plane projects to a different board
# radius from every camera. Visit 6's miss was measured by camera 1 at 215 mm -- on the
# surround, an honest MISS -- and by camera 3 at 150 mm, twenty millimetres INSIDE the
# board; the vote rule (`may_vote = tip_found && score != "MISS"`) silences the honest
# witness, so the parallax reading stands alone and a dart that never hit the board
# publishes S7 at 0.7 -- the census's one on-board error. Visit 4's miss was seen by NO
# camera where it is: camera 1 found nothing, camera 2's tip was the PREVIOUS dart's tip
# (2 px from its own previous reading -- the #1494/#1495 family, another slice's
# machinery) and camera 3's was the off-board dart projected just inside the top edge,
# so when that detection-marginal event fires at all, two cameras agree on S20 at 0.9
# about a dart neither saw on the board. The radial ruler's edge was REFUTED as the
# mechanism for either: the phantom readings sit 20..51 mm inside the edge where the
# bloom (#1510's held-out residuals) is 2..4 mm.
#
# WHAT #1505 SHIPS: the measurement (PointScore::on_surround, the edge probe) and the
# REFUSAL of the repair anybody would reach for. Letting the surround MISS vote repairs
# visit 6's phantom -- and, measured on the real binary over the whole clip, the SAME
# run flipped visit 3's second dart, a thrown 7 scored CORRECTLY as S7, to MISS off a
# flight artifact at ruler radius 1.177 (the honest witness reads 1.147; no radius
# separates them, and the artifact's placement flickers with the detection windows: this
# synchronous replay read the same flight at 1.44 and refused it). One dart repaired,
# one correct dart broken, in one run -- the acceptance criterion (nothing that is
# right may go wrong) refuses the trade, and OD_SURROUND=votes is the pin that
# re-measures it (i1492's shape for a refused repair). What separates the two readings
# is WHICH OBJECT the tip was found on: the tip machinery's question, not the vote's.
#
# MUTATION PROOF, run 2026-09-23 on this box, predictions stated before each run:
#
#   A  `aVoteIsCast` loses the pin gate (a surround MISS always votes). PREDICTED: the
#      fresh-mode unit run fails its two tree-rule claims (the surround MISS votes, so
#      both the abstention claim and the phantom-publishes control go red -- 2 reds)
#      and the harness's "the tree reproduces today's census" probe claim goes red.
#      MEASURED: fresh unit 2 reds, votes unit 0 reds, probe claim red as predicted.
#   B  scorePoint loses the rim bound (every outside tip is on_surround). PREDICTED:
#      the two beyond-rim unit claims go red in BOTH modes (2 reds apiece); the probe
#      halves survive, because on this replay's windows every artifact still loses by
#      index or by abstention elsewhere -- the plant is caught by the unit half, which
#      is why the unit half exists. MEASURED: fresh unit 2 reds, votes unit 3 reds
#      (the beyond-rim chooseScore scenario turns with them), probe green.
#   C  `surroundMissesVote` never fires (the pin is dead). PREDICTED: the votes-mode
#      unit fails its pin claim and every chooseScore claim that publishes a MISS
#      (3 of them -- {MISS,S7}, {MISS,MISS}, and the pin claim itself makes 5 with the
#      {S7,MISS} scenario still green), and the probe's pinned run is identical to the
#      tree's, so the "pin repairs visit 6's dart" claim goes red. MEASURED: votes unit
#      3 reds (pin claim, {surround MISS,S7}, {MISS,MISS}), fresh unit 0, probe claim
#      red as predicted.
#
# Each plant flips its own half and no plant is caught by everything, which is what says
# the claims are load-bearing rather than decorative.

RIG_DIR=/app/mocks/rig-20260918
CLIPS="$RIG_DIR/cam_1.mp4 $RIG_DIR/cam_2.mp4 $RIG_DIR/cam_3.mp4"
OUT=/run1505

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

# ---- preconditions, ASSERTED rather than relied on (#1449's guard) ----------------------
if [ ! -s $RIG_DIR/cam_1.mp4 ] || [ ! -s $RIG_DIR/cam_2.mp4 ] || [ ! -s $RIG_DIR/cam_3.mp4 ]; then
  echo "FAIL the rig fixture is not whole ($RIG_DIR); it is the only footage whose real"
  echo "     darts are recorded (#1478), so there is nothing to judge this issue on."
  exit 1
fi

echo "=== the decisions, on a synthetic board, both modes of one binary ==="
g++ -std=c++17 -O1 -I /app/src -I /app/src/utils -I /app/src/detector/geometry/calibration \
  -o $OUT/vote_check /app/testers/i1505_vote_check.cpp \
  /app/src/detector/geometry/calibration/*.cpp \
  /app/src/detector/geometry/detection/score_processing.cpp \
  /app/src/detector/geometry/detection/dart_processing.cpp \
  /app/src/detector/geometry/detection/motion_processing.cpp \
  $(pkg-config --cflags --libs opencv4) > $OUT/vc-build.log 2>&1
if [ $? -ne 0 ]; then
  tail -30 $OUT/vc-build.log
  echo "FAIL i1505_vote_check.cpp did not compile; nothing below measures anything"
  exit 2
fi
$OUT/vote_check fresh
[ $? -eq 0 ] && say "OK   every tree-rule claim held" ok \
             || say "FAIL a tree-rule claim did not hold -- its own words are above" no
$OUT/vote_check votes
[ $? -eq 0 ] && say "OK   every pinned-mode claim held" ok \
             || say "FAIL a pinned-mode claim did not hold -- its own words are above" no

echo
echo "=== the fixture, both rules on ONE binary (od_fix's shape) ==="
g++ -std=c++17 -O1 -I /app/src -I /app/src/utils -I /app/src/detector/geometry/calibration \
  -o $OUT/edge_probe /app/testers/i1505_edge_probe.cpp \
  /app/src/detector/geometry/calibration/*.cpp \
  /app/src/detector/geometry/detection/score_processing.cpp \
  /app/src/detector/geometry/detection/dart_processing.cpp \
  /app/src/detector/geometry/detection/motion_processing.cpp \
  $(pkg-config --cflags --libs opencv4) > $OUT/ep-build.log 2>&1
if [ $? -ne 0 ]; then
  tail -30 $OUT/ep-build.log
  echo "FAIL i1505_edge_probe.cpp did not compile; the fixture half cannot run"
  exit 2
fi

# The probe replays the detector's own calibration, motion, dart and scoring stages
# synchronously off the clips, so unlike the paced binary it is deterministic: the same
# tree read it twice to the dart while the binary's three runs read 18-18-17
# (GROUND-TRUTH.md's marginal-dart note). Determinism is what lets the two rules be
# compared dart for dart, and it is asserted below rather than assumed.
$OUT/edge_probe "" $CLIPS > $OUT/probe-tree.txt 2> $OUT/probe-tree.err
RC1=$?
OD_SURROUND=votes $OUT/edge_probe "" $CLIPS > $OUT/probe-pinned.txt 2> $OUT/probe-pinned.err
RC2=$?
[ $RC1 -eq 0 ] && [ $RC2 -eq 0 ] || {
  tail -10 $OUT/probe-tree.err $OUT/probe-pinned.err
  say "FAIL a probe run did not reach the end of the footage (rc $RC1/$RC2)" no
}

python3 - $OUT/probe-tree.txt $OUT/probe-pinned.txt <<'PY'
import re, sys

def read(path):
    visits, ends = [[]], []
    for line in open(path, errors="replace"):
        m = re.match(r"I1505VOTE dart=(\d+) published=(\S+) confidence=(\S+)", line)
        if m:
            visits[-1].append((int(m.group(1)), m.group(2), m.group(3)))
            continue
        m = re.match(r"I1505VISITEND cycle=(\d+)", line)
        if m:
            visits.append([])
            ends.append(int(m.group(1)))
    if visits and not visits[-1]:
        visits.pop()
    return visits, ends

tree, t_ends = read(sys.argv[1])
pinned, p_ends = read(sys.argv[2])
failed = 0
def say(ok, what):
    global failed
    print(("OK   " if ok else "FAIL ") + what)
    if not ok:
        failed += 1

t_darts = [d for v in tree for d in v]
p_darts = [d for v in pinned for d in v]
say(len(t_darts) >= 15 and len(p_darts) >= 15,
    "both runs read the fixture (tree %d darts, pinned %d)" % (len(t_darts), len(p_darts)))
say(len(t_darts) == len(p_darts) and t_ends == p_ends,
    "the two runs saw the SAME events, so they can be compared dart for dart -- the "
    "probe is a synchronous replay and the rule under test cannot change detection")
if failed:
    sys.exit(failed)

# The issue's dart, and today's census, reproduced rather than remembered: visit 6's
# first dart is thrown OFF the board (GROUND-TRUTH.md) and the tree publishes an
# on-board single for it -- the census's one on-board error, kept KNOWINGLY (the
# refusal in aVoteIsCast), and this claim is what notices if the tip machinery's
# repair ever lands and this harness's story goes stale.
ok = len(tree) >= 6 and len(pinned) >= 6 and tree[5] and pinned[5]
if not ok:
    say(False, "visit 6 is missing from a run, so the issue's own dart cannot be judged")
    sys.exit(failed)
t0, p0 = tree[5][0], pinned[5][0]
say(t0[1].startswith("S"),
    "the tree reproduces today's census: visit 6's off-board dart publishes %s@%s -- "
    "the one on-board error, kept knowingly; if this is now MISS, the tip machinery "
    "has moved and #1505's refusal should be re-measured" % (t0[1], t0[2]))
say(p0[1] == "MISS",
    "under OD_SURROUND=votes the same dart publishes MISS at %s -- the repair "
    "mechanism is real, which is what makes the refusal a measurement and not a "
    "shrug" % p0[2])

# Every difference the pin makes is a dart it publishes as MISS: the pin admits one
# reading class and changes nothing else.
changed = [(a, b) for a, b in zip(t_darts, p_darts) if a[1] != b[1]]
say(all(b[1] == "MISS" for a, b in changed),
    "every dart the two rules disagree on became MISS under the pin (%s)"
    % ", ".join("dart %d %s->%s@%s" % (a[0], a[1], b[1], b[2]) for a, b in changed))

# In THIS replay's windows the trade does not show (the flight artifact lands beyond
# the rim here and abstains); on the real binary's windows it flipped visit 3's
# correct S7. That difference IS the refusal's second half: the artifact's placement
# is detection noise, so a correct dart's verdict would flicker run to run.
regressed = [(a, b) for v in (0, 1, 2, 3, 4, 6) if v < len(tree) and v < len(pinned)
             for a, b in zip(tree[v], pinned[v]) if a[1] != b[1]]
say(not regressed,
    "in this replay the pin changes nothing outside visit 6"
    + ("" if not regressed else " -- IT DOES: "
       + ", ".join("dart %d %s->%s" % (a[0], a[1], b[1]) for a, b in regressed))
    + "; the real binary's windows DID flip visit 3's correct S7, which is the refusal")

# Said rather than asserted: the OTHER off-board throw. When visit 4's third event is
# detected at all (it is detection-marginal), two cameras agree on S20 about a dart
# neither saw on the board -- one tip is the previous dart's, one is the off-board
# dart projected inside the top edge. No reading in that vote is a MISS, so no vote
# rule can reach it; its repair is tip identity, another slice's machinery.
if len(tree) >= 4 and len(tree[3]) >= 3:
    d = tree[3][2]
    print("NOTE visit 4's third event was detected in this replay and published %s@%s -- "
          "measured, unfixed, and named in the report" % (d[1], d[2]))
else:
    print("NOTE visit 4's third event was not detected in this replay (it is marginal; "
          "the real binary's census of 2026-09-23 missed it too)")
sys.exit(failed)
PY
PROBE_RC=$?
[ $PROBE_RC -eq 0 ] || FAILED=1

echo
echo "CHECK_RC=$FAILED"
# The harness exits on what it measured: run_all.sh reads the exit code and nothing
# else, and an echo returns 0 whatever it printed (#1335, #1463, #1479).
exit $FAILED
