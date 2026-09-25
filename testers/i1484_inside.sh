set -u
# #1484: a run reports HOW its darts were scored, not only what they scored.
#
# THE STATE THIS IS ABOUT. `chooseScore` has decided this since #1346 and nothing reads
# it. 0.9 is two or more cameras that MEASURED a wedge and agreed; 0.7 is measured with no
# two agreeing, so the published score is `measured[0]` -- first by array index rather than
# by merit; 0.5 is `by_default`, meaning no camera measured a wedge at all and #1346's
# fallback asserted the 20. Those three separate A WRONG SCORE from NO ANCHOR AT ALL, and
# from outside the two are indistinguishable. Every question asked of this detector needed
# a hand-built probe and an hour of reading logs; this makes it a command.
#
# IT IS A MEASUREMENT AND NOT A CHECK. Nothing below asserts anything about a number the
# detector produced. A threshold here would be a constant fitted to today's tree, which is
# #1322's mistake, and it would be fitted to a tree everybody already knows is wrong: not
# one score on the rig fixture is correct. What this harness DOES fail on is a run it could
# not read -- a missing binary, a fixture that is not there, a run that never reached the
# scoring loop, a ground-truth table it cannot parse. Those are the states in which a
# census of nought reads exactly like a census.
#
# TWO FIXTURES, AND THEY STOP FOR THE TWO DIFFERENT REASONS ON PURPOSE.
#
#   RIG    mocks/rig-20260918, run with NO cycle budget, so it ends where the footage
#          ends and the census covers the WHOLE clip. It is the only footage in this
#          repository whose real darts are recorded, so it is the only one the accuracy
#          half can be asked of.
#   MOCKS  the shipped mocks, run under a cycle budget, so it ends on the cap. Its figures
#          carry #1478's caveat -- every calibration constant in this repository was fitted
#          against this footage, so a good result here is circular -- and it is also where
#          the truncation notice is PROVED to fire. A run that ended on the cap is invisible
#          in the output: its last line is an ordinary END, exactly like a visit that
#          finished, which is how the first comparison ever written against the rig ground
#          truth came to report on three sevenths of a fixture and call it the whole thing.
#
# So the two stop reasons are each other's control. A harness in which every run ends the
# same way could print the truncation sentence, or never print it, and be wrong either way
# with nothing to show it.

BIN=/app/build/opendartboard
RIG_DIR=/app/mocks/rig-20260918
RIG=$RIG_DIR/cam_1.mp4,$RIG_DIR/cam_2.mp4,$RIG_DIR/cam_3.mp4
MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4
TRUTH=$RIG_DIR/GROUND-TRUTH.md
CENSUS=/app/testers/i1484_confidence_census.py
RUN=/run1484

# A budget for the run that is supposed to be cut off, and a ceiling for the one that is
# not. The rig run is given no OD_MAX_CYCLES at all so that it ends on the footage; the
# `timeout` below is a bound on a run that is STUCK and never on a run that is merely slow
# (#1317's number, one level down), and a run it cuts off says so -- the census names a
# stop it cannot account for rather than reporting on an unknown share of a clip.
MOCK_CYCLES="${OD_1484_MOCK_CYCLES:-900}"
RIG_CYCLES="${OD_1484_RIG_CYCLES:-0}"
RUN_TIMEOUT="${OD_1484_TIMEOUT:-900}"

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

# ---- preconditions, ASSERTED rather than relied on --------------------------------------
#
# #1449's guard and #1474's, inherited for their reason: a stale binary does not fail
# loudly, it fails as a plausible census. A binary built before #1346 publishes no 0.9 at
# all and the report would read as a finding about the anchor.
if [ ! -x $BIN ]; then
  echo "FAIL no $BIN -- a fresh worktree has none."
  echo "     Build it:  make build   (or run testers/run_all.sh, which builds first)"
  exit 1
fi
NEWEST=$(find /app/src /app/CMakeLists.txt -newer $BIN 2>/dev/null | head -5)
if [ -n "$NEWEST" ]; then
  echo "FAIL $BIN is OLDER than the source it is supposed to be measuring:"
  echo "$NEWEST" | sed 's/^/       /'
  echo "     The census below would be of some other tree. Rebuild and re-run."
  exit 1
