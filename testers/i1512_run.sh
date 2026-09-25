#!/bin/bash
# #1512's harness: the entry-intersection census -- geometric position and score
# against ground truth, side by side with what was published (by path) and with the
# string vote's own reading (#1584), coverage and refusals, overlays, and the inherited
# falsification targets read out by name.
#
#   testers/run_all.sh 1512        build the tree and run this (and the pure check)
#   testers/i1512_run.sh           run it against this checkout
#
# testers/i1512_inside.sh holds what is measured: a control run proving OD_GEO_SCORE
# defaults off (zero I1512 lines, published scores diffed), a censused whole-clip run
# per rig fixture against the ground-truth tables and testers/i1511_annotations/, and
# the phantom/re-report targets from turnaus#1505/#1535 reported with the solver's
# verdict beside each. Four detector replays, i1511's cost apiece.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1512"
if [ -d "$RUN" ]; then
  od_run "1512-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1512 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

od_run "1512" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1512 -w /run1512 \
  "$OD_IMAGE" bash /app/testers/i1512_inside.sh 2>&1 | tee "$RUN/out.txt"
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
echo "RUN=1512 rc=$RC wall_s=$WALL host_busy_pct=$BUSY load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
