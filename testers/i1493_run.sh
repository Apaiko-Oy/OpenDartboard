#!/bin/bash
# #1493's harness: compose the three board planes into camera poses, intersect the rays
# two cameras cast at one dart, and report how close they come to meeting, in millimetres.
#
#   testers/run_all.sh 1493       run this
#   testers/i1493_run.sh          run it against this checkout
#
# It needs NO detector binary and OD_SKIP_BUILD changes nothing about it: it compiles the
# calibration and detection stages directly and replays mocks/rig-20260918 through them,
# exactly as #1490's harness does and from the same code. It CHANGES NOTHING -- #1493 is a
# measurement, `git diff --stat -- src/` is empty on the branch that added it, it asserts
# no threshold and it concludes no architecture.
#
# The script ends on `exit`, never on an `echo`: #1463.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1493"
docker rm -f "$(od_name i1493)" > /dev/null 2>&1
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"

START=$(date +%s)
od_run "i1493" --cpus=2 --network none -v "$OD_TREE_ROOT":/app -v "$RUN":/run1493 \
  "$OD_IMAGE" bash /app/testers/i1493_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
END=$(date +%s)

# #1463 and README rule 5: a timeout with no load reading beside it is not evidence of
# anything, so the load at the END of the run is printed whatever happened.
echo "RUN=1493 rc=$RC seconds=$((END - START)) load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
exit $RC
