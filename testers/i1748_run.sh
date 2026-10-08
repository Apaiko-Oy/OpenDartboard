#!/bin/bash
# #1748's harness: a doubles span the ring identity calls TREBLE must not build a plane
# 1.589x too large, and a fit whose own fitted ring contradicts its scale is refused.
#
#   testers/run_all.sh 1748       build the tree and run this
#   testers/i1748_run.sh          run it against this checkout
#
# It needs NO build of the detector binary: what it measures is the calibration stack
# (geometry_calibration::calibrateSingleCamera, wire_processing::conicOfDoublesFor and
# board_model::fitBoardToCamera), and the census compiles those sources directly, the
# way i1423_run.sh and i1510_run.sh do. OD_SKIP_BUILD makes no difference to it.
#
# Five sections; 4 and 5 plant the defect out again and assert the earlier sections could
# not have passed on the planted tree (#1412, #1463). i1748_inside.sh holds them.
#
# The script ends on `exit`, never on an `echo`: #1463.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1748"
docker rm -f "$(od_name i1748)" > /dev/null 2>&1
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"

START=$(date +%s)
od_run "i1748" --cpus=2 --network none -v "$OD_TREE_ROOT":/app -v "$RUN":/run1748 \
  "$OD_IMAGE" bash /app/testers/i1748_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
END=$(date +%s)

# #1463 and README rule 5: a timeout with no load reading beside it is not evidence of
# anything, so the load at the END of the run is printed whatever happened.
echo "RUN=1748 rc=$RC seconds=$((END - START)) load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
exit $RC
