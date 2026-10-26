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
#   A  `aVoteIsCast` loses the pin gate (a surround MISS always votes). PREDICTED:
#      fresh-mode unit 2 reds (the abstention claim and the phantom-publishes control),
#      votes-mode 0, and the probe's "the tree reproduces today's census" claim red.
#      MEASURED: exactly that -- fresh 2, votes 0, probe red on that claim. (A's FIRST
#      run also tripped a same-events claim this harness no longer makes: the replay
#      lost visit 6's marginal dart between the two runs, which is detection variance
#      and not a vote finding, and the comparison was made per-visit because of it.)
#   B  scorePoint loses the rim bound (every outside tip is on_surround). PREDICTED:
#      fresh-mode unit 1 red (beyond-rim not on_surround; the abstention claim stays
#      green there because the pin is off), votes-mode 2 reds (that claim plus
#      "abstains in every mode"; the beyond-rim chooseScore scenario is built from
#      constructed readings and cannot see this plant), and the probe's pinned run
#      flips darts 8 and 13 -- the CORRECT S7 and S15 -- so the outside-visit-6 claim
#      goes red, which is the refusal's own mechanism demonstrated live. MEASURED:
#      exactly that -- fresh 1, votes 2, probe red naming darts 8 and 13.
#   C  `surroundMissesVote` never fires (the pin is dead). PREDICTED: fresh-mode 0,
#      votes-mode 2 reds (the pin claim, and {surround MISS, S7} whose votes are
#      computed through aVoteIsCast; the other scenarios pass literal vote flags and
#      cannot see it), and the probe's "under the pin the dart publishes MISS" claim
#      red. MEASURED: exactly that -- fresh 0, votes 2, probe red on that claim.
#
# Each plant flips its own half and no plant is caught by everything, which is what says
# the claims are load-bearing rather than decorative.
#
# turnaus#1864 OVERTOOK visit 6's half of the story above. Since #1682's entry threshold
# the miss is its own event and publishes MISS, and the S7 #1505 read as a phantom was the
# thrown 7 in the miss's slot (the census claim below has the measurement). The probe's two
# visit-6 claims were rewritten to today's census; the proof above was run against the old
# wording and was not re-run for the new.

RIG_DIR=/app/mocks/rig-20260918
CLIPS="$RIG_DIR/cam_1.mp4 $RIG_DIR/cam_2.mp4 $RIG_DIR/cam_3.mp4"
OUT=/run1505
# turnaus#1863: the probe's motion clock is the footage's. OD_WINDOW_UNIT=cycles (tester_paths'
# docker wrapper, #1826) takes the WINDOWS off the wall clock and leaves the cooldown, the spike
# window, the safety timeout and the body-window vote on it, so until #1863 this harness ran
# those four timers on wall time -- 1691 cycles in ~36 s on this box where the footage plays
# them in 56 -- and a loaded box cut them differently again. docs/rig.md's #1688 paragraph said
# "1535's capture-clock probe found no darts": that probe never wound the clock, so it ran on
# one stuck at 0. The edge probe winds it now (testers/replay_clock.hpp), and measured
# 2026-10-14 on a quiet box the capture-clock replay published the same darts, visit for visit
# and camera for camera, as the wall-clock one -- so this pin moves no figure, it only takes
# the box's load out of them. 1838-replay-noise holds that claim.
export OD_MOTION_CLOCK=capture

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
# synchronously off the clips. It is NOT fully deterministic -- measured on this box:
# five runs of one binary read the clip 19-19-19-18-19 darts, the odd run losing
# exactly visit 6's second dart, which is GROUND-TRUTH.md's marginal dart -- so the
# comparison below aligns per VISIT (the boundaries were identical in every run) and
# compares dart for dart only as far as both runs detected, reporting a count mismatch
# as the detection variance it is rather than as a vote finding.
$OUT/edge_probe "" $CLIPS > $OUT/probe-tree.txt 2> $OUT/probe-tree.err
RC1=$?
OD_SURROUND=votes $OUT/edge_probe "" $CLIPS > $OUT/probe-pinned.txt 2> $OUT/probe-pinned.err
RC2=$?
[ $RC1 -eq 0 ] && [ $RC2 -eq 0 ] || {
  tail -10 $OUT/probe-tree.err $OUT/probe-pinned.err
  say "FAIL a probe run did not reach the end of the footage (rc $RC1/$RC2)" no
}

