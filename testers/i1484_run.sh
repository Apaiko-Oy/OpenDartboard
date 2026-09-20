#!/bin/bash
# #1484's harness: a run reports HOW its darts were scored, not only what they scored.
#
#   testers/run_all.sh 1484       build the tree and run this
#   testers/i1484_run.sh          run it against this checkout
#
# It measures build/opendartboard, so it needs one: run_all.sh builds first, and the
# inside script refuses a binary older than /app/src rather than reporting a census of
# some other tree.
#
# Two detector runs -- mocks/rig-20260918 to the end of its footage, the shipped mocks
# under a cycle budget -- and then the census of each. testers/i1484_inside.sh holds what
# is measured and why those two runs stop for different reasons.
#
# It asserts nothing about the numbers the detector produced (#1322). It fails on a run it
# could not READ: no binary, no fixture, a run that never reached the scoring loop, a
# ground-truth table it cannot parse.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1484"
# The detector writes cache/ and debug_frames/ as root, so an old run directory is removed
# from inside a container rather than by this shell.
if [ -d "$RUN" ]; then
  od_run "1484-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1484 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

od_run "1484" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1484 -w /run1484 \
  "$OD_IMAGE" bash /app/testers/i1484_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat
BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

# README rule 5: a quiet box at the start is not a quiet box throughout, and a timeout with
# no load reading beside it is not evidence of anything.
echo "RUN=1484 rc=$RC wall_s=$WALL host_busy_pct=$BUSY load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
