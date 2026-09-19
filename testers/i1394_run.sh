#!/bin/bash
# #1394's Linux harness: #1340's shape with #1335's paths. One run, one fresh directory
# reaped from a container because the detector writes debug_frames/ and cache/ as root,
# the container named for the checkout and reaped by that name alone, --cpus=2,
# --network none. No credential exists in the run's cfg/, so the client pushes nothing.
#
#   testers/run_all.sh 1394        build the tree and run this
#   testers/i1394_run.sh           run it against whatever is in build/
#
# It measures $OD_TREE_ROOT/build/opendartboard and does not build it, which is run_all's
# job. Every number below belongs to a build carrying DEBUG_SEEK_VIDEO, which seeks a file
# source three seconds in: a release binary calibrates on a different frame of the same
# clip and measures a different bull.
#
# EVERY DETECTOR RUN BELOW IS UNDER A BOUND, and that is not belt and braces. A board that
# cannot calibrate goes to #895's fault vigil and STAYS UP on purpose -- it beats ERROR at
# a Station rather than exiting -- and OD_MAX_CYCLES does not bound it. Phase 5 hands the
# detector one camera, which can never reach three and therefore always faults, so its
# four runs would never return if they were not ended by their own recorded pid.
#
# What is asserted, and why each half is here:
#
#   1. Both fixtures calibrate 3 of 3 with the six bull centres #1320 recorded and the six
#      fitted boards `1331-framing` section 2.5 pins. This slice changes what the colour
#      stage KEEPS, upstream of the bull and of the ellipse fit, so those twelve numbers
#      are the ones that would move if it had moved anything.
#
#   2. The window sizes per camera, before and after, on ONE binary --
#      OD_COLOUR_WINDOWS=frame restores the four fractions of the frame width.
#
#   3. What the windows really keep and drop, in pixels, from the stage's own census
#      rather than inferred from the ellipse two stages down.
#
#   4. The floor. OD_BOARD_FLOOR=area restores `minBoardAreaPercent`, and rig camera 1 is
#      the camera it refuses: a board plainly there, measured here at 2.45% of the frame
#      and at 194 px of radius one stage later.
#
#   5. The three frame-keyed sites that STAY, counted rather than argued about.
#
#   6. FALSIFY, on the same binary and the same footage: mocks/cam_1.mp4 with the board at
#      half size, built by #1339's own scaler. This is where a window that is a fraction of
#      the frame stops being a fraction of a board, and where the area floor stops seeing a
#      board at all. Every claim above is only honest beside it.
#
# And every switch this tester uses is asserted to MOVE the numbers it is asked about --
# #1340's rule, because a switch that only ever refuses passes any test asking it to refuse.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
SCRIPT="${1:-$OD_TREE_ROOT/testers/phases1394/1394-windows.sh}"
BASE="$OD_RUNS_BASE/1394"
RUN="$BASE/windows"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name "i1394-clean")" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/windows > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

docker run --rm --name "$(od_name "i1394-windows")" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run1394 -v "$RUN/cfg":/root/.config \
  -w /run1394 "$OD_IMAGE" bash /run1394/inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=windows rc=$RC wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
exit $RC
