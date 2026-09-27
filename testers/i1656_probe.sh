#!/bin/bash
# #1656: look at one bull. Builds testers/i1656_bull_probe.cpp against this tree and runs
# it on one clip, window and look, keeping the calibration's debug frames.
#
#   testers/i1656_probe.sh <fixture> <cam 1-3> <open|dev> <avg|k>
#
# Output lands in $OD_RUNS_BASE/1656-probe/<fixture>.cam<c>.<win>.<look>/.
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
FX="$1"; C="$2"; WIN="$3"; LOOK="$4"
OUT="$OD_RUNS_BASE/1656-probe/$FX.cam$C.$WIN.$LOOK"
mkdir -p "$OD_RUNS_BASE/1656-probe" "$OUT"
od_run "1656-probe" --cpus=2 --network none -e HOME=/root -e OD_BULL_SUBPIXEL="${OD_BULL_SUBPIXEL:-}" \
  -v "$OD_TREE_ROOT":/app -v "$OD_RUNS_BASE/1656-probe":/probe -w "/probe/$(basename "$OUT")" \
  "$OD_IMAGE" bash -c '
    B=/probe/bull_probe
    if [ ! -x $B ] || [ -n "$(find /app/src/detector/geometry/calibration /app/testers/i1656_bull_probe.cpp -newer $B 2>/dev/null | head -1)" ]; then
      g++ -std=c++17 -O1 -I /app/src -I /app/src/utils -I /app/src/detector/geometry/calibration \
        -o $B /app/testers/i1656_bull_probe.cpp /app/src/detector/geometry/calibration/*.cpp \
        $(pkg-config --cflags --libs opencv4) > /probe/build.log 2>&1 || { echo FAIL build; tail -20 /probe/build.log; exit 2; }
    fi
    $B /app/mocks/'"$FX"'/cam_'"$C"'.mp4 '"$((C - 1))"' '"$WIN"' '"$LOOK"' 5 30 2>/dev/null | grep "^I1656PROBE"
    exit 0'
RC=$?
echo "dir=$OUT"
exit $RC
