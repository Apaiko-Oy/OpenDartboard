set -u
# #1389: two is the floor, a board below it does not score and says why per camera, and
# a board that cannot reach the floor stays up.
#
# ADR-0081 §1 is the claim this measures: a one-camera board is ADMITTED, opens dart
# windows and can never register a dart or a takeout, because both state-vote branches
# fail and the state stays where it was. It is inert and it reports itself healthy. That
# claim had never been demonstrated anywhere in this repository; phase E demonstrates it,
# on the same binary as phase B, which is what makes phase B a rule rather than a build.
#
#   A  the control: the shipped mocks, three of three. It must calibrate and print no
#      ERROR and no WARN. If this fails, the floor has broken a healthy rig and nothing
#      below it matters.
#   B  one camera. Refused -- and the refusal names EVERY camera and its own board_look
#      reason (ADR-0081 §3), not the count. Two cameras answer `NoFrame` here.
#   C  a board with one camera looking at something that is not a dartboard and one that
#      produced nothing. The same refusal with a SECOND board_look reason in it, so the
#      per-camera half is shown carrying the vocabulary rather than one string.
#   D  two cameras, both looking at the board. It MUST score: two is the floor, not the
#      fault. #1346's three confidences are untouched by this slice and a two-camera
#      board publishing at 0.7 is the design.
#   E  falsification, and the issue's own "before": the same one-camera board on the same
#      binary under OD_CAMERA_QUORUM=1 OD_STATE_FLOOR=2 OD_STATE_QUORUM=absolute, which is
#      exactly what #1318, #1353 and the pre-#1348 vote shipped -- an admission gate at
#      one, an event census at one and a vote at two. It must calibrate, start scoring,
#      open dart windows and move its state NOT ONCE.
#
#      All three switches are needed and the reason is worth reading, because the first
#      draft of this phase set only the first two and passed for the wrong reason. The
#      defect ADR-0081 §1 is about is not a one-camera board; it is three numbers that
#      DISAGREE. OD_CAMERA_QUORUM=1 alone moves all three together, so the board is
#      admitted AND can move its own state on one camera -- it went to DART_1 and the
#      phase counted zero refused windows, which reads as inertness and is the opposite of
#      it. OD_STATE_FLOOR=2 is what puts the disagreement back.
# ADR-0081 §4 -- whether two cameras score as ACCURATELY as three -- is explicitly out of
# this slice's scope and is NOT asked here. It is a measurement and one pair of runs ended
# by SIGTERM is not one. What fell out of phase D while writing it is in the pull request
# and wants an issue of its own: on 90 seconds of the shipped mocks the two-camera board
# published the same wedge at the same confidence as the three-camera control for every
# MEASURED dart (0.7), and the two diverged only inside the 0.5 band -- the band #1346
# defines as a wedge nobody measured -- where three cameras said MISS and two said S20.
#
#   F  the same board with OD_CAMERA_QUORUM=1 alone -- the floor moved and nothing else.
#      It must be admitted, which is what makes phase B a rule about a number rather than
#      a fact about a build.
#   F  #895's vigil: the refused board in phase B is still running after its verdict. It
#      does not score, it stays up, and it says so. A camera that comes back finds the
#      detector there.
#
# Every detector started here can end in #895's fault vigil, which never returns, so each
# is backgrounded and ended by its own recorded pid. Never by pattern, and never
# unbounded: OD_MAX_CYCLES does not bound a board that cannot calibrate.

BIN=/app/build/opendartboard
MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4

await() {
  local file="$1" needle="$2" limit="$3" i=0
  while [ $i -lt "$limit" ]; do
    grep -qaE "$needle" "$file" 2>/dev/null && { echo "$i"; return 0; }
    i=$((i + 1)); sleep 1
  done
  echo "TIMEOUT-$limit"; return 1
}

echo "--- the footage of a camera that opens and sees no dartboard (#1318's generator) ---"
g++ -std=c++17 -O1 -o /run1389/make_source /app/testers/i1318_make_source.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
/run1389/make_source face /run1389/face.avi 1280 720 15 120 || exit 1

