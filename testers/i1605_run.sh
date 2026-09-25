#!/bin/bash
# #1605's harness: a dart standing through camera 1's bull outlasted #1445's look budget,
# so rig-20260922's camera 1 never calibrated in the dev window. The census that names
# the cause, the budget that now outlasts it, and the pin that restores the old one.
#
#   testers/run_all.sh 1605      build the tree and run this
#   testers/i1605_run.sh         run it against this checkout's src/ and build/
#
# testers/i1605_inside.sh holds what is measured and asserted. No whole-clip replay:
# one look census (41 single-frame calibrations of one clip) and eight calibration-only
# detector runs under OD_MAX_CYCLES=1, the shape of i1551_run.sh.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1605"
if [ -d "$RUN" ]; then
  od_run "1605-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1605 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

# CRLF-safe copy, i1551_run.sh's reason.
tr -d '\r' < "$OD_TREE_ROOT/testers/i1605_inside.sh" > "$RUN/inside.sh"

T0=$(date +%s)
od_run "1605" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1605 -w /run1605 \
  "$OD_IMAGE" bash /run1605/inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}

echo "RUN=1605 rc=$RC wall_s=$(( $(date +%s) - T0 )) dir=$RUN"
exit $RC
