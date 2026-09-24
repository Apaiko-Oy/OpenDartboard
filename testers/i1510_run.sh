#!/bin/bash
# #1510's fixture half: the one-board fit measured on the evidence fixtures --
# overlays, per-ring residuals and the fit verdict per camera, on the frames
# i1510_inside.sh documents (rig-22's clean frame 270, #1514's finding).
#
#   testers/run_all.sh 1510        build the tree and run this (and the pure checks)
#   testers/i1510_run.sh           run it against this checkout
#
# A PROBE on i1490's terms: it asserts nothing about the numbers and fails only when
# a build or a calibration run fails, so a PASS means "the census ran", and the
# overlays under the run directory are the evidence a reader judges.
#
# Registered by #1512 because it was NOT: #1510 shipped i1510_inside.sh and no line
# in run_all.sh, so the label existed and the suite would never have run it -- a
# tester that cannot be reached is the same as one that cannot fail (#1463,
# 1423-ringidentity's story retold, found by census.sh on the stacked gate).
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1510"
if [ -d "$RUN" ]; then
  od_run "1510-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1510 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

od_run "1510" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1510 -w /run1510 \
  "$OD_IMAGE" bash /app/testers/i1510_inside.sh 2>&1 | tee "$RUN/out.txt"
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
echo "RUN=1510 rc=$RC wall_s=$WALL host_busy_pct=$BUSY load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