echo "=== A: the control, three of three ==="
OD_MAX_CYCLES=20 $BIN --cams $MOCKS --width 1280 --height 720 > /run1389/a.out 2>&1
echo "A_RC=$?"

echo "=== B: one camera of three (OD_DROP_CAM=1,2) ==="
OD_DROP_CAM=1,2 OD_DROP_EVERY=1 $BIN --cams $MOCKS --width 1280 --height 720 \
  > /run1389/b.out 2>&1 &
B=$!
echo "B reached its verdict after $(await /run1389/b.out 'BOARD FAULTED|Scorer running with' 120)s"
sleep 8
# #895's vigil, measured before the kill: the process that was refused is still there.
if kill -0 $B 2>/dev/null; then B_ALIVE=yes; else B_ALIVE=no; fi
echo "B_ALIVE=$B_ALIVE"
kill -TERM $B 2>/dev/null; wait $B 2>/dev/null

echo "=== C: one camera looking at a warm room, one board camera, one silent ==="
OD_DROP_CAM=2 OD_DROP_EVERY=1 $BIN --cams /run1389/face.avi,/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4 \
  --width 1280 --height 720 > /run1389/c.out 2>&1 &
C=$!
echo "C reached its verdict after $(await /run1389/c.out 'BOARD FAULTED|Scorer running with' 120)s"
sleep 6
kill -TERM $C 2>/dev/null; wait $C 2>/dev/null

echo "=== D: two cameras, both looking at the board (OD_DROP_CAM=2) ==="
OD_DROP_CAM=2 OD_DROP_EVERY=1 $BIN --cams $MOCKS --width 1280 --height 720 \
  > /run1389/d.out 2>&1 &
D=$!
echo "D started scoring after $(await /run1389/d.out 'Scorer running with|BOARD FAULTED' 180)s"
# Long enough to publish scores rather than only to calibrate. At 20s this phase saw two
# dart windows and no score at all, and its confidence assertion passed by having nothing
# to look at -- which is the shape of assertion this repository keeps paying for.
sleep 90
kill -TERM $D 2>/dev/null; wait $D 2>/dev/null

echo "=== E: the same one camera, with the three quorums put back as they disagreed ==="
OD_CAMERA_QUORUM=1 OD_STATE_FLOOR=2 OD_STATE_QUORUM=absolute OD_DROP_CAM=1,2 OD_DROP_EVERY=1 \
  $BIN --cams $MOCKS --width 1280 --height 720 > /run1389/e.out 2>&1 &
E=$!
echo "E reached its verdict after $(await /run1389/e.out 'BOARD FAULTED|Scorer running with' 120)s"
sleep 60
kill -TERM $E 2>/dev/null; wait $E 2>/dev/null

echo "=== F: the same one camera, with the floor moved to 1 and nothing else ==="
OD_CAMERA_QUORUM=1 OD_DROP_CAM=1,2 OD_DROP_EVERY=1 \
  $BIN --cams $MOCKS --width 1280 --height 720 > /run1389/f.out 2>&1 &
F=$!
echo "F reached its verdict after $(await /run1389/f.out 'BOARD FAULTED|Scorer running with' 120)s"
sleep 6
kill -TERM $F 2>/dev/null; wait $F 2>/dev/null

# Colour codes are in every console line; strip them once and read the plain text.
for f in a b c d e f; do sed 's/\x1b\[[0-9;]*m//g' /run1389/$f.out > /run1389/$f.txt; done

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

echo
echo "=== 1. the control is untouched: the floor did not break a healthy rig ==="
if grep -qa 'Initial calibration completed successfully on 3 of 3 cameras' /run1389/a.txt; then
  say "OK   the mocks calibrate, all three" ok
