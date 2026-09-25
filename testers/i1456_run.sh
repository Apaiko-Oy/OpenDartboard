#!/bin/bash
# #1456's harness: a camera refused on its averaged frame seals the look with the highest
# R of the budget, not the first look that passed.
#
#   testers/run_all.sh 1456-bestlook   build the tree and run this
#   testers/i1456_run.sh               run it against this checkout's src/ and build/
#   OD_I1456_REPS=5 testers/i1456_run.sh
#                                      the decision comment's three arms at 5 runs each
#                                      (the registered row runs 2 of the tree's rule and
#                                      1 of each pin, which is enough to hold the rule)
#
# testers/i1456_inside.sh holds what is measured and asserted. No whole-clip replay:
# calibration-only detector runs under OD_MAX_CYCLES=1, i1605_run.sh's shape.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1456"
if [ -d "$RUN" ]; then
  od_run "1456-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1456 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

# CRLF-safe copy, i1551_run.sh's reason.
tr -d '\r' < "$OD_TREE_ROOT/testers/i1456_inside.sh" > "$RUN/inside.sh"

T0=$(date +%s)
od_run "1456" --cpus=2 --network none -e HOME=/root -e OD_I1456_REPS="${OD_I1456_REPS:-2}" \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1456 -w /run1456 \
  "$OD_IMAGE" bash /run1456/inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}

echo "RUN=1456 rc=$RC wall_s=$(( $(date +%s) - T0 )) dir=$RUN"
exit $RC
