#!/bin/bash
# #1554's harness: the cast-shadow subtraction on the rig fixtures -- the axis
# census with the subtraction live and with the OD_AXIS_SHADOW=off pin thrown,
# both fixtures, corrected-matcher censuses side by side.
#
#   testers/run_all.sh 1554        build the tree and run this (and the pure check)
#   testers/i1554_run.sh           run it against this checkout
#
# testers/i1554_inside.sh holds what is measured: per fixture, one run with the
# subtraction at its default (on) and one with the falsification pin thrown, each
# censused by the #1554-corrected i1511_census.py; the pin census (every line
# subtract=0 off, no valid line subtract=0 on) is ASSERTED, the accuracy movement is
# REPORTED because separate replays of stateful detection carry documented run-to-run
# variance. Four detector replays, i1511's cost.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1554"
if [ -d "$RUN" ]; then
  od_run "1554-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1554 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

od_run "1554" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1554 -w /run1554 \
  "$OD_IMAGE" bash /app/testers/i1554_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat
BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

# README rule 5: a quiet box at the start is not a quiet box throughout, and a timeout
# with no load reading beside it is not evidence of anything.
echo "RUN=1554 rc=$RC wall_s=$WALL host_busy_pct=$BUSY load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
