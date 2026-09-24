#!/bin/bash
# #1510 Phase 2's harness: the paint-containment census -- the fitted board model's
# answer beside the published one, per camera per dart, on both rig fixtures.
#
#   testers/run_all.sh 1510p2      build the tree and run this (and the pure check)
#   testers/i1510p2_run.sh         run it against this checkout
#
# testers/i1510p2_inside.sh holds what is measured: a control run proving
# OD_MODEL_SCORE defaults off (zero I1510P2 lines), then a shadowed whole-clip run per
# rig fixture, censused by i1510p2_census.py against the ground-truth tables. Three
# detector replays, i1499's cost apiece.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1510p2"
if [ -d "$RUN" ]; then
  od_run "1510p2-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1510p2 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

od_run "1510p2" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1510p2 -w /run1510p2 \
  "$OD_IMAGE" bash /app/testers/i1510p2_inside.sh 2>&1 | tee "$RUN/out.txt"
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
echo "RUN=1510p2 rc=$RC wall_s=$WALL host_busy_pct=$BUSY load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
