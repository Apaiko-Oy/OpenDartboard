#!/bin/bash
# #1652's measurement: the published tip's move on the board, per camera, when the
# fresh-diff chain is made translation-free. testers/i1652_tipshift_inside.sh holds what
# is run and reported (four whole-clip replays on OD_MOTION_CLOCK=capture).
#
#   testers/i1652_tipshift_run.sh     against this checkout's build/
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1652-tipshift"
if [ -d "$RUN" ]; then
  od_run "1652-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1652-tipshift > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"
tr -d '\r' < "$OD_TREE_ROOT/testers/i1652_tipshift_inside.sh" > "$RUN/inside.sh"

T0=$(date +%s)
od_run "1652" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1652 -w /run1652 \
  "$OD_IMAGE" bash /run1652/inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}

echo "RUN=1652 rc=$RC wall_s=$(( $(date +%s) - T0 )) load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
