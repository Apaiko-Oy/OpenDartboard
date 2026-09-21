#!/bin/bash
# #1486's harness: an anchor a camera did not measure itself, and the refusal that keeps a
# derived one honest.
#
#   testers/run_all.sh 1486       build the tree and run this
#   testers/i1486_run.sh          run it against this checkout
#
# Three phases, and testers/i1486_inside.sh holds what each one measures:
#
#   1  the derivation's own decisions, as a compiled check over the pure header. No
#      detector, no footage.
#   2  the same check against five PLANTED mutations of that header -- what each
#      assertion is worth, measured on this box rather than claimed in a commit message.
#   3  the detector on the shipped mocks, twice on ONE binary: ordinary, and under
#      OD_ANCHOR=own, which is the rule every build before #1486 had. The shipped mocks
#      are the fixture for this because they are the only footage in the repository where
#      a camera anchors itself at all -- one star camera and two cameras that cannot --
#      so they are the only place a propagated anchor has anything to propagate FROM.
#      mocks/rig-20260918 anchors NO camera (#1486's own measurement), so what it can say
#      about this slice is that nothing is derived there, which phase 3 also asserts.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1486"
if [ -d "$RUN" ]; then
  od_run "1486-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1486 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

od_run "1486" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1486 -w /run1486 \
  "$OD_IMAGE" bash /app/testers/i1486_inside.sh 2>&1 | tee "$RUN/out.txt"
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
echo "RUN=1486 rc=$RC wall_s=$WALL host_busy_pct=$BUSY load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
