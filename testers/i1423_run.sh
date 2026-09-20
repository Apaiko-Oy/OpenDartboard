#!/bin/bash
# #1423's harness: which ring did the colour stage measure?
#
#   testers/run_all.sh 1423       build the tree and run this
#   testers/i1423_run.sh          run it against this checkout
#
# It needs NO build of the detector binary: what it measures is
# `ring_identity::identify` and the two stages above it, and the census compiles those
# calibration sources directly. So it is cheap, and OD_SKIP_BUILD makes no difference
# to it.
#
# Six sections, and section 6 is the one that makes the other five worth reading: it
# plants the defect this issue is about -- a search that never looks outside the measured
# span -- rebuilds the census on the planted tree and asserts that section 1 could not
# have passed on it. A tester that cannot fail is the thing this repository keeps buying.
#
# The script ends on `exit`, never on an `echo`: #1463.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1423"
docker rm -f "$(od_name i1423)" > /dev/null 2>&1
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"

START=$(date +%s)
od_run "i1423" --cpus=2 --network none -v "$OD_TREE_ROOT":/app -v "$RUN":/run1423 \
  "$OD_IMAGE" bash /app/testers/i1423_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
END=$(date +%s)

# #1463 and README rule 5: a timeout with no load reading beside it is not evidence of
# anything, so the load at the END of the run is printed whatever happened.
echo "RUN=1423 rc=$RC seconds=$((END - START)) load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
exit $RC
