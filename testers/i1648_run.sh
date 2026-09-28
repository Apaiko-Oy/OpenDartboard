#!/bin/bash
# #1648's harness: rig-20260922's opening window, visits 1-3, under #1646's exposure hold
# (the default since #1662). On that correctly exposed picture the visit-1 takeout cannot
# reconcile CLEAN, because the calibration reference still holds the parked 8, so cameras
# 2 and 3 fall by less than a dart. The takeout re-report rule (the default since #1662;
# OD_TAKEOUT_REREPORT=off is the pin) lets a camera whose only new tip re-reports an
# earlier dart of the visit, and whose cumulative figure fell, vote CLEAN. This measures
# the hold alone (the rule pinned off) and the hold with the rule (the default).
#
#   testers/run_all.sh 1648-takeout    build the tree and run this
#   testers/i1648_run.sh               run it against this checkout's build/
#
# testers/i1648_inside.sh holds what is measured and asserted.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1648"
if [ -d "$RUN" ]; then
  od_run "1648-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1648 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

# CRLF-safe copy, i1551_run.sh's reason.
tr -d '\r' < "$OD_TREE_ROOT/testers/i1648_inside.sh" > "$RUN/inside.sh"

T0=$(date +%s)
od_run "1648" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1648 -w /run1648 \
  "$OD_IMAGE" bash /run1648/inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}

echo "RUN=1648 rc=$RC wall_s=$(( $(date +%s) - T0 )) load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
