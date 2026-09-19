set -u
# #1318: a camera that is not looking at a dartboard, in four runs and one control.
#
# The failure was measured on Windows hardware nobody here has -- a laptop's built-in
# webcam at index 0, calibrated as camera 1, the operator's face written to
# debug_frames/roi_processing/roi_frame_0.jpg. What made it look like a dartboard is in
# the issue's own numbers: the doubles mask came back at 237,036 white pixels against
# 27,624 for a real board camera in the same run. testers/i1318_make_source.cpp writes
# footage with that signature so the Linux container can be handed a camera that opens
# perfectly and sees no board, and `wall` is the other half of the control -- a grey
# room, nothing warm in it -- because a check that only ever refuses red things has not
# been shown to refuse anything else.
#
# What is asserted is what a tester can see without knowing any flag exists: the camera
# that is not looking at the board is named, once, with the count it failed on; the two
# that ARE looking at it calibrate and the board runs; the mocks are unchanged; --cams
# is obeyed exactly as typed; and a start with no --cams finds the board cameras with a
# non-board device ahead of them in the list.

echo "--- build the non-board footage from testers/i1318_make_source.cpp ---"
g++ -std=c++17 -O1 -o /run1318/make_source /app/testers/i1318_make_source.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
/run1318/make_source face /run1318/face.avi 1280 720 15 120 || exit 1
/run1318/make_source wall /run1318/wall.avi 1280 720 15 120 || exit 1

MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4

echo "--- A: the webcam ahead of the board cameras, WITHOUT --debug ---"
OD_MAX_CYCLES=20 /app/build/opendartboard \
  --cams /run1318/face.avi,/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4 \
  --width 1280 --height 720 > /run1318/a.out 2> /run1318/a.err
echo "A_RC=$?"

echo "--- B: the control, the footage the detector is known to calibrate on ---"
OD_MAX_CYCLES=20 /app/build/opendartboard \
  --cams $MOCKS --width 1280 --height 720 > /run1318/b.out 2> /run1318/b.err
echo "B_RC=$?"

echo "--- C: --cams wins, with a candidate list that says otherwise ---"
OD_CAM_CANDIDATES=/run1318/face.avi,/run1318/wall.avi \
OD_MAX_CYCLES=20 /app/build/opendartboard \
  --cams /app/mocks/cam_3.mp4,/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4 \
  --width 1280 --height 720 > /run1318/c.out 2> /run1318/c.err
echo "C_RC=$?"

echo "--- D: no --cams at all, and a non-board device first in the list ---"
OD_CAM_CANDIDATES=/run1318/face.avi,/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
OD_MAX_CYCLES=20 /app/build/opendartboard \
  --width 1280 --height 720 > /run1318/d.out 2> /run1318/d.err
echo "D_RC=$?"

echo "--- E: every camera on a grey wall; the board faults and says which ---"
OD_MAX_CYCLES=20 /app/build/opendartboard \
  --cams /run1318/wall.avi,/run1318/wall.avi,/run1318/wall.avi \
  --width 1280 --height 720 > /run1318/e.out 2> /run1318/e.err &
E=$!
sleep 25
kill -TERM $E 2>/dev/null
wait $E 2>/dev/null
echo "E_RC=$?"

# Colour codes are in every console line; strip them once and read the plain text.
for f in a b c d e; do sed 's/\x1b\[[0-9;]*m//g' /run1318/$f.out > /run1318/$f.txt; done

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

echo "=== 1. the camera that is not looking at the board is refused BY NAME ==="
# #1392 moved what this sentence is a share OF. The camera, the measure and the line are
# still in one line -- #1321's rule, which is what this phase is really about -- but the
# measure is no longer a share of the frame held to a constant: it is the whole picture's
# colour against what a WHOLE board in a frame this shape could account for. The old
# sentence is still reachable, word for word, under OD_LOOK=frame, and #1392's own tester
# is what holds it to #1318's numbers.
grep -E '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera' /run1318/a.txt || true
if grep -qE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera 1 .*is not looking at the dartboard: [0-9.]+% of its whole picture keys as dartboard red or green and the biggest board that fits in a [0-9]+x[0-9]+ frame could account for at most [0-9.]+%' /run1318/a.txt; then
  say "OK   camera 1 is named, with the measure and the threshold in one sentence" ok
else say "FAIL no line names camera 1 and what it failed on" no; fi

echo "=== 1b. the number printed is on the wrong side of the threshold it is printed against ==="
# #1321's rule. A line nothing can falsify is not evidence: a camera refused for holding
# LESS red and green than the check allows did not fail on red and green.
#
# $NF rather than a field number since #1392: the threshold is the last number in the
# matched string either way, and counting words was a thing the sentence could break.
BAD=$(grep -oE '[0-9.]+% of its whole picture keys as dartboard red or green and .* this check allows [0-9.]+%' /run1318/a.txt \
  | tr -d '%' | awk '{ if ($1 <= $NF) print }' | wc -l)