# turnaus#1863: the probe ran on the capture clock and WOUND it -- motionMs at its end is the
# footage's length rather than 0 (1838-replay-noise's first check). A probe that stopped calling
# replay_clock::advance would run its cooldown and spike window on a clock stuck at 0 -- and
# nothing below would see it: measured 2026-10-14 (#1863's mutation proof, the spread census
# with this call planted out), the stuck clock still scored this fixture's 21 darts, 63 readings
# identical, because the cycle-pinned windows decide every dart here. Only this line names it.
clock_wound() {
  local end cycles clock ms played
  end=$(grep '^I1505END ' "$1")
  cycles=$(echo "$end" | sed 's/.*cycles=\([0-9]*\).*/\1/')
  clock=$(echo "$end" | sed 's/.*motionClock=\([a-z]*\).*/\1/')
  ms=$(echo "$end" | sed 's/.*motionMs=\([0-9]*\).*/\1/')
  played=$(( ${cycles:-0} * 1000 / 30 ))
  if [ "$clock" = capture ] && [ "${ms:-0}" -ge "$played" ] && [ "${ms:-0}" -le $(( played + 5000 )) ]; then
    say "OK   $2 ran on the capture clock and wound it: $cycles cycles, motionMs=$ms (the $played ms they play at 30 fps, plus up to 5000 for the calibration seek and average)" ok
  else
    say "FAIL $2's motion clock was not the wound capture clock (motionClock=$clock motionMs=$ms over $cycles cycles) -- the probe is not calling replay_clock::advance, so its timers ran on a clock that never moved" no
  fi
}
clock_wound $OUT/probe-tree.txt "the tree run"
clock_wound $OUT/probe-pinned.txt "the pinned run"

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
say(t_ends == p_ends and len(tree) == len(pinned),
    "the two runs saw the same VISITS, so they can be aligned visit for visit")
for v in range(min(len(tree), len(pinned))):
    if len(tree[v]) != len(pinned[v]):
        print("NOTE visit %d was read with %d darts by one run and %d by the other -- "
              "the marginal-dart detection variance GROUND-TRUTH.md records, not a vote "
              "finding; the comparison covers the darts both runs detected"
              % (v + 1, len(tree[v]), len(pinned[v])))
if failed:
    sys.exit(failed)

# The issue's dart, and today's census, reproduced rather than remembered. Visit 6 is
# thrown miss, 7, 2 (GROUND-TRUTH.md), the miss resting in the number ring off the board.
#
# THE EARLIER CENSUS, and what it really was (turnaus#1864, measured 2026-10-26 on fork
# main bf98151, capture clock, cycle windows). Until #1682 lowered OD_SPIKE_THRESHOLD from
# 0.011 to 0.006 this row asserted that visit 6's first dart published S7@0.70, and #1505
# called it a parallax phantom: camera 1 read the miss at 215 mm, on the surround, and
# camera 3 read 150 mm, inside the board. Re-run with OD_SPIKE_THRESHOLD=0.011 alone, the
# clip reads 20 darts and visit 6 TWO: the miss opens no event of its own, and the first
# event (cycle 1439) is the THROWN 7 -- camera 3's tip is (488,457), the pixel it reports
# for the 7 at that same cycle on main, and camera 1's 215 mm tip is the miss, still in its
# diff because no event had taken it. So S7@0.70 was the 7, correct, published in the
# miss's slot: #1504's misalignment, not a wrong score. On main the lower entry opens the
# miss's own event (cycle 1386, no camera finds a tip, MISS@0.50 by default), visit 6
# reads MISS, S7, S2 -- the ground truth -- and the clip reads 21: the 20 arrivals plus
# visit 7's third (cycle 1653), which both thresholds publish.
ok = len(tree) >= 6 and len(pinned) >= 6 and tree[5] and pinned[5]
if not ok:
    say(False, "visit 6 is missing from a run, so the issue's own dart cannot be judged")
    sys.exit(failed)
t6 = [d[1] for d in tree[5]]
say(t6 == ["MISS", "S7", "S2"],
    "the tree reproduces today's census: visit 6 publishes %s for the thrown miss, 7, 2 "
    "-- the off-board dart is its own event and publishes MISS (#1682's entry threshold); "
    "the earlier census, S7@0.70 in its slot, was the thrown 7 with the miss unseen"
    % ", ".join("%s@%s" % (d[1], d[2]) for d in tree[5]))
p6 = [d[1] for d in pinned[5]]
say(len(p6) == 3 and p6[0] == "MISS" and p6[2] == "MISS",
    "under OD_SURROUND=votes visit 6 publishes %s: the pin has no phantom left to repair, "
    "and what it reaches is the CORRECT 2 (camera 2's surround reading of it votes) -- "
    "the repair mechanism is live, and #1505's refusal of it still holds on today's tree"
    % ", ".join("%s@%s" % (d[1], d[2]) for d in pinned[5]))

# Every difference the pin makes is a dart it publishes as MISS: the pin admits one
# reading class and changes nothing else. Aligned per visit, as far as both runs
# detected.
changed = [(v, a, b) for v in range(min(len(tree), len(pinned)))
           for a, b in zip(tree[v], pinned[v]) if a[1] != b[1]]
say(all(b[1] == "MISS" for v, a, b in changed),
    "every dart the two rules disagree on became MISS under the pin (%s)"
    % ", ".join("dart %d %s->%s@%s" % (a[0], a[1], b[1], b[2]) for v, a, b in changed))

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
