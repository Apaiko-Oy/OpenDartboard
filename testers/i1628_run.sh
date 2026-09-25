#!/bin/bash
# #1628's fixture half: rig-20260922 dev with #1605's budget and #1618's alignment on,
# replayed twice on one binary -- the default and OD_LONE_WIRE=clear (the refused reselection). What
# is asserted is in i1628_inside.sh. i1555_run.sh's shape: one container, the host's busy
# share and the wall time printed on the RUN line.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1628"
if [ -d "$RUN" ]; then
  od_run "1628-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1628 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"
T0=$(date +%s)
od_run "1628" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1628 -w /run1628 \
  "$OD_IMAGE" bash /app/testers/i1628_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
echo "RUN=1628 rc=$RC wall_s=$(( $(date +%s) - T0 )) load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
