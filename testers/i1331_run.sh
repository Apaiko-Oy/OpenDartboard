#!/bin/bash
# #1331's Linux harness: #1323's shape with #1335's paths. One run, one fresh directory
# reaped from a container because the detector writes debug_frames/ and cache/ as root,
# the container named for the checkout and reaped by that name alone, --cpus=2,
# --network none. No credential exists in the run's cfg/, so the client pushes nothing.
#
#   testers/run_all.sh 1331        build the tree and run this
#   testers/i1331_run.sh           run it against whatever is in build/
#
# It measures $OD_TREE_ROOT/build/opendartboard and does not build it, which is run_all's
# job -- and the build matters here as much as it does anywhere in this directory. Every
# number below belongs to a build carrying DEBUG_SEEK_VIDEO: it seeks a file source three
# seconds in, so this tester calibrates on frame 90 of the mocks and a release binary
# calibrates on frame 0 of the same clip. They disagree, and two phantom red testers were
# produced by that mistake on 2026-09-19 alone.
#
# What is asserted, and why each half is here:
#
#   1. Both rigs, as they ship, calibrate on all three cameras with no ERROR and no WARN,
#      at the six bull centres and the six board measurements they have always given.
#      ADR-0079 moved the first stage of the pipeline; the control is what says it moved
#      without moving anything under it.
#
#   2. The region really is drawn around the board that was found, and the line says so.
#
#   3. A camera whose board is whole but well off the middle of its frame calibrates, and
#      the board it measures is the whole board.
#
#   4. FALSIFY, on the SAME binary: OD_ROI=frame puts the frame-centred ellipse back --
#      80% of the frame, 0.95 wide, 1.1 tall, the four constants ADR-0079 §1 retired --
#      and the same clip's board is then measured at 0.61 of its real size with its ring
#      traced by 60 rays instead of 96, ten above the floor that refuses a camera.
#
#   5. A board the FRAME's own edge cuts does not calibrate, at two shifts: one where the
#      board is plainly half out of shot, and one where the largest coloured region still
#      looks like a tidy whole board 56 px clear of the edge. The second is why the
#      framing question is asked of everything the colour stage kept and not of the
#      winning region alone.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
BASE="$OD_RUNS_BASE/1331"
RUN="$BASE/framing"

if [ ! -x "$OD_TREE_ROOT/build/opendartboard" ]; then
  echo "no detector at $OD_TREE_ROOT/build/opendartboard -- run testers/run_all.sh 1331, which builds it"
  exit 2
fi

mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  od_run "i1331-clean" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/framing > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

od_run "i1331-framing" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run1331 -v "$RUN/cfg":/root/.config \
  -w /run1331 "$OD_IMAGE" bash /app/testers/i1331_inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=framing rc=$RC wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
exit $RC
