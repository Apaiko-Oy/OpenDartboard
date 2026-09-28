#!/bin/bash
# #1487's harness: is a dart whose wedge nobody measured said to be one -- once per run?
#
#   testers/run_all.sh 1487       build the tree and run this
#   testers/i1487_run.sh          run it against this checkout
#
# Sections 1-2 and 4 need NO detector binary: they compile the pure notice and ask it,
# on this tree and on planted copies. Section 3 runs `build/opendartboard` over the whole
# of `mocks/rig-20260918/` three times, so OD_SKIP_BUILD does change what it measures.
#
# The script ends on `exit`, never on an `echo`: #1463.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1487"
docker rm -f "$(od_name i1487)" > /dev/null 2>&1
if [ -d "$RUN" ]; then
  od_run "1487-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1487 > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"

START=$(date +%s)
od_run "i1487" --cpus=2 --network none -v "$OD_TREE_ROOT":/app -v "$RUN":/run1487 \
  "$OD_IMAGE" bash /app/testers/i1487_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
END=$(date +%s)

# #1463 and README rule 5: the load at the END of the run, whatever happened.
echo "RUN=1487 rc=$RC seconds=$((END - START)) load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
exit $RC
