set -u
# #1317: a partial wire detection, the three guards, and the control, in one run.
#
# The partial detection is NOT constructed. mocks/rig-20260918 is the rig that produced
# `Selected 9 averaged wires from 21 candidates` live on 2026-09-18, and it reproduces the
# fault: over its clean first fifteen seconds the ensemble selects 18, 19 or 20 wires
# depending on which second the calibration lands on, and before this issue every one of
# those runs printed `Found 20 wire boundaries` and calibrated. Nine was that day's
# framing; eighteen is the same bug.
#
# The clip is cut from second 6 because that is where the cameras come up short. How MANY
# of the three do is not a property of the fixture: it depends on the second each camera's
# calibration lands on, and that depends on the host's clock and on whether the binary was
# built with DEBUG_SEEK_VIDEO. On the tree #1335 measured, two of the three fall short and
# the third finds its twenty wires, so the board-level failure is measured in 3d on a board
# made of one refused clip rather than asserted of this one. It is cut, rather than the
# .mp4 handed to the detector directly, for two reasons:
# the dev build seeks a file source three seconds in, so a short clip has to start before
# what it is meant to show; and the fixture's later half has darts and a person in it, and
# a low wire count read off a frame with an arm across the board would prove nothing about
# this issue. i1317_partial_footage's two transforms are both off here -- a scan of them is
# in the report, and occluding the frame costs boundary points faster than wires, so the
# camera is refused at the ellipse stage and never reaches the wires at all.
#
# Since #1317 a camera whose wire stage came up short does not calibrate, so the partial
# run goes to #895's fault vigil and stays there. It is therefore run in the background and
# ended by its own recorded pid, never by pattern.

START="${START:-6}"
OCCLUDE="${OCCLUDE:-0}"
BLUR="${BLUR:-0}"
MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4

echo "--- the three guards, called directly ---"
g++ -std=c++17 -O1 -o /run1317/guards /app/testers/i1317_guards.cpp \
  /app/src/detector/geometry/calibration/perspective_processing.cpp \
  /app/src/detector/geometry/detection/score_processing.cpp \
  -I/app/src -I/app/src/utils \
  -I/app/src/detector/geometry/calibration -I/app/src/detector/geometry/detection \
  $(pkg-config --cflags --libs opencv4) -lpthread || exit 1
/run1317/guards > /run1317/guards.out 2>&1
echo "GUARDS_RC=$?"
cat /run1317/guards.out

echo "--- cut the calibration window out of mocks/rig-20260918 ---"
g++ -std=c++17 -O1 -o /run1317/partial /app/testers/i1317_partial_footage.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
for i in 1 2 3; do
  /run1317/partial /app/mocks/rig-20260918/cam_$i.mp4 /run1317/partial_$i.avi \
    "$START" "$OCCLUDE" "$BLUR" 300 || exit 1
done

echo "--- the partial detection, WITHOUT --debug ---"
/app/build/opendartboard \
  --cams /run1317/partial_1.avi,/run1317/partial_2.avi,/run1317/partial_3.avi \
  --width 1280 --height 720 > /run1317/partial.out 2> /run1317/partial.err &
PART=$!
sleep 25
kill -TERM $PART 2>/dev/null
wait $PART 2>/dev/null
echo "PARTIAL_RC=$?"

echo "--- the same input WITH --debug, so the wire counts are on the record ---"
/app/build/opendartboard --debug \
  --cams /run1317/partial_1.avi,/run1317/partial_2.avi,/run1317/partial_3.avi \
  --width 1280 --height 720 > /run1317/partial_dbg.out 2> /run1317/partial_dbg.err &
PARTD=$!
sleep 25
kill -TERM $PARTD 2>/dev/null
wait $PARTD 2>/dev/null
echo "PARTIAL_DBG_RC=$?"

echo "--- the control: the footage the detector is known to calibrate on ---"
OD_MAX_CYCLES=20 /app/build/opendartboard --cams $MOCKS \
  --width 1280 --height 720 > /run1317/control.out 2> /run1317/control.err
echo "CONTROL_RC=$?"

echo "--- the control WITH --debug, for the wire counts it reports ---"
OD_MAX_CYCLES=20 /app/build/opendartboard --debug --cams $MOCKS \
  --width 1280 --height 720 > /run1317/control_dbg.out 2> /run1317/control_dbg.err
echo "CONTROL_DBG_RC=$?"

