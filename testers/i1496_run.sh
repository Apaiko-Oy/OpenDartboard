#!/bin/bash
# #1496, the MEASUREMENT half. It asserts nothing and fixes nothing: it prints the
# clip-wire census per camera on both rig fixtures and on the shipped mocks, and writes
# the annotated frames a reader is meant to look at.
#
# It is deliberately NOT registered in run_all.sh. A tester that asserts is what the
# repair slice owes; a census that only prints would sit in the gate reporting on the box
# rather than on the tree, which is the thing "Before you merge" is about.
#
#   testers/i1496_run.sh
#
# The shipped mocks are the control and they are load-bearing here: that fixture's
# camera 2 is the ONE camera in this repository whose clip wires anchor it, so "four
# clips" and "one clip" are measured by the same binary on the same day.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/i1496"
mkdir -p "$RUN"
echo "logs: $RUN"

od_run i1496 --cpus=2 --network none \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1496 "$OD_IMAGE" bash -c '
set -u
g++ -std=c++17 -O2 -I /app/src -I /app/src/utils -I /app/src/detector/geometry/calibration \
  -o /run1496/probe /app/testers/i1496_clip_census.cpp \
  /app/src/detector/geometry/calibration/*.cpp \
  $(pkg-config --cflags --libs opencv4) > /run1496/build.log 2>&1 || {
    tail -40 /run1496/build.log; echo "FAIL the census did not build"; exit 2; }

for FIX in rig-20260918 rig-20260922 mocks; do
  if [ "$FIX" = mocks ]; then D=/app/mocks; else D=/app/mocks/$FIX; fi
  mkdir -p "/run1496/$FIX"
  /run1496/probe "/run1496/$FIX" "$D/cam_1.mp4" "$D/cam_2.mp4" "$D/cam_3.mp4" \
    > "/run1496/$FIX.rows.txt" 2> "/run1496/$FIX.log"
  echo "=== $FIX (rc=$?) ==="
  grep "^I1496CAM " "/run1496/$FIX.rows.txt" || echo "  no rows"
done

echo
echo "=== the four candidates, told apart ==========================================="
echo "  A wire is refused if and only if its 200 px outward extension crosses a bright"
echo "  pixel of the number-ring mask. `reason` says which happened to each."
for FIX in rig-20260918 rig-20260922 mocks; do
  echo "--- $FIX"
  awk "/^I1496WIRE/{for(i=1;i<=NF;i++){split(\$i,a,\"=\"); k[a[1]]=a[2]}; print \"  cam \" k[\"cam\"] \" \" k[\"reason\"]}" \
    "/run1496/$FIX.rows.txt" | sed "s/@[0-9]*//" | sort | uniq -c
done

echo
echo "=== the stride control ========================================================"
echo "  spiderSampleCount=50 over 200 px is one sample every 4 px, so a bright feature"
echo "  thinner than that can be STEPPED OVER. The same ray walked at 1 px says whether"
echo "  any admitted clip was admitted by the sampling rather than by the picture."
for FIX in rig-20260918 rig-20260922 mocks; do
  awk -v f="$FIX" "/^I1496WIRE/{delete k; for(i=1;i<=NF;i++){split(\$i,a,\"=\"); k[a[1]]=a[2]};
      c=k[\"cam\"]; if(k[\"isClip\"]==1)s[c]++; if(k[\"denseIsClip\"]==1)d[c]++; if(k[\"steppedOver\"]==1)o[c]++ }
    END{ for(c in s) printf \"  %s cam %s: stride-clips=%d dense-clips=%d steppedOver=%d\n\", f, c, s[c]+0, d[c]+0, o[c]+0 }" \
    "/run1496/$FIX.rows.txt" | sort
done

echo
echo "=== what a reader is handed =================================================="
echo "  /run1496/<fixture>/cam<N>/clips.png      every wire with its verdict drawn on it"
echo "  /run1496/<fixture>/cam<N>/collision.png  the mask the extensions are tested against"
echo "  /run1496/<fixture>/cam<N>/averaged.png   the frame calibration really ran on"
'
rc=$?
echo
echo "i1496: measurement complete (rc=$rc). It asserts nothing; read the frames."
exit $rc
