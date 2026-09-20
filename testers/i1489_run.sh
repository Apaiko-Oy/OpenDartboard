#!/bin/bash
# #1489's harness: is a camera that published a 25 a camera that measured a wedge?
#
#   testers/run_all.sh 1489       build the tree and run this
#   testers/i1489_run.sh          run it against this checkout
#
# Sections 1-3 and 5 need NO detector binary: they compile `score_processing` directly and
# ask the shipped decision itself. Section 4 runs `build/opendartboard` over the whole of
# `mocks/rig-20260918/` three times -- the fixture as this branch scores it, and both ways
# round #1489 with #1485's ring falsifier holding the 25s in place -- so OD_SKIP_BUILD does
# change what it measures, the same way it does for every other detector tester here.
#
# The script ends on `exit`, never on an `echo`: #1463.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1489"
docker rm -f "$(od_name i1489)" > /dev/null 2>&1
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"

START=$(date +%s)
od_run "i1489" --cpus=2 --network none -v "$OD_TREE_ROOT":/app -v "$RUN":/run1489 \
  "$OD_IMAGE" bash /app/testers/i1489_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
END=$(date +%s)

# #1463 and README rule 5: a timeout with no load reading beside it is not evidence of
# anything, so the load at the END of the run is printed whatever happened.
echo "RUN=1489 rc=$RC seconds=$((END - START)) load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
exit $RC
