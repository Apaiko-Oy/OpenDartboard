#!/bin/bash
# #1392's Linux harness: #1340's shape with #1335's paths. One run, one fresh directory
# reaped from a container because the detector writes debug_frames/ and cache/ as root,
# the container named for the checkout and reaped by that name alone, --cpus=2,
# --network none. No credential exists in the run's cfg/, so the client pushes nothing.
#
#   testers/run_all.sh 1392        build the tree and run this
#   testers/i1392_run.sh           run it against whatever is in build/
#
# It measures $OD_TREE_ROOT/build/opendartboard and does not build it, which is run_all's
# job. Every number it asserts belongs to a build carrying DEBUG_SEEK_VIDEO, which seeks a
# file source three seconds in: a release binary calibrates on a different frame of the
# same clip and measures a different bull.
#
# EVERY DETECTOR RUN IN THE PHASE IS UNDER `timeout`. A board that cannot calibrate goes
# to #895's fault vigil and STAYS UP on purpose -- it beats ERROR at a Station rather than
# exiting -- so a phase that expects a refusal and does not bound it never returns. #1340's
# agent lost nine minutes to exactly that on 2026-09-19.
#
# It re-encodes seven clips and runs the detector eleven times, so it is one of the slower
# testers in this directory; OD_TESTER_TIMEOUT is what run_all.sh gives it.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
SCRIPT="${1:-$OD_TREE_ROOT/testers/phases1392/1392-annulus.sh}"
BASE="$OD_RUNS_BASE/1392"
RUN="$BASE/annulus"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name "i1392-clean")" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/annulus > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

docker run --rm --name "$(od_name "i1392-annulus")" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run1392 -v "$RUN/cfg":/root/.config \
  -w /run1392 "$OD_IMAGE" bash /run1392/inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=annulus rc=$RC wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
exit $RC
