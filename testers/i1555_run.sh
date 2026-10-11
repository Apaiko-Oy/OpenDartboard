#!/bin/bash
# #1555's harness: both scoring paths over every ground-truthed fixture in both
# calibration windows, side by side against the truth tables, and the pin exercised on
# the same binary so the losing path stays measurable.
#
#   testers/run_all.sh 1555        build the tree and run this (and the pure check)
#   testers/i1555_run.sh           run it against this checkout
#
# testers/i1555_inside.sh holds what is measured. Seven whole-clip detector replays,
# i1511's cost apiece:
#
#   1. rig-20260918, the registry build's own 3 s calibration seek (#1551: `dev`)
#   2. rig-20260918, OD_SEEK_VIDEO=off -- the clip's opening (`opening`)
#   3. rig-20260922, dev            -- the window that admits two cameras
#   4. rig-20260922, opening        -- the window that admits three (#1551)
#   5. rig-20260918, dev, OD_SCORE_PATH=vote -- the pin, on the same binary
#   6. rig-20260929, dev            -- (#1674) the first fixture no constant was fitted to,
#   7. rig-20260929, opening           NOT annotated, so joined by visit order
#
# The pooled ACCURACY line is over all three fixtures (the 96% target from #1674 on);
# POOLED-r18+r22 is the same pool without rig-20260929, comparable with earlier runs.
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
# census pin OD_MASK_SHIFT_CENSUS, #1656 OD_BULL_SUBPIXEL, #1677 OD_SPIKE_THRESHOLD, #1678
# OD_LONE_CAMERA and its census OD_LONE_CENSUS, #1687 OD_WINDOW_CROP, #1689 OD_BOARD_COUNT,
# #1690 OD_HELD_REBASE, #1691 OD_CLEAN_ADOPT, #1707 OD_RIM_FALLBACK, #1730 OD_WIRE_REGION and
# OD_WIRE_REGION_MARGIN -- the wire stage's region, measured on the twenty-fold model).
# turnaus#1793 added OD_PAST_THREE.
# turnaus#1815 added OD_FLAG_SIGMAS (the geometric flag's threshold, in its own sigmas).
# turnaus#1820 added OD_REREAD_HOLD and its census OD_REREAD_CENSUS.
# turnaus#1821 added OD_RIM_OFFER and its census OD_RIM_OFFER_CENSUS.
# turnaus#1783 added OD_MOTION_REGION, its census OD_MOTION_REGION_CENSUS, and OD_WINDOW_CENSUS
# (#1358's per-window line, which carries the cycle every END closed on).
#
# #1655: OD_MOTION_CLOCK selects the clock the motion timers (cooldown, spike window,
# safety timeout) run on. This harness replays one frame per cycle as fast as the box
# allows, so on `wall` a 1000 ms cooldown spans ~27 frames at load 4-6 and fewer at heavy
# load, and a dart can publish or not with the box's load. On `capture` the timers read the
# footage's own presentation times, which advance 33.333 ms a frame (the rig's 30 fps,
# testers/i1655_fps_probe.py), so the run is the rig at its real frame rate on any box.
# The clock every run used is printed on its ACCURACY lines.
#
# The bakeoff DEFAULTS to `capture` (#1655, measured 2026-09-27: on it the baseline gave
# 79/86 and the hold+departure+spike stack 82/86, each row-identical across two runs at
# different loads (695-946 s wall) and to its wall-clock figure in runs-spread/r1 and
# runs-i1650; turnaus#1655 has the four runs).
# OD_MOTION_CLOCK=wall pins the pre-#1655 instrument; its figures move with the box's load.
export OD_MOTION_CLOCK="${OD_MOTION_CLOCK:-capture}"
#
# #1797: OD_SCHEDULED_RESTART=off keeps the detector from stopping itself at 06:00 in the
# container's zone (UTC in od-amd64:bullseye, 09:00 in Helsinki). The bakeoff runs
# unbounded (OD_MAX_CYCLES=0) for twenty minutes and a gate that crosses six o'clock would
# otherwise measure a replay that stopped at a 10-minute mark; the pin is the only way to
# keep a board from stopping at six (docs/rig.md), so the bakeoff DEFAULTS to it and
# forwards it. OD_SCHEDULED_RESTART=on measures the stop instead, which is i1797_replay.sh's.
export OD_SCHEDULED_RESTART="${OD_SCHEDULED_RESTART:-off}"
FWD=()
for v in OD_MOTION_CLOCK OD_SCHEDULED_RESTART OD_RIM_OFFER OD_RIM_OFFER_CENSUS OD_REREAD_HOLD OD_REREAD_CENSUS OD_WINDOW_UNIT OD_MOTION_REGION OD_MOTION_REGION_CENSUS OD_WINDOW_CENSUS OD_PAST_THREE OD_BODY_WINDOW OD_BODY_CENSUS OD_WINDOW_CROP OD_BOARD_COUNT OD_HELD_REBASE OD_CLEAN_ADOPT OD_RIM_FALLBACK OD_WIRE_REGION OD_WIRE_REGION_MARGIN OD_SPIKE_THRESHOLD OD_LONE_CAMERA OD_LONE_CENSUS OD_SETTLE_EXPOSURE OD_TAKEOUT_REREPORT OD_COOLDOWN_EXPIRY OD_AXIS_UNSHIFT OD_MASK_UNSHIFT OD_MASK_SHIFT_CENSUS OD_BULL_SUBPIXEL OD_SOLVE_CONTROL OD_FLAG_SIGMAS; do
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
