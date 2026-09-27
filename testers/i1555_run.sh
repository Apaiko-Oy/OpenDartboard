#!/bin/bash
# #1555's harness: both scoring paths over both ground-truthed fixtures in both
# calibration windows, side by side against the truth tables, and the pin exercised on
# the same binary so the losing path stays measurable.
#
#   testers/run_all.sh 1555        build the tree and run this (and the pure check)
#   testers/i1555_run.sh           run it against this checkout
#
# testers/i1555_inside.sh holds what is measured. Five whole-clip detector replays,
# i1511's cost apiece:
#
#   1. rig-20260918, the registry build's own 3 s calibration seek (#1551: `dev`)
#   2. rig-20260918, OD_SEEK_VIDEO=off -- the clip's opening (`opening`)
#   3. rig-20260922, dev            -- the window that admits two cameras
#   4. rig-20260922, opening        -- the window that admits three (#1551)
#   5. rig-20260918, dev, OD_SCORE_PATH=vote -- the pin, on the same binary
#
# The two windows are NEVER pooled blind and never compared against
# od-baselines/5bc3b0a, which is a third window again (a release build's).
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1555"
if [ -d "$RUN" ]; then
  od_run "1555-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1555 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

# #1648: docker run does not inherit the host's environment, so an opt-in switch set on
# the command line (`OD_SETTLE_EXPOSURE=hold testers/run_all.sh 1555-bakeoff`) never
# reached the detector and the run silently measured the default. `-e VAR` with no value
# forwards the host's VAR when it is set and nothing when it is not, so the default run is
# unchanged. Only the opt-in switches the bakeoff is asked to measure are named here
# (#1650 added OD_COOLDOWN_EXPIRY, #1649 OD_AXIS_UNSHIFT, #1652 OD_MASK_UNSHIFT and its
# census pin OD_MASK_SHIFT_CENSUS).
FWD=()
for v in OD_SETTLE_EXPOSURE OD_TAKEOUT_REREPORT OD_COOLDOWN_EXPIRY OD_AXIS_UNSHIFT OD_MASK_UNSHIFT OD_MASK_SHIFT_CENSUS; do
  if [ -n "${!v+x}" ]; then FWD+=(-e "$v"); echo "I1555 FORWARD $v=${!v}"; fi
done

od_run "1555" --cpus=2 --network none -e HOME=/root "${FWD[@]}" \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1555 -w /run1555 \
  "$OD_IMAGE" bash /app/testers/i1555_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat
BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

# README rule 5: a quiet box at the start is not a quiet box throughout, and a timeout
# with no load reading beside it is not evidence of anything.
echo "RUN=1555 rc=$RC wall_s=$WALL host_busy_pct=$BUSY load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
