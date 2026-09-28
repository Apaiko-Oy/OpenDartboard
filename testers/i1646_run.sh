#!/bin/bash
# #1646's harness: rig-20260922's opening window, visits 1-3 only. Camera 3's automatic
# exposure is still walking back when a takeout's motion event settles, so the takeout's
# window reads a third of camera 3's board as changed and cannot reconcile; the next
# arrival's window then reads the exposure recovering as a takeout and bakes the arriving
# dart (v2.1's 12, v3.1's 20) into the clean reference. The exposure hold makes the event
# wait for every camera's board level to stop moving; it is the default since #1662
# (OD_SETTLE_EXPOSURE=off is the pin), so this measures the pinned settle AND the hold,
# both with #1648's rule pinned off (OD_TAKEOUT_REREPORT=off) to keep the hold alone.
#
#   testers/run_all.sh 1646-exposure   build the tree and run this
#   testers/i1646_run.sh               run it against this checkout's build/
#
# testers/i1646_inside.sh holds what is measured and asserted: two narrowed replays of
# the opening window (OD_SEEK_VIDEO=off, 780 cycles, about two minutes apiece).
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1646"
if [ -d "$RUN" ]; then
  od_run "1646-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1646 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

# CRLF-safe copy, i1551_run.sh's reason.
tr -d '\r' < "$OD_TREE_ROOT/testers/i1646_inside.sh" > "$RUN/inside.sh"

T0=$(date +%s)
od_run "1646" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1646 -w /run1646 \
  "$OD_IMAGE" bash /run1646/inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}

echo "RUN=1646 rc=$RC wall_s=$(( $(date +%s) - T0 )) load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
