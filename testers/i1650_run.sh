#!/bin/bash
# #1650's harness: rig-20260918 dev, visit 6. The 2 (v6.3) splashes for ONE cycle on
# camera 3 (0.0136 of its board at f1569), and case COOLDOWN drops a spike on the cycle its
# clock runs out. #1646's hold moves the 7's event six cycles later, which at a whole-clip
# replay's cycle rate puts the cooldown's last cycle on that splash. OD_COOLDOWN_EXPIRY=spike
# (opt-in) starts the event on that cycle instead.
#
#   testers/run_all.sh 1650-cooldown   build the tree and run this
#   testers/i1650_run.sh               run it against this checkout's build/
#
# testers/i1650_inside.sh holds what is measured and asserted: three narrowed replays
# (1620 cycles) on OD_MOTION_CLOCK=capture with OD_COOLDOWN_MS=900, which puts the
# cooldown's last cycle where a ~37 ms wall-clock cycle puts it, on any box at any load.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1650-cooldown"
if [ -d "$RUN" ]; then
  od_run "1650-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1650-cooldown > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

# CRLF-safe copy, i1551_run.sh's reason.
tr -d '\r' < "$OD_TREE_ROOT/testers/i1650_inside.sh" > "$RUN/inside.sh"

T0=$(date +%s)
od_run "1650" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1650 -w /run1650 \
  "$OD_IMAGE" bash /run1650/inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}

echo "RUN=1650 rc=$RC wall_s=$(( $(date +%s) - T0 )) load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