if [ "$BAD" = "0" ]; then say "OK   every percentage printed is above the threshold it is printed against" ok
else say "FAIL $BAD refusals print a percentage that is not above its own threshold" no; fi

echo "=== 2. exactly one ERROR per refused camera, and none for the two that are fine ==="
ERRS=$(grep -cE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera' /run1318/a.txt || true)
if [ "$ERRS" = "1" ]; then say "OK   one calibration ERROR, for the one camera that earned it" ok
else say "FAIL $ERRS calibration ERROR lines for one refused camera" no; fi

echo "=== 3. the board does not fail for that one camera, and says who was fine ==="
grep -E 'CAMERAS: [0-9]+ of|Initial calibration' /run1318/a.txt || true
if grep -qE 'CAMERAS: 2 of 3 are looking at the dartboard \(2,3\); refused: 1' /run1318/a.txt; then
  say "OK   two named as seeing, one named as refused" ok
else say "FAIL the run does not say which cameras were fine" no; fi
if grep -q 'Initial calibration completed successfully on 2 of 3 cameras' /run1318/a.txt; then
  say "OK   the board calibrated on the two that can see" ok
else say "FAIL the board did not calibrate on the two good cameras" no; fi
if grep -q 'BOARD FAULTED' /run1318/a.txt; then
  say "FAIL one bad camera still faulted the whole board" no
else say "OK   no BOARD FAULTED" ok; fi

echo "=== 4. the control calibrates and says nothing new ==="
if grep -q 'Initial calibration completed successfully on 3 of 3 cameras' /run1318/b.txt; then
  say "OK   the mocks calibrate, all three" ok
else say "FAIL the mocks did not calibrate" no; fi
grep -E '^\[(ERROR|WARN)\]' /run1318/b.txt || true
NOISE=$(grep -cE '^\[(ERROR|WARN)\]' /run1318/b.txt || true)
if [ "$NOISE" = "0" ]; then say "OK   the control prints no ERROR and no WARN" ok
else say "FAIL the control prints $NOISE ERROR/WARN lines" no; fi

echo "=== 5. --cams wins, exactly as typed, and nothing is probed ==="
grep -E '^      [0-9]+: |looking through' /run1318/c.txt | head -4 || true
if grep -q '      1: /app/mocks/cam_3.mp4' /run1318/c.txt \
   && grep -q '      2: /app/mocks/cam_1.mp4' /run1318/c.txt \
   && grep -q '      3: /app/mocks/cam_2.mp4' /run1318/c.txt; then
  say "OK   the three named on the command line, in the order they were named" ok
else say "FAIL --cams did not decide the cameras" no; fi
if grep -q 'looking through' /run1318/c.txt; then
  say "FAIL the probe ran although --cams was given" no
else say "OK   the probe did not run" ok; fi
if grep -q 'Initial calibration completed successfully on 3 of 3' /run1318/c.txt; then
  say "OK   and all three calibrate" ok
else say "FAIL the explicit three did not calibrate" no; fi

echo "=== 6. no --cams: the board cameras are found with a non-board device ahead of them ==="
grep -E 'CAMERA /|CAMERAS:' /run1318/d.txt | head -8 || true
if grep -qE 'CAMERA /run1318/face.avi: not used - is not looking at the dartboard' /run1318/d.txt; then
  say "OK   the non-board device is refused by name before it is ever opened for scoring" ok
else say "FAIL the probe did not refuse the non-board device by name" no; fi
if grep -q 'CAMERAS: /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 (looked through' /run1318/d.txt; then
  say "OK   the three that can see the dartboard are the three that are opened" ok
else say "FAIL the probe did not choose the three board cameras" no; fi
if grep -q 'Initial calibration completed successfully on 3 of 3 cameras' /run1318/d.txt; then
  say "OK   and the board calibrates on all three" ok
else say "FAIL the chosen three did not calibrate" no; fi

echo "=== 7. a board that really cannot see still faults, and says which camera ==="
grep -E 'CAMERAS: 0 of|BOARD FAULTED' /run1318/e.txt | head -2 || true
if grep -q 'CAMERAS: 0 of 3 are looking at the dartboard; refused: 1,2,3' /run1318/e.txt; then
  say "OK   all three refused, all three named" ok
else say "FAIL the blind run does not name its cameras" no; fi
if grep -qE 'BOARD FAULTED: camera [0-9]+ did not calibrate' /run1318/e.txt; then
  say "OK   BOARD FAULTED still names the camera and the stage" ok
else say "FAIL BOARD FAULTED does not name the camera" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED
