#!/bin/bash
# #1618's harness: a correctly calibrated third camera made rig-20260922 dev worse under
# #1605's OD_LOOK_BUDGET=1605, and the cause is the dev replay's staggered seek. The
# census that shows the echo windows, and the pin (OD_SEEK_ALIGN=1618) that removes them.
#
#   testers/run_all.sh 1618      build the tree and run this
#   testers/i1618_run.sh         run it against this checkout's src/ and build/
#
# testers/i1618_inside.sh holds what is measured and asserted: three whole-clip
# rig-20260922 dev replays (i1555's shape and cost apiece) and one calibration-only run.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1618"
if [ -d "$RUN" ]; then
  od_run "1618-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1618 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

# CRLF-safe copy, i1551_run.sh's reason.
tr -d '\r' < "$OD_TREE_ROOT/testers/i1618_inside.sh" > "$RUN/inside.sh"

T0=$(date +%s)
od_run "1618" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1618 -w /run1618 \
  "$OD_IMAGE" bash /run1618/inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}

echo "RUN=1618 rc=$RC wall_s=$(( $(date +%s) - T0 )) load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
