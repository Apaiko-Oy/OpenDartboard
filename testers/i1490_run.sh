#!/bin/bash
# #1490's harness: how far apart do two cameras place the same dart, once each is mapped
# through its own board plane?
#
#   testers/run_all.sh 1490       run this
#   testers/i1490_run.sh          run it against this checkout
#
# It needs NO detector binary and OD_SKIP_BUILD changes nothing about it: it compiles the
# calibration and detection stages directly and replays mocks/rig-20260918 through them,
# which is where every number it prints comes from. It CHANGES NOTHING -- #1490 is a
# measurement, and `git diff --stat -- src/` is empty on the branch that added it.
#
# The script ends on `exit`, never on an `echo`: #1463.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1490"
docker rm -f "$(od_name i1490)" > /dev/null 2>&1
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"

START=$(date +%s)
od_run "i1490" --cpus=2 --network none -v "$OD_TREE_ROOT":/app -v "$RUN":/run1490 \
  "$OD_IMAGE" bash /app/testers/i1490_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
END=$(date +%s)

# #1463 and README rule 5: a timeout with no load reading beside it is not evidence of
# anything, so the load at the END of the run is printed whatever happened.
echo "RUN=1490 rc=$RC seconds=$((END - START)) load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
exit $RC