# Colour codes are in every console line; strip them once and read the plain text.
for f in partial partial_dbg control control_dbg; do
  sed 's/\x1b\[[0-9;]*m//g' /run1317/$f.out > /run1317/$f.txt
done

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

echo "=== 1. the three guards fire ==="
if grep -q '^GUARDS_RC=0' /run1317/guards.out; then
  say "OK   every guard fired, and its positive control passed beside it" ok
else say "FAIL the guard tester failed; its lines are above" no; fi

echo "=== 2. the log stops lying: Found N is what was found ==="
grep -E 'Found [0-9]+ wire boundaries' /run1317/partial_dbg.txt | sort -u || true
# The ensemble says how many it selected and the wire stage says how many it found. They
# are the same number, and nothing can assert that by agreeing with a constant.
MISMATCH=0
while read -r sel; do
  grep -q "Found $sel wire boundaries" /run1317/partial_dbg.txt || MISMATCH=$((MISMATCH+1))
done < <(grep -oE 'Selected [0-9]+ averaged wires' /run1317/partial_dbg.txt | awk '{print $2}' | sort -u)
if [ "$MISMATCH" = "0" ]; then say "OK   every count the ensemble selected is the count the stage reports" ok
else say "FAIL $MISMATCH selected counts are reported as something else" no; fi
SHORT=$(grep -oE 'Found [0-9]+ wire boundaries' /run1317/partial_dbg.txt | awk '$2 != 20' | wc -l)
if [ "$SHORT" != "0" ]; then say "OK   $SHORT of those lines print a number that is not 20" ok
else say "FAIL every line still prints 20, so nothing was measured" no; fi