fi
# The three confidences are what this harness reports, so the binary must be one that can
# publish all three. Each of the three branches of chooseScore logs its own sentence, and
# a binary carrying only some of them is one the census would report a skewed answer from
# with nothing failing.
for needle in \
  'Consensus score: ' \
  'No consensus, using single camera score: ' \
  "No measured wedge: publishing camera" \
  'wedge by default'; do
  if ! strings $BIN | grep -qF "$needle"; then
    echo "FAIL $BIN does not carry '$needle', so it cannot publish every branch of"
    echo "     chooseScore and a census of it would be a census of a different decision."
    exit 1
  fi
done
# #1584: since #1555 a dart is published by one of two paths, and the census counts each
# under its own heading by the PATH line beside it. A binary that cannot print those
# sentences leaves every dart "path unknown" -- read, but censused under no path.
for needle in \
  'PATH: the geometric entry publishes (' \
  'PATH: DEGRADED -- no geometric entry (' \
  'PATH: the string vote publishes' \
  'BOARD: wedge from the solved entry'; do
  if ! strings $BIN | grep -qF "$needle"; then
    echo "FAIL $BIN does not carry '$needle', so the census cannot say which path"
    echo "     published a dart and would name one it did not read (#1584)."
    exit 1
  fi
done
if [ ! -s $RIG_DIR/cam_1.mp4 ] || [ ! -s $RIG_DIR/cam_2.mp4 ] || [ ! -s $RIG_DIR/cam_3.mp4 ]; then
  echo "FAIL the rig fixture is not whole ($RIG_DIR), and it is the only footage in this"
  echo "     repository whose real darts are recorded. Without it there is no accuracy half."
  exit 1
fi
if [ ! -s /app/mocks/cam_1.mp4 ]; then
  echo "FAIL mocks/cam_1.mp4 is missing, so the second fixture has nothing to read."
  exit 1
fi
if [ ! -s $TRUTH ]; then
  echo "FAIL $TRUTH is missing. The accuracy half would silently become a confidence census"
  echo "     of one fixture, which is not what this harness says it did."
  exit 1
fi
if [ ! -s $CENSUS ]; then
  echo "FAIL $CENSUS is missing; it is the reporter and there is nothing to report with."
  exit 1
fi
echo "--- $BIN carries all three of chooseScore's branches and both publishing paths, and is newer than /app/src ---"

# ---- how long each clip is, so a run can say how much of it it consumed -----------------
LENGTHS=$RUN/clip-lengths.txt
if ! g++ -std=c++17 -O1 -o $RUN/clip_length /app/testers/i1484_clip_length.cpp \
     $(pkg-config --cflags --libs opencv4) 2> $RUN/clip_length.err; then
  echo "FAIL testers/i1484_clip_length.cpp did not compile, so no run below could say what"
  echo "     share of its fixture it consumed -- which is this issue's whole trap."
  sed 's/^/    /' $RUN/clip_length.err | tail -10
  exit 1
fi
if ! $RUN/clip_length $RIG_DIR/cam_1.mp4 $RIG_DIR/cam_2.mp4 $RIG_DIR/cam_3.mp4 \
     /app/mocks/cam_1.mp4 /app/mocks/cam_2.mp4 /app/mocks/cam_3.mp4 > $LENGTHS; then
  echo "FAIL a clip could not be opened to be measured; see $LENGTHS"
fi
echo "--- clip lengths (path frames fps duration_ms) ---"
sed 's/^/    /' $LENGTHS
if awk '$4 == 0 { found = 1 } END { exit !found }' $LENGTHS; then
  say "FAIL a clip measured 0 ms, so the share consumed cannot be stated for it" no
fi

