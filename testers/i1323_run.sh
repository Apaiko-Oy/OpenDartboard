#!/bin/bash
# #1323's Linux harness: #1321's shape with #1335's paths. One run, one fresh directory
# reaped from a container because the detector writes debug_frames/ and cache/ as root,
# the container named for the checkout and reaped by that name alone, --cpus=2,
# --network none. No credential exists in the run's cfg/, so the client pushes nothing.
#
#   testers/run_all.sh 1323        build the tree and run this
#   testers/i1323_run.sh           run it against whatever is in build/
#
# It measures $OD_TREE_ROOT/build/opendartboard and does not build it, which is run_all's
# job -- and the build matters here more than it does in most of these files. Every number
# below belongs to a build carrying DEBUG_SEEK_VIDEO: it seeks a file source three seconds
# in, so this tester calibrates on frame 45 of the mocks and a release binary calibrates on
# frame 0 of the same clip. They disagree. At frame 0 camera 1's bull reads (616,282)
# rather than (616,283), and rig-20260918's camera 3 fails #1320's 4%-of-the-frame board
# gate outright and never reports a bull at all. That is the same trap #1335 wrote into
# run_all.sh's header and into #1320's §4, met a third time from a third direction.
#
# Five things happen, and the last three are there because a window nothing can fail is
# not evidence:
#
#   1. The two rigs, as they ship. Both calibrate, neither says ERROR or WARN, and all six
#      bull centres are the ones #1320 recorded.
#
#   2. The camera this issue is about: the same mock footage shifted 180 px right and 90
#      px down, so the middle of the frame and the middle of the board are 170 px apart.
#      Its bull must be found, and found at the control's bull plus that shift.
#
#   3. That colour processing says which middle it measured, and that it is the board's.
#
#   4. #1320's speck painted on the same clip: the bull is still found and the speck is
#      still refused on size.
#
#   5. FALSIFY, three arms, each built from a COPY of src/ and never from the worktree:
#      the rule put back on the frame (which is the code before #1323 -- it loses the bull
#      and the camera does not calibrate, and that is the reproduction); the window made
#      to always keep (many more regions reach bull detection, one of them hundreds of
#      pixels off the board); and the window made to always drop (the control loses a
#      bull).
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
BASE="$OD_RUNS_BASE/1323"
RUN="$BASE/offaim"

if [ ! -x "$OD_TREE_ROOT/build/opendartboard" ]; then
  echo "no detector at $OD_TREE_ROOT/build/opendartboard -- run testers/run_all.sh 1323, which builds it"
  exit 2
fi

mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  od_run "i1323-clean" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/offaim > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

od_run "i1323-offaim" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run1323 -v "$RUN/cfg":/root/.config \
  -w /run1323 "$OD_IMAGE" bash /app/testers/i1323_inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=offaim rc=$RC wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
exit $RC
