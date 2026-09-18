#!/bin/bash
# #1321's Linux harness: #1276's shape with its own paths. One run, one fresh directory
# reaped from a container because the detector writes debug_frames/ and cache/ as root,
# stdout and stderr separated inside the container, host busy % from /proc/stat, wall
# seconds, the container named for the run and reaped by that name alone, --cpus=2,
# --network none. No credential exists in the run's cfg/, so the client pushes nothing.
#
#   testers/i1321_run.sh [container-bash-script-file]
#
# The default script is the one this issue is about: a board that cannot calibrate, and
# the mock footage beside it as the control.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
SCRIPT="${1:-$OD_TREE_ROOT/testers/phases1321/1321-reason.sh}"
BASE="$OD_RUNS_BASE/1321"
RUN="$BASE/reason"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name "i1321-clean")" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/reason > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

docker run --rm --name "$(od_name "i1321-reason")" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run1321 -v "$RUN/cfg":/root/.config \
  -w /run1321 "$OD_IMAGE" bash /run1321/inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=reason rc=$RC wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
exit $RC
