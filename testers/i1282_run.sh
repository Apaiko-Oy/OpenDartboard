#!/bin/bash
# #1282's Linux harness: #1249's shape unchanged (one run, one fresh directory reaped from
# a container because the detector writes cache/ as root, stdout and stderr separated
# inside the container, host busy % from /proc/stat, wall seconds, the container named for
# the run and reaped by that name alone, --cpus=2, --network none), mounting this worktree.
#
# It measures the Linux half of #1282's second criterion -- a clip that runs out ends the
# run instead of spinning -- and the control that gives it meaning: a board made blind on
# the same footage says BOARD SIGHT LOST and says nothing about any footage ending. The
# first criterion is the score socket's and is measured by the tester run_all.sh already
# has, 1188-subscribers; the Windows half of both is testers/i1282_windows.sh, which needs
# a Windows toolchain and is run by hand.
#
#   testers/i1282_run.sh [container-bash-script-file]
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
SCRIPT="${1:-$OD_TREE_ROOT/testers/i1282_inside.sh}"
BASE="$OD_RUNS_BASE/1282"
RUN="$BASE/footage"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name "i1282-clean")" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/footage > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

docker run --rm --name "$(od_name "i1282-footage")" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run1282 -v "$RUN/cfg":/root/.config \
  -w /run1282 "$OD_IMAGE" bash /run1282/inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=footage rc=$RC wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
# #1335: the harness exits on what it measured, and an echo returns 0 whatever it printed.
exit $RC
