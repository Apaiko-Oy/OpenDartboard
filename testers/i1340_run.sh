#!/bin/bash
# #1340's Linux harness: #1320's shape with #1335's paths. One run, one fresh directory
# reaped from a container because the detector writes debug_frames/ and cache/ as root,
# the container named for the checkout and reaped by that name alone, --cpus=2,
# --network none. No credential exists in the run's cfg/, so the client pushes nothing.
#
#   testers/run_all.sh 1340        build the tree and run this
#   testers/i1340_run.sh           run it against whatever is in build/
#
# It measures $OD_TREE_ROOT/build/opendartboard and does not build it, which is run_all's
# job. Every number below belongs to a build carrying DEBUG_SEEK_VIDEO, which seeks a file
# source three seconds in: a release binary calibrates on a different frame of the same
# clip and measures a different bull. Two phantom red testers were produced by that
# mistake on 2026-09-19 alone.
#
# EVERY DETECTOR RUN BELOW IS UNDER `timeout`, and that is not belt and braces. A board
# that cannot calibrate goes to #895's fault vigil and STAYS UP on purpose -- it beats
# ERROR at a Station rather than exiting -- so a phase that expects a refusal and does not
# bound it never returns. Measured here on 2026-09-19: an unbounded run of the falsifier
# below sat for nine minutes before it was killed by hand.
#
# What is asserted, and why each half is here:
#
#   1. TEN CONSECUTIVE RUNS of mocks/rig-20260918 as it ships give the same per-camera
#      answer -- the same three bull centres and the same three board spans, ten times.
#      That is #1340's own acceptance criterion and it is the one a smaller constant
#      would also appear to satisfy, which is why 3, 4 and 5 are in the same file.
#
#   2. The shipped fixture does not regress, and its before/after is on one binary.
#
#   3. FALSIFY, on the SAME binary: OD_BOARD=frame puts the pre-#1340 stage back -- disc
#      radius from the enclosed area, boundary centroid, 4%-of-frame floor -- and is
#      held to the six radii and six ratios #1320 recorded before any of this was
#      touched. A falsifier that merely refuses proves nothing; one that reproduces
#      another issue's numbers to the decimal is restoring the old stage.
#
#   4. THE ISSUE ITSELF, on committed footage: a board whose doubles ring is broken over
#      150 degrees. Under the old floor it is refused for having no board in the frame;
#      under #1340 the board is measured, the bull is found where the unbroken fixture
#      finds it, and the camera is then refused by the WIRE stage -- the stage that can
#      see the damage -- instead of by a floor that was measuring colour coverage.
#
#   5. #1320's speck is still refused on size, on this binary, in this run.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
SCRIPT="${1:-$OD_TREE_ROOT/testers/phases1340/1340-floor.sh}"
BASE="$OD_RUNS_BASE/1340"
RUN="$BASE/floor"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name "i1340-clean")" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/floor > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

docker run --rm --name "$(od_name "i1340-floor")" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run1340 -v "$RUN/cfg":/root/.config \
  -w /run1340 "$OD_IMAGE" bash /run1340/inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=floor rc=$RC wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
exit $RC
