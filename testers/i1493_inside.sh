#!/bin/bash
# #1493, inside the container. Everything here runs against /app, which is the tree, and
# writes to /run1493.
#
# It never ends on an `echo` (#1463): the last statement is `exit $FAILED`.
set -u

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

CENSUS=/run1493/census
SRC=/app

echo "=== the control: a rig built from known poses, before the real one is read ======"
# #708's rule one domain over: the needle is planted and proved present before an absence
# is a finding. The pose arithmetic can be wrong in ways that read exactly like "the rays
# do not meet", so it is first asked about three cameras whose poses are known by
# construction.
python3 "$SRC/testers/i1493_control.py" > /run1493/control_rows.txt 2>/run1493/control.err
python3 "$SRC/testers/i1493_poses.py" /run1493/control_rows.txt > /run1493/control.txt 2>&1
CTL_RC=$?
grep -E '^      camera [0-9]  f=|^      centre ' /run1493/control.txt
echo "  the six pairs the control planted:"
sed -n '/dart  pair/,/^$/p' /run1493/control.txt | sed -n '3,$p' | sed 's/^/  /'
# 450 mm at 35 degrees, recovered; six pairs meeting to within a tenth of a millimetre;
# and the 20 mm proud point found 20 mm proud rather than mapped onto the plane.
CTL_CENTRES=$(grep -c '|C| = 450\.0 mm' /run1493/control.txt || true)
CTL_RES=$(sed -n '/dart  pair/,/^$/p' /run1493/control.txt | sed -n '3,$p' | awk '{print $3}' | grep -c '^0\.0$' || true)
CTL_HEIGHT=$(sed -n '/dart  pair/,/^$/p' /run1493/control.txt | sed -n '3,$p' | awk '{print $6}' | grep -c '^+20\.0$' || true)
if [ "$CTL_RC" -eq 0 ] && [ "$CTL_CENTRES" -eq 3 ] && [ "$CTL_RES" -eq 6 ] && [ "$CTL_HEIGHT" -eq 3 ]; then
  say "OK   three centres at 450 mm, six pairs meeting to 0.0 mm, and the proud point 20 mm proud" ok
else
  tail -20 /run1493/control.txt
  say "FAIL the pose arithmetic does not recover a rig it was handed the answer to ($CTL_CENTRES centres, $CTL_RES residuals, $CTL_HEIGHT heights, rc=$CTL_RC)" no
fi

echo
echo "=== building the pose census ==================================================="
g++ -std=c++17 -O1 -I "$SRC/src" -I "$SRC/src/utils" -I "$SRC/src/detector/geometry/calibration" \
  -o "$CENSUS" "$SRC/testers/i1493_pose_census.cpp" \
  "$SRC"/src/detector/geometry/calibration/*.cpp \
  "$SRC"/src/detector/geometry/detection/score_processing.cpp \
  "$SRC"/src/detector/geometry/detection/dart_processing.cpp \
  "$SRC"/src/detector/geometry/detection/motion_processing.cpp \
  $(pkg-config --cflags --libs opencv4) > "$CENSUS.build.log" 2>&1
if [ $? -ne 0 ]; then
  tail -40 "$CENSUS.build.log"
  echo "FAIL the pose census did not build; nothing below measures anything"
  exit 2
fi

echo
echo "=== replaying the whole of mocks/rig-20260918 through the detector's own stages ==="
# NO CYCLE CAP AND NO FRAME BUDGET: the clip is played to its end, as #1490's harness does.
"$CENSUS" "$SRC/mocks/rig-20260918/cam_1.mp4" "$SRC/mocks/rig-20260918/cam_2.mp4" \
  "$SRC/mocks/rig-20260918/cam_3.mp4" > /run1493/rows.txt 2> /run1493/pipeline.log
RC=$?
grep '^I1493CAM ' /run1493/rows.txt
grep '^I1493END ' /run1493/rows.txt
if [ "$RC" -ne 0 ]; then
  tail -20 /run1493/pipeline.log
  say "FAIL the census exited $RC; no rows to read" no
fi

echo
python3 "$SRC/testers/i1493_poses.py" /run1493/rows.txt
if [ $? -eq 0 ]; then
  say "OK   at least one dart was seen by two cameras with poses, so there are rays to intersect" ok
else
  say "FAIL no dart in the whole clip gave two rays, so this census intersected nothing" no
fi

echo
echo "=== the replay is the detector's own pipeline, and this is its control ========="
# #1490's control, unchanged: three cameras calibrated, a board plane on at least two, and
# darts for the replay to have scored.
CAMS=$(grep -c '^I1493CAM ' /run1493/rows.txt || true)
PLANES=$(grep '^I1493CAM ' /run1493/rows.txt | grep -c 'planeBuilt=1' || true)
DARTS=$(grep '^I1493END ' /run1493/rows.txt | sed 's/.*darts=//')
echo "  $CAMS cameras calibrated, $PLANES of them with a board plane, $DARTS darts scored by the replay"
if [ "$CAMS" -eq 3 ] && [ "$PLANES" -ge 2 ] && [ "${DARTS:-0}" -gt 0 ]; then
  say "OK   three cameras, a plane on at least two of them, and darts to read" ok
else
  say "FAIL the replay did not reproduce a three-camera board with darts on it" no
fi

echo
if [ "$FAILED" = 0 ]; then echo "i1493: PASS"; else echo "i1493: FAIL"; fi
exit $FAILED
