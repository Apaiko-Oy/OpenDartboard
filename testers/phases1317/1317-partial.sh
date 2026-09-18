set -u
# #1317: a partial wire detection, the three guards, and the control, in one run.
#
# The failing input is made here rather than committed, the way #1321 makes its dark
# footage. mocks/rig-20260918 IS the rig that produced `Selected 9 averaged wires from 21
# candidates` live on 2026-09-18, and its clean first fifteen seconds were scanned before
# this script was written: the ensemble finds 20, 20 and 21 wires there (see the report on
# the issue). So the nine-wire count belongs to that rig's framing on the day rather than
# to the footage, and the partial detection is constructed -- from that same rig, by one
# stated transform: a fraction of the frame's width blacked out from the right edge, which
# is something standing in front of one side of the board. The wires behind it produce no
# colour transition and no Hough line, so the ensemble returns fewer than twenty groups,
# while the doubles ring on the rest of the board is still traced from well over the fifty
# rays the ellipse fitter needs -- so the run reaches the wire stage rather than failing
# above it, which is the whole point of the input.
#
# Since #1317 a camera whose wire stage came up short does not calibrate, so the partial
# run goes to #895's fault vigil and stays there. It is therefore run in the background and
# ended by its own recorded pid, never by pattern.

OCCLUDE="${OCCLUDE:-0.30}"
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

echo "--- build the partially occluded footage from mocks/rig-20260918 ---"
g++ -std=c++17 -O1 -o /run1317/partial /app/testers/i1317_partial_footage.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
for i in 1 2 3; do
  /run1317/partial /app/mocks/rig-20260918/cam_$i.mp4 /run1317/partial_$i.avi 0 "$OCCLUDE" 300 || exit 1
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
REASONS=$(grep -cE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera [0-9]+ did not calibrate: the wire stage found [0-9]+ wire boundaries and all [0-9]+ are needed' /run1317/partial.txt || true)
if [ "$REASONS" -ge 1 ]; then say "OK   $REASONS camera(s) refused by name, with the count and the threshold in one sentence" ok
else say "FAIL no camera was refused on its wire count" no; fi

echo "=== 3b. the number it prints is the number that fell short ==="
# #1321's rule: a line nothing can falsify is not evidence. A camera refused for finding
# AS MANY wires as the stage needs did not fail on the wire count.
BAD=$(grep -oE 'the wire stage found [0-9]+ wire boundaries and all [0-9]+ are needed' /run1317/partial.txt \
  | awk '$5 >= $10 { print }' | wc -l)
if [ "$BAD" = "0" ]; then say "OK   every wire count printed is below the threshold it is printed against" ok
else say "FAIL $BAD refusals print a count that is not below its own threshold" no; fi

echo "=== 3c. it does not report success ==="
if grep -q 'Initial calibration completed successfully' /run1317/partial.txt; then
  say "FAIL the partial detection still calibrated" no
else say "OK   no 'Initial calibration completed successfully'" ok; fi
if grep -q 'PnP calibration successful' /run1317/partial_dbg.txt; then
  say "FAIL PnP still reported success on a partial detection" no
else say "OK   no 'PnP calibration successful' on the partial input" ok; fi
if grep -qE 'BOARD FAULTED: camera [0-9]+ did not calibrate: the wire stage found' /run1317/partial.txt; then
  say "OK   the vigil says which camera and that it was the wires" ok
else say "FAIL BOARD FAULTED does not name the wire count" no; fi

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
