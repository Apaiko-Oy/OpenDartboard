#!/bin/bash
# #1497's harness: crop the board's NUMBER RING out of the averaged calibration frame, on
# both fixtures, and report how big a printed number is in pixels -- with the crops saved
# so a person can judge legibility rather than take a number's word for it.
#
#   testers/run_all.sh 1497       run this
#   testers/i1497_run.sh          run it against this checkout
#
# It needs NO detector binary and OD_SKIP_BUILD changes nothing about it: it compiles the
# calibration stages directly and calibrates on mocks/rig-20260918 and mocks/ exactly as
# #1490's and #1493's harnesses do, from the same code. It CHANGES NOTHING -- #1497 is a
# measurement, `git diff --stat -- src/` is empty on the branch that added it, it asserts
# no threshold, it builds no reader and it concludes nothing (#1498 is the decision and it
# is the maintainer's).
#
# The script ends on `exit`, never on an `echo`: #1463.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1497"
docker rm -f "$(od_name i1497)" > /dev/null 2>&1
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"

START=$(date +%s)
od_run "i1497" --cpus=2 --network none -v "$OD_TREE_ROOT":/app -v "$RUN":/run1497 \
  "$OD_IMAGE" bash /app/testers/i1497_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
END=$(date +%s)

echo "the crops are in $RUN"
# #1463 and README rule 5: a timeout with no load reading beside it is not evidence of
# anything, so the load at the END of the run is printed whatever happened.
echo "RUN=1497 rc=$RC seconds=$((END - START)) load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
exit $RC
