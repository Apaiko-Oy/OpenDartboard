#!/bin/bash
# #1490, inside the container. Everything here runs against /app, which is the tree, and
# writes to /run1490.
#
# It never ends on an `echo` (#1463): the last statement is `exit $FAILED`.
set -u

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

CENSUS=/run1490/census
SRC=/app

echo "=== building the spread census ==="
g++ -std=c++17 -O1 -I "$SRC/src" -I "$SRC/src/utils" -I "$SRC/src/detector/geometry/calibration" \
  -o "$CENSUS" "$SRC/testers/i1490_spread_census.cpp" \
  "$SRC"/src/detector/geometry/calibration/*.cpp \
  "$SRC"/src/detector/geometry/detection/score_processing.cpp \
  "$SRC"/src/detector/geometry/detection/dart_processing.cpp \
  "$SRC"/src/detector/geometry/detection/motion_processing.cpp \
  $(pkg-config --cflags --libs opencv4) > "$CENSUS.build.log" 2>&1
if [ $? -ne 0 ]; then
  tail -40 "$CENSUS.build.log"
  echo "FAIL the spread census did not build; nothing below measures anything"
  exit 2
fi

echo
echo "=== replaying the whole of mocks/rig-20260918 through the detector's own stages ==="
# NO CYCLE CAP AND NO FRAME BUDGET: the clip is played to its end. An earlier run of a
# different harness stopped at 1200 cycles, reached four of seven visits, and had its
# figures quoted as the whole clip's.
"$CENSUS" "$SRC/mocks/rig-20260918/cam_1.mp4" "$SRC/mocks/rig-20260918/cam_2.mp4" \
  "$SRC/mocks/rig-20260918/cam_3.mp4" > /run1490/rows.txt 2> /run1490/pipeline.log
RC=$?
grep '^I1490CAM ' /run1490/rows.txt
grep '^I1490END ' /run1490/rows.txt
if [ "$RC" -ne 0 ]; then
  tail -20 /run1490/pipeline.log
  say "FAIL the census exited $RC; no rows to read" no
fi

echo
echo "=== how far apart two cameras place one dart, in millimetres ==================="
python3 "$SRC/testers/i1490_spread.py" /run1490/rows.txt
if [ $? -eq 0 ]; then
  say "OK   darts were placed on the board by two or more cameras, so there is a spread to read" ok
else
  say "FAIL no dart in the whole clip was placed on the board by two cameras, so this census compared nothing" no
fi

echo
echo "=== the replay is the detector's own pipeline, and this is the control ========="
# The census is not the shipped binary, so what can be checked without one is that it saw
# the footage the detector sees: three cameras calibrated, a board plane on each, and the
# dart count the detector's own SCORE lines report over the whole of this clip -- 19,
# recorded in ADR-0084 and measured again by #1485's section 5.
CAMS=$(grep -c '^I1490CAM ' /run1490/rows.txt || true)
PLANES=$(grep '^I1490CAM ' /run1490/rows.txt | grep -c 'planeBuilt=1' || true)
DARTS=$(grep '^I1490END ' /run1490/rows.txt | sed 's/.*darts=//')
echo "  $CAMS cameras calibrated, $PLANES of them with a board plane, $DARTS darts scored by the replay"
if [ "$CAMS" -eq 3 ] && [ "$PLANES" -ge 2 ] && [ "${DARTS:-0}" -gt 0 ]; then
  say "OK   three cameras, a plane on at least two of them, and darts to read" ok
else
  say "FAIL the replay did not reproduce a three-camera board with darts on it" no
fi

echo
if [ "$FAILED" = 0 ]; then echo "i1490: PASS"; else echo "i1490: FAIL"; fi
exit $FAILED
