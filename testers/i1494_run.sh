#!/bin/bash
# #1494 and #1495's harness: the two mechanisms #1492 measured and refused to repair,
# repaired together and measured against the tree that had neither.
#
#   testers/run_all.sh 1494       run this
#   testers/i1494_run.sh          run it against this checkout
#
# It needs NO detector binary and OD_SKIP_BUILD changes nothing about it: it compiles the
# calibration and detection stages directly and replays mocks/rig-20260918 through them,
# to the end of the footage and with no cycle cap -- #1492's harness, with the arms it
# could not have because the repair did not exist.
#
# THE ARMS ARE ONE BINARY. Both repairs carry an OD_* pin in the od_fix shape (#1339,
# #1358, #1492) so "different build" is never a confound:
#
#   OD_ADVANCE_RESET=per-camera   the reference moves inside the per-camera loop, from
#                                 this camera's own candidate -- the tree before #1495
#   OD_TIP_FIGURE=union           the tip is picked from a hull over EVERY admitted
#                                 contour -- the tree before #1494
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1494"
docker rm -f "$(od_name i1494)" > /dev/null 2>&1
if [ -d "$RUN" ]; then
  od_run "i1494-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1494 > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"

START=$(date +%s)
od_run "i1494" --cpus=2 --network none -v "$OD_TREE_ROOT":/app -v "$RUN":/run1494 \
  "$OD_IMAGE" bash /app/testers/i1494_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
END=$(date +%s)

# #1463 and README rule 5: a timeout with no load reading beside it is not evidence of
# anything, so the load at the END of the run is printed whatever happened.
echo "RUN=1494 rc=$RC seconds=$((END - START)) load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
exit $RC
