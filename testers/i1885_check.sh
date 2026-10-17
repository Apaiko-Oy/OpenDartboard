#!/bin/bash
# turnaus#1885: the scheduled stop is armed from the moment the PROGRAM started, so a board
# that is still calibrating at 06:00 stops for the launcher once it has calibrated.
#
# THE FAULT. #1797 armed the schedule in Scorer::run() from the clock at that moment --
# after the constructor had opened the cameras and calibrated, LOOK AGAIN included. On
# device/2 on 2026-10-17 the program started at 05:54:04 and calibrated at 13:41 (the
# machine slept in the middle of an orientation), so it was armed `due at 2026-10-18 06:00`
# and the release offered since the day before was not looked for. scheduled_stop.hpp's
# rule was right all along; the instant scorer.cpp handed it was the wrong one, so only the
# binary can measure this, and that is what this does.
#
# THE MEASUREMENT. The real dev binary on mocks/rig-20260918, bounded by OD_MAX_CYCLES,
# under the test-only OD_SCHEDULED_CLOCK, which since #1885 is the rule's clock AT PROGRAM
# START (scorer.cpp, kProgramStartedAt) and advances with the steady clock from there.
# Calibration on this footage takes seconds, so:
#
#   early    the clock reads 06:00 minus EARLY_S seconds as the program starts, and 06:00
#            passes while it calibrates. Fixed: armed `due at <today> 06:00`, `already
#            past`, then `I1797 SCHEDULED RESTART at 06:00` before any SCORE: line, exit
#            60. The old arming (--mutate-arming): armed from after calibration, so due
#            TOMORROW, no stop, the cycle budget ends the run, exit 0.
#   control  the clock reads 06:00:01 as the program starts -- the launcher's restart
#            after a scheduled stop. Due tomorrow, no stop, exit 0 on the budget: not due
#            twice, under either arming.
#
# The early run also asserts that the arming really came AFTER 06:00 by the rule's clock
# (the log's own stamps are the machine's, so it is read from the steady seconds between
# the session start and the arming line): a run whose calibration took less than EARLY_S
# would pass both armings and measure nothing, and says so instead (INCONCLUSIVE, exit 2).
#
#   --mutate-arming   a COPY of the tree (never this worktree), its build directory with
#                     it, with the arming line put back to `scheduleClock()` and rebuilt
#                     incrementally; the early run must go red. A harness that survives it
#                     is not measuring this slice.
#
# Windows are counted in cycles (tester_paths.sh pins OD_WINDOW_UNIT=cycles), and the
# result does not depend on the hour this is run at: both clocks are injected.
#
#   testers/i1885_check.sh [--mutate-arming]
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
MODE="${1:-}"
EARLY_S="${OD_1885_EARLY_S:-1}"
RUN="$OD_RUNS_BASE/i1885"
TREE="$OD_TREE_ROOT"

if [ ! -s "$OD_TREE_ROOT/build/opendartboard" ]; then  # -s, not -x (Git Bash)
  echo "i1885_check: no $OD_TREE_ROOT/build/opendartboard -- build this worktree first (run_all.sh does)" >&2
  exit 2
fi

# The run directory is written as root from the container, so it is reaped from one.
mkdir -p "$OD_RUNS_BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name i1885-clean)" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/i1885 > /dev/null 2>&1
fi
mkdir -p "$RUN"

if [ "$MODE" = "--mutate-arming" ]; then
  TREE="$RUN/tree"
  mkdir -p "$TREE"
  # The tree but its history and its footage, and the build directory with it, so the
  # rebuild is one object and a link. The footage is mounted from this worktree, read-only.
  tar -C "$OD_TREE_ROOT" --exclude=./.git --exclude=./mocks -cf - . | tar -C "$TREE" -xf - || {
    echo "MUTATION_FAILED: the tree could not be copied"; exit 2; }
  F="$TREE/src/scorer/scorer.cpp"
  LINE='schedule_ = scheduled_stop::Schedule(scheduledRestartIsOn(), kProgramStartedAt);'
  [ "$(grep -cF "$LINE" "$F")" = "1" ] || { echo "MUTATION_FAILED: the arming line is not where the mutation expects it"; exit 2; }
  sed -i "s/Schedule(scheduledRestartIsOn(), kProgramStartedAt);/Schedule(scheduledRestartIsOn(), scheduleClock());/" "$F"
  grep -qF "Schedule(scheduledRestartIsOn(), scheduleClock());" "$F" || { echo "MUTATION_FAILED: the line was not changed"; exit 2; }
  echo "MUTATED: the schedule is armed from the clock after calibration, as before #1885"
  if ! od_run i1885-build --cpus=2 -e HOME=/root -v "$TREE":/app -w /app "$OD_IMAGE" \
      bash -c 'cmake --build /app/build -- -j2 --no-print-directory' > "$RUN/mutated-build.log" 2>&1; then
    tail -20 "$RUN/mutated-build.log"
    echo "MUTATION_FAILED: the mutated tree did not build"
    exit 2
  fi
fi

