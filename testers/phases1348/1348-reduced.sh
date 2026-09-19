set -u
# #1348: a board whose cameras cannot reach the vote's quorum, and what it says about
# itself.
#
# #1338 refused a board that could not form a dart event, and tied the refusal to the
# ANSWERING count against `min_cameras_for_event`. #1353 then moved that constant to 1 --
# a dart splash is a one-camera motion fact -- and in closing that half of #1348 it moved
# the binding arithmetic one stage on with nothing asking it: the dart-state vote still
# took 2, and a board with one camera able to vote holds CLEAN for ever and cannot see a
# takeout either. It calibrated, it beat READY, and it could not score. Measured on this
# fixture at issue-1358 (79f7dd2), before this change: `1338-partial` FAILED on exactly
# that -- "a board that cannot form a dart event beat READY", "it never beat ERROR
# either".
#
# Three phases, and the third is what makes the first two mean anything:
#
#   A  the control: the shipped mocks, three of three, which must still calibrate and
#      must print no ERROR and no WARN.
#   B  the same footage with two cameras dropped. One camera can vote, the quorum is 2,
#      so the board must be refused -- and the sentence must name the count and the
#      threshold it fell short of (#1321's rule).
#   C  falsification: the same reduced board on the same binary under
#      OD_STATE_QUORUM=absolute, which restores the vote as it was before #1348 -- the
#      absolute count, and a calibration that never asks the vote's arithmetic. It must
#      calibrate again. Without C, B is a claim about a build.
#
#      #1389 added the second half of that switch. There are two rules to put back now,
#      not one: the vote's, and the camera quorum the admission gate reads since ADR-0081
#      (OD_CAMERA_QUORUM=1, the value #1318 and #1353 shipped). Restoring only the vote
#      leaves the board refused by the floor instead, which is a true refusal and not the
#      one this phase is falsifying -- so C sets both and the pre-#1348 board comes back
#      whole.
#
# Every detector started here can end in #895's fault vigil, which never returns, so each
# is backgrounded and ended by its own recorded pid. Never by pattern.

BIN=/app/build/opendartboard
MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4

echo "=== A: the control, three of three ==="
OD_MAX_CYCLES=20 $BIN --cams $MOCKS --width 1280 --height 720 > /run1348/a.out 2>&1
echo "A_RC=$?"

echo "=== B: two cameras dropped, so one camera can vote ==="
OD_DROP_CAM=1,2 OD_DROP_EVERY=1 $BIN --cams $MOCKS --width 1280 --height 720 \
  > /run1348/b.out 2>&1 &
B=$!
i=0; while [ $i -lt 120 ]; do grep -qa 'BOARD FAULTED\|Scorer running with' /run1348/b.out 2>/dev/null && break; i=$((i+1)); sleep 1; done
echo "B reached its verdict after ${i}s"
sleep 5
kill -TERM $B 2>/dev/null; wait $B 2>/dev/null
echo "B_RC=$?"

echo "=== C: the same board under OD_STATE_QUORUM=absolute OD_CAMERA_QUORUM=1 ==="
OD_STATE_QUORUM=absolute OD_CAMERA_QUORUM=1 OD_DROP_CAM=1,2 OD_DROP_EVERY=1 $BIN --cams $MOCKS \
  --width 1280 --height 720 > /run1348/c.out 2>&1 &
C=$!
i=0; while [ $i -lt 120 ]; do grep -qa 'BOARD FAULTED\|Scorer running with' /run1348/c.out 2>/dev/null && break; i=$((i+1)); sleep 1; done
echo "C reached its verdict after ${i}s"
sleep 5
kill -TERM $C 2>/dev/null; wait $C 2>/dev/null
echo "C_RC=$?"

# Colour codes are in every console line; strip them once and read the plain text.
for f in a b c; do sed 's/\x1b\[[0-9;]*m//g' /run1348/$f.out > /run1348/$f.txt; done

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

echo
echo "=== 1. the control is untouched ==="
if grep -qa 'Initial calibration completed successfully on 3 of 3 cameras' /run1348/a.txt; then
  say "OK   the mocks calibrate, all three" ok
else say "FAIL the mocks did not calibrate" no; fi
grep -aE '^\[(ERROR|WARN)\]' /run1348/a.txt || true
NOISE=$(grep -caE '^\[(ERROR|WARN)\]' /run1348/a.txt || true)
if [ "$NOISE" = "0" ]; then say "OK   the control prints no ERROR and no WARN" ok
else say "FAIL the control prints $NOISE ERROR/WARN lines" no; fi

echo
echo "=== 2. a board that cannot move its own state is refused, and says why ==="
grep -aE 'Initial calibration failed|BOARD FAULTED|Scorer running with' /run1348/b.txt | head -3 || true
if grep -qa 'Scorer running with' /run1348/b.txt; then
  say "FAIL a board that can never change state started scoring" no
else say "OK   it did not start scoring" ok; fi
if grep -qa 'Initial calibration failed: only 1 of 3 cameras can vote on what is on the board' /run1348/b.txt; then
  say "OK   the refusal names the voting population, not the answering one" ok
else say "FAIL the refusal does not name the voting population" no; fi
if grep -qa 'it takes 2 of them to move the board' /run1348/b.txt; then
  say "OK   and the threshold it fell short of, in the same sentence (#1321)" ok
else say "FAIL the sentence does not state the count against its threshold" no; fi
if grep -qa 'neither call a dart nor see one taken out' /run1348/b.txt; then
  say "OK   and that a board which cannot move cannot see a takeout either" ok
else say "FAIL the sentence says nothing about the takeout" no; fi
if grep -qa 'BOARD FAULTED: only 1 of 3 cameras can vote' /run1348/b.txt; then
  say "OK   the vigil repeats that sentence rather than a choice of two" ok
else say "FAIL BOARD FAULTED does not carry the reason" no; fi
# #1321's rule, read out of the run: the count printed must be on the wrong side of the
# threshold printed beside it, or a board refused for having ENOUGH cameras would pass.
BAD=$(grep -oaE 'only ([0-9]+) of [0-9]+ cameras can vote on what is on the board.*it takes ([0-9]+) of them' /run1348/b.txt \
  | sed -E 's/^only ([0-9]+) of.*it takes ([0-9]+) of them.*/\1 \2/' \
  | awk '{ if ($1 >= $2) print }' | wc -l)
if [ "$BAD" = "0" ]; then say "OK   every count printed is below the threshold it is printed against" ok
else say "FAIL $BAD refusals print a count that is not below its own threshold" no; fi

echo
echo "=== 3. falsification: the same board, the same binary, both rules put back ==="
grep -aE 'Initial calibration (failed|completed)|BOARD FAULTED|Scorer running with' /run1348/c.txt | head -3 || true
if grep -qa 'Initial calibration completed successfully on 1 of 3 cameras' /run1348/c.txt; then
  say "OK   under OD_STATE_QUORUM=absolute OD_CAMERA_QUORUM=1 the same board calibrates again" ok
else say "FAIL the switch did not restore the behaviour this issue changed, so phase B is about a build and not about a rule" no; fi
if grep -qa 'Scorer running with 1 of 3 cameras' /run1348/c.txt; then
  say "OK   and starts scoring -- which is what it did before #1348, and could not do" ok
else say "FAIL it did not start scoring under the restored rule" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED
