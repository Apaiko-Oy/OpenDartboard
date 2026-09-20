#!/bin/bash
# #1467's harness: does a twenty-fold model place a board's ring where counting could not?
#
#   testers/run_all.sh 1467       build the tree and run this
#   testers/i1467_run.sh          run it against this checkout
#
# It needs NO build of the detector binary: what it measures is `wire_model`, the wire
# stage that calls it and the calibration above them, and the census compiles those
# sources directly. So OD_SKIP_BUILD makes no difference to it.
#
# Six sections, and section 6 is the one that makes the other five worth reading: it
# plants the defect this issue is about -- a plane built WITHOUT the bull, which is
# #1466's affine unprojection and was measured there as worse than doing nothing --
# rebuilds the census on the planted tree and asserts that section 1 could not have
# passed on it.
#
# The script ends on `exit`, never on an `echo`: #1463.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1467"
docker rm -f "$(od_name i1467)" > /dev/null 2>&1
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"

START=$(date +%s)
od_run "i1467" --cpus=2 --network none -v "$OD_TREE_ROOT":/app -v "$RUN":/run1467 \
  "$OD_IMAGE" bash /app/testers/i1467_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
END=$(date +%s)

# #1463 and README rule 5: a timeout with no load reading beside it is not evidence of
# anything, so the load at the END of the run is printed whatever happened.
echo "RUN=1467 rc=$RC seconds=$((END - START)) load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
exit $RC
