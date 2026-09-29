#!/bin/bash
# #1627's harness: with #1605's look budget and #1618's alignment on (the default since #1631), the
# visit-7 takeout on mocks/rig-20260922 lost its motion event to a camera-3 blip while it
# settled, and v8.1's arrival window then read the takeout and baked the T1 into the empty
# board. The census that shows it, and the pin (OD_SETTLE_SPIKE=discard) that restores it.
#
#   testers/run_all.sh 1627-takeout   build the tree and run this
#   testers/i1627_run.sh              run it against this checkout's src/ and build/
#   OD_SETTLE_SPIKE=discard testers/run_all.sh 1627-takeout
#                                     the mutation proof: the pin forced on every replay,
#                                     the repaired one included, so the row must go red
#                                     (i1627_inside.sh says what it predicts)
#
# testers/i1627_inside.sh holds what is measured and asserted: whole-clip rig-20260922
# dev replays (i1555's shape and cost apiece).
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1627"
if [ -d "$RUN" ]; then
  od_run "1627-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1627 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

# CRLF-safe copy, i1551_run.sh's reason.
tr -d '\r' < "$OD_TREE_ROOT/testers/i1627_inside.sh" > "$RUN/inside.sh"

T0=$(date +%s)
# The one host switch forwarded: OD_SETTLE_SPIKE, for the mutation proof. Each replay's
# environment is otherwise written out in inside.sh, so nothing else on the host reaches it.
od_run "1627" --cpus=2 --network none -e HOME=/root -e OD_SETTLE_SPIKE="${OD_SETTLE_SPIKE:-}" \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1627 -w /run1627 \
  "$OD_IMAGE" bash /run1627/inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}

echo "RUN=1627 rc=$RC wall_s=$(( $(date +%s) - T0 )) load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