else say "FAIL the mocks did not calibrate" no; fi
grep -aE '^\[(ERROR|WARN)\]' /run1389/a.txt || true
NOISE=$(grep -caE '^\[(ERROR|WARN)\]' /run1389/a.txt || true)
if [ "$NOISE" = "0" ]; then say "OK   the control prints no ERROR and no WARN" ok
else say "FAIL the control prints $NOISE ERROR/WARN lines" no; fi

echo
echo "=== 2. one camera does not score, and the refusal names every camera ==="
grep -aE 'Initial calibration failed' /run1389/b.txt | head -1 || true
if grep -qa 'Scorer running with' /run1389/b.txt; then
  say "FAIL a board below the floor started scoring" no
else say "OK   it did not start scoring" ok; fi
if grep -qa 'Initial calibration failed' /run1389/b.txt; then
  say "OK   and it said so" ok
else say "FAIL it printed no refusal at all" no; fi
# ADR-0081 section 3. The count is the less useful half; the per-camera reason is what
# somebody can act on. All three cameras, on the refusal's own line.
for n in 1 2 3; do
  if grep -qa "Initial calibration failed:.*camera $n: " /run1389/b.txt; then
    say "OK   camera $n is named on the refusal line" ok
  else say "FAIL camera $n is not named on the refusal line" no; fi
done
if [ "$(grep -oa 'camera [0-9]: no frame to look at' /run1389/b.txt | sort -u | wc -l)" -ge 2 ]; then
  say "OK   the two silent cameras each carry board_look's NoFrame words (#1319, the USB bus)" ok
else say "FAIL the silent cameras do not carry a NoFrame reason each" no; fi
if grep -qa 'camera 1: sees the dartboard and can vote' /run1389/b.txt; then
  say "OK   and the camera that CAN vote is named too, so 'one of three' says WHICH one" ok
else say "FAIL the working camera is not named" no; fi
# The mutation that matters: a refusal that has degraded back to a count.
if grep -qaE 'Initial calibration failed:.*camera 1:.*camera 2:.*camera 3:' /run1389/b.txt; then
  say "OK   the refusal is a per-camera census and not a ratio" ok
else say "FAIL the refusal has become a count again" no; fi

echo
echo "=== 3. #895's vigil: it does not score, it stays up, and it says so ==="
if [ "$B_ALIVE" = yes ]; then
  say "OK   the refused board was still running after its verdict -- a camera that comes back finds it there" ok
else say "FAIL the refused board exited; #895's vigil is gone" no; fi
if grep -qa 'BOARD FAULTED' /run1389/b.txt; then
  say "OK   and the vigil is not silent about it (ADR-0055)" ok
else say "FAIL the board said nothing while it sat there" no; fi

echo
echo "=== 4. a second board_look reason, carried by the same sentence ==="
grep -aE 'Initial calibration failed' /run1389/c.txt | head -1 || true
if grep -qa 'camera 1: is not looking at the dartboard' /run1389/c.txt; then
  say "OK   the camera that answered and was refused says what it failed on -- its aim, not its cable" ok
else say "FAIL the refused camera carries no board_look reason" no; fi
if grep -qa 'camera 3: no frame to look at' /run1389/c.txt; then
  say "OK   and the silent camera beside it still says the other thing" ok
else say "FAIL the silent camera is not distinguished from the refused one" no; fi

echo
echo "=== 5. two cameras is the floor, not the fault ==="
grep -aE 'Initial calibration completed successfully|Scorer running with' /run1389/d.txt | head -2 || true
if grep -qa 'Initial calibration completed successfully on 2 of 3 cameras' /run1389/d.txt \
   && grep -qa 'Scorer running with 2 of 3 cameras' /run1389/d.txt; then
  say "OK   a two-camera board calibrates and scores (ADR-0081 §1)" ok
