#!/bin/bash
# #1388's Linux harness: #899's shape with its own paths, because it measures the same
# lifecycle. One run, one fresh directory reaped from a container because the detector
# writes debug_frames/ and cache/ as root, stdout and stderr separated inside the
# container, host busy % from /proc/stat, wall seconds, the container named for the run
# and reaped by that name alone, --cpus=2, --network none. No credential exists in the
# run's cfg/, so the client pushes nothing.
#
#   testers/i1388_run.sh [container-bash-script-file]
#
# The default script is the one this issue is about: the retry budget ADR-0080 says must
# be measured rather than chosen, measured; a rig knocked during one measurement and back
# before the next, recovering on the calibration it held; a rig that has really moved,
# faulting after the budget and recording it where the next start reads it; that next
# start refusing; and a scratch build in which the recovery DOES adopt the fresh geometry,
# to prove the guard that says it must not can be made to fire.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
SCRIPT="${1:-$OD_TREE_ROOT/testers/phases1388/1388-budget.sh}"
BASE="$OD_RUNS_BASE/1388"
RUN="$BASE/budget"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name "i1388-clean")" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/budget > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

# --cpus=4 rather than #899's 2: one phase builds a second binary from a scratch copy of
# the tree, and the four cores are the difference between a tester that takes four minutes
# and one that takes ten. Every figure this run ASSERTS on is a geometry measurement or a
# count of log lines; nothing in it races a clock, so the wider allowance cannot move a
# verdict. The measured per-attempt cost it PRINTS is a timing and is labelled as one.
docker run --rm --name "$(od_name "i1388-budget")" --cpus=4 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run1388 -v "$RUN/cfg":/root/.config \
  -w /run1388 "$OD_IMAGE" bash /run1388/inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=budget rc=$RC wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
exit $RC
