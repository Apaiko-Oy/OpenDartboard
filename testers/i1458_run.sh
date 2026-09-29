#!/bin/bash
# #1458's harness: every camera says how well its perspective fit fits, and on the shipped
# mocks that figure is no worse than the baseline recorded when it was first said.
#
#   testers/run_all.sh 1458      build the tree and run this
#   testers/i1458_run.sh         run it against this checkout's src/ and build/
#
# testers/i1458_inside.sh holds what is measured and asserted. No whole-clip replay:
# calibration-only detector runs under OD_MAX_CYCLES=1, i1456_run.sh's shape.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1458"
if [ -d "$RUN" ]; then
  od_run "1458-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1458 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

# CRLF-safe copy, i1551_run.sh's reason.
tr -d '\r' < "$OD_TREE_ROOT/testers/i1458_inside.sh" > "$RUN/inside.sh"

T0=$(date +%s)
od_run "1458" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1458 -w /run1458 \
  "$OD_IMAGE" bash /run1458/inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}

echo "RUN=1458 rc=$RC wall_s=$(( $(date +%s) - T0 )) load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