# ---- a run --------------------------------------------------------------------------
# Each fixture calibrates fresh: a run inheriting the previous fixture's cache would be a
# census of geometry fitted to the other footage.
run_fixture() {
  local tag="$1" cams="$2" cycles="$3"
  echo
  echo "############ running the detector on $tag ############"
  rm -rf $RUN/cache $RUN/debug_frames
  local t0 t1
  t0=$(date +%s)
  env OD_MAX_CYCLES="$cycles" timeout "$RUN_TIMEOUT" \
    $BIN --cams "$cams" --width 1280 --height 720 > $RUN/$tag.out 2>&1
  local rc=$?
  t1=$(date +%s)
  sed 's/\x1b\[[0-9;]*m//g' $RUN/$tag.out > $RUN/$tag.txt
  echo "    detector rc=$rc seconds=$((t1 - t0)) OD_MAX_CYCLES=$cycles lines=$(wc -l < $RUN/$tag.txt)"
  if [ $rc -eq 124 ]; then
    say "FAIL $tag: the detector was still running after ${RUN_TIMEOUT}s and was cut off by the" no
    echo "     harness rather than by the footage or by a budget. The census below is of an"
    echo "     unknown share of the fixture, which is exactly the state this issue is about."
  elif [ $rc -ne 0 ]; then
    say "FAIL $tag: the detector exited $rc; 0 is the status of a run that reached the scoring loop" no
  fi
  grep -aE 'SCORING:|Initial calibration completed|did not calibrate' $RUN/$tag.txt | head -4 | sed 's/^/    /'
}

run_fixture rig   "$RIG"   "$RIG_CYCLES"
run_fixture mocks "$MOCKS" "$MOCK_CYCLES"

# ---- the census -------------------------------------------------------------------------
echo
echo "########################################################################"
echo "#  the census: how each dart was scored, and how much clip it came from"
echo "########################################################################"
echo
# #1584: which rule publishes is not assumed. Both runs are made with OD_SCORE_PATH unset,
# which since #1555 is geometry-first -- every dart the geometric entry or, where it
# refused, the string vote as its DEGRADED fallback. The census is told so and reads the
# PATH line of every dart; one that says otherwise is named by the census (PATH-MISMATCH)
# and exits 3, because every heading below it would be describing a path the run did not
# take. Exporting OD_SCORE_PATH=vote into this container is the mutation that shows it.
EXPECT_PATH=geometry-first
python3 $CENSUS --log $RUN/rig.txt --fixture "mocks/rig-20260918 (the rig footage)" \
  --clips "$RIG" --lengths $LENGTHS --truth $TRUTH --expect-path "$EXPECT_PATH"
RIG_RC=$?
[ $RIG_RC -eq 0 ] || FAILED=1

echo
python3 $CENSUS --log $RUN/mocks.txt --fixture "mocks/ (the shipped mocks)" \
  --clips "$MOCKS" --lengths $LENGTHS --expect-path "$EXPECT_PATH" \
  --caveat "every calibration constant in this repository was fitted against this footage (#1478), so a good result here is circular -- and it has no ground truth, so nothing here says whether a score is RIGHT"
MOCK_RC=$?
[ $MOCK_RC -eq 0 ] || FAILED=1

# The mismatch report's own control, on the rig run just read and at no replay's cost: the
# same log censused as if the OTHER rule had published it must be refused by name. A check
# that has never been seen to fire on a real log is one nobody knows can (#1463).
echo
OTHER_PATH=vote
[ "$EXPECT_PATH" = vote ] && OTHER_PATH=geometry-first
python3 $CENSUS --log $RUN/rig.txt --fixture "mocks/rig-20260918, censused as $OTHER_PATH (control)" \
  --expect-path "$OTHER_PATH" > $RUN/rig-mismatch-control.txt
CONTROL_RC=$?
CONTROL_NAMED=$(grep -c '^    PATH-MISMATCH visit ' $RUN/rig-mismatch-control.txt)
grep -m1 '^=== PUBLISHED PATH' $RUN/rig-mismatch-control.txt
grep -m2 '^    PATH-MISMATCH visit ' $RUN/rig-mismatch-control.txt
if [ $CONTROL_RC -eq 3 ] && [ "$CONTROL_NAMED" -gt 0 ]; then
  say "OK   the rig run censused as $OTHER_PATH is refused: rc=3, $CONTROL_NAMED darts named PATH-MISMATCH" ok
else
  say "FAIL the rig run censused as $OTHER_PATH exited $CONTROL_RC naming $CONTROL_NAMED darts;" no
  echo "     the census cannot be shown to report a path it was not told (#1584)"
fi

echo
echo "CHECK_RC=$FAILED  (this harness fails on a run it could not READ, never on what the run said)"
# The harness exits on what it measured: run_all.sh reads the exit code and nothing else,
# and an echo returns 0 whatever it printed (#1335, #1463, #1479).
exit $FAILED
