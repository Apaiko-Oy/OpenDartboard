#!/bin/bash
# #1797: the scheduled stop on a real replay of mocks/rig-20260918, twice, by hand.
#
# NOT IN run_all.sh, deliberately: it runs the detector unbounded on sixty seconds of
# three-camera footage (the bakeoff's shape, ~2-4 min a run on this box), and the pure
# checks in i1797_check.sh are what gates the rule. This is the proof that the stop goes
# out by the ordinary shutdown and changes nothing when it does not fire:
#
#   control   OD_SCHEDULED_RESTART unset, the container's real clock (UTC; the run says
#             which hour, and it must not be 05:xx or 06:xx for the control to mean
#             anything). The replay runs to the end of the footage: no `I1797 SCHEDULED
#             RESTART at` line, `SCORE:` lines counted, exit code 0.
#   forced    OD_SCHEDULED_CLOCK=<today 05:59:xx UTC as unix seconds>, an INJECTED clock
#             for the rule alone (scorer.cpp, scheduleClock): the schedule starts at that
#             instant and advances with the steady clock, so it meets 06:00 OFFSET
#             seconds after the detector starts -- after scoring begins and before the
#             first dart lands, which is where the control's log puts it. The run must
#             end with, in this order: the `I1797 SCHEDULED RESTART at 06:00` sentence,
#             #1787's log-upload sentence, `TURNAUS: client stopped`; and exit 60.
#
#   testers/i1797_replay.sh [offset-seconds]     default 40
#
# Both runs pass --log-file, because the log-upload sentence is only said about a log
# file, and the file is what the forced run's ordering is read from.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
OFFSET="${1:-40}"
RUN="$OD_RUNS_BASE/i1797"
rm -rf "$RUN" && mkdir -p "$RUN"

if [ ! -x "$OD_TREE_ROOT/build/opendartboard" ]; then
  echo "i1797_replay: no $OD_TREE_ROOT/build/opendartboard -- build this worktree first" >&2
  exit 2
fi

T0=$(date +%s)
od_run "1797-replay" --cpus=2 --network none -e HOME=/root -e OFFSET="$OFFSET" \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1797 -w /run1797 "$OD_IMAGE" bash -c '
  set -u
  BIN=/app/build/opendartboard
  DIR=/app/mocks/rig-20260918
  CAMS="$DIR/cam_1.mp4,$DIR/cam_2.mp4,$DIR/cam_3.mp4"
  FAILED=0
  note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAILED=$((FAILED + 1)); fi; }

  replay() { # replay <name> <env...>
    local name="$1"; shift
    rm -rf /run1797/cache /run1797/debug_frames
    env OD_MAX_CYCLES=0 "$@" timeout 900 $BIN --cams "$CAMS" --width 1280 --height 720 \
        --log-file "/run1797/$name.log" > "/run1797/$name.out" 2>&1
    local rc=$?
    sed "s/\x1b\[[0-9;]*m//g" "/run1797/$name.out" > "/run1797/$name.txt"
    echo "replay $name rc=$rc lines=$(wc -l < /run1797/$name.txt) SCORE_lines=$(grep -c "SCORE:" "/run1797/$name.log")"
    return $rc
  }

  echo "==== control: the real clock, hour $(date -u +%H) UTC, pin unset ===================="
  replay control; RC=$?
  note $([ $RC -eq 0 ] && echo 0 || echo 1) "control: the replay ran to the end of the footage and exited 0 (rc=$RC)"
  grep -q "I1797 SCHEDULED RESTART armed" /run1797/control.log; note $? "control: the schedule was armed (the rule is on by default)"
  ! grep -q "I1797 SCHEDULED RESTART at" /run1797/control.log; note $? "control: and it never stopped the replay"
  ! grep -q "OD_SCHEDULED_RESTART=off" /run1797/control.log; note $? "control: and no pin was said"
  echo "control: I1797 lines:"; grep "I1797" /run1797/control.log | sed "s/^/     | /"
  echo "control: first SCORE line and SCORING STARTS, with the log clock:"
  grep -m1 "SCORING STARTS\|Scorer running" /run1797/control.log | sed "s/^/     | /"
  grep -m1 "SCORE:" /run1797/control.log | sed "s/^/     | /"
  grep -c "SCORE:" /run1797/control.log | sed "s/^/     control SCORE: lines = /"
  tail -3 /run1797/control.log | sed "s/^/     | /"

  echo
  echo "==== forced: OD_SCHEDULED_CLOCK puts the rule at 06:00 ${OFFSET}s after the start ===="
  FORCED=$(( $(date -u -d "$(date -u +%Y-%m-%d) 06:00:00" +%s) - OFFSET ))
  echo "OD_SCHEDULED_CLOCK=$FORCED ($(date -u -d @$FORCED +%H:%M:%S) UTC for the rule; the log stamps stay real)"
  replay forced OD_SCHEDULED_CLOCK="$FORCED"; RC=$?
  note $([ $RC -eq 60 ] && echo 0 || echo 1) "forced: the detector exited 60, scheduled_stop::kExitCode (rc=$RC)"
  grep -q "I1797 SCHEDULED RESTART at 06:00: the launcher looks for an update and starts the board again" /run1797/forced.log
  note $? "forced: the stop sentence is in the log, verbatim"
  STOP=$(grep -n "I1797 SCHEDULED RESTART at" /run1797/forced.log | head -1 | cut -d: -f1)
  UPLOAD=$(grep -n "TURNAUS: log upload:" /run1797/forced.log | head -1 | cut -d: -f1)
  STOPPED=$(grep -n "TURNAUS: client stopped" /run1797/forced.log | head -1 | cut -d: -f1)
  echo "     line numbers: I1797 stop=$STOP  log upload=$UPLOAD  client stopped=$STOPPED"
  [ -n "$STOP" ] && [ -n "$UPLOAD" ] && [ -n "$STOPPED" ] && [ "$STOP" -lt "$UPLOAD" ] && [ "$UPLOAD" -lt "$STOPPED" ]
  note $? "forced: I1797, then the log-upload sentence, then client stopped, in that order"
  grep -q "announcement withdrawn\|announcement not withdrawn\|announcing: no" /run1797/forced.log
  note $? "forced: and the announcement was dealt with on the way out (the ordinary unwind)"
  SCORED_BEFORE=$(sed -n "1,${STOP:-1}p" /run1797/forced.log | grep -c "SCORE:")
  echo "     SCORE: lines before the stop: $SCORED_BEFORE (the guard would have postponed a stop inside 10 min of one)"
  grep -q "Scorer running with" /run1797/forced.log; note $? "forced: the stop came after scoring had started, not during calibration"
  echo "forced: the tail of the log:"
  sed -n "$((STOP > 3 ? STOP - 3 : 1)),\$p" /run1797/forced.log | sed "s/^/     | /"

  echo
  echo "$FAILED failed"
  exit $FAILED
'
RC=$?
echo "WALL $(( $(date +%s) - T0 )) s"
echo "CHECK_RC=$RC"
exit $RC