else say "FAIL the floor refused a board that is standing on it" no; fi
# #1346's three confidences are out of this slice's scope and must not have moved. A
# two-camera board publishing at 0.7 is the design, not a bug to round up: 0.9 is two or
# more MEASURED cameras agreeing, 0.7 is one measurement standing alone, 0.5 is a wedge
# nobody measured. `END` is a takeout marker rather than a wedge and carries 1.0, which is
# not a confidence in a score and is not read here.
WEDGES=$(grep -oaE 'SCORE: [A-Z0-9]+ \| Position: \([0-9,-]+\) \| Confidence: [0-9.]+' /run1389/d.txt \
  | grep -v 'SCORE: END' || true)
echo "$WEDGES" | sed 's/^/    /' | head -12
N=$(echo "$WEDGES" | grep -c 'SCORE:' || true)
if [ "$N" -ge 1 ]; then
  say "OK   it published $N scores, so the line below has something to be about" ok
else say "FAIL the two-camera board published no score at all, so nothing here is measured" no; fi
BADCONF=$(echo "$WEDGES" | grep -oE 'Confidence: [0-9.]+' | grep -vcE 'Confidence: 0\.(500000|700000|900000)' || true)
CONF=$(echo "$WEDGES" | grep -oE 'Confidence: [0-9.]+' | sort -u | tr '\n' ' ')
echo "    confidences: $CONF"
if [ "$BADCONF" = "0" ] && [ "$N" -ge 1 ]; then
  say "OK   every one is one of #1346's three, and 0.7 on two cameras is the design" ok
else say "FAIL $BADCONF scores carry a confidence outside #1346's 0.5/0.7/0.9" no; fi


echo
echo "=== 6. the 'before' this issue was filed about, on this same binary ==="
grep -aE 'Initial calibration (completed|failed)|Scorer running with' /run1389/e.txt | head -2 || true
if grep -qa 'Initial calibration completed successfully on 1 of 3 cameras' /run1389/e.txt \
   && grep -qa 'Scorer running with 1 of 3 cameras' /run1389/e.txt; then
  say "OK   with the three quorums disagreeing again, the same board is ADMITTED and starts scoring" ok
else say "FAIL the switches did not restore the state this issue is about, so nothing below is measured" no; fi
WINDOWS=$(grep -ca 'STATE VOTE' /run1389/e.txt || true)
# A window whose vote MOVED the state prints no STATE VOTE line at all -- refusedWindowAccount
# returns empty when final_state != previous_state (#1350) -- so counting refusals is not
# how you find a move. The state itself is what says: every line names the state it stayed
# in, and on an inert board that is CLEAN in every one of them.
NOTCLEAN=$(grep -a 'STATE VOTE' /run1389/e.txt | grep -cav 'so it stays CLEAN' || true)
REACHED=$(grep -ca 'which those 1 could not have reached' /run1389/e.txt || true)
echo "    it opened $WINDOWS dart windows; $NOTCLEAN of them account for a board not in CLEAN"
if [ "$WINDOWS" -ge 1 ]; then
  say "OK   it forms dart windows, so it is not idle" ok
else say "FAIL it opened no window at all, so this phase measured nothing about inertness" no; fi
if [ "$NOTCLEAN" = "0" ]; then
  say "OK   the board never left CLEAN: inert, while reporting itself healthy (ADR-0081 §1)" ok
else say "FAIL $NOTCLEAN windows account for a board that had moved, so it was not inert" no; fi
if [ "$REACHED" = "$WINDOWS" ] && [ "$WINDOWS" -ge 1 ]; then
  say "OK   and every window says why -- one voter against a quorum of two (#1348's clause)" ok
else say "FAIL only $REACHED of $WINDOWS windows say the voters could not have reached the quorum" no; fi

echo
echo "=== 7. falsification: the floor moved, and nothing else ==="
grep -aE 'Initial calibration (completed|failed)|Scorer running with' /run1389/f.txt | head -2 || true
if grep -qa 'Initial calibration completed successfully on 1 of 3 cameras' /run1389/f.txt; then
  say "OK   at OD_CAMERA_QUORUM=1 the board phase B refused is admitted, so B is about the number" ok
else say "FAIL the gate did not move with the number, so phase B is a claim about a build" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED
