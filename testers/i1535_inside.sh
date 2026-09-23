set -u
# #1535, inside the container. Everything here runs against /app, which is the tree, and
# writes to /run1535. It never ends on an `echo` (#1479): the last statement is `exit`.
#
# THE STATE THIS IS ABOUT. rig-20260918, visit 4, third dart -- thrown OFF the board,
# published S20@0.9 whenever its detection-marginal event fires. #1505 measured how: no
# camera saw the dart where it is. Camera 1 found nothing; camera 2's "tip" for the new
# dart was the PREVIOUS dart's tip, 2.2 px from where it had already reported it, with
# the fresh diff's real change 82 px away (I1492TIP tipGap=82); camera 3's was the
# off-board dart parallax-projected inside the top edge. Two "S20" strings agreed, and
# agreement earns 0.9. #1505 also measured that no vote rule can reach it -- the vote
# holds no MISS reading to admit -- and refused the vote-side repair on the acceptance
# criterion (it broke visit 3's correct S7, 1:1; the record is at aVoteIsCast under
# OD_SURROUND=votes). The separator is which OBJECT the tip was found on: the tip
# machinery's question, asked by #1535.
#
# WHAT #1535 SHIPS. `isAReReportOfAnEarlierTip` (pure, inline, dart_processing.hpp): a
# camera whose "new" tip lies within 12 px of a tip it already reported for an earlier
# dart of this visit, while the fresh figure's nearest point is 40+ px away, is
# re-reporting -- it abstains (`tip_found` stays false) for the new dart. The memory is
# per camera, appended when the VOTE accepts a dart (#1495's sentence) and cleared at
# the reconciled CLEAN beside the working backgrounds (#1349). The thresholds' census --
# both fixtures, denominators, the needle -- is in the rule's own docblock.
# OD_TIP_IDENTITY=off restores the unguarded machinery, so before/after is two runs of
# one binary (od_fix's shape).
#
# MUTATION PROOF: see the recorded runs in the #1535 report comment -- each plant's
# prediction stated before its run, each plant flipping its own claims and no plant
# caught by everything.

RIG_DIR=/app/mocks/rig-20260918
CLIPS="$RIG_DIR/cam_1.mp4 $RIG_DIR/cam_2.mp4 $RIG_DIR/cam_3.mp4"
OUT=/run1535

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

# ---- preconditions, ASSERTED rather than relied on (#1449's guard) ----------------------
if [ ! -s $RIG_DIR/cam_1.mp4 ] || [ ! -s $RIG_DIR/cam_2.mp4 ] || [ ! -s $RIG_DIR/cam_3.mp4 ]; then
  echo "FAIL the rig fixture is not whole ($RIG_DIR); it is the only footage whose real"
  echo "     darts are recorded (#1478), so there is nothing to judge this issue on."
  exit 1
fi

echo "=== the rule, held to its own census, both modes of one binary ==="
g++ -std=c++17 -O1 -I /app/src -I /app/src/utils -I /app/src/detector/geometry/calibration \
  -o $OUT/identity_check /app/testers/i1535_identity_check.cpp \
  /app/src/detector/geometry/calibration/*.cpp \
  /app/src/detector/geometry/detection/score_processing.cpp \
  /app/src/detector/geometry/detection/dart_processing.cpp \
  /app/src/detector/geometry/detection/motion_processing.cpp \
  $(pkg-config --cflags --libs opencv4) > $OUT/ic-build.log 2>&1
if [ $? -ne 0 ]; then
  tail -30 $OUT/ic-build.log
  echo "FAIL i1535_identity_check.cpp did not compile; nothing below measures anything"
  exit 2
fi
$OUT/identity_check fresh
[ $? -eq 0 ] && say "OK   every fresh-mode claim held" ok \
             || say "FAIL a fresh-mode claim did not hold -- its own words are above" no
OD_TIP_IDENTITY=off $OUT/identity_check pinned
[ $? -eq 0 ] && say "OK   the pin is alive" ok \
             || say "FAIL the pin claim did not hold -- its own words are above" no

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

# The probe replays the detector's own stages synchronously over the whole clip. It is
# NOT fully deterministic (#1505 measured 19-19-19-18-19 darts over five runs, the odd
# run losing GROUND-TRUTH.md's marginal dart), so the comparison aligns per VISIT and
# judges visit 4's third dart only when a run detected it -- reporting a miss of that
# marginal event as the detection variance it is, not as a rule finding.
# A run that lost the marginal event is re-run once, saying so: the claims below are
# about the vote's behaviour on the event, not about whether a marginal event fired.
visit4_whole() {
  python3 - "$1" <<'V4'
import re, sys
visit, darts = 1, 0
for line in open(sys.argv[1], errors="replace"):
    if line.startswith("I1505VISITEND"):
        visit += 1
    elif line.startswith("I1505VOTE") and visit == 4:
        darts += 1
sys.exit(0 if darts >= 3 else 1)
V4
}
probe_run() { # $1 out-prefix, $2.. env pin
  env "$@" $OUT/edge_probe "" $CLIPS > "$OUT/$1.txt" 2> "$OUT/$1.err"
  local rc=$?
  if [ $rc -eq 0 ] && ! visit4_whole "$OUT/$1.txt"; then
    echo "NOTE $1: visit 4's third event was not detected (the marginal-event variance"
    echo "     GROUND-TRUTH.md records) -- re-running once so the claims judge the vote"
    env "$@" $OUT/edge_probe "" $CLIPS > "$OUT/$1.txt" 2> "$OUT/$1.err"
    rc=$?
  fi
  return $rc
}
probe_run probe-tree OD_TIP_IDENTITY=
RC1=$?
probe_run probe-pinned OD_TIP_IDENTITY=off
RC2=$?
[ $RC1 -eq 0 ] && [ $RC2 -eq 0 ] || {
  tail -10 $OUT/probe-tree.err $OUT/probe-pinned.err
  say "FAIL a probe run did not reach the end of the footage (rc $RC1/$RC2)" no
}

python3 - $OUT/probe-tree.txt $OUT/probe-pinned.txt <<'PY'
import re, sys

def read(path):
    visits, ends, cams = [[]], [], {}
    for line in open(path, errors="replace"):
        m = re.match(r"I1505CAM dart=(\d+) cycle=\d+ state=\S+ cam=(\d+) tipFound=(\d) tip=([-\d.]+),([-\d.]+)", line)
        if m:
            cams.setdefault(int(m.group(1)), {})[int(m.group(2))] = (
                int(m.group(3)), float(m.group(4)), float(m.group(5)))
            continue
        m = re.match(r"I1505VOTE dart=(\d+) published=(\S+) confidence=(\S+) camera=\S+ replayAgrees=\d agreeing=(\d+)", line)
        if m:
            visits[-1].append((int(m.group(1)), m.group(2), float(m.group(3)), int(m.group(4))))
            continue
        m = re.match(r"I1505VISITEND cycle=(\d+)", line)
        if m:
            visits.append([])
            ends.append(int(m.group(1)))
    if visits and not visits[-1]:
        visits.pop()
    return visits, ends, cams

tree, t_ends, t_cams = read(sys.argv[1])
pinned, p_ends, p_cams = read(sys.argv[2])
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

# THE ISSUE'S DART. Visit 4's third event, when detected: under the pin (the old
# machinery) camera 2 re-reports the previous dart's tip -- within a few px of a tip it
# already reported earlier in visit 4 -- and votes with it; whether that false witness
# earns 0.9 turns on where camera 3's parallax projection happens to land (S20 agrees,
# T20 does not -- both were measured across runs), so the claims below pin the
# MECHANISM, which is deterministic, and the tree-rule confidence, which can no longer
# reach 0.9 either way. The 0.9 itself is #1505's record and this issue's report quote.
p_has = len(pinned) >= 4 and len(pinned[3]) >= 3
t_has = len(tree) >= 4 and len(tree[3]) >= 3
if p_has:
    d = pinned[3][2]
    cam2 = p_cams.get(d[0], {}).get(2)
    earlier = [p_cams.get(e[0], {}).get(2) for e in pinned[3][:2]]
    near = min((((cam2[1] - e[1]) ** 2 + (cam2[2] - e[2]) ** 2) ** 0.5)
               for e in earlier if e and e[0] == 1) if cam2 and cam2[0] == 1 else None
    say(near is not None and near <= 12.0,
        "under OD_TIP_IDENTITY=off visit 4's off-board third dart is given a camera-2 "
        "\"tip\" %s px from a tip that camera already reported for an earlier dart of "
        "the visit -- the false witness, restorable on this binary (it published %s@%.1f)"
        % ("%.1f" % near if near is not None else "?", d[1], d[2]))
else:
    print("NOTE visit 4's third event was not detected in the pinned run (it is marginal; "
          "GROUND-TRUTH.md records the variance) -- the needle claims cannot be judged "
          "on this run; re-run the harness")
    say(False, "the pinned run detected visit 4's third event")
if t_has:
    d = tree[3][2]
    say(d[2] <= 0.71,
        "under the tree rule the same dart publishes %s@%.1f with %d agreeing -- the "
        "re-reporting camera abstained, so two witnesses cannot form (#1535)" % (d[1], d[2], d[3]))
    cam2 = t_cams.get(d[0], {}).get(2)
    say(cam2 is not None and cam2[0] == 0,
        "and its camera 2 abstained (tipFound=%s) -- the abstention is the mechanism, "
        "shown not inferred" % (cam2 and cam2[0]))
else:
    print("NOTE visit 4's third event was not detected in the tree run (detection "
          "variance, not the rule: the rule cannot remove an EVENT -- the state vote "
          "reads fresh_share, never tip_found)")

# NOTHING RIGHT GOES WRONG, on the darts both runs detected: outside visit 4's third
# dart, the two rules publish identical scores at identical confidences.
changed = []
for v in range(min(len(tree), len(pinned))):
    for k, (a, b) in enumerate(zip(tree[v], pinned[v])):
        if (v, k) == (3, 2):
            continue
        if a[1] != b[1] or abs(a[2] - b[2]) > 0.01:
            changed.append((v + 1, a, b))
say(not changed,
    "outside visit 4's third dart the two rules agree dart for dart"
    + ("" if not changed else " -- THEY DO NOT: "
       + ", ".join("v%d dart %d %s@%.1f vs %s@%.1f" % (v, a[0], a[1], a[2], b[1], b[2])
                   for v, a, b in changed)))
sys.exit(failed)
PY
PROBE_RC=$?
[ $PROBE_RC -eq 0 ] || FAILED=1

echo
echo "CHECK_RC=$FAILED"
# The harness exits on what it measured: run_all.sh reads the exit code and nothing
# else, and an echo returns 0 whatever it printed (#1335, #1463, #1479).
exit $FAILED