echo "=== 3. a partial detection does not calibrate, and the failure names the count ==="
grep -E '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera' /run1317/partial.txt || true
REFUSED=$(grep -cE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera [0-9]+ did not calibrate: the wire stage found [0-9]+ wire boundaries and all [0-9]+ are needed' /run1317/partial.txt || true)
if [ "$REFUSED" -ge 1 ]; then say "OK   $REFUSED camera(s) refused by name, with the count and the threshold in one sentence" ok
else say "FAIL no camera was refused on its wire count -- this input is not a partial detection and the rest of this section proves nothing" no; fi

echo "=== 3b. the number it prints is the number that fell short ==="
# #1321's rule: a line nothing can falsify is not evidence. A camera refused for finding
# AS MANY wires as the stage needs did not fail on the wire count.
BAD=$(grep -oE 'the wire stage found [0-9]+ wire boundaries and all [0-9]+ are needed' /run1317/partial.txt \
  | awk '$5 >= $10 { print }' | wc -l)
if [ "$BAD" = "0" ]; then say "OK   every wire count printed is below the threshold it is printed against" ok
else say "FAIL $BAD refusals print a count that is not below its own threshold" no; fi

echo "=== 3c. every camera is accounted for, and no refused camera reaches PnP ==="
# The expectations are read out of the run rather than pinned, because which cameras come
# up short depends on the frame each one is seeked to. What cannot vary is the arithmetic:
# three cameras, each either seeing or refused, and a PnP fit for each one that is seeing
# and for none that is not. That is the criterion -- a partial detection ends in a reported
# failure naming the count, not in `PnP calibration successful` -- said as a census.
#
# #1335: the arithmetic used to count the cameras refused ON THE WIRE COUNT against the
# census, which assumes the wire count is the only thing that can turn a camera down. It
# is not -- a camera whose board cannot be measured at all is refused before the wires
# are ever counted -- and when that happened the assertion failed saying the census did
# not add up, which was not what had gone wrong. The census names the cameras it refused,
# so the arithmetic is done against that list and the wire-count refusal is asserted as
# itself on the line below.
CENSUS=$(grep -E 'CAMERAS: [0-9]+ of 3' /run1317/partial.txt | head -1)
echo "${CENSUS:-no camera census was printed}"
SEEING=$(echo "$CENSUS" | grep -oE 'CAMERAS: [0-9]+ of 3' | awk '{print $2}')
SEEING=${SEEING:-x}
BLIND=$(echo "$CENSUS" | sed -n 's/.*refused: \([0-9,]*\).*/\1/p' | tr ',' '\n' | grep -c '[0-9]' || true)
if [ "$SEEING" != "x" ] && [ $((SEEING + BLIND)) = "3" ]; then
  say "OK   $SEEING seeing plus $BLIND refused is all three cameras" ok
else say "FAIL $SEEING seeing and $BLIND refused does not account for three cameras" no; fi
if [ "$REFUSED" -ge 1 ] && [ "$BLIND" -ge "$REFUSED" ]; then
  say "OK   $REFUSED of the $BLIND cameras the census refused fell short on the wire count" ok
else say "FAIL the census refused $BLIND cameras and $REFUSED of them on the wire count" no; fi
PNP=$(grep -c 'PnP calibration successful' /run1317/partial_dbg.txt || true)
SEEING_DBG=$(grep -oE 'CAMERAS: [0-9]+ of 3' /run1317/partial_dbg.txt | head -1 | awk '{print $2}')
SEEING_DBG=${SEEING_DBG:-x}
if [ "$PNP" = "$SEEING_DBG" ]; then
  say "OK   $PNP PnP fits for $SEEING_DBG camera(s) that saw the board, and none for the rest" ok
else say "FAIL $PNP PnP fits against $SEEING_DBG cameras that saw the board" no; fi

echo "=== 3d. a board on which nothing calibrated says so ==="
# #1335: this required all three cameras to come up short at once, which is what second 6
# of this fixture did when #1317 was carried and does not do on this tree: camera 3 finds
# its twenty wires and the board comes up on one camera. The header above says why that is
# not a surprise -- how many cameras fall short depends on the second the calibration
# lands on, and the second it lands on depends on the host's clock -- so asserting the
# board-level failure on a three-camera board was asserting a coincidence.
#
# It is measured instead on a board where it cannot vary: the clip this run has just
# refused on the wire count, given to a board that has nothing else. Which clip that is
# is read out of the run above, never pinned, and if the run refused none there is
# nothing to measure and this section says so rather than passing.
SHORT=$(grep -oE 'Camera [0-9]+ did not calibrate: the wire stage found' /run1317/partial.txt \
  | head -1 | awk '{print $2}')
if [ -z "${SHORT:-}" ]; then
  say "FAIL no camera was refused on its wire count above, so there is no board to measure this on" no
else
  echo "--- the clip camera $SHORT was refused on, alone on its own board ---"
  /app/build/opendartboard --cams /run1317/partial_$SHORT.avi \
    --width 1280 --height 720 > /run1317/alone.out 2> /run1317/alone.err &
  ALONE=$!
  sleep 25
  kill -TERM $ALONE 2>/dev/null
  wait $ALONE 2>/dev/null
  echo "ALONE_RC=$?"
  sed 's/\x1b\[[0-9;]*m//g' /run1317/alone.out > /run1317/alone.txt
  grep -E 'CAMERAS: [0-9]+ of 1|did not calibrate' /run1317/alone.txt | head -2 || true
  # The positive control: this board really did refuse its only camera, on the wires.
  if grep -qE 'CAMERAS: 0 of 1' /run1317/alone.txt &&
     grep -qE 'Camera 1 did not calibrate: the wire stage found [0-9]+ wire boundaries' /run1317/alone.txt; then
    say "OK   the only camera on this board was refused on its wire count" ok
  else say "FAIL this board did not refuse its only camera on the wire count, so the rest of 3d proves nothing" no; fi
  if grep -q 'Initial calibration completed successfully' /run1317/alone.txt; then
    say "FAIL no camera saw the board and it still calibrated" no
  else say "OK   no 'Initial calibration completed successfully'" ok; fi
  if grep -qE 'BOARD FAULTED: camera [0-9]+ did not calibrate: the wire stage found' /run1317/alone.txt; then
    say "OK   the vigil says which camera and that it was the wires" ok
  else
    grep -E 'BOARD FAULTED' /run1317/alone.txt | head -2 || true
    say "FAIL BOARD FAULTED does not name the wire count" no
  fi
fi

echo "=== 4. the control still calibrates and says nothing new ==="
if grep -q 'Initial calibration completed successfully on 3 of 3 cameras' /run1317/control.txt; then
  say "OK   the mocks calibrate, all three" ok
else say "FAIL the mocks did not calibrate" no; fi
grep -E '^\[(ERROR|WARN)\]' /run1317/control.txt || true
NOISE=$(grep -cE '^\[(ERROR|WARN)\]' /run1317/control.txt || true)
if [ "$NOISE" = "0" ]; then say "OK   the control prints no ERROR and no WARN" ok
else say "FAIL the control prints $NOISE ERROR/WARN lines" no; fi
grep -E 'Found [0-9]+ wire boundaries' /run1317/control_dbg.txt | sort -u || true

echo "CHECK_RC=$FAILED"
exit $FAILED
