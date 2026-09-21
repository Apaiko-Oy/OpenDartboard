#!/bin/bash
# #1498's harness: does the board's own PRINTED NUMBER RING say where the sequence starts,
# and how strongly -- measured on both fixtures, against the same reader pointed at a ring
# with no numbers in it, and against the one camera in this repository that anchors itself
# from four clip wires.
#
#   testers/run_all.sh 1498       run this
#   testers/i1498_run.sh          run it against this checkout
#
# It needs NO detector binary and OD_SKIP_BUILD changes nothing about it: it compiles the
# calibration stages directly and calibrates on mocks/rig-20260918 and mocks/ exactly as
# #1490's, #1493's and #1497's harnesses do, from the same code.
#
# The script ends on `exit`, never on an `echo`: #1463.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1498"
docker rm -f "$(od_name i1498)" > /dev/null 2>&1
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"

START=$(date +%s)
od_run "i1498" --cpus=2 --network none -v "$OD_TREE_ROOT":/app -v "$RUN":/run1498 \
  "$OD_IMAGE" bash /app/testers/i1498_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
END=$(date +%s)

echo "the cells this census read are in $RUN"
# #1463 and README rule 5: a timeout with no load reading beside it is not evidence of
# anything, so the load at the END of the run is printed whatever happened.
echo "RUN=1498 rc=$RC seconds=$((END - START)) load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
exit $RC