T0=$(date +%s)
od_run i1885-check --cpus=2 --network none -e HOME=/root -e EARLY_S="$EARLY_S" \
  -v "$TREE":/app -v "$OD_TREE_ROOT/mocks":/footage:ro -v "$RUN":/run1885 -w /run1885 "$OD_IMAGE" bash -c '
  set -u
  BIN=/app/build/opendartboard
  DIR=/footage/rig-20260918
  CAMS="$DIR/cam_1.mp4,$DIR/cam_2.mp4,$DIR/cam_3.mp4"
  SIX=$(date -d "today 06:00" +%s)
  TODAY=$(date -d @$SIX +%Y-%m-%d)
  TOMORROW=$(date -d "@$((SIX + 86400 + 3600))" +%Y-%m-%d)
  FAILED=0
  note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAILED=$((FAILED + 1)); fi; }

  replay() { # replay <name> <clock at program start>
    rm -rf /run1885/cache /run1885/debug_frames
    env OD_MAX_CYCLES=150 OD_SCHEDULED_CLOCK="$2" timeout 600 $BIN --cams "$CAMS" --width 1280 --height 720 \
        --log-file "/run1885/$1.log" > "/run1885/$1.out" 2>&1
    local rc=$?
    echo "replay $1 clock=$(date -d @$2 +%H:%M:%S) rc=$rc SCORE_lines=$(grep -c "SCORE:" "/run1885/$1.log")"
    grep "I1797\|Session Started\|CYCLE BUDGET REACHED\|Scorer running" "/run1885/$1.log" | sed "s/^/     | /"
    return $rc
  }
  stamp() { # stamp <file> <pattern>: seconds of the day of the first matching log line
    grep -m1 "$2" "$1" | sed -n "s/^\[\([0-9][0-9]\):\([0-9][0-9]\):\([0-9][0-9]\)\..*/\1 \2 \3/p" |
      awk "{ print \$1 * 3600 + \$2 * 60 + \$3 }"
  }

  echo "==== early: the rule reads 06:00 minus ${EARLY_S}s as the program starts ($(date +%Z)) ===="
  replay early $((SIX - EARLY_S)); RC=$?
  L=/run1885/early.log
  FIRST=$(stamp "$L" "^\[")
  ARMED=$(stamp "$L" "I1797 SCHEDULED RESTART armed")
  if [ -z "$FIRST" ] || [ -z "$ARMED" ]; then
    echo "INCONCLUSIVE: the early run did not say when it started and when it armed"; exit 2
  fi
  CAL=$((ARMED - FIRST))
  echo "early: armed ${CAL}s after the first line of the session (calibration), the rule clock moved past 06:00 at ${EARLY_S}s"
  if [ "$CAL" -le "$EARLY_S" ]; then
    echo "INCONCLUSIVE: calibration took ${CAL}s, not more than ${EARLY_S}s, so 06:00 had not passed when the schedule was armed and this run cannot tell the two armings apart"
    exit 2
  fi
  grep -q "I1797 SCHEDULED RESTART armed: due at $TODAY 06:00 local, the first 06:00 after this program started at $TODAY 05:59" "$L"
  note $? "early: armed due at $TODAY 06:00 -- from the program start at 05:59, not from the arming after calibration"
  grep -q "already past, because calibrating took this start beyond it" "$L"
  note $? "early: and the arming line says the stop is already past"
  grep -q "I1797 SCHEDULED RESTART at 06:00" "$L"
  note $? "early: the board stopped at its first poll after calibrating"
  STOP_LINE=$(grep -n -m1 "I1797 SCHEDULED RESTART at" "$L" | cut -d: -f1)
  FIRST_SCORE=$(grep -n -m1 "SCORE:" "$L" | cut -d: -f1)
  [ -n "$STOP_LINE" ] && { [ -z "$FIRST_SCORE" ] || [ "$STOP_LINE" -lt "$FIRST_SCORE" ]; }
  note $? "early: before any SCORE: line -- a board that has only calibrated holds nothing the guard reads"
  ! grep -q "postponed" "$L"
  note $? "early: and the guard did not postpone it"
  ! grep -q "CYCLE BUDGET REACHED" "$L"
  note $? "early: the run ended on the schedule, not on the cycle budget"
  [ $RC -eq 60 ]
  note $? "early: exit 60, the scheduled stop (rc=$RC)"

  echo "==== control: the rule reads 06:00:01 as the program starts (the launcher restart) ===="
  replay control $((SIX + 1)); RC=$?
  L=/run1885/control.log
  grep -q "I1797 SCHEDULED RESTART armed: due at $TOMORROW 06:00 local" "$L"
  note $? "control: armed due at $TOMORROW 06:00 -- a start at 06:00:01 waits a day"
  ! grep -q "already past" "$L"
  note $? "control: and is not said to be past"
  ! grep -q "I1797 SCHEDULED RESTART at" "$L"
  note $? "control: no stop: the board the launcher starts again is not due twice"
  grep -q "CYCLE BUDGET REACHED" "$L"
  note $? "control: the run ended on its cycle budget"
  [ $RC -eq 0 ]
  note $? "control: exit 0 (rc=$RC)"

  echo "FAILED=$FAILED"
  [ $FAILED -eq 0 ]
'
RC=$?
T1=$(date +%s)
echo "WALL $((T1 - T0)) s"
echo "CHECK_RC=$RC"
if [ "$RC" -eq 2 ]; then
  exit 2   # inconclusive is never a pass and never a caught mutation
fi
if [ -n "$MODE" ]; then
  if [ "$RC" -eq 0 ]; then
    echo "MUTATION NOT CAUGHT: the harness passed under $MODE."
    exit 1
  fi
  echo "MUTATION CAUGHT: the harness goes red under $MODE."
  exit 0
fi
# run_all.sh reads the exit code and nothing else (#1335).
exit $RC
