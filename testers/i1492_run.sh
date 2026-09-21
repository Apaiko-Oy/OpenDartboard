#!/bin/bash
# #1492's harness: what did each camera actually find, on the darts where they disagree?
#
#   testers/run_all.sh 1492       run this
#   testers/i1492_run.sh          run it against this checkout
#
# It needs NO detector binary and OD_SKIP_BUILD changes nothing about it: it compiles the
# calibration and detection stages directly and replays mocks/rig-20260918 through them.
# It changes no decision -- #1492's first half is a measurement, and the only thing it
# added to src/ is a census and a PIN (OD_TIP_PIECE_FLOOR) that is not read on an
# ordinary run.
#
# The script ends on `exit`, never on an `echo`: #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1492"
docker rm -f "$(od_name i1492)" > /dev/null 2>&1
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"

START=$(date +%s)
od_run "i1492" --cpus=2 --network none -v "$OD_TREE_ROOT":/app -v "$RUN":/run1492 \
  "$OD_IMAGE" bash /app/testers/i1492_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
END=$(date +%s)

# #1463 and README rule 5: a timeout with no load reading beside it is not evidence of
# anything, so the load at the END of the run is printed whatever happened.
echo "RUN=1492 rc=$RC seconds=$((END - START)) load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
exit $RC
