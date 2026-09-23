#!/bin/bash
# #1535's harness: a camera that re-reports a pixel it already reported this visit is
# not a second witness.
#
#   testers/run_all.sh 1535       build the tree and run this
#   testers/i1535_run.sh          run it against this checkout
#
# testers/i1535_inside.sh holds what is measured -- the pure rule's own census
# (i1535_identity_check.cpp), and rig-20260918's visit 4 through the detector's own
# stages (#1505's edge probe), both rules on one binary via OD_TIP_IDENTITY=off, so
# "different build" is never a confound.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1535"
if [ -d "$RUN" ]; then
  od_run "1535-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1535 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

od_run "1535" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1535 -w /run1535 \
  "$OD_IMAGE" bash /app/testers/i1535_inside.sh 2>&1 | tee "$RUN/out.txt"
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
echo "RUN=1535 rc=$RC wall_s=$WALL host_busy_pct=$BUSY load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
